// The ending windows (docs/spec/06 §1.7 Finale, §1.9, §7 Q83): a full picture
// of its kind, drawn from the Settings.txt list with a source of our own
// (never the game's random numbers), a few words, Scores and Close. The main
// window opens them at a turn's start, each as it comes (finale.hpp); several
// due at once show one after another, and the conquest of the galaxy asks
// whether to play on.

#include "client/classic/finale.hpp"
#include "client/classic/screens/screens.hpp"
#include "client/classic/widgets.hpp"

#include <algorithm>
#include <cctype>
#include <format>
#include <random>
#include <string>
#include <vector>

namespace opense4::client::classic {

namespace {

std::optional<FinaleKind> kindNamed(std::string_view name) {
    std::string want;
    for (char c : name)
        if (std::isalpha(static_cast<unsigned char>(c))) want += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (want.empty()) return std::nullopt;
    if (want == "lose") return FinaleKind::Lose;
    if (want == "humandead") return FinaleKind::HumanDead;
    if (want == "conquered") return FinaleKind::Conquered;
    return FinaleKind::Victory;
}

// ScreenArgs::text lists the kinds ("lose,victory"); else ScreenArgs::index is one.
std::vector<FinaleKind> kindsOf(const ScreenArgs& args) {
    std::vector<FinaleKind> out;
    std::string_view rest = args.text;
    while (!rest.empty()) {
        const size_t comma = rest.find(',');
        if (const auto k = kindNamed(rest.substr(0, comma))) out.push_back(*k);
        if (comma == std::string_view::npos) break;
        rest.remove_prefix(comma + 1);
    }
    if (out.empty()) {
        const int last = static_cast<int>(FinaleKind::Conquered);
        out.push_back(args.index >= 0 && args.index <= last ? static_cast<FinaleKind>(args.index) : FinaleKind::Victory);
    }
    return out;
}

class FinaleScreen final : public Screen {
public:
    explicit FinaleScreen(const ScreenArgs& args) : kinds_(kindsOf(args)) {}
    bool modal() const override { return true; }

    bool draw(UiContext& ui) override {
        if (asking_) return askToContinue(ui);
        const FinaleKind kind = kinds_[shown_];
        if (!chosen_) choose(ui, kind);
        bool next = false;
        {
            Dialog d(ui, "Finale###finale", Vec2{640, 480});
            if (!d.open()) return d.keepOpen();
            d.beginContent();
            heading(ui, title(kind));
            if (const Sprite pic = picture_.empty() ? Sprite{} : ui.art.image(picture_, false)) {
                // The pictures are 426x341 or 400x300; at most 400 pixels wide here.
                const float scale = std::min(1.0f, 400.0f / std::max(1.0f, pic.size.x));
                image(ui, pic, {pic.size.x * scale, pic.size.y * scale});
            }
            ImGui::Spacing();
            wrappedDim(text(ui, kind));
            d.beginButtons();
            if (d.button("Scores")) ui.open(ScreenId::Scores);
            next = d.close();
            if (!next && d.keepOpen()) return true;
        }
        // Closed: the conquest asks whether to play on; then the next ending, if any.
        if (kind == FinaleKind::Conquered) {
            asking_ = true;
            question_.open("Do you want to continue playing?", "All Players Eliminated");
            return true;
        }
        return advance();
    }

private:
    static const char* title(FinaleKind k) {
        switch (k) {
            case FinaleKind::Victory: return "The Game Is Over";
            case FinaleKind::Lose: return "Your Empire Has Fallen";
            case FinaleKind::HumanDead: return "No Human Player Is Left";
            case FinaleKind::Conquered: return "The Galaxy Is Yours";
        }
        return "";
    }

    static std::string text(const UiContext& ui, FinaleKind k) {
        const game::GameState& s = ui.state();
        switch (k) {
            case FinaleKind::Victory: {
                std::string t = "A victory condition has been met, so this game has ended. The Scores window shows how every empire finished.";
                if (s.winner.valid() && s.winner.index() < s.empires.size()) t += std::format(" The best score belongs to the {}.", s.empire(s.winner).name);
                return t;
            }
            case FinaleKind::Lose:
                return "Your empire has no colony and no ship left. This is your last turn: when it ends, your empire is out of the game.";
            case FinaleKind::HumanDead:
                return "Every empire left is played by the computer: no human player remains. The game ends here and no further turn is played.";
            case FinaleKind::Conquered: {
                const game::Empire& me = ui.me();
                return std::format("Every other empire has been destroyed. The {} under {} {} stands alone in the quadrant.", me.name, me.leaderTitle,
                                   me.leaderName);
            }
        }
        return {};
    }

    // The question after the conquest: Yes plays on, No leaves the game for
    // the intro (inferred, spec 06 §7 Q99).
    bool askToContinue(UiContext& ui) {
        const std::optional<bool> yes = question_.answer(ui);
        if (!yes) return true;
        asking_ = false;
        if (*yes) return advance();
        ui.requests.quitToIntro = true;
        return false;
    }

    bool advance() {
        if (++shown_ >= kinds_.size()) return false;
        chosen_ = false;
        picture_.clear();
        return true;
    }

    // One picture of the kind's list, uniformly, from a source of our own.
    void choose(UiContext& ui, FinaleKind kind) {
        chosen_ = true;
        const std::vector<std::string> list = finalePictures(ui.rules().data().settings, kind);
        if (list.empty()) return;
        std::random_device seed;
        std::mt19937 gen(seed());
        std::uniform_int_distribution<size_t> pick(0, list.size() - 1);
        picture_ = list[pick(gen)];
    }

    std::vector<FinaleKind> kinds_;
    size_t shown_ = 0;
    bool chosen_ = false;
    bool asking_ = false;
    std::string picture_;
    YesNoPrompt question_;
};

} // namespace

std::unique_ptr<Screen> makeFinale(const ScreenArgs& args) { return std::make_unique<FinaleScreen>(args); }

} // namespace opense4::client::classic
