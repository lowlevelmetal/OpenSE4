#include "client/script/player.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <format>
#include <utility>

namespace opense4::client::script {

namespace {

// Frames after a step's last event before the next step looks at the
// screen: the window a click opens is drawn, and tagged, a frame later.
constexpr int kSettleFrames = 2;
// Two presses this close in time and place make a double click (Dear ImGui's
// default 0.3 s, at 60 frames a second): separate clicks wait longer.
constexpr uint64_t kDoubleClickFrames = 24;
constexpr float kDoubleClickDistance = 8.0f;

std::string lower(std::string_view s) {
    std::string out(s);
    for (char& c : out)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return out;
}

bool contains(const std::vector<std::string>& list, std::string_view s) { return std::find(list.begin(), list.end(), s) != list.end(); }

std::string joined(const std::vector<std::string>& list, size_t most = 30) {
    std::string out;
    for (size_t i = 0; i < list.size() && i < most; ++i) out += (i ? ", " : "") + list[i];
    if (list.size() > most) out += std::format(", ... ({} more)", list.size() - most);
    return out.empty() ? "none" : out;
}

// in=<scope>: the window that drew the widget (its id, main, lesson, front), a
// Dear ImGui window by name, or tag:<name>, a UI tag the widget lies in.
bool scopeMatches(const Item& item, std::string_view scope, const Probe& probe) {
    if (scope.empty()) return true;
    if (scope.starts_with("tag:")) {
        const ImVec2 c((item.min.x + item.max.x) * 0.5f, (item.min.y + item.max.y) * 0.5f);
        for (const Box& b : probe.tagBoxes(scope.substr(4)))
            if (c.x >= b.min.x && c.x < b.max.x && c.y >= b.min.y && c.y < b.max.y) return true;
        return false;
    }
    return item.scope == scope || labelMatches(item.window, scope);
}

void modifierEvents(std::vector<InputEvent>& frame, bool ctrl, bool shift, bool alt, bool down) {
    // Pressed in the order Ctrl, Shift, Alt and let go the other way round,
    // each event carrying the modifiers held after it.
    bool c = !down && ctrl, s = !down && shift, a = !down && alt;
    auto key = [&](ImGuiKey k) {
        InputEvent e;
        e.kind = down ? InputEvent::Kind::KeyDown : InputEvent::Kind::KeyUp;
        e.key = k;
        e.ctrl = c;
        e.shift = s;
        e.alt = a;
        frame.push_back(e);
    };
    struct Mod {
        bool wanted;
        bool* held;
        ImGuiKey key;
    };
    const Mod mods[] = {{ctrl, &c, ImGuiKey_LeftCtrl}, {shift, &s, ImGuiKey_LeftShift}, {alt, &a, ImGuiKey_LeftAlt}};
    for (size_t i = 0; i < 3; ++i) {
        const Mod& m = mods[down ? i : 2 - i];
        if (!m.wanted) continue;
        *m.held = down;
        key(m.key);
    }
}

// The UTF-8 characters of a text, one string each.
std::vector<std::string> characters(std::string_view text) {
    std::vector<std::string> out;
    for (size_t i = 0; i < text.size();) {
        const auto c = static_cast<unsigned char>(text[i]);
        const size_t n = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xe ? 3 : (c >> 3) == 0x1e ? 4 : 1;
        out.emplace_back(text.substr(i, n));
        i += n;
    }
    return out;
}

} // namespace

Player::Player(Script script, std::filesystem::path outputDir) : script_(std::move(script)), outputDir_(std::move(outputDir)) {}

std::filesystem::path Player::failureShot() const {
    return outputDir_ / (std::filesystem::path(script_.file).stem().string() + "-failure.png");
}

Player::Status Player::fail(std::string why) {
    why_ = std::move(why);
    return Status::Failed;
}

FrameOutput Player::tick(const Probe& probe) {
    FrameOutput out;
    ++frame_;
    if (finished_ || failed_) return out;
    // Steps that take no frame (checks, echoes, screenshots) run one after
    // another in the same frame; a step that sends input or waits ends it.
    for (size_t guard = 0; guard < 10000 && step_ < script_.steps.size(); ++guard) {
        const Step& st = script_.steps[step_];
        if (st.op == Op::Repeat || st.op == Op::End) {
            // Loops take no frame: they only move the script on.
            started_ = false;
            if (st.op == Op::End) {
                step_ = st.jump;
                continue;
            }
            const auto [it, first] = loops_.try_emplace(step_);
            if (first) it->second.since = probe.mark(false);
            bool leave = false;
            if (st.condition) {
                std::string error;
                const auto h = probe.holds(*st.condition, it->second.since, error);
                if (!h) {
                    why_ = error;
                    failed_ = true;
                } else {
                    leave = *h;
                }
            }
            if (!failed_ && !leave && it->second.passes >= st.number) {
                if (st.condition) {
                    why_ = std::format("the condition still does not hold after {} passes", st.number);
                    failed_ = true;
                } else {
                    leave = true;
                }
            }
            if (failed_) {
                failure_ = std::format("{}:{}: {}\n  {}\n  ({})", script_.file, st.line, st.source, why_, context(probe));
                out.messages.push_back("FAILED " + failure_);
                out.captures.push_back(failureShot());
                return out;
            }
            if (leave) {
                loops_.erase(it);
                step_ = st.jump + 1;
            } else {
                ++it->second.passes;
                out.messages.push_back(std::format("  {}:{}: {} (pass {})", std::filesystem::path(script_.file).filename().string(), st.line,
                                                   st.source, it->second.passes));
                ++step_;
            }
            continue;
        }
        if (!started_) {
            started_ = true;
            stepFrames_ = 0;
            queue_.clear();
            queued_ = false;
            settle_ = 0;
            decisive_.reset();
            awaitingVerdict_ = false;
            aimed_.reset();
            why_.clear();
            mark_ = probe.mark(false);
            out.messages.push_back(std::format("  {}:{}: {}", std::filesystem::path(script_.file).filename().string(), st.line, st.source));
        }
        const Status s = run(st, probe, out);
        if (s == Status::Failed) {
            failed_ = true;
            failure_ = std::format("{}:{}: {}\n  {}\n  ({})", script_.file, st.line, st.source, why_, context(probe));
            out.messages.push_back("FAILED " + failure_);
            out.captures.push_back(failureShot());
            return out;
        }
        if (s == Status::Running) {
            ++stepFrames_;
            return out;
        }
        ++step_;
        started_ = false;
    }
    if (step_ >= script_.steps.size()) finished_ = true;
    return out;
}

void Player::verdicts(std::span<const Verdict> v) {
    if (!awaitingVerdict_) return;
    for (size_t i = 0; i < v.size() && i < sentDecisive_.size(); ++i) {
        if (!sentDecisive_[i]) continue;
        // One refused press or key decides (a double click's two presses).
        if (!decisive_ || v[i] == Verdict::Drop) decisive_ = v[i];
    }
    sentDecisive_.clear();
    awaitingVerdict_ = false;
}

Player::Status Player::run(const Step& st, const Probe& probe, FrameOutput& out) {
    switch (st.op) {
        case Op::Click:
        case Op::DoubleClick:
        case Op::RightClick:
        case Op::MiddleClick:
        case Op::Drag:
        case Op::Move:
        case Op::Wheel: return pointerStep(st, probe, out);
        case Op::Key: return keyStep(st, out);
        case Op::Type: {
            if (!queued_ && !probe.typing()) {
                if (stepFrames_ >= st.timeout)
                    return fail(std::format("timed out after {} frames: no text field has the keyboard (click one first)", st.timeout));
                return Status::Running;
            }
            return typeStep(st, out);
        }
        case Op::Wait:
        case Op::WaitFor:
        case Op::WaitGone:
        case Op::WaitWindow:
        case Op::WaitClosed:
        case Op::WaitStep:
        case Op::WaitUntil:
        case Op::WaitTurn:
        case Op::WaitResult:
        case Op::WaitScreen:
        case Op::WaitLesson: return waitStep(st, probe);
        case Op::Screenshot: {
            const std::filesystem::path p(st.text);
            out.captures.push_back(p.is_absolute() ? p : outputDir_ / p);
            return Status::Done;
        }
        case Op::Echo: out.messages.push_back(st.text); return Status::Done;
        case Op::Dump: {
            // What a script can name now: the UI tags, and the widgets (of one scope) in their order.
            out.messages.push_back("tags: " + joined(probe.tagNames(), 500));
            if (const std::string lock = probe.lockDescription(); !lock.empty()) out.messages.push_back(lock);
            std::map<std::string, int> seen;
            std::string list;
            for (const Item& item : probe.items()) {
                if (!st.text.empty() && !scopeMatches(item, st.text, probe)) continue;
                const int n = ++seen[item.scope + "\n" + item.label];
                // Where it is, in frame pixels.
                const ImVec2 o = probe.framePoint(0, 0);
                const float k = std::max(0.01f, probe.frameScale());
                list += std::format("\n    item:{}{}{}{}  [{:.0f},{:.0f} {:.0f}x{:.0f}]", quoteWord(item.label),
                                    item.scope.empty() ? "" : " in=" + quoteWord(item.scope), n > 1 ? std::format(" nth={}", n) : "",
                                    item.disabled ? " (dim)" : "", (item.min.x - o.x) / k, (item.min.y - o.y) / k, (item.max.x - item.min.x) / k,
                                    (item.max.y - item.min.y) / k);
            }
            out.messages.push_back("items:" + list);
            return Status::Done;
        }
        case Op::Audit: {
            const std::vector<std::string> lines = probe.lessonAudit();
            if (lines.empty()) out.messages.push_back("audit: no tutorial step is active");
            for (const std::string& l : lines) out.messages.push_back(l);
            return Status::Done;
        }
        case Op::Print: {
            std::string text;
            for (const std::string& key : st.facts) {
                const learn::FactInfo* f = learn::findFact(key);
                const auto v = f ? probe.factValue(f->fact, probe.mark(true)) : std::nullopt;
                text += std::format("{}{} = {}", text.empty() ? "" : ", ", key, v ? std::to_string(*v) : std::string("(no game)"));
            }
            out.messages.push_back(text);
            return Status::Done;
        }
        default: return check(st, probe);
    }
}

// Sends the queued frames one per tick, checks the decisive verdict, then
// lets the screen settle.
Player::Status Player::playQueue(const Step& st, FrameOutput& out) {
    if (decisive_) {
        const Verdict v = *decisive_;
        decisive_.reset();
        if (st.refused && v == Verdict::Pass) return fail("the tutorial input lock let this through; the script expected it to be refused");
        if (!st.refused && v == Verdict::Drop) return fail("the tutorial input lock refused this: the active lesson step does not allow it");
    }
    if (!queue_.empty()) {
        out.events = std::move(queue_.front());
        queue_.pop_front();
        sentDecisive_.clear();
        for (const InputEvent& e : out.events) {
            sentDecisive_.push_back(e.decisive);
            if (e.kind == InputEvent::Kind::ButtonDown) {
                lastPressFrame_ = frame_;
                lastPressPos_ = e.pos;
            }
        }
        awaitingVerdict_ = std::any_of(out.events.begin(), out.events.end(), [](const InputEvent& e) { return e.decisive; });
        return Status::Running;
    }
    if (settle_ < kSettleFrames) {
        ++settle_;
        return Status::Running;
    }
    return Status::Done;
}

Player::Status Player::pointerStep(const Step& st, const Probe& probe, FrameOutput& out) {
    if (!queued_) {
        std::string why;
        auto at = resolve(st.target, probe, why);
        std::optional<Resolved> to;
        if (at && st.op == Op::Drag) {
            to = resolve(st.to, probe, why);
            if (!to) at.reset();
        }
        if (at && at->disabled && !st.refused && st.op != Op::Move) {
            at.reset();
            why = std::format("{} is dim (disabled)", st.target.text);
        }
        if (!at) {
            if (stepFrames_ >= st.timeout && st.optional) {
                out.messages.push_back(std::format("  (skipped: {})", why));
                return Status::Done;
            }
            if (stepFrames_ >= st.timeout) return fail(std::format("timed out after {} frames: {}", st.timeout, why));
            why_ = why;
            return Status::Running;
        }
        const ImVec2 p = at->point;
        const int button = st.op == Op::RightClick ? 3 : st.op == Op::MiddleClick ? 2 : st.op == Op::Drag && st.button ? st.button : 1;
        auto event = [&](InputEvent::Kind kind, ImVec2 pos, bool decisive = false) {
            InputEvent e;
            e.kind = kind;
            e.pos = pos;
            e.button = button;
            e.decisive = decisive;
            return e;
        };
        const bool mods = st.ctrl || st.shift || st.alt;
        std::vector<InputEvent> first;
        modifierEvents(first, st.ctrl, st.shift, st.alt, true);
        first.push_back(event(InputEvent::Kind::Motion, p));
        queue_.push_back(std::move(first));
        // A press right after another one at the same place would make a double click.
        const bool presses = st.op != Op::Move && st.op != Op::Wheel;
        if (presses && lastPressFrame_ > 0) {
            const float dx = p.x - lastPressPos_.x, dy = p.y - lastPressPos_.y;
            if (dx * dx + dy * dy < kDoubleClickDistance * kDoubleClickDistance)
                for (uint64_t f = frame_ + 1; f < lastPressFrame_ + kDoubleClickFrames; ++f) queue_.emplace_back();
        }
        switch (st.op) {
            case Op::Click:
            case Op::RightClick:
            case Op::MiddleClick:
                queue_.push_back({event(InputEvent::Kind::ButtonDown, p, true)});
                queue_.push_back({event(InputEvent::Kind::ButtonUp, p)});
                break;
            case Op::DoubleClick:
                queue_.push_back({event(InputEvent::Kind::ButtonDown, p, true)});
                queue_.push_back({event(InputEvent::Kind::ButtonUp, p)});
                queue_.push_back({event(InputEvent::Kind::ButtonDown, p, true)});
                queue_.push_back({event(InputEvent::Kind::ButtonUp, p)});
                break;
            case Op::Drag: {
                queue_.push_back({event(InputEvent::Kind::ButtonDown, p, true)});
                const ImVec2 q = to->point;
                const int n = std::max(1, st.dragFrames);
                for (int i = 1; i <= n; ++i) {
                    const float f = float(i) / float(n);
                    queue_.push_back({event(InputEvent::Kind::Motion, ImVec2(p.x + (q.x - p.x) * f, p.y + (q.y - p.y) * f))});
                }
                queue_.push_back({event(InputEvent::Kind::ButtonUp, q)});
                break;
            }
            case Op::Wheel: {
                // One notch a frame, as a wheel turns.
                for (int64_t i = 0; i < (st.number < 0 ? -st.number : st.number); ++i) {
                    InputEvent w = event(InputEvent::Kind::Wheel, p, i == 0);
                    w.wheel = st.number < 0 ? -1.0f : 1.0f;
                    queue_.push_back({w});
                }
                break;
            }
            default: break;   // Move: the motion is all
        }
        if (mods) {
            std::vector<InputEvent> last;
            modifierEvents(last, st.ctrl, st.shift, st.alt, false);
            queue_.push_back(std::move(last));
        }
        queued_ = true;
        aimed_ = p;
        reaims_ = 0;
    }
    // Just before the press: if the target has moved since (a window still
    // settling its layout), the pointer goes there first.
    const bool pressNext = !queue_.empty() && std::any_of(queue_.front().begin(), queue_.front().end(), [](const InputEvent& e) {
        return e.kind == InputEvent::Kind::ButtonDown || e.kind == InputEvent::Kind::Wheel;
    });
    if (pressNext && aimed_ && reaims_ < 10) {
        std::string why;
        if (const auto now = resolve(st.target, probe, why);
            now && (std::abs(now->point.x - aimed_->x) > 0.5f || std::abs(now->point.y - aimed_->y) > 0.5f)) {
            const ImVec2 from = *aimed_;
            for (std::vector<InputEvent>& frame : queue_)
                for (InputEvent& e : frame)
                    if (e.kind != InputEvent::Kind::KeyDown && e.kind != InputEvent::Kind::KeyUp && e.pos.x == from.x && e.pos.y == from.y) e.pos = now->point;
            InputEvent move;
            move.kind = InputEvent::Kind::Motion;
            move.pos = now->point;
            queue_.push_front({move});
            aimed_ = now->point;
            ++reaims_;
        }
    }
    return playQueue(st, out);
}

Player::Status Player::keyStep(const Step& st, FrameOutput& out) {
    if (!queued_) {
        const KeyChord& c = st.chord;
        const bool mods = c.ctrl || c.shift || c.alt;
        if (mods) {
            std::vector<InputEvent> down;
            modifierEvents(down, c.ctrl, c.shift, c.alt, true);
            queue_.push_back(std::move(down));
        }
        for (const bool press : {true, false}) {
            InputEvent e;
            e.kind = press ? InputEvent::Kind::KeyDown : InputEvent::Kind::KeyUp;
            e.key = c.key;
            e.ctrl = c.ctrl;
            e.shift = c.shift;
            e.alt = c.alt;
            e.decisive = press;
            queue_.push_back({e});
        }
        if (mods) {
            std::vector<InputEvent> up;
            modifierEvents(up, c.ctrl, c.shift, c.alt, false);
            queue_.push_back(std::move(up));
        }
        queued_ = true;
    }
    return playQueue(st, out);
}

Player::Status Player::typeStep(const Step& st, FrameOutput& out) {
    if (!queued_) {
        bool first = true;
        for (std::string& ch : characters(st.text)) {
            InputEvent e;
            e.kind = InputEvent::Kind::Text;
            e.text = std::move(ch);
            e.decisive = first;
            first = false;
            queue_.push_back({std::move(e)});
        }
        queued_ = true;
    }
    return playQueue(st, out);
}

Player::Status Player::waitStep(const Step& st, const Probe& probe) {
    bool ok = false;
    std::string why;
    const std::optional<LessonInfo> lesson = probe.lesson();
    switch (st.op) {
        case Op::Wait: ok = stepFrames_ >= st.number; break;
        case Op::WaitFor: ok = resolve(st.target, probe, why).has_value(); break;
        case Op::WaitGone:
            ok = !resolve(st.target, probe, why).has_value();
            why = std::format("{} is still there", st.target.text);
            break;
        case Op::WaitWindow:
            ok = contains(probe.openWindows(), st.text);
            why = std::format("the window '{}' is not open (open: {})", st.text, joined(probe.openWindows()));
            break;
        case Op::WaitClosed:
            ok = !contains(probe.openWindows(), st.text);
            why = std::format("the window '{}' is still open", st.text);
            break;
        case Op::WaitStep:
            ok = lesson && lesson->active == size_t(st.number);
            why = lesson ? std::format("the lesson is at step {}", lesson->active) : "no lesson is running";
            break;
        case Op::WaitUntil: {
            std::string error;
            const auto h = probe.holds(*st.condition, mark_, error);
            if (!h) return fail(error);
            ok = *h;
            why = "the condition does not hold";
            break;
        }
        case Op::WaitTurn: {
            const auto t = probe.turn();
            ok = t && *t >= uint64_t(st.number);
            why = t ? std::format("the game is at turn {}", *t) : "no game is running";
            break;
        }
        case Op::WaitResult:
            ok = lesson && lesson->result == st.text;
            why = lesson ? std::format("the lesson's result is {}", lesson->result) : "no lesson is running";
            break;
        case Op::WaitScreen:
            ok = probe.screen() == st.text;
            why = std::format("the screen is {}", probe.screen());
            break;
        case Op::WaitLesson:
            ok = st.text == "none" ? !lesson : lesson && lesson->slug == st.text;
            why = lesson ? std::format("the lesson '{}' is running", lesson->slug) : "no lesson is running";
            break;
        default: break;
    }
    if (ok) return Status::Done;
    if (stepFrames_ >= st.timeout) return fail(std::format("timed out after {} frames: {}", st.timeout, why.empty() ? "still waiting" : why));
    return Status::Running;
}

Player::Status Player::check(const Step& st, const Probe& probe) {
    std::string why;
    const std::optional<LessonInfo> lesson = probe.lesson();
    switch (st.op) {
        case Op::AssertPresent:
            if (resolve(st.target, probe, why)) return Status::Done;
            return fail(why);
        case Op::AssertAbsent:
            if (!resolve(st.target, probe, why)) return Status::Done;
            return fail(std::format("{} is on screen", st.target.text));
        case Op::AssertEnabled:
        case Op::AssertDisabled: {
            const auto r = resolve(st.target, probe, why);
            if (!r) return fail(why);
            const bool want = st.op == Op::AssertDisabled;
            if (r->disabled == want) return Status::Done;
            return fail(std::format("{} is {}", st.target.text, r->disabled ? "dim (disabled)" : "enabled"));
        }
        case Op::AssertWindow:
            if (contains(probe.openWindows(), st.text)) return Status::Done;
            return fail(std::format("the window '{}' is not open (open: {})", st.text, joined(probe.openWindows())));
        case Op::AssertNoWindow:
            if (!contains(probe.openWindows(), st.text)) return Status::Done;
            return fail(std::format("the window '{}' is open", st.text));
        case Op::AssertStep:
            if (lesson && lesson->active == size_t(st.number)) return Status::Done;
            return fail(lesson ? std::format("the lesson is at step {}, not {}", lesson->active, st.number) : "no lesson is running");
        case Op::Assert: {
            std::string error;
            const auto h = probe.holds(*st.condition, probe.mark(true), error);
            if (!h) return fail(error);
            if (*h) return Status::Done;
            return fail("the condition does not hold");
        }
        case Op::AssertLog:
        case Op::AssertNoLog: {
            const std::string want = lower(st.text);
            const std::vector<std::string> lines = probe.logLines();
            const auto hit = std::find_if(lines.begin(), lines.end(), [&](const std::string& l) { return lower(l).find(want) != std::string::npos; });
            if ((hit != lines.end()) == (st.op == Op::AssertLog)) return Status::Done;
            if (st.op == Op::AssertNoLog) return fail(std::format("the log has \"{}\"", *hit));
            return fail(std::format("no log entry has \"{}\" ({} entries{})", st.text, lines.size(),
                                    lines.empty() ? "" : std::format("; the last: \"{}\"", lines.back())));
        }
        case Op::AssertResult:
            if (lesson && lesson->result == st.text) return Status::Done;
            return fail(lesson ? std::format("the lesson's result is {}", lesson->result) : "no lesson is running");
        case Op::AssertScreen:
            if (probe.screen() == st.text) return Status::Done;
            return fail(std::format("the screen is {}", probe.screen()));
        case Op::AssertLesson:
            if (st.text == "none" ? !lesson : lesson && lesson->slug == st.text) return Status::Done;
            return fail(lesson ? std::format("the lesson '{}' is running", lesson->slug) : "no lesson is running");
        case Op::AssertFits: {
            // Every text drawn into a box of its own fits it, and no widget the
            // scope draws is cut off by its window (but in a window that scrolls).
            std::vector<std::string> faults;
            for (const Item& text : probe.texts())
                if (text.overflow && scopeMatches(text, st.text, probe)) faults.push_back(std::format("text {} runs out of its box", quoteWord(text.label)));
            // A selectable row reaches half the item spacing past its window's edge: a few pixels are no fault.
            const float slack = 4.0f * std::max(0.01f, probe.frameScale());
            for (const Item& item : probe.items())
                if (item.clipped && item.hidden > slack && !item.scrolls && !visibleLabel(item.label).empty() && !item.label.starts_with("window:") &&
                    scopeMatches(item, st.text, probe))
                    faults.push_back(std::format("item:{} is cut off by its window", quoteWord(item.label)));
            if (faults.empty()) return Status::Done;
            std::string list;
            for (size_t i = 0; i < faults.size() && i < 8; ++i) list += (i ? "; " : "") + faults[i];
            if (faults.size() > 8) list += std::format("; and {} more", faults.size() - 8);
            return fail(list);
        }
        case Op::AssertInside: {
            const auto a = resolve(st.target, probe, why);
            if (!a) return fail(why);
            Box box;
            if (!resolveBox(st.to, probe, box, why)) return fail(why);
            if (a->point.x >= box.min.x && a->point.x < box.max.x && a->point.y >= box.min.y && a->point.y < box.max.y) return Status::Done;
            return fail(std::format("{} is not inside {}", st.target.text, st.to.text));
        }
        case Op::AssertTurn: {
            const auto t = probe.turn();
            if (t && *t == uint64_t(st.number)) return Status::Done;
            return fail(t ? std::format("the game is at turn {}", *t) : "no game is running");
        }
        default: return Status::Done;
    }
}

bool Player::resolveBox(const Target& t, const Probe& probe, Box& box, std::string& why) const {
    if (t.kind == TargetKind::At) {
        const ImVec2 p = probe.framePoint(t.x, t.y);
        box = Box{p, ImVec2(p.x + 1, p.y + 1)};
        return true;
    }
    Target whole = t;
    whole.offset = {};
    const auto r = resolve(whole, probe, why, &box);
    return r.has_value();
}

std::optional<Player::Resolved> Player::resolve(const Target& t, const Probe& probe, std::string& why, Box* boxOut) const {
    std::vector<Box> boxes;
    std::vector<bool> dim;
    switch (t.kind) {
        case TargetKind::At: return Resolved{probe.framePoint(t.x, t.y), false};
        case TargetKind::Tag:
        case TargetKind::Window: {
            const std::string name = t.kind == TargetKind::Window ? "window:" + t.name : t.name;
            for (const Box& b : probe.tagBoxes(name))
                if (b.valid()) boxes.push_back(b);
            if (boxes.empty()) {
                // Names that look alike, for the message.
                std::vector<std::string> alike;
                const std::string head = name.substr(0, name.find(':'));
                for (const std::string& n : probe.tagNames())
                    if ((n.starts_with(head + ":") || n.find(t.name) != std::string::npos) && !contains(alike, n)) alike.push_back(n);
                why = std::format("no UI tag '{}' on screen (tags like it: {})", name, joined(alike, 20));
                return std::nullopt;
            }
            break;
        }
        case TargetKind::Item: {
            std::vector<std::string> labels;
            for (const Item& item : probe.items()) {
                if (!scopeMatches(item, t.scope, probe)) continue;
                if (labelMatches(item.label, t.name)) {
                    boxes.push_back({item.min, item.max});
                    dim.push_back(item.disabled);
                } else if (const std::string_view shown = visibleLabel(item.label); !shown.empty() && !contains(labels, shown)) {
                    labels.emplace_back(shown);
                }
            }
            if (boxes.empty()) {
                why = std::format("no item '{}'{} on screen (labels{}: {})", t.name, t.scope.empty() ? "" : " in " + t.scope,
                                  t.scope.empty() ? "" : " there", joined(labels, 40));
                return std::nullopt;
            }
            break;
        }
        case TargetKind::Sector:
            boxes = probe.sectors(t, why);
            if (boxes.empty()) {
                if (why.empty()) why = std::format("no sector of the system view matches '{}'", t.name);
                return std::nullopt;
            }
            break;
        case TargetKind::System:
            boxes = probe.systems(t, why);
            if (boxes.empty()) {
                if (why.empty()) why = std::format("no system of the galaxy panel matches '{}'", t.name);
                return std::nullopt;
            }
            break;
    }
    if (size_t(t.nth) > boxes.size()) {
        why = std::format("{} matches only {} time(s), not {}", t.text, boxes.size(), t.nth);
        return std::nullopt;
    }
    const size_t i = size_t(t.nth) - 1;
    const Box& b = boxes[i];
    if (boxOut) *boxOut = b;
    Resolved r;
    r.disabled = i < dim.size() && dim[i];
    r.point = b.center();
    if (t.offset.set) {
        const float k = probe.frameScale();
        auto along = [k](float lo, float hi, float v, bool percent) {
            if (percent) return lo + (hi - lo) * v / 100.0f;
            return v >= 0 ? lo + v * k : hi + v * k;
        };
        r.point = ImVec2(along(b.min.x, b.max.x, t.offset.x, t.offset.xPercent), along(b.min.y, b.max.y, t.offset.y, t.offset.yPercent));
    }
    return r;
}

std::string Player::context(const Probe& probe) const {
    std::string out = std::format("frame {}, screen {}", frame_, probe.screen());
    if (const auto l = probe.lesson())
        out += std::format("; {} '{}' at step {} of {}{}, result {}", l->tutorial ? "tutorial" : "training game", l->slug, l->active, l->steps,
                           l->locked ? " (input locked)" : "", l->result);
    if (const auto t = probe.turn()) out += std::format("; turn {}", *t);
    out += "; windows: " + joined(probe.openWindows());
    return out;
}

} // namespace opense4::client::script
