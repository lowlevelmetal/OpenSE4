// The ending window (docs/spec/06 §1.7 Finale, §1.9): a full picture of its
// kind, drawn from the Settings.txt list with a source of our own (never the
// game's random numbers), a few words, Scores and Close. The main window
// opens it once when the game ends (finale.hpp).

#include "client/classic/finale.hpp"
#include "client/classic/screens/screens.hpp"
#include "client/classic/widgets.hpp"

#include <algorithm>
#include <cctype>
#include <format>
#include <random>
#include <string>

namespace opense4::client::classic {

namespace {

FinaleKind kindOf(const ScreenArgs& args) {
    if (args.index >= 0 && args.index <= static_cast<int>(FinaleKind::HumanDead)) return static_cast<FinaleKind>(args.index);
    std::string want;
    for (char c : args.text)
        if (std::isalpha(static_cast<unsigned char>(c))) want += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (want == "lose") return FinaleKind::Lose;
    if (want == "humandead") return FinaleKind::HumanDead;
    return FinaleKind::Victory;
}

class FinaleScreen final : public Screen {
public:
    explicit FinaleScreen(const ScreenArgs& args) : kind_(kindOf(args)) {}
    bool modal() const override { return true; }

    bool draw(UiContext& ui) override {
        if (!chosen_) choose(ui);
        Dialog d(ui, "Finale###finale", Vec2{640, 480});
        if (!d.open()) return d.keepOpen();
        d.beginContent();
        const game::GameState& s = ui.state();
        heading(ui, kind_ == FinaleKind::Victory ? "The Game Is Over" : kind_ == FinaleKind::Lose ? "Your Empire Has Fallen" : "No Human Player Is Left");
        if (const Sprite pic = picture_.empty() ? Sprite{} : ui.art.image(picture_, false)) {
            // The pictures are 426x341 or 400x300; at most 400 pixels wide here.
            const float scale = std::min(1.0f, 400.0f / std::max(1.0f, pic.size.x));
            image(ui, pic, {pic.size.x * scale, pic.size.y * scale});
        }
        ImGui::Spacing();
        std::string text;
        switch (kind_) {
            case FinaleKind::Victory:
                text = "A victory condition has been met, so this game has ended. The Scores window shows how every empire finished.";
                if (s.winner.valid() && s.winner.index() < s.empires.size())
                    text += std::format(" The best score belongs to the {}.", s.empire(s.winner).name);
                break;
            case FinaleKind::Lose: text = "Your last populated planet and your last ship are gone. Your empire is out of the game."; break;
            case FinaleKind::HumanDead:
                text = "Every empire left is played by the computer: no human player remains. The game ends here and no further turn is played.";
                break;
        }
        wrappedDim(text);
        d.beginButtons();
        if (d.button("Scores")) ui.open(ScreenId::Scores);
        d.close();
        return d.keepOpen();
    }

private:
    // One picture of the kind's list, uniformly, from a source of our own.
    void choose(UiContext& ui) {
        chosen_ = true;
        const std::vector<std::string> list = finalePictures(ui.rules().data().settings, kind_);
        if (list.empty()) return;
        std::random_device seed;
        std::mt19937 gen(seed());
        std::uniform_int_distribution<size_t> pick(0, list.size() - 1);
        picture_ = list[pick(gen)];
    }

    FinaleKind kind_;
    bool chosen_ = false;
    std::string picture_;
};

} // namespace

std::unique_ptr<Screen> makeFinale(const ScreenArgs& args) { return std::make_unique<FinaleScreen>(args); }

} // namespace opense4::client::classic
