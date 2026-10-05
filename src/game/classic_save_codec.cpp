// The original's saved-game container and sections (docs/spec/08 §2, §3):
// the key table and key stream, the value encodings, and one io() per
// section that both reads and writes it, so that a decoded file encodes back
// to the same values.

#include "game/classic_save.hpp"

#include "core/rng.hpp"

#include <algorithm>
#include <cstring>
#include <format>
#include <map>
#include <utility>

namespace opense4::game::classic {

// ---- Keys (§2.2 - §2.4) -----------------------------------------------------------------------------

int32_t secondSeed(int32_t selector) {
    static constexpr std::array<int32_t, 11> kSeeds{6857, 4444, 23234, 48223, 3827198, 392737, 94846, 9956, 11211, 232, 777456};
    return selector >= 0 && selector <= 10 ? kSeeds[static_cast<size_t>(selector)] : 3234;
}

Keys drawKeys(uint64_t seed) {
    Rng rng(seed);
    Keys k;
    k.k[0] = rng.rangeInt(1, 10);
    for (size_t i = 1; i <= 4; ++i) k.k[i] = rng.rangeInt(1, 10000);
    k.k[5] = secondSeed(k.k[0]);
    return k;
}

Keys testVectorKeys() { return Keys{{3, 777, 11, 22, 4321, 48223}}; }

namespace {

constexpr size_t kColumns = 1000;

// Rows 1, 57 and 123 of the key table (§2.3), columns 1..1000 at 0..999.
struct KeyTable {
    std::array<uint8_t, kColumns> r1{}, r57{}, r123{};

    explicit KeyTable(const Keys& keys) {
        int64_t a = keys.k[1];
        int64_t b = secondSeed(keys.k[0]);
        const uint32_t mask = static_cast<uint32_t>(keys.k[4]);
        for (int row = 1; row <= 123; ++row)
            for (size_t col = 0; col < kColumns; ++col) {
                int64_t s = a + b;
                if (s > 1'000'000'000) s -= 1'000'000'000;
                if (s > 1'000'000'000) s -= 1'000'000'000;
                const uint32_t v = static_cast<uint32_t>(s) ^ mask;
                const auto cell = static_cast<uint8_t>(v & 255u);
                if (row == 1) r1[col] = cell;
                else if (row == 57) r57[col] = cell;
                else if (row == 123) r123[col] = cell;
                a = b;
                b = v;
            }
    }
};

// The one key position of a file (§2.4).
class KeyStream {
public:
    explicit KeyStream(const Keys& keys) : table_(keys) {}

    uint32_t number() {
        const size_t i = p_ - 1;
        const uint32_t k = uint32_t{table_.r1[i]} * table_.r57[i] * table_.r123[i];
        p_ += table_.r1[i];
        if (p_ > kColumns) p_ -= kColumns;
        return k;
    }
    uint8_t character() {
        p_ = p_ + 1 > kColumns ? 1 : p_ + 1;
        return table_.r1[p_ - 1];
    }

private:
    KeyTable table_;
    size_t p_ = 1;
};

constexpr uint8_t kTagInt8 = 2, kTagInt16 = 3, kTagInt32 = 4, kTagFloat = 5, kTagString = 6, kTagFalse = 8, kTagTrue = 9, kTagLongString = 12;

} // namespace

// The file's text is Latin-1 (§2.5); OpenSE4's strings are UTF-8.
std::string latin1ToUtf8(std::string_view latin1) {
    std::string out;
    out.reserve(latin1.size());
    for (char c : latin1) {
        const auto u = static_cast<unsigned char>(c);
        if (u < 0x80) {
            out += c;
        } else {
            out += static_cast<char>(0xC0 | (u >> 6));
            out += static_cast<char>(0x80 | (u & 0x3F));
        }
    }
    return out;
}

std::string utf8ToLatin1(std::string_view utf8, size_t* replaced) {
    std::string out;
    out.reserve(utf8.size());
    for (size_t i = 0; i < utf8.size();) {
        const auto b = static_cast<unsigned char>(utf8[i]);
        size_t len = b < 0x80 ? 1 : (b >> 5) == 0x6 ? 2 : (b >> 4) == 0xE ? 3 : (b >> 3) == 0x1E ? 4 : 0;
        bool valid = len > 0 && i + len <= utf8.size();
        uint32_t cp = len == 1 ? b : len == 2 ? (b & 0x1Fu) : len == 3 ? (b & 0x0Fu) : (b & 0x07u);
        for (size_t k = 1; valid && k < len; ++k) {
            const auto c = static_cast<unsigned char>(utf8[i + k]);
            if ((c & 0xC0) != 0x80) valid = false;
            cp = (cp << 6) | (c & 0x3Fu);
        }
        if (!valid) {
            // Not UTF-8: the byte as it is.
            out += static_cast<char>(b);
            ++i;
            continue;
        }
        if (cp <= 0xFF) {
            out += static_cast<char>(cp);
        } else {
            out += '?';
            if (replaced) ++*replaced;
        }
        i += len;
    }
    return out;
}

std::string latin1Safe(std::string_view utf8, size_t* replaced) { return latin1ToUtf8(utf8ToLatin1(utf8, replaced)); }

namespace {

// ---- Reading ---------------------------------------------------------------------------------------------

class Decoder {
public:
    static constexpr bool kReading = true;

    explicit Decoder(std::span<const uint8_t> in) : in_(in) {}

    bool ok() const { return error_.empty(); }
    const std::string& error() const { return error_; }
    size_t position() const { return pos_; }
    size_t remaining() const { return in_.size() - pos_; }
    void startKeys(const Keys& k) { keys_.emplace(k); }

    void fail(std::string why) {
        if (!error_.empty()) return;
        std::string where;
        for (const std::string& s : path_) where += (where.empty() ? "" : " > ") + s;
        error_ = where.empty() ? std::format("{} (byte {})", why, failAt_) : std::format("{}, in {} (byte {})", why, where, failAt_);
    }

    void push(std::string s) { path_.push_back(std::move(s)); }
    void pop() { path_.pop_back(); }

    // Raw bytes.
    bool raw(uint8_t* out, size_t n, const char* what) {
        failAt_ = pos_;
        if (!ok() || n > remaining()) {
            if (ok()) fail(std::format("the file ends in the middle of {}", what));
            std::fill_n(out, n, uint8_t{0});
            return false;
        }
        std::memcpy(out, in_.data() + pos_, n);
        pos_ += n;
        return true;
    }

    // The compact integers of the key header.
    void compact(int32_t& v, const char* name) {
        uint8_t tag = 0;
        if (!raw(&tag, 1, name)) return;
        if (tag == kTagInt8) {
            uint8_t b = 0;
            raw(&b, 1, name);
            v = static_cast<int8_t>(b);
        } else if (tag == kTagInt16) {
            uint8_t b[2]{};
            raw(b, 2, name);
            v = static_cast<int16_t>(static_cast<uint16_t>(b[0] | (b[1] << 8)));
        } else if (tag == kTagInt32) {
            uint8_t b[4]{};
            raw(b, 4, name);
            v = static_cast<int32_t>(uint32_t{b[0]} | (uint32_t{b[1]} << 8) | (uint32_t{b[2]} << 16) | (uint32_t{b[3]} << 24));
        } else {
            fail(std::format("{} is not an integer (tag {})", name, tag));
        }
    }

    void u8(uint8_t& v, const char*) {
        uint8_t b = 0;
        if (!raw(&b, 1, "a byte")) {
            v = 0;
            return;
        }
        v = static_cast<uint8_t>(b ^ (keys_->number() & 255u));
    }
    void u16(uint16_t& v, const char*) {
        uint8_t b[2]{};
        if (!raw(b, 2, "a word")) {
            v = 0;
            return;
        }
        v = static_cast<uint16_t>((b[0] | (b[1] << 8)) ^ (keys_->number() & 0xffffu));
    }
    void i32(int32_t& v, const char*) {
        uint8_t b[4]{};
        if (!raw(b, 4, "an integer")) {
            v = 0;
            return;
        }
        const uint32_t u = uint32_t{b[0]} | (uint32_t{b[1]} << 8) | (uint32_t{b[2]} << 16) | (uint32_t{b[3]} << 24);
        v = static_cast<int32_t>(u ^ keys_->number());
    }
    void flag(bool& v, const char*) {
        uint8_t tag = 0;
        if (!raw(&tag, 1, "a boolean")) {
            v = false;
            return;
        }
        const bool even = keys_->number() % 2 == 0;
        // Only tag 9 reads as a set bit; any other byte is a clear one (§2.5).
        v = (tag == kTagTrue) != even;
    }
    void str(std::string& v, const char* name) {
        v.clear();
        uint8_t tag = 0;
        if (!raw(&tag, 1, "a string")) return;
        size_t n = 0;
        if (tag == kTagString) {
            uint8_t len = 0;
            if (!raw(&len, 1, "a string")) return;
            n = len;
        } else if (tag == kTagLongString) {
            uint8_t b[4]{};
            if (!raw(b, 4, "a string")) return;
            n = uint32_t{b[0]} | (uint32_t{b[1]} << 8) | (uint32_t{b[2]} << 16) | (uint32_t{b[3]} << 24);
        } else {
            failAt_ = pos_ - 1;
            return fail(std::format("{} is not a string (tag {})", name, tag));
        }
        if (n > remaining()) {
            failAt_ = pos_;
            return fail(std::format("{} is longer ({} characters) than the rest of the file", name, n));
        }
        std::string raw(n, '\0');
        for (size_t i = 0; i < n; ++i) raw[i] = static_cast<char>(in_[pos_ + i] ^ keys_->character());
        pos_ += n;
        v = latin1ToUtf8(raw);
    }
    // Plain characters (the version string has a tag and keys; the summary does not).
    void text(std::string& v, size_t n, const char* name) {
        v.assign(n, ' ');
        raw(reinterpret_cast<uint8_t*>(v.data()), n, name);
    }
    void real(Float80& v, const char* name) {
        uint8_t tag = 0;
        if (!raw(&tag, 1, "a number")) return;
        if (tag == kTagFloat) {
            raw(v.bytes.data(), v.bytes.size(), name);
        } else if (tag == kTagInt8 || tag == kTagInt16 || tag == kTagInt32) {
            // A float may be given as a compact integer (§2.5).
            --pos_;
            int32_t i = 0;
            compact(i, name);
            v = toFloat80(xmath::Ext(int64_t{i}));
        } else {
            failAt_ = pos_ - 1;
            fail(std::format("{} is not a number (tag {})", name, tag));
        }
    }
    void set(BitSet& v, const char* name) {
        u16(v.capacity, name);
        v.words.assign((size_t{v.capacity} + 31) / 32, 0);
        for (uint32_t& w : v.words) {
            int32_t i = 0;
            i32(i, name);
            w = static_cast<uint32_t>(i);
        }
    }

    // A count read before its items: every item takes at least one byte.
    template <class T>
    bool sized(std::vector<T>& v, size_t n, const char* name) {
        if (!ok()) return false;
        if (n > remaining()) {
            failAt_ = pos_;
            fail(std::format("{} holds {} items, more than the rest of the file", name, n));
            return false;
        }
        v.clear();
        v.resize(n);
        return true;
    }

private:
    std::span<const uint8_t> in_;
    size_t pos_ = 0;
    size_t failAt_ = 0;
    std::optional<KeyStream> keys_;
    std::string error_;
    std::vector<std::string> path_;
};

// ---- Writing ---------------------------------------------------------------------------------------------

class Encoder {
public:
    static constexpr bool kReading = false;

    bool ok() const { return error_.empty(); }
    const std::string& error() const { return error_; }
    std::vector<uint8_t>& out() { return out_; }
    void startKeys(const Keys& k) { keys_.emplace(k); }
    void fail(std::string why) {
        if (!error_.empty()) return;
        std::string where;
        for (const std::string& s : path_) where += (where.empty() ? "" : " > ") + s;
        error_ = where.empty() ? why : std::format("{}, in {}", why, where);
    }
    void push(std::string s) { path_.push_back(std::move(s)); }
    void pop() { path_.pop_back(); }

    void compact(int32_t& v, const char*) {
        if (v >= -128 && v <= 127) {
            out_.push_back(kTagInt8);
            out_.push_back(static_cast<uint8_t>(static_cast<int8_t>(v)));
        } else if (v >= -32768 && v <= 32767) {
            out_.push_back(kTagInt16);
            const auto u = static_cast<uint16_t>(static_cast<int16_t>(v));
            out_.push_back(static_cast<uint8_t>(u & 255));
            out_.push_back(static_cast<uint8_t>(u >> 8));
        } else {
            out_.push_back(kTagInt32);
            putU32(static_cast<uint32_t>(v));
        }
    }
    void u8(uint8_t& v, const char*) { out_.push_back(static_cast<uint8_t>(v ^ (keys_->number() & 255u))); }
    void u16(uint16_t& v, const char*) {
        const auto u = static_cast<uint16_t>(v ^ (keys_->number() & 0xffffu));
        out_.push_back(static_cast<uint8_t>(u & 255));
        out_.push_back(static_cast<uint8_t>(u >> 8));
    }
    void i32(int32_t& v, const char*) { putU32(static_cast<uint32_t>(v) ^ keys_->number()); }
    void flag(bool& v, const char*) {
        const bool even = keys_->number() % 2 == 0;
        out_.push_back(v != even ? kTagTrue : kTagFalse);
    }
    void str(std::string& v, const char*) {
        const std::string t = utf8ToLatin1(v, nullptr);
        if (t.size() < 256) {
            out_.push_back(kTagString);
            out_.push_back(static_cast<uint8_t>(t.size()));
        } else {
            out_.push_back(kTagLongString);
            putU32(static_cast<uint32_t>(t.size()));
        }
        for (char c : t) out_.push_back(static_cast<uint8_t>(static_cast<uint8_t>(c) ^ keys_->character()));
    }
    void text(std::string& v, size_t n, const char*) {
        std::string t = v;
        t.resize(n, ' ');
        out_.insert(out_.end(), t.begin(), t.end());
    }
    void real(Float80& v, const char*) {
        out_.push_back(kTagFloat);
        out_.insert(out_.end(), v.bytes.begin(), v.bytes.end());
    }
    void set(BitSet& v, const char* name) {
        if (v.words.size() != (size_t{v.capacity} + 31) / 32) return fail(std::format("{}: the set has {} words for {} members", name, v.words.size(), v.capacity));
        u16(v.capacity, name);
        for (uint32_t& w : v.words) {
            auto i = static_cast<int32_t>(w);
            i32(i, name);
        }
    }
    template <class T>
    bool sized(std::vector<T>& v, size_t n, const char* name) {
        if (v.size() != n) {
            fail(std::format("{} has {} items where its count says {}", name, v.size(), n));
            return false;
        }
        return ok();
    }

private:
    void putU32(uint32_t u) {
        for (int i = 0; i < 4; ++i) out_.push_back(static_cast<uint8_t>(u >> (8 * i)));
    }
    std::vector<uint8_t> out_;
    std::optional<KeyStream> keys_;
    std::string error_;
    std::vector<std::string> path_;
};

// ---- Recording (describe, compare) ------------------------------------------------------------------------

// Lists every value with its path, for comparing two decoded files.
class Recorder {
public:
    static constexpr bool kReading = false;

    std::vector<std::pair<std::string, std::string>> values;

    bool ok() const { return true; }
    void startKeys(const Keys&) {}
    void fail(std::string) {}
    void push(std::string s) { path_.push_back(std::move(s)); }
    void pop() { path_.pop_back(); }

    void compact(int32_t&, const char*) {}  // keys are not compared
    void u8(uint8_t& v, const char* n) { add(n, std::to_string(v)); }
    void u16(uint16_t& v, const char* n) { add(n, std::to_string(v)); }
    void i32(int32_t& v, const char* n) { add(n, std::to_string(v)); }
    void flag(bool& v, const char* n) { add(n, v ? "true" : "false"); }
    void str(std::string& v, const char* n) { add(n, "\"" + v + "\""); }
    void text(std::string& v, size_t, const char* n) { add(n, "\"" + v + "\""); }
    void real(Float80& v, const char* n) {
        std::string hex;
        for (size_t i = v.bytes.size(); i-- > 0;) hex += std::format("{:02x}", v.bytes[i]);
        add(n, std::format("{} (x87 {})", float80Tenths(v), hex));
    }
    void set(BitSet& v, const char* n) {
        std::string bits = std::format("[{}]", v.capacity);
        for (size_t i = 0; i < v.capacity; ++i)
            if (v.test(i)) bits += std::format(" {}", i);
        add(n, bits);
    }
    template <class T>
    bool sized(std::vector<T>& v, size_t, const char* name) {
        add(std::format("{} count", name), std::to_string(v.size()));
        return true;
    }

private:
    void add(std::string_view name, std::string v) {
        std::string p;
        for (const std::string& s : path_) p += s + " > ";
        p += name;
        // Fields repeated under one name (arrays) get their occurrence number.
        if (const int n = seen_[p]++; n > 0) p += std::format(" #{}", n + 1);
        values.emplace_back(std::move(p), std::move(v));
    }
    std::vector<std::string> path_;
    std::map<std::string, int> seen_;
};

// RAII path element.
template <class Ar>
struct Scope {
    Ar& ar;
    Scope(Ar& a, std::string s) : ar(a) { ar.push(std::move(s)); }
    ~Scope() { ar.pop(); }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
};

// ---- Lists ---------------------------------------------------------------------------------------------------

// A list with a byte or word count before its items.
template <class Ar, class T, class F>
void list8(Ar& ar, std::vector<T>& v, const char* name, F&& each) {
    if constexpr (!Ar::kReading)
        if (v.size() > 255) return ar.fail(std::format("{} has {} items; the format holds at most 255", name, v.size()));
    auto n = static_cast<uint8_t>(v.size());
    ar.u8(n, name);
    if (!ar.sized(v, n, name)) return;
    for (size_t i = 0; i < v.size() && ar.ok(); ++i) {
        Scope<Ar> s(ar, std::format("{} {}", name, i + 1));
        each(v[i]);
    }
}

template <class Ar, class T, class F>
void list16(Ar& ar, std::vector<T>& v, const char* name, F&& each) {
    if constexpr (!Ar::kReading)
        if (v.size() > 65535) return ar.fail(std::format("{} has {} items; the format holds at most 65,535", name, v.size()));
    auto n = static_cast<uint16_t>(v.size());
    ar.u16(n, name);
    if (!ar.sized(v, n, name)) return;
    for (size_t i = 0; i < v.size() && ar.ok(); ++i) {
        Scope<Ar> s(ar, std::format("{} {}", name, i + 1));
        each(v[i]);
    }
}

// Items whose count was read earlier.
template <class Ar, class T, class F>
void items(Ar& ar, std::vector<T>& v, size_t n, const char* name, F&& each) {
    if (!ar.sized(v, n, name)) return;
    for (size_t i = 0; i < v.size() && ar.ok(); ++i) {
        Scope<Ar> s(ar, std::format("{} {}", name, i + 1));
        each(v[i]);
    }
}

template <class Ar>
void strings8(Ar& ar, std::vector<std::string>& v, const char* name) {
    list8(ar, v, name, [&](std::string& x) { ar.str(x, "text"); });
}
template <class Ar>
void strings16(Ar& ar, std::vector<std::string>& v, const char* name) {
    list16(ar, v, name, [&](std::string& x) { ar.str(x, "text"); });
}

// A word count kept in a local before other fields (queues, order lists, battles).
template <class Ar, class T>
uint16_t countOf(Ar& ar, const std::vector<T>& v, const char* name) {
    if constexpr (!Ar::kReading)
        if (v.size() > 65535) ar.fail(std::format("{} has {} items; the format holds at most 65,535", name, v.size()));
    auto n = static_cast<uint16_t>(v.size());
    ar.u16(n, name);
    return n;
}

// ---- Shared records ---------------------------------------------------------------------------------------------

template <class Ar>
void io(Ar& ar, Ability& a) {
    ar.u16(a.id, "ability id");
    ar.str(a.description, "ability description");
    ar.i32(a.value1, "ability value 1");
    ar.i32(a.value2, "ability value 2");
}
template <class Ar>
void abilities(Ar& ar, std::vector<Ability>& v) {
    list8(ar, v, "ability", [&](Ability& a) { io(ar, a); });
}

template <class Ar>
void population(Ar& ar, std::vector<PopulationEntry>& v) {
    list16(ar, v, "population", [&](PopulationEntry& p) {
        ar.u8(p.player, "race player");
        ar.i32(p.millions, "millions");
    });
}

template <class Ar>
void units(Ar& ar, std::vector<UnitEntry>& v, const char* name) {
    list16(ar, v, name, [&](UnitEntry& u) {
        ar.u16(u.design, "design");
        ar.u16(u.count, "count");
        ar.u16(u.killed, "killed");
    });
}

template <class Ar>
void io(Ar& ar, CargoRecord& c) {
    population(ar, c.population);
    ar.flag(c.hasUnits, "units follow");
    if (c.hasUnits) units(ar, c.units, "unit");
    else if constexpr (Ar::kReading) c.units.clear();
}

template <class Ar>
void io(Ar& ar, QueueRecord& q) {
    const uint16_t n = countOf(ar, q.items, "queue item");
    ar.flag(q.onHold, "on hold");
    ar.flag(q.emergency, "emergency");
    ar.flag(q.repeat, "repeat");
    ar.u8(q.rallyWaypoint, "rally waypoint");
    ar.u8(q.counter, "turns counter");
    for (int32_t& x : q.spent) ar.i32(x, "spent");
    items(ar, q.items, n, "queue item", [&](QueueEntry& e) {
        ar.u8(e.kind, "kind");
        ar.u16(e.item, "item");
        ar.u16(e.count, "count");
    });
}

template <class Ar>
void io(Ar& ar, OrderList& o) {
    const uint16_t n = countOf(ar, o.orders, "order");
    ar.flag(o.repeat, "repeat");
    ar.u16(o.current, "current order");
    items(ar, o.orders, n, "order", [&](OrderRecord& r) {
        ar.u8(r.kind, "kind");
        ar.u8(r.system, "system");
        ar.u8(r.sector, "sector");
        ar.u8(r.extra, "extra");
        ar.u16(r.target, "target");
        ar.str(r.targetName, "target name");
    });
}

// ---- Prologue to systems (§3.1 - §3.5) ---------------------------------------------------------------------------

template <class Ar>
void io(Ar& ar, Prologue& p) {
    Scope<Ar> s(ar, "prologue");
    ar.flag(p.unused1, "unused flag 1");
    ar.flag(p.unused2, "unused flag 2");
    ar.flag(p.autosave, "autosave");
    ar.u8(p.empireCountCopy, "empire count copy");
    ar.u8(p.currentPlayerCopy, "current player copy");
    ar.i32(p.dateCopy, "date copy");
    ar.str(p.versionCopy, "version copy");
    ar.flag(p.handOver, "hand-over");
    items(ar, p.copies, p.empireCountCopy, "empire names", [&](EmpireCopy& c) {
        ar.str(c.fullName, "full name");
        ar.str(c.leader, "leader");
        ar.str(c.raceFolder, "race folder");
    });
    ar.u8(p.empireCount, "empire count");
    ar.i32(p.date, "date");
    ar.i32(p.turnCounter, "turn counter");
    ar.str(p.version, "version");
}

template <class Ar>
void io(Ar& ar, Options& o) {
    Scope<Ar> s(ar, "game options");
    ar.u16(o.quadrantType, "quadrant type");
    ar.u8(o.quadrantSize, "quadrant size");
    ar.flag(o.allWarpPointsConnected, "all warp points connected");
    ar.flag(o.noWarpPoints, "no warp points");
    ar.flag(o.warpPointsAnywhere, "warp points anywhere");
    ar.flag(o.allSystemsSeen, "all systems seen");
    ar.flag(o.omnipresent, "omnipresent");
    ar.flag(o.finiteResources, "finite resources");
    ar.flag(o.allPlanetsSameSize, "same-size planets");
    ar.u8(o.eventFrequency, "event frequency");
    ar.u8(o.maxEventSeverity, "event severity");
    ar.u8(o.techCost, "technology cost");
    strings16(ar, o.techAreasAllowed, "allowed tech area");
    ar.u8(o.startingResources, "starting resources");
    ar.u8(o.homePlanetValue, "home planet value");
    ar.u8(o.startingPlanets, "starting planets");
    ar.flag(o.sameSystemAllowed, "same system");
    ar.flag(o.evenlyDistributed, "evenly distributed");
    ar.u8(o.scoreDisplay, "score display");
    ar.u8(o.startTechLevel, "starting tech level");
    ar.u8(o.racialPoints, "racial points");
    ar.flag(o.randomComputerEmpires, "random computer empires");
    ar.flag(o.randomNeutralEmpires, "random neutral empires");
    ar.u8(o.computerPlayers, "computer players");
    ar.u8(o.aiDifficulty, "computer difficulty");
    ar.u8(o.aiBonus, "computer bonus");
    ar.str(o.gameMasterPassword, "game master password");
    ar.u16(o.maxUnitsPerPlayer, "maximum units");
    ar.u16(o.maxShipsPerPlayer, "maximum ships");
    ar.flag(o.cheatCodes, "cheat codes");
    ar.flag(o.teamMode, "team mode");
    ar.flag(o.noTacticalCombat, "no tactical combat");
    ar.flag(o.unused1, "unused flag 1");
    ar.flag(o.unused2, "unused flag 2");
    ar.flag(o.completeTechTree, "complete tech tree");
    ar.flag(o.allowGifts, "gifts");
    ar.flag(o.allowTechTrades, "tech trades");
    ar.flag(o.allowSurrender, "surrender");
    ar.flag(o.allowIntel, "intelligence");
    ar.flag(o.noRuins, "no ruins");
    ar.flag(o.onlyBreathable, "only breathable");
    ar.flag(o.onlyHomeType, "only home type");
    ar.flag(o.playersCanSaveMap, "save map");
    ar.u8(o.playStyle, "play style");
    ar.str(o.gameName, "game name");
    ar.str(o.saveFolder, "save folder");
    ar.u8(o.connection, "connection");
    ar.u8(o.autosaveTurns, "autosave");
    ar.flag(o.turnBased, "turn-based");
    ar.flag(o.simultaneous, "simultaneous");
    ar.i32(o.replayCounter, "replay counter");
    ar.i32(o.gameCode, "game code");
    ar.i32(o.turnCode, "turn code");
    ar.i32(o.programSum, "program sum");
    for (int32_t& c : o.checksums) ar.i32(c, "data checksum");
}

template <class Ar>
void io(Ar& ar, Victory& v) {
    Scope<Ar> s(ar, "victory conditions");
    ar.flag(v.score, "score on");
    ar.i32(v.scoreValue, "score");
    ar.flag(v.years, "years on");
    ar.i32(v.yearsTurns, "years");
    ar.flag(v.percentOfSecond, "percent on");
    ar.i32(v.percentOfSecondValue, "percent");
    ar.flag(v.techPercent, "technology on");
    ar.u16(v.techPercentValue, "technology");
    ar.flag(v.peace, "peace on");
    ar.i32(v.peaceTurns, "peace");
    ar.i32(v.peacefulTurns, "peaceful turns");
    ar.flag(v.delay, "delay on");
    ar.i32(v.delayTurns, "delay");
    ar.flag(v.completed, "completed");
}

template <class Ar>
void io(Ar& ar, Globals& g) {
    Scope<Ar> s(ar, "globals");
    ar.u8(g.viewSystem, "shown system");
    ar.u8(g.viewSector, "shown sector");
    ar.u8(g.currentPlayer, "current player");
    ar.i32(g.seed, "seed");
    ar.flag(g.scenario, "scenario");
    ar.flag(g.tutorial, "tutorial");
    ar.str(g.scenarioStem, "scenario");
    ar.u16(g.scenarioPage, "scenario page");
}

template <class Ar>
void io(Ar& ar, SystemRecord& y) {
    ar.str(y.name, "name");
    ar.u8(y.number, "number");
    ar.u8(y.x, "x");
    ar.u8(y.y, "y");
    ar.str(y.typeDescription, "type description");
    ar.u8(y.physicalType, "physical type");
    ar.flag(y.canStart, "start allowed");
    ar.flag(y.maskBackground, "mask background");
    ar.flag(y.nonTiledCenter, "non-tiled centre");
    ar.str(y.backgroundBitmap, "background");
    ar.flag(y.changed, "changed");
    abilities(ar, y.abilities);
    ar.set(y.explored, "explored");
    ar.set(y.claimed, "claimed");
    for (std::string& note : y.notes) ar.str(note, "note");
}

// ---- Empires (§3.6) ---------------------------------------------------------------------------------------------

template <class Ar>
void io(Ar& ar, EmpireOptions& o) {
    Scope<Ar> s(ar, "empire options");
    ar.u8(o.unusedA, "unused");
    ar.u8(o.unusedB, "unused");
    ar.flag(o.showLogAtStart, "show log");
    ar.u8(o.pause, "pause");
    ar.flag(o.confirmEndTurn, "confirm end turn");
    ar.flag(o.unused1, "unused");
    ar.flag(o.confirmScrap, "confirm scrap");
    ar.flag(o.confirmStellar, "confirm stellar manipulation");
    ar.flag(o.confirmDeleteResearch, "confirm delete research");
    ar.flag(o.confirmDeleteIntel, "confirm delete intelligence");
    ar.flag(o.confirmDeleteFirstQueueItem, "confirm delete queue item");
    ar.flag(o.noteSimilar, "note similar abilities");
    ar.flag(o.skipUnderConstruction, "skip under construction");
    ar.flag(o.avoidTaggedMines, "avoid tagged mine fields");
    ar.flag(o.avoidRestricted, "avoid restricted systems");
    ar.flag(o.skipDamaged, "skip damaged");
    ar.flag(o.stopOncePerLocation, "stop once per location");
    ar.flag(o.skipInFleets, "skip in fleets");
    ar.flag(o.clearOnEnemy, "clear on enemy");
    ar.flag(o.clearOnAny, "clear on any");
    ar.flag(o.unused2, "unused");
    ar.flag(o.warpPointNames, "warp point names");
    ar.flag(o.planetNames, "planet names");
    ar.flag(o.facilityMarker1, "facility marker 1");
    ar.flag(o.colonizableMarkers, "colonizable markers");
    ar.flag(o.coordinateLocation, "coordinate location");
    for (bool& m : o.facilityMarkers2to12) ar.flag(m, "facility marker");
    ar.flag(o.galaxyGridLines, "grid lines");
    ar.flag(o.galaxyWarpLines, "warp lines");
    ar.flag(o.latestConstruction, "latest construction");
    ar.flag(o.latestComponents, "latest components");
    ar.flag(o.designsStatsView, "designs statistics view");
    ar.flag(o.designsHideObsolete, "designs hide obsolete");
    ar.flag(o.chooseColonyType, "choose colony type");
    ar.u8(o.turnEndSystem, "turn end system");
    ar.u8(o.turnEndSector, "turn end sector");
    ar.flag(o.autoClaim, "auto claim");
    ar.u8(o.coloniesTab, "colonies tab");
    ar.u8(o.planetsTab, "planets tab");
    ar.u8(o.shipsTab, "ships tab");
    ar.u8(o.queuesTab, "queues tab");
    ar.u8(o.setQueueTab, "set queue tab");
    ar.u8(o.designsTab, "designs tab");
    ar.u8(o.politicsTab, "politics tab");
    ar.u8(o.logFilter, "log filter");
    ar.u8(o.cargoTransferTab, "cargo transfer tab");
    ar.u8(o.unitsTransferTab, "units transfer tab");
    for (bool& b : o.shipsShown) ar.flag(b, "ships shown");
    for (bool& b : o.queuesShown) ar.flag(b, "queues shown");
    ar.flag(o.designerCondensed, "designer condensed");
    ar.flag(o.designerToHit, "designer to-hit");
    ar.flag(o.galaxyNames, "galaxy names");
    ar.flag(o.galaxyDistances, "galaxy distances");
    ar.flag(o.planetsHideAvoided, "planets hide avoided");
    ar.flag(o.simulatorNoObsolete, "simulator no obsolete");
    ar.flag(o.systemGrid, "system grid");
    ar.flag(o.replayFast, "replay fast");
    ar.flag(o.replayAnimate, "replay animate");
    ar.flag(o.replayGrid, "replay grid");
    ar.flag(o.replayViewRect, "replay viewing rectangle");
    for (auto& slot : o.sortKeys)
        for (uint8_t& k : slot) ar.u8(k, "sort key");
}

template <class Ar>
void io(Ar& ar, MessageRecord& m) {
    Scope<Ar> s(ar, "message");
    ar.u8(m.type, "type");
    ar.u8(m.sender, "sender");
    ar.u8(m.recipient, "recipient");
    ar.u8(m.tone, "tone");
    ar.u8(m.treaty, "treaty");
    ar.u8(m.third, "third empire");
    ar.u16(m.system, "system");
    ar.u16(m.planet, "planet");
    auto item = [&](PackageItemRecord& p) {
        ar.str(p.text, "text");
        ar.u8(p.kind, "kind");
        ar.u16(p.value, "value");
        ar.u8(p.quantity, "quantity");
    };
    list16(ar, m.offered, "offered item", item);
    list16(ar, m.requested, "requested item", item);
}

template <class Ar>
void io(Ar& ar, BattleRecord& b) {
    Scope<Ar> s(ar, "battle");
    ar.u16(b.number, "battle number");
    for (size_t i = 0; i < b.sides.size() && ar.ok(); ++i) {
        Scope<Ar> side(ar, std::format("player {}", i + 1));
        BattleSide& x = b.sides[i];
        ar.u8(x.player, "player");
        const uint16_t n = countOf(ar, x.forces, "ship");
        ar.flag(x.tookPart, "took part");
        items(ar, x.forces, n, "ship", [&](BattleShip& sh) {
            ar.str(sh.name, "name");
            ar.str(sh.hullCode, "hull code");
            ar.u16(sh.hull, "hull");
        });
        list16(ar, x.survivors, "survivor", [&](BattleSurvivor& v) {
            ar.str(v.name, "name");
            ar.u8(v.damage, "damage");
        });
    }
}

template <class Ar>
void io(Ar& ar, LogRecord& l) {
    ar.u8(l.owner, "owner");
    ar.u8(l.system, "system");
    ar.u8(l.sector, "sector");
    ar.i32(l.date, "date");
    ar.str(l.title, "title");
    ar.str(l.text, "text");
    ar.u8(l.target, "go-to");
    ar.u16(l.picture, "picture key");
    ar.flag(l.eventNotice, "event notice");
    ar.u8(l.otherEmpire, "other empire");
    ar.u8(l.kind, "kind");
    ar.u8(l.category, "category");
    ar.i32(l.dateRead, "date read");
    ar.u8(l.eventKind, "event kind");
    ar.u16(l.techArea, "tech area");
    bool hasMessage = l.message.has_value();
    ar.flag(hasMessage, "message follows");
    if constexpr (Ar::kReading) {
        if (hasMessage) io(ar, l.message.emplace());
        else l.message.reset();
    } else if (hasMessage) {
        io(ar, *l.message);
    }
    bool hasBattle = l.battle.has_value();
    ar.flag(hasBattle, "battle follows");
    if constexpr (Ar::kReading) {
        if (hasBattle) io(ar, l.battle.emplace());
        else l.battle.reset();
    } else if (hasBattle) {
        io(ar, *l.battle);
    }
}

template <class Ar>
void io(Ar& ar, StrategyRecord& t) {
    ar.u16(t.position, "position");
    ar.str(t.name, "name");
    ar.u8(t.primary, "primary movement");
    ar.u8(t.secondary, "secondary movement");
    ar.flag(t.typePriorityFirst, "type priority first");
    for (uint8_t& k : t.targeting) ar.u8(k, "targeting priority");
    for (size_t i = 0; i < kStrategyCategories; ++i) {
        ar.u8(t.typePriority[i], "type priority");
        ar.flag(t.dontFireOn[i], "don't fire on");
    }
    ar.u16(t.fighterGroup, "fighter launch group");
    ar.u16(t.dronesPerTarget, "drones per target");
    for (bool& b : t.breakFormation) ar.flag(b, "break formation");
    for (uint8_t& d : t.damagePercent) ar.u8(d, "damage percent");
    ar.flag(t.damageUntilWeaponsGone, "until weapons gone");
}

template <class Ar>
void io(Ar& ar, EmpireRecord& e, size_t traitCount) {
    // §3.6.1
    ar.str(e.leaderName, "leader name");
    ar.str(e.leaderTitle, "leader title");
    ar.str(e.name, "empire name");
    ar.str(e.type, "empire type");
    ar.u8(e.player, "player number");
    ar.flag(e.computer, "computer controlled");
    ar.flag(e.useRaceMinisterStyle, "race minister style");
    ar.str(e.raceFolder, "race folder");
    ar.str(e.artFolder, "art folder");
    ar.str(e.emblemFolder, "emblem folder");
    ar.str(e.shipNameFile, "ship-name file");
    ar.u8(e.homeSystem, "home system");
    ar.u8(e.homeSector, "home sector");
    ar.u8(e.atmosphere, "atmosphere");
    ar.u8(e.surface, "surface");
    ar.str(e.biology, "biology");
    ar.str(e.society, "society");
    ar.str(e.history, "history");
    ar.str(e.demeanor, "demeanor");
    ar.str(e.happinessType, "happiness type");
    ar.i32(e.experience, "experience");
    ar.u16(e.defaultFormation, "default formation");
    ar.u16(e.defaultStrategy, "default strategy");
    ar.u16(e.planetStrategy, "planet strategy");
    ar.flag(e.neutral, "neutral");
    ar.u8(e.difficulty, "difficulty");
    ar.u8(e.unusedByte, "unused");
    ar.u16(e.unusedWord1, "unused");
    ar.u16(e.unusedWord2, "unused");
    ar.u8(e.maintenancePercent, "maintenance percent");
    ar.u8(e.reproductionPercent, "reproduction percent");
    for (int32_t& x : e.stored) ar.i32(x, "stored resources");
    ar.i32(e.researchPoints, "research points");
    ar.i32(e.intelPoints, "intelligence points");
    for (bool& b : e.canColonize) ar.flag(b, "can colonize");
    for (bool& b : e.breathes) ar.flag(b, "breathes");
    ar.str(e.password, "password");
    ar.str(e.email, "e-mail");
    if (!ar.ok()) return;
    // §3.6.2
    {
        Scope<Ar> s(ar, "research");
        ar.flag(e.repeatResearch, "repeat");
        ar.flag(e.researchEvenly, "evenly");
        list16(ar, e.uniqueAreas, "unique area", [&](uint16_t& x) { ar.u16(x, "value"); });
        list16(ar, e.techLevels, "tech level", [&](uint16_t& x) { ar.u16(x, "level"); });
        list16(ar, e.research, "project", [&](ResearchItem& r) {
            ar.u16(r.area, "area");
            ar.u8(r.weight, "weight");
            ar.i32(r.spent, "spent");
        });
    }
    // §3.6.3
    {
        Scope<Ar> s(ar, "intelligence");
        list16(ar, e.intel, "project", [&](IntelItem& i) {
            ar.u16(i.project, "project");
            ar.u8(i.target, "target empire");
            ar.i32(i.spent, "spent");
            ar.i32(i.specific, "specific target");
        });
        ar.flag(e.repeatIntel, "repeat");
        ar.flag(e.intelEvenly, "evenly");
    }
    // §3.6.4
    for (size_t i = 0; i < e.politics.size(); ++i) {
        Scope<Ar> s(ar, std::format("politics with player {}", i + 1));
        ar.u8(e.politics[i].treaty, "treaty");
        ar.flag(e.politics[i].dominant, "dominant");
        ar.u16(e.politics[i].tradeCounter, "trade counter");
    }
    // §3.6.5
    {
        Scope<Ar> s(ar, "race");
        ar.u16(e.culture, "culture");
        if constexpr (Ar::kReading) e.traits.assign(traitCount, false);
        else if (e.traits.size() != traitCount)
            ar.fail(std::format("the empire has {} racial trait flags; the data set has {} traits", e.traits.size(), traitCount));
        for (size_t i = 0; i < e.traits.size(); ++i) {
            bool b = e.traits[i];
            ar.flag(b, "trait");
            if constexpr (Ar::kReading) e.traits[i] = b;
        }
        for (int32_t& c : e.characteristics) ar.i32(c, "characteristic");
    }
    // §3.6.6
    strings8(ar, e.designTypes, "design type");
    strings8(ar, e.colonyTypes, "colony type");
    list8(ar, e.queueTemplates, "queue template", [&](QueueTemplate& t) {
        ar.str(t.name, "name");
        io(ar, t.queue);
    });
    // §3.6.7
    io(ar, e.options);
    // §3.6.8
    for (size_t i = 0; i < e.waypoints.size(); ++i) {
        Scope<Ar> s(ar, std::format("waypoint {}", i + 1));
        ar.str(e.waypoints[i].name, "name");
        ar.u8(e.waypoints[i].system, "system");
        ar.u8(e.waypoints[i].sector, "sector");
    }
    strings16(ar, e.repairPriorities, "repair priority");
    list16(ar, e.taggedMinefields, "tagged mine field", [&](std::pair<uint8_t, uint8_t>& m) {
        ar.u8(m.first, "system");
        ar.u8(m.second, "sector");
    });
    list8(ar, e.systemsToAvoid, "system to avoid", [&](uint8_t& x) { ar.u8(x, "system"); });
    // §3.6.9
    {
        Scope<Ar> s(ar, "computer player");
        ar.u8(e.aiState, "state");
        ar.u8(e.staging, "staging system");
        ar.u8(e.secured, "secured system");
        ar.u8(e.incursion, "incursion system");
        ar.u16(e.turnsInState, "turns in state");
        ar.u16(e.afterAttack, "after-attack timer");
        list16(ar, e.defend, "system to defend", [&](uint8_t& x) { ar.u8(x, "system"); });
        list16(ar, e.targets, "attack target", [&](uint8_t& x) { ar.u8(x, "system"); });
        ar.str(e.ministerStyle, "minister style");
        for (bool& m : e.ministers) ar.flag(m, "minister");
        ar.flag(e.ministersForNewVehicles, "ministers for new vehicles");
        ar.flag(e.aiMinimalChanges, "no changes");
        for (size_t i = 0; i < kMaxPlayers; ++i) {
            ar.u8(e.anger[i], "anger");
            ar.u16(e.turnsSinceWar[i], "turns since war");
        }
        ar.u8(e.zero, "zero");
        for (uint8_t& x : e.unused7) ar.u8(x, "unused");
        ar.u16(e.shipNameIndex, "ship-name index");
        for (bool& b : e.enemyCapabilities) ar.flag(b, "enemy capability");
        ar.u16(e.droneNameCounter, "drone name counter");
    }
    // §3.6.10
    ar.flag(e.destroyed, "destroyed");
    ar.str(e.networkName, "network name");
    // §3.6.11
    list16(ar, e.log, "log entry", [&](LogRecord& l) { io(ar, l); });
    // §3.6.12
    list16(ar, e.fleets, "fleet", [&](FleetRecord& f) {
        ar.u16(f.number, "number");
        ar.u8(f.owner, "owner");
        ar.str(f.name, "name");
        ar.u8(f.system, "system");
        ar.u8(f.sector, "sector");
        ar.real(f.experience, "experience");
        ar.u16(f.formation, "formation");
        ar.u16(f.strategy, "strategy");
        ar.flag(f.minister, "minister");
        ar.u16(f.leader, "leader");
    });
    ar.u16(e.fleetsCreated, "fleets created");
    // §3.6.13
    list16(ar, e.strategies, "strategy", [&](StrategyRecord& t) { io(ar, t); });
}

// ---- Designs, objects (§3.7, §3.8) ---------------------------------------------------------------------------

template <class Ar>
void io(Ar& ar, DesignRecord& d) {
    ar.u16(d.id, "id");
    ar.u8(d.owner, "owner");
    ar.u16(d.hull, "hull");
    ar.str(d.type, "design type");
    ar.str(d.templateName, "template");
    ar.str(d.name, "name");
    ar.i32(d.created, "date created");
    ar.flag(d.obsolete, "obsolete");
    ar.flag(d.everBuilt, "built");
    ar.u8(d.speed, "speed");
    for (int32_t& c : d.cost) ar.i32(c, "cost");
    list16(ar, d.parts, "part", [&](DesignPart& p) {
        ar.u16(p.component, "component");
        ar.u8(p.mount, "mount");
    });
    ar.u16(d.strategy, "strategy");
    ar.u16(d.typeCode, "type code");
    for (int32_t& x : d.lastSeen) ar.i32(x, "last seen");
    abilities(ar, d.abilities);
    ar.i32(d.built, "built");
    ar.i32(d.lost, "lost");
    ar.i32(d.scrapped, "scrapped");
    ar.i32(d.tonnageDestroyed, "tonnage destroyed");
    ar.flag(d.changed, "changed");
}

template <class Ar>
void io(Ar& ar, ColonyRecord& c) {
    Scope<Ar> s(ar, "colony");
    ar.u8(c.owner, "owner");
    ar.str(c.type, "colony type");
    population(ar, c.population);
    ar.u8(c.anger, "anger");
    ar.u8(c.plague, "plague");
    ar.flag(c.cloaked, "cloaked");
    ar.u8(c.atmosphereTurns, "atmosphere counter");
    {
        Scope<Ar> cargo(ar, "cargo");
        io(ar, c.cargo);
    }
    list16(ar, c.facilities, "facility", [&](FacilityEntry& f) {
        ar.u16(f.facility, "facility");
        ar.u8(f.count, "count");
        ar.u8(f.destroyed, "destroyed");
    });
    {
        Scope<Ar> q(ar, "queue");
        io(ar, c.queue);
    }
    units(ar, c.landedTroops, "landed troops");
    ar.u8(c.invader, "invader");
    ar.u16(c.militia, "militia");
    io(ar, c.orders);
    ar.flag(c.capital, "capital");
    ar.flag(c.minister, "minister");
}

template <class Ar>
void io(Ar& ar, ObjectRecord& o) {
    ar.u8(o.cls, "class");
    if (!ar.ok()) return;
    if (o.cls < 1 || o.cls > 10) return ar.fail(std::format("unknown object class {}", o.cls));
    ar.u8(o.recordClass, "record class");
    ar.u16(o.id, "id");
    ar.u8(o.system, "system");
    ar.u8(o.sector, "sector");
    const auto c = static_cast<ObjectClass>(o.cls);
    switch (c) {
        case ObjectClass::Star:
        case ObjectClass::WarpPoint:
        case ObjectClass::Storm:
        case ObjectClass::Comet:
            abilities(ar, o.abilities);
            ar.u16(o.sectorType, "sector type");
            if (c == ObjectClass::Star) ar.str(o.name, "name");
            if (c == ObjectClass::WarpPoint) {
                ar.u8(o.destSystem, "destination system");
                ar.u8(o.destSector, "destination sector");
            }
            break;
        case ObjectClass::Planet: {
            abilities(ar, o.abilities);
            ar.u16(o.sectorType, "sector type");
            ar.flag(o.changed, "changed");
            ar.str(o.name, "name");
            ar.real(o.conditions, "conditions");
            for (int32_t& v : o.value) ar.i32(v, "value");
            bool hasColony = o.colony.has_value();
            ar.flag(hasColony, "colony follows");
            if constexpr (Ar::kReading) {
                if (hasColony) io(ar, o.colony.emplace());
                else o.colony.reset();
            } else if (hasColony) {
                io(ar, *o.colony);
            }
            break;
        }
        case ObjectClass::Ship: {
            ar.real(o.dayAccumulator, "day accumulator");
            abilities(ar, o.abilities);
            ar.u16(o.design, "design");
            ar.u8(o.owner, "owner");
            ar.u8(o.heading, "heading");
            ar.u8(o.movement, "movement");
            ar.u8(o.maxMovement, "maximum movement");
            ar.i32(o.supply, "supply");
            ar.u8(o.status, "status");
            ar.real(o.experience, "experience");
            ar.flag(o.changed, "changed");
            ar.flag(o.cloaked, "cloaked");
            ar.str(o.name, "name");
            ar.u16(o.fleet, "fleet");
            ar.flag(o.minister, "minister");
            io(ar, o.orders);
            {
                Scope<Ar> cargo(ar, "cargo");
                io(ar, o.cargo);
            }
            ar.set(o.destroyedParts, "destroyed parts");
            bool hasQueue = o.queue.has_value();
            ar.flag(hasQueue, "queue follows");
            if constexpr (Ar::kReading) {
                if (hasQueue) io(ar, o.queue.emplace());
                else o.queue.reset();
            } else if (hasQueue) {
                io(ar, *o.queue);
            }
            break;
        }
        case ObjectClass::MineField:
        case ObjectClass::SatelliteGroup:
            ar.u8(o.owner, "owner");
            ar.flag(o.changed, "changed");
            units(ar, o.units, "unit");
            io(ar, o.orders);
            ar.flag(o.minister, "minister");
            ar.flag(o.cloaked, "cloaked");
            break;
        case ObjectClass::FighterGroup:
        case ObjectClass::DroneGroup:
            ar.real(o.dayAccumulator, "day accumulator");
            ar.u8(o.movement, "movement");
            ar.u8(o.maxMovement, "maximum movement");
            ar.i32(o.supply, "supply");
            ar.u8(o.heading, "heading");
            ar.u8(o.owner, "owner");
            ar.flag(o.changed, "changed");
            if (c == ObjectClass::DroneGroup) ar.str(o.name, "name");
            ar.u16(o.fleet, "fleet");
            ar.flag(o.minister, "minister");
            units(ar, o.units, "unit");
            io(ar, o.orders);
            break;
    }
}

// ---- The whole body ---------------------------------------------------------------------------------------------

template <class Ar>
void body(Ar& ar, ClassicSave& s) {
    io(ar, s.prologue);
    io(ar, s.options);
    io(ar, s.victory);
    list8(ar, s.events, "timed event", [&](TimedEvent& e) {
        ar.u16(e.event, "event");
        ar.i32(e.date, "date");
        ar.u8(e.system, "system");
        ar.u8(e.sector, "sector");
        ar.u16(e.target, "target");
        ar.u8(e.player, "player");
    });
    io(ar, s.globals);
    list8(ar, s.systems, "system", [&](SystemRecord& y) { io(ar, y); });
    list16(ar, s.specificStarts, "specific starting point", [&](StartPointRecord& p) {
        ar.u16(p.system, "system");
        ar.u16(p.sector, "sector");
        ar.u8(p.player, "player");
    });
    list16(ar, s.commonStarts, "common starting point", [&](StartPointRecord& p) {
        ar.u16(p.system, "system");
        ar.u16(p.sector, "sector");
    });
    if (!ar.ok()) return;
    items(ar, s.empires, s.prologue.empireCount, "empire", [&](EmpireRecord& e) { io(ar, e, s.traitCount); });
    list16(ar, s.designs, "design", [&](DesignRecord& d) { io(ar, d); });
    list16(ar, s.objects, "object", [&](ObjectRecord& o) { io(ar, o); });
    list16(ar, s.launched, "launch", [&](LaunchRecord& l) {
        ar.u16(l.launcher, "launcher");
        ar.u8(l.kind, "unit kind");
        ar.u16(l.count, "count");
    });
}

// ---- The summary (§2.6) -----------------------------------------------------------------------------------------

std::string fit(std::string_view text, size_t width) {
    std::string t(text.substr(0, width));
    return std::string(width - t.size(), ' ') + t;
}

constexpr size_t kSummaryHead = 6 + 6 + 3 + 13 + 20 + 3;
constexpr size_t kSummaryRow = 3 + 40 + 40 + 40 + 6;

std::string trim(std::string_view s) {
    size_t a = 0, b = s.size();
    while (a < b && s[a] == ' ') ++a;
    while (b > a && s[b - 1] == ' ') --b;
    return std::string(s.substr(a, b - a));
}

int parseSmall(std::string_view s) {
    int v = 0;
    bool any = false;
    for (char c : s) {
        if (c >= '0' && c <= '9') {
            v = v * 10 + (c - '0');
            any = true;
        } else if (c != ' ') {
            return -1;
        }
    }
    return any ? v : -1;
}

std::expected<Summary, std::string> parseSummary(std::string_view head, std::string_view rows, int count) {
    Summary s;
    s.version = trim(head.substr(0, 6));
    s.date = parseSmall(head.substr(6, 6));
    s.empires = count;
    s.simultaneous = trim(head.substr(15, 13)) == "Simultaneous";
    s.differentMachines = trim(head.substr(28, 20)) == "Different Machines";
    s.humans = parseSmall(head.substr(48, 3));
    for (int i = 0; i < count; ++i) {
        const std::string_view r = rows.substr(static_cast<size_t>(i) * kSummaryRow, kSummaryRow);
        SummaryRow row;
        row.number = parseSmall(r.substr(0, 3));
        row.name = latin1ToUtf8(trim(r.substr(3, 40)));
        row.leader = latin1ToUtf8(trim(r.substr(43, 40)));
        row.email = latin1ToUtf8(trim(r.substr(83, 40)));
        row.alive = trim(r.substr(123, 6)) != "Dead";
        s.rows.push_back(std::move(row));
    }
    return s;
}

} // namespace

std::string summaryText(const Summary& s) {
    std::string out = fit(s.version, 6) + fit(std::to_string(s.date), 6) + fit(std::to_string(s.empires), 3) +
                      fit(s.simultaneous ? "Simultaneous " : "Turn Based   ", 13) +
                      fit(s.differentMachines ? "Different Machines  " : "Same Machine        ", 20) + fit(std::to_string(s.humans), 3);
    for (const SummaryRow& r : s.rows)
        out += fit(std::to_string(r.number), 3) + fit(utf8ToLatin1(r.name, nullptr), 40) + fit(utf8ToLatin1(r.leader, nullptr), 40) +
               fit(utf8ToLatin1(r.email, nullptr), 40) + fit(r.alive ? "Alive " : "Dead  ", 6);
    return out;
}

// ---- Values ------------------------------------------------------------------------------------------------------

Float80 toFloat80(xmath::Ext v) {
    Float80 f;
    if (v.isZero()) return f;
    const uint64_t mant = v.significand();
    for (size_t i = 0; i < 8; ++i) f.bytes[i] = static_cast<uint8_t>(mant >> (8 * i));
    const int64_t biased = std::clamp<int64_t>(int64_t{v.exponent()} + 63 + 16383, 1, 0x7ffe);
    const auto top = static_cast<uint16_t>((v.isNegative() ? 0x8000u : 0u) | static_cast<uint16_t>(biased));
    f.bytes[8] = static_cast<uint8_t>(top & 255);
    f.bytes[9] = static_cast<uint8_t>(top >> 8);
    return f;
}

xmath::Ext fromFloat80(const Float80& f) {
    uint64_t mant = 0;
    for (size_t i = 0; i < 8; ++i) mant |= uint64_t{f.bytes[i]} << (8 * i);
    const auto top = static_cast<uint16_t>(f.bytes[8] | (f.bytes[9] << 8));
    const int64_t biased = top & 0x7fff;
    if (biased == 0x7fff || mant == 0) return {};   // infinities and NaNs never occur; read as 0
    return xmath::Ext::fromParts((top & 0x8000) != 0, mant, biased - 16383 - 63);
}

Float80 tenthsToFloat80(int64_t tenths) {
    return toFloat80((xmath::Ext(tenths) / xmath::Ext(10)).roundedTo(xmath::kDoubleBits));
}

int64_t float80Tenths(const Float80& f) { return (fromFloat80(f) * xmath::Ext(10)).round(); }

void BitSet::set(size_t i) {
    if (i >= capacity) capacity = static_cast<uint16_t>(std::min<size_t>(i + 1, 65535));
    words.resize((size_t{capacity} + 31) / 32, 0);
    if (i < capacity) words[i / 32] |= uint32_t{1} << (i % 32);
}

BitSet BitSet::sized(uint16_t capacity) {
    BitSet b;
    b.capacity = capacity;
    b.words.assign((size_t{capacity} + 31) / 32, 0);
    return b;
}

std::string_view displayName(ObjectClass c) {
    switch (c) {
        case ObjectClass::Star: return "star";
        case ObjectClass::WarpPoint: return "warp point";
        case ObjectClass::Storm: return "storm";
        case ObjectClass::Planet: return "planet";
        case ObjectClass::Ship: return "ship";
        case ObjectClass::Comet: return "comet";
        case ObjectClass::MineField: return "mine field";
        case ObjectClass::SatelliteGroup: return "satellite group";
        case ObjectClass::FighterGroup: return "fighter group";
        case ObjectClass::DroneGroup: return "drone group";
    }
    return "object";
}

bool ObjectRecord::blank() const {
    switch (objectClass()) {
        case ObjectClass::Star: return name.empty() && sectorType == 0;
        case ObjectClass::WarpPoint: return sectorType == 0 && destSystem == 0;
        case ObjectClass::Storm:
        case ObjectClass::Comet: return sectorType == 0;
        case ObjectClass::Planet: return name.empty() && sectorType == 0 && fromFloat80(conditions).isZero();
        case ObjectClass::Ship: return owner == 0 || design == 0 || system == 0;
        case ObjectClass::MineField:
        case ObjectClass::SatelliteGroup:
        case ObjectClass::FighterGroup:
        case ObjectClass::DroneGroup: return owner == 0;
    }
    return true;
}

KeyRows keyRows(const Keys& keys, size_t n) {
    const KeyTable t(keys);
    n = std::min(n, kColumns);
    KeyRows r;
    r.r1.assign(t.r1.begin(), t.r1.begin() + static_cast<std::ptrdiff_t>(n));
    r.r57.assign(t.r57.begin(), t.r57.begin() + static_cast<std::ptrdiff_t>(n));
    r.r123.assign(t.r123.begin(), t.r123.begin() + static_cast<std::ptrdiff_t>(n));
    return r;
}

std::vector<uint32_t> keySample(const Keys& keys, std::string_view text, size_t numbers) {
    KeyStream k(keys);
    std::vector<uint32_t> out;
    for (size_t i = 0; i < text.size(); ++i) out.push_back(k.character());
    for (size_t i = 0; i < numbers; ++i) out.push_back(k.number());
    return out;
}

// ---- Decoding and encoding ------------------------------------------------------------------------------------------

bool looksLikeClassicSave(std::span<const uint8_t> bytes) {
    // Six compact integers, then a string tag (the version): tags 2, 3 or 4.
    size_t pos = 0;
    for (int i = 0; i < 6; ++i) {
        if (pos >= bytes.size()) return false;
        const uint8_t tag = bytes[pos];
        const size_t width = tag == kTagInt8 ? 1 : tag == kTagInt16 ? 2 : tag == kTagInt32 ? 4 : 0;
        if (width == 0) return false;
        pos += 1 + width;
    }
    return pos < bytes.size() && (bytes[pos] == kTagString || bytes[pos] == kTagLongString);
}

namespace {

std::expected<ClassicSave, std::string> decodeWith(std::span<const uint8_t> bytes, size_t traitCount) {
    if (!looksLikeClassicSave(bytes)) return std::unexpected(std::string("this is not a Space Empires IV saved game (no key header)"));
    Decoder d(bytes);
    ClassicSave s;
    s.traitCount = traitCount;
    for (int32_t& k : s.keys.k) d.compact(k, "the key header");
    d.startKeys(s.keys);
    d.str(s.version, "the version");
    if (!d.ok()) return std::unexpected(d.error());
    if (s.version != kVersion)
        return std::unexpected(std::format("the game was saved by version {} of Space Empires IV; only saves of version {} can be read",
                                           s.version, kVersion));
    // The summary: a fixed head, then one row per empire (§2.6).
    if (d.remaining() < kSummaryHead) return std::unexpected(std::string("the file ends in the summary"));
    std::string head;
    d.text(head, kSummaryHead, "the summary");
    const int count = parseSmall(std::string_view(head).substr(12, 3));
    if (count < 0 || count > kMaxPlayers) return std::unexpected(std::format("the summary names {} empires", count));
    std::string rows;
    d.text(rows, static_cast<size_t>(count) * kSummaryRow, "the summary");
    if (!d.ok()) return std::unexpected(d.error());
    auto summary = parseSummary(head, rows, count);
    if (!summary) return std::unexpected(summary.error());
    s.summary = std::move(*summary);
    body(d, s);
    if (d.ok() && d.remaining() != 0)
        return std::unexpected(std::format("{} bytes are left after the last section (byte {})", d.remaining(), d.position()));
    if (!d.ok()) return std::unexpected(d.error());
    return s;
}

} // namespace

std::expected<ClassicSave, std::string> decodeClassicSave(std::span<const uint8_t> bytes, size_t traitCount) {
    return decodeWith(bytes, traitCount);
}

std::vector<size_t> traitCountsThatDecode(std::span<const uint8_t> bytes) {
    std::vector<size_t> out;
    for (size_t t = 0; t <= 64; ++t)
        if (decodeWith(bytes, t)) out.push_back(t);
    return out;
}

std::expected<std::vector<uint8_t>, std::string> encodeClassicSave(const ClassicSave& save) {
    ClassicSave s = save;   // the writers take references
    if (s.summary.empires != static_cast<int>(s.summary.rows.size()))
        return std::unexpected(std::format("the summary counts {} empires but has {} rows", s.summary.empires, s.summary.rows.size()));
    if (s.empires.size() != s.prologue.empireCount)
        return std::unexpected(std::format("the prologue counts {} empires; there are {}", s.prologue.empireCount, s.empires.size()));
    Encoder e;
    for (int32_t& k : s.keys.k) e.compact(k, "key");
    e.startKeys(s.keys);
    e.str(s.version, "version");
    std::string text = summaryText(s.summary);
    e.text(text, text.size(), "summary");
    body(e, s);
    if (!e.ok()) return std::unexpected(e.error());
    return std::move(e.out());
}

// ---- Describe and compare ----------------------------------------------------------------------------------------------

std::string describeDate(uint32_t turn) { return std::format("{}.{}", 2400 + turn / 10, turn % 10); }

std::string describe(const ClassicSave& s) {
    std::string out;
    const int32_t date = s.prologue.date;
    out += std::format("Space Empires IV saved game, version {}\n", s.version);
    out += std::format("Date {}.{} (date counter {}, turn counter {}), {} game\n", date / 10, date % 10, date, s.prologue.turnCounter,
                       s.options.simultaneous ? "simultaneous" : "turn-based");
    out += std::format("Current player {}, seed {}\n", s.globals.currentPlayer, s.globals.seed);
    out += std::format("{} empires:\n", s.empires.size());
    for (const EmpireRecord& e : s.empires) {
        size_t levels = 0;
        for (uint16_t l : e.techLevels) levels += l;
        out += std::format("  {:2}. {} {} ({}), {}{}{}; {} tech areas ({} levels), {} log entries, {} fleet slots, {} strategies\n", e.player,
                           e.name, e.type, e.raceFolder, e.computer ? "computer" : "human", e.neutral ? ", neutral" : "",
                           e.destroyed ? ", destroyed" : "", e.techLevels.size(), levels, e.log.size(), e.fleets.size(), e.strategies.size());
    }
    size_t freeDesigns = 0;
    for (const DesignRecord& d : s.designs) freeDesigns += d.owner == 0;
    out += std::format("{} systems, {} specific and {} common starting points\n", s.systems.size(), s.specificStarts.size(),
                       s.commonStarts.size());
    out += std::format("{} designs ({} free slots)\n", s.designs.size(), freeDesigns);
    std::map<int, std::pair<size_t, size_t>> classes;   // class -> (objects, blanks)
    size_t colonies = 0;
    for (const ObjectRecord& o : s.objects) {
        auto& [n, blanks] = classes[o.cls];
        ++n;
        blanks += o.blank();
        colonies += o.colony.has_value();
    }
    out += std::format("{} objects:", s.objects.size());
    for (const auto& [c, counts] : classes)
        out += std::format(" {} {}{}", counts.first, displayName(static_cast<ObjectClass>(c)),
                           counts.second ? std::format(" ({} blank)", counts.second) : std::string{});
    out += std::format("\n{} colonies, {} timed-event slots, {} launches this turn\n", colonies, s.events.size(), s.launched.size());
    return out;
}

std::vector<std::string> compareSaves(const ClassicSave& a, const ClassicSave& b, size_t limit) {
    Recorder ra, rb;
    ClassicSave x = a, y = b;
    {
        std::string version = x.version;
        ra.str(version, "version");
        body(ra, x);
    }
    {
        std::string version = y.version;
        rb.str(version, "version");
        body(rb, y);
    }
    std::map<std::string, std::string> left(ra.values.begin(), ra.values.end());
    std::map<std::string, std::string> right(rb.values.begin(), rb.values.end());
    std::vector<std::string> out;
    for (const auto& [path, value] : ra.values) {
        if (out.size() >= limit) break;
        const auto it = right.find(path);
        if (it == right.end()) out.push_back(std::format("{}: {} (only in the first)", path, value));
        else if (it->second != value) out.push_back(std::format("{}: {} != {}", path, value, it->second));
    }
    for (const auto& [path, value] : rb.values) {
        if (out.size() >= limit) break;
        if (!left.contains(path)) out.push_back(std::format("{}: {} (only in the second)", path, value));
    }
    return out;
}

} // namespace opense4::game::classic
