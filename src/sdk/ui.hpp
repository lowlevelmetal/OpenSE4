#pragma once

// The interface tier of the modding SDK (docs/sdk/interface.md,
// docs/MODDING_SDK.md §8): what a mod's ui/*.toml files add to the classic
// windows, the strings of its text/<lang>.toml files, and the values the
// additions show, read from the player's own view of the game (fog of war)
// and, for computed values, from Python functions the mod's ui/*.py files
// register with opense4.ui.
//
//     [[order]]                    # how the interface shows one of the mod's orders
//     name = "overcharge"
//     icon = "Pictures/Mod/Overcharge.png"
//     key = "Ctrl+Shift+O"
//     [order.args.power]
//     label = "Power"
//     choices = [1, 2, 3]
//
//     [[panel]]                    # a section of an object's report
//     name = "shields"
//     title = "Shields"
//     report = "ship"              # ship, fleet, planet, colony, system
//     [[panel.row]]
//     label = "Charge"
//     value = "charge"             # or field, mod_data, ability
//     [[panel.button]]
//     order = "overcharge"
//
//     [[column]]                   # a column of a list window
//     name = "charge"
//     list = "ships"               # ships, planets, colonies, designs
//     label = "Charge"
//     mod_data = "charge"
//
//     [[empire_page]]              # a page of the Empires window: a table over the empires
//     name = "relics"
//     title = "Relics"
//     [[empire_page.column]]
//     label = "Relics"
//     mod_data = "relics"
//
// Interface extensions never change the game: they read the player's view and
// give orders only as commands (the mod orders of docs/sdk/rules.md). A mod
// whose files hold only ui/ and text/ does not change the game's identity.

#include "game/rules.hpp"
#include "game/state.hpp"
#include "mods/manifest.hpp"
#include "mods/package.hpp"
#include "script/value.hpp"

#include <cstdint>
#include <expected>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace opense4::sdk {

// ---- Declarations ----------------------------------------------------------------------------------

// Where a shown value comes from: exactly one of these per row or column.
struct UiValueSource {
    enum class Kind : uint8_t {
        Field,      // a field of the thing's record in the player's view (docs/sdk/view.md): "supply", "location.x"
        ModData,    // a key of a mod's data on the thing, as the player's view holds it: "relics", "charge.level"
        Ability,    // an ability's value on the thing (a vehicle's design, a colony's facilities, a system, a design)
        Computed,   // a function of the mod's ui/*.py, registered with @ui.value(name)
    };
    Kind kind = Kind::Field;
    std::string path;     // Field, ModData: dotted keys; Ability: its name; Computed: the value's name
    std::string mod;      // ModData: whose data (default: the declaring mod); Computed: the registering mod
    std::string format;   // "{} kT": the value in place of {} (empty: the value alone)
};

struct UiRow {
    std::string name;     // optional: names its label in text/ (panel.<panel>.<row>)
    std::string label;
    UiValueSource source;
    int line = 0;
};

// A button that gives a mod order to the thing the report shows.
struct UiButton {
    std::string mod;      // the order's mod (default: the declaring mod)
    std::string order;    // its name ([[rules.orders]])
    std::string label;    // empty: the order's label
    int line = 0;
};

// The reports a panel can be added to.
enum class UiReport : uint8_t { Ship, Fleet, Planet, Colony, System };
// The list windows a column can be added to.
enum class UiList : uint8_t { Ships, Planets, Colonies, Designs };
// Whose things a panel shows on.
enum class UiWhose : uint8_t { Any, Mine, Others };

std::string_view uiReportName(UiReport r);   // "ship"
std::string_view uiListName(UiList l);       // "ships"

struct UiPanel {
    std::string mod;
    std::string name;
    std::string title;
    UiReport report = UiReport::Ship;
    UiWhose whose = UiWhose::Any;
    std::vector<UiRow> rows;
    std::vector<UiButton> buttons;
    std::string key;      // a suggested key that shows or hides it ("Ctrl+Shift+S"); empty: none
    std::string file;     // "ui/shields.toml"
    int line = 0;
};

struct UiColumn {
    std::string mod;
    std::string name;
    UiList list = UiList::Ships;
    std::string label;
    int width = 80;       // frame pixels
    UiValueSource source;
    std::string file;
    int line = 0;
};

struct UiEmpirePage {
    std::string mod;
    std::string name;
    std::string title;
    std::vector<UiColumn> columns;   // their `list` is unused
    std::string key;
    std::string file;
    int line = 0;
};

// How an argument of a mod order is asked for (beyond its declaration).
struct UiArgStyle {
    std::string name;
    std::string label;                 // the prompt's question; empty: the argument's name
    std::vector<script::Value> choices;   // int or text arguments: a list to choose from
};

// How the interface shows one of the mod's orders.
struct UiOrderStyle {
    std::string mod;
    std::string order;
    std::string icon;     // a picture under the mod's assets/ ("Pictures/Mod/Overcharge.png"); empty: none
    std::string key;      // a suggested key binding; empty: none
    std::vector<UiArgStyle> args;
    std::string file;
    int line = 0;

    const UiArgStyle* arg(std::string_view name) const;
};

// What one ui/*.toml file declares.
struct UiFile {
    std::vector<UiPanel> panels;
    std::vector<UiColumn> columns;
    std::vector<UiEmpirePage> pages;
    std::vector<UiOrderStyle> orders;
};

// Reads one ui/*.toml file of the mod `mod`; `file` names it in messages
// ("ui/shields.toml"). Every problem names the file and line. `decl` (the
// mod's rules) checks the orders it names; null: not checked.
std::expected<UiFile, std::vector<std::string>> parseUiFile(std::string_view text, std::string_view file, std::string_view mod,
                                                            const mods::RulesDecl* decl = nullptr);

// Every mod's interface extensions, in load order.
struct UiExtensions {
    std::vector<UiPanel> panels;
    std::vector<UiColumn> columns;
    std::vector<UiEmpirePage> pages;
    std::vector<UiOrderStyle> orders;
    // What did not load ("mod example.x: ui/a.toml:3: ..."), by mod.
    std::vector<std::pair<std::string, std::string>> problems;
    // The mods with ui/*.py files (their computed values), in load order.
    std::vector<std::string> scriptMods;

    bool empty() const { return panels.empty() && columns.empty() && pages.empty() && orders.empty(); }
    const UiOrderStyle* order(std::string_view mod, std::string_view name) const;
    std::vector<const UiPanel*> panelsFor(UiReport r) const;
    std::vector<const UiColumn*> columnsFor(UiList l) const;
    std::vector<std::string> problemsOf(std::string_view mod) const;
};

// Reads the ui/ folders of these packages (in their order). The orders a
// button or [[order]] names must be declared by the mod named (one of these
// packages); the icons must be in the mod's assets/.
UiExtensions loadUiExtensions(std::span<const mods::Package> packages);

// A mod order's argument as the interface asks for it: a number, yes or no,
// text, a choice from a list (declared choices, or the game's empires or the
// player's designs), or a pick on the map (systems, stellar objects,
// colonies, vehicles, fleets).
struct UiArgStep {
    enum class Ask : uint8_t { Number, YesNo, Text, Choice, Empire, Design, Pick };
    std::string name;
    std::string type;          // the declaration's: int, bool, text, empire, system, object, colony, vehicle, fleet, design
    std::string question;      // the prompt
    Ask ask = Ask::Number;
    std::optional<int64_t> min, max;
    script::Value defaultValue;   // null: none
    bool optional = false;     // an id argument may be left empty
    std::vector<script::Value> choices;
};
// The steps that ask for an order's arguments, in their declared order.
std::vector<UiArgStep> uiArgumentSteps(const mods::ModOrderDecl& order, const UiOrderStyle* style);
// The arguments map from the answers (by argument name); an answer may be
// left out for an argument with a default (or an id). The reason when an
// answer does not fit its argument.
std::expected<script::Value, std::string> uiOrderArguments(const mods::ModOrderDecl& order, const script::Value& answers);

// ---- Text (text/<lang>.toml) --------------------------------------------------------------------------

// The strings of the mods' text/ folders: a mod's text/fr.toml holds French
// strings by key ("order.overcharge.label"; TOML tables nest the keys, so
// [order.overcharge] label = "..." is the same key). A string is looked up in
// the chosen language, then its base language ("pt" for "pt-br"), then
// English ("en"), then the declaration's own text.
class UiTexts {
public:
    // Adds a package's text/*.toml files; problems are kept (problems()).
    void add(const mods::Package& p);
    void setLanguage(std::string language);
    const std::string& language() const { return language_; }
    // The languages the mods' files have, sorted ("de", "en", "fr").
    std::vector<std::string> languages() const;
    // The mod's string for `key`, or `fallback`.
    std::string get(std::string_view mod, std::string_view key, std::string_view fallback) const;
    const std::vector<std::string>& problems() const { return problems_; }

private:
    // mod -> language -> key -> text
    std::map<std::string, std::map<std::string, std::map<std::string, std::string>>> strings_;
    std::string language_ = "en";
    std::vector<std::string> problems_;
};

// Reads one text/<lang>.toml: its strings by dotted key.
std::expected<std::map<std::string, std::string>, std::vector<std::string>> parseUiTexts(std::string_view text, std::string_view file);
// Whether a file name's stem is a language tag ("en", "fr", "pt-br").
bool validLanguageTag(std::string_view tag);

// ---- Values ------------------------------------------------------------------------------------------

// A thing a value is shown for: "vehicle", "fleet", "object" (a planet or
// stellar object), "colony" (by its planet), "system", "empire", "design".
struct UiThing {
    std::string kind;
    int64_t id = -1;
    bool operator==(const UiThing&) const = default;
};

struct UiValueRequest {
    const UiValueSource* source = nullptr;
    UiThing thing;
};

// A value as the interface shows it.
struct UiShown {
    std::string text;          // the value, formatted ("-" when there is none)
    bool none = false;         // the thing has no such value
    std::string error;         // a failed computed value, in one line ("ValueError: ...")
    std::string traceback;
};

// Budgets of computed values (docs/sdk/interface.md "Budgets").
struct UiBudgets {
    int64_t perValue = 2'000'000;       // bytecodes one call of a value's function may use
    int64_t perBatch = 40'000'000;      // all the calls of one batch together
    size_t heapBytes = size_t{16} << 20;
};

// Computes values for one player's view of one state. Field, mod data and
// ability values are read in C++; computed values run in the script runtime,
// all of a batch in one interpreter (on a thread of their own, never while a
// game's session holds the runtime: then they wait for the next batch).
// Results are kept per (source, thing) until the object is dropped: make a
// new one when the game changes (the client does at each new revision).
class UiValues {
public:
    UiValues(const game::Rules& r, const game::GameState& s, game::EmpireId player, std::span<const mods::Package> packages, UiBudgets budgets = {});
    ~UiValues();
    UiValues(const UiValues&) = delete;
    UiValues& operator=(const UiValues&) = delete;

    // The values for these requests, in their order. A computed value that
    // cannot run now (the runtime is busy) comes back with `pending()` set.
    std::vector<UiShown> get(std::span<const UiValueRequest> requests);
    // A computed value waits for the runtime: ask again later.
    bool pending() const { return pending_; }
    // How many batches ran the script runtime (tests: values are cached).
    int batches() const { return batches_; }
    // The thing's record in the player's view (null: the view does not have it).
    script::Value record(const UiThing& t);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    bool pending_ = false;
    int batches_ = 0;
};

// A value as text: whole numbers as they are, true and false as Yes and No,
// None as "-", lists joined with ", ", maps as JSON; `format`'s {} replaced.
std::string formatUiValue(const script::Value& v, std::string_view format = {});
// A field of a record by dotted keys ("location.x"); null when absent.
script::Value uiFieldValue(const script::Value& record, std::string_view path);

// ---- Checking (opense4-sdk check) ------------------------------------------------------------------

struct PlayerCheck;
// The mod's ui/ and text/ files: they read, name what they may, their
// pictures are there, the ui/*.py modules import and register every
// computed value the .toml files name.
PlayerCheck checkModUi(const mods::Package& p, std::span<const mods::Package> others = {});

} // namespace opense4::sdk
