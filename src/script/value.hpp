#pragma once

// The values the modding SDK passes between the engine and scripts
// (docs/MODDING_SDK.md, "Implementation decisions"): null, booleans, whole
// numbers, text, lists and maps. Deliberately no floating point: what a script
// gives the game is whole numbers, as the engine itself uses.
//
// The same tree is the in-game scripts' view and commands (converted to and
// from the script runtime's objects) and the external bots' JSON. Maps keep
// their insertion order, so a value converts the same way on every platform.

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace opense4::script {

class Value;
using ValueList = std::vector<Value>;
using ValueMap = std::vector<std::pair<std::string, Value>>;   // in insertion order; keys unique

enum class Kind : uint8_t { Null, Bool, Int, String, List, Map };

class Value {
public:
    Value() = default;
    Value(std::nullptr_t) {}
    Value(bool b) : v_(b) {}
    Value(int i) : v_(int64_t{i}) {}
    Value(int64_t i) : v_(i) {}
    Value(uint32_t i) : v_(int64_t{i}) {}
    Value(std::string s) : v_(std::move(s)) {}
    Value(std::string_view s) : v_(std::string(s)) {}
    Value(const char* s) : v_(std::string(s)) {}
    Value(ValueList l) : v_(std::make_shared<ValueList>(std::move(l))) {}
    Value(ValueMap m) : v_(std::make_shared<ValueMap>(std::move(m))) {}
    // A bare `Value(0u)` or `Value(size_t)` would be ambiguous; say what is meant.
    template <class T> Value(T) = delete;

    static Value emptyList() { return Value(ValueList{}); }
    static Value emptyMap() { return Value(ValueMap{}); }

    Kind kind() const { return static_cast<Kind>(v_.index()); }
    bool isNull() const { return kind() == Kind::Null; }
    bool isBool() const { return kind() == Kind::Bool; }
    bool isInt() const { return kind() == Kind::Int; }
    bool isString() const { return kind() == Kind::String; }
    bool isList() const { return kind() == Kind::List; }
    bool isMap() const { return kind() == Kind::Map; }

    // Accessors: the kind must match (check first, or use the find/get helpers).
    bool asBool() const { return std::get<bool>(v_); }
    int64_t asInt() const { return std::get<int64_t>(v_); }
    const std::string& asString() const { return std::get<std::string>(v_); }
    const ValueList& asList() const { return *std::get<std::shared_ptr<ValueList>>(v_); }
    const ValueMap& asMap() const { return *std::get<std::shared_ptr<ValueMap>>(v_); }
    // Mutable access copies a shared list or map first (values have value semantics).
    ValueList& editList() { return mutableShared<ValueList>(); }
    ValueMap& editMap() { return mutableShared<ValueMap>(); }

    // Maps: the value under `key`, or nullptr (also when this is not a map).
    const Value* find(std::string_view key) const {
        if (!isMap()) return nullptr;
        for (const auto& [k, v] : asMap())
            if (k == key) return &v;
        return nullptr;
    }
    // Sets `key`, replacing an existing entry in place or appending a new one.
    Value& set(std::string_view key, Value value) {
        ValueMap& m = editMap();
        for (auto& [k, v] : m)
            if (k == key) return v = std::move(value);
        m.emplace_back(std::string(key), std::move(value));
        return m.back().second;
    }
    // Lists: appends.
    Value& push(Value value) {
        ValueList& l = editList();
        l.push_back(std::move(value));
        return l.back();
    }
    size_t size() const {
        if (isList()) return asList().size();
        if (isMap()) return asMap().size();
        return 0;
    }

    friend bool operator==(const Value& a, const Value& b);

private:
    template <class T>
    T& mutableShared() {
        auto& p = std::get<std::shared_ptr<T>>(v_);
        if (p.use_count() > 1) p = std::make_shared<T>(*p);
        return *p;
    }

    std::variant<std::monostate, bool, int64_t, std::string, std::shared_ptr<ValueList>, std::shared_ptr<ValueMap>> v_;
};

// A short, single-line description for diagnostics, e.g. {kind: "SetOrders", vehicle: 12}.
std::string describe(const Value& v, size_t maxLength = 200);

std::string_view kindName(Kind k);

} // namespace opense4::script
