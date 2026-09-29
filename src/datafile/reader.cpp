#include "datafile/reader.hpp"

#include <charconv>

namespace opense4::datafile {

std::optional<int64_t> parseInteger(std::string_view text) {
    if (!text.empty() && text.front() == '+') text.remove_prefix(1);
    int64_t value = 0;
    const auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (ec != std::errc{} || ptr != text.data() + text.size()) return std::nullopt;
    return value;
}

std::optional<bool> parseBoolean(std::string_view text) {
    if (keysEqual(text, "true") || text == "1") return true;
    if (keysEqual(text, "false") || text == "0") return false;
    return std::nullopt;
}

RecordReader::RecordReader(const DataFile& file, const Record& record, Diagnostics& diag)
    : file_(file), record_(record), diag_(diag), read_(record.fields.size(), false) {}

RecordReader::~RecordReader() {
    for (size_t i = 0; i < record_.fields.size(); ++i)
        if (!read_[i]) ++diag_.unreadFields[std::format("{}: {}", file_.name, record_.fields[i].key)];
}

std::string RecordReader::context() const {
    const std::string& label = record_.fields.empty() ? std::string{} : record_.fields.front().value;
    return std::format("{}:{} [{}]", file_.name, record_.line, label);
}

void RecordReader::error(std::string_view message) const { diag_.errors.push_back(std::format("{}: {}", context(), message)); }
void RecordReader::warn(std::string_view message) const { diag_.warnings.push_back(std::format("{}: {}", context(), message)); }

bool RecordReader::has(std::string_view key) const { return record_.find(key) != nullptr; }

const Field* RecordReader::take(std::string_view key, Need need) {
    for (size_t i = 0; i < record_.fields.size(); ++i) {
        if (keysEqual(record_.fields[i].key, key)) {
            read_[i] = true;
            return &record_.fields[i];
        }
    }
    if (need == Need::Required) error(std::format("missing field '{}'", key));
    return nullptr;
}

std::string RecordReader::str(std::string_view key, Need need) {
    const Field* f = take(key, need);
    return f ? f->value : std::string{};
}

int64_t RecordReader::integer(std::string_view key, Need need, int64_t fallback) {
    const Field* f = take(key, need);
    if (!f) return fallback;
    if (f->value.empty()) return fallback;  // blank numbers mean "not used" in these files
    if (auto v = parseInteger(f->value)) return *v;
    diag_.errors.push_back(std::format("{}:{}: '{}' should be a whole number, not '{}'", file_.name, f->line, f->key, f->value));
    return fallback;
}

int RecordReader::int32(std::string_view key, Need need, int fallback) {
    const int64_t v = integer(key, need, fallback);
    if (v < INT32_MIN || v > INT32_MAX) {
        error(std::format("'{}' is out of range", key));
        return fallback;
    }
    return static_cast<int>(v);
}

bool RecordReader::boolean(std::string_view key, Need need, bool fallback) {
    const Field* f = take(key, need);
    if (!f || f->value.empty()) return fallback;
    if (auto v = parseBoolean(f->value)) return *v;
    diag_.errors.push_back(std::format("{}:{}: '{}' should be True or False, not '{}'", file_.name, f->line, f->key, f->value));
    return fallback;
}

std::vector<std::string> RecordReader::list(std::string_view key, char separator, Need need) {
    std::vector<std::string> out;
    const Field* f = take(key, need);
    if (!f) return out;
    std::string_view rest = f->value;
    while (!rest.empty()) {
        const size_t pos = rest.find(separator);
        std::string_view item = rest.substr(0, pos);
        while (!item.empty() && item.front() == ' ') item.remove_prefix(1);
        while (!item.empty() && item.back() == ' ') item.remove_suffix(1);
        if (!item.empty()) out.emplace_back(item);
        if (pos == std::string_view::npos) break;
        rest.remove_prefix(pos + 1);
    }
    return out;
}

std::vector<std::pair<std::string, std::string>> RecordReader::remaining() {
    std::vector<std::pair<std::string, std::string>> out;
    for (size_t i = 0; i < record_.fields.size(); ++i) {
        if (read_[i]) continue;
        read_[i] = true;
        out.emplace_back(record_.fields[i].key, record_.fields[i].value);
    }
    return out;
}

std::vector<int> RecordReader::intList(std::string_view key, Need need) {
    std::vector<int> out;
    const Field* f = take(key, need);
    if (!f) return out;
    std::string_view rest = f->value;
    while (!rest.empty()) {
        while (!rest.empty() && (rest.front() == ' ' || rest.front() == '\t' || rest.front() == ',')) rest.remove_prefix(1);
        size_t n = 0;
        while (n < rest.size() && rest[n] != ' ' && rest[n] != '\t' && rest[n] != ',') ++n;
        if (n == 0) break;
        if (auto v = parseInteger(rest.substr(0, n)); v && *v >= INT32_MIN && *v <= INT32_MAX) {
            out.push_back(static_cast<int>(*v));
        } else {
            diag_.errors.push_back(std::format("{}:{}: '{}' has a non-numeric entry '{}'", file_.name, f->line, f->key, rest.substr(0, n)));
        }
        rest.remove_prefix(n);
    }
    return out;
}

} // namespace opense4::datafile
