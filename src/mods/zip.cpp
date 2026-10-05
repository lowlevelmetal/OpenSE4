#include "mods/zip.hpp"

#include <miniz.h>

#include <format>
#include <fstream>
#include <iterator>
#include <memory>
#include <system_error>

namespace opense4::mods {

namespace fs = std::filesystem;

bool safeEntryName(std::string_view name) {
    if (name.empty() || name.size() > 400 || name.front() == '/' || name.find('\\') != std::string_view::npos) return false;
    if (name.size() >= 2 && name[1] == ':') return false;  // a drive letter
    for (char c : name)
        if (static_cast<unsigned char>(c) < 0x20) return false;
    std::string_view rest = name;
    while (!rest.empty()) {
        const size_t slash = rest.find('/');
        const std::string_view part = rest.substr(0, slash);
        if (part == "." || part == "..") return false;
        if (part.empty() && slash != std::string_view::npos) return false;  // "a//b"
        if (slash == std::string_view::npos) break;
        rest.remove_prefix(slash + 1);
    }
    return true;
}

namespace {

// The archive in memory, read through a path as the rest of the program
// reads files (Unicode names on every platform).
struct Archive {
    std::string bytes;
    mz_zip_archive zip{};
    bool open = false;

    ~Archive() {
        if (open) mz_zip_reader_end(&zip);
    }
};

std::expected<std::unique_ptr<Archive>, std::string> openArchive(const fs::path& file) {
    std::error_code ec;
    const auto size = fs::file_size(file, ec);
    if (ec) return std::unexpected(std::format("{}: {}", file.string(), ec.message()));
    if (size > kMaxZipBytes) return std::unexpected(std::format("{}: the archive is larger than {} MiB", file.string(), kMaxZipBytes >> 20));
    auto a = std::make_unique<Archive>();
    std::ifstream in(file, std::ios::binary);
    a->bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    if (a->bytes.size() != size) return std::unexpected(std::format("{}: cannot read the archive", file.string()));
    mz_zip_zero_struct(&a->zip);
    if (!mz_zip_reader_init_mem(&a->zip, a->bytes.data(), a->bytes.size(), 0))
        return std::unexpected(std::format("{}: not a zip archive ({})", file.string(), mz_zip_get_error_string(mz_zip_get_last_error(&a->zip))));
    a->open = true;
    return a;
}

std::expected<std::vector<ZipEntry>, std::string> readEntries(const fs::path& file, Archive& a) {
    const mz_uint count = mz_zip_reader_get_num_files(&a.zip);
    if (count > kMaxZipEntries) return std::unexpected(std::format("{}: too many files in the archive ({})", file.string(), count));
    std::vector<ZipEntry> out;
    uint64_t total = 0;
    for (mz_uint i = 0; i < count; ++i) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&a.zip, i, &st)) return std::unexpected(std::format("{}: entry {} is damaged", file.string(), i + 1));
        ZipEntry e;
        e.name = st.m_filename;
        e.directory = st.m_is_directory != 0;
        e.size = st.m_uncomp_size;
        if (!st.m_is_supported || st.m_is_encrypted)
            return std::unexpected(std::format("{}: '{}' is encrypted or packed in a way that cannot be read", file.string(), e.name));
        std::string name = e.name;
        while (!name.empty() && name.back() == '/') name.pop_back();
        if (!safeEntryName(name)) return std::unexpected(std::format("{}: the archive holds an unsafe file name '{}'", file.string(), e.name));
        total += e.size;
        if (total > kMaxUnpackedBytes)
            return std::unexpected(std::format("{}: the archive unpacks to more than {} MiB", file.string(), kMaxUnpackedBytes >> 20));
        e.name = std::move(name);
        out.push_back(std::move(e));
    }
    return out;
}

} // namespace

std::expected<std::vector<ZipEntry>, std::string> listZip(const fs::path& zip) {
    auto a = openArchive(zip);
    if (!a) return std::unexpected(a.error());
    return readEntries(zip, **a);
}

std::expected<void, std::string> extractZip(const fs::path& zip, const fs::path& dest) {
    auto a = openArchive(zip);
    if (!a) return std::unexpected(a.error());
    auto list = readEntries(zip, **a);
    if (!list) return std::unexpected(list.error());
    std::error_code ec;
    fs::create_directories(dest, ec);
    if (ec) return std::unexpected(std::format("{}: {}", dest.string(), ec.message()));
    std::vector<char> buffer;
    for (mz_uint i = 0; i < list->size(); ++i) {
        const ZipEntry& e = (*list)[i];
        const fs::path target = dest / fs::path(e.name);
        if (e.directory) {
            fs::create_directories(target, ec);
            continue;
        }
        fs::create_directories(target.parent_path(), ec);
        buffer.resize(static_cast<size_t>(e.size));
        if (e.size > 0 && !mz_zip_reader_extract_to_mem(&(*a)->zip, i, buffer.data(), buffer.size(), 0))
            return std::unexpected(std::format("{}: cannot unpack '{}' ({})", zip.string(), e.name,
                                               mz_zip_get_error_string(mz_zip_get_last_error(&(*a)->zip))));
        std::ofstream out(target, std::ios::binary | std::ios::trunc);
        out.write(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        if (!out) return std::unexpected(std::format("{}: cannot write the file", target.string()));
    }
    return {};
}

std::expected<void, std::string> writeZip(const fs::path& out, std::span<const ZipInput> entries) {
    mz_zip_archive zip;
    mz_zip_zero_struct(&zip);
    if (!mz_zip_writer_init_heap(&zip, 0, 1 << 16)) return std::unexpected(std::string("cannot start the archive"));
    for (const ZipInput& e : entries) {
        if (!safeEntryName(e.name)) {
            mz_zip_writer_end(&zip);
            return std::unexpected(std::format("'{}' cannot be a name in a zip archive", e.name));
        }
        if (!mz_zip_writer_add_mem(&zip, e.name.c_str(), e.bytes.data(), e.bytes.size(), MZ_BEST_COMPRESSION)) {
            const std::string why = mz_zip_get_error_string(mz_zip_get_last_error(&zip));
            mz_zip_writer_end(&zip);
            return std::unexpected(std::format("cannot add '{}' to the archive ({})", e.name, why));
        }
    }
    void* data = nullptr;
    size_t size = 0;
    if (!mz_zip_writer_finalize_heap_archive(&zip, &data, &size)) {
        mz_zip_writer_end(&zip);
        return std::unexpected(std::string("cannot finish the archive"));
    }
    mz_zip_writer_end(&zip);
    std::error_code ec;
    if (out.has_parent_path()) fs::create_directories(out.parent_path(), ec);
    fs::path tmp = out;
    tmp += ".tmp";
    {
        std::ofstream file(tmp, std::ios::binary | std::ios::trunc);
        file.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
        mz_free(data);
        if (!file) return std::unexpected(std::format("{}: cannot write the file", tmp.string()));
    }
    fs::rename(tmp, out, ec);
    if (ec) return std::unexpected(std::format("{}: {}", out.string(), ec.message()));
    return {};
}

} // namespace opense4::mods
