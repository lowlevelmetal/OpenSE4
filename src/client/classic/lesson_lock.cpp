#include "client/classic/lesson_lock.hpp"

#include "client/classic/order_rules.hpp"
#include "learn/access.hpp"
#include "learn/ids.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <format>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace opense4::client::classic {

namespace {

bool isModifier(ImGuiKey k) {
    switch (k) {
        case ImGuiKey_LeftCtrl:
        case ImGuiKey_RightCtrl:
        case ImGuiKey_LeftShift:
        case ImGuiKey_RightShift:
        case ImGuiKey_LeftAlt:
        case ImGuiKey_RightAlt:
        case ImGuiKey_LeftSuper:
        case ImGuiKey_RightSuper: return true;
        default: return false;
    }
}

// The keys that answer a prompt (spec 06 §3.4): Y and N, T and S, Enter and Esc.
bool isPromptKey(const KeyChord& c) {
    if (c.ctrl || c.alt) return false;
    switch (c.key) {
        case ImGuiKey_Y:
        case ImGuiKey_N:
        case ImGuiKey_T:
        case ImGuiKey_S:
        case ImGuiKey_Enter:
        case ImGuiKey_KeypadEnter:
        case ImGuiKey_Escape: return true;
        default: return false;
    }
}

size_t slot(int button) { return static_cast<size_t>(std::clamp(button, 0, 7)); }

// The order strip's ids and the actions of their keys.
constexpr std::pair<std::string_view, Action> kOrderActions[] = {
    {"move-to", Action::MoveTo},
    {"warp", Action::Warp},
    {"move-to-waypoint", Action::MoveToWaypoint},
    {"colonize", Action::Colonize},
    {"attack", Action::Attack},
    {"fleet-transfer", Action::FleetTransfer},
    {"resupply", Action::Resupply},
    {"repair", Action::Repair},
    {"clear-orders", Action::ClearOrders},
    {"build-queue", Action::BuildQueue},
    {"cargo-transfer", Action::CargoTransfer},
    {"launch-recover", Action::LaunchRecover},
    {"load-cargo", Action::LoadCargo},
    {"drop-cargo", Action::DropCargo},
    {"launch-remote", Action::LaunchRemote},
    {"recover-remote", Action::RecoverRemote},
    {"sentry", Action::Sentry},
    {"explore", Action::Explore},
    {"patrol", Action::Patrol},
    {"repeat-orders", Action::RepeatOrders},
    {"stellar-manipulation", Action::StellarManipulation},
    {"rename", Action::Rename},
    {"scrap", Action::Scrap},
    {"strategy", Action::Strategy},
    {"view-orders", Action::ViewOrders},
    {"sweep-mines", Action::SweepMines},
    {"scrap-facilities", Action::ScrapFacilities},
    {"jettison", Action::Jettison},
    {"cloak", Action::Cloak},
    {"decloak", Action::Decloak},
    {"use-component", Action::UseComponent},
    {"use-facility", Action::UseFacility},
    {"abandon-planet", Action::AbandonPlanet},
    {"convert-resources", Action::ConvertResources},
    {"minister", Action::Minister},
    {"replay-play", Action::ReplayPlay},
    {"replay-ship", Action::ReplayShip},
    {"replay-step", Action::ReplayStep},
    {"replay-rewind", Action::ReplayRewind},
};

// The command buttons' windows and their keys.
constexpr std::pair<std::string_view, Action> kCommandActions[] = {
    {"game-menu", Action::GameMenu}, {"designs", Action::Designs},     {"planets", Action::Planets},
    {"colonies", Action::Colonies},  {"ships", Action::Ships},         {"queues", Action::Queues},
    {"research", Action::Research},  {"empires", Action::Empires},     {"log", Action::Log},
    {"empire-status", Action::EmpireStatus}, {"help", Action::Help},
};

// The window a tag is in ("research:areas", "window:research"), if any.
std::optional<std::string_view> windowOf(std::string_view tag) {
    if (tag.starts_with("window:")) return tag.substr(7);
    const size_t colon = tag.find(':');
    if (colon == std::string_view::npos) return std::nullopt;
    const std::string_view prefix = tag.substr(0, colon);
    if (const learn::WindowInfo* w = learn::findWindow(prefix)) return w->id;
    return std::nullopt;
}

bool contains(const std::vector<std::string>& v, std::string_view x) { return std::find(v.begin(), v.end(), x) != v.end(); }
bool contains(const std::vector<std::string_view>& v, std::string_view x) { return std::find(v.begin(), v.end(), x) != v.end(); }

const TaggedArea* findTag(const std::vector<TaggedArea>& tags, std::string_view name) {
    for (const TaggedArea& t : tags)
        if (t.name == name) return &t;
    return nullptr;
}

// A part with a size (a tag can be registered for something not drawn).
const TaggedArea* shownTag(const std::vector<TaggedArea>& tags, std::string_view name) {
    for (const TaggedArea& t : tags)
        if (t.name == name && t.area.max.x > t.area.min.x && t.area.max.y > t.area.min.y) return &t;
    return nullptr;
}

// The lesson panel's own parts and the T button: above every window.
bool isLessonTag(std::string_view tag) { return tag.starts_with("lesson:") || tag == "status:lesson"; }

bool anyContains(const std::vector<LockArea>& areas, ImVec2 p) {
    return std::any_of(areas.begin(), areas.end(), [&](const LockArea& a) { return a.contains(p); });
}

void grow(std::vector<LockArea>& areas, float by) {
    for (LockArea& a : areas) a = a.grown(by);
}

} // namespace

std::optional<std::string_view> tagWindowId(std::string_view tag) { return windowOf(tag); }

std::vector<Action> tagActions(std::string_view tag) {
    if (tag == "button:end-turn") return {Action::EndTurn};
    if (tag == "status:lesson") return {Action::LessonText};
    if (tag == "cycle:ship") return {Action::NextIdleShip, Action::NextShip, Action::PreviousShip};
    if (tag == "cycle:fleet") return {Action::NextFleet, Action::PreviousFleet};
    if (tag == "cycle:colony") return {Action::NextColony, Action::PreviousColony};
    if (tag.starts_with("command:"))
        for (const auto& [id, action] : kCommandActions)
            if (id == tag.substr(8)) return {action};
    if (tag.starts_with("order:"))
        for (const auto& [id, action] : kOrderActions)
            if (id == tag.substr(6)) return {action};
    return {};
}

LockState makeLockState(const learn::Step& step, const std::vector<TaggedArea>& tags, const std::vector<std::string>& openWindows,
                        const std::vector<LockArea>& prompts, bool typing, const Bindings& bindings, const std::vector<std::string>& leftOpen) {
    LockState st;
    st.active = true;
    st.typing = typing;
    // An action step's outlines are its buttons; an explanation step's are to
    // look at, as is what any step shows.
    std::vector<std::string> allowed = step.done ? step.highlight : std::vector<std::string>{};
    const std::vector<std::string> outlinedLook = step.done ? std::vector<std::string>{} : step.highlight;
    std::vector<std::string> look = outlinedLook;
    look.insert(look.end(), step.show.begin(), step.show.end());
    allowed.insert(allowed.end(), step.allow.begin(), step.allow.end());
    allowed.emplace_back("lesson:panel");
    allowed.emplace_back("status:lesson");
    // The windows the step says nothing about.
    auto named = [](const std::vector<std::string>& list, std::string_view window) {
        return std::any_of(list.begin(), list.end(), [&](const std::string& tag) { return windowOf(tag) == window; });
    };
    // Those an earlier step left open can only be closed: whatever else they
    // do is not what this step asks for (the queue windows of the step before
    // "close both queue windows, then open Colonies").
    for (const std::string& w : openWindows)
        if (contains(leftOpen, w) && !named(allowed, w) && !named(look, w)) allowed.push_back(w + ":close");
    // A window the step works in that is closed: the ways to open it, two levels deep
    // (the designer's targets wait behind Designs, which waits behind its command button).
    // A Close button needs no window opened for it.
    for (int depth = 0; depth < 2; ++depth) {
        std::vector<std::string> now = allowed;
        if (depth == 0) now.insert(now.end(), look.begin(), look.end());
        for (const std::string& tag : now) {
            if (tag.ends_with(":close")) continue;
            const auto window = windowOf(tag);
            if (!window || contains(openWindows, *window)) continue;
            for (std::string& opener : learn::openersOf(*window))
                if (!contains(allowed, opener)) allowed.push_back(std::move(opener));
        }
    }
    // The other windows the step says nothing about are the player's.
    auto constrained = [&](std::string_view window) { return named(allowed, window) || named(look, window); };
    // An outlined part another window the step names covers: that window's
    // Close button (and its keys), the way back the lesson points at. A window
    // the step says nothing about can be closed anyway.
    for (const std::string& tag : step.highlight)
        if (const auto cover = coveringWindow(tag, tags, openWindows); cover && constrained(*cover))
            if (std::string close = *cover + ":close"; !contains(allowed, close)) allowed.push_back(std::move(close));
    // The open windows, front first: where one lies, it decides.
    for (auto w = openWindows.rbegin(); w != openWindows.rend(); ++w)
        if (const TaggedArea* t = findTag(tags, "window:" + *w))
            st.windows.push_back(LockWindow{*w, t->area, constrained(*w), {}, {}});
    // The choosers the step names options of, and those options ("*": all of them).
    std::vector<std::pair<const learn::ChoiceGroup*, std::vector<std::string_view>>> chosen;
    for (const auto* list : {&step.highlight, &step.allow})
        for (const std::string& tag : *list)
            if (const learn::ChoiceGroup* g = learn::choiceGroupOf(tag)) {
                auto at = std::find_if(chosen.begin(), chosen.end(), [&](const auto& c) { return c.first == g; });
                if (at == chosen.end()) at = chosen.insert(chosen.end(), {g, {}});
                at->second.push_back(std::string_view(tag).substr(g->tag.size() + 1));
            }
    auto layerOf = [&](const TaggedArea& t) -> LockChoices& {
        if (t.top) return st.topChoices;
        const auto window = windowOf(t.name);
        auto in = std::find_if(st.windows.begin(), st.windows.end(), [&](const LockWindow& w) { return window && w.id == *window; });
        return in != st.windows.end() ? in->choices : st.choices;
    };
    for (const TaggedArea& t : tags) {
        const learn::ChoiceGroup* g = learn::choiceGroupOf(t.name);
        if (!g) continue;
        const auto picked = std::find_if(chosen.begin(), chosen.end(), [&](const auto& c) { return c.first == g; });
        if (picked == chosen.end()) continue;
        const std::string_view option = t.name.substr(g->tag.size() + 1);
        const bool any = contains(picked->second, "*");
        (any || contains(picked->second, option) ? layerOf(t).chosen : layerOf(t).refused).push_back(t.area);
    }
    for (const TaggedArea& t : tags) {
        // A page arrow that stands in for an outlined order only turns the
        // page: it responds on an explanation step too.
        const bool act = contains(allowed, t.name) || (t.pager && contains(outlinedLook, t.name));
        if (!act && !contains(look, t.name)) continue;
        // The lesson panel and the T button lie above every window.
        if (isLessonTag(t.name)) {
            if (act) st.top.push_back(t.area);
            continue;
        }
        const auto window = windowOf(t.name);
        auto in = std::find_if(st.windows.begin(), st.windows.end(), [&](const LockWindow& w) { return window && w.id == *window; });
        if (in != st.windows.end()) (act ? in->areas : in->lookAreas).push_back(t.area);
        else (act ? st.areas : st.lookAreas).push_back(t.area);
    }
    st.windowKeys = !openWindows.empty() && !constrained(openWindows.back());
    st.top.insert(st.top.end(), prompts.begin(), prompts.end());
    st.prompt = !prompts.empty();
    // Keys: the step's, its tags' hotkeys, and the panel's.
    for (const std::string& k : step.keys)
        if (const auto chord = parseChord(k); chord && !chord->empty()) st.keys.push_back(*chord);
    auto addAction = [&](Action a) {
        for (const KeyChord& c : bindings.chords(a))
            if (!c.empty()) st.keys.push_back(c);
    };
    for (const std::string& tag : allowed) {
        for (const Action a : tagActions(tag)) addAction(a);
        if (tag.ends_with(":close")) {
            st.keys.push_back(KeyChord{ImGuiKey_Escape});
            st.keys.push_back(KeyChord{ImGuiKey_Enter});
        }
    }
    for (const Action a : {Action::LessonText, Action::LessonNext, Action::LessonBack, Action::LessonSkip, Action::LessonReadMore, Action::ContextHelp}) addAction(a);
    return st;
}

bool LockChoices::refuses(ImVec2 p) const { return anyContains(refused, p) && !anyContains(chosen, p); }

LockState::Access LockState::access(ImVec2 p) const {
    if (anyContains(top, p)) return topChoices.refuses(p) ? Access::Refused : Access::Act;
    for (const LockWindow& w : windows) {
        if (!w.area.contains(p)) continue;
        if (w.choices.refuses(p)) return Access::Refused;
        if (!w.constrained || anyContains(w.areas, p)) return Access::Act;
        return anyContains(w.lookAreas, p) ? Access::Look : Access::None;
    }
    if (choices.refuses(p)) return Access::Refused;
    if (anyContains(areas, p)) return Access::Act;
    return anyContains(lookAreas, p) ? Access::Look : Access::None;
}

LockState LockState::grown(float by) const {
    LockState g = *this;
    grow(g.areas, by);
    grow(g.lookAreas, by);
    grow(g.choices.chosen, by);
    grow(g.topChoices.chosen, by);
    for (LockWindow& w : g.windows) {
        grow(w.areas, by);
        grow(w.lookAreas, by);
        grow(w.choices.chosen, by);
    }
    return g;
}

std::vector<LockArea> LockState::rects() const {
    std::vector<LockArea> out = top;
    auto add = [&](const std::vector<LockArea>& v) { out.insert(out.end(), v.begin(), v.end()); };
    add(areas);
    add(lookAreas);
    for (const LockChoices* c : {&choices, &topChoices}) {
        add(c->chosen);
        add(c->refused);
    }
    for (const LockWindow& w : windows) {
        out.push_back(w.area);
        add(w.areas);
        add(w.lookAreas);
        add(w.choices.chosen);
        add(w.choices.refused);
    }
    return out;
}

size_t LockState::parts() const {
    size_t n = top.size() + areas.size() + lookAreas.size();
    for (const LockWindow& w : windows) n += w.constrained ? w.areas.size() + w.lookAreas.size() : 1;
    return n;
}

std::optional<std::string> coveringWindow(std::string_view tag, const std::vector<TaggedArea>& tags, const std::vector<std::string>& openWindows) {
    const TaggedArea* t = shownTag(tags, tag);
    if (!t || isLessonTag(tag)) return std::nullopt;
    // The windows in front of the one the part is in (all of them for the main window's parts).
    size_t first = 0;
    if (const auto own = windowOf(tag))
        if (const auto at = std::find(openWindows.begin(), openWindows.end(), *own); at != openWindows.end())
            first = size_t(at - openWindows.begin()) + 1;
    const ImVec2 middle = t->area.centre();
    for (size_t i = openWindows.size(); i-- > first;)
        if (const TaggedArea* w = findTag(tags, "window:" + openWindows[i]); w && w->area.contains(middle)) return openWindows[i];
    return std::nullopt;
}

Recovery findRecovery(const learn::Step& step, const std::vector<TaggedArea>& tags, const std::vector<std::string>& openWindows) {
    std::vector<std::string_view> targets;
    for (const std::string& t : step.highlight)
        if (!isLessonTag(t)) targets.push_back(t);
    // Nothing to do while an outlined part can be used.
    for (std::string_view t : targets)
        if (shownTag(tags, t) && !coveringWindow(t, tags, openWindows)) return {};
    auto uncover = [&](std::string_view target, std::string window) {
        Recovery r{Recovery::Kind::Uncover, std::string(target), std::move(window), {}, {}};
        if (std::string close = r.window + ":close"; shownTag(tags, close)) r.press = std::move(close);
        return r;
    };
    // A part on screen that a window covers, or a way to open its window again
    // that is on screen (two windows deep), in the order the step names them.
    for (std::string_view t : targets) {
        if (shownTag(tags, t)) {
            if (auto cover = coveringWindow(t, tags, openWindows)) return uncover(t, std::move(*cover));
            continue;
        }
        const auto window = windowOf(t);
        if (!window || contains(openWindows, *window)) continue;
        const std::vector<std::string> openers = learn::openersOf(*window);
        for (const std::string& o : openers) {
            if (!shownTag(tags, o)) continue;
            if (auto cover = coveringWindow(o, tags, openWindows)) return uncover(t, std::move(*cover));
            return Recovery{Recovery::Kind::Reopen, std::string(t), std::string(*window), o, {}};
        }
        for (const std::string& o : openers) {
            const auto inner = windowOf(o);
            if (!inner || contains(openWindows, *inner)) continue;
            for (const std::string& first : learn::openersOf(*inner)) {
                if (!shownTag(tags, first)) continue;
                if (auto cover = coveringWindow(first, tags, openWindows)) return uncover(t, std::move(*cover));
                return Recovery{Recovery::Kind::Reopen, std::string(t), std::string(*window), first, o};
            }
        }
    }
    return {};
}

namespace {

// The order strip's name of an order tag ("order:resupply": "Resupply At Nearest").
std::string orderTitle(std::string_view tag) {
    const std::string_view id = tag.substr(tag.find(':') + 1);
    for (size_t i = 0; i < kOrderCount; ++i) {
        const auto o = static_cast<OrderId>(i);
        if (learn::orderStripId(orderSlotKey(o)) == id) return std::string(orderName(o));
    }
    return std::string(id);
}

} // namespace

std::vector<std::string> pagedTargets(const learn::Step& step, const std::vector<TaggedArea>& tags) {
    std::vector<std::string> out;
    for (const std::string& t : step.highlight) {
        if (!t.starts_with("order:") || contains(out, t)) continue;
        bool paged = false, shown = false;
        for (const TaggedArea& a : tags)
            if (a.name == t) (a.pager ? paged : shown) = true;
        if (paged && !shown) out.push_back(t);
    }
    return out;
}

std::string pagerHint(const learn::Step& step, const std::vector<TaggedArea>& tags) {
    const std::vector<std::string> paged = pagedTargets(step, tags);
    if (paged.empty()) return {};
    std::string names;
    std::vector<float> arrows;   // the left edges of the arrows they lie on
    for (size_t i = 0; i < paged.size(); ++i) {
        names += std::format("{}**{}**", i == 0 ? "" : i + 1 == paged.size() ? " and " : ", ", orderTitle(paged[i]));
        for (const TaggedArea& a : tags)
            if (a.pager && a.name == paged[i] &&
                std::none_of(arrows.begin(), arrows.end(), [&](float x) { return std::abs(x - a.area.min.x) < 0.5f; }))
                arrows.push_back(a.area.min.x);
    }
    const bool both = arrows.size() > 1;
    return std::format("Press {} outlined arrow to show more order buttons: {} {} on {}.", both ? "an" : "the", names,
                       paged.size() > 1 ? "are" : "is", both ? "other pages" : "another page");
}

namespace {

// `negated`: under a `not` (a window closing rather than opening).
bool waits(const learn::Condition& c, bool negated) {
    using learn::Fact;
    if (c.op != learn::Condition::Op::Fact) {
        const bool inner = negated != (c.op == learn::Condition::Op::Not);
        return std::any_of(c.children.begin(), c.children.end(), [&](const learn::Condition& x) { return waits(x, inner); });
    }
    switch (c.fact) {
        // What a click or a key brings about at once.
        case Fact::Selected:
        case Fact::Command:
        case Fact::Order:
        case Fact::Tab:
        case Fact::DesignComponents:
        case Fact::DesignHullChosen:
        case Fact::DesignTypeChosen:
        case Fact::DesignVehicle:
        case Fact::DesignNamed:
        case Fact::SimulatorOwners:
        case Fact::SimulatorItems:
        case Fact::SimulatorOwner:
        case Fact::BattleBegun:
        case Fact::Option:
        case Fact::ResearchQueued:
        case Fact::ConstructionQueued: return false;
        // A battle window closes once its battle has played out (or been watched).
        case Fact::Window: return negated && (c.text == "tactical-combat" || c.text == "strategic-combat" || c.text == "ground-combat");
        // A battle order may wait for the combat turns that bring the enemy in range.
        case Fact::BattleOrder: return true;
        // The turns, and what grows with them.
        default: return true;
    }
}

} // namespace

bool waitsOnGame(const learn::Condition& done) { return waits(done, false); }

bool InputLock::anyHeld() const {
    return std::any_of(held_.begin(), held_.end(), [](uint8_t h) { return h != 0; });
}

InputVerdict InputLock::mouseMove(ImVec2 p) const {
    if (!state_.active || anyHeld() || state_.allows(p) || state_.looks(p)) return InputVerdict::Pass;
    return InputVerdict::PointerAway;
}

InputVerdict InputLock::mouseButton(ImVec2 p, int button, bool down) {
    const size_t b = slot(button);
    if (down) {
        // A press decides for its drag and its release.
        if (!state_.active || state_.allows(p)) {
            held_[b] = 1;
            swallowed_[b] = 0;
            return InputVerdict::Pass;
        }
        swallowed_[b] = 1;
        refused_ = p;
        return InputVerdict::Drop;
    }
    if (swallowed_[b]) {
        swallowed_[b] = 0;
        return InputVerdict::Drop;
    }
    held_[b] = 0;
    return InputVerdict::Pass;
}

InputVerdict InputLock::wheel(ImVec2 p) const {
    if (!state_.active || state_.allows(p) || state_.looks(p)) return InputVerdict::Pass;
    return InputVerdict::Drop;
}

InputVerdict InputLock::key(const KeyChord& chord, bool down) const {
    if (!state_.active || !down || state_.typing || isModifier(chord.key)) return InputVerdict::Pass;
    if (state_.prompt && isPromptKey(chord)) return InputVerdict::Pass;
    if (state_.windowKeys && !chord.ctrl && !chord.alt && !chord.shift &&
        (chord.key == ImGuiKey_Escape || chord.key == ImGuiKey_Enter || chord.key == ImGuiKey_KeypadEnter))
        return InputVerdict::Pass;
    for (const KeyChord& k : state_.keys)
        if (k == chord || (k.key == ImGuiKey_Enter && chord.key == ImGuiKey_KeypadEnter && k.ctrl == chord.ctrl && k.shift == chord.shift && k.alt == chord.alt))
            return InputVerdict::Pass;
    return InputVerdict::Drop;
}

InputVerdict InputLock::text() const {
    if (!state_.active || state_.typing) return InputVerdict::Pass;
    return InputVerdict::Drop;
}

} // namespace opense4::client::classic
