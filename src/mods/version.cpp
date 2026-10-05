#include "mods/version.hpp"

#include <algorithm>
#include <charconv>
#include <format>

namespace opense4::mods {

std::strong_ordering operator<=>(const Version& a, const Version& b) {
    const size_t n = std::max(a.parts.size(), b.parts.size());
    for (size_t i = 0; i < n; ++i) {
        const uint32_t x = i < a.parts.size() ? a.parts[i] : 0;
        const uint32_t y = i < b.parts.size() ? b.parts[i] : 0;
        if (x != y) return x <=> y;
    }
    return std::strong_ordering::equal;
}

std::optional<Version> parseVersion(std::string_view text) {
    Version v;
    v.text = std::string(text);
    if (text.empty()) return std::nullopt;
    std::string_view rest = text;
    while (true) {
        const size_t dot = rest.find('.');
        const std::string_view part = rest.substr(0, dot);
        uint32_t number = 0;
        if (part.empty() || part.size() > 9) return std::nullopt;
        const auto [ptr, ec] = std::from_chars(part.data(), part.data() + part.size(), number);
        if (ec != std::errc{} || ptr != part.data() + part.size()) return std::nullopt;
        v.parts.push_back(number);
        if (dot == std::string_view::npos) break;
        rest.remove_prefix(dot + 1);
    }
    if (v.parts.size() > 4) return std::nullopt;
    return v;
}

bool VersionRange::contains(const Version& v) const {
    for (const Bound& b : bounds) {
        const auto c = v <=> b.version;
        switch (b.op) {
            case Op::Equal:
                if (c != 0) return false;
                break;
            case Op::Greater:
                if (c <= 0) return false;
                break;
            case Op::GreaterEqual:
                if (c < 0) return false;
                break;
            case Op::Less:
                if (c >= 0) return false;
                break;
            case Op::LessEqual:
                if (c > 0) return false;
                break;
            case Op::Prefix:
                for (size_t i = 0; i < b.version.parts.size(); ++i)
                    if ((i < v.parts.size() ? v.parts[i] : 0) != b.version.parts[i]) return false;
                break;
        }
    }
    return true;
}

std::expected<VersionRange, std::string> parseVersionRange(std::string_view text) {
    VersionRange range;
    range.text = std::string(text);
    auto bad = [&](std::string_view why) {
        return std::unexpected(std::format("version range '{}': {} (write for example \">=1.0\", \">=1.0, <2\" or \"1.2\")", text, why));
    };
    std::string_view rest = text;
    bool any = false;
    while (true) {
        while (!rest.empty() && (rest.front() == ' ' || rest.front() == ',' || rest.front() == '\t')) rest.remove_prefix(1);
        if (rest.empty()) break;
        size_t end = 0;
        // An operator, then a version up to the next separator.
        size_t opLength = 0;
        VersionRange::Op op = VersionRange::Op::Prefix;
        bool caret = false, tilde = false;
        if (rest.starts_with(">=")) op = VersionRange::Op::GreaterEqual, opLength = 2;
        else if (rest.starts_with("<=")) op = VersionRange::Op::LessEqual, opLength = 2;
        else if (rest.starts_with("==")) op = VersionRange::Op::Equal, opLength = 2;
        else if (rest.starts_with(">")) op = VersionRange::Op::Greater, opLength = 1;
        else if (rest.starts_with("<")) op = VersionRange::Op::Less, opLength = 1;
        else if (rest.starts_with("=")) op = VersionRange::Op::Equal, opLength = 1;
        else if (rest.starts_with("^")) caret = true, opLength = 1;
        else if (rest.starts_with("~")) tilde = true, opLength = 1;
        rest.remove_prefix(opLength);
        while (!rest.empty() && rest.front() == ' ') rest.remove_prefix(1);
        while (end < rest.size() && rest[end] != ' ' && rest[end] != ',' && rest[end] != '\t') ++end;
        const std::string_view word = rest.substr(0, end);
        rest.remove_prefix(end);
        if (word == "*" && opLength == 0) {
            any = true;
            continue;
        }
        auto v = parseVersion(word);
        if (!v) return bad(word.empty() ? std::string_view("an operator without a version") : std::string_view("not a version"));
        if (caret || tilde) {
            // ^1.2: from 1.2 below 2; ~1.2 and ~1.2.3: below 1.3; ~1: below 2.
            Version upper;
            const size_t keep = caret || v->parts.size() == 1 ? 1 : 2;
            upper.parts.assign(v->parts.begin(), v->parts.begin() + static_cast<std::ptrdiff_t>(keep));
            upper.parts.back() += 1;
            range.bounds.push_back({VersionRange::Op::GreaterEqual, *v});
            range.bounds.push_back({VersionRange::Op::Less, upper});
            continue;
        }
        range.bounds.push_back({op, *v});
    }
    if (range.bounds.empty() && !any) return bad("it is empty");
    return range;
}

} // namespace opense4::mods
