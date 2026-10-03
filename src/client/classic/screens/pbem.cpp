// Play by e-mail from the front end (docs/MULTIPLAYER.md, "Play by e-mail"):
// open the turn file the host sent (the game as the player's empire knows
// it), give the empire's password and play the turn. End Turn then saves the
// signed orders file (.plr) for the host (ClassicSession, SessionKind::Pbem;
// the logic is in pbem_play.hpp). The screen shows the host key that signed
// the turn file; a key other than the one trusted for the game, and showing
// an OpenSE4 0.6 password's old form, each need the player's agreement twice.

#include "client/classic/frontend.hpp"
#include "client/classic/screens/list_widgets.hpp"
#include "client/classic/pbem_play.hpp"

#include <cstdio>
#include <format>
#include <optional>
#include <utility>

namespace opense4::client::classic {

namespace {

void pathField(const char* label, std::string& value, float width, ImGuiInputTextFlags flags = 0) {
    char buffer[1024] = {};
    std::snprintf(buffer, sizeof buffer, "%s", value.c_str());
    ImGui::SetNextItemWidth(width);
    if (ImGui::InputText(label, buffer, sizeof buffer, flags)) value = buffer;
}

class PbemScreen final : public FrontScreen {
public:
    explicit PbemScreen(std::string_view file) : file_(file), openAtOnce_(!file.empty()) {}

    void draw(MenuContext& ctx) override {
        if (!scanned_) {
            scanned_ = true;
            folder_ = pbemDir();
            files_ = listTurnFiles(folder_);
        }
        if (std::exchange(openAtOnce_, false)) open(ctx);

        ImGui::SetNextWindowPos(ctx.at({62, 50}));
        ImGui::SetNextWindowSize(ctx.size({900, 668}));
        ImGui::PushFont(ctx.fonts.regular, kTextSize * ctx.k());
        ImGui::Begin("Play by E-mail", nullptr,
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
        ImGui::TextWrapped("The host of a play-by-e-mail game sends each player a turn file (.turn): the game as your empire knows "
                           "it, which only your password opens. Open yours here, give your password and play your turn. End Turn "
                           "saves your orders file (.plr), signed with your password and readable by the host only; send that file "
                           "back to the host.");
        ImGui::Spacing();
        pathField("Turn file", file_, ctx.px(600), ImGuiInputTextFlags_None);
        ImGui::SameLine();
        if (ImGui::Button("Open", ctx.size({90, 0}))) open(ctx);
        ImGui::TextDisabled("Turn files in %s:", folder_.string().c_str());
        beginList(ctx.painter(), "##files", ImVec2(0, ctx.px(120)), kListLineStep, ImGuiChildFlags_AlwaysUseWindowPadding);
        if (files_.empty()) ImGui::TextDisabled("None. Put the turn file there, or type its full path above.");
        for (const auto& f : files_)
            if (ImGui::Selectable(f.filename().string().c_str(), game_ && game_->gameFile == f)) {
                file_ = f.string();
                open(ctx);
            }
        endList(ctx.painter());

        if (game_) gamePanel(ctx);

        ImGui::Spacing();
        if (ImGui::Button("Back", ctx.size({160, 34})) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) ctx.go(FrontId::Multiplayer);
        if (!error_.empty()) ImGui::TextColored(ImVec4(1, 0.5f, 0.4f, 1), "%s", error_.c_str());
        ImGui::End();
        ImGui::PopFont();
    }

private:
    void open(MenuContext& ctx) {
        error_.clear();
        game_.reset();
        choices_.clear();
        chosen_ = -1;
        trust_ = {};
        confirming_ = Confirm::None;
        if (file_.empty()) {
            error_ = "Choose a turn file.";
            return;
        }
        auto loaded = loadPbemGame(*ctx.rules, file_);
        if (!loaded) {
            error_ = loaded.error();
            return;
        }
        game_ = std::move(*loaded);
        host_ = pbemHostKey(*game_);
        choices_ = pbemEmpires(*game_);
        // Preselect the only empire that can play now, if there is one.
        int playable = 0;
        for (size_t i = 0; i < choices_.size(); ++i)
            if (choices_[i].playable && choices_[i].yourTurn) {
                ++playable;
                chosen_ = static_cast<int>(i);
            }
        if (playable != 1) chosen_ = -1;
        ordersDir_ = game_->gameFile.has_parent_path() ? game_->gameFile.parent_path().string() : std::string(".");
    }

    void gamePanel(MenuContext& ctx) {
        // Before the password only the turn file's header is readable.
        const game::SaveInfo& info = game_->info;
        ImGui::SeparatorText("Game");
        ImGui::Text("%s", std::format("'{}', turn {}", info.gameName, info.turn).c_str());
        if (game_->turnBased) {
            const game::EmpireId active = pbemActivePlayer(*game_);
            ImGui::TextColored(ImVec4(1, 0.85f, 0.45f, 1), "%s",
                               active.valid() && active.index() < info.empires.size()
                                   ? std::format("Turn-based: it is {}'s turn.", info.empires[active.index()]).c_str()
                                   : "Turn-based: nobody's turn (the host plays on first).");
            ImGui::TextWrapped("Your orders are carried out at once on your copy, as a preview. The host carries them out on the whole "
                               "game, which decides battles and what you cannot see; your next turn file shows the result.");
        } else {
            ImGui::TextUnformatted("Simultaneous turns: every player sends orders for this turn.");
        }
        hostPanel(ctx);
        ImGui::SeparatorText("Your empire");
        for (size_t i = 0; i < choices_.size(); ++i) {
            const PbemEmpireChoice& c = choices_[i];
            std::string label = c.name;
            if (!c.player.empty()) label += std::format(" (player {})", c.player);
            if (!c.playable) label += "  - not yours to play";
            else if (!c.yourTurn) label += "  - not its turn";
            ImGui::BeginDisabled(!c.playable || !c.yourTurn);
            if (ImGui::RadioButton(std::format("{}##e{}", label, i).c_str(), chosen_ == static_cast<int>(i))) chosen_ = static_cast<int>(i);
            ImGui::EndDisabled();
        }
        ImGui::Spacing();
        pathField("Password", password_, ctx.px(300), ImGuiInputTextFlags_Password);
        if (pbemNeedsNewPassword(*game_)) oldPasswordPanel(ctx);
        pathField("Save orders in", ordersDir_, ctx.px(600));
        ImGui::Spacing();
        if (ImGui::Button("Play Turn", ctx.size({160, 34})) && game_) play(ctx);
    }

    void play(MenuContext& ctx) {
        error_.clear();
        if (chosen_ < 0 || static_cast<size_t>(chosen_) >= choices_.size()) {
            error_ = "Choose your empire.";
            return;
        }
        auto turn = beginPbemTurn(*game_, choices_[static_cast<size_t>(chosen_)].id, password_, ordersDir_, newPassword_, trust_);
        password_.clear();
        newPassword_.clear();
        if (!turn) {
            error_ = turn.error();
            return;
        }
        PbemGame g = std::move(*game_);
        game_.reset();
        ctx.startGame(ClassicSession::pbem(ctx.rules, std::move(g), std::move(*turn), pbemDraftsDir()));
    }

    // The host key that signed the turn file, against the one trusted for the game.
    void hostPanel(MenuContext& ctx) {
        const ImVec4 warn(1, 0.5f, 0.4f, 1);
        ImGui::SeparatorText("Host");
        ImGui::PushTextWrapPos(0);
        switch (host_.status) {
            case net::pbem::HostKeyStatus::Known:
                ImGui::Text("Signed by the host key %s, the one this computer trusts for this game.", host_.fingerprint.c_str());
                break;
            case net::pbem::HostKeyStatus::First:
                ImGui::TextWrapped("Signed by the host key %s. This is the game's first turn file here: the key is trusted from your "
                                   "turn on, and a turn file signed by another is refused. Compare it with the one the host sees.",
                                   host_.fingerprint.c_str());
                break;
            case net::pbem::HostKeyStatus::Changed:
                ImGui::TextColored(warn, "This computer trusts the host key %s for this game, but this turn file is signed by %s.",
                                   host_.trusted.c_str(), host_.fingerprint.c_str());
                if (trust_.trustChangedHostKey) {
                    ImGui::TextWrapped("You trust the new key: it is remembered when your turn opens.");
                    break;
                }
                ImGui::TextWrapped("Trust the new key only if the host says it made one (a new computer, or a lost key file), and the "
                                   "fingerprint the host sees is the new one. Otherwise someone else made this turn file.");
                if (confirming_ != Confirm::HostKey) {
                    if (ImGui::Button("Trust the New Key...", ctx.size({300, 30}))) confirming_ = Confirm::HostKey;
                    break;
                }
                ImGui::TextColored(warn, "%s", std::format("Really trust {} for '{}'?", host_.fingerprint, game_->info.gameName).c_str());
                if (ImGui::Button("Yes, Trust It", ctx.size({200, 30}))) {
                    trust_.trustChangedHostKey = true;
                    confirming_ = Confirm::None;
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel##key", ctx.size({120, 30}))) confirming_ = Confirm::None;
                break;
        }
        ImGui::PopTextWrapPos();
    }

    // A game of OpenSE4 0.6: a new password, and the old one's form shown once
    // to the host, after the player compared its key.
    void oldPasswordPanel(MenuContext& ctx) {
        const ImVec4 warn(1, 0.5f, 0.4f, 1);
        ImGui::PushTextWrapPos(0);
        ImGui::TextWrapped("This game was made by OpenSE4 0.6, and your password is still in that version's form. Choose a new "
                           "password: this turn's orders file shows the host the old one's form once, and the new one counts from "
                           "this turn on. Do it only with a host you trust: compare its key %s with the one the host sees. Anyone "
                           "who recorded your files of OpenSE4 0.6 knows the old form: if that worries you, ask the host to reset "
                           "your password instead.",
                           host_.fingerprint.c_str());
        pathField("New password", newPassword_, ctx.px(300), ImGuiInputTextFlags_Password);
        if (trust_.showOldPassword) {
            ImGui::TextWrapped("Agreed: this turn's orders file shows the host with the key %s your old password's form, once.",
                               host_.fingerprint.c_str());
        } else if (confirming_ != Confirm::OldPassword) {
            if (ImGui::Button("The Key Matches: Agree...", ctx.size({300, 30}))) confirming_ = Confirm::OldPassword;
        } else {
            ImGui::TextColored(warn, "%s",
                               std::format("Really show your old password's form to the host with the key {}, once?", host_.fingerprint).c_str());
            if (ImGui::Button("Yes, Show It Once", ctx.size({200, 30}))) {
                trust_.showOldPassword = true;
                confirming_ = Confirm::None;
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel##old", ctx.size({120, 30}))) confirming_ = Confirm::None;
        }
        ImGui::PopTextWrapPos();
    }

    enum class Confirm { None, HostKey, OldPassword };

    std::string file_;
    bool openAtOnce_ = false;
    bool scanned_ = false;
    std::filesystem::path folder_;
    std::vector<std::filesystem::path> files_;
    std::optional<PbemGame> game_;
    std::vector<PbemEmpireChoice> choices_;
    int chosen_ = -1;
    std::string password_;
    std::string newPassword_;  // a game of OpenSE4 0.6: the password from this turn on
    net::pbem::HostKeyCheck host_;
    PbemTrust trust_;          // what the player agreed to (each confirmed twice)
    Confirm confirming_ = Confirm::None;
    std::string ordersDir_;
    std::string error_;
};

} // namespace

std::unique_ptr<FrontScreen> makePbemScreen(std::string_view file) { return std::make_unique<PbemScreen>(file); }

} // namespace opense4::client::classic
