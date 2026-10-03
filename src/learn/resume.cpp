#include "learn/resume.hpp"

#include "core/hash.hpp"
#include "learn/access.hpp"
#include "learn/ids.hpp"

#include <algorithm>
#include <optional>
#include <span>

namespace opense4::learn {

namespace {

// The window a tag lies in, if any (as the input lock reads tags).
std::optional<std::string_view> windowOf(std::string_view tag) {
    if (tag.starts_with("window:")) return tag.substr(7);
    const size_t colon = tag.find(':');
    if (colon == std::string_view::npos) return std::nullopt;
    if (const WindowInfo* w = findWindow(tag.substr(0, colon))) return w->id;
    return std::nullopt;
}

bool contains(const std::vector<std::string_view>& v, std::string_view x) { return std::find(v.begin(), v.end(), x) != v.end(); }

// The step works in one of `windows`, or one of its tags opens one.
bool touches(const Step& step, const std::vector<std::string_view>& windows) {
    for (const auto* list : {&step.highlight, &step.allow, &step.show})
        for (const std::string& tag : *list) {
            if (const auto w = windowOf(tag); w && contains(windows, *w)) return true;
            for (std::string_view opened : windowsOpenedBy(tag))
                if (contains(windows, opened)) return true;
        }
    return false;
}

} // namespace

std::vector<std::string_view> stepWindows(const Step& step) {
    std::vector<std::string_view> out;
    for (const auto* list : {&step.highlight, &step.allow, &step.show})
        for (const std::string& tag : *list)
            if (const auto w = windowOf(tag); w && !contains(out, *w)) out.push_back(*w);
    return out;
}

bool worksIn(const Step& step, std::string_view window) {
    for (const auto* list : {&step.highlight, &step.allow, &step.show})
        for (const std::string& tag : *list)
            if (!tag.ends_with(":close") && windowOf(tag) == window) return true;
    return false;
}

bool opens(const Step& step, std::string_view window) {
    for (const auto* list : {&step.highlight, &step.allow})
        for (const std::string& tag : *list)
            for (std::string_view opened : windowsOpenedBy(tag))
                if (opened == window) return true;
    return false;
}

namespace {

constexpr Fact kDesignerWork[] = {Fact::DesignComponents, Fact::DesignHullChosen, Fact::DesignTypeChosen, Fact::DesignNamed, Fact::DesignVehicle};
constexpr Fact kSimulatorWork[] = {Fact::SimulatorOwners, Fact::SimulatorItems, Fact::SimulatorOwner};
constexpr Fact kMessageWork[] = {Fact::DraftMessageType, Fact::DraftTreaty};

// The buttons of such a window that do not use its work: the simulator's
// Strategies opens the strategies whatever the battle set up.
constexpr std::string_view kSimulatorFree[] = {"combat-simulator:strategies"};

bool reads(const Condition& c, std::span<const Fact> facts) {
    if (c.op != Condition::Op::Fact)
        return std::any_of(c.children.begin(), c.children.end(), [&](const Condition& x) { return reads(x, facts); });
    return std::find(facts.begin(), facts.end(), c.fact) != facts.end();
}

} // namespace

std::span<const Fact> lostWithWindow(std::string_view window) {
    if (window == "create-design") return kDesignerWork;
    if (window == "combat-simulator") return kSimulatorWork;
    if (window == "communicate") return kMessageWork;
    return {};
}

bool usesWork(const Step& step, std::string_view window) {
    if (lostWithWindow(window).empty()) return false;
    const std::span<const std::string_view> free = window == "combat-simulator" ? std::span<const std::string_view>(kSimulatorFree)
                                                                                : std::span<const std::string_view>();
    for (const auto* list : {&step.highlight, &step.allow})
        for (const std::string& tag : *list)
            if (!tag.ends_with(":close") && windowOf(tag) == window && std::find(free.begin(), free.end(), tag) == free.end())
                return true;
    return false;
}

bool clientOnly(const Condition& c) {
    if (c.op != Condition::Op::Fact) return std::all_of(c.children.begin(), c.children.end(), [](const Condition& x) { return clientOnly(x); });
    switch (c.fact) {
        case Fact::Window:
        case Fact::Tab:
        case Fact::Selected:
        case Fact::Picking:
        case Fact::MovementLines:
        case Fact::DesignComponents:
        case Fact::DesignHullChosen:
        case Fact::DesignTypeChosen:
        case Fact::DesignNamed:
        case Fact::DesignVehicle:
        case Fact::SimulatorOwners:
        case Fact::SimulatorItems:
        case Fact::SimulatorOwner:
        case Fact::DraftMessageType:
        case Fact::DraftTreaty:
        // A battle in the Combat Simulator is the client's (a real one is fought in turns, which block).
        case Fact::BattleBegun:
        case Fact::BattleOrder:
        case Fact::BattleTurn: return true;
        default: return false;
    }
}

size_t rewindStep(const Lesson& lesson, size_t active, std::string_view window) {
    const std::span<const Fact> lost = lostWithWindow(window);
    if (active >= lesson.steps.size() || lost.empty() || !usesWork(lesson.steps[active], window)) return active;
    size_t to = active;
    for (size_t at = active; at-- > 0;) {
        const Step& st = lesson.steps[at];
        if (st.done && !clientOnly(*st.done)) break;   // the game kept what it did
        if (st.done && reads(*st.done, lost)) to = at;
    }
    return to;
}

size_t resumeStep(const Lesson& lesson, size_t active) {
    if (lesson.steps.empty()) return 0;
    size_t at = std::min(active, lesson.steps.size() - 1);
    for (;;) {
        while (at > 0) {
            const std::vector<std::string_view> windows = stepWindows(lesson.steps[at]);
            if (windows.empty() || !touches(lesson.steps[at - 1], windows)) break;
            --at;
        }
        // The work a window held is gone with it: back to the step that set
        // up what this one uses (a battle in the simulator), and from there
        // to where the work in that window began.
        size_t to = at;
        for (std::string_view w : stepWindows(lesson.steps[at])) to = std::min(to, rewindStep(lesson, at, w));
        if (to == at) return at;
        at = to;
    }
}

uint64_t lessonFingerprint(const Lesson& lesson) {
    Hasher h;
    h.add(std::string_view(kindName(lesson.kind))).add(lesson.steps.size());
    for (const Step& st : lesson.steps) {
        for (const auto* list : {&st.highlight, &st.allow, &st.keys}) {
            h.add(list->size());
            for (const std::string& s : *list) h.add(std::string_view(s));
        }
        // What a step shows counts once there is some (older places of lessons without it still fit).
        if (!st.show.empty()) {
            h.add(std::string_view("show")).add(st.show.size());
            for (const std::string& s : st.show) h.add(std::string_view(s));
        }
        if (!st.rightClick.empty()) {
            h.add(std::string_view("right_click")).add(st.rightClick.size());
            for (const std::string& s : st.rightClick) h.add(std::string_view(s));
        }
        h.add(std::string_view(st.done ? describe(*st.done) : std::string("-")));
    }
    return h.value();
}

} // namespace opense4::learn
