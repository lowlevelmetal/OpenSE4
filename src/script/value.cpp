#include "script/value.hpp"

#include <format>

namespace opense4::script {

bool operator==(const Value& a, const Value& b) {
    if (a.kind() != b.kind()) return false;
    switch (a.kind()) {
    case Kind::Null: return true;
    case Kind::Bool: return a.asBool() == b.asBool();
    case Kind::Int: return a.asInt() == b.asInt();
    case Kind::String: return a.asString() == b.asString();
    case Kind::List: return a.asList() == b.asList();
    case Kind::Map: return a.asMap() == b.asMap();
    }
    return false;
}

std::string_view kindName(Kind k) {
    switch (k) {
    case Kind::Null: return "null";
    case Kind::Bool: return "bool";
    case Kind::Int: return "int";
    case Kind::String: return "string";
    case Kind::List: return "list";
    case Kind::Map: return "map";
    }
    return "?";
}

namespace {

void describeInto(std::string& out, const Value& v, size_t limit) {
    if (out.size() > limit) return;
    switch (v.kind()) {
    case Kind::Null: out += "null"; break;
    case Kind::Bool: out += v.asBool() ? "true" : "false"; break;
    case Kind::Int: out += std::format("{}", v.asInt()); break;
    case Kind::String: out += std::format("\"{}\"", v.asString()); break;
    case Kind::List: {
        out += '[';
        bool first = true;
        for (const Value& item : v.asList()) {
            if (!first) out += ", ";
            first = false;
            describeInto(out, item, limit);
            if (out.size() > limit) return;
        }
        out += ']';
        break;
    }
    case Kind::Map: {
        out += '{';
        bool first = true;
        for (const auto& [key, item] : v.asMap()) {
            if (!first) out += ", ";
            first = false;
            out += key;
            out += ": ";
            describeInto(out, item, limit);
            if (out.size() > limit) return;
        }
        out += '}';
        break;
    }
    }
}

} // namespace

std::string describe(const Value& v, size_t maxLength) {
    std::string out;
    describeInto(out, v, maxLength);
    if (out.size() > maxLength) {
        out.resize(maxLength);
        out += "...";
    }
    return out;
}

} // namespace opense4::script
