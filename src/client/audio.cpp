#include "client/audio.hpp"

#include "client/audio_mixer.hpp"
#include "core/log.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <format>
#include <map>
#include <set>

namespace opense4::client {

namespace {

// Frames mixed per step of the audio callback.
constexpr int kCallbackFrames = 1024;
// How often the log may report the music running dry, and the settings (a
// slider being dragged changes them every frame).
constexpr uint64_t kUnderrunLogMs = 5000;
constexpr uint64_t kSettingsLogMs = 1000;

std::string percent(float v) { return std::format("{} %", static_cast<int>(v * 100.0f + 0.5f)); }

} // namespace

struct Audio::Impl {
    SDL_AudioDeviceID device = 0;
    SDL_AudioSpec deviceSpec{};
    SDL_AudioStream* output = nullptr;  // the mix, bound to the device; its callback pulls from the mixer
    std::unique_ptr<audiomix::Mixer> mixer;
    std::array<float, kCallbackFrames * audiomix::kChannels> callbackBuffer{};  // the audio thread's

    const assets::InstallFiles* files = nullptr;
    AudioOptions options;
    bool optionsSet = false;
    std::string settingsLogged;  // the last settings line written to the log
    uint64_t settingsLogTicks = 0;
    std::map<std::string, std::shared_ptr<const audiomix::Clip>> clips;  // misses are cached too (nullptr)

    // Music: one track, looped by its decoder.
    std::string trackFile;
    bool musicPlaying = false;
    std::unique_ptr<audiomix::MusicTrack> track;
    bool trackReported = false;
    uint64_t trackLoops = 0;
    std::set<std::string> failedTracks;  // logged once, not tried again
    uint64_t underrunsLogged = 0, underrunLogTicks = 0;
    bool background = false;  // the game's window is in the background
    bool muted = false;       // and that mutes it

    // Main-thread calls into the mixer hold the output stream's lock, which the
    // audio thread holds while it mixes.
    struct Lock {
        SDL_AudioStream* s;
        explicit Lock(SDL_AudioStream* stream) : s(stream) { SDL_LockAudioStream(s); }
        ~Lock() { SDL_UnlockAudioStream(s); }
        Lock(const Lock&) = delete;
        Lock& operator=(const Lock&) = delete;
    };

    static void SDLCALL feed(void* user, SDL_AudioStream* stream, int additional, int /*total*/) {
        Impl& a = *static_cast<Impl*>(user);
        constexpr int kFrameBytes = static_cast<int>(sizeof(float)) * audiomix::kChannels;
        int frames = (additional + kFrameBytes - 1) / kFrameBytes;
        while (frames > 0) {
            const int n = std::min(frames, kCallbackFrames);
            a.mixer->mix(a.callbackBuffer.data(), static_cast<size_t>(n));
            SDL_PutAudioStreamData(stream, a.callbackBuffer.data(), n * kFrameBytes);
            frames -= n;
        }
    }

    // The settings in the log: at once, then at most once a second, and the
    // last ones always (update() writes what waited).
    void logSettings() {
        const AudioOptions& o = options;
        const std::string line =
            std::format("Audio settings: sound effects {}{}, music {}, {} in the background",
                        o.sound ? "on at " + percent(o.soundVolume) : std::string("off"),
                        o.sound ? (o.remastered ? " (the remastered set in Sounds/New)" : " (the classic set in Sounds)") : "",
                        o.music ? std::format("on at step {} of 5", musicStep(o.musicVolume)) : std::string("off"),
                        o.muteInBackground ? "muted" : "not muted");
        if (line == settingsLogged || SDL_GetTicks() < settingsLogTicks) return;
        log::info("{}", line);
        settingsLogged = line;
        settingsLogTicks = SDL_GetTicks() + kSettingsLogMs;
    }

    float musicGainNow() const { return options.music ? musicGain(musicStep(options.musicVolume)) : 0.0f; }
    float effectsGainNow() const { return options.sound ? options.soundVolume : 0.0f; }

    void applyGains() {
        if (!output) return;
        const Lock lock(output);
        mixer->setEffectsGain(effectsGainNow());
        mixer->setMusicGain(musicGainNow());
        if (!options.sound) mixer->stopEffects();
    }

    // Mutes or unmutes the mix as the window and the setting say.
    void applyMute() {
        const bool mute = background && options.muteInBackground;
        if (mute == muted) return;
        muted = mute;
        if (!output) return;
        log::info("{}", mute ? "Audio: muted: the game is in the background" : "Audio: unmuted: the game is in the foreground again");
        const Lock lock(output);
        mixer->setMuted(mute);
    }

    std::shared_ptr<const audiomix::Clip> clip(std::string_view name) {
        const std::string key = std::string(name) + (options.remastered ? "#new" : "#old");
        if (auto it = clips.find(key); it != clips.end()) return it->second;
        std::shared_ptr<const audiomix::Clip> loaded;
        const std::vector<std::string> candidates = soundCandidates(name, options.remastered);
        bool found = false;
        if (files)
            for (const std::string& candidate : candidates) {
                const auto path = files->find(candidate);
                if (!path) continue;
                found = true;
                std::vector<uint8_t> bytes;
                std::string why = audiomix::readFileBytes(*path, bytes);
                if (why.empty()) {
                    if (auto c = audiomix::decodeWav(bytes, mixer->rate(), why)) {
                        loaded = std::make_shared<const audiomix::Clip>(std::move(*c));
                        break;
                    }
                }
                log::warn("Sound: cannot play {}: {}", path->string(), why);
            }
        if (!found && files) files->noteMissing(candidates.empty() ? std::string(name) : candidates.front());
        clips.emplace(key, loaded);
        return loaded;
    }

    void dropTrack() {
        if (output) {
            const Lock lock(output);
            mixer->setMusic(nullptr);  // fades out what plays
        }
        track.reset();
    }
};

Audio::Audio() : impl_(std::make_unique<Impl>()) {}

Audio::~Audio() { close(); }

bool Audio::open() {
    Impl& a = *impl_;
    if (a.device) return true;
    if (!SDL_WasInit(SDL_INIT_AUDIO) && !SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        log::warn("Audio: no sound or music: SDL cannot start its audio ({})", SDL_GetError());
        return false;
    }
    // The device's own format, unless one of SDL's format hints (SDL_AUDIO_FREQUENCY,
    // SDL_AUDIO_FORMAT, SDL_AUDIO_CHANNELS) asks for another: SDL applies them only
    // to a blank request (docs/SETUP.md "Sound and music").
    const bool hinted = SDL_GetHint(SDL_HINT_AUDIO_FREQUENCY) || SDL_GetHint(SDL_HINT_AUDIO_FORMAT) || SDL_GetHint(SDL_HINT_AUDIO_CHANNELS);
    const SDL_AudioSpec blank{};
    a.device = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, hinted ? &blank : nullptr);
    if (!a.device) {
        log::warn("Audio: no sound or music: no audio device can be opened ({}, driver {})", SDL_GetError(),
                  SDL_GetCurrentAudioDriver() ? SDL_GetCurrentAudioDriver() : "none");
        return false;
    }
    int sampleFrames = 0;
    if (!SDL_GetAudioDeviceFormat(a.device, &a.deviceSpec, &sampleFrames)) a.deviceSpec = SDL_AudioSpec{SDL_AUDIO_F32, 2, 48000};
    // Mix at the device's rate (one conversion per source, done off the audio thread).
    const int rate = a.deviceSpec.freq >= 8000 && a.deviceSpec.freq <= 192000 ? a.deviceSpec.freq : 48000;
    a.mixer = std::make_unique<audiomix::Mixer>(rate);
    const SDL_AudioSpec mix{SDL_AUDIO_F32, audiomix::kChannels, rate};
    a.output = SDL_CreateAudioStream(&mix, &a.deviceSpec);
    if (!a.output || !SDL_SetAudioStreamGetCallback(a.output, &Impl::feed, &a) || !SDL_BindAudioStream(a.device, a.output)) {
        log::warn("Audio: no sound or music: the mix cannot be sent to the device ({})", SDL_GetError());
        close();
        return false;
    }
    const char* name = SDL_GetAudioDeviceName(a.device);
    log::info("Audio: {} through {}: {} Hz, {} channels, {}, {} frames a buffer; mixing at {} Hz", name ? name : "the default device",
              SDL_GetCurrentAudioDriver() ? SDL_GetCurrentAudioDriver() : "?", a.deviceSpec.freq, a.deviceSpec.channels,
              SDL_GetAudioFormatName(a.deviceSpec.format), sampleFrames, rate);
    a.applyGains();
    a.mixer->setMuted(a.muted);
    return true;
}

void Audio::close() {
    if (!impl_) return;
    Impl& a = *impl_;
    if (a.output) SDL_DestroyAudioStream(a.output);  // no callback after this
    a.output = nullptr;
    a.track.reset();
    a.mixer.reset();
    a.clips.clear();
    a.musicPlaying = false;
    a.trackFile.clear();
    if (a.device) SDL_CloseAudioDevice(a.device);
    a.device = 0;
}

bool Audio::active() const { return impl_->device != 0; }

void Audio::setInstall(const assets::InstallFiles* files) {
    impl_->files = files;
    impl_->clips.clear();
    impl_->failedTracks.clear();
}

void Audio::setOptions(const AudioOptions& options) {
    Impl& a = *impl_;
    AudioOptions o = options;
    o.soundVolume = std::clamp(o.soundVolume, 0.0f, 1.0f);
    o.musicVolume = std::clamp(o.musicVolume, 0.0f, 1.0f);
    const AudioOptions& old = a.options;
    const bool changed = !a.optionsSet || o.sound != old.sound || o.music != old.music || o.soundVolume != old.soundVolume ||
                         o.musicVolume != old.musicVolume || o.remastered != old.remastered || o.muteInBackground != old.muteInBackground;
    if (!changed) return;
    const bool musicWasOn = a.options.music;
    a.options = o;
    a.optionsSet = true;
    a.logSettings();
    a.applyGains();
    a.applyMute();
    if (musicWasOn && !o.music) stopMusic();
}

const AudioOptions& Audio::options() const { return impl_->options; }

void Audio::play(std::string_view name) {
    Impl& a = *impl_;
    if (!a.device || !a.options.sound || name.empty()) return;
    if (a.muted) {
        log::debug("Sound: {} (dropped: muted in the background)", name);
        return;
    }
    auto c = a.clip(name);
    if (!c) return;
    log::debug("Sound: {}", name);
    const Impl::Lock lock(a.output);
    a.mixer->playEffect(std::move(c));
}

void Audio::playTrack(const std::string& file) {
    Impl& a = *impl_;
    if (!a.device || file.empty()) return;
    if (a.musicPlaying && a.trackFile == file) return;  // already playing it
    if (a.failedTracks.contains(file)) return;          // logged when it failed
    const std::string relative = "Music/" + file;
    const auto path = a.files ? a.files->find(relative) : std::nullopt;
    if (!path) {
        a.failedTracks.insert(file);
        if (a.files) a.files->noteMissing(relative);
        log::warn("Music: cannot play {}: it is not in the installed game ({})", relative,
                  a.files ? (a.files->root() / "Music").string() : std::string("no install"));
        return;
    }
    // The new track decodes on its own thread; the old one fades out meanwhile.
    a.track = std::make_unique<audiomix::MusicTrack>(*path, a.mixer->rate());
    {
        const Impl::Lock lock(a.output);
        a.mixer->setMusic(a.track->ring());
    }
    a.trackFile = file;
    a.musicPlaying = true;
    a.trackReported = false;
    a.trackLoops = 0;
}

void Audio::stopMusic() {
    Impl& a = *impl_;
    if (!a.musicPlaying && !a.track) return;
    a.musicPlaying = false;
    if (!a.trackFile.empty()) log::info("Music: stopped Music/{}", a.trackFile);
    a.trackFile.clear();
    a.dropTrack();
}

bool Audio::musicPlaying() const { return impl_->musicPlaying; }

void Audio::setBackground(bool background) {
    Impl& a = *impl_;
    a.background = background;
    a.applyMute();
}

void Audio::update() {
    Impl& a = *impl_;
    if (a.optionsSet) a.logSettings();
    if (!a.device) return;
    if (a.track) {
        switch (a.track->state()) {
            case audiomix::MusicTrack::State::Opening: break;
            case audiomix::MusicTrack::State::Failed:
                log::warn("Music: cannot play Music/{}: {}", a.trackFile, a.track->error());
                a.failedTracks.insert(a.trackFile);
                a.musicPlaying = false;
                a.trackFile.clear();
                a.dropTrack();
                break;
            case audiomix::MusicTrack::State::Playing:
                if (!a.trackReported) {
                    const audiomix::Mp3Info& i = a.track->info();
                    log::info("Music: playing Music/{} ({} Hz, {} channel{}, {:.0f} s, looped)", a.trackFile, i.sampleRate, i.channels,
                              i.channels == 1 ? "" : "s", i.seconds);
                    a.trackReported = true;
                }
                if (const uint64_t loops = a.track->loops(); loops != a.trackLoops) {
                    log::debug("Music: Music/{} starts again", a.trackFile);
                    a.trackLoops = loops;
                }
                break;
        }
    }
    std::vector<std::shared_ptr<audiomix::FrameRing>> retired;
    uint64_t underruns = 0;
    {
        const Impl::Lock lock(a.output);
        retired = a.mixer->takeRetired();
        underruns = a.mixer->underruns();
    }
    retired.clear();  // freed here, not on the audio thread
    if (underruns > a.underrunsLogged && SDL_GetTicks() >= a.underrunLogTicks) {
        log::warn("Audio: the music ran dry {} time{} so far: its decoding fell behind (is the computer very busy?)", underruns,
                  underruns == 1 ? "" : "s");
        a.underrunsLogged = underruns;
        a.underrunLogTicks = SDL_GetTicks() + kUnderrunLogMs;
    }
}

Audio& audio() {
    static Audio instance;
    return instance;
}

} // namespace opense4::client
