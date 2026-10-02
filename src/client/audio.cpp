#include "client/audio.hpp"

#include "core/log.hpp"
#include "core/rng.hpp"
#include "ruleset/ruleset.hpp"

#include <SDL3/SDL.h>

#define DR_MP3_IMPLEMENTATION
#include <dr_mp3.h>

#include <algorithm>
#include <format>
#include <map>

namespace opense4::client {

namespace {

constexpr int kMusicBufferSeconds = 2;

} // namespace

struct Audio::Impl {
    struct Clip {
        SDL_AudioSpec spec{};
        std::vector<uint8_t> data;
    };

    SDL_AudioDeviceID device = 0;
    SDL_AudioSpec deviceSpec{};
    const assets::InstallFiles* files = nullptr;
    AudioOptions options;
    std::map<std::string, std::optional<Clip>> clips;  // misses are cached too
    std::vector<SDL_AudioStream*> voices;

    // Music: one track, looped.
    std::string trackFile;
    bool musicPlaying = false;
    drmp3 mp3{};
    bool mp3Open = false;
    SDL_AudioStream* musicStream = nullptr;

    const Clip* clip(std::string_view name) {
        const std::string key = std::string(name) + (options.remastered ? "#new" : "#old");
        if (auto it = clips.find(key); it != clips.end()) return it->second ? &*it->second : nullptr;
        std::optional<Clip> loaded;
        if (files)
            for (const std::string& candidate : soundCandidates(name, options.remastered))
                if (auto path = files->find(candidate)) {
                    Clip c;
                    Uint8* buf = nullptr;
                    Uint32 len = 0;
                    if (SDL_LoadWAV(path->string().c_str(), &c.spec, &buf, &len)) {
                        c.data.assign(buf, buf + len);
                        SDL_free(buf);
                        loaded = std::move(c);
                        break;
                    }
                }
        auto [it, inserted] = clips.emplace(key, std::move(loaded));
        return it->second ? &*it->second : nullptr;
    }

    void closeTrack() {
        if (mp3Open) drmp3_uninit(&mp3);
        mp3Open = false;
        if (musicStream) SDL_DestroyAudioStream(musicStream);
        musicStream = nullptr;
    }

    float musicGainNow() const { return musicGain(musicStep(options.musicVolume)); }

    // Opens the track (again, to loop it); a missing file plays nothing.
    bool openTrack() {
        closeTrack();
        if (!files || trackFile.empty()) return false;
        auto path = files->find("Music/" + trackFile);
        if (!path) return false;
#if defined(_WIN32)
        // The wide name: fopen of a narrow one takes the ANSI code page (unless the manifest's UTF-8 applies).
        if (!drmp3_init_file_w(&mp3, path->wstring().c_str(), nullptr)) return false;
#else
        if (!drmp3_init_file(&mp3, path->string().c_str(), nullptr)) return false;
#endif
        mp3Open = true;
        SDL_AudioSpec src{SDL_AUDIO_S16, static_cast<int>(mp3.channels), static_cast<int>(mp3.sampleRate)};
        musicStream = SDL_CreateAudioStream(&src, &deviceSpec);
        if (!musicStream || !SDL_BindAudioStream(device, musicStream)) {
            closeTrack();
            return false;
        }
        SDL_SetAudioStreamGain(musicStream, musicGainNow());
        return true;
    }

    void feedMusic() {
        if (!musicPlaying || !options.music) return;
        if (!mp3Open && !openTrack()) {
            musicPlaying = false;
            return;
        }
        const int bytesPerFrame = static_cast<int>(mp3.channels) * 2;
        const int want = static_cast<int>(mp3.sampleRate) * bytesPerFrame * kMusicBufferSeconds;
        std::vector<drmp3_int16> pcm(4096 * mp3.channels);
        while (SDL_GetAudioStreamQueued(musicStream) < want) {
            const drmp3_uint64 frames = drmp3_read_pcm_frames_s16(&mp3, 4096, pcm.data());
            if (frames == 0) {
                // Track finished: let the queued tail play out, then start it again.
                if (SDL_GetAudioStreamQueued(musicStream) + SDL_GetAudioStreamAvailable(musicStream) == 0) openTrack();
                return;
            }
            SDL_PutAudioStreamData(musicStream, pcm.data(), static_cast<int>(frames) * bytesPerFrame);
        }
    }
};

Audio::Audio() : impl_(std::make_unique<Impl>()) {}

Audio::~Audio() { close(); }

bool Audio::open() {
    if (impl_->device) return true;
    if (!SDL_WasInit(SDL_INIT_AUDIO) && !SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        log::info("No audio: {}", SDL_GetError());
        return false;
    }
    impl_->device = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr);
    if (!impl_->device) {
        log::info("No audio device: {}", SDL_GetError());
        return false;
    }
    SDL_GetAudioDeviceFormat(impl_->device, &impl_->deviceSpec, nullptr);
    return true;
}

void Audio::close() {
    if (!impl_) return;
    impl_->closeTrack();
    for (SDL_AudioStream* v : impl_->voices) SDL_DestroyAudioStream(v);
    impl_->voices.clear();
    if (impl_->device) SDL_CloseAudioDevice(impl_->device);
    impl_->device = 0;
}

bool Audio::active() const { return impl_->device != 0; }

void Audio::setInstall(const assets::InstallFiles* files) {
    impl_->files = files;
    impl_->clips.clear();
}

void Audio::setOptions(const AudioOptions& options) {
    const bool musicWasOn = impl_->options.music;
    impl_->options = options;
    impl_->options.soundVolume = std::clamp(options.soundVolume, 0.0f, 1.0f);
    impl_->options.musicVolume = std::clamp(options.musicVolume, 0.0f, 1.0f);
    if (impl_->musicStream) SDL_SetAudioStreamGain(impl_->musicStream, impl_->musicGainNow());
    if (musicWasOn && !options.music) impl_->closeTrack();
}

const AudioOptions& Audio::options() const { return impl_->options; }

void Audio::play(std::string_view name) {
    Impl& a = *impl_;
    if (!a.device || !a.options.sound || name.empty()) return;
    const Impl::Clip* c = a.clip(name);
    if (!c) return;
    // One effect at a time: the new one cuts off the one playing.
    for (SDL_AudioStream* v : a.voices) SDL_DestroyAudioStream(v);
    a.voices.clear();
    SDL_AudioStream* s = SDL_CreateAudioStream(&c->spec, &a.deviceSpec);
    if (!s) return;
    if (!SDL_BindAudioStream(a.device, s)) {
        SDL_DestroyAudioStream(s);
        return;
    }
    SDL_SetAudioStreamGain(s, a.options.soundVolume);
    SDL_PutAudioStreamData(s, c->data.data(), static_cast<int>(c->data.size()));
    SDL_FlushAudioStream(s);
    a.voices.push_back(s);
}

void Audio::playTrack(const std::string& file) {
    Impl& a = *impl_;
    if (!a.device || file.empty()) return;
    if (a.musicPlaying && a.trackFile == file) return;  // already playing it
    a.trackFile = file;
    a.closeTrack();
    a.musicPlaying = true;
}

void Audio::stopMusic() {
    impl_->musicPlaying = false;
    impl_->trackFile.clear();
    impl_->closeTrack();
}

bool Audio::musicPlaying() const { return impl_->musicPlaying; }

void Audio::update() {
    Impl& a = *impl_;
    if (!a.device) return;
    std::erase_if(a.voices, [](SDL_AudioStream* s) {
        if (SDL_GetAudioStreamQueued(s) > 0 || SDL_GetAudioStreamAvailable(s) > 0) return false;
        SDL_DestroyAudioStream(s);
        return true;
    });
    a.feedMusic();
}

Audio& audio() {
    static Audio instance;
    return instance;
}

} // namespace opense4::client
