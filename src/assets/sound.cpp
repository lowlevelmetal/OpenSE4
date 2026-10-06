#include "assets/sound.hpp"

#include <algorithm>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <format>

// stb_vorbis's declarations; its implementation is assets/vorbis_impl.cpp.
#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#define STB_VORBIS_HEADER_ONLY
#include <stb_vorbis.c>

namespace opense4::assets {

bool isOgg(std::span<const uint8_t> b) { return b.size() >= 4 && b[0] == 'O' && b[1] == 'g' && b[2] == 'g' && b[3] == 'S'; }

bool isWav(std::span<const uint8_t> b) {
    return b.size() >= 12 && std::memcmp(b.data(), "RIFF", 4) == 0 && std::memcmp(b.data() + 8, "WAVE", 4) == 0;
}

namespace {

std::string vorbisError(int code) {
    switch (code) {
        case VORBIS_outofmem: return "out of memory";
        case VORBIS_missing_capture_pattern:
        case VORBIS_invalid_first_page: return "it is not an OGG file";
        case VORBIS_invalid_setup:
        case VORBIS_invalid_stream_structure_version:
        case VORBIS_invalid_stream: return "it is not an OGG Vorbis stream (another codec, or damaged)";
        case VORBIS_unexpected_eof: return "the file ends too soon";
        case VORBIS_feature_not_supported: return "it uses a feature of Vorbis that cannot be decoded";
        default: return std::format("it cannot be decoded (error {})", code);
    }
}

stb_vorbis* openVorbis(std::span<const uint8_t> bytes, std::string& why) {
    if (!isOgg(bytes)) {
        why = "it is not an OGG file";
        return nullptr;
    }
    if (bytes.size() > static_cast<size_t>(INT_MAX)) {
        why = "the file is too large";
        return nullptr;
    }
    int error = 0;
    stb_vorbis* v = stb_vorbis_open_memory(bytes.data(), static_cast<int>(bytes.size()), &error, nullptr);
    if (!v) why = vorbisError(error);
    return v;
}

SoundInfo infoOf(stb_vorbis* v) {
    const stb_vorbis_info i = stb_vorbis_get_info(v);
    return {static_cast<int>(i.sample_rate), i.channels, static_cast<double>(stb_vorbis_stream_length_in_seconds(v))};
}

} // namespace

std::expected<SoundInfo, std::string> probeOgg(std::span<const uint8_t> bytes) {
    std::string why;
    stb_vorbis* v = openVorbis(bytes, why);
    if (!v) return std::unexpected(why);
    const SoundInfo info = infoOf(v);
    stb_vorbis_close(v);
    if (info.sampleRate <= 0 || info.channels <= 0) return std::unexpected(std::string("it holds no sound"));
    return info;
}

std::expected<Pcm16, std::string> decodeOgg(std::span<const uint8_t> bytes) {
    std::string why;
    stb_vorbis* v = openVorbis(bytes, why);
    if (!v) return std::unexpected(why);
    Pcm16 pcm;
    const SoundInfo info = infoOf(v);
    pcm.sampleRate = info.sampleRate;
    pcm.channels = info.channels;
    std::vector<int16_t> chunk(4096 * static_cast<size_t>(std::max(1, pcm.channels)));
    for (;;) {
        const int n = stb_vorbis_get_samples_short_interleaved(v, pcm.channels, chunk.data(), static_cast<int>(chunk.size()));
        if (n <= 0) break;
        pcm.samples.insert(pcm.samples.end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(n) * pcm.channels);
    }
    stb_vorbis_close(v);
    if (pcm.sampleRate <= 0 || pcm.channels <= 0 || pcm.samples.empty()) return std::unexpected(std::string("it holds no sound"));
    return pcm;
}

struct OggStream::Decoder {
    stb_vorbis* v = nullptr;
};

std::expected<std::unique_ptr<OggStream>, std::string> OggStream::open(std::span<const uint8_t> bytes) {
    std::string why;
    stb_vorbis* v = openVorbis(bytes, why);
    if (!v) return std::unexpected(why);
    std::unique_ptr<OggStream> s(new OggStream());
    s->decoder_ = new Decoder{v};
    s->info_ = infoOf(v);
    if (s->info_.sampleRate <= 0 || s->info_.channels <= 0) return std::unexpected(std::string("it holds no sound"));
    return s;
}

OggStream::~OggStream() {
    if (decoder_) stb_vorbis_close(decoder_->v);
    delete decoder_;
}

size_t OggStream::read(int16_t* out, size_t frames) {
    const int channels = info_.channels;
    const auto shorts = static_cast<int>(std::min<size_t>(frames * static_cast<size_t>(channels), static_cast<size_t>(INT_MAX)));
    const int n = stb_vorbis_get_samples_short_interleaved(decoder_->v, channels, out, shorts);
    return n > 0 ? static_cast<size_t>(n) : 0;
}

bool OggStream::rewind() { return stb_vorbis_seek_start(decoder_->v) != 0; }

} // namespace opense4::assets
