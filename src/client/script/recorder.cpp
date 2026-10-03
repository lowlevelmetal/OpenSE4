#include "client/script/recorder.hpp"

#include "client/script/items.hpp"
#include "client/script/script.hpp"

#include <cmath>
#include <format>
#include <fstream>

// The SDL3 backend's key mapping (imgui_impl_sdl3.cpp).
ImGuiKey ImGui_ImplSDL3_KeyEventToImGuiKey(SDL_Keycode keycode, SDL_Scancode scancode);

namespace opense4::client::script {

namespace {

constexpr float kDragDistance = 6.0f;
constexpr double kDoubleClickSeconds = 0.3;

float area(ImVec2 a, ImVec2 b) { return std::max(0.0f, b.x - a.x) * std::max(0.0f, b.y - a.y); }
bool inside(ImVec2 p, ImVec2 a, ImVec2 b) { return p.x >= a.x && p.y >= a.y && p.x < b.x && p.y < b.y; }

// "@x,y" in frame pixels from the rectangle's corner, unless p is at its middle.
std::string offsetFor(ImVec2 p, ImVec2 a, ImVec2 b, float k) {
    const ImVec2 c((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
    if (std::abs(p.x - c.x) <= 4 * k && std::abs(p.y - c.y) <= 4 * k) return {};
    return std::format("@{},{}", std::lround((p.x - a.x) / k), std::lround((p.y - a.y) / k));
}

bool printable(ImGuiKey k) {
    return (k >= ImGuiKey_A && k <= ImGuiKey_Z) || (k >= ImGuiKey_0 && k <= ImGuiKey_9) || k == ImGuiKey_Space ||
           (k >= ImGuiKey_Apostrophe && k <= ImGuiKey_GraveAccent) || (k >= ImGuiKey_Keypad0 && k <= ImGuiKey_KeypadEqual);
}

bool modifierKey(ImGuiKey k) {
    return k == ImGuiKey_LeftCtrl || k == ImGuiKey_RightCtrl || k == ImGuiKey_LeftShift || k == ImGuiKey_RightShift || k == ImGuiKey_LeftAlt ||
           k == ImGuiKey_RightAlt || k == ImGuiKey_LeftSuper || k == ImGuiKey_RightSuper;
}

} // namespace

Recorder::Recorder(std::filesystem::path file, std::vector<std::string> options) : file_(std::move(file)), options_(std::move(options)) {}

std::string Recorder::targetFor(ImVec2 p, const Probe& probe) {
    const float k = std::max(0.01f, probe.frameScale());
    // The smallest labelled widget under the pointer.
    const std::vector<Item>& items = probe.items();
    const Item* best = nullptr;
    for (const Item& item : items) {
        if (!inside(p, item.min, item.max) || item.label.empty() || item.label == "##classic") continue;
        // On a tie the later one: our own names come after Dear ImGui's for the same widget.
        if (!best || area(item.min, item.max) <= area(best->min, best->max)) best = &item;
    }
    // The smallest UI tag around it.
    std::optional<std::pair<std::string, Box>> tag;
    for (const std::string& name : probe.tagNames())
        for (const Box& b : probe.tagBoxes(name))
            if (inside(p, b.min, b.max) && (!tag || area(b.min, b.max) < area(tag->second.min, tag->second.max))) tag = {name, b};
    if (best && (!tag || area(best->min, best->max) < area(tag->second.min, tag->second.max))) {
        // Its label; where it is drawn, and which of them, when the label is not unique.
        size_t total = 0, inScope = 0, index = 0, indexInScope = 0;
        for (const Item& item : items) {
            if (item.label != best->label) continue;
            if (&item == best) {
                index = total;
                indexInScope = inScope;
            }
            ++total;
            if (item.scope == best->scope) ++inScope;
        }
        std::string out = "item:" + quoteWord(best->label);
        if (area(best->min, best->max) > 1600 * k * k) out += offsetFor(p, best->min, best->max, k);
        if (total > 1 && !best->scope.empty()) {
            out += " in=" + quoteWord(best->scope);
            if (inScope > 1) out += std::format(" nth={}", indexInScope + 1);
        } else if (total > 1) {
            out += std::format(" nth={}", index + 1);
        }
        return out;
    }
    // The system view and the galaxy panel name sectors and systems.
    if (std::string map = probe.targetAt(p); !map.empty() && (!tag || tag->first == "panel:system" || tag->first == "panel:galaxy")) return map;
    if (tag) {
        std::string name = tag->first;
        if (name.starts_with("window:")) name = "window:" + name.substr(7);
        else name = "tag:" + name;
        return name + offsetFor(p, tag->second.min, tag->second.max, k);
    }
    // A point of the classic frame.
    const ImVec2 origin = probe.framePoint(0, 0);
    return std::format("at:{},{}", std::lround((p.x - origin.x) / k), std::lround((p.y - origin.y) / k));
}

void Recorder::line(std::string text, double seconds) {
    if (lastAction_ >= 0 && seconds - lastAction_ > 1.0) lines_.push_back(std::format("# ({:.1f} s later)", seconds - lastAction_));
    lastAction_ = seconds;
    lines_.push_back(std::move(text));
    // What the press already did (a lesson step done on the press) follows the click.
    if (!press_) {
        for (std::string& l : deferred_) lines_.push_back(std::move(l));
        deferred_.clear();
    }
}

void Recorder::progress(std::string text) {
    if (press_) deferred_.push_back(std::move(text));
    else lines_.push_back(std::move(text));
}

void Recorder::flushText(double seconds) {
    if (text_.empty()) return;
    line("type " + quoteWord(text_), seconds);
    text_.clear();
}

void Recorder::event(const SDL_Event& e, const Probe& probe, double seconds) {
    auto mods = [&] {
        std::string m;
        if (ctrl_) m += " ctrl";
        if (shift_) m += " shift";
        if (alt_) m += " alt";
        return m;
    };
    switch (e.type) {
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP: {
            ctrl_ = (e.key.mod & SDL_KMOD_CTRL) != 0;
            shift_ = (e.key.mod & SDL_KMOD_SHIFT) != 0;
            alt_ = (e.key.mod & (SDL_KMOD_ALT | SDL_KMOD_MODE)) != 0;
            if (e.type != SDL_EVENT_KEY_DOWN || e.key.repeat) break;
            const ImGuiKey key = ImGui_ImplSDL3_KeyEventToImGuiKey(e.key.key, e.key.scancode);
            if (key == ImGuiKey_None || modifierKey(key)) break;
            // Typing into a text field is recorded as its text.
            if (probe.typing() && !ctrl_ && !alt_ && printable(key)) break;
            flushText(seconds);
            line("key " + chordName(KeyChord{key, ctrl_, shift_, alt_}), seconds);
            break;
        }
        case SDL_EVENT_TEXT_INPUT:
            if (e.text.text) text_ += e.text.text;
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN: {
            flushText(seconds);
            const ImVec2 p(e.button.x, e.button.y);
            press_ = Press{e.button.button, p, targetFor(p, probe), false};
            break;
        }
        case SDL_EVENT_MOUSE_MOTION:
            if (press_) {
                const float dx = e.motion.x - press_->at.x, dy = e.motion.y - press_->at.y;
                if (dx * dx + dy * dy > kDragDistance * kDragDistance) press_->moved = true;
            }
            break;
        case SDL_EVENT_MOUSE_BUTTON_UP: {
            if (!press_ || press_->button != e.button.button) break;
            const Press pr = *press_;
            press_.reset();
            if (pr.moved) {
                line(std::format("drag {} to {}{}", pr.target, targetFor(ImVec2(e.button.x, e.button.y), probe), mods()), seconds);
                break;
            }
            const char* verb = pr.button == SDL_BUTTON_RIGHT ? "right-click" : pr.button == SDL_BUTTON_MIDDLE ? "middle-click" : "click";
            const float dx = pr.at.x - lastClick_.at.x, dy = pr.at.y - lastClick_.at.y;
            if (pr.button == SDL_BUTTON_LEFT && pr.target == lastClick_.target && seconds - lastClick_.seconds < kDoubleClickSeconds &&
                dx * dx + dy * dy < kDragDistance * kDragDistance && lastClick_.line + 1 == lines_.size()) {
                // The second click of a double click: one line for both.
                lines_.back() = std::format("double-click {}{}", pr.target, mods());
                lastClick_.seconds = -10;
                break;
            }
            line(std::format("{} {}{}", verb, pr.target, mods()), seconds);
            if (pr.button == SDL_BUTTON_LEFT) lastClick_ = LastClick{pr.target, pr.at, seconds, lines_.size() - 1};
            break;
        }
        case SDL_EVENT_MOUSE_WHEEL: {
            flushText(seconds);
            const int notches = static_cast<int>(std::lround(e.wheel.y));
            if (notches == 0) break;
            const std::string target = targetFor(ImVec2(e.wheel.mouse_x, e.wheel.mouse_y), probe);
            // Turns in a row over one place make one line.
            const std::string prefix = "wheel " + target + " ";
            if (!lines_.empty() && lines_.back().starts_with(prefix) && seconds - lastAction_ < 1.0) {
                const int before = std::atoi(lines_.back().c_str() + prefix.size());
                lines_.back() = prefix + std::to_string(before + notches);
                lastAction_ = seconds;
            } else {
                line(prefix + std::to_string(notches), seconds);
            }
            break;
        }
        default: break;
    }
}

void Recorder::frame(const Probe& probe, double seconds) {
    const auto lesson = probe.lesson();
    const std::string slug = lesson ? lesson->slug : std::string{};
    if (slug != lessonSlug_) {
        lessonSlug_ = slug;
        lessonStep_ = lesson ? lesson->active : 0;
        lessonResult_ = lesson ? lesson->result : std::string{};
        if (lesson) {
            flushText(seconds);
            progress("wait-lesson " + slug);
        }
        return;
    }
    if (!lesson) return;
    if (lesson->active != lessonStep_) {
        lessonStep_ = lesson->active;
        flushText(seconds);
        if (lesson->tutorial && lessonStep_ <= lesson->steps) progress(std::format("wait-step {}", lessonStep_));
    }
    if (lesson->result != lessonResult_) {
        lessonResult_ = lesson->result;
        flushText(seconds);
        progress("wait-result " + lessonResult_);
    }
    if (const auto t = probe.turn(); t != turn_) {
        if (turn_ && t) {
            flushText(seconds);
            progress(std::format("wait-turn {}", *t));
        }
        turn_ = t;
    }
}

bool Recorder::save(std::string& error) {
    flushText(lastAction_);
    for (std::string& l : deferred_) lines_.push_back(std::move(l));
    deferred_.clear();
    std::ofstream out(file_, std::ios::binary);
    if (!out) {
        error = std::format("cannot write {}", file_.string());
        return false;
    }
    out << "# Recorded with opense4 --record-input: check the targets and add waits and checks.\n";
    if (!options_.empty()) {
        out << "options";
        for (const std::string& o : options_) out << ' ' << quoteWord(o);
        out << '\n';
    }
    for (const std::string& l : lines_) out << l << '\n';
    return static_cast<bool>(out);
}

} // namespace opense4::client::script
