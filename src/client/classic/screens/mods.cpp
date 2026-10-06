// Mods in the front end (docs/sdk/packages-and-data.md "Choosing mods in the
// game"): the Mods window, OpenSE4's own in the classic look, with the mods of
// the mods folder, which are on and in what order, what each holds and what
// is wrong; the line about them in the setup screens; and a saved game played
// with other mods. Changes apply to the next game: the data set is read again
// with the mods chosen when the window closes with Done.

#include "client/classic/frontend.hpp"
#include "client/classic/mods_model.hpp"
#include "client/classic/pointers.hpp"
#include "client/classic/settings.hpp"
#include "client/classic/widgets.hpp"
#include "client/script/items.hpp"
#include "datafile/datafile.hpp"
#include "game/serialize.hpp"

#include <algorithm>
#include <cfloat>
#include <format>
#include <optional>

namespace opense4::client::classic {

namespace {

const ImVec4 kGood{0.45f, 0.90f, 0.50f, 1.0f};
const ImVec4 kBad{1.0f, 0.45f, 0.40f, 1.0f};
const ImVec4 kWarn{1.0f, 0.85f, 0.45f, 1.0f};

void wrapped(const ImVec4& color, std::string_view text) {
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(color, "%.*s", int(text.size()), text.data());
    ImGui::PopTextWrapPos();
    script::reportText(text, ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
}

void label(std::string_view text) {
    ImGui::TextColored(kLabelBlue, "%.*s", int(text.size()), text.data());
    script::reportText(text, ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
}

std::string displayName(const ModsChoice::Row& row) {
    if (!row.package) return row.id;
    return row.package->manifest.name.empty() ? row.id : row.package->manifest.name;
}

std::unique_ptr<FrontScreen> backScreen(const FrontFactory& back) { return back ? back() : makeFrontScreen(FrontId::Intro); }

class ModsScreen final : public FrontScreen {
public:
    explicit ModsScreen(FrontFactory back) : back_(std::move(back)) {}

    void draw(MenuContext& ctx) override {
        introBackground(ctx);
        if (!choice_) scan();
        const Painter p = ctx.painter();
        Dialog d(p, "Mods", DialogSize::Large);
        if (!d.open()) return;
        resolved_ = choice_->resolve();   // once a frame: the list, the details and Done read it
        d.beginContent();
        {
            // OpenSE4's own window: its own text font (docs/spec/06 §5.4).
            const ReadingText reading(p, p.fonts.readingFont());
            content(p);
        }
        d.beginButtons();
        buttons(ctx, d);
    }

private:
    void scan() {
        const LoadedMods& loaded = loadedMods();
        mods::ModLibrary library = mods::scanModsFolder(loaded.modsDir, loaded.open);
        choice_.emplace(std::move(library), settings().enabledMods);
        error_.clear();
        const std::vector<ModsChoice::Row>& rows = rowsCache();
        if (selected_.empty() || std::none_of(rows.begin(), rows.end(), [&](const auto& r) { return r.id == selected_; }))
            selected_ = rows.empty() ? std::string{} : rows.front().id;
    }

    const std::vector<ModsChoice::Row>& rowsCache() {
        rows_ = choice_->rows();
        return rows_;
    }

    const ModsChoice::Row* selectedRow() const {
        for (const ModsChoice::Row& r : rows_)
            if (r.id == selected_) return &r;
        return nullptr;
    }

    void content(const Painter& p) {
        rowsCache();
        const float listW = p.px(236);
        const float upperH = p.px(270);
        list(p, listW, upperH);
        ImGui::SameLine();
        ImGui::BeginChild("##details", ImVec2(0, upperH), ImGuiChildFlags_Borders);
        details(p);
        ImGui::EndChild();
        ImGui::BeginChild("##choice", ImVec2(0, 0), ImGuiChildFlags_Borders);
        summary(p);
        ImGui::EndChild();
    }

    void list(const Painter& p, float width, float height) {
        ImGui::BeginChild("##mods", ImVec2(width, height), ImGuiChildFlags_Borders);
        const LoadedMods& loaded = loadedMods();
        if (rows_.empty()) {
            wrapped(kDimText, std::format("No mods yet. A mod is a folder or a .zip with its mod.toml; put it in {}.", loaded.modsDir.string()));
        }
        const float rowH = std::max(p.px(34), ImGui::GetTextLineHeight() * 2.0f + p.px(2));
        ImDrawList* dl = ImGui::GetWindowDrawList();
        for (const ModsChoice::Row& row : rows_) {
            ImGui::PushID(row.id.c_str());
            if (ImGui::Selectable("##row", selected_ == row.id, ImGuiSelectableFlags_AllowDoubleClick, ImVec2(0, rowH))) {
                selected_ = row.id;
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && row.package) toggle(row.id);
            }
            script::reportItem("mod:" + row.id);   // input scripts find a mod by its id
            const ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
            // The lamp: green when on, blue when off; red for one that is not there.
            const ImVec2 lamp(min.x + p.px(4), min.y + p.px(4));
            const ImU32 lampColor = !row.package ? IM_COL32(220, 70, 60, 255) : row.enabled ? IM_COL32(80, 220, 90, 255) : IM_COL32(60, 90, 200, 255);
            if (const Sprite s = row.package ? p.art.region("Pictures/Game/General.bmp", 178 + 13 * (row.enabled ? 1 : 0), 0, 13, 13) : Sprite{})
                dl->AddImage(ImTextureRef(static_cast<ImTextureID>(s.tex.value)), lamp, ImVec2(lamp.x + p.px(13), lamp.y + p.px(13)),
                             ImVec2(s.uv.min.x, s.uv.min.y), ImVec2(s.uv.max.x, s.uv.max.y));
            else dl->AddCircleFilled(ImVec2(lamp.x + p.px(6.5f), lamp.y + p.px(6.5f)), p.px(5), lampColor);
            script::reportItem(std::format("{}:{}", row.enabled ? "on" : "off", row.id), lamp, ImVec2(lamp.x + p.px(13), lamp.y + p.px(13)));
            const float textX = min.x + p.px(22);
            const std::string name = displayName(row);
            const float orderW = row.order > 0 ? ImGui::CalcTextSize("00").x + p.px(6) : 0.0f;
            drawFitted(p, dl, ImGui::GetFont(), kTextSize, ImVec2(textX, min.y + p.px(1)), max.x - textX - orderW - p.px(4), IM_COL32_WHITE, name, 0.0f, 0.0f, true);
            if (row.order > 0) {
                // Its place in the load order.
                const std::string n = std::to_string(row.order);
                const ImVec2 at(max.x - ImGui::CalcTextSize(n.c_str()).x - p.px(6), min.y + p.px(1));
                dl->AddText(at, ImGui::GetColorU32(kLabelBlue), n.c_str());
                script::reportItem(std::format("order:{}:{}", row.order, row.id), at, ImVec2(max.x, at.y + ImGui::GetTextLineHeight()));
            }
            const float second = min.y + std::max(p.px(17), ImGui::GetTextLineHeight() + p.px(2));
            std::string sub;
            if (!row.package) sub = "not in the mods folder";
            else sub = std::format("{}, {}", row.package->manifest.version.text, mods::tierNames(row.package->tiers));
            bool trouble = row.enabled && !row.package;
            if (row.enabled && !resolved_)
                for (const std::string& e : resolved_.error()) trouble = trouble || mentionsMod(e, row.id);
            drawFitted(p, dl, ImGui::GetFont(), kTextSize, ImVec2(textX, second), max.x - textX - p.px(4),
                       trouble ? ImGui::GetColorU32(kBad) : imColor(palette::kSecondary), sub, 0.0f, 0.0f, true);
            ImGui::PopID();
        }
        ImGui::EndChild();
    }

    void details(const Painter& p) {
        const ModsChoice::Row* row = selectedRow();
        if (!row) {
            wrapped(kDimText, "Choose a mod in the list to see what it holds.");
            return;
        }
        heading(p, displayName(*row).c_str());
        script::reportItem("details:" + row->id, ImGui::GetItemRectMin(), ImGui::GetItemRectMax());   // which mod the details show
        if (!row->package) {
            wrapped(kBad, std::format("{}: this mod is chosen but no longer in the mods folder. Done leaves it out.", row->id));
            return;
        }
        const mods::Package& pk = *row->package;
        const mods::Manifest& m = pk.manifest;
        wrapped(kDimText, std::format("{} {}{}", pk.id(), m.version.text, m.authors.empty() ? std::string{} : " by " + [&] {
            std::string a;
            for (const std::string& s : m.authors) a += (a.empty() ? "" : ", ") + s;
            return a;
        }()));
        if (!m.description.empty()) {
            ImGui::Spacing();
            wrapped(ImVec4(1, 1, 1, 1), m.description);
        }
        ImGui::Spacing();
        label("Holds");
        ImGui::SameLine();
        wrapped(ImVec4(1, 1, 1, 1), mods::tierNames(pk.tiers) + (pk.classic ? " (a classic mod)" : ""));
        if (pk.affectsGame())
            wrapped(kWarn, "It changes the game: everyone in a network or e-mail game needs the same, and saved games remember it.");
        else wrapped(kDimText, "Pictures, sounds or interface only: other players may have other ones.");
        script::reportItem(pk.affectsGame() ? "changes-game" : "cosmetic", ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
        if (!m.requirements.empty()) {
            std::string needs;
            for (const mods::Requirement& r : m.requirements) needs += std::format("{}{} {}", needs.empty() ? "" : ", ", r.id, r.range.text);
            label("Needs");
            ImGui::SameLine();
            wrapped(ImVec4(1, 1, 1, 1), needs);
        }
        if (!m.loadAfter.empty()) {
            std::string after;
            for (const std::string& a : m.loadAfter) after += (after.empty() ? "" : ", ") + a;
            label("Loads after");
            ImGui::SameLine();
            wrapped(ImVec4(1, 1, 1, 1), after);
        }
        if (row->enabled && !resolved_)
            for (const std::string& e : resolved_.error())
                if (mentionsMod(e, row->id)) {
                    wrapped(kBad, e);
                    script::reportItem("problem:" + row->id, ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
                }
        for (const std::string& w : pk.warnings) wrapped(kWarn, w);
        ImGui::Spacing();
        label("Identity");
        ImGui::SameLine();
        wrapped(kDimText, pk.hash);
        wrapped(kDimText, std::format("From {}", pk.source.string()));
    }

    void summary(const Painter&) {
        const LoadedMods& loaded = loadedMods();
        // What Done will do, and what keeps it from it.
        if (!error_.empty()) {
            wrapped(kBad, "These mods cannot be used:");
            script::reportItem("load-error", ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
            wrapped(kBad, error_);
        }
        if (resolved_) {
            std::string order, ids;
            for (const mods::Package& pk : resolved_->packages) {
                order += std::format("{}{}", order.empty() ? "" : ", ", pk.label());
                ids += (ids.empty() ? "" : ",") + pk.id();
            }
            label("Load order");
            ImGui::SameLine();
            wrapped(ImVec4(1, 1, 1, 1), order.empty() ? std::string("none: the game as installed") : order);
            script::reportItem("load-order:" + (ids.empty() ? std::string("none") : ids), ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
        } else {
            for (const std::string& e : resolved_.error()) wrapped(kBad, e);
        }
        for (const std::string& id : choice_->missing()) wrapped(kBad, std::format("{} is not in the mods folder: Done leaves it out.", id));
        for (const std::string& problem : choice_->library().problems) wrapped(kBad, problem);
        if (!loaded.startProblems.empty()) {
            wrapped(kBad, "When OpenSE4 started, the mods of the settings could not be used, so it started without them:");
            for (const std::string& problem : loaded.startProblems) wrapped(kBad, problem);
        }
        wrapped(kDimText, choice_->changed() ? "Done reads the game's data again with these mods. They apply to the next game you start or load."
                                             : "These are the mods in use. Changes apply to the next game: Done reads the game's data again.");
        if (loaded.fromCommandLine)
            wrapped(kWarn, "This run started with --mod or --no-mods: Done replaces those with the choice here, which the settings keep.");
        wrapped(kDimText, std::format("Mods folder: {}", loaded.modsDir.string()));
    }

    void toggle(const std::string& id) {
        choice_->toggle(id);
        error_.clear();
    }

    void buttons(MenuContext& ctx, Dialog& d) {
        const ModsChoice::Row* row = selectedRow();
        const std::string id = row ? row->id : std::string{};
        if (row && !row->package) {
            if (d.button("Remove")) choice_->dropMissing(id);
        } else if (d.button(row && row->enabled ? "Disable" : "Enable", row != nullptr)) {
            toggle(id);
        }
        // Where the mod is in the order, before either button changes it.
        const std::vector<std::string>& enabled = choice_->enabled();
        const auto at = std::find(enabled.begin(), enabled.end(), id);
        const bool canUp = at != enabled.end() && at != enabled.begin();
        const bool canDown = at != enabled.end() && at + 1 != enabled.end();
        if (d.button("Move Up", canUp)) choice_->move(id, -1);
        if (d.button("Move Down", canDown)) choice_->move(id, 1);
        d.spacer();
        if (d.button("Refresh")) {
            // Mods copied into the folder meanwhile; the choice so far stays,
            // and Done still compares it with the one in use.
            std::vector<std::string> ids = choice_->enabled();
            ids.insert(ids.end(), choice_->missing().begin(), choice_->missing().end());
            const LoadedMods& loaded = loadedMods();
            choice_.emplace(mods::scanModsFolder(loaded.modsDir, loaded.open), std::move(ids), settings().enabledMods);
            rowsCache();
            resolved_ = choice_->resolve();
        }
        for (int i = 0; i < 7; ++i) d.spacer();
        if (d.button("Done", resolved_.has_value())) {
            // Nothing to read again when the choice is the data in use (the
            // command line's mods and mods that failed at the start are not).
            const LoadedMods& loaded = loadedMods();
            if (!choice_->changed() && !loaded.fromCommandLine && loaded.startProblems.empty()) {
                ctx.goTo(backScreen(back_));
            } else {
                MenuContext::ModsChange change;
                change.mods = choice_->enabled();
                change.next = back_;
                change.failed = [this](const std::string& why) { error_ = why; };
                ctx.changeMods(std::move(change));
            }
        }
        if (d.close(true, "Cancel")) ctx.goTo(backScreen(back_));
    }

    FrontFactory back_;
    std::optional<ModsChoice> choice_;
    std::vector<ModsChoice::Row> rows_;
    std::expected<mods::ModSet, std::vector<std::string>> resolved_;
    std::string selected_;
    std::string error_;   // the last Done's: the data set could not be read with these mods
};

// A saved game played with other mods than the ones in use.
class SavedGameModsScreen final : public FrontScreen {
public:
    SavedGameModsScreen(std::filesystem::path file, FrontFactory back) : file_(std::move(file)), back_(std::move(back)) {}

    void draw(MenuContext& ctx) override {
        introBackground(ctx);
        if (!checked_) {
            checked_ = true;
            needs_ = savedGameMods(file_, *ctx.rules, loadedMods());
            if (!needs_) {
                // Nothing to choose any more: load it.
                if (auto problem = loadFromFrontEnd(ctx, file_, back_)) error_ = *problem;
                return;
            }
        }
        const Painter p = ctx.painter();
        Dialog d(p, "Other Mods", DialogSize::Large);
        if (!d.open()) return;
        d.beginContent();
        {
            const ReadingText reading(p, p.fonts.readingFont());
            heading(p, file_.stem().string().c_str());
            wrapped(ImVec4(1, 1, 1, 1), "This game was played with other mods than the ones in use, and plays only with the same ones "
                                        "(pictures and sounds aside):");
            if (needs_)
                for (const std::string& line : needs_->differences) {
                    ImGui::Bullet();
                    wrapped(kBad, line);
                    script::reportItem(line, ImGui::GetItemRectMin(), ImGui::GetItemRectMax());   // input scripts read each difference
                }
            ImGui::Spacing();
            if (needs_ && needs_->unavailable.empty()) {
                wrapped(kGood, needs_->ids.empty() ? std::string("Load Without Mods reads the game's data again without mods, then loads the game.")
                                                   : std::format("The mods folder has them: Load with Its Mods reads the game's data again "
                                                                 "with {}, then loads the game.",
                                                                 ruleset::describeMods(needs_->recorded)));
            } else if (needs_) {
                wrapped(kDimText, "The mods folder does not have them all:");
                for (const std::string& line : needs_->unavailable) {
                    ImGui::Bullet();
                    wrapped(kDimText, line);
                }
                wrapped(kDimText, "Get those mods from whoever you played with, put them in the mods folder, and try again.");
            }
            if (!error_.empty()) wrapped(kBad, error_);
        }
        d.beginButtons();
        const bool available = needs_ && needs_->unavailable.empty();
        if (d.button(available && needs_->ids.empty() ? "Load Without Mods" : "Load with Its Mods", available)) {
            MenuContext::ModsChange change;
            change.mods = needs_->ids;
            change.next = back_;
            change.load = file_;
            change.failed = [this](const std::string& why) { error_ = why; };
            ctx.changeMods(std::move(change));
        }
        if (d.button("Mods")) {
            const std::filesystem::path file = file_;
            const FrontFactory back = back_;
            ctx.goTo(makeModsScreen([file, back]() -> std::unique_ptr<FrontScreen> { return makeSavedGameModsScreen(file, back); }));
        }
        if (d.close(true, "Cancel")) ctx.goTo(backScreen(back_));
    }

private:
    std::filesystem::path file_;
    FrontFactory back_;
    bool checked_ = false;
    std::optional<SavedGameMods> needs_;
    std::string error_;
};

} // namespace

std::unique_ptr<FrontScreen> makeModsScreen(FrontFactory back) { return std::make_unique<ModsScreen>(std::move(back)); }

std::unique_ptr<FrontScreen> makeSavedGameModsScreen(std::filesystem::path file, FrontFactory back) {
    return std::make_unique<SavedGameModsScreen>(std::move(file), std::move(back));
}

std::optional<std::string> loadFromFrontEnd(MenuContext& ctx, const std::filesystem::path& file, FrontFactory back) {
    if (savedGameMods(file, *ctx.rules, loadedMods())) {
        ctx.goTo(makeSavedGameModsScreen(file, std::move(back)));
        return std::nullopt;
    }
    const BusyPointer busy;  // the Hourglass while it loads (spec 06 §5.8)
    auto session = ClassicSession::load(ctx.rules, file);
    if (!session) return session.error();
    restoreHistoryFrom(file);
    if (ctx.loadedFromIntro) ctx.loadedFromIntro();
    ctx.startGame(std::move(*session));
    return std::nullopt;
}

bool modsLine(MenuContext& ctx, ImVec2 textAt, ImVec2 buttonAt, ImVec2 buttonSize, float width) {
    const Painter p = ctx.painter();
    const LoadedMods& loaded = loadedMods();
    const std::string text = "Mods: " + modsSummary(loaded.packages);
    // Up to three lines in the small face, ending above the button.
    ImFont* font = p.fonts.small ? p.fonts.small : p.fonts.regular;
    const float size = p.fontPx(kSmallSize);
    const ImVec2 extent = font->CalcTextSizeA(size, FLT_MAX, width, text.data(), text.data() + text.size());
    const ImVec2 at(textAt.x, std::max(textAt.y - std::max(0.0f, extent.y - size * 1.2f), textAt.y - size * 2.4f));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddText(font, size, at, loaded.startProblems.empty() ? imColor(palette::kSecondary) : IM_COL32(255, 128, 100, 255), text.data(),
                text.data() + text.size(), width);
    script::reportItem(text, at, ImVec2(at.x + width, at.y + extent.y));   // input scripts read the summary
    ImGui::SetCursorScreenPos(buttonAt);
    ImGui::PushFont(p.fonts.bold, p.fontPx(kTitleSize));
    const bool open = classicButton(p, "Mods", Vec2{buttonSize.x / p.k(), buttonSize.y / p.k()});
    ImGui::PopFont();
    return open;
}

} // namespace opense4::client::classic
