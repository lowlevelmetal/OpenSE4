#include "client/classic/lesson_lock.hpp"

#include "learn/access.hpp"
#include "learn/ids.hpp"

#include <algorithm>
#include <cstddef>
#include <iterator>

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
                        const std::vector<LockArea>& prompts, bool typing, const Bindings& bindings) {
    LockState st;
    st.active = true;
    st.typing = typing;
    // An action step's outlines are its buttons; an explanation step's are to look at.
    std::vector<std::string> allowed = step.done ? step.highlight : std::vector<std::string>{};
    const std::vector<std::string> look = step.done ? std::vector<std::string>{} : step.highlight;
    allowed.insert(allowed.end(), step.allow.begin(), step.allow.end());
    allowed.emplace_back("lesson:panel");
    allowed.emplace_back("status:lesson");
    // A window the step works in that is closed: the ways to open it, two levels deep
    // (the designer's targets wait behind Designs, which waits behind its command button).
    for (int depth = 0; depth < 2; ++depth) {
        std::vector<std::string> now = allowed;
        if (depth == 0) now.insert(now.end(), look.begin(), look.end());
        for (const std::string& tag : now) {
            const auto window = windowOf(tag);
            if (!window || contains(openWindows, *window)) continue;
            for (std::string& opener : learn::openersOf(*window))
                if (!contains(allowed, opener)) allowed.push_back(std::move(opener));
        }
    }
    for (const TaggedArea& t : tags) {
        if (contains(allowed, t.name)) st.areas.push_back(t.area);
        else if (contains(look, t.name)) st.lookAreas.push_back(t.area);
    }
    // The windows the step says nothing about are the player's.
    auto constrained = [&](std::string_view window) {
        auto in = [&](const std::string& tag) { return windowOf(tag) == window; };
        return std::any_of(allowed.begin(), allowed.end(), in) || std::any_of(look.begin(), look.end(), in);
    };
    for (const std::string& w : openWindows) {
        if (constrained(w)) continue;
        const std::string windowTag = "window:" + w;
        for (const TaggedArea& t : tags)
            if (t.name == windowTag) st.areas.push_back(t.area);
    }
    st.windowKeys = !openWindows.empty() && !constrained(openWindows.back());
    st.areas.insert(st.areas.end(), prompts.begin(), prompts.end());
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
    addAction(Action::LessonText);
    return st;
}

bool InputLock::allowedAt(ImVec2 p) const {
    return std::any_of(state_.areas.begin(), state_.areas.end(), [&](const LockArea& a) { return a.contains(p); });
}

bool InputLock::lookAt(ImVec2 p) const {
    return std::any_of(state_.lookAreas.begin(), state_.lookAreas.end(), [&](const LockArea& a) { return a.contains(p); });
}

bool InputLock::anyHeld() const {
    return std::any_of(held_.begin(), held_.end(), [](uint8_t h) { return h != 0; });
}

InputVerdict InputLock::mouseMove(ImVec2 p) const {
    if (!state_.active || anyHeld() || allowedAt(p) || lookAt(p)) return InputVerdict::Pass;
    return InputVerdict::PointerAway;
}

InputVerdict InputLock::mouseButton(ImVec2 p, int button, bool down) {
    const size_t b = slot(button);
    if (down) {
        // A press decides for its drag and its release.
        if (!state_.active || allowedAt(p)) {
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
    if (!state_.active || allowedAt(p) || lookAt(p)) return InputVerdict::Pass;
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
