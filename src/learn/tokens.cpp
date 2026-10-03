#include "learn/tokens.hpp"

#include "datafile/datafile.hpp"
#include "learn/condition.hpp"

#include <cctype>
#include <format>
#include <optional>

namespace opense4::learn {

namespace {

// A token found in a text: "{kind:argument}" from `begin` to `end` (past the brace).
struct Found {
    size_t begin = 0, end = 0;
    std::string kind, argument;
    bool closed = false;
};

// The next token at or after `from`: a brace, a lower-case word and a colon.
// A brace not followed by that is plain text.
std::optional<Found> nextToken(std::string_view text, size_t from) {
    for (size_t at = text.find('{', from); at != std::string_view::npos; at = text.find('{', at + 1)) {
        size_t i = at + 1;
        while (i < text.size() && (std::islower(static_cast<unsigned char>(text[i])) || text[i] == '-')) ++i;
        if (i == at + 1 || i >= text.size() || text[i] != ':') continue;
        Found f;
        f.begin = at;
        f.kind = std::string(text.substr(at + 1, i - at - 1));
        const size_t close = text.find_first_of("}{\n", i + 1);
        f.closed = close != std::string_view::npos && text[close] == '}';
        f.end = f.closed ? close + 1 : (close == std::string_view::npos ? text.size() : close);
        f.argument = std::string(text.substr(i + 1, (f.closed ? close : f.end) - i - 1));
        return f;
    }
    return std::nullopt;
}

void blockProblems(const std::vector<Block>& blocks, std::vector<std::string>& out);

void inlineProblems(const Inline& text, std::vector<std::string>& out) {
    for (const Span& s : text)
        for (std::string& p : tokenProblems(s.text)) out.push_back(std::move(p));
}

void listProblems(const List& l, std::vector<std::string>& out) {
    for (const ListItem& item : l.items) {
        inlineProblems(item.text, out);
        for (const List& sub : item.sub) listProblems(sub, out);
    }
}

void blockProblems(const std::vector<Block>& blocks, std::vector<std::string>& out) {
    for (const Block& b : blocks) {
        inlineProblems(b.text, out);
        listProblems(b.list, out);
        for (const Inline& cell : b.table.header) inlineProblems(cell, out);
        for (const auto& row : b.table.rows)
            for (const Inline& cell : row) inlineProblems(cell, out);
        blockProblems(b.tip, out);
    }
}

void expandInline(Inline& text, const game::GameState& s, game::EmpireId e) {
    for (Span& span : text) span.text = expandTokens(span.text, s, e);
}

void expandList(List& l, const game::GameState& s, game::EmpireId e) {
    for (ListItem& item : l.items) {
        expandInline(item.text, s, e);
        for (List& sub : item.sub) expandList(sub, s, e);
    }
}

void expandBlocks(std::vector<Block>& blocks, const game::GameState& s, game::EmpireId e) {
    for (Block& b : blocks) {
        expandInline(b.text, s, e);
        expandList(b.list, s, e);
        for (Inline& cell : b.table.header) expandInline(cell, s, e);
        for (auto& row : b.table.rows)
            for (Inline& cell : row) expandInline(cell, s, e);
        expandBlocks(b.tip, s, e);
    }
}

} // namespace

std::vector<std::string> tokenProblems(std::string_view text) {
    std::vector<std::string> out;
    for (auto f = nextToken(text, 0); f; f = nextToken(text, f->end)) {
        if (!f->closed) out.push_back(std::format("the token '{{{}:{}' has no closing brace", f->kind, f->argument));
        else if (f->kind != "design") out.push_back(std::format("unknown token '{{{}:{}}}' (the tokens: {{design:<type>}})", f->kind, f->argument));
        else if (!isDesignTypeName(f->argument))
            out.push_back(std::format("unknown design type '{}' in {{design:{}}} (the AI design types of spec 05 §7.7, such as \"Attack Ship\", or "
                                      "\"Colony\")",
                                      f->argument, f->argument));
    }
    return out;
}

std::vector<std::string> tokenProblems(const std::vector<Block>& blocks) {
    std::vector<std::string> out;
    blockProblems(blocks, out);
    return out;
}

std::string designNameOfType(const game::GameState& s, game::EmpireId empire, std::string_view type) {
    if (!empire.valid() || empire.index() >= s.empires.size()) return {};
    const game::Empire& e = s.empire(empire);
    // "Colony": the colony ship of the race's own planet type comes first.
    std::string native;
    if (datafile::keysEqual(type, "Colony")) {
        const std::string_view surface = e.race.nativeSurface;
        native = std::format("Colony ({})", datafile::keysEqual(surface, "Ice") ? "Ice" : datafile::keysEqual(surface, "Rock") ? "Rock" : "Gas");
    }
    const game::Design* best = nullptr;
    int bestRank = -1;
    for (game::DesignId id : e.designs) {
        if (id.index() >= s.designs.size()) continue;
        const game::Design& d = s.design(id);
        if (d.obsolete || !designTypeMatches(d.designType, type)) continue;
        const int rank = native.empty() || datafile::keysEqual(d.designType, native) ? 1 : 0;
        // The newest wins; of designs made in one turn, the one made last.
        if (!best || rank > bestRank || (rank == bestRank && (d.createdTurn > best->createdTurn ||
                                                              (d.createdTurn == best->createdTurn && d.id.value > best->id.value)))) {
            best = &d;
            bestRank = rank;
        }
    }
    return best ? best->name : std::string{};
}

std::string expandTokens(std::string_view text, const game::GameState& s, game::EmpireId empire) {
    std::string out;
    size_t from = 0;
    for (auto f = nextToken(text, 0); f; f = nextToken(text, f->end)) {
        out += text.substr(from, f->begin - from);
        from = f->end;
        if (!f->closed || f->kind != "design") {
            out += text.substr(f->begin, f->end - f->begin);
            continue;
        }
        const std::string name = designNameOfType(s, empire, f->argument);
        out += name.empty() ? f->argument : name;
    }
    out += text.substr(from);
    return out;
}

std::vector<Block> expandTokens(const std::vector<Block>& blocks, const game::GameState& s, game::EmpireId empire) {
    std::vector<Block> out = blocks;
    expandBlocks(out, s, empire);
    return out;
}

std::string tokensAsWords(std::string_view text) {
    std::string out;
    size_t from = 0;
    for (auto f = nextToken(text, 0); f; f = nextToken(text, f->end)) {
        out += text.substr(from, f->begin - from);
        out += "Name";
        from = f->end;
    }
    out += text.substr(from);
    return out;
}

} // namespace opense4::learn
