#include "client/audio_mixer.hpp"

#include <SDL3/SDL.h>

#define DR_MP3_IMPLEMENTATION
#include <dr_mp3.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <format>
#include <fstream>

namespace opense4::client::audiomix {

namespace {

size_t framesFor(int rate, float seconds) { return std::max<size_t>(1, static_cast<size_t>(static_cast<float>(rate) * seconds)); }

SDL_AudioSpec mixSpec(int rate) { return SDL_AudioSpec{SDL_AUDIO_F32, kChannels, rate}; }

} // namespace

// --- decoding -------------------------------------------------------------------

std::string readFileBytes(const std::filesystem::path& file, std::vector<uint8_t>& out) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(file, ec);
    if (ec) return ec.message();
    std::ifstream in(file, std::ios::binary);
    if (!in) return "it cannot be opened";
    out.resize(static_cast<size_t>(size));
    if (!in.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(out.size()))) return "it cannot be read";
    return {};
}

std::optional<Clip> decodeWav(std::span<const uint8_t> bytes, int mixRate, std::string& error) {
    SDL_IOStream* io = SDL_IOFromConstMem(bytes.data(), bytes.size());
    if (!io) {
        error = SDL_GetError();
        return std::nullopt;
    }
    SDL_AudioSpec spec{};
    Uint8* pcm = nullptr;
    Uint32 length = 0;
    if (!SDL_LoadWAV_IO(io, true, &spec, &pcm, &length)) {
        error = std::format("not a WAV file SDL can read ({})", SDL_GetError());
        return std::nullopt;
    }
    const SDL_AudioSpec dst = mixSpec(mixRate);
    Uint8* converted = nullptr;
    int convertedLength = 0;
    const bool ok = SDL_ConvertAudioSamples(&spec, pcm, static_cast<int>(length), &dst, &converted, &convertedLength);
    SDL_free(pcm);
    if (!ok) {
        error = std::format("its format cannot be converted ({})", SDL_GetError());
        return std::nullopt;
    }
    Clip clip;
    clip.samples.resize(static_cast<size_t>(convertedLength) / sizeof(float));
    std::memcpy(clip.samples.data(), converted, clip.samples.size() * sizeof(float));
    SDL_free(converted);
    if (clip.frames() == 0) {
        error = "it holds no sound";
        return std::nullopt;
    }
    return clip;
}

std::optional<Mp3Info> probeMp3(std::span<const uint8_t> bytes, std::string& error) {
    drmp3 mp3;
    if (!drmp3_init_memory(&mp3, bytes.data(), bytes.size(), nullptr)) {
        error = "no MP3 audio found in it";
        return std::nullopt;
    }
    Mp3Info info{static_cast<int>(mp3.sampleRate), static_cast<int>(mp3.channels), 0.0};
    const drmp3_uint64 frames = drmp3_get_pcm_frame_count(&mp3);
    drmp3_uninit(&mp3);
    if (frames == 0 || info.sampleRate <= 0 || info.channels <= 0) {
        error = "no MP3 audio found in it";
        return std::nullopt;
    }
    info.seconds = static_cast<double>(frames) / info.sampleRate;
    return info;
}

// --- FrameRing ------------------------------------------------------------------

FrameRing::FrameRing(size_t capacityFrames) : data_(std::max<size_t>(capacityFrames, 1) * kChannels), capacity_(std::max<size_t>(capacityFrames, 1)) {}

size_t FrameRing::available() const { return writePos_.load(std::memory_order_acquire) - readPos_.load(std::memory_order_acquire); }

size_t FrameRing::space() const { return capacity_ - available(); }

size_t FrameRing::write(const float* frames, size_t count) {
    const size_t w = writePos_.load(std::memory_order_relaxed);
    const size_t r = readPos_.load(std::memory_order_acquire);
    const size_t n = std::min(count, capacity_ - (w - r));
    const size_t at = w % capacity_;
    const size_t first = std::min(n, capacity_ - at);
    std::copy_n(frames, first * kChannels, data_.data() + at * kChannels);
    std::copy_n(frames + first * kChannels, (n - first) * kChannels, data_.data());
    writePos_.store(w + n, std::memory_order_release);
    return n;
}

size_t FrameRing::read(float* out, size_t count) {
    const size_t r = readPos_.load(std::memory_order_relaxed);
    const size_t w = writePos_.load(std::memory_order_acquire);
    const size_t n = std::min(count, w - r);
    const size_t at = r % capacity_;
    const size_t first = std::min(n, capacity_ - at);
    std::copy_n(data_.data() + at * kChannels, first * kChannels, out);
    std::copy_n(data_.data(), (n - first) * kChannels, out + first * kChannels);
    readPos_.store(r + n, std::memory_order_release);
    return n;
}

// --- MusicTrack -----------------------------------------------------------------

MusicTrack::MusicTrack(std::filesystem::path file, int mixRate, double bufferSeconds)
    : file_(std::move(file)), mixRate_(mixRate),
      ring_(std::make_shared<FrameRing>(static_cast<size_t>(std::max(0.2, bufferSeconds) * mixRate))) {
    start();
}

MusicTrack::MusicTrack(std::vector<uint8_t> mp3, int mixRate, double bufferSeconds)
    : bytes_(std::move(mp3)), mixRate_(mixRate),
      ring_(std::make_shared<FrameRing>(static_cast<size_t>(std::max(0.2, bufferSeconds) * mixRate))) {
    start();
}

void MusicTrack::start() { thread_ = std::thread([this] { run(); }); }

MusicTrack::~MusicTrack() {
    {
        const std::lock_guard lock(mutex_);
        stop_ = true;
    }
    wake_.notify_all();
    if (thread_.joinable()) thread_.join();
}

void MusicTrack::run() {
    auto fail = [this](std::string why) {
        error_ = std::move(why);
        state_.store(State::Failed, std::memory_order_release);
    };
    if (!file_.empty())
        if (std::string why = readFileBytes(file_, bytes_); !why.empty()) return fail(std::format("cannot read the file: {}", why));
    std::string why;
    const auto probed = probeMp3(bytes_, why);
    if (!probed) return fail(why);
    drmp3 mp3;
    if (!drmp3_init_memory(&mp3, bytes_.data(), bytes_.size(), nullptr)) return fail("no MP3 audio found in it");
    const SDL_AudioSpec src{SDL_AUDIO_S16, static_cast<int>(mp3.channels), static_cast<int>(mp3.sampleRate)};
    const SDL_AudioSpec dst = mixSpec(mixRate_);
    SDL_AudioStream* convert = SDL_CreateAudioStream(&src, &dst);
    if (!convert) {
        drmp3_uninit(&mp3);
        return fail(std::format("cannot convert {} Hz, {} channels to {} Hz stereo ({})", src.freq, src.channels, dst.freq, SDL_GetError()));
    }
    info_ = *probed;
    state_.store(State::Playing, std::memory_order_release);

    // Decode a chunk whenever the ring has room for what it gives; the rest of
    // the time wait (woken early only to stop).
    constexpr size_t kChunk = 1152;  // one MPEG-1 layer III frame
    const size_t chunkOut = kChunk * static_cast<size_t>(mixRate_) / mp3.sampleRate + 64;
    std::vector<drmp3_int16> pcm(kChunk * mp3.channels);
    std::vector<float> out(chunkOut * kChannels);
    int emptyReads = 0;
    std::unique_lock lock(mutex_);
    while (!stop_) {
        if (ring_->space() < chunkOut) {
            wake_.wait_for(lock, std::chrono::milliseconds(10));
            continue;
        }
        lock.unlock();
        const drmp3_uint64 frames = drmp3_read_pcm_frames_s16(&mp3, kChunk, pcm.data());
        if (frames == 0) {
            // The end: loop from the start, without a gap (the converter keeps its state).
            if (++emptyReads > 2) {
                lock.lock();
                fail("decoding stopped: the file looks damaged");
                break;
            }
            drmp3_seek_to_pcm_frame(&mp3, 0);
            loops_.fetch_add(1, std::memory_order_relaxed);
        } else {
            emptyReads = 0;
            SDL_PutAudioStreamData(convert, pcm.data(), static_cast<int>(frames * mp3.channels * sizeof(drmp3_int16)));
            const int room = static_cast<int>(std::min(ring_->space(), chunkOut) * kChannels * sizeof(float));
            const int got = SDL_GetAudioStreamData(convert, out.data(), room);
            if (got > 0) ring_->write(out.data(), static_cast<size_t>(got) / (kChannels * sizeof(float)));
        }
        lock.lock();
    }
    SDL_DestroyAudioStream(convert);
    drmp3_uninit(&mp3);
}

// --- Mixer ----------------------------------------------------------------------

namespace {
constexpr size_t kScratchFrames = 2048;
constexpr size_t kMaxFading = 4;
} // namespace

Mixer::Mixer(int rate)
    : rate_(rate), edgeFrames_(framesFor(rate, kEdgeSeconds)), cutFrames_(framesFor(rate, kCutSeconds)),
      primeFrames_(framesFor(rate, kPrimeSeconds)), reserveFrames_(framesFor(rate, kReserveSeconds)),
      trackFadeStep_(1.0f / static_cast<float>(framesFor(rate, kTrackFadeSeconds))),
      resumeStep_(1.0f / static_cast<float>(framesFor(rate, kResumeSeconds))),
      rampStep_(1.0f / static_cast<float>(framesFor(rate, kRampSeconds))), scratch_(kScratchFrames * kChannels) {
    fading_.reserve(kMaxFading);
    retired_.reserve(8);
}

void Mixer::playEffect(std::shared_ptr<const Clip> clip) {
    stopEffects();
    if (clip && clip->frames() > 0) effect_ = Voice{std::move(clip), 0, 1.0f};
}

void Mixer::stopEffects() {
    if (!effect_.clip) return;
    if (fading_.size() >= kMaxFading)
        fading_.erase(std::min_element(fading_.begin(), fading_.end(), [](const Voice& a, const Voice& b) { return a.level < b.level; }));
    fading_.push_back(std::move(effect_));
    effect_ = {};
}

void Mixer::setEffectsGain(float gain) { effectsTarget_ = std::clamp(gain, 0.0f, 1.0f); }

void Mixer::setMusicGain(float gain) { musicTarget_ = std::clamp(gain, 0.0f, 1.0f); }

void Mixer::setMusic(std::shared_ptr<FrameRing> ring) {
    if (switching_) {
        if (pending_) retired_.push_back(std::move(pending_));
        pending_.reset();
        switching_ = false;
    }
    if (ring == music_) return;  // back to what plays: it fades in again
    pending_ = std::move(ring);
    switching_ = true;
}

std::vector<std::shared_ptr<FrameRing>> Mixer::takeRetired() {
    std::vector<std::shared_ptr<FrameRing>> out;
    out.swap(retired_);
    retired_.reserve(8);
    return out;
}

namespace {
float approach(float value, float target, float step) {
    if (value < target) return std::min(target, value + step);
    return std::max(target, value - step);
}
} // namespace

bool Mixer::addVoice(Voice& v, float* out, size_t frames, bool fading) {
    const Clip& c = *v.clip;
    const size_t total = c.frames();
    const size_t n = std::min(frames, total - v.pos);
    const float edge = static_cast<float>(edgeFrames_);
    const float cut = 1.0f / static_cast<float>(cutFrames_);
    for (size_t k = 0; k < n; ++k) {
        const size_t p = v.pos + k;
        // Ramps at both ends of the clip: some files start or end mid-wave.
        float e = 1.0f;
        if (p < edgeFrames_) e = static_cast<float>(p) / edge;
        if (const size_t left = total - 1 - p; left < edgeFrames_) e = std::min(e, static_cast<float>(left) / edge);
        if (fading) {
            v.level = std::max(0.0f, v.level - cut);
            e *= v.level;
        }
        out[2 * k] += c.samples[2 * p] * e;
        out[2 * k + 1] += c.samples[2 * p + 1] * e;
    }
    v.pos += n;
    return v.pos < total && (!fading || v.level > 0.0f);
}

void Mixer::mixEffects(float* out, size_t frames) {
    if (!effect_.clip && fading_.empty()) {
        effectsGain_ = effectsTarget_;
        return;
    }
    float* buf = scratch_.data();
    std::fill_n(buf, frames * kChannels, 0.0f);
    std::erase_if(fading_, [&](Voice& v) { return !addVoice(v, buf, frames, true); });
    if (effect_.clip && !addVoice(effect_, buf, frames, false)) effect_ = {};
    for (size_t k = 0; k < frames; ++k) {
        effectsGain_ = approach(effectsGain_, effectsTarget_, rampStep_);
        out[2 * k] += buf[2 * k] * effectsGain_;
        out[2 * k + 1] += buf[2 * k + 1] * effectsGain_;
    }
}

void Mixer::mixMusic(float* out, size_t frames) {
    size_t done = 0;
    while (done < frames) {
        if (switching_ && (!music_ || level_ <= 0.0f || musicState_ == MusicState::Waiting)) {
            if (music_) retired_.push_back(std::move(music_));
            music_ = std::move(pending_);
            pending_.reset();
            switching_ = false;
            musicState_ = MusicState::Waiting;
            level_ = 0.0f;
        }
        if (!music_) return;
        if (musicState_ == MusicState::Waiting) {
            if (music_->available() < primeFrames_) return;  // silent until enough is buffered
            musicState_ = MusicState::Running;
        }
        size_t want = std::min(frames - done, kScratchFrames);
        // Keep a reserve: music that runs dry (its decoder fell behind) fades
        // out over its last frames instead of breaking off.
        const size_t avail = music_->available();
        const bool dry = avail < want + reserveFrames_;
        if (dry) want = std::min(want, avail);
        float* buf = scratch_.data();
        const size_t got = music_->read(buf, want);
        const float target = switching_ || dry ? 0.0f : 1.0f;
        const float step = dry ? std::max(level_ / static_cast<float>(std::max<size_t>(avail, 1)), 1e-6f)
                         : switching_ ? trackFadeStep_
                                      : resumeStep_;
        float* o = out + done * kChannels;
        for (size_t k = 0; k < got; ++k) {
            level_ = approach(level_, target, step);
            musicGain_ = approach(musicGain_, musicTarget_, rampStep_);
            const float g = level_ * musicGain_;
            o[2 * k] += buf[2 * k] * g;
            o[2 * k + 1] += buf[2 * k + 1] * g;
        }
        done += got;
        if (dry && got == avail) {
            // Empty: silent until enough is buffered again, then it fades back in.
            if (!switching_) ++underruns_;
            level_ = 0.0f;
            musicState_ = MusicState::Waiting;
            if (!switching_) return;
        }
    }
}

void Mixer::mix(float* out, size_t frames) {
    while (frames > 0) {
        const size_t n = std::min(frames, kScratchFrames);
        std::fill_n(out, n * kChannels, 0.0f);
        mixMusic(out, n);
        mixEffects(out, n);
        for (size_t i = 0; i < n * kChannels; ++i) out[i] = softClip(out[i]);
        out += n * kChannels;
        frames -= n;
    }
}

float softClip(float x) {
    constexpr float kKnee = 0.9f;
    const float a = std::fabs(x);
    if (a <= kKnee) return x;
    const float y = kKnee + (1.0f - kKnee) * std::tanh((a - kKnee) / (1.0f - kKnee));
    return std::copysign(y, x);
}

} // namespace opense4::client::audiomix
