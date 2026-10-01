// The learning system's headless parts (src/learn, docs/LEARNING.md): the
// Markdown subset, the lesson and training loaders, the conditions against
// the engine fixture, the content library, and a check of every built-in
// file under assets/learn.

#include "engine_fixture.hpp"

#include "game/research.hpp"
#include "game/score.hpp"
#include "learn/condition.hpp"
#include "learn/ids.hpp"
#include "learn/library.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <format>

using namespace opense4;
using namespace opense4::learn;
using namespace opense4::test;

namespace {

const std::filesystem::path kFixtures = std::filesystem::path(OPENSE4_FIXTURE_DIR) / "learn";

std::string problemsText(const std::vector<Diagnostic>& problems) {
    std::string out;
    for (const Diagnostic& d : problems) out += d.text() + "\n";
    return out;
}

bool hasProblem(const std::vector<Diagnostic>& problems, int line, std::string_view part) {
    return std::any_of(problems.begin(), problems.end(), [&](const Diagnostic& d) {
        return (line == 0 || d.line == line) && d.message.find(part) != std::string::npos;
    });
}

// One condition parsed from a TOML inline table ("{ colonies = 2 }").
Condition condition(std::string_view table) {
    std::vector<Diagnostic> problems;
    const std::string text = std::format("title = \"t\"\n[[objective]]\ntext = \"o\"\nwhen = {}\n", table);
    auto l = parseLesson(text, "test.toml", LessonKind::Training, problems);
    REQUIRE_MESSAGE(l.has_value(), problemsText(problems));
    return l->objectives.front().when;
}

} // namespace

// ---- Markdown ----------------------------------------------------------------------------------

TEST_CASE("learn: inline markup gives styled spans and links") {
    const Inline t = parseInline("Press `F1` for **bold *and italic*** text, [a **link**](colonies#founding) and \\*stars\\*.");
    REQUIRE(t.size() >= 7);
    CHECK(t[0] == Span{"Press ", false, false, false, ""});
    CHECK(t[1] == Span{"F1", false, false, true, ""});
    CHECK(t[3] == Span{"bold ", true, false, false, ""});
    CHECK(t[4] == Span{"and italic", true, true, false, ""});
    const auto link = std::find_if(t.begin(), t.end(), [](const Span& s) { return s.text == "link"; });
    REQUIRE(link != t.end());
    CHECK(link->bold);
    CHECK(link->link == "colonies#founding");
    CHECK(plainText(t) == "Press F1 for bold and italic text, a link and *stars*.");
    // Markers without a partner stay as they are.
    CHECK(plainText(parseInline("2 * 3 and turns_passed")) == "2 * 3 and turns_passed");
    CHECK(plainText(parseInline("[not a link] here")) == "[not a link] here");
}

TEST_CASE("learn: anchors are the lower-case heading text") {
    CHECK(anchorOf("Your homeworld") == "your-homeworld");
    CHECK(anchorOf("Ships & fleets") == "ships--fleets");
    CHECK(anchorOf("F1: Help (the encyclopedia)") == "f1-help-the-encyclopedia");
}

TEST_CASE("learn: a manual page parses into blocks with front matter and anchors") {
    const auto text = DirectorySource(kFixtures).read("manual/01-basics.md");
    REQUIRE(text);
    const Document doc = parseMarkdown(*text, "basics.md");
    CHECK_MESSAGE(doc.problems.empty(), problemsText(doc.problems));
    CHECK(doc.title == "The basics");
    CHECK(doc.windows == std::vector<std::string>{"main", "empire-status"});

    std::vector<const Block*> headings;
    for (const Block& b : doc.blocks)
        if (b.kind == Block::Kind::Heading) headings.push_back(&b);
    REQUIRE(headings.size() == 5);
    CHECK(headings[0]->level == 1);
    CHECK(headings[1]->anchor == "the-main-window");
    CHECK(headings[2]->level == 3);
    CHECK(headings[2]->anchor == "the-status-bar");
    CHECK(headings[3]->anchor == "tips-and-tables");
    CHECK(headings[4]->anchor == "the-main-window-1");   // the duplicate

    // The paragraph's lines are joined.
    const Block& intro = doc.blocks[1];
    REQUIRE(intro.kind == Block::Kind::Paragraph);
    CHECK(plainText(intro.text) == "A page written for the tests. Press F1 for help, read slowly, and continue on the next line of the same paragraph.");
    CHECK(intro.line == 6);

    auto find = [&](Block::Kind k) {
        return std::find_if(doc.blocks.begin(), doc.blocks.end(), [&](const Block& b) { return b.kind == k; });
    };
    const auto tip = find(Block::Kind::Tip);
    REQUIRE(tip != doc.blocks.end());
    REQUIRE(tip->tip.size() == 2);
    CHECK(tip->tip[0].kind == Block::Kind::Paragraph);
    CHECK(tip->tip[1].kind == Block::Kind::List);

    const auto table = find(Block::Kind::Table);
    REQUIRE(table != doc.blocks.end());
    CHECK(table->table.header.size() == 2);
    CHECK(table->table.align == std::vector<Align>{Align::Left, Align::Center});
    REQUIRE(table->table.rows.size() == 2);
    CHECK(table->table.rows[1][0].front().code);

    const auto list = find(Block::Kind::List);
    REQUIRE(list != doc.blocks.end());
    CHECK_FALSE(list->list.numbered);
    REQUIRE(list->list.items.size() == 2);
    CHECK(plainText(list->list.items[0].text) == "First item continued here");
    REQUIRE(list->list.items[0].sub.size() == 1);
    CHECK(plainText(list->list.items[0].sub[0].items[0].text) == "A nested item");
    const auto numbered = std::find_if(list + 1, doc.blocks.end(), [](const Block& b) { return b.kind == Block::Kind::List; });
    REQUIRE(numbered != doc.blocks.end());
    CHECK(numbered->list.numbered);
    CHECK(numbered->list.items.size() == 2);

    const auto links = collectLinks(doc.blocks);
    std::vector<std::string> targets;
    for (const LinkUse& l : links) targets.push_back(l.target);
    CHECK(targets == std::vector<std::string>{"window:research", "help:components", "colonies", "colonies#founding-colonies", "#tips-and-tables"});
}

TEST_CASE("learn: the parser reports what it does not use") {
    const Document doc = parseMarkdown("---\nwindow: x\n---\n# One\n#### Deep\n# Two\n", "p.md");
    CHECK(hasProblem(doc.problems, 2, "unknown front matter key 'window'"));
    CHECK(hasProblem(doc.problems, 5, "only #, ## and ###"));
    CHECK(hasProblem(doc.problems, 6, "one '#' title"));
    CHECK(doc.title == "One");
}

TEST_CASE("learn: links name pages, windows, Help tabs and web pages") {
    CHECK(parseLink("colonies").kind == Link::Kind::Page);
    CHECK(parseLink("colonies#founding").anchor == "founding");
    CHECK(parseLink("#here").target.empty());
    CHECK(parseLink("window:research").kind == Link::Kind::Window);
    CHECK(parseLink("help:components").target == "components");
    CHECK(parseLink("https://example.org").kind == Link::Kind::External);
    CHECK(parseLink("mailto:x").kind == Link::Kind::Invalid);
    CHECK(parseLink("window:").kind == Link::Kind::Invalid);
}

// ---- Vocabularies -------------------------------------------------------------------------------

TEST_CASE("learn: the vocabularies come from the engine and the client") {
    CHECK(isCommandName("QueueAdd"));
    CHECK(isCommandName("SetResearch"));
    CHECK(commandNames().size() == std::variant_size_v<game::Command>);
    CHECK_FALSE(isCommandName("queue-add"));
    for (size_t i = 0; i < static_cast<size_t>(game::OrderKind::Count); ++i) {
        const auto k = static_cast<game::OrderKind>(i);
        CHECK(orderKindFromId(orderKindId(k)) == k);
    }
    CHECK(isUiTag("window:research"));
    CHECK(isUiTag("order:colonize"));
    CHECK(isUiTag("button:end-turn"));
    CHECK_FALSE(isUiTag("window:nothing"));
    CHECK_FALSE(isUiTag("order:fly"));
    CHECK(orderStripId("Move") == "move-to");
    CHECK(findWindow("create-design"));
    CHECK_FALSE(findWindow("tactical-combat")->openable);
    // Tags are unique.
    std::vector<std::string_view> tags(fixedUiTags().begin(), fixedUiTags().end());
    std::sort(tags.begin(), tags.end());
    CHECK(std::adjacent_find(tags.begin(), tags.end()) == tags.end());
}

// ---- Lessons ------------------------------------------------------------------------------------

TEST_CASE("learn: a tutorial loads with its setup and steps") {
    std::vector<Diagnostic> problems;
    const auto text = DirectorySource(kFixtures).read("tutorials/01-first-steps.toml");
    REQUIRE(text);
    const auto l = parseLesson(*text, "first-steps.toml", LessonKind::Tutorial, problems);
    REQUIRE_MESSAGE(l.has_value(), problemsText(problems));
    CHECK(l->title == "First steps");
    CHECK(l->minutes == 5);
    CHECK(l->setup.seed == 12345u);
    CHECK(l->setup.computerPlayers == 1);
    CHECK(l->setup.systems == 12);
    REQUIRE(l->steps.size() == 4);
    CHECK_FALSE(l->steps[0].done.has_value());
    CHECK(l->steps[1].highlight == std::vector<std::string>{"command:research"});
    REQUIRE(l->steps[1].done);
    CHECK(l->steps[1].done->fact == Fact::Window);
    CHECK(l->steps[1].manual == "basics#the-main-window");
    CHECK(l->steps[2].text.size() == 2);   // a paragraph and a list
    CHECK(l->steps[2].text[0].line == 26);  // lines count in the file
    CHECK(l->steps[2].text[1].line == 28);
}

TEST_CASE("learn: a training game loads with objectives, pages, hints and a fail rule") {
    std::vector<Diagnostic> problems;
    const auto text = DirectorySource(kFixtures).read("training/01-expansion.toml");
    REQUIRE(text);
    const auto l = parseLesson(*text, "expansion.toml", LessonKind::Training, problems);
    REQUIRE_MESSAGE(l.has_value(), problemsText(problems));
    REQUIRE(l->objectives.size() == 2);
    CHECK(l->objectives[0].byTurn == 30u);
    CHECK_FALSE(l->objectives[1].byTurn);
    REQUIRE(l->pages.size() == 2);
    CHECK(l->pages[1].series == "briefing");
    REQUIRE(l->hints.size() == 1);
    CHECK(l->hints[0].title == "Hint");
    CHECK(l->hints[0].when.op == Condition::Op::All);
    REQUIRE(l->fail);
    CHECK(l->fail->when.op == Condition::Op::Not);
    CHECK(l->setup.quadrantSize == 0);
    CHECK(l->setup.events == 0);

    game::GameOptions o;
    applySetup(l->setup, o);
    CHECK(o.quadrantSize == 0);
    CHECK(o.eventFrequency == 0);
    CHECK_FALSE(o.simultaneous);
}

TEST_CASE("learn: lesson errors name the file and line") {
    std::vector<Diagnostic> problems;
    const char* text = R"(title = "Bad"
colour = "red"

[setup]
seed = "seven"
turn_style = "sideways"

[[step]]
title = "One"
text = "Text"
highlight = ["window:research", "panel:nowhere"]
done = { colonise = 2 }

[[step]]
title = "Two"
text = "Text"
done = { all = [{ window = "nowhere" }, { command = "Fly" }, { order = "fly" }, { selected = "moon" }] }

[[step]]
text = "No title"
done = { turn = "five" }
)";
    CHECK_FALSE(parseLesson(text, "bad.toml", LessonKind::Tutorial, problems).has_value());
    for (const Diagnostic& d : problems) CHECK(d.file == "bad.toml");
    CHECK(hasProblem(problems, 2, "unknown key 'colour'"));
    CHECK(hasProblem(problems, 5, "'seed' must be a whole number"));
    CHECK(hasProblem(problems, 6, "'turn_style' must be one of"));
    CHECK(hasProblem(problems, 11, "unknown UI tag 'panel:nowhere'"));
    CHECK(hasProblem(problems, 12, "unknown condition key 'colonise'"));
    CHECK(hasProblem(problems, 17, "unknown window id 'nowhere'"));
    CHECK(hasProblem(problems, 17, "unknown command name 'Fly'"));
    CHECK(hasProblem(problems, 17, "unknown order kind 'fly'"));
    CHECK(hasProblem(problems, 17, "unknown selection kind 'moon'"));
    CHECK(hasProblem(problems, 19, "needs 'title'"));
    CHECK(hasProblem(problems, 21, "'turn' takes a whole number"));

    problems.clear();
    CHECK_FALSE(parseLesson("title = \"x\"\n[[step]\n", "broken.toml", LessonKind::Tutorial, problems).has_value());
    REQUIRE(problems.size() == 1);
    CHECK(problems.front().line == 2);

    problems.clear();
    CHECK_FALSE(parseLesson("title = \"x\"\n[[step]]\ntitle = \"s\"\ntext = \"t\"\n", "t.toml", LessonKind::Training, problems).has_value());
    CHECK(hasProblem(problems, 2, "unknown key 'step'"));
    CHECK(hasProblem(problems, 0, "at least one [[objective]]"));
}

TEST_CASE("learn: slugs drop the order number and the extension") {
    CHECK(slugOf("03-first-colony.toml") == "first-colony");
    CHECK(slugOf("manual/10-ship-design.md") == "ship-design");
    CHECK(slugOf("plain.md") == "plain");
    CHECK(slugOf("2024.md") == "2024");
}

// ---- Conditions against the engine fixture --------------------------------------------------------

namespace {

struct Eval {
    const game::Rules& rules = engineRules();
    game::GameState state = newEngineGame(7, 2, 12, true);
    game::EmpireId me{0u};
    ClientFacts client;
    Tracker tracker;
    Mark mark;

    Eval() { mark = markNow(rules, state, me, tracker); }
    bool operator()(std::string_view table) {
        const Condition c = condition(table);
        return holds(c, EvalContext{rules, state, me, client, tracker, mark});
    }
    int64_t value(Fact f) { return factValue(f, EvalContext{rules, state, me, client, tracker, mark}); }
    game::Empire& empire() { return state.empires[0]; }
};

} // namespace

TEST_CASE("learn conditions: client facts") {
    Eval ev;
    CHECK_FALSE(ev("{ window = \"research\" }"));
    ev.client.openWindows = {"designs", "research"};
    CHECK(ev("{ window = \"research\" }"));
    CHECK_FALSE(ev("{ selected = \"planet\" }"));
    ev.client.selected = {"planet", "colony"};
    CHECK(ev("{ selected = \"colony\" }"));
    CHECK(ev("{ all = [{ window = \"designs\" }, { selected = \"planet\" }] }"));
    CHECK(ev("{ any = [{ window = \"log\" }, { selected = \"planet\" }] }"));
    CHECK_FALSE(ev("{ not = { selected = \"planet\" } }"));
    // Several keys in one table must all hold.
    CHECK(ev("{ window = \"designs\", selected = \"planet\" }"));
    CHECK_FALSE(ev("{ window = \"log\", selected = \"planet\" }"));
}

TEST_CASE("learn conditions: commands and orders count from the mark") {
    Eval ev;
    ev.tracker.issued(game::cmd::SetResearch{});
    CHECK(ev("{ command = \"SetResearch\" }"));
    CHECK_FALSE(ev("{ command = \"QueueAdd\" }"));
    game::cmd::SetOrders orders;
    orders.orders.push_back(game::Order{game::OrderKind::Colonize});
    ev.tracker.issued(orders);
    CHECK(ev("{ order = \"colonize\" }"));
    CHECK_FALSE(ev("{ order = \"move-to\" }"));
    // A new step starts counting afresh.
    ev.mark = markNow(ev.rules, ev.state, ev.me, ev.tracker);
    CHECK_FALSE(ev("{ command = \"SetResearch\" }"));
    CHECK_FALSE(ev("{ order = \"colonize\" }"));
}

TEST_CASE("learn conditions: time") {
    Eval ev;
    CHECK(ev("{ turn = 0 }"));
    CHECK_FALSE(ev("{ turn = 3 }"));
    CHECK_FALSE(ev("{ turns_passed = 1 }"));
    ev.state.turn += 3;
    CHECK(ev("{ turn = 3 }"));
    CHECK(ev("{ turns_passed = 3 }"));
    ev.mark = markNow(ev.rules, ev.state, ev.me, ev.tracker);
    CHECK_FALSE(ev("{ turns_passed = 1 }"));
}

TEST_CASE("learn conditions: what the empire owns") {
    Eval ev;
    game::GameState& s = ev.state;
    const game::Rules& r = ev.rules;
    const int64_t colonies = ev.value(Fact::Colonies);
    CHECK(colonies >= 1);
    CHECK(ev(std::format("{{ colonies = {} }}", colonies)));
    CHECK_FALSE(ev(std::format("{{ colonies = {} }}", colonies + 1)));
    int64_t people = 0;
    for (const auto& c : s.colonies)
        if (c && c->owner == ev.me) people += c->totalPopulation();
    CHECK(ev.value(Fact::Population) == people);

    const game::Location home{ev.empire().homeSystem, s.galaxy.object(homeworld(s, ev.me).planet).sector};
    const int64_t ships = ev.value(Fact::Ships), bases = ev.value(Fact::Bases), units = ev.value(Fact::Units);
    const game::DesignId frigate = addTestDesign(s, r, ev.me, "Learner", "Test Frigate", {"Test Bridge", "Test Engine"});
    const game::DesignId station = addTestDesign(s, r, ev.me, "Anchor", "Test Station", {"Test Bridge", "Test Life Support"});
    const game::DesignId wasp = addTestDesign(s, r, ev.me, "Wasp", "Test Fighter Hull", {"Test Fighter Gun", "Test Fighter Engine"});
    addTestVehicle(s, r, frigate, home);
    addTestVehicle(s, r, station, home);
    game::Vehicle& fighters = addTestVehicle(s, r, wasp, home);
    fighters.count = 4;
    CHECK(ev.value(Fact::Ships) == ships + 1);
    CHECK(ev.value(Fact::Bases) == bases + 1);
    CHECK(ev.value(Fact::Units) == units + 4);
    CHECK(ev(std::format("{{ ships = {} }}", ships + 1)));

    // Designs: obsolete ones do not count.
    const int64_t designs = ev.value(Fact::Designs);
    s.designs[frigate.index()].obsolete = true;
    CHECK(ev.value(Fact::Designs) == designs - 1);

    // Fleets.
    const int64_t fleets = ev.value(Fact::Fleets);
    game::Fleet f;
    f.id = game::FleetId{static_cast<uint32_t>(s.fleets.size())};
    f.owner = ev.me;
    s.fleets.push_back(f);
    CHECK(ev.value(Fact::Fleets) == fleets + 1);

    // The treasury.
    ev.empire().stockpile = game::Resources{100, 200, 300};
    CHECK(ev("{ minerals = 100, organics = 200, radioactives = 300 }"));
    CHECK_FALSE(ev("{ radioactives = 301 }"));
}

TEST_CASE("learn conditions: queues, research and the score") {
    Eval ev;
    game::GameState& s = ev.state;
    const game::Rules& r = ev.rules;
    CHECK_FALSE(ev("{ research_queued = 1 }"));
    ev.empire().research.push_back({techArea(r, "Test Construction"), 0});
    CHECK(ev("{ research_queued = 1 }"));

    const int64_t queued = ev.value(Fact::ConstructionQueued);
    homeworld(s, ev.me).queue.items.push_back(game::QueueItem{});
    homeworld(s, ev.me).queue.items.push_back(game::QueueItem{});
    CHECK(ev.value(Fact::ConstructionQueued) == queued + 2);

    CHECK_FALSE(ev("{ techs_researched = 1 }"));
    ev.empire().techLevels[techArea(r, "Test Construction").index()] += 2;
    CHECK(ev("{ techs_researched = 2 }"));
    CHECK(ev.value(Fact::Score) == game::score::empireScore(r, s, ev.me));
    CHECK(ev(std::format("{{ score = {} }}", ev.value(Fact::Score))));
}

TEST_CASE("learn conditions: exploration and diplomacy") {
    Eval ev;
    game::GameState& s = ev.state;
    const int64_t explored = ev.value(Fact::SystemsExplored);
    CHECK(explored >= 1);
    for (size_t i = 0; i < ev.empire().knowledge.explored.size(); ++i)
        if (!ev.empire().knowledge.explored[i]) {
            ev.empire().knowledge.explored[i] = 1;
            break;
        }
    CHECK(ev.value(Fact::SystemsExplored) == explored + 1);

    game::Relation& rel = ev.empire().relation(game::EmpireId{1u});
    rel.contact = false;
    CHECK_FALSE(ev("{ empires_met = 1 }"));
    rel.contact = true;
    rel.treaty = game::Treaty::None;
    CHECK(ev("{ empires_met = 1 }"));
    CHECK_FALSE(ev("{ treaties = 1 }"));
    rel.treaty = game::Treaty::TradeAlliance;
    CHECK(ev("{ treaties = 1 }"));
    // A destroyed empire is not counted.
    s.empires[1].alive = false;
    CHECK_FALSE(ev("{ empires_met = 1 }"));
}

TEST_CASE("learn conditions: enemy ships destroyed count each battle once") {
    Eval ev;
    game::GameState& s = ev.state;
    game::CombatRecord rec;
    rec.turn = s.turn;
    rec.participants = {game::EmpireId{0u}, game::EmpireId{1u}};
    rec.pieces.resize(4);
    rec.pieces[0].owner = game::EmpireId{0u};
    rec.pieces[1].owner = game::EmpireId{1u};
    rec.pieces[2].owner = game::EmpireId{1u};
    rec.pieces[3].owner = game::EmpireId{1u};
    rec.pieces[3].kind = game::CombatPiece::Kind::Seeker;
    auto destroyed = [&](uint32_t piece) {
        game::CombatEvent e;
        e.kind = game::CombatEvent::Kind::Destroyed;
        e.piece = piece;
        rec.events.push_back(e);
    };
    destroyed(1);
    destroyed(2);
    destroyed(3);   // a seeker is no ship
    destroyed(0);   // our own loss
    s.combats.push_back(rec);
    ev.tracker.observe(s, ev.me);
    ev.tracker.observe(s, ev.me);   // the same battle again
    CHECK(ev.tracker.enemyShipsDestroyed() == 2);
    CHECK(ev("{ enemy_ships_destroyed = 2 }"));
    CHECK_FALSE(ev("{ enemy_ships_destroyed = 3 }"));
    // Not our battle: not counted.
    rec.participants = {game::EmpireId{1u}};
    rec.turn += 1;
    s.combats.push_back(rec);
    ev.tracker.observe(s, ev.me);
    CHECK(ev.tracker.enemyShipsDestroyed() == 2);
    CHECK(describe(condition("{ not = { colonies = 1 } }")) == "not = {colonies = 1}");
}

// ---- The library ---------------------------------------------------------------------------------

TEST_CASE("learn: the fixture content loads and validates") {
    const DirectorySource source(kFixtures);
    Library lib = loadLibrary(source);
    validate(lib);
    CHECK_MESSAGE(lib.problems.empty(), problemsText(lib.problems));
    REQUIRE(lib.manual.size() == 2);
    CHECK(lib.manual[0].slug == "basics");
    CHECK(lib.manual[1].slug == "colonies");
    CHECK(lib.manual[0].sections.size() == 4);
    CHECK(lib.tutorials.size() == 1);
    CHECK(lib.training.size() == 1);
    CHECK(lib.lesson(LessonKind::Tutorial, "first-steps"));
    CHECK(lib.lesson(LessonKind::Tutorial, "first-steps")->origin == kFixtures.string());
    CHECK_FALSE(lib.next(LessonKind::Tutorial, "first-steps"));
    CHECK(lib.pageForWindow("planets") == lib.page("colonies"));
    CHECK(lib.pageForWindow("main") == lib.page("basics"));
    CHECK_FALSE(lib.pageForWindow("research"));

    const auto hits = lib.search("colony ship");
    REQUIRE(hits.size() == 1);
    CHECK(hits[0].page->slug == "colonies");
    CHECK(hits[0].anchor == "founding-colonies");
    CHECK(hits[0].excerpt.find("colony ship") != std::string::npos);
    CHECK(lib.search("status bar").front().anchor == "the-status-bar");
    CHECK(lib.search("").empty());
}

TEST_CASE("learn: validation finds broken links, ids and slugs") {
    MemorySource source("mem/");
    source.add("manual/01-a.md", "---\nwindows: researchh\n---\n# A\n\n## Part\n\n[b](b) [c](a#nowhere) [w](window:tactical-combat) [h](help:nothing) [x](ftp:x) [ok](#part)\n");
    source.add("manual/02-a.md", "# Same slug\n");
    source.add("tutorials/01-t.toml", "title = \"T\"\n[[step]]\ntitle = \"s\"\ntext = \"[w](window:nowhere)\"\nmanual = \"a#gone\"\n");
    source.add("stray/file.txt", "?");
    source.add("README.md", "notes for writers");
    Library lib = loadLibrary(source);
    validate(lib);
    const auto& p = lib.problems;
    CHECK(hasProblem(p, 2, "unknown window id 'researchh'"));
    CHECK(hasProblem(p, 8, "no manual page 'b'"));
    CHECK(hasProblem(p, 8, "has no section 'nowhere'"));
    CHECK(hasProblem(p, 8, "cannot be opened from a link"));
    CHECK(hasProblem(p, 8, "unknown Help tab 'nothing'"));
    CHECK(hasProblem(p, 8, "'ftp:x' is not a link"));
    CHECK(hasProblem(p, 0, "already has the slug 'a'"));
    CHECK(hasProblem(p, 4, "unknown window id 'nowhere'"));
    CHECK(hasProblem(p, 2, "the page 'a' has no section 'gone'"));
    CHECK(hasProblem(p, 0, "not a manual page"));
    CHECK(std::none_of(p.begin(), p.end(), [](const Diagnostic& d) { return d.file == "mem/README.md"; }));
    CHECK(p.size() == 10);
}

TEST_CASE("learn: a layered source lets the first layer win") {
    auto disk = std::make_unique<MemorySource>("disk/");
    disk->add("manual/01-a.md", "# From disk\n");
    auto builtIn = std::make_unique<MemorySource>("built/");
    builtIn->add("manual/01-a.md", "# Built in\n");
    builtIn->add("manual/02-b.md", "# Only built in\n");
    std::vector<std::unique_ptr<Source>> layers;
    layers.push_back(std::move(disk));
    layers.push_back(std::move(builtIn));
    const LayeredSource source(std::move(layers));
    CHECK(source.list() == std::vector<std::string>{"manual/01-a.md", "manual/02-b.md"});
    CHECK(source.read("manual/01-a.md") == "# From disk\n");
    CHECK(source.describe("manual/02-b.md") == "built/manual/02-b.md");
    CHECK(source.origin("manual/02-b.md") == "built in");
}

// ---- The built-in content ------------------------------------------------------------------------

TEST_CASE("learn: every built-in lesson, training game and manual page is valid") {
    // assets/learn as it is built into the executable: every condition key,
    // command name, window id and UI tag known, every link resolving. With no
    // content yet this passes trivially.
    const DirectorySource source(std::filesystem::path(OPENSE4_ASSETS_DIR) / "learn");
    Library lib = loadLibrary(source);
    validate(lib);
    CHECK_MESSAGE(lib.problems.empty(), problemsText(lib.problems));
    MESSAGE(std::format("assets/learn: {} manual pages, {} tutorials, {} training games", lib.manual.size(), lib.tutorials.size(),
                        lib.training.size()));
}
