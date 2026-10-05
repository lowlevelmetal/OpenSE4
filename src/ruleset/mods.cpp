#include "ruleset/mods.hpp"

#include "core/hash.hpp"

#include <algorithm>
#include <format>

namespace opense4::ruleset {

std::string_view displayName(Combine c) {
    switch (c) {
        case Combine::Sum: return "sum";
        case Combine::Max: return "max";
        case Combine::Min: return "min";
        case Combine::Count: break;
    }
    return "?";
}

std::string modSetIdentity(std::span<const ModRecord> mods) {
    Hasher h;
    size_t count = 0;
    for (const ModRecord& m : mods) {
        if (!m.affectsGame) continue;
        ++count;
        h.add(std::string_view(m.id));
        h.add(std::string_view(m.version));
        h.add(std::string_view(m.hash));
    }
    if (count == 0) return {};
    h.addSize(count);
    return std::format("{:016x}", h.value());
}

std::string describeMods(std::span<const ModRecord> mods) {
    std::string out;
    for (const ModRecord& m : mods) out += std::format("{}{} {}", out.empty() ? "" : ", ", m.id, m.version);
    return out.empty() ? std::string("none") : out;
}

std::vector<std::string> compareModSets(std::span<const ModRecord> game, std::span<const ModRecord> mine, std::string_view theirs) {
    std::vector<const ModRecord*> wanted, have;
    for (const ModRecord& m : game)
        if (m.affectsGame) wanted.push_back(&m);
    for (const ModRecord& m : mine)
        if (m.affectsGame) have.push_back(&m);
    auto find = [](const std::vector<const ModRecord*>& list, std::string_view id) -> const ModRecord* {
        for (const ModRecord* m : list)
            if (m->id == id) return m;
        return nullptr;
    };
    auto shortHash = [](const std::string& hash) { return hash.substr(0, std::min<size_t>(hash.size(), 8)); };
    std::vector<std::string> out;
    for (const ModRecord* m : wanted) {
        const ModRecord* local = find(have, m->id);
        if (!local)
            out.push_back(std::format("{} uses mod {} {}, which you do not have enabled", theirs, m->id, m->version));
        else if (local->version != m->version)
            out.push_back(std::format("mod {}: {} uses version {}, you have {}", m->id, theirs, m->version, local->version));
        else if (local->hash != m->hash)
            out.push_back(std::format("mod {} {}: your copy's files differ from the one {} uses ({} here, {} there)", m->id, m->version, theirs,
                                      shortHash(local->hash), shortHash(m->hash)));
    }
    for (const ModRecord* m : have)
        if (!find(wanted, m->id))
            out.push_back(std::format("mod {} {} changes the game, and {} does not use it: turn it off", m->id, m->version, theirs));
    if (out.empty()) {
        bool sameOrder = true;
        for (size_t i = 0; i < wanted.size() && i < have.size(); ++i) sameOrder = sameOrder && wanted[i]->id == have[i]->id;
        if (!sameOrder) {
            std::vector<ModRecord> a, b;
            for (const ModRecord* m : wanted) a.push_back(*m);
            for (const ModRecord* m : have) b.push_back(*m);
            out.push_back(std::format("the mods load in another order ({}: {}; yours: {})", theirs, describeMods(a), describeMods(b)));
        }
    }
    return out;
}

} // namespace opense4::ruleset
