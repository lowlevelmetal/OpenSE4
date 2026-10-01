// The Learn window and the manual viewer (docs/LEARNING.md), in a game
// (ScreenId::Learn, ScreenId::Manual) and in the front end (FrontId::Learn).

#include "client/classic/screens/learn_screens.hpp"

#include "client/audio.hpp"
#include "client/classic/frontend.hpp"
#include "client/classic/screens/screens.hpp"
#include "client/classic/widgets.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <format>

namespace opense4::client::classic {

namespace {

const ImVec4 kDone{0.45f, 0.9f, 0.45f, 1.0f};

void dim(std::string_view text) {
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(imColorV(palette::kSecondary), "%.*s", int(text.size()), text.data());
    ImGui::PopTextWrapPos();
}

} // namespace

// ---- Learn ---------------------------------------------------------------------------------------

LearnView::Tab LearnView::tabFromName(std::string_view name) {
    if (name == "training") return Tab::Training;
    if (name == "manual") return Tab::Manual;
    return Tab::Tutorials;
}

bool LearnView::draw(const Painter& p, Dialog& d, LearnHost& host) {
    if (!d.open()) return d.keepOpen();
    const learn::Library& lib = host.content->library;
    d.beginContent();
    switch (tab_) {
        case Tab::Tutorials: lessons(p, host, learn::LessonKind::Tutorial); break;
        case Tab::Training: lessons(p, host, learn::LessonKind::Training); break;
        case Tab::Manual: contents(p, host); break;
    }

    d.beginButtons();
    if (d.tab("Tutorials", tab_ == Tab::Tutorials)) tab_ = Tab::Tutorials;
    if (d.tab("Training", tab_ == Tab::Training)) tab_ = Tab::Training;
    if (d.tab("Manual", tab_ == Tab::Manual)) tab_ = Tab::Manual;
    d.spacer();
    const std::string& sel = selected_[static_cast<size_t>(tab_)];
    if (tab_ == Tab::Manual) {
        if (d.button("Read", !lib.manual.empty())) host.openManual(sel);
    } else {
        const learn::LessonKind kind = tab_ == Tab::Tutorials ? learn::LessonKind::Tutorial : learn::LessonKind::Training;
        const learn::Lesson* l = lib.lesson(kind, sel);
        if (d.button(tab_ == Tab::Tutorials ? "Start Lesson" : "Start Game", l != nullptr)) host.start(kind, l->slug);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", host.inGame ? "Ends the current game and starts this one" : "Starts this lesson's game");
    }
    if (!host.content->originalManual.empty()) {
        if (d.button("Original Manual")) openOriginalManual(*host.content);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Opens the game's own HTML manual in the browser");
    }
    d.close();
    return d.keepOpen();
}

void LearnView::lessons(const Painter& p, LearnHost& host, learn::LessonKind kind) {
    const learn::Library& lib = host.content->library;
    const auto& list = lib.lessons(kind);
    std::string& sel = selected_[kind == learn::LessonKind::Tutorial ? 0 : 1];
    if (list.empty()) {
        heading(p, kind == learn::LessonKind::Tutorial ? "Tutorials" : "Training");
        dim(kind == learn::LessonKind::Tutorial ? "No tutorials are installed yet." : "No training games are installed yet.");
        return;
    }
    if (!lib.lesson(kind, sel)) sel = list.front().slug;

    // The list on the left, the chosen lesson on the right.
    ImGui::BeginChild("##lessons", ImVec2(p.px(250), 0), ImGuiChildFlags_Borders);
    for (const learn::Lesson& l : list) {
        ImGui::PushID(l.slug.c_str());
        const bool done = lessonDone(kind, l.slug);
        if (ImGui::Selectable("##row", sel == l.slug, ImGuiSelectableFlags_AllowDoubleClick, ImVec2(0, p.px(34)))) {
            sel = l.slug;
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) host.start(kind, l.slug);
        }
        const ImVec2 min = ImGui::GetItemRectMin();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddText(ImVec2(min.x + p.px(4), min.y + p.px(2)), IM_COL32_WHITE, l.title.c_str());
        const std::string sub = l.minutes > 0 ? std::format("About {} minutes", l.minutes) : std::string{};
        dl->AddText(ImVec2(min.x + p.px(4), min.y + p.px(18)), imColor(palette::kSecondary), sub.c_str());
        if (done) {
            const char* mark = "Done";
            const float w = ImGui::CalcTextSize(mark).x;
            dl->AddText(ImVec2(ImGui::GetItemRectMax().x - w - p.px(6), min.y + p.px(2)), ImGui::GetColorU32(kDone), mark);
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##lesson", ImVec2(0, 0), ImGuiChildFlags_Borders);
    if (const learn::Lesson* l = lib.lesson(kind, sel)) {
        heading(p, l->title.c_str());
        if (l->origin != "built in" && ImGui::IsItemHovered()) ImGui::SetTooltip("From %s", l->origin.c_str());
        if (lessonDone(kind, l->slug)) ImGui::TextColored(kDone, "%s", kind == learn::LessonKind::Tutorial ? "Completed" : "Won");
        ImGui::Spacing();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(l->summary.c_str());
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        if (l->minutes > 0) ImGui::TextColored(kLabelBlue, "About %d minutes", l->minutes);
        if (kind == learn::LessonKind::Tutorial) {
            ImGui::TextColored(kLabelBlue, "%zu steps", l->steps.size());
            ImGui::Spacing();
            for (size_t i = 0; i < l->steps.size(); ++i) ImGui::Text("%zu. %s", i + 1, l->steps[i].title.c_str());
        } else {
            ImGui::TextColored(kLabelBlue, "Objectives");
            for (const learn::Objective& o : l->objectives) {
                ImGui::Bullet();
                ImGui::TextUnformatted(o.byTurn ? std::format("{} (by {})", o.text, formatDate(*o.byTurn)).c_str() : o.text.c_str());
            }
        }
    }
    ImGui::EndChild();
}

void LearnView::contents(const Painter& p, LearnHost& host) {
    const learn::Library& lib = host.content->library;
    std::string& sel = selected_[2];
    if (lib.manual.empty()) {
        heading(p, "Manual");
        dim("No manual pages are installed yet.");
        if (!host.content->originalManual.empty()) dim("The game's own manual opens in the browser with Original Manual.");
        return;
    }
    heading(p, "Manual");
    dim("Click a chapter or section to read it.");
    ImGui::Spacing();
    ImGui::BeginChild("##chapters", ImVec2(0, 0), ImGuiChildFlags_Borders);
    for (const learn::ManualPage& page : lib.manual) {
        ImGui::PushID(page.slug.c_str());
        if (ImGui::Selectable(page.doc.title.c_str(), sel == page.slug)) {
            sel = page.slug;
            host.openManual(page.slug);
        }
        for (const learn::Section& s : page.sections) {
            if (s.level > 2) continue;
            const std::string target = page.slug + "#" + s.anchor;
            ImGui::Indent(p.px(18));
            ImGui::PushStyleColor(ImGuiCol_Text, imColorV(palette::kSecondary));
            if (ImGui::Selectable(std::format("{}##{}", s.title, s.anchor).c_str(), sel == target)) {
                sel = target;
                host.openManual(target);
            }
            ImGui::PopStyleColor();
            ImGui::Unindent(p.px(18));
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
}

// ---- Manual --------------------------------------------------------------------------------------

void ManualView::go(const std::string& target) {
    if (!history_.empty() && history_[at_] == target) {
        // The same place again: scroll there again.
        const learn::Link l = learn::parseLink(target);
        options_.scrollTo = l.anchor;
        toTop_ = l.anchor.empty();
        return;
    }
    if (!history_.empty()) history_.resize(at_ + 1);
    history_.push_back(target);
    at_ = history_.size() - 1;
    const learn::Link l = learn::parseLink(target);
    options_.scrollTo = l.anchor;
    toTop_ = l.anchor.empty();
}

void ManualView::follow(const std::string& target, LearnHost& host) {
    const learn::Link l = learn::parseLink(target);
    switch (l.kind) {
        case learn::Link::Kind::Page: go(l.target.empty() ? page_ + "#" + l.anchor : target); break;
        case learn::Link::Kind::Window:
        case learn::Link::Kind::Help:
            if (host.follow) host.follow(l);
            break;
        case learn::Link::Kind::External: SDL_OpenURL(l.target.c_str()); break;
        case learn::Link::Kind::Invalid: break;
    }
    audio().play("button");
}

void ManualView::contents(const Painter& p, const learn::Library& lib) {
    ImGui::SetNextItemWidth(-FLT_MIN);
    inputString("##search", query_, 80);
    if (query_.empty() && !ImGui::IsItemActive()) {
        const ImVec2 at = ImGui::GetItemRectMin();
        ImGui::GetWindowDrawList()->AddText(ImVec2(at.x + p.px(4), at.y + p.px(2)), imColor(palette::kDim), "Search");
    }
    ImGui::BeginChild("##toc", ImVec2(0, 0), ImGuiChildFlags_Borders);
    if (!query_.empty()) {
        const auto hits = lib.search(query_);
        if (hits.empty()) ImGui::TextColored(imColorV(palette::kSecondary), "Nothing found.");
        for (size_t i = 0; i < hits.size(); ++i) {
            const auto& h = hits[i];
            ImGui::PushID(int(i));
            const std::string label = h.anchor.empty() ? h.page->doc.title : std::format("{}: {}", h.page->doc.title, h.section);
            if (ImGui::Selectable(label.c_str(), false)) go(h.anchor.empty() ? h.page->slug : h.page->slug + "#" + h.anchor);
            ImGui::PushTextWrapPos(0.0f);
            ImGui::PushFont(p.fonts.small, p.fontPx(kSmallSize));
            ImGui::TextColored(imColorV(palette::kSecondary), "%s", h.excerpt.c_str());
            ImGui::PopFont();
            ImGui::PopTextWrapPos();
            ImGui::Spacing();
            ImGui::PopID();
        }
    } else {
        for (const learn::ManualPage& page : lib.manual) {
            ImGui::PushID(page.slug.c_str());
            const bool current = page.slug == page_;
            ImGui::SetNextItemOpen(current, current ? ImGuiCond_Always : ImGuiCond_Appearing);
            const ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth |
                                             (current ? ImGuiTreeNodeFlags_Selected : 0) | (page.sections.empty() ? ImGuiTreeNodeFlags_Leaf : 0);
            const bool open = ImGui::TreeNodeEx("##page", flags, "%s", page.doc.title.c_str());
            if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) go(page.slug);
            if (open) {
                for (const learn::Section& s : page.sections) {
                    if (s.level > 2) continue;
                    ImGui::PushStyleColor(ImGuiCol_Text, imColorV(palette::kSecondary));
                    if (ImGui::Selectable(std::format("{}##{}", s.title, s.anchor).c_str(), false)) go(page.slug + "#" + s.anchor);
                    ImGui::PopStyleColor();
                }
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
}

bool ManualView::draw(const Painter& p, Dialog& d, LearnHost& host) {
    if (!d.open()) return d.keepOpen();
    const learn::Library& lib = host.content->library;
    // The page shown: the history entry's, or the first page.
    const learn::Link where = learn::parseLink(history_.empty() ? std::string{} : history_[at_]);
    const learn::ManualPage* page = lib.page(where.target);
    if (!page && !lib.manual.empty()) page = &lib.manual.front();
    page_ = page ? page->slug : std::string{};

    d.beginContent();
    if (!page) {
        heading(p, "Manual");
        dim("No manual pages are installed yet.");
    } else {
        ImGui::BeginChild("##contents", ImVec2(p.px(230), 0), ImGuiChildFlags_None);
        contents(p, lib);
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, p.size({12, 8}));
        ImGui::BeginChild("##page", ImVec2(0, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
        ImGui::PopStyleVar();
        if (toTop_) {
            ImGui::SetScrollY(0.0f);
            toTop_ = false;
        }
        options_.canFollow = [&host](const learn::Link& l) {
            return host.inGame || (l.kind != learn::Link::Kind::Window && l.kind != learn::Link::Kind::Help);
        };
        if (auto clicked = drawMarkdown(p, page->doc.blocks, options_)) follow(*clicked, host);
        if (!options_.scrollTo.empty()) {
            // No such section on the page: its top.
            options_.scrollTo.clear();
            ImGui::SetScrollY(0.0f);
        }
        ImGui::EndChild();
    }

    d.beginButtons();
    if (d.button("Back", at_ > 0)) {
        --at_;
        const learn::Link l = learn::parseLink(history_[at_]);
        options_.scrollTo = l.anchor;
        toTop_ = l.anchor.empty();
    }
    if (d.button("Forward", at_ + 1 < history_.size())) {
        ++at_;
        const learn::Link l = learn::parseLink(history_[at_]);
        options_.scrollTo = l.anchor;
        toTop_ = l.anchor.empty();
    }
    if (d.button("Contents", !lib.manual.empty())) {
        query_.clear();
        go(lib.manual.front().slug);
    }
    if (!host.content->originalManual.empty()) {
        if (d.button("Original Manual")) openOriginalManual(*host.content);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Opens the game's own HTML manual in the browser");
    }
    d.close();
    // Alt+Left and Alt+Right go back and forward, as in a browser.
    const ImGuiIO& io = ImGui::GetIO();
    if (io.KeyAlt && !io.WantTextInput && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, false) && at_ > 0) {
            --at_;
            options_.scrollTo = learn::parseLink(history_[at_]).anchor;
            toTop_ = options_.scrollTo.empty();
        }
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, false) && at_ + 1 < history_.size()) {
            ++at_;
            options_.scrollTo = learn::parseLink(history_[at_]).anchor;
            toTop_ = options_.scrollTo.empty();
        }
    }
    return d.keepOpen();
}

// ---- In a game -----------------------------------------------------------------------------------

namespace {

LearnHost gameHost(UiContext& ui) {
    LearnHost h;
    h.content = ui.learn;
    h.inGame = true;
    h.openManual = [&ui](const std::string& target) {
        ScreenArgs a;
        a.text = target;
        ui.open(ScreenId::Manual, a);
    };
    h.follow = [&ui](const learn::Link& l) {
        if (l.kind == learn::Link::Kind::Window) {
            if (const auto id = screenFromWindowId(l.target)) ui.open(*id);
        } else if (l.kind == learn::Link::Kind::Help) {
            ScreenArgs a;
            a.text = l.target;
            ui.open(ScreenId::Help, a);
        }
    };
    return h;
}

class LearnScreen final : public Screen {
public:
    explicit LearnScreen(const ScreenArgs& args) : view_(LearnView::tabFromName(args.text)) {}

    bool draw(UiContext& ui) override {
        if (!ui.learn) return false;
        LearnHost host = gameHost(ui);
        host.start = [this](learn::LessonKind kind, const std::string& slug) {
            pending_ = {kind, slug};
            confirm_.open("Leave this game and start the lesson's game? Anything not saved is lost.", "Start Lesson");
        };
        bool keep = true;
        {
            Dialog d(ui, "Learn", DialogSize::Large);
            keep = view_.draw(ui.painter(), d, host);
            // A Yes/No message box: Y means Yes; N, Esc and Enter mean No (spec 06 §3.4).
            if (confirm_.draw(ui)) ui.requests.startLesson = pending_;
        }
        return keep;
    }

private:
    LearnView view_;
    std::pair<learn::LessonKind, std::string> pending_;
    YesNoPrompt confirm_;
};

class ManualScreen final : public Screen {
public:
    explicit ManualScreen(const ScreenArgs& args) : view_(args.text) {}

    bool draw(UiContext& ui) override {
        if (!ui.learn) return false;
        LearnHost host = gameHost(ui);
        // Links to other pages stay in this window.
        host.openManual = [this](const std::string& target) { view_.go(target); };
        Dialog d(ui, "Manual", DialogSize::Full);
        return view_.draw(ui.painter(), d, host);
    }

private:
    ManualView view_;
};

// ---- In the front end ----------------------------------------------------------------------------

class LearnFrontScreen final : public FrontScreen {
public:
    // `start`: a Learn tab name, or "manual:<slug#anchor>" to open the manual
    // (closing it then returns to the intro).
    explicit LearnFrontScreen(std::string_view start) : learn_(LearnView::tabFromName(start)) {
        if (start.starts_with("manual:")) {
            manual_.emplace(std::string(start.substr(7)));
            manualOnly_ = true;
        }
    }

    void draw(MenuContext& ctx) override {
        introBackground(ctx);
        if (!ctx.learn) {
            ctx.go(FrontId::Intro);
            return;
        }
        LearnHost host;
        host.content = ctx.learn;
        host.start = [&ctx](learn::LessonKind kind, const std::string& slug) {
            if (ctx.startLesson) ctx.startLesson(kind, slug);
        };
        host.openManual = [this](const std::string& target) {
            if (manual_) manual_->go(target);
            else manual_.emplace(target);
        };
        const Painter p = ctx.painter();
        if (manual_) {
            Dialog d(p, "Manual", DialogSize::Full);
            if (!manual_->draw(p, d, host)) {
                manual_.reset();
                if (manualOnly_) ctx.go(FrontId::Intro);
            }
            return;
        }
        Dialog d(p, "Learn", DialogSize::Large);
        if (!learn_.draw(p, d, host)) ctx.go(FrontId::Intro);
    }

private:
    LearnView learn_;
    std::optional<ManualView> manual_;
    bool manualOnly_ = false;
};

} // namespace

std::unique_ptr<Screen> makeLearn(const ScreenArgs& args) { return std::make_unique<LearnScreen>(args); }
std::unique_ptr<Screen> makeManual(const ScreenArgs& args) { return std::make_unique<ManualScreen>(args); }
std::unique_ptr<FrontScreen> makeLearnFrontScreen(std::string_view start) { return std::make_unique<LearnFrontScreen>(start); }

} // namespace opense4::client::classic
