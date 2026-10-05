// The audio mix without a device (client/audio_mixer.hpp): decoding, the ring
// the music decoder fills, and the mixer's fades and ramps. A click is a large
// jump between neighbouring samples, so most checks bound the largest step of
// the output on worst-case signals (constant levels, which a cut or a hard
// start turns into a full jump).
//
// tests/fixtures/audio/tone.mp3 is our own: one second of a 440 Hz sine at
// half scale (the sine source gives one eighth, and -ac 2 takes 3 dB) with 50 ms fades, made with
//   ffmpeg -f lavfi -i "sine=frequency=440:duration=1:sample_rate=22050"
//     -af "volume=5.66,afade=t=in:d=0.05,afade=t=out:st=0.95:d=0.05" -ac 2 -ar 22050
//     -c:a libmp3lame -b:a 32k -write_xing 0 -id3v2_version 0 -write_id3v1 0 tone.mp3
// (MPEG-2 layer III at 22.05 kHz, the format of the classic game's music).

#include "assets/assets.hpp"
#include "client/audio.hpp"
#include "client/audio_mixer.hpp"
#include "ruleset/ruleset.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <string>
#include <thread>
#include <vector>

using namespace opense4;
using namespace opense4::client::audiomix;

namespace {

constexpr int kRate = 48000;

// The largest jump between neighbouring samples of either channel.
float maxStep(const std::vector<float>& s) {
    float m = 0.0f;
    for (size_t i = 2; i < s.size(); ++i) m = std::max(m, std::fabs(s[i] - s[i - 2]));
    return m;
}

std::vector<float> mixFrames(Mixer& m, size_t frames, size_t chunk = 512) {
    std::vector<float> out(frames * kChannels);
    for (size_t done = 0; done < frames; done += chunk) m.mix(out.data() + done * kChannels, std::min(chunk, frames - done));
    return out;
}

void append(std::vector<float>& to, const std::vector<float>& more) { to.insert(to.end(), more.begin(), more.end()); }

std::shared_ptr<const Clip> constantClip(float level, size_t frames) {
    auto c = std::make_shared<Clip>();
    c->samples.assign(frames * kChannels, level);
    return c;
}

std::shared_ptr<FrameRing> constantRing(float level, size_t frames, size_t capacity = kRate) {
    auto r = std::make_shared<FrameRing>(capacity);
    const std::vector<float> data(frames * kChannels, level);
    r->write(data.data(), frames);
    return r;
}

// A WAV file in memory: PCM, `bits` 8 or 16.
std::vector<uint8_t> makeWav(int rate, int channels, int bits, size_t frames, float amplitude, float hz) {
    const uint32_t bytesPer = static_cast<uint32_t>(bits / 8);
    const uint32_t dataSize = static_cast<uint32_t>(frames) * static_cast<uint32_t>(channels) * bytesPer;
    std::vector<uint8_t> w;
    auto put = [&](const void* p, size_t n) { w.insert(w.end(), static_cast<const uint8_t*>(p), static_cast<const uint8_t*>(p) + n); };
    auto u32 = [&](uint32_t v) { put(&v, 4); };
    auto u16 = [&](uint16_t v) { put(&v, 2); };
    put("RIFF", 4);
    u32(36 + dataSize);
    put("WAVEfmt ", 8);
    u32(16);
    u16(1);
    u16(static_cast<uint16_t>(channels));
    u32(static_cast<uint32_t>(rate));
    u32(static_cast<uint32_t>(rate) * static_cast<uint32_t>(channels) * bytesPer);
    u16(static_cast<uint16_t>(static_cast<uint32_t>(channels) * bytesPer));
    u16(static_cast<uint16_t>(bits));
    put("data", 4);
    u32(dataSize);
    for (size_t i = 0; i < frames; ++i) {
        const float v = amplitude * std::sin(2.0f * 3.14159265f * hz * static_cast<float>(i) / static_cast<float>(rate));
        for (int c = 0; c < channels; ++c) {
            if (bits == 16) {
                const int16_t s = static_cast<int16_t>(std::lround(v * 32767.0f));
                put(&s, 2);
            } else {
                const uint8_t s = static_cast<uint8_t>(std::lround(128.0f + v * 127.0f));
                put(&s, 1);
            }
        }
    }
    return w;
}

std::vector<uint8_t> toneMp3() {
    std::vector<uint8_t> bytes;
    REQUIRE(readFileBytes(std::filesystem::path(OPENSE4_FIXTURE_DIR) / "audio" / "tone.mp3", bytes).empty());
    return bytes;
}

float peak(const std::vector<float>& s) {
    float p = 0.0f;
    for (float v : s) p = std::max(p, std::fabs(v));
    return p;
}

} // namespace

TEST_CASE("audio mix: the frame ring keeps frames in order across its end") {
    FrameRing ring(5);
    CHECK(ring.available() == 0);
    CHECK(ring.space() == 5);
    const float a[] = {1, 1, 2, 2, 3, 3, 4, 4};
    CHECK(ring.write(a, 4) == 4);
    float out[16] = {};
    CHECK(ring.read(out, 3) == 3);
    CHECK(out[4] == 3.0f);
    const float b[] = {5, 5, 6, 6, 7, 7, 8, 8, 9, 9};
    CHECK(ring.write(b, 5) == 4);  // room for four
    CHECK(ring.available() == 5);
    CHECK(ring.read(out, 8) == 5);
    CHECK(std::vector<float>(out, out + 10) == std::vector<float>{4, 4, 5, 5, 6, 6, 7, 7, 8, 8});
    CHECK(ring.available() == 0);
}

TEST_CASE("audio mix: WAV files decode to stereo float at the mix rate") {
    std::string why;
    // 16-bit stereo at 44.1 kHz (the classic sound files), to 48 kHz.
    const auto clip = decodeWav(makeWav(44100, 2, 16, 4410, 0.5f, 1000.0f), kRate, why);
    REQUIRE(clip);
    CHECK(clip->frames() == doctest::Approx(4800).epsilon(0.01));
    CHECK(peak(clip->samples) == doctest::Approx(0.5).epsilon(0.05));
    // 8-bit mono at 22.05 kHz: both channels the same.
    const auto mono = decodeWav(makeWav(22050, 1, 8, 2205, 0.5f, 500.0f), kRate, why);
    REQUIRE(mono);
    CHECK(mono->frames() == doctest::Approx(4800).epsilon(0.01));
    for (size_t i = 0; i < mono->frames(); i += 97) CHECK(mono->samples[2 * i] == mono->samples[2 * i + 1]);
    // Not a WAV file: why.
    const std::vector<uint8_t> junk(200, 7);
    CHECK_FALSE(decodeWav(junk, kRate, why));
    CHECK_FALSE(why.empty());
}

TEST_CASE("audio mix: MP3 files are probed for their format") {
    std::string why;
    const auto info = probeMp3(toneMp3(), why);
    REQUIRE(info);
    CHECK(info->sampleRate == 22050);
    CHECK(info->channels == 2);
    CHECK(info->seconds == doctest::Approx(1.0).epsilon(0.1));
    const std::vector<uint8_t> junk(4000, 0x55);
    CHECK_FALSE(probeMp3(junk, why));
    CHECK(why.find("MP3") != std::string::npos);
}

TEST_CASE("audio mix: a music track decodes on its own thread and loops without a click") {
    MusicTrack track(toneMp3(), kRate, 0.5);
    std::vector<float> got;
    std::vector<float> buf(4096 * kChannels);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    // Three and a half plays of the one-second tone, read as the mixer would.
    while (got.size() < static_cast<size_t>(3.5 * kRate) * kChannels && std::chrono::steady_clock::now() < deadline) {
        const size_t n = track.ring()->read(buf.data(), 4096);
        got.insert(got.end(), buf.begin(), buf.begin() + static_cast<std::ptrdiff_t>(n * kChannels));
        if (n == 0) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    REQUIRE(track.state() == MusicTrack::State::Playing);
    CHECK(track.info().sampleRate == 22050);
    CHECK(track.loops() >= 3);
    REQUIRE(got.size() >= static_cast<size_t>(3.5 * kRate) * kChannels);
    // A 440 Hz sine at half scale moves at most 0.029 a sample at 48 kHz; the
    // loop points, where the tone has faded out and in, add nothing larger.
    CHECK(maxStep(got) < 0.05f);
    CHECK(peak(got) == doctest::Approx(0.5).epsilon(0.15));
    // The tone sounds in every play, not just the first.
    for (int play = 0; play < 3; ++play) {
        const size_t mid = (static_cast<size_t>(play) * kRate + kRate / 2) * kChannels;
        float level = 0.0f;
        for (size_t i = mid; i < mid + 4800 * kChannels; ++i) level = std::max(level, std::fabs(got[i]));
        CHECK(level > 0.3f);
    }
}

TEST_CASE("audio mix: a track that cannot be played says why") {
    MusicTrack junk(std::vector<uint8_t>(5000, 0x55), kRate);
    MusicTrack missing(std::filesystem::path(OPENSE4_FIXTURE_DIR) / "audio" / "no such track.mp3", kRate);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while ((junk.state() == MusicTrack::State::Opening || missing.state() == MusicTrack::State::Opening) &&
           std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    CHECK(junk.state() == MusicTrack::State::Failed);
    CHECK(junk.error().find("MP3") != std::string::npos);
    CHECK(missing.state() == MusicTrack::State::Failed);
    CHECK(missing.error().find("cannot read the file") != std::string::npos);
}

TEST_CASE("audio mix: an effect cut off by the next fades out, and clips ramp at both ends") {
    Mixer m(kRate);
    // Constant levels are the worst case: cut or started hard, they jump at once.
    m.playEffect(constantClip(0.8f, kRate));
    std::vector<float> out = mixFrames(m, 1000);
    CHECK(out.front() == 0.0f);                              // the clip ramps in
    CHECK(out[2 * 999] == doctest::Approx(0.8f));
    m.playEffect(constantClip(-0.8f, kRate));               // cuts the first off mid-clip
    append(out, mixFrames(m, 2000));
    CHECK(out[2 * 2500] == doctest::Approx(-0.8f));         // only the new one plays
    m.playEffect(constantClip(0.8f, 300));                   // a short one, played to its end
    append(out, mixFrames(m, 2000));
    CHECK(out[2 * 4999] == 0.0f);
    CHECK_FALSE(m.effectPlaying());
    CHECK(maxStep(out) < 0.02f);                             // a hard cut would jump 1.6
}

TEST_CASE("audio mix: a new effect volume ramps") {
    Mixer m(kRate);
    m.playEffect(constantClip(0.8f, kRate));
    std::vector<float> out = mixFrames(m, 2000);
    m.setEffectsGain(0.1f);
    append(out, mixFrames(m, 4000));
    CHECK(out.back() == doctest::Approx(0.08f));
    CHECK(maxStep(out) < 0.01f);
}

TEST_CASE("audio mix: music fades in, fades out for the next track, and stops with a fade") {
    Mixer m(kRate);
    auto first = constantRing(0.5f, 30000);
    m.setMusic(first);
    std::vector<float> out = mixFrames(m, 2400);
    CHECK(out[2 * 2399] == doctest::Approx(0.5f));
    // The next track: the first fades out (a quarter second), then the second starts.
    auto second = constantRing(-0.5f, 30000);
    m.setMusic(second);
    append(out, mixFrames(m, 6000));
    CHECK(out.back() > 0.0f);  // still fading out
    append(out, mixFrames(m, 9000));
    CHECK(out.back() == doctest::Approx(-0.5f));
    CHECK(m.takeRetired() == std::vector<std::shared_ptr<FrameRing>>{first});
    // A volume step (100 % to 20 %, -30 dB) ramps.
    m.setMusicGain(client::musicGain(1));
    append(out, mixFrames(m, 3000));
    CHECK(out.back() == doctest::Approx(-0.5f * client::musicGain(1)).epsilon(0.01));
    m.setMusicGain(1.0f);
    append(out, mixFrames(m, 2000));
    // Stopped: it fades out, then silence.
    m.setMusic(nullptr);
    append(out, mixFrames(m, 13000));
    CHECK(out.back() == 0.0f);
    CHECK(m.takeRetired() == std::vector<std::shared_ptr<FrameRing>>{second});
    CHECK(maxStep(out) < 0.002f);
    CHECK(m.underruns() == 0);
}

TEST_CASE("audio mix: music that runs dry fades out on its last frames and back in when fed") {
    Mixer m(kRate);
    auto ring = constantRing(0.5f, 14400);  // 0.3 s, then the decoder falls behind
    m.setMusic(ring);
    std::vector<float> out = mixFrames(m, 24000);
    CHECK(out[2 * 10000] == doctest::Approx(0.5f));
    CHECK(out.back() == 0.0f);
    CHECK(m.underruns() == 1);
    // Fed again: it waits for a tenth of a second of music, then fades in.
    const std::vector<float> more(9600 * kChannels, 0.5f);
    ring->write(more.data(), 9600);
    append(out, mixFrames(m, 4800));
    CHECK(out.back() == doctest::Approx(0.5f));
    CHECK(maxStep(out) < 0.002f);  // breaking off would jump 0.5
    CHECK(m.underruns() == 1);
}

// Muting (the game's window in the background): Mixer::kMuteSeconds of fade
// is 12000 frames at 48 kHz; a frame or two more may go by as the float
// envelope reaches 0.
constexpr size_t kMuteFrames = 12000;

TEST_CASE("audio mix: muted, the mix fades out, the music pauses where it is and goes on from there") {
    Mixer m(kRate);
    auto ring = constantRing(0.5f, 40000);
    m.setMusic(ring);
    std::vector<float> out = mixFrames(m, 4800);
    m.playEffect(constantClip(0.3f, kRate));
    append(out, mixFrames(m, 2400));
    CHECK(out.back() == doctest::Approx(0.8f));
    m.setMuted(true);
    CHECK(m.muted());
    // Halfway through the fade, both at half their level.
    append(out, mixFrames(m, kMuteFrames / 2));
    CHECK_FALSE(m.silent());
    CHECK(out.back() == doctest::Approx(0.4f).epsilon(0.01));
    append(out, mixFrames(m, kMuteFrames / 2 + 16));
    CHECK(m.silent());
    CHECK(out.back() == 0.0f);
    CHECK_FALSE(m.effectPlaying());  // let go once silent
    // The music stopped right where the fade ended: what was heard, no more.
    const size_t left = ring->available();
    CHECK(left <= 40000 - 4800 - 2400 - kMuteFrames);
    CHECK(left + 16 >= 40000 - 4800 - 2400 - kMuteFrames);
    // Silent: the ring is not read (the decoder would wait on it full), and an
    // effect asked for meanwhile is dropped.
    std::vector<float> quiet = mixFrames(m, kRate);
    m.playEffect(constantClip(0.3f, kRate));
    CHECK_FALSE(m.effectPlaying());
    append(quiet, mixFrames(m, kRate));
    CHECK(peak(quiet) == 0.0f);
    CHECK(ring->available() == left);
    append(out, quiet);
    // Unmuted: the music goes on from the frame where it paused and fades in;
    // the dropped effect does not come back.
    m.setMuted(false);
    CHECK_FALSE(m.silent());
    append(out, mixFrames(m, kMuteFrames / 2));
    CHECK(out.back() == doctest::Approx(0.25f).epsilon(0.01));
    append(out, mixFrames(m, kMuteFrames / 2 + 2400));
    CHECK(out.back() == doctest::Approx(0.5f));
    CHECK(ring->available() == left - kMuteFrames - 2400);
    CHECK(maxStep(out) < 0.005f);  // the clip's own 2 ms ramp is the steepest
    CHECK(m.underruns() == 0);
}

TEST_CASE("audio mix: unmuted before the fade ends, the mix comes straight back") {
    Mixer m(kRate);
    auto ring = constantRing(0.5f, 40000);
    m.setMusic(ring);
    std::vector<float> out = mixFrames(m, 2400);
    m.setMuted(true);
    append(out, mixFrames(m, 3000));
    m.setMuted(false);
    append(out, mixFrames(m, 6000));
    CHECK(out.back() == doctest::Approx(0.5f));
    float least = 1.0f;
    for (size_t i = 2400 * kChannels; i < out.size(); ++i) least = std::min(least, out[i]);
    CHECK(least > 0.3f);  // it never went silent, nor paused
    CHECK(ring->available() == 40000 - 11400);
    CHECK(maxStep(out) < 0.002f);
}

TEST_CASE("audio mix: a track changed or stopped while muted changes unheard") {
    Mixer m(kRate);
    auto first = constantRing(0.5f, 30000);
    m.setMusic(first);
    std::vector<float> out = mixFrames(m, 2400);
    m.setMuted(true);
    append(out, mixFrames(m, kMuteFrames + 16));
    REQUIRE(m.silent());
    // The next track: the old one is let go at once, the new one waits unread.
    auto second = constantRing(-0.5f, 30000);
    m.setMusic(second);
    append(out, mixFrames(m, 2400));
    CHECK(m.takeRetired() == std::vector<std::shared_ptr<FrameRing>>{first});
    CHECK(second->available() == 30000);
    // Unmuted, the new track fades in.
    m.setMuted(false);
    append(out, mixFrames(m, kMuteFrames + 2400));
    CHECK(out.back() == doctest::Approx(-0.5f));
    // Stopped while muted: silence once unmuted.
    m.setMuted(true);
    append(out, mixFrames(m, kMuteFrames + 16));
    REQUIRE(m.silent());
    m.setMusic(nullptr);
    append(out, mixFrames(m, 512));
    CHECK(m.takeRetired() == std::vector<std::shared_ptr<FrameRing>>{second});
    m.setMuted(false);
    const std::vector<float> after = mixFrames(m, 4800);
    CHECK(peak(after) == 0.0f);
    append(out, after);
    CHECK(maxStep(out) < 0.002f);
    CHECK(m.underruns() == 0);
}

TEST_CASE("audio mix: a track muted for a while goes on where it paused, its decoder waiting meanwhile") {
    using namespace std::chrono_literals;
    auto waitFor = [](auto done) {
        const auto deadline = std::chrono::steady_clock::now() + 20s;
        while (!done() && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(1ms);
        return done();
    };
    // The track as it plays without a pause, to compare with.
    MusicTrack whole(toneMp3(), kRate, 4.0);
    REQUIRE(waitFor([&] { return whole.ring()->available() >= static_cast<size_t>(kRate) * 3; }));
    std::vector<float> reference(static_cast<size_t>(kRate) * 3 * kChannels);
    whole.ring()->read(reference.data(), static_cast<size_t>(kRate) * 3);

    Mixer m(kRate);
    MusicTrack track(toneMp3(), kRate, 0.5);
    // Decoded ahead first, so the music starts at once and every frame mixed is a frame of the track.
    REQUIRE(waitFor([&] { return track.ring()->space() < 4096; }));
    m.setMusic(track.ring());
    // Mixes as the audio thread would, never faster than the decoder fills the ring.
    std::vector<float> out;
    std::vector<float> buf(512 * kChannels);
    auto play = [&](size_t frames) {
        for (size_t done = 0; done < frames;) {
            if (!m.silent()) REQUIRE(waitFor([&] { return track.ring()->available() >= 2048; }));
            const size_t n = std::min<size_t>(512, frames - done);
            m.mix(buf.data(), n);
            out.insert(out.end(), buf.begin(), buf.begin() + static_cast<std::ptrdiff_t>(n * kChannels));
            done += n;
        }
    };
    play(static_cast<size_t>(kRate) * 3 / 10);  // 0.3 s of the one-second tone
    m.setMuted(true);
    play(kMuteFrames + 512);
    REQUIRE(m.silent());
    const size_t heard = out.size() / kChannels;
    // 2.7 seconds of silence: the decoder fills the ring and waits; it does not
    // run on through the track.
    play(static_cast<size_t>(kRate) * 27 / 10);
    REQUIRE(waitFor([&] { return track.ring()->space() < 4096; }));
    const uint64_t loops = track.loops();
    std::this_thread::sleep_for(200ms);
    CHECK(track.loops() == loops);
    CHECK(track.ring()->space() < 4096);
    // Unmuted: once faded in, the music is the track again from where it paused.
    m.setMuted(false);
    const size_t back = out.size() / kChannels;
    play(static_cast<size_t>(kRate) * 3 / 2);
    CHECK(m.underruns() == 0);
    CHECK(maxStep(out) < 0.05f);  // as the tone itself
    // Where the silence began: the frames heard, the fade's included.
    size_t paused = heard;
    while (paused > 0 && out[2 * (paused - 1)] == 0.0f && out[2 * (paused - 1) + 1] == 0.0f) --paused;
    CHECK(paused + 1024 > heard);
    // The track goes on from there: the frame the fade ended on (silent, but
    // read), give or take the float envelope's last step or two.
    const size_t from = kMuteFrames + 256;
    float best = 1.0f, level = 0.0f;
    size_t bestShift = 0;
    for (size_t shift = 0; shift < 8; ++shift) {
        float diff = 0.0f;
        const size_t at = back * kChannels, ref = (paused + shift) * kChannels;
        for (size_t i = from * kChannels; i < (from + 9600) * kChannels; ++i) {
            diff = std::max(diff, std::fabs(out[at + i] - reference[ref + i]));
            level = std::max(level, std::fabs(out[at + i]));
        }
        if (diff < best) {
            best = diff;
            bestShift = shift;
        }
    }
    CHECK(level > 0.3f);
    CHECK(best < 1e-4f);
    CHECK(bestShift >= 1);  // the frame the fade reached 0 on was read
}

TEST_CASE("audio mix: the sum bends smoothly towards full scale instead of clipping") {
    CHECK(softClip(0.5f) == 0.5f);
    CHECK(softClip(-0.9f) == -0.9f);
    CHECK(softClip(1.2f) < 1.0f);
    CHECK(softClip(-3.0f) >= -1.0f);
    float last = -2.0f;
    for (float x = -2.0f; x <= 2.0f; x += 0.01f) {
        const float y = softClip(x);
        CHECK(y >= last);
        last = y;
    }
    // Effects and music adding up past full scale.
    Mixer m(kRate);
    m.setMusic(constantRing(0.6f, 20000));
    m.playEffect(constantClip(0.8f, 20000));
    const std::vector<float> out = mixFrames(m, 10000);
    CHECK(peak(out) < 1.0f);
    CHECK(peak(out) > 0.95f);
}

TEST_CASE("audio: tracks the playlists name and the install lacks") {
    client::Playlists lists;
    lists.intro = {"intro.mp3"};
    lists.background = {"intro.mp3", "calm.mp3"};
    const assets::InstallFiles files(std::filesystem::path(OPENSE4_FIXTURE_DIR) / "install");
    CHECK(client::missingTracks(lists, files) == std::vector<std::string>{"Music/intro.mp3", "Music/calm.mp3"});
}

TEST_CASE("installed data set: every sound and music file decodes (opt-in)") {
    const char* env = std::getenv("OPENSE4_CLASSIC_DATA");
    if (!env) return;
    const auto dir = ruleset::findInstalledDataDir(std::string_view(env) == "auto" ? std::filesystem::path{} : std::filesystem::path(env));
    REQUIRE(dir);
    const auto loaded = ruleset::loadRuleset(*dir);
    REQUIRE(loaded.ruleset);
    std::vector<std::string> problems;
    int sounds = 0;
    for (const char* folder : {"Sounds", "Sounds/New"}) {
        std::error_code ec;
        for (const auto& e : std::filesystem::directory_iterator(dir->parent_path() / folder, ec)) {
            if (!e.is_regular_file() || e.path().extension() != ".wav") continue;
            std::vector<uint8_t> bytes;
            std::string why = readFileBytes(e.path(), bytes);
            if (why.empty() && decodeWav(bytes, kRate, why)) ++sounds;
            else problems.push_back(std::format("{}: {}", e.path().string(), why));
        }
    }
    CHECK(sounds > 0);
    const assets::InstallFiles files(dir->parent_path());
    const client::Playlists lists = client::readPlaylists(loaded.ruleset->settings);
    for (const auto* list : {&lists.intro, &lists.background, &lists.combat})
        for (const std::string& track : *list) {
            const auto path = files.find("Music/" + track);
            if (!path) continue;  // reported by the test of the names in test_audio.cpp
            std::vector<uint8_t> bytes;
            std::string why = readFileBytes(*path, bytes);
            if (!why.empty() || !probeMp3(bytes, why)) problems.push_back(std::format("{}: {}", path->string(), why));
        }
    std::string all;
    for (const std::string& p : problems) all += p + "\n";
    INFO(all);
    CHECK(problems.empty());
}
