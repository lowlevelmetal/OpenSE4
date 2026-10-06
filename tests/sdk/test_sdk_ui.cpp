// The interface tier of the modding SDK (docs/sdk/interface.md, sdk/ui.hpp):
// the ui/*.toml declarations read and checked, the values they show (the
// player's view, mod data, abilities and Python), the budgets and failures of
// computed values, how a mod order's arguments are asked for, the text of
// text/<lang>.toml with its fallbacks, opense4-sdk check, the mod identity
// without ui/ and text/, and a server's setup file that starts a scenario.

#include "rules_fixture.hpp"

#include "mod_fixture.hpp"

#include "game/commands.hpp"
#include "mods/data_set.hpp"
#include "sdk/players.hpp"
#include "sdk/rules.hpp"
#include "sdk/ui.hpp"
#include "sdk/worker.hpp"
#include "server/setup_file.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <mutex>
#include <string>

using namespace opense4;
using namespace opense4::game;
using namespace opense4::sdktest;
using script::Value;
using script::ValueList;
using script::ValueMap;

namespace {

constexpr const char* kUiMod = "test.ui-fixture";

const mods::Package& uiFixtureMod() {
    static const mods::Package p = [] {
        auto opened = mods::openPackage(fixturePath() / "mods" / "ui-fixture");
        REQUIRE_MESSAGE(opened.has_value(), (opened ? std::string{} : opened.error()));
        return std::move(*opened);
    }();
    return p;
}

// The engine's test rules with the interface fixture in their mod set.
const Rules& uiRules() {
    static const Rules r = [] {
        ruleset::Ruleset rs = test::buildEngineRuleset();
        rs.mods.push_back(uiFixtureMod().record());
        rs.reindex();
        return Rules(rs);
    }();
    return r;
}

GameState uiGame(uint64_t seed) {
    GameSetup setup = playersSetup(seed, false, std::vector<Controller>(3), 10);
    setup.empires[0].kind = PlayerKind::Human;
    auto created = createGame(uiRules(), setup);
    REQUIRE_MESSAGE(created.has_value(), (created ? std::string{} : created.error()));
    test::addHomeShips(*created, uiRules());
    return std::move(*created);
}

// The SDK installed with the interface fixture among the mods its sessions find.
struct InstalledUi {
    InstalledUi() {
        sdk::PlayerSetup setup;
        setup.mods.push_back(uiFixtureMod());
        sdk::installPlayers(std::move(setup));
    }
    ~InstalledUi() { sdk::uninstallPlayers(); }
    InstalledUi(const InstalledUi&) = delete;
    InstalledUi& operator=(const InstalledUi&) = delete;
};

const Vehicle* ownVehicle(const GameState& s, EmpireId e) {
    for (const Vehicle& v : s.vehicles)
        if (v.owner == e && v.count > 0) return &v;
    return nullptr;
}

const Colony* ownColony(const GameState& s, EmpireId e) {
    for (const auto& c : s.colonies)
        if (c && c->owner == e) return &*c;
    return nullptr;
}

bool has(const std::vector<std::string>& errors, std::string_view part) {
    return std::any_of(errors.begin(), errors.end(), [&](const std::string& e) { return e.find(part) != std::string::npos; });
}

std::vector<std::string> uiErrors(std::string_view text, const mods::RulesDecl* decl = nullptr) {
    auto r = sdk::parseUiFile(text, "ui/test.toml", "test.mod", decl);
    return r ? std::vector<std::string>{} : r.error();
}

} // namespace

TEST_CASE("sdk ui: the fixture's interface files read") {
    const std::span<const mods::Package> packages(&uiFixtureMod(), 1);
    const sdk::UiExtensions ext = sdk::loadUiExtensions(packages);
    for (const auto& [mod, problem] : ext.problems) MESSAGE(problem);
    CHECK(ext.problems.empty());
    REQUIRE(ext.panels.size() == 3);
    CHECK(ext.panels[0].name == "markings");
    CHECK(ext.panels[0].report == sdk::UiReport::Ship);
    CHECK(ext.panels[0].whose == sdk::UiWhose::Mine);
    CHECK(ext.panels[0].key == "Ctrl+Shift+P");
    REQUIRE(ext.panels[0].rows.size() == 3);
    CHECK(ext.panels[0].rows[0].source.kind == sdk::UiValueSource::Kind::ModData);
    CHECK(ext.panels[0].rows[0].source.mod == kUiMod);
    CHECK(ext.panels[0].rows[1].source.format == "{} units");
    CHECK(ext.panels[0].rows[2].source.kind == sdk::UiValueSource::Kind::Computed);
    REQUIRE(ext.panels[0].buttons.size() == 1);
    CHECK(ext.panels[0].buttons[0].order == "mark");
    CHECK(ext.panelsFor(sdk::UiReport::Colony).size() == 1);
    CHECK(ext.panelsFor(sdk::UiReport::System).size() == 1);
    CHECK(ext.columnsFor(sdk::UiList::Ships).size() == 1);
    CHECK(ext.columnsFor(sdk::UiList::Planets).size() == 1);
    CHECK(ext.columnsFor(sdk::UiList::Colonies).size() == 1);
    CHECK(ext.columnsFor(sdk::UiList::Designs).size() == 1);
    REQUIRE(ext.pages.size() == 1);
    CHECK(ext.pages[0].columns.size() == 2);
    const sdk::UiOrderStyle* mark = ext.order(kUiMod, "mark");
    REQUIRE(mark != nullptr);
    CHECK(mark->icon == "Pictures/UiFixture/Mark.png");
    CHECK(mark->key == "Ctrl+Shift+M");
    REQUIRE(mark->arg("colour") != nullptr);
    CHECK(mark->arg("colour")->choices.size() == 3);
    CHECK(ext.scriptMods == std::vector<std::string>{kUiMod});
    // A mod without ui/ adds nothing.
    const std::span<const mods::Package> rulesOnly(&rulesFixtureMod(), 1);
    CHECK(sdk::loadUiExtensions(rulesOnly).empty());
}

TEST_CASE("sdk ui: mistakes in a ui file are errors that name its line") {
    CHECK(has(uiErrors("[[panel]\n"), "ui/test.toml:1:"));
    CHECK(has(uiErrors("[[panel]]\nname = \"a\"\nreport = \"ship\"\ncolour = 1\n[[panel.row]]\nlabel = \"x\"\nfield = \"y\"\n"),
              "ui/test.toml:4: unknown key 'colour' in [[panel]]"));
    CHECK(has(uiErrors("[[panel]]\nname = \"a\"\nreport = \"ships\"\n[[panel.row]]\nfield = \"y\"\n"), "'report' is one of"));
    CHECK(has(uiErrors("[[panel]]\nname = \"a\"\nreport = \"ship\"\n[[panel.row]]\nfield = \"y\"\nvalue = \"z\"\n"), "takes only one of"));
    CHECK(has(uiErrors("[[panel]]\nname = \"a\"\nreport = \"ship\"\n[[panel.row]]\nlabel = \"y\"\n"), "needs one of field, mod_data, ability or value"));
    CHECK(has(uiErrors("[[panel]]\nname = \"a\"\nreport = \"ship\"\n"), "needs rows"));
    CHECK(has(uiErrors("[[panel]]\nname = \"A b\"\nreport = \"ship\"\n[[panel.row]]\nfield = \"y\"\n"), "may hold only lowercase letters"));
    CHECK(has(uiErrors("[[panel]]\nname = \"a\"\nreport = \"ship\"\nkey = \"Ctrl+\"\n[[panel.row]]\nfield = \"y\"\n"), "is not a key"));
    CHECK(has(uiErrors("[[column]]\nname = \"a\"\nlist = \"ships\"\nfield = \"y\"\nformat = \"kT\"\n"), "should hold {}"));
    CHECK(has(uiErrors("[[column]]\nname = \"a\"\nlist = \"ships\"\nfield = \"y\"\nwidth = 5000\n"), "'width' should be"));
    CHECK(has(uiErrors("[[column]]\nname = \"a\"\nlist = \"fleets\"\nfield = \"y\"\n"), "'list' is one of"));
    CHECK(has(uiErrors("[[column]]\nname = \"a\"\nlist = \"ships\"\nfield = \"y\"\n[[column]]\nname = \"a\"\nlist = \"ships\"\nfield = \"z\"\n"),
              "another column named 'a'"));
    CHECK(has(uiErrors("[[empire_page]]\nname = \"a\"\n"), "needs columns"));
    CHECK(has(uiErrors("[[toolbar]]\nname = \"a\"\n"), "unknown table 'toolbar'"));
    CHECK(has(uiErrors("[[column]]\nname = \"a\"\nlist = \"ships\"\nfield = \"y\"\nmod = \"x.y\"\n"), "'mod' goes with mod_data only"));
    // Orders: those the mod declares, with their arguments.
    const mods::RulesDecl& decl = uiFixtureMod().manifest.rules;
    auto ordersOf = [&](std::string_view text) {
        auto r = sdk::parseUiFile(text, "ui/test.toml", kUiMod, &decl);
        return r ? std::vector<std::string>{} : r.error();
    };
    CHECK(has(ordersOf("[[order]]\nname = \"fly\"\n"), "declares no order 'fly'"));
    CHECK(has(ordersOf("[[order]]\nname = \"mark\"\n[order.args.size]\nlabel = \"x\"\n"), "declares no argument 'size'"));
    CHECK(has(ordersOf("[[order]]\nname = \"beacon\"\n[order.args.count]\nchoices = [\"a\"]\n"), "should be whole numbers"));
    CHECK(has(ordersOf("[[order]]\nname = \"beacon\"\n[order.args.count]\nchoices = [9]\n"), "outside the argument's range"));
    CHECK(has(ordersOf("[[order]]\nname = \"mark\"\nicon = \"../x.png\"\n"), "under the mod's assets/"));
    CHECK(has(ordersOf("[[order]]\nname = \"mark\"\nicon = \"x.gif\"\n"), ".png or .bmp"));
    CHECK(has(ordersOf("[[panel]]\nname = \"p\"\nreport = \"ship\"\n[[panel.button]]\norder = \"fly\"\n"), "declares no order 'fly'"));
    CHECK(ordersOf("[[button]]\nname = \"b\"\nreport = \"ship\"\norder = \"mark\"\n").empty());
}

TEST_CASE("sdk ui: a missing picture and another mod's missing order are the mod's problems") {
    test::ModDir m("ui_problems", "test.ui-problems", "1.0.0", "[[rules.orders]]\nname = \"ping\"\napplies_to = \"self\"\n");
    m.file("scripts/pinger.py", "from opense4 import rules\n\n@rules.order(\"ping\")\ndef ping(game, order, fx):\n    pass\n");
    m.file("ui/a.toml", "[[order]]\nname = \"ping\"\nicon = \"Pictures/Nowhere/Ping.png\"\n\n[[panel]]\nname = \"p\"\nreport = \"ship\"\n"
                        "[[panel.button]]\norder = \"other.mod:launch\"\n[[panel.row]]\nlabel = \"x\"\nfield = \"name\"\n");
    m.file("ui/b.toml", "[[panel]\n");
    const mods::Package p = m.open();
    const sdk::UiExtensions ext = sdk::loadUiExtensions(std::span<const mods::Package>(&p, 1));
    const std::vector<std::string> problems = ext.problemsOf("test.ui-problems");
    CHECK(has(problems, "ui/a.toml:1: the icon assets/Pictures/Nowhere/Ping.png is not in the mod"));
    CHECK(has(problems, "the order other.mod:launch is of a mod that is not loaded"));
    CHECK(has(problems, "ui/b.toml:1:"));
    // What reads stays: the panel without its button, the order without its picture.
    REQUIRE(ext.panels.size() == 1);
    CHECK(ext.panels[0].buttons.empty());
    REQUIRE(ext.orders.size() == 1);
    CHECK(ext.orders[0].icon.empty());
}

TEST_CASE("sdk ui: values from the player's view, the mod's data and its Python") {
    InstalledUi installed;
    const Rules& r = uiRules();
    GameState s = uiGame(31);
    const EmpireId me{0u};
    const std::span<const mods::Package> packages(&uiFixtureMod(), 1);
    const sdk::UiExtensions ext = sdk::loadUiExtensions(packages);
    const sdk::UiPanel& markings = *ext.panelsFor(sdk::UiReport::Ship).front();
    const Vehicle* ship = ownVehicle(s, me);
    REQUIRE(ship != nullptr);
    const sdk::UiThing thing{"vehicle", static_cast<int64_t>(ship->id.value)};
    std::vector<sdk::UiValueRequest> requests;
    for (const sdk::UiRow& row : markings.rows) requests.push_back({&row.source, thing});
    {
        sdk::UiValues values(r, s, me, packages);
        const std::vector<sdk::UiShown> shown = values.get(requests);
        REQUIRE(shown.size() == 3);
        CHECK(shown[0].none);   // no mark yet
        CHECK(shown[0].text == "-");
        CHECK(shown[1].text == std::format("{} units", ship->supply));
        CHECK(shown[2].error.empty());
        CHECK(shown[2].text == s.design(ship->design).name);
        CHECK(values.batches() == 1);
        // Kept: asked again, nothing runs.
        (void)values.get(requests);
        CHECK(values.batches() == 1);
    }
    // The mod's order marks the ship; the player's view has its data (players_see_mod_data).
    const std::vector<sdk::ModOrderChoice> choices = sdk::modOrders(r, s, me, {"vehicle", thing.id}, packages);
    REQUIRE(choices.size() == 1);
    CHECK(choices[0].order.name == "mark");
    const auto args = sdk::uiOrderArguments(choices[0].order, Value(ValueMap{{"colour", Value("green")}}));
    REQUIRE(args.has_value());
    const CommandResult res = apply(r, s, me, sdk::modOrderCommand(choices[0], {"vehicle", thing.id}, *args));
    REQUIRE_MESSAGE(res.ok, res.error);
    {
        sdk::UiValues values(r, s, me, packages);
        CHECK(values.get(requests)[0].text == "green");
        // Another empire sees none of it.
        sdk::UiValues theirs(r, s, EmpireId{1u}, packages);
        CHECK(theirs.get(requests)[0].none);
    }
    // A colony's people, from Python; a value that raises is an error with its traceback.
    const Colony* colony = ownColony(s, me);
    REQUIRE(colony != nullptr);
    const sdk::UiPanel& watch = *ext.panelsFor(sdk::UiReport::Colony).front();
    const sdk::UiPanel& broken = *ext.panelsFor(sdk::UiReport::System).front();
    sdk::UiValues values(r, s, me, packages);
    const std::vector<sdk::UiShown> shown =
        values.get(std::vector<sdk::UiValueRequest>{{&watch.rows[0].source, {"colony", static_cast<int64_t>(colony->planet.value)}},
                                                     {&broken.rows[0].source, {"system", 0}}});
    CHECK(shown[0].text == std::format("{}M", colony->totalPopulation()));
    CHECK(shown[1].error.find("ValueError: the fixture's value always fails") != std::string::npos);
    CHECK(shown[1].traceback.find("fixture_values.py") != std::string::npos);
    CHECK(shown[1].text == "error");
    CHECK(values.batches() == 1);   // both in one interpreter
    // The thing's record in the player's view; nothing of what the view lacks.
    CHECK(values.record(thing).find("name")->asString() == ship->name);
    CHECK(values.record({"vehicle", 99999}).isNull());
}

TEST_CASE("sdk ui: computed values wait while a game holds the runtime, and stop at their budget") {
    InstalledUi installed;
    const Rules& r = uiRules();
    const GameState s = uiGame(37);
    const EmpireId me{0u};
    const std::span<const mods::Package> packages(&uiFixtureMod(), 1);
    const sdk::UiExtensions ext = sdk::loadUiExtensions(packages);
    const sdk::UiPanel& markings = *ext.panelsFor(sdk::UiReport::Ship).front();
    const sdk::UiValueRequest request{&markings.rows[2].source, {"vehicle", static_cast<int64_t>(ownVehicle(s, me)->id.value)}};
    {
        sdk::UiValues values(r, s, me, packages);
        {
            // A game's session has the runtime: the value waits, and is not kept.
            std::unique_lock held(sdk::interpreterSlot());
            const std::vector<sdk::UiShown> waiting = values.get(std::span(&request, 1));
            CHECK(values.pending());
            CHECK(waiting[0].text == "...");
            CHECK(values.batches() == 0);
        }
        const std::vector<sdk::UiShown> now = values.get(std::span(&request, 1));
        CHECK_FALSE(values.pending());
        CHECK(now[0].text == s.design(ownVehicle(s, me)->design).name);
    }
    // A value that never ends stops at its budget, and the batch's others still run.
    test::ModDir m("ui_budget", "test.ui-budget");
    m.file("ui/spin.toml", "[[column]]\nname = \"spin\"\nlist = \"ships\"\nvalue = \"spin\"\n\n[[column]]\nname = \"named\"\nlist = \"ships\"\nvalue = \"named\"\n");
    m.file("ui/spin.py", "from opense4 import ui\n\n@ui.value(\"spin\")\ndef spin(view, thing):\n    while True:\n        pass\n\n"
                         "@ui.value(\"named\")\ndef named(view, thing):\n    return thing.name\n");
    const mods::Package spinner = m.open();
    const sdk::UiExtensions spinExt = sdk::loadUiExtensions(std::span<const mods::Package>(&spinner, 1));
    REQUIRE(spinExt.columns.size() == 2);
    sdk::UiBudgets budgets;
    budgets.perValue = 200'000;
    sdk::UiValues values(r, s, me, std::span<const mods::Package>(&spinner, 1), budgets);
    const sdk::UiThing ship{"vehicle", static_cast<int64_t>(ownVehicle(s, me)->id.value)};
    const std::vector<sdk::UiShown> shown =
        values.get(std::vector<sdk::UiValueRequest>{{&spinExt.columns[0].source, ship}, {&spinExt.columns[1].source, ship}});
    CHECK(shown[0].error.find("Budget") != std::string::npos);
    CHECK(shown[1].text == ownVehicle(s, me)->name);
}

TEST_CASE("sdk ui: a mod order's arguments, asked for and checked") {
    const mods::RulesDecl& decl = uiFixtureMod().manifest.rules;
    const std::span<const mods::Package> packages(&uiFixtureMod(), 1);
    const sdk::UiExtensions ext = sdk::loadUiExtensions(packages);
    const std::vector<sdk::UiArgStep> mark = sdk::uiArgumentSteps(*decl.order("mark"), ext.order(kUiMod, "mark"));
    REQUIRE(mark.size() == 1);
    CHECK(mark[0].ask == sdk::UiArgStep::Ask::Choice);
    CHECK(mark[0].question == "Which colour");
    CHECK(mark[0].choices.size() == 3);
    CHECK(mark[0].defaultValue == Value("red"));
    const std::vector<sdk::UiArgStep> beacon = sdk::uiArgumentSteps(*decl.order("beacon"), ext.order(kUiMod, "beacon"));
    REQUIRE(beacon.size() == 1);
    CHECK(beacon[0].ask == sdk::UiArgStep::Ask::Number);
    CHECK(beacon[0].min == 1);
    CHECK(beacon[0].max == 5);
    const std::vector<sdk::UiArgStep> survey = sdk::uiArgumentSteps(*decl.order("survey"), nullptr);
    REQUIRE(survey.size() == 1);
    CHECK(survey[0].ask == sdk::UiArgStep::Ask::Pick);
    CHECK(survey[0].optional);
    CHECK(survey[0].question == "system");   // without a style: the argument's name

    // The answers make the arguments: defaults for those left out.
    auto beaconArgs = sdk::uiOrderArguments(*decl.order("beacon"), Value::emptyMap());
    REQUIRE(beaconArgs.has_value());
    CHECK(beaconArgs->find("count")->asInt() == 1);
    CHECK_FALSE(sdk::uiOrderArguments(*decl.order("beacon"), Value(ValueMap{{"count", Value(int64_t{9})}})).has_value());
    CHECK_FALSE(sdk::uiOrderArguments(*decl.order("beacon"), Value(ValueMap{{"count", Value("two")}})).has_value());
    CHECK(sdk::uiOrderArguments(*decl.order("beacon"), Value(ValueMap{{"bogus", Value(int64_t{1})}})).error().find("no argument 'bogus'") !=
          std::string::npos);
    auto surveyArgs = sdk::uiOrderArguments(*decl.order("survey"), Value::emptyMap());
    REQUIRE(surveyArgs.has_value());
    CHECK(surveyArgs->find("system")->isNull());   // an id may be left empty
    mods::ModOrderDecl needy;
    needy.name = "needy";
    needy.args.push_back(mods::ModArgDecl{"note", "text", {}, {}, Value(), false, 0});
    CHECK(sdk::uiOrderArguments(needy, Value::emptyMap()).error().find("'note' needs an answer") != std::string::npos);
    // Formatting the answers and values.
    CHECK(sdk::formatUiValue(Value(true)) == "Yes");
    CHECK(sdk::formatUiValue(Value(ValueList{Value(int64_t{1}), Value("b")})) == "1, b");
    CHECK(sdk::formatUiValue(Value(int64_t{40}), "{} kT") == "40 kT");
    CHECK(sdk::formatUiValue(Value(), "{} kT") == "-");
    const Value record(ValueMap{{"location", Value(ValueMap{{"x", Value(int64_t{4})}})}, {"items", Value(ValueList{Value("a"), Value("b")})}});
    CHECK(sdk::uiFieldValue(record, "location.x").asInt() == 4);
    CHECK(sdk::uiFieldValue(record, "items.1").asString() == "b");
    CHECK(sdk::uiFieldValue(record, "items.7").isNull());
    CHECK(sdk::uiFieldValue(record, "nothing.here").isNull());
}

TEST_CASE("sdk ui: text in the chosen language, then its base, English, and the mod's own words") {
    sdk::UiTexts texts;
    texts.add(uiFixtureMod());
    CHECK(texts.problems().empty());
    CHECK(texts.languages() == std::vector<std::string>{"en", "fr"});
    CHECK(texts.get(kUiMod, "order.mark.label", "declared") == "Mark the ship");
    texts.setLanguage("fr");
    CHECK(texts.get(kUiMod, "order.mark.label", "declared") == "Marquer le vaisseau");
    CHECK(texts.get(kUiMod, "panel.markings.mark", "Mark") == "Marque");
    CHECK(texts.get(kUiMod, "mod.name", "declared") == "Interface fixture");   // not in French: English
    CHECK(texts.get(kUiMod, "order.survey.label", "Survey a system") == "Survey a system");   // in neither: the mod's own words
    texts.setLanguage("fr-CA");
    CHECK(texts.language() == "fr-ca");
    CHECK(texts.get(kUiMod, "order.beacon.label", "x") == "Allumer des balises");   // the base language
    CHECK(texts.get("another.mod", "order.mark.label", "theirs") == "theirs");
    // Files that do not read are problems, named.
    test::ModDir m("ui_text", "test.ui-text");
    m.file("text/french.toml", "a = \"b\"\n");
    m.file("text/de.toml", "[order.x]\nlabel = 3\n");
    m.file("text/it.toml", "a = \n");
    sdk::UiTexts bad;
    bad.add(m.open());
    CHECK(has(bad.problems(), "text/french.toml: the file's name should be a language"));
    CHECK(has(bad.problems(), "text/de.toml:2: 'order.x.label' should be text in quotes"));
    CHECK(has(bad.problems(), "text/it.toml:1:"));
    CHECK(sdk::validLanguageTag("pt-br"));
    CHECK_FALSE(sdk::validLanguageTag("EN"));
    CHECK_FALSE(sdk::validLanguageTag("e"));
}

TEST_CASE("sdk ui: opense4-sdk check reads the interface and runs its Python") {
    const sdk::PlayerCheck fixture = sdk::checkModUi(uiFixtureMod());
    for (const std::string& e : fixture.errors) MESSAGE(e);
    CHECK(fixture.errors.empty());
    CHECK(fixture.warnings.empty());
    test::ModDir m("ui_check", "test.ui-check");
    m.file("ui/a.toml", "[[column]]\nname = \"c\"\nlist = \"ships\"\nvalue = \"forgotten\"\n");
    m.file("ui/values.py", "from opense4 import ui\n\n@ui.value(\"extra\")\ndef extra(view, thing):\n    return 1\n");
    m.file("ui/notes.txt", "not read\n");
    const sdk::PlayerCheck check = sdk::checkModUi(m.open());
    CHECK(has(check.errors, "ui/a.toml:1: the value 'forgotten' is not registered"));
    CHECK(has(check.warnings, "the value 'extra' is registered, but no panel, column or page shows it"));
    CHECK(has(check.warnings, "ui/notes.txt: ui/ holds .toml declarations and .py modules"));
    test::ModDir broken("ui_check_import", "test.ui-import");
    broken.file("ui/a.toml", "[[column]]\nname = \"c\"\nlist = \"ships\"\nvalue = \"v\"\n");
    broken.file("ui/values.py", "raise RuntimeError(\"no import\")\n");
    CHECK(has(sdk::checkModUi(broken.open()).errors, "the ui/ scripts do not import: RuntimeError: no import"));
    test::ModDir none("ui_check_nopy", "test.ui-nopy");
    none.file("ui/a.toml", "[[column]]\nname = \"c\"\nlist = \"ships\"\nvalue = \"v\"\n");
    CHECK(has(sdk::checkModUi(none.open()).errors, "the mod has no ui/*.py to register it"));
}

TEST_CASE("sdk ui: a mod's identity leaves its interface and text out") {
    test::ModDir m("ui_identity", "test.ui-identity");
    m.file("data/names.toml", "[system_names]\nadd = [\"Lantern\"]\n");
    const std::string before = m.open().hash;
    m.file("ui/a.toml", "[[column]]\nname = \"c\"\nlist = \"ships\"\nfield = \"name\"\n");
    m.file("text/fr.toml", "[mod]\nname = \"Lanterne\"\n");
    const mods::Package after = m.open();
    CHECK(after.hash == before);
    CHECK((after.tiers & mods::kTierInterface) != 0);
    CHECK((after.tiers & mods::kTierText) != 0);
    m.file("data/names.toml", "[system_names]\nadd = [\"Kepler\"]\n");
    CHECK(m.open().hash != before);
}

TEST_CASE("sdk ui: a server's setup file starts from a mod's scenario") {
    test::GameFolder folder("ui_scenario");
    mods::LoadedDataSet data = mods::loadDataSet(folder.root, folder.data(), test::modSet({uiFixtureMod()}));
    REQUIRE_MESSAGE(data.ruleset.has_value(), test::allErrors(data.diagnostics));
    const Rules r(*data.ruleset);
    auto file = server::parseSetup("scenario = \"test.ui-fixture:frontier\"\n", "game.toml", r);
    REQUIRE_MESSAGE(file.has_value(), (file ? std::string{} : file.error()));
    CHECK(file->scenario.mod == kUiMod);
    CHECK(file->scenario.name == "frontier");
    CHECK(file->seed == std::optional<uint64_t>(5));
    CHECK_FALSE(file->options.simultaneous);
    CHECK(file->options.systemCount == 8);
    REQUIRE(file->empires.size() == 2);
    CHECK(file->empires[0].setup.name == "Lamplighters");
    CHECK(file->empires[1].setup.kind == PlayerKind::Computer);
    const std::vector<sdk::ModOptionChoice> choices = sdk::modOptions(r);
    REQUIRE(choices.size() == 2);
    CHECK(sdk::modOptionValue(file->options, choices[0]) == 120);
    // The file's own seed, options and empires take the scenario's places.
    auto own = server::parseSetup("scenario = \"test.ui-fixture:frontier\"\nseed = 9\n[options]\nsystems = 6\n[options.mod.\"test.ui-fixture\"]\n"
                                  "beacon_bonus = 7\n[[empire]]\nkind = \"computer\"\n",
                                  "game.toml", r);
    REQUIRE_MESSAGE(own.has_value(), (own ? std::string{} : own.error()));
    CHECK(own->seed == std::optional<uint64_t>(9));
    CHECK(own->options.systemCount == 6);
    CHECK(sdk::modOptionValue(own->options, choices[0]) == 7);
    CHECK(own->empires.size() == 1);
    CHECK(own->scenario.name == "frontier");
    auto missing = server::parseSetup("scenario = \"test.ui-fixture:nowhere\"\n", "game.toml", r);
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().find("has no scenario nowhere") != std::string::npos);
    auto notUsed = server::parseSetup("scenario = \"test.elsewhere:frontier\"\n", "game.toml", r);
    REQUIRE_FALSE(notUsed.has_value());
    CHECK(notUsed.error().find("needs the mod test.elsewhere") != std::string::npos);
    CHECK(server::parseSetup("scenario = \"frontier\"\n", "game.toml", r).error().find("'scenario' must be") != std::string::npos);
}
