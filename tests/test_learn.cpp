// The learning system's headless parts (src/learn, docs/LEARNING.md): the
// Markdown subset, the lesson and training loaders, the conditions against
// the engine fixture, the content library, and a check of every built-in
// file under assets/learn.

#include "engine_fixture.hpp"

#include "game/ai.hpp"
#include "game/query.hpp"
#include "game/research.hpp"
#include "game/score.hpp"
#include "learn/access.hpp"
#include "learn/condition.hpp"
#include "learn/ids.hpp"
#include "learn/library.hpp"
#include "learn/progress.hpp"
#include "learn/tokens.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <format>
#include <sstream>

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
    CHECK(plainText(intro.text) ==
          "A page written for the tests. Press F1 for help, read slowly, and continue on the next line of the same paragraph.");
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

    game::GameSetup g;
    game::StartExtras extras;
    applySetup(l->setup, g, extras);
    CHECK(extras.lessonShips.empty());  // no starting_ships: a lesson's game starts without ships, as any game
    CHECK(g.options.quadrantSize == 0);
    CHECK(g.options.eventFrequency == 0);
    CHECK_FALSE(g.options.simultaneous);
    CHECK(g.options.randomAiPlayers.empty());   // no ai_difficulty: the computer empires play as a quick start's
}

TEST_CASE("learn: ai_difficulty sets the level of the lesson's computer empires") {
    std::vector<Diagnostic> problems;
    const auto l = parseLesson("title = \"t\"\n[setup]\nai_difficulty = \"high\"\n[[objective]]\ntext = \"o\"\nwhen = { turn = 3 }\n", "d.toml",
                               LessonKind::Training, problems);
    REQUIRE_MESSAGE(l.has_value(), problemsText(problems));
    game::GameSetup g;
    g.seed = 7;
    g.options.systemCount = 12;
    for (int i = 0; i < 3; ++i) {
        game::EmpireSetup e;
        e.name = std::format("Empire {}", i + 1);
        e.kind = i == 0 ? game::PlayerKind::Human : game::PlayerKind::Computer;
        g.empires.push_back(e);
    }
    game::StartExtras extras;
    applySetup(l->setup, g, extras);
    CHECK(g.options.aiDifficulty == game::kDifficultyHigh);
    CHECK(g.options.randomAiPlayers == std::vector<uint8_t>{0, 1, 1});
    auto s = game::createGame(engineRules(), g);
    REQUIRE_MESSAGE(s.has_value(), (s ? std::string{} : s.error()));
    CHECK(game::ai::difficultyOf(*s, game::EmpireId{1u}) == game::kDifficultyHigh);
    CHECK(game::ai::difficultyOf(*s, game::EmpireId{2u}) == game::kDifficultyHigh);
    CHECK(game::ai::difficultyOf(*s, game::EmpireId{0u}) == game::kDifficultyMedium);   // the player's ministers
}

TEST_CASE("learn: starting_ships gives the lesson's player ships of its Quick Start designs") {
    std::vector<Diagnostic> problems;
    const auto l = parseLesson("title = \"t\"\n[setup]\nstarting_ships = [\"Attack Ship\", \"Attack Ship\", \"Colony\"]\n"
                               "[[objective]]\ntext = \"o\"\nwhen = { turn = 3 }\n",
                               "s.toml", LessonKind::Training, problems);
    REQUIRE_MESSAGE(l.has_value(), problemsText(problems));
    CHECK(l->setup.startingShips == std::vector<std::string>{"Attack Ship", "Attack Ship", "Colony"});
    game::GameSetup g;
    g.seed = 7;
    g.options.systemCount = 12;
    for (int i = 0; i < 2; ++i) {
        game::EmpireSetup e;
        e.name = std::format("Empire {}", i + 1);
        e.kind = i == 0 ? game::PlayerKind::Human : game::PlayerKind::Computer;
        g.empires.push_back(e);
    }
    game::StartExtras extras;
    extras.designMinisterRun.push_back(game::EmpireId{0u});  // as a quick start
    applySetup(l->setup, g, extras);
    REQUIRE(extras.lessonShips.size() == 1);
    auto s = game::createGame(engineRules(), g, extras);
    REQUIRE_MESSAGE(s.has_value(), (s ? std::string{} : s.error()));
    std::vector<std::string> types;
    for (const game::Vehicle& v : s->vehicles) {
        CHECK(v.owner == game::EmpireId{0u});  // the computer player gets nothing
        CHECK(v.location == game::locationOf(s->galaxy, homeworld(*s, game::EmpireId{0u}).planet));
        types.push_back(s->design(v.design).designType);
    }
    const std::string colony = std::format("Colony ({})", s->empire(game::EmpireId{0u}).race.nativeSurface == "Ice" ? "Ice" : "Rock");
    CHECK(types == std::vector<std::string>{"Attack Ship", "Attack Ship", colony});
    CHECK(s->empire(game::EmpireId{1u}).designs.empty());

    // Anything but a design type is refused.
    problems.clear();
    CHECK_FALSE(parseLesson("title = \"t\"\n[setup]\nstarting_ships = [\"Scout\"]\n[[objective]]\ntext = \"o\"\nwhen = { turn = 3 }\n",
                            "b.toml", LessonKind::Training, problems));
    CHECK(problemsText(problems).find("starting_ships") != std::string::npos);
}

TEST_CASE("learn: a lesson's recap and what it suggests next") {
    std::vector<Diagnostic> problems;
    const auto l = parseLesson("title = \"t\"\nlearned = [\"Found a colony\", \"Send {design:Attack Ship} exploring\"]\n"
                               "suggest = \"training:land-rush\"\n[[step]]\ntitle = \"s\"\ntext = \"Press **Finish**.\"\n",
                               "r.toml", LessonKind::Tutorial, problems);
    REQUIRE_MESSAGE(l.has_value(), problemsText(problems));
    CHECK(l->learned == std::vector<std::string>{"Found a colony", "Send {design:Attack Ship} exploring"});
    CHECK(l->suggest == "training:land-rush");
    const auto ref = parseLessonRef(l->suggest);
    REQUIRE(ref);
    CHECK(ref->kind == LessonKind::Training);
    CHECK(ref->slug == "land-rush");
    CHECK_FALSE(parseLessonRef("land-rush"));
    CHECK_FALSE(parseLessonRef("training:"));

    problems.clear();
    CHECK_FALSE(parseLesson("title = \"t\"\nlearned = \"all of it\"\nsuggest = \"land-rush\"\n[[step]]\ntitle = \"s\"\ntext = \"x\"\n", "r.toml",
                            LessonKind::Tutorial, problems));
    CHECK(hasProblem(problems, 2, "'learned' must be a list"));
    CHECK(hasProblem(problems, 3, "'suggest' names a lesson"));
    problems.clear();
    CHECK_FALSE(parseLesson("title = \"t\"\nlearned = [\"\", \"{design:Warship}\"]\n[[step]]\ntitle = \"s\"\ntext = \"x\"\n", "r.toml",
                            LessonKind::Tutorial, problems));
    CHECK(hasProblem(problems, 2, "each of 'learned' must be a sentence"));
    CHECK(hasProblem(problems, 2, "unknown design type 'Warship'"));

    // validate() checks that the suggestion exists; following() prefers it to the next in the list.
    MemorySource source;
    source.add("tutorials/01-a.toml", std::string_view("title = \"A\"\n[[step]]\ntitle = \"s\"\ntext = \"Press **Finish**.\"\n"));
    source.add("tutorials/02-b.toml",
               std::string_view("title = \"B\"\nsuggest = \"training:c\"\n[[step]]\ntitle = \"s\"\ntext = \"Press **Finish**.\"\n"));
    source.add("tutorials/03-d.toml",
               std::string_view("title = \"D\"\nsuggest = \"training:nowhere\"\n[[step]]\ntitle = \"s\"\ntext = \"Press **Finish**.\"\n"));
    source.add("training/01-c.toml", std::string_view("title = \"C\"\n[[objective]]\ntext = \"o\"\nwhen = { turn = 3 }\n"));
    Library lib = loadLibrary(source);
    const auto found = validate(lib);
    CHECK(found.size() == 1);
    CHECK(hasProblem(found, 0, "'suggest' names no lesson: 'training:nowhere'"));
    CHECK(lib.following(*lib.lesson(LessonKind::Tutorial, "a"))->slug == "b");
    CHECK(lib.following(*lib.lesson(LessonKind::Tutorial, "b"))->slug == "c");
    CHECK(lib.following(*lib.lesson(LessonKind::Tutorial, "d")) == nullptr);
    CHECK(lib.following(*lib.lesson(LessonKind::Training, "c")) == nullptr);
}

TEST_CASE("learn: {design:<type>} tokens show the player's design names") {
    CHECK(tokenProblems("Click {design:Attack Ship} twice.").empty());
    CHECK(tokenProblems("Braces { like these } and {Capital:x} are text.").empty());
    CHECK(tokenProblems("{design:Warship}").front().find("unknown design type 'Warship'") != std::string::npos);
    CHECK(tokenProblems("{colour:red}").front().find("unknown token") != std::string::npos);
    CHECK(tokenProblems("{design:Attack Ship").front().find("no closing brace") != std::string::npos);
    CHECK(tokensAsWords("Click {design:Attack Ship} twice.") == "Click Name twice.");

    // In a lesson's text, a bad token is an error with its line.
    std::vector<Diagnostic> problems;
    CHECK_FALSE(parseLesson("title = \"t\"\n[[step]]\ntitle = \"s\"\ntext = \"Pick **{design:Scout}**.\"\n", "tok.toml", LessonKind::Tutorial,
                            problems));
    CHECK(hasProblem(problems, 4, "unknown design type 'Scout'"));

    const game::Rules& r = engineRules();
    game::GameState s = newEngineGame(7, 2, 12, true);
    const game::EmpireId me{0u};
    // No such design: the type itself.
    CHECK(designNameOfType(s, me, "Attack Ship").empty());
    CHECK(expandTokens("Click {design:Attack Ship}.", s, me) == "Click Attack Ship.");
    const game::DesignId first = addTestDesign(s, r, me, "Lancer", "Test Frigate", {"Test Bridge", "Test Engine"});
    s.designs[first.index()].designType = "Attack Ship";
    CHECK(expandTokens("Click {design:Attack Ship} twice.", s, me) == "Click Lancer twice.");
    // The newest design wins; of one turn's designs, the last made; obsolete ones never.
    const game::DesignId second = addTestDesign(s, r, me, "Pike", "Test Frigate", {"Test Bridge", "Test Engine"});
    s.designs[second.index()].designType = "Attack Ship";
    CHECK(designNameOfType(s, me, "attack ship") == "Pike");
    s.designs[second.index()].obsolete = true;
    CHECK(designNameOfType(s, me, "Attack Ship") == "Lancer");
    CHECK(designNameOfType(s, game::EmpireId{1u}, "Attack Ship").empty());   // another empire's designs are not ours
    // "Colony": the race's own colony ship first.
    const game::DesignId ice = addTestDesign(s, r, me, "Frost", "Test Frigate", {"Test Bridge", "Test Engine"});
    const game::DesignId rock = addTestDesign(s, r, me, "Stone", "Test Frigate", {"Test Bridge", "Test Engine"});
    s.designs[ice.index()].designType = "Colony (Ice)";
    s.designs[rock.index()].designType = "Colony (Rock)";
    s.empires[0].race.nativeSurface = "Ice";
    CHECK(designNameOfType(s, me, "Colony") == "Frost");
    s.empires[0].race.nativeSurface = "Rock";
    CHECK(designNameOfType(s, me, "Colony") == "Stone");
    // Every span of every block.
    Document doc = parseMarkdown("Click **{design:Attack Ship}**.\n\n- then {design:Colony}\n", "t", false);
    const std::vector<Block> out = expandTokens(doc.blocks, s, me);
    CHECK(plainText(out).find("Click Lancer.") != std::string::npos);
    CHECK(plainText(out).find("then Stone") != std::string::npos);
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

    // "At least 0" always holds: it is an error, except for the first turn.
    problems.clear();
    CHECK_FALSE(parseLesson("title = \"x\"\n[[objective]]\ntext = \"o\"\nwhen = { colonies = 0 }\n", "z.toml", LessonKind::Training, problems));
    CHECK(hasProblem(problems, 4, "'colonies = 0' always holds"));
    problems.clear();
    CHECK(parseLesson("title = \"x\"\n[[objective]]\ntext = \"o\"\nwhen = { turn = 0 }\n", "z.toml", LessonKind::Training, problems));

    problems.clear();
    CHECK_FALSE(parseLesson("title = \"x\"\n[[step]]\ntitle = \"s\"\ntext = \"t\"\n", "t.toml", LessonKind::Training, problems).has_value());
    CHECK(hasProblem(problems, 2, "unknown key 'step'"));
    CHECK(hasProblem(problems, 0, "at least one [[objective]]"));
}

TEST_CASE("learn: the new condition keys check their values") {
    std::vector<Diagnostic> problems;
    const char* text = R"(title = "Keys"
[[objective]]
text = "o"
when = { all = [{ tab = "log:nothing" }, { option = "loud" }, { treaty = "friendship" }, { design_hull_chosen = 1 }, { simulator_owners = 0 }] }
)";
    CHECK_FALSE(parseLesson(text, "k.toml", LessonKind::Training, problems));
    CHECK(hasProblem(problems, 4, "unknown window tab 'log:nothing'"));
    CHECK(hasProblem(problems, 4, "unknown option 'loud'"));
    CHECK(hasProblem(problems, 4, "unknown treaty kind 'friendship'"));
    CHECK(hasProblem(problems, 4, "'design_hull_chosen' takes true or false"));
    CHECK(hasProblem(problems, 4, "'simulator_owners = 0' always holds"));
    problems.clear();
    CHECK(parseLesson("title = \"k\"\n[[objective]]\ntext = \"o\"\nwhen = { tab = \"planets:colonizable\", option = \"research-evenly\", "
                      "treaty = \"non-aggression\", design_hull_chosen = true, design_components = 2, planets_captured = 1 }\n",
                      "ok.toml", LessonKind::Training, problems));
    CHECK_MESSAGE(problems.empty(), problemsText(problems));
}

TEST_CASE("learn: design_type qualifies selected, order and command") {
    std::vector<Diagnostic> problems;
    // Beside a key it qualifies, in one table.
    Condition c = condition("{ order = \"explore\", design_type = \"Attack Ship\" }");
    CHECK(c.op == Condition::Op::Fact);
    CHECK(c.fact == Fact::Order);
    CHECK(c.designType == "Attack Ship");
    CHECK(describe(c) == "order = \"explore\", design_type = \"Attack Ship\"");
    c = condition("{ selected = \"ship\", design_type = \"Colony\" }");
    CHECK(c.fact == Fact::Selected);
    CHECK(c.designType == "Colony");
    // With more keys every qualifying key takes it; the others do not.
    c = condition("{ command = \"QueueAdd\", window = \"set-queue\", design_type = \"Defense Base\" }");
    REQUIRE(c.op == Condition::Op::All);
    REQUIRE(c.children.size() == 2);
    for (const Condition& x : c.children) CHECK(x.designType == (x.fact == Fact::Command ? "Defense Base" : ""));

    const char* text = R"(title = "Types"
[[objective]]
text = "o"
when = { all = [{ design_type = "Attack Ship" }, { order = "explore", design_type = "Scout" }, { command = "SetResearch", design_type = "Attack Ship" }, { selected = "planet", design_type = "Attack Ship" }, { design_type_chosen = "Warship" }] }
)";
    CHECK_FALSE(parseLesson(text, "types.toml", LessonKind::Training, problems));
    CHECK(hasProblem(problems, 4, "'design_type' qualifies a 'selected', 'order' or 'command' key"));
    CHECK(hasProblem(problems, 4, "'design_type' takes a design type"));
    CHECK(hasProblem(problems, 4, "cannot qualify the command 'SetResearch'"));
    CHECK(hasProblem(problems, 4, "qualifies a selected vehicle"));
    CHECK(hasProblem(problems, 4, "unknown design type 'Warship'"));

    CHECK(isDesignTypeName("Attack Ship"));
    CHECK(isDesignTypeName("attack ship"));
    CHECK(isDesignTypeName("Colony"));
    CHECK_FALSE(isDesignTypeName("Scout"));
    CHECK(designTypeMatches("Colony (Rock)", "Colony"));
    CHECK(designTypeMatches("Attack Ship", "attack ship"));
    CHECK_FALSE(designTypeMatches("Colony (Rock)", "Colony (Ice)"));
    CHECK_FALSE(designTypeMatches("Colony", "Colony (Ice)"));
    CHECK_FALSE(designTypeMatches("Attack Ship", "Colony"));
}

TEST_CASE("learn: a step's allow list and keys") {
    std::vector<Diagnostic> problems;
    const char* text = R"(title = "Lock"
[[step]]
title = "One"
text = "t"
highlight = ["command:research"]
allow = ["button:end-turn", "window:research", "research:close"]
keys = ["F12", "Ctrl+L", "Alt+1", "Shift+Ctrl+Escape"]
done = { window = "research" }

[[step]]
title = "Two"
text = "t"
allow = ["panel:nowhere", "nowhere:close", 3]
keys = ["Hyper+Q", "F13", "Ctrl+", "q"]
)";
    CHECK_FALSE(parseLesson(text, "lock.toml", LessonKind::Tutorial, problems).has_value());
    CHECK(hasProblem(problems, 13, "unknown UI tag 'panel:nowhere'"));
    CHECK(hasProblem(problems, 13, "unknown UI tag 'nowhere:close'"));
    CHECK(hasProblem(problems, 13, "'allow' takes strings"));
    CHECK(hasProblem(problems, 14, "unknown key 'Hyper+Q'"));
    CHECK(hasProblem(problems, 14, "unknown key 'F13'"));
    CHECK(hasProblem(problems, 14, "unknown key 'Ctrl+'"));
    CHECK(hasProblem(problems, 14, "unknown key 'q'"));

    problems.clear();
    const std::string good(text, std::string_view(text).find("\n[[step]]\ntitle = \"Two\""));
    const auto l = parseLesson(good, "lock.toml", LessonKind::Tutorial, problems);
    REQUIRE_MESSAGE(l.has_value(), problemsText(problems));
    CHECK(l->steps[0].allow == std::vector<std::string>{"button:end-turn", "window:research", "research:close"});
    CHECK(l->steps[0].keys == std::vector<std::string>{"F12", "Ctrl+L", "Alt+1", "Shift+Ctrl+Escape"});
}

TEST_CASE("learn: key chords and Close tags") {
    for (const char* k : {"F1", "F12", "A", "Z", "0", "9", "Escape", "Enter", "PageDown", "LeftArrow", "Ctrl+L", "Alt+1", "Ctrl+Shift+F5",
                          "Shift+Alt+Comma"})
        CHECK_MESSAGE(isKeyChord(k), k);
    for (const char* k : {"", "F0", "F13", "a", "Esc", "Ctrl+", "Ctrl", "Meta+A", "AB", "Ctrl+ L"}) CHECK_FALSE_MESSAGE(isKeyChord(k), k);
    CHECK(isUiTag("research:close"));
    CHECK(isUiTag("create-design:close"));
    CHECK_FALSE(isUiTag("nowhere:close"));
    CHECK_FALSE(isUiTag("panel:close"));
    CHECK(isUiTag("create-design:suggest"));
    CHECK(isUiTag("lesson:free-play"));
    CHECK(isUiTag("lesson:leave"));
    // The tactical orders, by the id a lesson names them with.
    CHECK(isBattleOrderKind("move"));
    CHECK(isBattleOrderKind("fire"));
    CHECK(isBattleOrderKind("end-turn"));
    CHECK_FALSE(isBattleOrderKind("dance"));
    CHECK(battleOrderId(game::combat::TacticalOrder::Kind::Move) == "move");
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
    // The selection a game starts with is not one the player made.
    ev.client.selected = {"planet", "colony"};
    CHECK_FALSE(ev("{ selected = \"colony\" }"));
    ev.client.selections = 1;
    CHECK(ev("{ selected = \"colony\" }"));
    CHECK(ev("{ all = [{ window = \"designs\" }, { selected = \"planet\" }] }"));
    CHECK(ev("{ any = [{ window = \"log\" }, { selected = \"planet\" }] }"));
    CHECK_FALSE(ev("{ not = { selected = \"planet\" } }"));
    // Several keys in one table must all hold.
    CHECK(ev("{ window = \"designs\", selected = \"planet\" }"));
    CHECK_FALSE(ev("{ window = \"log\", selected = \"planet\" }"));
}

TEST_CASE("learn conditions: a selection counts when it was made since the step began") {
    Eval ev;
    ev.client.selected = {"ship"};
    ev.client.selections = 4;
    CHECK(ev("{ selected = \"ship\" }"));
    // A new step: the ship selected before it does not count until it is selected again.
    ev.mark = markNow(ev.rules, ev.state, ev.me, ev.tracker, ev.client.selections);
    CHECK_FALSE(ev("{ selected = \"ship\" }"));
    ev.client.selections = 5;
    CHECK(ev("{ selected = \"ship\" }"));
}

TEST_CASE("learn conditions: window tabs and work in progress") {
    Eval ev;
    CHECK_FALSE(ev("{ tab = \"log:combat\" }"));
    ev.client.tabs = {"log:combat"};
    CHECK(ev("{ tab = \"log:combat\" }"));
    CHECK_FALSE(ev("{ tab = \"log:all\" }"));

    // The designer, only while it is open.
    CHECK_FALSE(ev("{ design_components = 1 }"));
    CHECK_FALSE(ev("{ design_hull_chosen = false }"));
    ev.client.designComponents = 3;
    CHECK(ev("{ design_components = 3 }"));
    CHECK_FALSE(ev("{ design_components = 4 }"));
    CHECK(ev("{ design_hull_chosen = false }"));
    CHECK_FALSE(ev("{ design_hull_chosen = true }"));
    ev.client.designHullChosen = true;
    CHECK(ev("{ design_hull_chosen = true }"));

    // The simulator's sides.
    CHECK_FALSE(ev("{ simulator_owners = 2 }"));
    ev.client.simulatorOwners = 2;
    ev.client.simulatorItems = 5;
    CHECK(ev("{ simulator_owners = 2, simulator_items = 5 }"));
    CHECK(describe(condition("{ design_hull_chosen = true }")) == "design_hull_chosen = true");
}

TEST_CASE("learn conditions: the empire's options") {
    Eval ev;
    CHECK(ev("{ option = \"research-evenly\" }"));   // on for a new empire
    ev.empire().researchEvenly = false;
    CHECK_FALSE(ev("{ option = \"research-evenly\" }"));
    CHECK(ev("{ not = { option = \"research-evenly\" } }"));
    CHECK_FALSE(ev("{ option = \"planet-names\" }"));
    ev.empire().interfaceOptions.planetNames = true;
    CHECK(ev("{ option = \"planet-names\" }"));
    CHECK(ev("{ option = \"auto-claim-colonized\" }"));
    ev.empire().repeatIntel = true;
    CHECK(ev("{ option = \"intel-repeat\" }"));
    for (std::string_view name : optionNames()) CHECK(optionValue(ev.empire(), name).has_value());
}

TEST_CASE("learn conditions: a treaty of a kind with anyone") {
    Eval ev;
    game::Relation& rel = ev.empire().relation(game::EmpireId{1u});
    rel.contact = true;
    rel.treaty = game::Treaty::War;
    CHECK(ev("{ treaty = \"war\" }"));
    CHECK_FALSE(ev("{ treaty = \"trade-alliance\" }"));
    rel.treaty = game::Treaty::TradeAlliance;
    CHECK(ev("{ treaty = \"trade-alliance\" }"));
    CHECK_FALSE(ev("{ treaty = \"war\" }"));
    rel.contact = false;   // only empires we have met
    CHECK_FALSE(ev("{ treaty = \"trade-alliance\" }"));
    rel.contact = true;
    ev.state.empires[1].alive = false;
    CHECK_FALSE(ev("{ treaty = \"trade-alliance\" }"));
}

TEST_CASE("learn conditions: planets captured from hostile empires") {
    Eval ev;
    game::GameState& s = ev.state;
    const game::EmpireId them{1u};
    ev.tracker.observe(s, ev.me);   // the lesson's start: who owns what
    ev.mark = markNow(ev.rules, s, ev.me, ev.tracker);
    game::Colony* theirs = nullptr;
    for (auto& c : s.colonies)
        if (c && c->owner == them) theirs = &*c;
    REQUIRE(theirs);
    ev.empire().relation(them).contact = true;
    s.empires[1].relation(ev.me).contact = true;
    ev.empire().relation(them).treaty = game::Treaty::War;
    s.empires[1].relation(ev.me).treaty = game::Treaty::War;
    CHECK_FALSE(ev("{ planets_captured = 1 }"));
    theirs->owner = ev.me;
    ev.tracker.observe(s, ev.me);
    CHECK(ev("{ planets_captured = 1 }"));
    ev.tracker.observe(s, ev.me);   // seen again: still one
    CHECK_FALSE(ev("{ planets_captured = 2 }"));

    // A colony handed over by a friend is no capture.
    Eval friendly;
    game::GameState& f = friendly.state;
    friendly.tracker.observe(f, friendly.me);
    friendly.mark = markNow(friendly.rules, f, friendly.me, friendly.tracker);
    friendly.empire().relation(them).contact = true;
    friendly.empire().relation(them).treaty = game::Treaty::TradeAlliance;
    f.empires[1].relation(friendly.me).contact = true;
    f.empires[1].relation(friendly.me).treaty = game::Treaty::TradeAlliance;
    for (auto& c : f.colonies)
        if (c && c->owner == them) c->owner = friendly.me;
    friendly.tracker.observe(f, friendly.me);
    CHECK_FALSE(friendly("{ planets_captured = 1 }"));
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

TEST_CASE("learn conditions: a tactical battle begun and its orders") {
    Eval ev;
    CHECK_FALSE(ev("{ battle_begun = true }"));
    CHECK(ev("{ battle_begun = false }"));
    ev.client.battleBegun = true;
    CHECK(ev("{ battle_begun = true }"));
    CHECK_FALSE(ev("{ battle_order = \"move\" }"));
    ev.client.battleOrders = {"move", "fire"};
    CHECK(ev("{ battle_order = \"move\" }"));
    CHECK(ev("{ battle_order = \"fire\" }"));
    CHECK_FALSE(ev("{ battle_order = \"end-turn\" }"));
    // A new step counts the orders given after it began.
    ev.mark = markNow(ev.rules, ev.state, ev.me, ev.tracker, 0, ev.client.battleOrders.size());
    CHECK_FALSE(ev("{ battle_order = \"move\" }"));
    ev.client.battleOrders.push_back("move");
    CHECK(ev("{ battle_order = \"move\" }"));
    CHECK_FALSE(ev("{ battle_order = \"fire\" }"));
}

TEST_CASE("learn conditions: design_type counts only vehicles and designs of that type") {
    Eval ev;
    game::GameState& s = ev.state;
    const game::Rules& r = ev.rules;
    const game::Location home{ev.empire().homeSystem, s.galaxy.object(homeworld(s, ev.me).planet).sector};
    const game::DesignId attack = addTestDesign(s, r, ev.me, "Lancer", "Test Frigate", {"Test Bridge", "Test Engine"});
    const game::DesignId colony = addTestDesign(s, r, ev.me, "Seeder", "Test Frigate", {"Test Bridge", "Test Engine"});
    s.designs[attack.index()].designType = "Attack Ship";
    s.designs[colony.index()].designType = "Colony (Rock)";
    const game::VehicleId a1 = addTestVehicle(s, r, attack, home).id;
    const game::VehicleId a2 = addTestVehicle(s, r, attack, home).id;
    const game::VehicleId c1 = addTestVehicle(s, r, colony, home).id;

    // A selection: the selected vehicle's design decides.
    ev.client.selected = {"ship"};
    ev.client.selections = 1;
    ev.client.selectedVehicle = c1;
    CHECK(ev("{ selected = \"ship\" }"));
    CHECK_FALSE(ev("{ selected = \"ship\", design_type = \"Attack Ship\" }"));
    CHECK(ev("{ selected = \"ship\", design_type = \"Colony\" }"));
    ev.client.selectedVehicle = a1;
    CHECK(ev("{ selected = \"ship\", design_type = \"Attack Ship\" }"));

    // An order: the ship (or every ship of the fleet) it went to.
    auto explore = [](game::VehicleId v, game::FleetId f = {}) {
        game::cmd::SetOrders o;
        o.vehicle = v;
        o.fleet = f;
        o.orders.push_back(game::Order{game::OrderKind::Explore});
        return o;
    };
    ev.tracker.issued(explore(c1));
    CHECK(ev("{ order = \"explore\" }"));
    CHECK_FALSE(ev("{ order = \"explore\", design_type = \"Attack Ship\" }"));
    CHECK_FALSE(ev("{ command = \"SetOrders\", design_type = \"Attack Ship\" }"));
    ev.tracker.issued(explore(a2));
    CHECK(ev("{ order = \"explore\", design_type = \"Attack Ship\" }"));
    CHECK(ev("{ command = \"SetOrders\", design_type = \"Attack Ship\" }"));
    game::Fleet f;
    f.id = game::FleetId{static_cast<uint32_t>(s.fleets.size())};
    f.owner = ev.me;
    f.members = {a1, c1};
    s.fleets.push_back(f);
    ev.mark = markNow(r, s, ev.me, ev.tracker);
    ev.tracker.issued(explore({}, f.id));
    CHECK_FALSE(ev("{ order = \"explore\", design_type = \"Attack Ship\" }"));   // not every member is one
    s.fleets.back().members = {a1, a2};
    CHECK(ev("{ order = \"explore\", design_type = \"Attack Ship\" }"));

    // Commands that name designs and vehicles.
    ev.mark = markNow(r, s, ev.me, ev.tracker);
    game::cmd::QueueAdd add;
    add.item.kind = game::QueueItem::Kind::Facility;
    ev.tracker.issued(add);
    CHECK(ev("{ command = \"QueueAdd\" }"));
    CHECK_FALSE(ev("{ command = \"QueueAdd\", design_type = \"Colony\" }"));
    add.item.kind = game::QueueItem::Kind::Vehicle;
    add.item.design = colony;
    ev.tracker.issued(add);
    CHECK(ev("{ command = \"QueueAdd\", design_type = \"Colony\" }"));
    CHECK_FALSE(ev("{ command = \"QueueAdd\", design_type = \"Attack Ship\" }"));
    game::cmd::CreateDesign made;
    made.design.designType = "Attack Ship";
    ev.tracker.issued(made);
    CHECK(ev("{ command = \"CreateDesign\", design_type = \"Attack Ship\" }"));
    ev.tracker.issued(game::cmd::JoinFleet{f.id, c1});
    CHECK_FALSE(ev("{ command = \"JoinFleet\", design_type = \"Attack Ship\" }"));
    ev.tracker.issued(game::cmd::JoinFleet{f.id, a2});
    CHECK(ev("{ command = \"JoinFleet\", design_type = \"Attack Ship\" }"));
}

TEST_CASE("learn conditions: the design being made: its type and its name") {
    Eval ev;
    // Only while the Create Design window is open.
    ev.client.designType = "Attack Ship";
    ev.client.designNamed = true;
    CHECK_FALSE(ev("{ design_type_chosen = \"Attack Ship\" }"));
    CHECK_FALSE(ev("{ design_named = true }"));
    ev.client.designComponents = 0;
    CHECK(ev("{ design_type_chosen = \"Attack Ship\" }"));
    CHECK_FALSE(ev("{ design_type_chosen = \"Defense Base\" }"));
    CHECK(ev("{ design_named = true }"));
    ev.client.designNamed = false;
    CHECK(ev("{ design_named = false }"));
    ev.client.designType.clear();
    CHECK_FALSE(ev("{ design_type_chosen = \"Attack Ship\" }"));
    CHECK(describe(condition("{ design_type_chosen = \"Attack Ship\" }")) == "design_type_chosen = \"Attack Ship\"");
}

TEST_CASE("learn conditions: counters tell how far a count or a wait has come") {
    Eval ev;
    const int64_t explored = ev.value(Fact::SystemsExplored);
    auto count = [&](std::string_view table) { return counters(condition(table), EvalContext{ev.rules, ev.state, ev.me, ev.client, ev.tracker, ev.mark}); };
    auto list = count(std::format("{{ systems_explored = {} }}", explored + 4));
    REQUIRE(list.size() == 1);
    CHECK(list[0].fact == Fact::SystemsExplored);
    CHECK(list[0].current == explored);
    CHECK(list[0].target == explored + 4);
    CHECK(list[0].text() == std::format("Systems explored: {} of {}", explored, explored + 4));
    // Turns since the step began; the value never shows above its target.
    ev.state.turn += 5;
    list = count("{ turns_passed = 3 }");
    REQUIRE(list.size() == 1);
    CHECK(list[0].text() == "Turns: 3 of 3");
    // Every counted fact of all and any, each once; nothing under not, nothing that is not a number.
    list = count("{ any = [{ treaty = \"non-aggression\" }, { treaties = 1 }, { turns_passed = 8 }, { not = { colonies = 9 } }, "
                 "{ turns_passed = 9 }] }");
    REQUIRE(list.size() == 2);
    CHECK(list[0].label == "Treaties");
    CHECK(list[1].text() == "Turns: 5 of 8");
    CHECK(count("{ window = \"research\" }").empty());
    CHECK(count("{ all = [{ simulator_owners = 2 }, { simulator_items = 3 }] }").size() == 2);
    // Every numeric fact has a name for the progress line.
    for (const FactInfo& f : facts())
        if (f.value == FactValue::Number) CHECK_MESSAGE(!f.counter.empty(), f.key);
}

TEST_CASE("learn access: a done condition needs the tags that bring it about") {
    auto access = [](std::vector<std::string> tags, std::vector<std::string> keys = {}) {
        StepAccess a;
        a.tags = std::move(tags);
        a.keys = std::move(keys);
        return a;
    };
    auto ok = [](std::string_view table, const StepAccess& a) {
        return reachProblems(condition(table), a).empty();
    };
    // Windows: a tag that opens them; closing: the window, its Close button or Esc.
    CHECK(ok("{ window = \"research\" }", access({"command:research"})));
    CHECK_FALSE(ok("{ window = \"research\" }", access({"command:designs"})));
    CHECK(ok("{ window = \"create-design\" }", access({"designs:create"})));
    CHECK(ok("{ window = \"galaxy-map\" }", access({"panel:galaxy"})));
    CHECK(ok("{ not = { window = \"designs\" } }", access({"designs:close"})));
    CHECK(ok("{ not = { window = \"designs\" } }", access({}, {"Escape"})));
    CHECK_FALSE(ok("{ not = { window = \"designs\" } }", access({"command:designs"})));
    // Orders, commands and selections.
    CHECK(ok("{ order = \"explore\" }", access({"order:explore"})));
    CHECK_FALSE(ok("{ order = \"explore\" }", access({"order:move-to"})));
    CHECK(ok("{ order = \"move-to\" }", access({"panel:system"})));
    CHECK(ok("{ order = \"move-to-waypoint\" }", access({}, {"Ctrl+1"})));
    CHECK(ok("{ command = \"SetWaypoint\" }", access({}, {"Alt+1"})));
    CHECK_FALSE(ok("{ command = \"SetWaypoint\" }", access({"panel:system"})));
    CHECK(ok("{ command = \"QueueAdd\" }", access({"set-queue:available"})));
    CHECK_FALSE(ok("{ command = \"QueueAdd\" }", access({"set-queue:queue"})));
    CHECK(ok("{ selected = \"ship\" }", access({"cycle:ship"})));
    CHECK_FALSE(ok("{ selected = \"ship\" }", access({"command:ships"})));
    // What comes with the turns needs End Turn, as a button or a key.
    CHECK(ok("{ turns_passed = 1 }", access({"button:end-turn"})));
    CHECK(ok("{ turns_passed = 1 }", access({}, {"F12"})));
    CHECK_FALSE(ok("{ turns_passed = 1 }", access({"command:research"})));
    // Battles.
    CHECK(ok("{ battle_begun = true }", access({"tactical-combat:end-turn"})));
    CHECK(ok("{ not = { window = \"tactical-combat\" } }", access({"tactical-combat:end-turn"})));   // the battle played out
    CHECK(ok("{ battle_order = \"move\" }", access({"tactical-combat:map"})));
    CHECK_FALSE(ok("{ battle_order = \"move\" }", access({"tactical-combat:end-turn"})));
    // The design being made: its type box, its name box (or the list of names).
    CHECK(ok("{ design_type_chosen = \"Attack Ship\" }", access({"create-design:type"})));
    CHECK_FALSE(ok("{ design_type_chosen = \"Attack Ship\" }", access({"create-design:hull"})));
    CHECK(ok("{ design_named = true }", access({"create-design:suggest"})));
    CHECK_FALSE(ok("{ design_named = true }", access({"create-design:save"})));
    // The homeworld's sector and its row in the report select the colony.
    CHECK(ok("{ selected = \"colony\" }", access({"sector:home", "report:colony"})));
    // All needs every part, any one of them.
    CHECK_FALSE(ok("{ all = [{ window = \"research\" }, { turns_passed = 1 }] }", access({"command:research"})));
    CHECK(ok("{ any = [{ window = \"research\" }, { turns_passed = 1 }] }", access({"command:research"})));
    CHECK_FALSE(ok("{ any = [{ window = \"designs\" }, { turns_passed = 1 }] }", access({"command:research"})));
    // The step's highlight and allow lists both count.
    learn::Step st;
    st.highlight = {"command:research"};
    st.allow = {"button:end-turn"};
    st.keys = {"Ctrl+L"};
    const StepAccess a = stepAccess(st);
    CHECK(a.has("command:research"));
    CHECK(a.has("button:end-turn"));
    CHECK(a.hasKey("Ctrl+L"));
    CHECK(windowsOpenedBy("command:research") == std::vector<std::string_view>{"research"});
    const auto openers = openersOf("create-design");
    CHECK(std::find(openers.begin(), openers.end(), "designs:create") != openers.end());
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
    source.add("manual/01-a.md", "---\nwindows: researchh\n---\n# A\n\n## Part\n\n"
                                 "[b](b) [c](a#nowhere) [w](window:tactical-combat) [h](help:nothing) [x](ftp:x) [ok](#part)\n");
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

TEST_CASE("learn: every built-in tutorial step can be done with what the lock allows") {
    // The input lock (docs/LEARNING.md) lets the player use only a step's
    // highlighted and allowed tags and its keys. A step that waits for an
    // action must name what the action needs; a step that does not wait says
    // to press Next, and one that waits does not.
    const DirectorySource source(std::filesystem::path(OPENSE4_ASSETS_DIR) / "learn");
    const Library lib = loadLibrary(source);
    for (const Lesson& lesson : lib.tutorials) {
        for (size_t i = 0; i < lesson.steps.size(); ++i) {
            const Step& st = lesson.steps[i];
            const std::string where = std::format("{} step {} \"{}\"", lesson.slug, i + 1, st.title);
            const std::string text = plainText(st.text);
            const bool last = i + 1 == lesson.steps.size();   // its button is Finish
            const bool saysNext = text.find(last ? "Finish" : "Next") != std::string::npos;
            if (!st.done) {
                CHECK_MESSAGE(saysNext, where, ": a step without `done` must tell the player to press ", last ? "Finish" : "Next");
                continue;
            }
            CHECK_MESSAGE(text.find("Press Next") == std::string::npos, where, ": a step with `done` moves on by itself");
            CHECK_MESSAGE(text.find("press Next") == std::string::npos, where, ": a step with `done` moves on by itself");
            CHECK_MESSAGE(!st.highlight.empty(), where, ": a step that waits for an action outlines where to do it");
            for (const std::string& p : reachProblems(*st.done, stepAccess(st))) CHECK_MESSAGE(false, where, ": ", p);
        }
    }
}

// ---- Progress through a lesson ---------------------------------------------------------------------

namespace {

learn::Lesson fixtureLesson(LessonKind kind, const char* file) {
    std::vector<Diagnostic> problems;
    const auto text = DirectorySource(kFixtures).read(file);
    REQUIRE(text);
    auto l = parseLesson(*text, file, kind, problems);
    REQUIRE_MESSAGE(l.has_value(), problemsText(problems));
    return *l;
}

} // namespace

TEST_CASE("learn progress: tutorial steps wait for Next or their condition") {
    const game::Rules& r = engineRules();
    game::GameState s = newEngineGame(7, 2, 12, true);
    const game::EmpireId me{0u};
    LessonProgress p(fixtureLesson(LessonKind::Tutorial, "tutorials/01-first-steps.toml"), r, s, me);
    ClientFacts client;

    // Step 1 has no condition: Next moves on.
    CHECK(p.step() == 0);
    CHECK(p.canGoNext());
    CHECK_FALSE(p.canGoBack());
    CHECK_FALSE(p.update(r, s, me, client).stepChanged);
    CHECK(p.goNext(r, s, me));
    CHECK(p.step() == 1);

    // Step 2 waits for the Research window, then moves on by itself.
    CHECK_FALSE(p.canGoNext());
    CHECK_FALSE(p.goNext(r, s, me));
    client.openWindows = {"research"};
    CHECK(p.update(r, s, me, client).stepChanged);
    CHECK(p.step() == 2);
    CHECK(p.completed(1));

    // Back shows a done step with Next open; Next returns to where we were.
    p.goBack();
    CHECK(p.step() == 1);
    CHECK(p.canGoNext());
    CHECK(p.goNext(r, s, me));
    CHECK(p.step() == 2);

    // Step 3 counts commands from when it was first shown.
    CHECK_FALSE(p.update(r, s, me, client).stepChanged);
    p.issued(game::cmd::QueueAdd{});
    CHECK_FALSE(p.update(r, s, me, client).stepChanged);
    p.issued(game::cmd::SetResearch{});
    CHECK(p.update(r, s, me, client).stepChanged);
    CHECK(p.step() == 3);

    // The last step: a turn must end, then the lesson is done.
    CHECK_FALSE(p.update(r, s, me, client).finished);
    s.turn += 1;
    const auto ch = p.update(r, s, me, client);
    CHECK(ch.finished);
    CHECK(p.result() == LessonProgress::Result::Done);
    CHECK_FALSE(p.canGoNext());
}

TEST_CASE("learn progress: Back reads earlier steps; the active step keeps its condition, and Skip gives it up") {
    const game::Rules& r = engineRules();
    game::GameState s = newEngineGame(7, 2, 12, true);
    const game::EmpireId me{0u};
    LessonProgress p(fixtureLesson(LessonKind::Tutorial, "tutorials/01-first-steps.toml"), r, s, me);
    ClientFacts client;
    CHECK(p.goNext(r, s, me));
    CHECK(p.active() == 1);
    // Reading step 1 again: Next goes back to step 2, the active step does not move.
    p.goBack();
    CHECK(p.step() == 0);
    CHECK(p.active() == 1);
    CHECK(p.canGoNext());
    // The active step's condition counts while an earlier one is shown, and its successor is shown.
    client.openWindows = {"research"};
    CHECK(p.update(r, s, me, client).stepChanged);
    CHECK(p.step() == 2);
    CHECK(p.active() == 2);
    // Back and Next never pass the active step.
    p.goBack();
    p.goBack();
    CHECK(p.goNext(r, s, me));
    CHECK(p.goNext(r, s, me));
    CHECK(p.step() == 2);
    CHECK_FALSE(p.goNext(r, s, me));
    // Skip gives up the active step, even while an earlier one is shown.
    p.goBack();
    p.skip(r, s, me);
    CHECK(p.step() == 3);
    CHECK(p.active() == 3);
    CHECK(p.completed(2));
    p.skip(r, s, me);
    CHECK(p.result() == LessonProgress::Result::Done);
}

TEST_CASE("learn progress: jumping to a step marks the ones before done") {
    const game::Rules& r = engineRules();
    game::GameState s = newEngineGame(7, 2, 12, true);
    LessonProgress p(fixtureLesson(LessonKind::Tutorial, "tutorials/01-first-steps.toml"), r, s, game::EmpireId{0u});
    p.jumpTo(2, r, s, game::EmpireId{0u});
    CHECK(p.step() == 2);
    CHECK(p.completed(0));
    CHECK(p.completed(1));
    CHECK_FALSE(p.completed(2));
    p.jumpTo(99, r, s, game::EmpireId{0u});
    CHECK(p.step() == 3);
}

TEST_CASE("learn progress: the active step's counters") {
    const game::Rules& r = engineRules();
    game::GameState s = newEngineGame(7, 2, 12, true);
    const game::EmpireId me{0u};
    LessonProgress p(fixtureLesson(LessonKind::Tutorial, "tutorials/01-first-steps.toml"), r, s, me);
    const ClientFacts client;
    // Step 1 waits for Next, step 2 for a window: nothing to count.
    CHECK(p.counters(r, s, me, client).empty());
    CHECK(p.goNext(r, s, me));
    CHECK(p.counters(r, s, me, client).empty());
    // The last step waits for a turn; its counter counts from where it began, and only while it is shown.
    p.jumpTo(3, r, s, me);
    auto list = p.counters(r, s, me, client);
    REQUIRE(list.size() == 1);
    CHECK(list[0].text() == "Turns: 0 of 1");
    p.goBack();
    CHECK(p.counters(r, s, me, client).empty());
    CHECK(p.goNext(r, s, me));
    s.turn += 1;
    p.update(r, s, me, client);
    CHECK(p.result() == LessonProgress::Result::Done);
    CHECK(p.counters(r, s, me, client).empty());
}

TEST_CASE("learn progress: training objectives, pages, hints and the result") {
    const game::Rules& r = engineRules();
    game::GameState s = newEngineGame(7, 2, 12, true);
    const game::EmpireId me{0u};
    LessonProgress p(fixtureLesson(LessonKind::Training, "training/01-expansion.toml"), r, s, me);
    const ClientFacts client;

    // The first turn's briefing comes up; both of its pages are in the series.
    auto ch = p.update(r, s, me, client);
    CHECK(ch.pageShown);
    CHECK(p.page() == 0u);
    CHECK(p.series() == std::vector<size_t>{0, 1});
    CHECK_FALSE(p.objectiveDone(0));
    CHECK_FALSE(p.objectiveDone(1));

    // An objective holds once and stays done.
    s.empires[0].research.push_back({techArea(r, "Test Construction"), 0});
    p.update(r, s, me, client);
    CHECK(p.objectiveDone(1));
    s.empires[0].research.clear();
    p.update(r, s, me, client);
    CHECK(p.objectiveDone(1));

    // The hint comes once its condition holds, and once only.
    CHECK_FALSE(p.hint());
    s.turn = 2;
    ch = p.update(r, s, me, client);
    CHECK(ch.hintShown);
    REQUIRE(p.hint());
    p.dismissHint();
    CHECK_FALSE(p.hint());
    s.turn = 3;
    CHECK_FALSE(p.update(r, s, me, client).hintShown);

    // The deadline of "two colonies" passes: the game is lost.
    s.turn = 31;
    ch = p.update(r, s, me, client);
    CHECK(ch.finished);
    CHECK(p.objectiveFailed(0));
    CHECK(p.result() == LessonProgress::Result::Lost);
    CHECK(p.why().find("Have two colonies") != std::string::npos);
}

TEST_CASE("learn progress: the fail rule and winning") {
    const game::Rules& r = engineRules();
    const game::EmpireId me{0u};
    {
        // Every colony lost: the fail rule holds.
        game::GameState s = newEngineGame(7, 2, 12, true);
        LessonProgress p(fixtureLesson(LessonKind::Training, "training/01-expansion.toml"), r, s, me);
        for (auto& c : s.colonies)
            if (c && c->owner == me) c->owner = game::EmpireId{1u};
        CHECK(p.update(r, s, me, ClientFacts{}).finished);
        CHECK(p.result() == LessonProgress::Result::Lost);
        CHECK(p.why() == "Every colony was lost.");
    }
    {
        // Two colonies and a project before the deadline: won.
        game::GameState s = newEngineGame(7, 2, 12, true);
        LessonProgress p(fixtureLesson(LessonKind::Training, "training/01-expansion.toml"), r, s, me);
        for (auto& c : s.colonies)
            if (c && c->owner == game::EmpireId{1u}) c->owner = me;
        s.empires[0].research.push_back({techArea(r, "Test Construction"), 0});
        s.turn = 12;
        CHECK(p.update(r, s, me, ClientFacts{}).finished);
        CHECK(p.result() == LessonProgress::Result::Won);
        CHECK(p.why() == "Every objective was met by 2401.2.");
    }
}
