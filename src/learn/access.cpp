#include "learn/access.hpp"

#include "learn/ids.hpp"

#include <algorithm>
#include <format>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace opense4::learn {

namespace {

// Tags whose click opens a window, besides the command buttons.
constexpr std::pair<std::string_view, std::string_view> kOpens[] = {
    {"panel:galaxy", "galaxy-map"},
    {"designs:create", "create-design"},
    {"designs:simulator", "combat-simulator"},
    {"research:tech-tree", "tech-tree"},
    {"order:build-queue", "set-queue"},
    {"queues:list", "set-queue"},
    {"order:fleet-transfer", "fleet-transfer"},
    {"order:view-orders", "view-orders"},
    {"order:cargo-transfer", "cargo-transfer"},
    {"order:launch-recover", "launch-recover"},
    {"order:scrap", "scrap"},
    {"order:rename", "rename"},
    {"order:stellar-manipulation", "stellar-manipulation"},
    {"order:move-to-waypoint", "select-waypoint"},
    {"empires:intelligence", "intelligence"},
    {"empires:list", "communicate"},
    {"log:send-reply", "communicate"},
    {"combat-simulator:begin", "tactical-combat"},
    {"combat-simulator:begin", "strategic-combat"},
    {"combat-simulator:strategies", "strategies"},
    {"tactical-combat:orders", "tactical-orders"},
    {"tactical-combat:options", "tactical-options"},
};

// The order-strip buttons that give an order kind.
constexpr std::pair<std::string_view, std::string_view> kOrderButtons[] = {
    {"move-to", "order:move-to"},
    {"warp", "order:warp"},
    {"attack", "order:attack"},
    {"resupply", "order:resupply"},
    {"repair", "order:repair"},
    {"explore", "order:explore"},
    {"colonize", "order:colonize"},
    {"colonize", "planets:send-colony-ship"},
    {"sentry", "order:sentry"},
    {"load-cargo", "order:load-cargo"},
    {"drop-cargo", "order:drop-cargo"},
    {"launch-units", "order:launch-recover"},
    {"launch-units", "order:launch-remote"},
    {"recover-units", "order:launch-recover"},
    {"recover-units", "order:recover-remote"},
    {"cloak", "order:cloak"},
    {"decloak", "order:decloak"},
    {"sweep-mines", "order:sweep-mines"},
    {"use-component", "order:use-component"},
    {"stellar-manipulation", "order:stellar-manipulation"},
    {"move-to-waypoint", "order:move-to-waypoint"},
    {"self-destruct", "order:scrap"},
};

// The widgets that give a command.
constexpr std::pair<std::string_view, std::string_view> kCommandWidgets[] = {
    {"CreateFleet", "fleet-transfer:create-fleet"},
    {"JoinFleet", "fleet-transfer:ships"},
    {"JoinFleet", "fleet-transfer:fleets"},
    {"LeaveFleet", "fleet-transfer:fleets"},
    {"QueueAdd", "set-queue:available"},
    {"QueueRemove", "set-queue:queue"},
    {"QueueMove", "set-queue:queue"},
    {"SetResearch", "research:areas"},
    {"SetResearch", "research:queue"},
    {"SetResearch", "research:divide-evenly"},
    {"SetResearch", "research:repeat"},
    {"SetIntel", "window:intelligence"},
    {"CreateDesign", "create-design:save"},
    {"EditDesign", "create-design:save"},
    {"SendMessage", "communicate:send"},
    {"AnswerMessage", "window:communicate"},
    {"AnswerMessage", "window:log"},
};

// Windows that close by themselves when their work is done, and the tags that
// do that work: a tactical battle played to its end closes its window.
constexpr std::pair<std::string_view, std::string_view> kClosesBy[] = {
    {"tactical-combat", "tactical-combat:end-turn"},
    {"tactical-combat", "tactical-combat:auto"},
};

bool anyOf(const StepAccess& a, std::initializer_list<std::string_view> tags) {
    return std::any_of(tags.begin(), tags.end(), [&](std::string_view t) { return a.has(t); });
}

bool endsTurns(const StepAccess& a) { return a.has("button:end-turn") || a.hasKey("F12"); }

bool hasDigitKey(const StepAccess& a, std::string_view modifier) {
    for (char d = '0'; d <= '9'; ++d)
        if (a.hasKey(std::string(modifier) + d)) return true;
    return false;
}

// Why the fact cannot be brought about; nothing when it can.
std::optional<std::string> leafProblem(const Condition& c, const StepAccess& a) {
    const std::string_view key = factInfo(c.fact).key;
    auto missing = [&](std::string_view what) { return std::format("{} needs {}", describe(c), what); };
    switch (c.fact) {
        case Fact::Window: {
            for (const std::string& t : a.tags) {
                const auto opened = windowsOpenedBy(t);
                if (std::find(opened.begin(), opened.end(), c.text) != opened.end()) return std::nullopt;
            }
            return missing(std::format("a highlighted or allowed tag that opens '{}'", c.text));
        }
        case Fact::Tab:
            if (a.has(c.text)) return std::nullopt;
            return missing(std::format("the tag '{}'", c.text));
        case Fact::Selected:
            if (c.text == "colony" || c.text == "planet") {
                if (anyOf(a, {"panel:system", "panel:report", "cycle:colony"})) return std::nullopt;
            } else if (c.text == "ship" || c.text == "base" || c.text == "unit" || c.text == "fleet") {
                if (anyOf(a, {"panel:system", "panel:report", "cycle:ship", "cycle:fleet"})) return std::nullopt;
            } else if (anyOf(a, {"panel:system", "panel:galaxy"})) {
                return std::nullopt;
            }
            return missing("the system view, the report or a selection cycle");
        case Fact::Command: {
            if (c.text == "SetOrders") {
                for (const std::string& t : a.tags)
                    if (t.starts_with("order:") || t == "panel:system" || t == "planets:send-colony-ship") return std::nullopt;
                return missing("an order button or the system view");
            }
            if (c.text == "SetWaypoint") {
                if (hasDigitKey(a, "Alt+")) return std::nullopt;
                return missing("an Alt+digit key");
            }
            bool known = false;
            for (const auto& [command, tag] : kCommandWidgets) {
                if (command != c.text) continue;
                known = true;
                if (a.has(tag)) return std::nullopt;
            }
            if (!known) return std::nullopt;   // no rule for it: the author's to judge
            return missing("the widget that gives that command");
        }
        case Fact::Order: {
            for (const auto& [kind, tag] : kOrderButtons)
                if (kind == c.text && a.has(tag)) return std::nullopt;
            if (c.text == "move-to" && a.has("panel:system")) return std::nullopt;   // right-click
            if (c.text == "move-to-waypoint" && hasDigitKey(a, "Ctrl+")) return std::nullopt;
            return missing("its order button");
        }
        case Fact::DesignComponents:
            if (a.has("create-design:components")) return std::nullopt;
            return missing("create-design:components");
        case Fact::DesignHullChosen:
            if (a.has("create-design:hull")) return std::nullopt;
            return missing("create-design:hull");
        case Fact::SimulatorOwners:
            if (a.has("combat-simulator:owners") && a.has("combat-simulator:items")) return std::nullopt;
            return missing("combat-simulator:owners and combat-simulator:items");
        case Fact::SimulatorItems:
            if (a.has("combat-simulator:items")) return std::nullopt;
            return missing("combat-simulator:items");
        case Fact::BattleBegun:
            if (a.has("tactical-combat:end-turn")) return std::nullopt;   // Begin is the button that becomes End Turn
            return missing("tactical-combat:end-turn (its Begin button)");
        case Fact::BattleOrder:
            if (c.text == "move" && a.has("tactical-combat:map")) return std::nullopt;
            if (c.text == "fire" && (a.has("tactical-combat:map") || a.has("tactical-combat:target"))) return std::nullopt;
            if (c.text == "toggle-weapon" && a.has("tactical-combat:weapons")) return std::nullopt;
            if (c.text == "end-turn" && (a.has("tactical-combat:end-turn") || a.hasKey("E"))) return std::nullopt;
            if (c.text == "auto" && a.has("tactical-combat:auto")) return std::nullopt;
            if (a.has("tactical-combat:orders") || a.has("window:tactical-combat")) return std::nullopt;
            return missing("the Tactical Combat widget that gives it");
        case Fact::Option:
            if (c.text == "research-evenly" && !a.has("research:divide-evenly")) return missing("research:divide-evenly");
            if (c.text == "research-repeat" && !a.has("research:repeat")) return missing("research:repeat");
            return std::nullopt;
        case Fact::Fleets:
            if (a.has("fleet-transfer:create-fleet") || endsTurns(a)) return std::nullopt;
            return missing("fleet-transfer:create-fleet");
        case Fact::Designs:
            if (a.has("create-design:save") || endsTurns(a)) return std::nullopt;
            return missing("create-design:save");
        case Fact::ResearchQueued:
            if (a.has("research:areas")) return std::nullopt;
            return missing("research:areas");
        case Fact::ConstructionQueued:
            if (a.has("set-queue:available") || endsTurns(a)) return std::nullopt;
            return missing("set-queue:available");
        default:
            // What comes with the turns: End Turn.
            if (endsTurns(a)) return std::nullopt;
            return missing(std::format("End Turn (button:end-turn) for '{}'", key));
    }
}

void problems(const Condition& c, const StepAccess& a, std::vector<std::string>& out) {
    switch (c.op) {
        case Condition::Op::All:
            for (const Condition& x : c.children) problems(x, a, out);
            return;
        case Condition::Op::Any: {
            std::vector<std::string> all;
            for (const Condition& x : c.children) {
                std::vector<std::string> mine;
                problems(x, a, mine);
                if (mine.empty()) return;
                all.insert(all.end(), mine.begin(), mine.end());
            }
            out.push_back(std::format("no part of {} can be reached ({})", describe(c), all.empty() ? std::string{} : all.front()));
            return;
        }
        case Condition::Op::Not: {
            if (c.children.empty()) return;
            const Condition& inner = c.children.front();
            if (inner.op == Condition::Op::Fact && inner.fact == Fact::Window) {
                // Closing a window: its Close button, the whole window, or Esc / Enter.
                if (a.has("window:" + inner.text) || a.has(inner.text + ":close") || a.hasKey("Escape") || a.hasKey("Enter")) return;
                for (const auto& [window, tag] : kClosesBy)
                    if (window == inner.text && a.has(tag)) return;
                out.push_back(std::format("{0} needs 'window:{1}' or '{1}:close' (or the Escape key)", describe(c), inner.text));
                return;
            }
            if (inner.op == Condition::Op::Fact && inner.fact == Fact::Option) {
                if (auto p = leafProblem(inner, a)) out.push_back(*p);
            }
            return;   // "not" of anything else holds by staying away from it
        }
        case Condition::Op::Fact:
            if (auto p = leafProblem(c, a)) out.push_back(*p);
            return;
    }
}

} // namespace

bool StepAccess::has(std::string_view tag) const { return std::find(tags.begin(), tags.end(), tag) != tags.end(); }
bool StepAccess::hasKey(std::string_view chord) const { return std::find(keys.begin(), keys.end(), chord) != keys.end(); }

StepAccess stepAccess(const Step& step) {
    StepAccess a;
    a.tags = step.highlight;
    a.tags.insert(a.tags.end(), step.allow.begin(), step.allow.end());
    a.keys = step.keys;
    return a;
}

std::vector<std::string_view> windowsOpenedBy(std::string_view tag) {
    std::vector<std::string_view> out;
    if (tag.starts_with("command:")) {
        const std::string_view id = tag.substr(8);
        if (const WindowInfo* w = findWindow(id)) out.push_back(w->id);
    }
    for (const auto& [t, window] : kOpens)
        if (t == tag) out.push_back(window);
    return out;
}

std::vector<std::string> openersOf(std::string_view window) {
    std::vector<std::string> out;
    if (const std::string command = "command:" + std::string(window); isUiTag(command)) out.push_back(command);
    for (const auto& [t, w] : kOpens)
        if (w == window) out.emplace_back(t);
    return out;
}

std::vector<std::string> reachProblems(const Condition& done, const StepAccess& access) {
    std::vector<std::string> out;
    problems(done, access, out);
    return out;
}

} // namespace opense4::learn
