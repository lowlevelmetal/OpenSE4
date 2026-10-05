#pragma once

// The audio mix behind client/audio.hpp, without an audio device (tested
// directly): effects decoded to the mix's format, music decoded on its own
// thread into a ring of frames, and the mixer that the device's audio thread
// pulls from. The mix is interleaved stereo float at one rate (the device's).
//
// Nothing here starts or stops a sound abruptly: an effect cut off by the next
// one fades out over a few milliseconds, every clip starts and ends with a
// short ramp, a track change fades the old track out before the new one
// starts, volume changes ramp, music that runs dry (its decoder fell
// behind) fades out and back in instead of breaking off, and muting fades the
// whole mix out and back in.

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace opense4::client::audiomix {

constexpr int kChannels = 2;

// Reads a whole file (the path as the platform spells it, wide on Windows):
// empty on success, else why not.
std::string readFileBytes(const std::filesystem::path& file, std::vector<uint8_t>& out);

// A sound effect ready to mix: interleaved stereo float at the mix rate.
struct Clip {
    std::vector<float> samples;
    size_t frames() const { return samples.size() / kChannels; }
};

// What a sound or music file holds, by its first bytes (not its name).
enum class AudioFormat { Unknown, Wav, Ogg, Mp3 };
AudioFormat audioFormat(std::span<const uint8_t> head);

// Decodes a WAV file (any PCM or float format SDL reads, at any rate) to the
// mix rate. On failure, nullopt and why in `error`.
std::optional<Clip> decodeWav(std::span<const uint8_t> bytes, int mixRate, std::string& error);
// Decodes an OGG Vorbis file (any rate, mono or stereo) to the mix rate.
std::optional<Clip> decodeOgg(std::span<const uint8_t> bytes, int mixRate, std::string& error);
// A sound effect in either format, by its bytes (docs/sdk/packages-and-data.md "Sounds and music").
std::optional<Clip> decodeSound(std::span<const uint8_t> bytes, int mixRate, std::string& error);

// What a music track holds, read from its first frames.
struct TrackInfo {
    int sampleRate = 0;
    int channels = 0;
    double seconds = 0;  // 0 when unknown
    AudioFormat format = AudioFormat::Unknown;
};
using Mp3Info = TrackInfo;

// Opens an MP3 in memory: its format, or nullopt and why in `error`.
std::optional<TrackInfo> probeMp3(std::span<const uint8_t> bytes, std::string& error);
// Opens an OGG Vorbis file in memory.
std::optional<TrackInfo> probeOgg(std::span<const uint8_t> bytes, std::string& error);
// A music track in either format, by its bytes.
std::optional<TrackInfo> probeTrack(std::span<const uint8_t> bytes, std::string& error);

// Single producer (a decoder thread), single consumer (the audio thread) queue
// of stereo frames. Lock-free.
class FrameRing {
public:
    explicit FrameRing(size_t capacityFrames);
    size_t capacity() const { return capacity_; }
    size_t available() const;  // frames ready to read
    size_t space() const;      // frames that can be written
    size_t write(const float* frames, size_t count);  // producer; returns frames written
    size_t read(float* out, size_t count);            // consumer; returns frames read

private:
    std::vector<float> data_;
    size_t capacity_;
    std::atomic<size_t> readPos_{0}, writePos_{0};  // in frames, ever increasing
};

// A music track: an MP3 or OGG Vorbis file decoded on its own thread,
// converted to the mix rate and looped without a gap, into a FrameRing the
// mixer reads.
class MusicTrack {
public:
    enum class State { Opening, Playing, Failed };

    // Reads the file on the decoding thread.
    MusicTrack(std::filesystem::path file, int mixRate, double bufferSeconds = 1.0);
    // From a file already in memory (tests).
    MusicTrack(std::vector<uint8_t> bytes, int mixRate, double bufferSeconds = 1.0);
    ~MusicTrack();  // stops the thread; the ring lives on while the mixer holds it
    MusicTrack(const MusicTrack&) = delete;
    MusicTrack& operator=(const MusicTrack&) = delete;

    State state() const { return state_.load(std::memory_order_acquire); }
    // Valid once the state is Playing (the format) or Failed (why).
    const TrackInfo& info() const { return info_; }
    const std::string& error() const { return error_; }
    uint64_t loops() const { return loops_.load(std::memory_order_relaxed); }
    const std::shared_ptr<FrameRing>& ring() const { return ring_; }

private:
    void start();
    void run();
    // Decodes the whole file again and again into the ring until stopped:
    // `read` gives up to `frames` 16-bit frames (0 at the end), `rewind` goes back to the start.
    template <class Read, class Rewind>
    void pump(int channels, int rate, Read read, Rewind rewind);

    std::filesystem::path file_;
    std::vector<uint8_t> bytes_;
    int mixRate_;
    std::shared_ptr<FrameRing> ring_;
    std::atomic<State> state_{State::Opening};
    TrackInfo info_;
    std::string error_;
    std::atomic<uint64_t> loops_{0};
    std::mutex mutex_;
    std::condition_variable wake_;
    bool stop_ = false;
    std::thread thread_;
};

// The mix. Not thread-safe: the owner serializes calls with the audio thread
// (Audio holds the output stream's lock around them).
class Mixer {
public:
    explicit Mixer(int rate);
    int rate() const { return rate_; }

    // Effects: one at a time. A new one cuts off the one playing (docs/spec/06
    // §5.5), which fades out over kCutSeconds meanwhile.
    void playEffect(std::shared_ptr<const Clip> clip);
    void stopEffects();
    bool effectPlaying() const { return effect_.clip != nullptr; }
    void setEffectsGain(float gain);

    // Music: the ring to play from (nullptr: none). What plays fades out first;
    // the new ring starts once it holds kPrimeSeconds of music.
    void setMusic(std::shared_ptr<FrameRing> ring);
    bool musicSounding() const { return music_ != nullptr && level_ > 0.0f; }
    void setMusicGain(float gain);

    // Muting (the game's window in the background, docs/SETUP.md "Sound and
    // music"): the whole mix fades out over kMuteSeconds. Once it is silent the
    // music pauses where it is (its ring is not read, so its decoder waits on
    // the full ring, and nothing runs dry) and the effects are let go. Effects
    // asked for while muted are dropped. A track change or stop while silent
    // takes place at once, unheard. Unmuted, the music goes on from the frame
    // where it paused and the mix fades back in over kMuteSeconds.
    void setMuted(bool muted) { muted_ = muted; }
    bool muted() const { return muted_; }
    bool silent() const { return muted_ && master_ <= 0.0f; }  // muted and faded out

    // Fills `frames` interleaved stereo frames.
    void mix(float* out, size_t frames);

    // Times the music ran dry while playing (its decoder fell behind).
    uint64_t underruns() const { return underruns_; }
    // Rings the mixer has let go of, to free off the audio thread.
    std::vector<std::shared_ptr<FrameRing>> takeRetired();

    static constexpr float kEdgeSeconds = 0.002f;    // the ramp at each end of a clip
    static constexpr float kCutSeconds = 0.006f;     // an effect cut off by the next one
    static constexpr float kTrackFadeSeconds = 0.25f;  // music fading out for a change or stop
    static constexpr float kResumeSeconds = 0.02f;   // music fading in (start, after running dry)
    static constexpr float kRampSeconds = 0.03f;     // a volume change
    static constexpr float kPrimeSeconds = 0.1f;     // music buffered before it starts
    static constexpr float kReserveSeconds = 0.01f;  // music kept back to fade out on if it runs dry
    static constexpr float kMuteSeconds = 0.25f;     // the whole mix fading out when muted, and back in

private:
    struct Voice {
        std::shared_ptr<const Clip> clip;
        size_t pos = 0;
        float level = 1.0f;  // fades out when cut off
    };
    void mixEffects(float* out, size_t frames);
    void mixMusic(float* out, size_t frames);
    bool addVoice(Voice& v, float* out, size_t frames, bool fading);
    void takePending();  // the next ring (or none) replaces what plays
    void applyMaster(float* out, size_t frames);
    void whileSilent();

    int rate_;
    size_t edgeFrames_, cutFrames_, primeFrames_, reserveFrames_;
    float trackFadeStep_, resumeStep_, rampStep_, muteStep_;

    bool muted_ = false;
    float master_ = 1.0f;  // the mute envelope over the whole mix

    Voice effect_;
    std::vector<Voice> fading_;  // effects cut off, fading out
    float effectsGain_ = 1.0f, effectsTarget_ = 1.0f;

    enum class MusicState { Waiting, Running };
    std::shared_ptr<FrameRing> music_, pending_;
    bool switching_ = false;  // pending_ holds the next ring (or none)
    MusicState musicState_ = MusicState::Waiting;
    float level_ = 0.0f;  // the music's fade envelope
    float musicGain_ = 1.0f, musicTarget_ = 1.0f;
    uint64_t underruns_ = 0;
    std::vector<std::shared_ptr<FrameRing>> retired_;
    std::vector<float> scratch_;
};

// The sum of the mix stays within -1..1: above the knee, peaks bend smoothly
// towards full scale instead of being cut flat by the device's conversion.
float softClip(float x);

} // namespace opense4::client::audiomix
