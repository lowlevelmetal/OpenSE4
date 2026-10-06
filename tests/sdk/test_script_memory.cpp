// The script runtime's heap (docs/sdk/runtime.md, "Memory"): where it places
// objects, how much a fragmented heap holds, and what creating many objects costs.
//
// The allocator is first fit, its searches starting at a hint per size class and,
// when that doesn't find room at once, going on through an index of the free runs
// (our patch to MicroPython's py/gc.c). The first test drives it directly and
// compares every placement with a model of plain first fit. The others run scripts:
// a stress test with many sizes, frees and reallocations in a fixed heap, and
// timings of object-heavy workloads at two sizes, which must grow in proportion (a
// search that rescans the heap makes ten times the objects cost a hundred times).

#include "script_test_util.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <vector>

extern "C" {
void gc_probe_init(void* heap, size_t bytes);
size_t gc_probe_index_bytes(void);
void gc_probe_use_index(void* mem);
size_t gc_probe_block_bytes(void);
size_t gc_probe_blocks(void);
size_t gc_probe_block_of(const void* p);
void* gc_probe_alloc(size_t bytes);
void* gc_probe_realloc(void* p, size_t bytes);
void gc_probe_free(void* p);
}

using namespace opense4::script;
using namespace opense4::script::test;

namespace {

// First-fit allocation over blocks, as MicroPython's gc_alloc and gc_realloc define
// it: n blocks go to the start of the lowest run of n free blocks; a reallocation
// shrinks in place, grows in place when the blocks after it are free, and otherwise
// moves to a new first-fit place (found while the old one is still taken).
class FirstFit {
public:
    explicit FirstFit(size_t blocks) : used_(blocks, 0) {}

    size_t blocks() const { return used_.size(); }
    size_t used() const { return used_count_; }

    std::optional<size_t> alloc(size_t n) {
        auto at = find(n);
        if (at) mark(*at, n, 1);
        return at;
    }
    void release(size_t start, size_t n) { mark(start, n, 0); }
    // The allocation's new start, or nothing when it can't grow (it stays as it was).
    std::optional<size_t> realloc(size_t start, size_t n, size_t want) {
        if (want <= n) {
            mark(start + want, n - want, 0);
            return start;
        }
        size_t room = n;
        for (size_t b = start + n; b < used_.size() && used_[b] == 0 && room < want; ++b) ++room;
        if (room >= want) {
            mark(start + n, want - n, 1);
            return start;
        }
        auto at = find(want);
        if (!at) return std::nullopt;
        mark(*at, want, 1);
        mark(start, n, 0);
        return at;
    }

private:
    std::optional<size_t> find(size_t n) const {
        const uint8_t* u = used_.data();
        size_t run = 0;
        for (size_t b = 0, end = used_.size(); b < end; ++b) {
            run = u[b] != 0 ? 0 : run + 1;
            if (run == n) return b + 1 - n;
        }
        return std::nullopt;
    }
    void mark(size_t start, size_t n, uint8_t v) {
        for (size_t b = start; b < start + n; ++b) {
            used_count_ = used_count_ - used_[b] + v;
            used_[b] = v;
        }
    }
    std::vector<uint8_t> used_;
    size_t used_count_ = 0;
};

// A fixed heap stressed with many sizes, frees and reallocations (lists that grow
// and shrink, dicts and attribute tables that are rebuilt as they grow). run() churns
// a fixed number of live objects; high_water() keeps adding live objects while it
// churns, until the heap runs out, and says how many it held.
constexpr std::string_view kStress = R"PY(
class Box:
    pass

NAMES = ['a%d' % j for j in range(64)]

class Rand:
    def __init__(self, seed):
        self.state = seed

    def below(self, n):
        self.state = (self.state * 1103515245 + 12345) & 0x7fffffff
        return (self.state >> 4) % n

def size(rand):
    r = rand.below(100)
    if r < 70:
        return rand.below(9)
    if r < 95:
        return 9 + rand.below(56)
    return 65 + rand.below(336)

def make(rand):
    k = size(rand)
    kind = rand.below(7)
    if kind == 0:
        return [k] * k
    if kind == 1:
        return {j: j for j in range(k)}
    if kind == 2:
        return bytes(8 * k)
    if kind == 3:
        b = Box()
        for j in range(min(k, 64)):
            setattr(b, NAMES[j], j)
        return b
    if kind == 4:
        return 'x' * (8 * k)
    if kind == 5:
        return tuple(range(k))
    out = []
    for j in range(k):
        out.append(j)
    return out

def change(rand, slots, n):
    i = rand.below(n)
    x = slots[i]
    if type(x) is list and len(x) > 0 and rand.below(2):
        for _ in range(len(x) // 2 + 1):
            x.pop()
    elif type(x) is list and len(x) < 400:
        x.extend(range(size(rand)))
    else:
        slots[i] = make(rand)

def churn(slots, n, steps, rand):
    for _ in range(steps):
        if rand.below(5) == 0:
            change(rand, slots, n)
        else:
            slots[rand.below(n)] = make(rand)

def run(n, steps, seed):
    rand = Rand(seed)
    slots = [None] * n
    churn(slots, n, steps, rand)
    return n

def high_water(n, capacity, seed):
    rand = Rand(seed)
    slots = [None] * capacity
    churn(slots, n, 2 * n, rand)
    try:
        while n < capacity:
            churn(slots, n, 4, rand)
            slots[n] = make(rand)
            n += 1
    except MemoryError:
        pass
    slots = None
    return n
)PY";

// Object-heavy workloads, timed at two sizes.
constexpr std::string_view kWorkloads = R"PY(
class R:
    pass

def objects(n):
    r = None
    for i in range(n):
        r = R()
        r.a = i
    return n

def kept_objects(n):
    out = []
    for i in range(n):
        r = R()
        r.a = i
        r.b = -i
        out.append(r)
    return len(out)

def dicts(n):
    index = {}
    for i in range(n):
        index[i] = {'id': i, 'owner': i % 7, 'name': 'ship'}
    return len(index)

def lists(n):
    rows = []
    for i in range(n):
        rows.append([i] * (i % 48))
    total = 0
    for row in rows:
        row.append(0)
        total += len(row)
    return total

def garbage(n):
    for i in range(n):
        a = [0] * (i % 50)
        b = {'k': i, 'v': a}
        c = str(i) + 'x'
    return n

def collected(n):
    live = []
    for i in range(n // 2):
        r = R()
        r.a = i
        r.b = [i, i]
        live.append(r)
    for i in range(4 * n):
        x = R()
        x.a = i
    return len(live)
)PY";

// Milliseconds one call of `function(n)` takes, in a fresh interpreter.
double timeWorkload(std::string_view function, int n, size_t heapBytes) {
    Limits limits;
    limits.heapBytes = heapBytes;
    auto interp = makeInterpreter(limits);
    REQUIRE(interp->addFile("workloads.py", std::string(kWorkloads)).has_value());
    REQUIRE(interp->importModule("workloads").has_value());
    const auto t0 = std::chrono::steady_clock::now();
    auto r = interp->call("workloads", function, std::vector<Value>{Value(n)});
    const auto t1 = std::chrono::steady_clock::now();
    REQUIRE_MESSAGE(r.has_value(), explain(r.error()));
    return std::chrono::duration<double, std::milli>(t1 - t0).count();
}

} // namespace

TEST_CASE("script memory: objects go exactly where first fit puts them") {
    // The probe uses MicroPython's globals: no interpreter may live meanwhile.
    REQUIRE_FALSE(Interpreter::active());
    // Without the index of free runs, searches go on from their hints; with it, those
    // that don't end within a few steps continue through the index.
    for (const bool withIndex : {false, true}) {
        CAPTURE(withIndex);
        constexpr size_t kHeapBytes = size_t{64} << 10;
        std::vector<std::max_align_t> heap(kHeapBytes / sizeof(std::max_align_t));
        gc_probe_init(heap.data(), kHeapBytes);
        std::vector<uint32_t> index(withIndex ? gc_probe_index_bytes() / sizeof(uint32_t) : 0);   // zeroed
        if (withIndex) gc_probe_use_index(index.data());
        const size_t blockBytes = gc_probe_block_bytes();
        FirstFit model(gc_probe_blocks());

        struct Live {
            void* p;
            size_t start, n;
        };
        std::vector<Live> live;
        std::mt19937 rng(20261005);
        // Mostly small sizes, as objects are, some medium and a few large ones; the
        // classes of the allocator's hints go up to 64 blocks.
        auto wanted = [&]() -> size_t {
            const auto r = rng() % 100;
            if (r < 60) return 1 + rng() % 8;
            if (r < 92) return 9 + rng() % 60;
            return 69 + rng() % 250;
        };
        auto bytesFor = [&](size_t n) { return n * blockBytes - rng() % blockBytes; };

        size_t allocs = 0, refused = 0, frees = 0, reallocs = 0, moved = 0;
        std::string mismatch;
        for (int step = 0; step < 30000 && mismatch.empty(); ++step) {
            const auto op = rng() % 100;
            const size_t fill = model.used() * 100 / model.blocks();
            if (live.empty() || fill < 60 || (fill < 92 && op < 45)) {
                const size_t n = wanted();
                void* p = gc_probe_alloc(bytesFor(n));
                const auto expected = model.alloc(n);
                ++allocs;
                if ((p != nullptr) != expected.has_value() || (p != nullptr && gc_probe_block_of(p) != *expected)) {
                    mismatch = std::format("step {}: {} blocks went to {} instead of {}", step, n,
                                           p ? std::to_string(gc_probe_block_of(p)) : "nowhere",
                                           expected ? std::to_string(*expected) : "nowhere");
                } else if (p == nullptr) {
                    ++refused;
                } else {
                    live.push_back({p, *expected, n});
                }
            } else if (op < 80) {
                const size_t i = rng() % live.size();
                gc_probe_free(live[i].p);
                model.release(live[i].start, live[i].n);
                live[i] = live.back();
                live.pop_back();
                ++frees;
            } else {
                Live& l = live[rng() % live.size()];
                const size_t n = rng() % 4 == 0 ? 1 + rng() % l.n : wanted();
                void* p = gc_probe_realloc(l.p, bytesFor(n));
                const auto expected = model.realloc(l.start, l.n, n);
                ++reallocs;
                if ((p != nullptr) != expected.has_value() || (p != nullptr && gc_probe_block_of(p) != *expected)) {
                    mismatch = std::format("step {}: {} blocks at {} reallocated to {} went to {} instead of {}", step,
                                           l.n, l.start, n, p ? std::to_string(gc_probe_block_of(p)) : "nowhere",
                                           expected ? std::to_string(*expected) : "nowhere");
                } else if (p != nullptr) {
                    if (*expected != l.start) ++moved;
                    l = {p, *expected, n};
                }
            }
        }
        CHECK_MESSAGE(mismatch.empty(), mismatch);
        MESSAGE(std::format("{} blocks of {} bytes{}: {} allocations ({} found no room), {} frees, {} reallocations ({} moved)",
                            model.blocks(), blockBytes, withIndex ? ", with the index" : "", allocs, refused, frees,
                            reallocs, moved));
        CHECK(refused > 0);   // the heap was full at times
        CHECK(moved > 0);
    }
}

TEST_CASE("script memory: a fragmented heap holds as much as plain first fit") {
    Limits limits;
    limits.heapBytes = size_t{2} << 20;
    {
        // Churning a fixed set of live objects, collected again and again: MicroPython's
        // own allocator completes it too, with room to spare in every build (with this
        // sequence the heap runs out near 2700 objects in a debug build on x86_64, and
        // somewhere from 1500 to 2000 on 32-bit ARM and under the sanitizers, whose
        // stack frames leave the collector more stale pointers that keep garbage alive).
        auto interp = makeInterpreter(limits);
        REQUIRE(interp->addFile("stress.py", std::string(kStress)).has_value());
        auto r = interp->call("stress", "run", std::vector<Value>{Value(1000), Value(30000), Value(7)});
        REQUIRE_MESSAGE(r.has_value(), explain(r.error()));
    }
    // How many live objects the heap holds before an allocation first fails, for four
    // sequences. Placement is plain first fit's, so MicroPython's own allocator
    // reaches the same numbers. When this test was written: 2550, 2936, 2945 and 3892
    // (12323 in all) in a debug build on x86_64 Linux, 12247 in an optimized one, and
    // 10713 on 32-bit ARM. They vary a little between builds with the stale pointers
    // the collector finds on the C stack.
    std::string report;
    int64_t total = 0;
    for (int seed : {11, 21, 31, 41}) {
        auto interp = makeInterpreter(limits);
        REQUIRE(interp->addFile("stress.py", std::string(kStress)).has_value());
        auto r = interp->call("stress", "high_water", std::vector<Value>{Value(1000), Value(10000), Value(seed)});
        REQUIRE_MESSAGE(r.has_value(), explain(r.error()));
        report += std::format(" {}", r->asInt());
        total += r->asInt();
    }
    MESSAGE("a 2 MiB heap held" << report << " live objects of mixed sizes before an allocation failed");
    CHECK(total >= (sizeof(void*) == 8 ? 11500 : 10000));
}

TEST_CASE("script memory: ten times the objects take about ten times as long") {
    struct Workload {
        std::string_view name;
        size_t heapBytes10k, heapBytes100k;
    };
    // The heaps are large enough for the larger size to run without a collection,
    // except for `collected`, whose heap holds its live objects about twice over at
    // either size, so that it is collected about as often.
    const Workload workloads[] = {
        {"objects", size_t{64} << 20, size_t{64} << 20},
        {"kept_objects", size_t{64} << 20, size_t{64} << 20},
        {"dicts", size_t{64} << 20, size_t{64} << 20},
        {"lists", size_t{64} << 20, size_t{64} << 20},
        {"garbage", size_t{64} << 20, size_t{64} << 20},
        {"collected", size_t{2} << 20, size_t{20} << 20},
    };
    std::string report;
    for (const Workload& w : workloads) {
        double small = timeWorkload(w.name, 10'000, w.heapBytes10k);
        small = std::min(small, timeWorkload(w.name, 10'000, w.heapBytes10k));
        small = std::min(small, timeWorkload(w.name, 10'000, w.heapBytes10k));
        const double large = timeWorkload(w.name, 100'000, w.heapBytes100k);
        const double ratio = large / std::max(small, 0.001);
        report += std::format("\n  {:<14} 10k: {:8.1f} ms   100k: {:8.1f} ms   x{:.1f}", w.name, small, large, ratio);
        // In proportion is about 10; a search that rescans the heap gives about 100.
        CHECK_MESSAGE(ratio < 30.0, std::format("{}: {:.1f} ms for 10k, {:.1f} ms for 100k", w.name, small, large));
    }
    MESSAGE("object-heavy workloads:" << report);
}
