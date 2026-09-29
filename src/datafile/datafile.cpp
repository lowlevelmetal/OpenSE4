#include "datafile/datafile.hpp"

#include <fstream>
#include <sstream>

namespace opense4::datafile {

namespace {

std::string_view trim(std::string_view s) {
    const auto isSpace = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    while (!s.empty() && isSpace(s.front())) s.remove_prefix(1);
    while (!s.empty() && isSpace(s.back())) s.remove_suffix(1);
    return s;
}

char lower(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

} // namespace

bool keysEqual(std::string_view a, std::string_view b) {
    a = trim(a);
    b = trim(b);
    size_t i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        const bool spaceA = a[i] == ' ' || a[i] == '\t';
        const bool spaceB = b[j] == ' ' || b[j] == '\t';
        if (spaceA && spaceB) {
            while (i < a.size() && (a[i] == ' ' || a[i] == '\t')) ++i;
            while (j < b.size() && (b[j] == ' ' || b[j] == '\t')) ++j;
            continue;
        }
        if (lower(a[i]) != lower(b[j])) return false;
        ++i;
        ++j;
    }
    return i == a.size() && j == b.size();
}

std::string normalizeKey(std::string_view key) {
    key = trim(key);
    std::string out;
    out.reserve(key.size());
    bool space = false;
    for (char c : key) {
        if (c == ' ' || c == '\t') {
            space = true;
            continue;
        }
        if (space && !out.empty()) out += ' ';
        space = false;
        out += lower(c);
    }
    return out;
}

std::string latin1ToUtf8(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (char ch : text) {
        const auto c = static_cast<unsigned char>(ch);
        if (c < 0x80) {
            out += ch;
        } else {
            out += static_cast<char>(0xC0 | (c >> 6));
            out += static_cast<char>(0x80 | (c & 0x3F));
        }
    }
    return out;
}

const Field* Record::find(std::string_view key) const {
    for (const Field& f : fields)
        if (keysEqual(f.key, key)) return &f;
    return nullptr;
}

DataFile parse(std::string_view text, std::string name) {
    DataFile file;
    file.name = std::move(name);

    // Split into lines, tolerating CRLF and a missing final newline.
    std::vector<std::string_view> lines;
    for (size_t start = 0; start <= text.size();) {
        const size_t end = text.find('\n', start);
        const size_t stop = end == std::string_view::npos ? text.size() : end;
        lines.push_back(text.substr(start, stop - start));
        if (end == std::string_view::npos) break;
        start = end + 1;
    }

    size_t begin = lines.size();
    for (size_t i = 0; i < lines.size(); ++i)
        if (trim(lines[i]).starts_with("*BEGIN*")) {
            begin = i + 1;
            break;
        }

    if (begin == lines.size()) {
        for (std::string_view l : lines)
            if (auto t = trim(l); !t.empty()) file.entries.push_back(latin1ToUtf8(t));
        return file;
    }

    file.hasDataSection = true;
    std::string firstKey;
    for (size_t i = begin; i < lines.size(); ++i) {
        const std::string_view line = lines[i];
        if (trim(line).starts_with("*END*")) break;
        const size_t sep = line.find(":=");
        if (sep == std::string_view::npos) continue;
        Field field{latin1ToUtf8(trim(line.substr(0, sep))), latin1ToUtf8(trim(line.substr(sep + 2))), static_cast<int>(i + 1)};
        if (field.key.empty()) continue;
        if (firstKey.empty()) firstKey = field.key;
        if (file.records.empty() || keysEqual(field.key, firstKey)) file.records.push_back(Record{{}, field.line});
        file.records.back().fields.push_back(std::move(field));
    }
    return file;
}

std::expected<DataFile, std::string> load(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::unexpected("cannot open " + path.string());
    std::ostringstream ss;
    ss << in.rdbuf();
    return parse(ss.str(), path.filename().string());
}

} // namespace opense4::datafile
