#include "learn/resume.hpp"

#include "core/hash.hpp"
#include "learn/access.hpp"
#include "learn/ids.hpp"

#include <algorithm>
#include <optional>

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

size_t resumeStep(const Lesson& lesson, size_t active) {
    if (lesson.steps.empty()) return 0;
    size_t at = std::min(active, lesson.steps.size() - 1);
    while (at > 0) {
        const std::vector<std::string_view> windows = stepWindows(lesson.steps[at]);
        if (windows.empty() || !touches(lesson.steps[at - 1], windows)) break;
        --at;
    }
    return at;
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
        h.add(std::string_view(st.done ? describe(*st.done) : std::string("-")));
    }
    return h.value();
}

} // namespace opense4::learn
