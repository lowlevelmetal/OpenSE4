#pragma once

// OGG Vorbis sound and music (docs/sdk/packages-and-data.md "Sounds and
// music"): mods may give a sound or a music track as an OGG Vorbis file in
// place of the classic WAV or MP3 of the same name. Decoded with stb_vorbis to
// 16-bit samples at the file's own rate; the client's mixer converts them.

#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace opense4::assets {

// Whether the bytes start as an OGG file ("OggS").
bool isOgg(std::span<const uint8_t> head);
// Whether they start as a WAV file (RIFF ... WAVE).
bool isWav(std::span<const uint8_t> head);

struct SoundInfo {
    int sampleRate = 0;
    int channels = 0;
    double seconds = 0;
};

// The format of an OGG Vorbis file, or why it cannot be read.
std::expected<SoundInfo, std::string> probeOgg(std::span<const uint8_t> bytes);

// A whole OGG Vorbis file decoded: interleaved 16-bit samples.
struct Pcm16 {
    int sampleRate = 0;
    int channels = 0;
    std::vector<int16_t> samples;
    size_t frames() const { return channels > 0 ? samples.size() / static_cast<size_t>(channels) : 0; }
};
std::expected<Pcm16, std::string> decodeOgg(std::span<const uint8_t> bytes);

// An OGG Vorbis file decoded piece by piece (music). The bytes must outlive it.
class OggStream {
public:
    static std::expected<std::unique_ptr<OggStream>, std::string> open(std::span<const uint8_t> bytes);
    ~OggStream();
    OggStream(const OggStream&) = delete;
    OggStream& operator=(const OggStream&) = delete;

    const SoundInfo& info() const { return info_; }
    // Up to `frames` interleaved frames into `out` (info().channels samples
    // each); 0 at the end of the file.
    size_t read(int16_t* out, size_t frames);
    // Back to the first sample.
    bool rewind();

private:
    OggStream() = default;
    struct Decoder;
    Decoder* decoder_ = nullptr;
    SoundInfo info_;
};

} // namespace opense4::assets
