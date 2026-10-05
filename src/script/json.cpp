#include "script/json.hpp"

#include <cstdint>
#include <format>

namespace opense4::script {

std::string JsonError::describe() const {
    return std::format("line {}, column {}: {}", line, column, message);
}

namespace {

class Parser {
public:
    Parser(std::string_view text, int maxDepth) : text_(text), maxDepth_(maxDepth) {}

    std::expected<Value, JsonError> parse() {
        if (text_.starts_with("\xEF\xBB\xBF")) return fail("a byte order mark (JSON text starts directly)");
        skipSpace();
        Value v;
        if (!value(v, 0)) return std::unexpected(error_);
        skipSpace();
        if (pos_ < text_.size()) return fail("more text after the value");
        return v;
    }

private:
    std::string_view text_;
    size_t pos_ = 0;
    int maxDepth_;
    JsonError error_;

    std::unexpected<JsonError> fail(std::string message) {
        setError(std::move(message));
        return std::unexpected(error_);
    }

    bool setError(std::string message) {
        error_.message = std::move(message);
        error_.offset = pos_;
        error_.line = 1;
        error_.column = 1;
        for (size_t i = 0; i < pos_ && i < text_.size(); ++i) {
            if (text_[i] == '\n') {
                ++error_.line;
                error_.column = 1;
            } else {
                ++error_.column;
            }
        }
        return false;
    }

    void skipSpace() {
        while (pos_ < text_.size() && (text_[pos_] == ' ' || text_[pos_] == '\t' || text_[pos_] == '\n' || text_[pos_] == '\r'))
            ++pos_;
    }

    bool literal(std::string_view word) {
        if (text_.substr(pos_, word.size()) != word) return setError("an unknown word (expected a value)");
        pos_ += word.size();
        return true;
    }

    bool value(Value& out, int depth) {
        if (pos_ >= text_.size()) return setError("the text ends where a value should be");
        switch (text_[pos_]) {
        case 'n':
            if (!literal("null")) return false;
            out = Value();
            return true;
        case 't':
            if (!literal("true")) return false;
            out = Value(true);
            return true;
        case 'f':
            if (!literal("false")) return false;
            out = Value(false);
            return true;
        case '"': {
            std::string s;
            if (!string(s)) return false;
            out = Value(std::move(s));
            return true;
        }
        case '[': return array(out, depth);
        case '{': return object(out, depth);
        default:
            if (text_[pos_] == '-' || (text_[pos_] >= '0' && text_[pos_] <= '9')) return number(out);
            return setError("an unexpected character (expected a value)");
        }
    }

    bool number(Value& out) {
        size_t start = pos_;
        bool negative = false;
        if (text_[pos_] == '-') {
            negative = true;
            ++pos_;
        }
        if (pos_ >= text_.size() || text_[pos_] < '0' || text_[pos_] > '9') return setError("a '-' without digits");
        if (text_[pos_] == '0' && pos_ + 1 < text_.size() && text_[pos_ + 1] >= '0' && text_[pos_ + 1] <= '9')
            return setError("a number with a leading zero");
        // The magnitude, up to 2^63 (the most negative int64).
        uint64_t magnitude = 0;
        constexpr uint64_t limit = uint64_t{1} << 63;
        while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') {
            auto digit = static_cast<uint64_t>(text_[pos_] - '0');
            if (magnitude > (limit - digit) / 10) {
                pos_ = start;
                return setError("a number that doesn't fit in 64 bits");
            }
            magnitude = magnitude * 10 + digit;
            ++pos_;
        }
        if (pos_ < text_.size() && (text_[pos_] == '.' || text_[pos_] == 'e' || text_[pos_] == 'E'))
            return setError(text_[pos_] == '.' ? "a number with a fraction (only whole numbers)"
                                               : "a number with an exponent (only whole numbers)");
        if (!negative && magnitude == limit) {
            pos_ = start;
            return setError("a number that doesn't fit in 64 bits");
        }
        out = Value(negative ? (magnitude == limit ? INT64_MIN : -static_cast<int64_t>(magnitude)) : static_cast<int64_t>(magnitude));
        return true;
    }

    static int hexDigit(char c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    }

    bool hex4(uint32_t& v) {
        v = 0;
        for (int i = 0; i < 4; ++i) {
            if (pos_ >= text_.size()) return setError("the text ends inside a \\u escape");
            int d = hexDigit(text_[pos_]);
            if (d < 0) return setError("a \\u escape needs four hex digits");
            v = v * 16 + static_cast<uint32_t>(d);
            ++pos_;
        }
        return true;
    }

    static void appendUtf8(std::string& s, uint32_t cp) {
        if (cp < 0x80) {
            s += static_cast<char>(cp);
        } else if (cp < 0x800) {
            s += static_cast<char>(0xC0 | (cp >> 6));
            s += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            s += static_cast<char>(0xE0 | (cp >> 12));
            s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            s += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            s += static_cast<char>(0xF0 | (cp >> 18));
            s += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            s += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }

    // One UTF-8 character of raw text, checked.
    bool rawChar(std::string& s) {
        auto c = static_cast<unsigned char>(text_[pos_]);
        size_t n = 0;
        uint32_t cp = 0;
        if (c < 0x80) {
            s += static_cast<char>(c);
            ++pos_;
            return true;
        }
        if ((c & 0xE0) == 0xC0) {
            n = 1;
            cp = c & 0x1Fu;
        } else if ((c & 0xF0) == 0xE0) {
            n = 2;
            cp = c & 0x0Fu;
        } else if ((c & 0xF8) == 0xF0) {
            n = 3;
            cp = c & 0x07u;
        } else {
            return setError("text that is not valid UTF-8");
        }
        if (text_.size() - pos_ <= n) return setError("text that is not valid UTF-8");
        for (size_t k = 1; k <= n; ++k) {
            auto d = static_cast<unsigned char>(text_[pos_ + k]);
            if ((d & 0xC0) != 0x80) return setError("text that is not valid UTF-8");
            cp = (cp << 6) | (d & 0x3Fu);
        }
        if ((n == 1 && cp < 0x80) || (n == 2 && cp < 0x800) || (n == 3 && cp < 0x10000) || cp > 0x10FFFF ||
            (cp >= 0xD800 && cp <= 0xDFFF))
            return setError("text that is not valid UTF-8");
        s.append(text_.substr(pos_, n + 1));
        pos_ += n + 1;
        return true;
    }

    bool string(std::string& s) {
        ++pos_;   // the opening quote
        while (true) {
            if (pos_ >= text_.size()) return setError("the text ends inside a string");
            char c = text_[pos_];
            if (c == '"') {
                ++pos_;
                return true;
            }
            if (static_cast<unsigned char>(c) < 0x20) return setError("a control character in a string (it must be escaped)");
            if (c != '\\') {
                if (!rawChar(s)) return false;
                continue;
            }
            ++pos_;
            if (pos_ >= text_.size()) return setError("the text ends inside an escape");
            char e = text_[pos_++];
            switch (e) {
            case '"': s += '"'; break;
            case '\\': s += '\\'; break;
            case '/': s += '/'; break;
            case 'b': s += '\b'; break;
            case 'f': s += '\f'; break;
            case 'n': s += '\n'; break;
            case 'r': s += '\r'; break;
            case 't': s += '\t'; break;
            case 'u': {
                uint32_t cp = 0;
                if (!hex4(cp)) return false;
                if (cp >= 0xDC00 && cp <= 0xDFFF) return setError("a \\u escape with a lone low surrogate");
                if (cp >= 0xD800 && cp <= 0xDBFF) {
                    if (text_.substr(pos_, 2) != "\\u") return setError("a \\u escape with a high surrogate and no low one");
                    pos_ += 2;
                    uint32_t low = 0;
                    if (!hex4(low)) return false;
                    if (low < 0xDC00 || low > 0xDFFF) return setError("a \\u escape with a high surrogate and no low one");
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                }
                appendUtf8(s, cp);
                break;
            }
            default: --pos_; return setError("an unknown escape in a string");
            }
        }
    }

    bool array(Value& out, int depth) {
        if (depth + 1 > maxDepth_) return setError(std::format("values nested more than {} deep", maxDepth_));
        ++pos_;
        ValueList list;
        skipSpace();
        if (pos_ < text_.size() && text_[pos_] == ']') {
            ++pos_;
            out = Value(std::move(list));
            return true;
        }
        while (true) {
            skipSpace();
            Value item;
            if (!value(item, depth + 1)) return false;
            list.push_back(std::move(item));
            skipSpace();
            if (pos_ >= text_.size()) return setError("the text ends inside a list");
            if (text_[pos_] == ',') {
                ++pos_;
                skipSpace();
                if (pos_ < text_.size() && text_[pos_] == ']') return setError("a comma before ']'");
                continue;
            }
            if (text_[pos_] == ']') {
                ++pos_;
                out = Value(std::move(list));
                return true;
            }
            return setError("expected ',' or ']' in a list");
        }
    }

    bool object(Value& out, int depth) {
        if (depth + 1 > maxDepth_) return setError(std::format("values nested more than {} deep", maxDepth_));
        ++pos_;
        ValueMap map;
        skipSpace();
        if (pos_ < text_.size() && text_[pos_] == '}') {
            ++pos_;
            out = Value(std::move(map));
            return true;
        }
        while (true) {
            skipSpace();
            if (pos_ >= text_.size() || text_[pos_] != '"') return setError("expected a key in quotes");
            size_t keyAt = pos_;
            std::string key;
            if (!string(key)) return false;
            for (const auto& [k, v] : map) {
                if (k == key) {
                    pos_ = keyAt;
                    return setError(std::format("the key \"{}\" appears twice", key));
                }
            }
            skipSpace();
            if (pos_ >= text_.size() || text_[pos_] != ':') return setError("expected ':' after a key");
            ++pos_;
            skipSpace();
            Value item;
            if (!value(item, depth + 1)) return false;
            map.emplace_back(std::move(key), std::move(item));
            skipSpace();
            if (pos_ >= text_.size()) return setError("the text ends inside an object");
            if (text_[pos_] == ',') {
                ++pos_;
                skipSpace();
                if (pos_ < text_.size() && text_[pos_] == '}') return setError("a comma before '}'");
                continue;
            }
            if (text_[pos_] == '}') {
                ++pos_;
                out = Value(std::move(map));
                return true;
            }
            return setError("expected ',' or '}' in an object");
        }
    }
};

bool writeString(std::string& out, std::string_view s, std::string& error, const std::string& where) {
    out += '"';
    size_t i = 0;
    while (i < s.size()) {
        auto c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) {
            switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20)
                    out += std::format("\\u{:04x}", c);
                else
                    out += static_cast<char>(c);
            }
            ++i;
            continue;
        }
        size_t n = (c & 0xE0) == 0xC0 ? 1 : (c & 0xF0) == 0xE0 ? 2 : (c & 0xF8) == 0xF0 ? 3 : 0;
        uint32_t cp = n == 1 ? (c & 0x1Fu) : n == 2 ? (c & 0x0Fu) : (c & 0x07u);
        bool ok = n != 0 && s.size() - i > n;
        for (size_t k = 1; ok && k <= n; ++k) {
            auto d = static_cast<unsigned char>(s[i + k]);
            ok = (d & 0xC0) == 0x80;
            cp = (cp << 6) | (d & 0x3Fu);
        }
        ok = ok && !((n == 1 && cp < 0x80) || (n == 2 && cp < 0x800) || (n == 3 && cp < 0x10000) || cp > 0x10FFFF ||
                     (cp >= 0xD800 && cp <= 0xDFFF));
        if (!ok) {
            error = std::format("{} is not valid UTF-8 text", where);
            return false;
        }
        out.append(s.substr(i, n + 1));
        i += n + 1;
    }
    out += '"';
    return true;
}

bool write(std::string& out, const Value& v, bool pretty, int indent, std::string& error, std::string& where) {
    auto newline = [&](int level) {
        if (!pretty) return;
        out += '\n';
        out.append(static_cast<size_t>(level) * 2, ' ');
    };
    switch (v.kind()) {
    case Kind::Null: out += "null"; return true;
    case Kind::Bool: out += v.asBool() ? "true" : "false"; return true;
    case Kind::Int: out += std::format("{}", v.asInt()); return true;
    case Kind::String: return writeString(out, v.asString(), error, where);
    case Kind::List: {
        const ValueList& list = v.asList();
        out += '[';
        for (size_t i = 0; i < list.size(); ++i) {
            if (i) out += ',';
            newline(indent + 1);
            size_t mark = where.size();
            where += std::format("[{}]", i);
            if (!write(out, list[i], pretty, indent + 1, error, where)) return false;
            where.resize(mark);
        }
        if (!list.empty()) newline(indent);
        out += ']';
        return true;
    }
    case Kind::Map: {
        const ValueMap& map = v.asMap();
        out += '{';
        bool first = true;
        for (const auto& [key, item] : map) {
            if (!first) out += ',';
            first = false;
            newline(indent + 1);
            size_t mark = where.size();
            where += std::format("['{}']", key);
            if (!writeString(out, key, error, where)) return false;
            out += pretty ? ": " : ":";
            if (!write(out, item, pretty, indent + 1, error, where)) return false;
            where.resize(mark);
        }
        if (!map.empty()) newline(indent);
        out += '}';
        return true;
    }
    }
    return true;
}

} // namespace

std::expected<Value, JsonError> parseJson(std::string_view text, int maxDepth) {
    return Parser(text, maxDepth).parse();
}

std::expected<std::string, std::string> toJson(const Value& value, bool pretty) {
    std::string out;
    std::string error;
    std::string where = "the value";
    if (!write(out, value, pretty, 0, error, where)) return std::unexpected(error);
    return out;
}

} // namespace opense4::script
