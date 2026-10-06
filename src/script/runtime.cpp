#include "script/runtime.hpp"

#include "script/port/port.h"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <format>
#include <map>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace opense4::script {

namespace {

// Values nest at most this deep when they cross (also catches lists that contain themselves).
constexpr int kMaxValueDepth = 100;

// --- The bytecode cache ------------------------------------------------------------------

struct CacheEntry {
    std::string path;
    std::string source;
    std::vector<uint8_t> code;
};

constexpr size_t kCacheBytesLimit = size_t{64} << 20;

struct BytecodeCache {
    std::mutex mutex;
    std::map<std::pair<std::string, uint64_t>, std::shared_ptr<const CacheEntry>> entries;
    size_t bytes = 0;
    uint64_t hits = 0;
    uint64_t misses = 0;

    // Without the lock held: drops entries no interpreter holds.
    void evictUnused() {
        for (auto it = entries.begin(); it != entries.end();) {
            if (it->second.use_count() == 1) {
                bytes -= it->second->source.size() + it->second->code.size();
                it = entries.erase(it);
            } else {
                ++it;
            }
        }
    }
};

BytecodeCache& bytecodeCache() {
    static BytecodeCache cache;
    return cache;
}

uint64_t textHash(std::string_view text) {
    uint64_t h = 14695981039346656037ull;   // FNV-1a
    for (unsigned char c : text) {
        h ^= c;
        h *= 1099511628211ull;
    }
    return h ^ text.size();
}

// --- Names and text ------------------------------------------------------------------------

bool isIdentifier(std::string_view s) {
    if (s.empty()) return false;
    auto letter = [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; };
    if (!letter(s[0])) return false;
    for (char c : s)
        if (!letter(c) && !(c >= '0' && c <= '9')) return false;
    return true;
}

std::vector<std::string_view> split(std::string_view s, char sep) {
    std::vector<std::string_view> parts;
    size_t start = 0;
    while (true) {
        size_t at = s.find(sep, start);
        parts.push_back(s.substr(start, at == std::string_view::npos ? std::string_view::npos : at - start));
        if (at == std::string_view::npos) break;
        start = at + 1;
    }
    return parts;
}

bool isDottedName(std::string_view s) {
    for (std::string_view part : split(s, '.'))
        if (!isIdentifier(part)) return false;
    return true;
}

bool validUtf8(std::string_view s) {
    size_t i = 0;
    while (i < s.size()) {
        auto c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) {
            ++i;
            continue;
        }
        size_t n = 0;
        uint32_t cp = 0;
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
            return false;
        }
        if (s.size() - i <= n) return false;
        for (size_t k = 1; k <= n; ++k) {
            auto d = static_cast<unsigned char>(s[i + k]);
            if ((d & 0xC0) != 0x80) return false;
            cp = (cp << 6) | (d & 0x3Fu);
        }
        if ((n == 1 && cp < 0x80) || (n == 2 && cp < 0x800) || (n == 3 && cp < 0x10000) || cp > 0x10FFFF ||
            (cp >= 0xD800 && cp <= 0xDFFF))
            return false;
        i += n + 1;
    }
    return true;
}

// Modules that are part of the runtime: MicroPython's built-in ones; the library
// files are checked separately.
constexpr std::string_view kBuiltinModules[] = {"array", "builtins", "collections", "heapq", "json", "math",
                                                "re", "struct", "sys", "__main__"};

bool isBuiltinModule(std::string_view name) {
    return std::find(std::begin(kBuiltinModules), std::end(kBuiltinModules), name) != std::end(kBuiltinModules);
}

const LibraryFile* findLibraryFile(std::string_view path) {
    for (const LibraryFile& f : libraryFiles())
        if (f.path == path) return &f;
    return nullptr;
}

bool libraryHasPackage(std::string_view dir) {
    for (const LibraryFile& f : libraryFiles())
        if (f.path.size() > dir.size() && f.path.starts_with(dir) && f.path[dir.size()] == '/') return true;
    return false;
}

// The top-level names the runtime's library provides ("typing", "itertools"...).
bool isLibraryModule(std::string_view top) {
    for (const LibraryFile& f : libraryFiles()) {
        std::string_view p = f.path;
        size_t end = p.find_first_of("/.");
        if (p.substr(0, end) == top) return true;
    }
    return false;
}

Error makeError(ErrorKind kind, std::string message) {
    Error e;
    e.kind = kind;
    e.message = std::move(message);
    return e;
}

// Called from MicroPython's C code while it prints: no exception may leave it.
void appendTo(void* ctx, const char* text, size_t len) noexcept {
    try {
        static_cast<std::string*>(ctx)->append(text, len);
    } catch (...) {
    }
}

// --- Values: Python -> engine ------------------------------------------------------------
// Reads Python objects without allocating or raising, so it needs no protection.

struct FromPython {
    std::string where;                 // "the result", "argument 2 of engine.move"
    std::vector<std::string> path;     // "['ships']", "[3]"
    std::string error;

    std::string place() const {
        std::string s = where;
        for (const std::string& p : path) s += p;
        return s;
    }

    bool fail(std::string what) {
        error = std::format("{} {}", place(), what);
        return false;
    }

    bool convert(ose_obj o, Value& out, int depth) {
        if (depth > kMaxValueDepth)
            return fail(std::format("is nested more than {} levels deep (or contains itself)", kMaxValueDepth));
        switch (ose_kind(o)) {
        case OSE_NONE: out = Value(); return true;
        case OSE_BOOL: out = Value(ose_bool_value(o) != 0); return true;
        case OSE_INT: {
            int64_t v = 0;
            if (ose_int_value(o, &v) != 0) return fail("is an integer that doesn't fit in 64 bits");
            out = Value(v);
            return true;
        }
        case OSE_FLOAT: return fail("is a float; only whole numbers cross to the engine (use round() or int())");
        case OSE_STR: {
            size_t len = 0;
            const char* data = ose_str_value(o, &len);
            out = Value(std::string_view(data, len));
            return true;
        }
        case OSE_LIST: {
            ose_obj* items = nullptr;
            size_t n = ose_list_items(o, &items);
            ValueList list;
            list.reserve(n);
            for (size_t i = 0; i < n; ++i) {
                path.push_back(std::format("[{}]", i));
                Value item;
                if (!convert(items[i], item, depth + 1)) return false;
                path.pop_back();
                list.push_back(std::move(item));
            }
            out = Value(std::move(list));
            return true;
        }
        case OSE_DICT:
        case OSE_ORDERED_DICT: {
            // dicts keep insertion order here, as in CPython: the map has the same order
            std::vector<std::pair<std::string_view, ose_obj>> entries;
            entries.reserve(ose_dict_size(o));
            size_t pos = 0;
            ose_obj key = nullptr, value = nullptr;
            while (ose_dict_next(o, &pos, &key, &value)) {
                if (ose_kind(key) != OSE_STR)
                    return fail(std::format("has a key of type {}; map keys must be strings", ose_type_name(key)));
                size_t len = 0;
                const char* data = ose_str_value(key, &len);
                entries.emplace_back(std::string_view(data, len), value);
            }
            ValueMap map;
            map.reserve(entries.size());
            for (const auto& [k, v] : entries) {
                path.push_back(std::format("['{}']", k));
                Value item;
                if (!convert(v, item, depth + 1)) return false;
                path.pop_back();
                map.emplace_back(std::string(k), std::move(item));
            }
            out = Value(std::move(map));
            return true;
        }
        default:
            return fail(std::format("is a {}; what crosses to the engine is None, bool, int, str, list, tuple and dict",
                                    ose_type_name(o)));
        }
    }
};

// --- Values: engine -> Python ------------------------------------------------------------

// Checked before any Python object is made, so that converting can only fail for memory.
bool checkValue(const Value& v, int depth, std::string& path, std::string& error) {
    if (depth > kMaxValueDepth) {
        error = std::format("{} is nested more than {} levels deep", path, kMaxValueDepth);
        return false;
    }
    switch (v.kind()) {
    case Kind::String:
        if (!validUtf8(v.asString())) {
            error = std::format("{} is not valid UTF-8 text", path);
            return false;
        }
        return true;
    case Kind::List: {
        size_t i = 0;
        for (const Value& item : v.asList()) {
            size_t mark = path.size();
            path += std::format("[{}]", i++);
            if (!checkValue(item, depth + 1, path, error)) return false;
            path.resize(mark);
        }
        return true;
    }
    case Kind::Map:
        for (const auto& [key, item] : v.asMap()) {
            size_t mark = path.size();
            path += std::format("['{}']", key);
            if (!validUtf8(key)) {
                error = std::format("{} has a key that is not valid UTF-8 text", path);
                return false;
            }
            if (!checkValue(item, depth + 1, path, error)) return false;
            path.resize(mark);
        }
        return true;
    default:
        return true;
    }
}

// Inside ose_protect. Allocation may raise MemoryError, which unwinds past these
// frames: they hold nothing that needs destroying.
ose_obj toPython(const Value& v) {
    switch (v.kind()) {
    case Kind::Null: return ose_none();
    case Kind::Bool: return ose_bool(v.asBool() ? 1 : 0);
    case Kind::Int: return ose_int(v.asInt());
    case Kind::String: return ose_str(v.asString().data(), v.asString().size());
    case Kind::List: {
        const ValueList& items = v.asList();
        ose_obj list = ose_list(items.size());
        for (size_t i = 0; i < items.size(); ++i) ose_list_set(list, i, toPython(items[i]));
        return list;
    }
    case Kind::Map: {
        ose_obj dict = ose_dict();
        for (const auto& entry : v.asMap())
            ose_dict_set(dict, ose_str(entry.first.data(), entry.first.size()), toPython(entry.second));
        return dict;
    }
    }
    return ose_none();
}

std::atomic<bool> g_interpreterAlive{false};

} // namespace

// --- Interpreter -----------------------------------------------------------------------------

struct Interpreter::Impl {
    Limits limits;
    void* heap = nullptr;
    ose_host host{};
    std::unordered_map<std::string, std::string> files;
    std::vector<std::shared_ptr<const CacheEntry>> held;   // compiled code this interpreter runs
    struct Native {
        std::string module;
        std::string name;
        NativeFunction fn;
        bool created = false;
    };
    std::vector<Native> natives;
    std::string output;
    bool outputFull = false;
    int64_t budgetLeft = 0;
    int64_t lastCall = 0;
    std::atomic<bool> busy{false};
    // Engine work a native function charges to the call (Interpreter::charge).
    bool inNative = false;
    int64_t nativeCharge = 0;

    const char* source(const char* path, size_t* len) {
        if (auto it = files.find(path); it != files.end()) {
            *len = it->second.size();
            return it->second.data();
        }
        if (const LibraryFile* f = findLibraryFile(path)) {
            *len = f->text.size();
            return f->text.data();
        }
        return nullptr;
    }

    bool isPackage(std::string_view dir) const {
        for (const auto& [p, text] : files)
            if (p.size() > dir.size() && p.starts_with(dir) && p[dir.size()] == '/') return true;
        return libraryHasPackage(dir);
    }

    bool moduleExists(std::string_view module) {
        std::string path(module);
        std::replace(path.begin(), path.end(), '.', '/');
        size_t len = 0;
        if (source((path + ".py").c_str(), &len) || isPackage(path)) return true;
        if (module.find('.') == std::string_view::npos) {
            if (isBuiltinModule(module)) return true;
            for (const Native& n : natives)
                if (n.module == module) return true;
        }
        return false;
    }

    // --- host callbacks ---
    // MicroPython's C code calls these: no C++ exception may leave them.
    static void hostOutput(void* ctx, const char* text, size_t len) noexcept {
        auto* self = static_cast<Impl*>(ctx);
        if (self->outputFull) return;
        try {
            size_t room = self->limits.outputBytes - std::min(self->output.size(), self->limits.outputBytes);
            if (len > room) {
                self->output.append(text, room);
                self->output += "\n[further output dropped]\n";
                self->outputFull = true;
                return;
            }
            self->output.append(text, len);
        } catch (...) {
            self->outputFull = true;
        }
    }

    static const char* hostSource(void* ctx, const char* path, size_t* len) noexcept {
        try {
            return static_cast<Impl*>(ctx)->source(path, len);
        } catch (...) {
            return nullptr;
        }
    }

    static int hostIsPackage(void* ctx, const char* path) noexcept {
        return static_cast<Impl*>(ctx)->isPackage(path) ? 1 : 0;
    }

    static const uint8_t* hostCompiled(void* ctx, const char* path, size_t* len) noexcept {
        try {
            return compiled(static_cast<Impl*>(ctx), path, len);
        } catch (...) {
            return nullptr;   // compiled again
        }
    }

    static const uint8_t* compiled(Impl* self, const char* path, size_t* len) {
        size_t textLen = 0;
        const char* text = self->source(path, &textLen);
        if (!text) return nullptr;
        std::string_view source(text, textLen);
        BytecodeCache& cache = bytecodeCache();
        std::lock_guard lock(cache.mutex);
        auto it = cache.entries.find({std::string(path), textHash(source)});
        if (it == cache.entries.end() || it->second->source != source) {
            ++cache.misses;
            return nullptr;
        }
        ++cache.hits;
        self->held.push_back(it->second);
        *len = it->second->code.size();
        return it->second->code.data();
    }

    static const uint8_t* hostStoreCompiled(void* ctx, const char* path, const uint8_t* data, size_t len,
                                            size_t* keptLen) noexcept {
        try {
            return storeCompiled(static_cast<Impl*>(ctx), path, data, len, keptLen);
        } catch (...) {
            return nullptr;   // not cached; MicroPython compiles the file itself
        }
    }

    static const uint8_t* storeCompiled(Impl* self, const char* path, const uint8_t* data, size_t len, size_t* keptLen) {
        size_t textLen = 0;
        const char* text = self->source(path, &textLen);
        if (!text) return nullptr;
        auto entry = std::make_shared<CacheEntry>();
        entry->path = path;
        entry->source.assign(text, textLen);
        entry->code.assign(data, data + len);
        BytecodeCache& cache = bytecodeCache();
        std::lock_guard lock(cache.mutex);
        if (cache.bytes + entry->source.size() + entry->code.size() > kCacheBytesLimit) cache.evictUnused();
        auto key = std::make_pair(entry->path, textHash(entry->source));
        if (auto it = cache.entries.find(key); it != cache.entries.end()) {
            cache.bytes -= it->second->source.size() + it->second->code.size();
            cache.entries.erase(it);
        }
        cache.bytes += entry->source.size() + entry->code.size();
        cache.entries.emplace(std::move(key), entry);
        self->held.push_back(entry);
        *keptLen = entry->code.size();
        return entry->code.data();
    }

    // A native function: its arguments become Values, its result a Python object, and
    // whatever it throws a Python exception. Runs inside the script's call.
    static int hostCallNative(void* ctx, size_t index, size_t nArgs, const ose_obj* args, ose_obj* result) noexcept {
        try {
            return callNative(static_cast<Impl*>(ctx), index, nArgs, args, result);
        } catch (...) {
            // no memory for the conversions: report it as MicroPython would
            struct Make {
                ose_obj made;
            } make{nullptr};
            ose_obj failure = nullptr;
            if (ose_protect([](void* p) { static_cast<Make*>(p)->made = ose_new_exception("MemoryError", "", 0); }, &make,
                            &failure) != 0)
                make.made = failure;
            *result = make.made;
            return 1;
        }
    }

    static int callNative(Impl* self, size_t index, size_t nArgs, const ose_obj* args, ose_obj* result) {
        const Native& native = self->natives[index];
        std::string errorType;
        std::string errorMessage;
        std::vector<Value> values;
        values.reserve(nArgs);
        for (size_t i = 0; i < nArgs && errorType.empty(); ++i) {
            FromPython conv;
            conv.where = std::format("argument {} of {}.{}", i + 1, native.module, native.name);
            Value v;
            if (!conv.convert(args[i], v, 0)) {
                errorType = "TypeError";
                errorMessage = conv.error;
            }
            values.push_back(std::move(v));
        }
        Value out;
        self->inNative = true;
        self->nativeCharge = 0;
        if (errorType.empty()) {
            try {
                out = native.fn(values);
                std::string path = std::format("the result of {}.{}", native.module, native.name);
                std::string error;
                if (!checkValue(out, 0, path, error)) {
                    errorType = "RuntimeError";
                    errorMessage = error;
                }
            } catch (const NativeError& e) {
                errorType = e.type();
                errorMessage = e.what();
            } catch (const std::exception& e) {
                errorType = "RuntimeError";
                errorMessage = std::format("{}.{} failed: {}", native.module, native.name, e.what());
            } catch (...) {
                errorType = "RuntimeError";
                errorMessage = std::format("{}.{} failed", native.module, native.name);
            }
        }
        self->inNative = false;
        // Engine work: not charged to the script's budget, beyond what the
        // function charged itself (Interpreter::charge).
        int64_t budget = ose_budget_get();
        if (budget < OSE_BUDGET_UNLIMITED) budget -= std::min(self->nativeCharge, OSE_BUDGET_UNLIMITED);
        self->nativeCharge = 0;
        ose_budget_set(OSE_BUDGET_UNLIMITED);
        struct Make {
            const Value* value;
            const std::string* type;
            const std::string* message;
            ose_obj made;
        } make{&out, &errorType, &errorMessage, nullptr};
        ose_obj failure = nullptr;
        int status = ose_protect(
            [](void* p) {
                auto* m = static_cast<Make*>(p);
                m->made = m->type->empty() ? toPython(*m->value)
                                           : ose_new_exception(m->type->c_str(), m->message->data(), m->message->size());
            },
            &make, &failure);
        ose_budget_set(budget);
        if (status != 0) {
            *result = failure;   // e.g. MemoryError while converting
            return 1;
        }
        *result = make.made;
        return errorType.empty() ? 0 : 1;
    }

    // Inside ose_protect: the native modules not made yet in this interpreter.
    void createNatives() {
        for (size_t i = 0; i < natives.size(); ++i) {
            if (natives[i].created) continue;
            ose_obj module = ose_native_module(natives[i].module.c_str());
            ose_native_function(module, natives[i].name.c_str(), i);
            natives[i].created = true;
        }
    }

    Error exceptionError(ose_obj exc) {
        Error e;
        switch (ose_exception_kind(exc)) {
        case OSE_EXC_SYNTAX: e.kind = ErrorKind::Syntax; break;
        case OSE_EXC_BUDGET: e.kind = ErrorKind::Budget; break;
        case OSE_EXC_MEMORY: e.kind = ErrorKind::Memory; break;
        case OSE_EXC_RECURSION: e.kind = ErrorKind::Recursion; break;
        default: e.kind = ErrorKind::Exception; break;
        }
        e.type = ose_exception_type(exc);
        ose_exception_message(exc, appendTo, &e.message);
        ose_exception_traceback(exc, appendTo, &e.traceback);
        return e;
    }
};

namespace {

// Marks an interpreter busy for one call; refuses a second call at the same time.
class CallGuard {
public:
    explicit CallGuard(Interpreter::Impl& impl) : impl_(impl) {
        bool expected = false;
        ok_ = impl_.busy.compare_exchange_strong(expected, true);
    }
    ~CallGuard() {
        if (ok_) impl_.busy.store(false);
    }
    CallGuard(const CallGuard&) = delete;
    CallGuard& operator=(const CallGuard&) = delete;
    explicit operator bool() const { return ok_; }

private:
    Interpreter::Impl& impl_;
    bool ok_ = false;
};

Error busyError() {
    return makeError(ErrorKind::Usage, "the interpreter is already running a call (calls must not overlap, nor come "
                                       "from inside an engine function)");
}

// What one protected run of script code needs and produces.
enum class Phase : int { Setup, Script, Result };

struct Run {
    Interpreter::Impl* impl = nullptr;
    int64_t budget = 0;                 // for the script part
    int64_t budgetAtEnd = 0;            // left when the script part ended
    volatile Phase phase = Phase::Setup;
    // what to run
    enum class What { Import, Call, Exec, Eval } what = What::Import;
    std::string module;
    std::vector<std::string> attributes;
    std::span<const Value> args;
    std::string_view source;
    bool wantResult = false;
    // results
    size_t missing = SIZE_MAX;          // the attribute that doesn't exist
    bool hasValue = false;              // the value is kept (ose_keep) for converting
};

// Inside ose_protect only plain data changes: a C++ exception must not pass
// MicroPython's C frames, so nothing here allocates on the C++ side.

void runProtected(void* p) {
    Run& r = *static_cast<Run*>(p);
    r.impl->createNatives();
    ose_obj fn = nullptr;
    ose_obj argList = nullptr;
    ose_obj* argv = nullptr;
    size_t argc = 0;
    if (r.what == Run::What::Call) {
        argList = ose_list(r.args.size());
        for (size_t i = 0; i < r.args.size(); ++i) ose_list_set(argList, i, toPython(r.args[i]));
        argc = ose_list_items(argList, &argv);
    }
    r.phase = Phase::Script;
    ose_budget_set(r.budget);
    ose_obj value = nullptr;
    switch (r.what) {
    case Run::What::Import: ose_import(r.module.c_str()); break;
    case Run::What::Call: {
        fn = ose_import(r.module.c_str());
        for (size_t i = 0; i < r.attributes.size(); ++i) {
            fn = ose_getattr_maybe(fn, r.attributes[i].c_str());
            if (!fn) {
                r.missing = i;
                break;
            }
        }
        if (fn) value = ose_call(fn, argc, argv);
        break;
    }
    case Run::What::Exec: ose_exec(r.source.data(), r.source.size(), "<exec>", 0); break;
    case Run::What::Eval: value = ose_exec(r.source.data(), r.source.size(), "<eval>", 1); break;
    }
    r.budgetAtEnd = ose_budget_get();
    ose_budget_set(OSE_BUDGET_UNLIMITED);
    r.phase = Phase::Result;
    if (r.wantResult && value) {
        ose_keep(value);   // reachable while it is converted, after this returns
        r.hasValue = true;
    }
}

} // namespace

std::string_view errorKindName(ErrorKind k) {
    switch (k) {
    case ErrorKind::Syntax: return "Syntax";
    case ErrorKind::Exception: return "Exception";
    case ErrorKind::Budget: return "Budget";
    case ErrorKind::Memory: return "Memory";
    case ErrorKind::Recursion: return "Recursion";
    case ErrorKind::Conversion: return "Conversion";
    case ErrorKind::NotFound: return "NotFound";
    case ErrorKind::Usage: return "Usage";
    }
    return "?";
}

std::string Error::describe() const {
    std::string s(errorKindName(kind));
    s += ": ";
    if (!type.empty()) {
        s += type;
        if (!message.empty()) s += ": ";
    }
    s += message;
    return s;
}

Interpreter::Interpreter(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

Result<std::unique_ptr<Interpreter>> Interpreter::create(const Limits& limits) {
    if (limits.heapBytes < (size_t{64} << 10))
        return std::unexpected(makeError(ErrorKind::Usage, "the heap must be at least 64 KiB"));
    if (limits.cStackBytes < (size_t{16} << 10))
        return std::unexpected(makeError(ErrorKind::Usage, "the C stack limit must be at least 16 KiB"));
    if (limits.maxDepth < 1 || limits.budget < 0)
        return std::unexpected(makeError(ErrorKind::Usage, "the depth limit must be positive and the budget not negative"));
    bool expected = false;
    if (!g_interpreterAlive.compare_exchange_strong(expected, true))
        return std::unexpected(makeError(ErrorKind::Usage,
                                         "another script interpreter is alive in this process; MicroPython runs one at a time"));
    auto impl = std::make_unique<Impl>();
    impl->limits = limits;
    impl->budgetLeft = limits.budget;
    impl->heap = std::malloc(limits.heapBytes);
    if (!impl->heap) {
        g_interpreterAlive.store(false);
        return std::unexpected(makeError(ErrorKind::Memory, "no memory for the interpreter's heap"));
    }
    ose_host& h = impl->host;
    h.ctx = impl.get();
    h.output = &Impl::hostOutput;
    h.source = &Impl::hostSource;
    h.is_package = &Impl::hostIsPackage;
    h.compiled = &Impl::hostCompiled;
    h.store_compiled = &Impl::hostStoreCompiled;
    h.call_native = &Impl::hostCallNative;

    char stackTop = 0;
    ose_enter(&stackTop, limits.cStackBytes);
    if (ose_init(&impl->host, impl->heap, limits.heapBytes, limits.maxDepth) != 0) {
        std::free(impl->heap);
        g_interpreterAlive.store(false);
        return std::unexpected(makeError(ErrorKind::Usage, "another script interpreter is running"));
    }
    ose_enter(&stackTop, limits.cStackBytes);
    ose_obj failure = nullptr;
    if (ose_protect([](void*) { ose_setup(); }, nullptr, &failure) != 0) {
        Error e = impl->exceptionError(failure);
        ose_deinit();
        std::free(impl->heap);
        g_interpreterAlive.store(false);
        e.message = "the interpreter could not start: " + e.message;
        return std::unexpected(std::move(e));
    }
    return std::unique_ptr<Interpreter>(new Interpreter(std::move(impl)));
}

Interpreter::~Interpreter() {
    char stackTop = 0;
    ose_enter(&stackTop, impl_->limits.cStackBytes);
    ose_deinit();
    std::free(impl_->heap);
    impl_->held.clear();
    g_interpreterAlive.store(false);
}

bool Interpreter::active() {
    return g_interpreterAlive.load();
}

Result<void> Interpreter::addFile(std::string_view path, std::string text) {
    CallGuard guard(*impl_);
    if (!guard) return std::unexpected(busyError());
    if (!path.ends_with(".py"))
        return std::unexpected(makeError(ErrorKind::Usage, std::format("'{}': module files end in .py", path)));
    std::vector<std::string_view> parts = split(path.substr(0, path.size() - 3), '/');
    for (std::string_view part : parts)
        if (!isIdentifier(part))
            return std::unexpected(makeError(
                ErrorKind::Usage, std::format("'{}': each part of a module path is a Python name, separated by '/'", path)));
    std::string_view top = parts.front();
    if (isBuiltinModule(top) || isLibraryModule(top))
        return std::unexpected(
            makeError(ErrorKind::Usage, std::format("'{}': the module name '{}' belongs to the runtime", path, top)));
    for (const Impl::Native& n : impl_->natives)
        if (n.module == top)
            return std::unexpected(makeError(ErrorKind::Usage,
                                             std::format("'{}': '{}' is a module of engine functions", path, top)));
    if (!validUtf8(text))
        return std::unexpected(makeError(ErrorKind::Usage, std::format("'{}' is not valid UTF-8 text", path)));
    impl_->files[std::string(path)] = std::move(text);
    return {};
}

Result<void> Interpreter::addNativeFunction(std::string_view module, std::string_view name, NativeFunction fn) {
    CallGuard guard(*impl_);
    if (!guard) return std::unexpected(busyError());
    if (!isIdentifier(module) || !isIdentifier(name))
        return std::unexpected(
            makeError(ErrorKind::Usage, std::format("'{}.{}': a module and a function name, each one Python name", module, name)));
    if (isBuiltinModule(module) || isLibraryModule(module))
        return std::unexpected(makeError(ErrorKind::Usage, std::format("the module name '{}' belongs to the runtime", module)));
    for (const auto& [p, text] : impl_->files)
        if (p.starts_with(std::string(module) + "/") || p == std::string(module) + ".py")
            return std::unexpected(makeError(ErrorKind::Usage, std::format("'{}' is already a module file ({})", module, p)));
    for (const Impl::Native& n : impl_->natives)
        if (n.module == module && n.name == name)
            return std::unexpected(makeError(ErrorKind::Usage, std::format("'{}.{}' exists already", module, name)));
    impl_->natives.push_back({std::string(module), std::string(name), std::move(fn), false});
    return {};
}

namespace {

// Runs `run` with the interpreter's budget, then turns what happened into a Result.
Result<Value> execute(Interpreter::Impl& impl, Run& run, const CallOptions& options) {
    int64_t interpreterLeft = impl.budgetLeft;
    bool callLimited = options.budget >= 0 && options.budget < interpreterLeft;
    run.impl = &impl;
    run.budget = callLimited ? options.budget : interpreterLeft;
    impl.lastCall = 0;
    if (run.budget <= 0)
        return std::unexpected(makeError(ErrorKind::Budget, callLimited ? "the call was given no budget"
                                                                        : "the interpreter has used up its budget"));

    char stackTop = 0;
    ose_enter(&stackTop, impl.limits.cStackBytes);
    ose_begin_call();
    ose_obj exception = nullptr;
    int status = ose_protect(&runProtected, &run, &exception);
    int64_t leftAtEnd = status != 0 && run.phase == Phase::Script ? ose_budget_get() : run.budgetAtEnd;
    ose_budget_set(OSE_BUDGET_UNLIMITED);
    int64_t used = run.phase == Phase::Setup ? 0 : std::clamp<int64_t>(run.budget - leftAtEnd, 0, run.budget);
    impl.lastCall = used;
    impl.budgetLeft -= used;

    if (status != 0) {
        Error e = impl.exceptionError(exception);
        if (e.kind == ErrorKind::Budget) {
            e.message = callLimited ? std::format("the call used up its budget of {} bytecodes", run.budget)
                                    : std::format("the interpreter used up its budget of {} bytecodes", impl.limits.budget);
        }
        return std::unexpected(std::move(e));
    }
    if (run.missing != SIZE_MAX)
        return std::unexpected(makeError(ErrorKind::NotFound,
                                         std::format("module '{}' has no '{}'", run.module, run.attributes[run.missing])));
    if (!run.hasValue) return Value();
    // Reading Python objects allocates nothing in the interpreter, so no collection
    // can run while the kept value is converted.
    FromPython conversion;
    conversion.where = "the result";
    Value result;
    bool converted = conversion.convert(ose_kept(), result, 0);
    ose_keep(nullptr);
    if (!converted) return std::unexpected(makeError(ErrorKind::Conversion, conversion.error));
    return result;
}

} // namespace

Result<void> Interpreter::importModule(std::string_view module, const CallOptions& options) {
    CallGuard guard(*impl_);
    if (!guard) return std::unexpected(busyError());
    if (!isDottedName(module))
        return std::unexpected(makeError(ErrorKind::Usage, std::format("'{}' is not a module name", module)));
    if (!impl_->moduleExists(module))
        return std::unexpected(makeError(ErrorKind::NotFound, std::format("no module named '{}'", module)));
    Run run;
    run.what = Run::What::Import;
    run.module = module;
    auto r = execute(*impl_, run, options);
    if (!r) return std::unexpected(std::move(r.error()));
    return {};
}

Result<Value> Interpreter::call(std::string_view module, std::string_view function, std::span<const Value> args,
                                const CallOptions& options) {
    CallGuard guard(*impl_);
    if (!guard) return std::unexpected(busyError());
    if (!isDottedName(module))
        return std::unexpected(makeError(ErrorKind::Usage, std::format("'{}' is not a module name", module)));
    if (!isDottedName(function))
        return std::unexpected(makeError(ErrorKind::Usage, std::format("'{}' is not a function name", function)));
    if (!impl_->moduleExists(module))
        return std::unexpected(makeError(ErrorKind::NotFound, std::format("no module named '{}'", module)));
    for (size_t i = 0; i < args.size(); ++i) {
        std::string path = std::format("argument {}", i + 1);
        std::string error;
        if (!checkValue(args[i], 0, path, error)) return std::unexpected(makeError(ErrorKind::Conversion, error));
    }
    Run run;
    run.what = Run::What::Call;
    run.module = module;
    for (std::string_view part : split(function, '.')) run.attributes.emplace_back(part);
    run.args = args;
    run.wantResult = true;
    return execute(*impl_, run, options);
}

Result<void> Interpreter::exec(std::string_view source, const CallOptions& options) {
    CallGuard guard(*impl_);
    if (!guard) return std::unexpected(busyError());
    if (!validUtf8(source)) return std::unexpected(makeError(ErrorKind::Usage, "the source is not valid UTF-8 text"));
    Run run;
    run.what = Run::What::Exec;
    run.source = source;
    auto r = execute(*impl_, run, options);
    if (!r) return std::unexpected(std::move(r.error()));
    return {};
}

Result<Value> Interpreter::eval(std::string_view expression, const CallOptions& options) {
    CallGuard guard(*impl_);
    if (!guard) return std::unexpected(busyError());
    if (!validUtf8(expression)) return std::unexpected(makeError(ErrorKind::Usage, "the source is not valid UTF-8 text"));
    Run run;
    run.what = Run::What::Eval;
    run.source = expression;
    run.wantResult = true;
    return execute(*impl_, run, options);
}

const Limits& Interpreter::limits() const {
    return impl_->limits;
}

void Interpreter::charge(int64_t units) {
    if (!impl_->inNative || units <= 0) return;
    impl_->nativeCharge = std::min(impl_->nativeCharge + std::min(units, OSE_BUDGET_UNLIMITED), OSE_BUDGET_UNLIMITED);
}

int64_t Interpreter::budgetUsed() const {
    return impl_->limits.budget - impl_->budgetLeft;
}

int64_t Interpreter::budgetLeft() const {
    return impl_->budgetLeft;
}

int64_t Interpreter::lastCallBudget() const {
    return impl_->lastCall;
}

const std::string& Interpreter::output() const {
    return impl_->output;
}

size_t Interpreter::heapUsed() {
    return ose_heap_used();
}

BytecodeCacheStats bytecodeCacheStats() {
    BytecodeCache& cache = bytecodeCache();
    std::lock_guard lock(cache.mutex);
    BytecodeCacheStats s;
    s.entries = cache.entries.size();
    s.bytes = cache.bytes;
    s.hits = cache.hits;
    s.misses = cache.misses;
    return s;
}

void clearBytecodeCache() {
    BytecodeCache& cache = bytecodeCache();
    std::lock_guard lock(cache.mutex);
    cache.evictUnused();
}

} // namespace opense4::script
