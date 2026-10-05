#include "datafile/datafile.hpp"

#include <algorithm>
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

std::string utf8ToLatin1(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size();) {
        const auto c = static_cast<unsigned char>(text[i]);
        if (c < 0x80) {
            out += text[i++];
            continue;
        }
        // A two-byte sequence of U+0080..U+00FF maps back; anything else is '?'.
        if ((c == 0xC2 || c == 0xC3) && i + 1 < text.size() && (static_cast<unsigned char>(text[i + 1]) & 0xC0) == 0x80) {
            out += static_cast<char>(((c & 0x03) << 6) | (static_cast<unsigned char>(text[i + 1]) & 0x3F));
            i += 2;
            continue;
        }
        size_t length = 1;
        if ((c & 0xE0) == 0xC0) length = 2;
        else if ((c & 0xF0) == 0xE0) length = 3;
        else if ((c & 0xF8) == 0xF0) length = 4;
        out += '?';
        i += std::min(length, text.size() - i);
    }
    return out;
}

std::string keyPattern(std::string_view key) {
    const std::string normal = normalizeKey(key);
    std::string out;
    out.reserve(normal.size());
    for (size_t i = 0; i < normal.size(); ++i) {
        if (normal[i] >= '0' && normal[i] <= '9') {
            if (out.empty() || out.back() != '#') out += '#';
            continue;
        }
        out += normal[i];
    }
    return out;
}

std::string write(const DataFile& file, std::string_view header) {
    std::string out;
    auto line = [&](std::string_view text) {
        out += utf8ToLatin1(text);
        out += "\r\n";
    };
    if (!file.hasDataSection) {
        for (const std::string& entry : file.entries) line(entry);
        return out;
    }
    if (!header.empty()) line(header);
    line("*BEGIN*");
    for (const Record& record : file.records) {
        line("");
        for (const Field& field : record.fields) line(field.value.empty() ? field.key + " :=" : field.key + " := " + field.value);
    }
    line("");
    line("*END*");
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
        std::string_view raw = line.substr(sep + 2);
        if (!raw.empty() && raw.back() == '\r') raw.remove_suffix(1);
        Field field{latin1ToUtf8(trim(line.substr(0, sep))), latin1ToUtf8(trim(raw)), static_cast<int>(i + 1), latin1ToUtf8(raw)};
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
