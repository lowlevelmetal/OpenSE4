// Log (F10): this turn's news, messages from other empires and refused
// orders, with category filters, a details pane with the event picture, a
// mini-map, Send Reply, Combat Replay, Constr. Queues and Goto (docs/spec/06
// §4.1-§4.3, confirmed: binary).
//
// The filter, the selected position and the scroll position belong to the
// empire (game::InterfaceOptions, saved with the game) and come back on every
// opening.

#include "client/classic/screens/empire_widgets.hpp"
#include "client/classic/screens/screens.hpp"
#include "client/classic/widgets.hpp"

#include <algorithm>
#include <cmath>
#include <format>

namespace opense4::client::classic {

namespace {

using game::LogCategory;

constexpr float kRowH = 16.0f;
constexpr Vec2 kListPos{17, 57}, kListSize{272, 207};
constexpr Vec2 kMapPos{17, 272}, kMapSize{272, 190};
constexpr Vec2 kDetailsPos{296, 57}, kDetailsSize{278, 405};

const char* filterLabel(int category) {
    static const std::array<std::string, kLogCategories> names = [] {
        std::array<std::string, kLogCategories> a;
        for (int i = 0; i < kLogCategories; ++i) a[size_t(i)] = std::string(game::displayName(static_cast<LogCategory>(i)));
        a[size_t(LogCategory::Misc)] = "Misc";
        return a;
    }();
    return names[size_t(category)].c_str();
}

struct Row {
    enum class Kind : uint8_t { Entry, Message, Notice };
    Kind kind = Kind::Entry;
    uint32_t turn = 0;
    LogCategory category = LogCategory::Misc;
    std::string title;
    const game::LogEntry* entry = nullptr;
    const game::DiplomaticMessage* message = nullptr;
    const std::string* notice = nullptr;
};

class LogScreen final : public Screen {
public:
    bool draw(UiContext& ui) override {
        const game::GameState& s = ui.state();
        const std::vector<Row> all = rows(ui);
        std::vector<int> counts(kLogCategories, 0);
        for (const Row& r : all) ++counts[size_t(r.category)];
        if (!opened_) {
            // The stored filter, else All when its category is empty now; the
            // stored row, else the first (spec 06 §4.1).
            opened_ = true;
            const game::InterfaceOptions& o = ui.options();
            filter_ = logOpeningFilter(o.logFilter, counts);
            selected_ = o.logPosition;
            scrollRows_ = o.logScroll;
            restoreScroll_ = true;
        }
        std::vector<const Row*> shown;
        for (const Row& r : all)
            if (filter_ == 0 || int(r.category) == filter_ - 1) shown.push_back(&r);
        selected_ = logOpeningRow(selected_, shown.size());
        const Row* sel = selected_ >= 0 ? shown[size_t(selected_)] : nullptr;

        Dialog d(ui, "Log", DialogSize::Large);
        if (!d.open()) return d.keepOpen();
        d.beginContent();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddText(d.at({17, 40}), imColor(palette::kLabel), "Log Messages");
        dl->AddText(d.at({296, 40}), imColor(palette::kLabel), "Log Details");
        dl->AddText(d.at({450, 40}), imColor(palette::kLabel), "Game Date:");
        const std::string date = formatDate(s.turn);
        dl->AddText(d.at({530, 40}), IM_COL32_WHITE, date.c_str());

        list(ui, d, shown);
        ui.tagItem("log:messages");
        MiniMapStyle style;
        const std::optional<game::Location> where = location(sel);
        if (where) style.highlight.push_back(where->system);
        ImGui::SetCursorScreenPos(d.at(kMapPos));
        miniMap(ui, "##logMap", kMapSize, style);
        ImGui::SetCursorScreenPos(d.at(kDetailsPos));
        ImGui::BeginChild("##details", ui.size(kDetailsSize), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
        details(ui, sel);
        ImGui::EndChild();
        cannotReplyPopup(ui);

        // Button column (spec 06 §4.1): All, the seven filters, an empty slot,
        // Send Reply, Combat Replay, Constr. Queues, Goto, Close.
        d.beginButtons();
        if (d.tab("All", filter_ == 0)) setFilter(ui, 0);
        ui.tagTab("all", filter_ == 0);
        const ImVec2 categoriesMin = ImGui::GetItemRectMin();
        // The categories' ids for lessons (learn/ids.hpp windowTabs), in LogCategory order.
        static constexpr std::array<const char*, kLogCategories> kCategoryIds{
            {"construction", "research", "intelligence", "events", "politics", "combat", "misc"}};
        for (int c = 0; c < kLogCategories; ++c) {
            if (d.tab(filterLabel(c), filter_ == c + 1, counts[size_t(c)] > 0)) setFilter(ui, uint8_t(c + 1));
            ui.tagTab(kCategoryIds[size_t(c)], filter_ == c + 1);
        }
        ui.tag("log:categories", categoriesMin, ImGui::GetItemRectMax());
        d.spacer();
        const game::DiplomaticMessage* msg = sel ? sel->message : nullptr;
        const bool reply = d.button("Send Reply", msg != nullptr);
        ui.tagItem("log:send-reply");
        if (reply) {
            // One message per empire per turn: a second one is refused (spec 05 §3).
            if (msg->from.index() < ui.me().relations.size() && ui.me().relation(msg->from).messageSentThisTurn) {
                ImGui::OpenPopup("Cannot Reply");
            } else {
                ScreenArgs a;
                a.empire = msg->from;
                a.index = static_cast<int>(msg->id.value);
                ui.open(ScreenId::Communicate, a);
            }
        }
        const int combat = combatIndex(ui, sel);
        // Only with Settings.txt `Create Combat Replay` on (spec 06 §4.1).
        if (d.button("Combat Replay", combat >= 0 && ui.rules().settingFlag("Create Combat Replay", true))) {
            ScreenArgs a;
            a.index = combat;
            if (where) a.location = where;
            ui.open(ScreenId::CombatReplay, a);
        }
        if (d.button("Constr. Queues")) ui.open(ScreenId::Queues);
        const std::optional<LogWindow> window = sel && !where ? logWindowTarget(sel->category) : std::nullopt;
        bool close = false;
        if (d.button("Goto", where.has_value() || window.has_value())) {
            remember(ui);
            if (where) {
                // A location closes the Log and shows the sector in the main window.
                ui.requests.focus = *where;
                close = true;
            } else {
                ui.open(windowScreen(*window));   // over the Log, which stays open
            }
        }
        if (d.close()) remember(ui);
        if (close) d.requestClose();
        return d.keepOpen();
    }

private:
    // This turn's entries in the order they were made (spec 06 §4.1): the
    // messages other empires sent (politics comes first in a turn), the log,
    // then the orders the turn refused.
    std::vector<Row> rows(const UiContext& ui) const {
        const game::GameState& s = ui.state();
        const game::Empire& me = ui.me();
        std::vector<Row> out;
        auto thisTurn = [&](uint32_t turn) { return turn + 1 >= s.turn; };
        for (const game::DiplomaticMessage& m : s.messages) {
            if (m.to != me.id || !m.delivered || m.from.index() >= s.empires.size() || !thisTurn(m.sentTurn)) continue;
            Row r;
            r.kind = Row::Kind::Message;
            r.turn = m.sentTurn;
            r.category = LogCategory::Politics;
            r.title = std::format("{}: {}", s.empire(m.from).name, game::displayName(m.type));
            r.message = &m;
            out.push_back(std::move(r));
        }
        for (const game::LogEntry& l : me.log) {
            if (!thisTurn(l.turn)) continue;
            Row r;
            r.turn = l.turn;
            r.category = l.category;
            r.title = l.title.empty() ? std::string(game::displayName(l.category)) : l.title;
            r.entry = &l;
            out.push_back(std::move(r));
        }
        const auto& notices = ui.session.notices();
        for (const std::string& n : notices) {
            Row r;
            r.kind = Row::Kind::Notice;
            r.turn = s.turn == 0 ? 0 : s.turn - 1;
            r.category = LogCategory::Misc;
            r.title = "Order not carried out";
            r.notice = &n;
            out.push_back(std::move(r));
        }
        return out;
    }

    // The message list: 16 px rows, each with a lamp (green for the selected
    // row, blue for the others) and the title 17 px in; the row under the
    // pointer has the row grid texture behind it.
    void list(UiContext& ui, Dialog& d, const std::vector<const Row*>& shown) {
        ImGui::SetCursorScreenPos(d.at(kListPos));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
        ImGui::BeginChild("##list", ui.size(kListSize), ImGuiChildFlags_Borders);
        if (restoreScroll_) {
            ImGui::SetScrollY(ui.px(kRowH) * float(std::max(0, scrollRows_)));
            restoreScroll_ = false;
        }
        const Sprite green = ui.art.region("Pictures/Game/General.bmp", 190, 0, 13, 13);
        const Sprite blue = ui.art.region("Pictures/Game/General.bmp", 177, 0, 13, 13);
        const Sprite grid = ui.art.region("Pictures/Game/Dialogs/Rowgrid.bmp", 0, 0, int(kListSize.x), int(kRowH), false);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float width = ImGui::GetContentRegionAvail().x;
        for (size_t i = 0; i < shown.size(); ++i) {
            ImGui::PushID(int(i));
            const ImVec2 a = ImGui::GetCursorScreenPos();
            if (ImGui::InvisibleButton("##row", ImVec2(width, ui.px(kRowH)))) selected_ = int(i);
            const ImVec2 b{a.x + width, a.y + ui.px(kRowH)};
            if (ImGui::IsItemHovered() && grid) drawSprite(dl, grid, a, b);
            const Sprite& lampSprite = int(i) == selected_ ? green : blue;
            const ImVec2 l{a.x + ui.px(2), a.y + ui.px(1.5f)};
            if (lampSprite) drawSprite(dl, lampSprite, l, {l.x + ui.px(13), l.y + ui.px(13)});
            dl->PushClipRect({a.x + ui.px(17), a.y}, b, true);
            dl->AddText({a.x + ui.px(17), a.y + (ui.px(kRowH) - ImGui::GetTextLineHeight()) * 0.5f}, IM_COL32_WHITE, shown[i]->title.c_str());
            dl->PopClipRect();
            ImGui::PopID();
        }
        if (shown.empty()) dimText("Nothing to report this turn.");
        scrollRows_ = int(std::lround(ImGui::GetScrollY() / ui.px(kRowH)));
        ImGui::EndChild();
        ImGui::PopStyleVar(2);
    }

    void setFilter(UiContext& ui, uint8_t f) {
        if (f == filter_) return;
        filter_ = f;
        selected_ = 0;
        restoreScroll_ = true;
        scrollRows_ = 0;
        // Every filter click is stored with the empire (spec 06 §4.1).
        game::InterfaceOptions o = ui.options();
        o.logFilter = f;
        ui.setOptions(o);
    }

    // On Close and Goto the position and the scroll position are stored with the empire.
    void remember(UiContext& ui) const {
        game::InterfaceOptions o = ui.options();
        o.logFilter = filter_;
        o.logPosition = std::max(0, selected_);
        o.logScroll = std::max(0, scrollRows_);
        ui.setOptions(o);
    }

    static ScreenId windowScreen(LogWindow w) {
        switch (w) {
            case LogWindow::ConstructionQueues: return ScreenId::Queues;
            case LogWindow::Research: return ScreenId::Research;
            case LogWindow::Intelligence: return ScreenId::Intelligence;
            case LogWindow::EmpireOptions: return ScreenId::EmpireOptions;
            case LogWindow::Designs: return ScreenId::Designs;
            case LogWindow::Empires: return ScreenId::Empires;
        }
        return ScreenId::Queues;
    }

    static std::optional<game::Location> location(const Row* r) {
        if (!r) return std::nullopt;
        if (r->entry) return r->entry->location;
        if (r->message && r->message->system.valid()) return game::Location{r->message->system, {}};
        return std::nullopt;
    }

    // The battle a combat entry reports (battles of the last processed turn).
    static int combatIndex(const UiContext& ui, const Row* r) {
        if (!r || !r->entry || r->entry->category != LogCategory::Combat || !r->entry->location) return -1;
        const auto& combats = ui.state().combats;
        for (size_t i = 0; i < combats.size(); ++i)
            if (combats[i].location == *r->entry->location) return int(i);
        return -1;
    }

    void cannotReplyPopup(UiContext& ui) {
        ImGui::SetNextWindowSize(ui.size({340, 0}));
        if (!ImGui::BeginPopupModal("Cannot Reply", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize | kPromptFlags)) return;
        ImGui::TextWrapped("A message has already gone to this empire this turn. Only one message per empire can be sent each turn.");
        ImGui::Spacing();
        if (ImGui::Button("OK", ImVec2(-FLT_MIN, ui.px(26))) || okKey()) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    void details(UiContext& ui, const Row* r) {
        const game::GameState& s = ui.state();
        if (!r) return;
        Sprite picture;
        if (r->entry) picture = ui.art.eventPicture(r->entry->picture);
        else if (r->message) picture = ui.art.racePortrait(s.empire(r->message->from).race.style);
        else if (r->notice) picture = ui.art.eventPicture("OrdersNotCompleted");
        const int combat = combatIndex(ui, r);
        framedImage(ui, picture, {128, 128});
        ImGui::Spacing();
        if (combat >= 0) {
            combatDetails(ui, *r, s.combats[size_t(combat)]);
            return;
        }
        ImGui::PushTextWrapPos(0.0f);
        ImGui::PushFont(ui.fonts.bold, ui.fontPx(kTitleSize));
        ImGui::TextUnformatted(r->title.c_str());
        ImGui::PopFont();
        ImGui::PopTextWrapPos();
        labelValue(ui, "Date:", formatDate(r->turn), 40);
        if (r->entry) {
            if (!r->entry->text.empty()) wrappedText(r->entry->text);
        } else if (r->message) {
            messageDetails(ui, *r->message);
        } else if (r->notice) {
            wrappedText(*r->notice);
        }
    }

    // A combat entry (spec 06 §4.1): "Combat in <system>", the date and the
    // sector, then each empire and the damage of its ships, unit groups and planets.
    void combatDetails(UiContext& ui, const Row& r, const game::CombatRecord& c) {
        const game::GameState& s = ui.state();
        const bool known = c.location.system.index() < s.galaxy.systems.size();
        ImGui::PushFont(ui.fonts.bold, ui.fontPx(kTitleSize));
        ImGui::TextUnformatted(std::format("Combat in {}", known ? s.galaxy.system(c.location.system).name : std::string("?")).c_str());
        ImGui::PopFont();
        const float x = ImGui::GetCursorPosX();
        auto at = [&](float dx) { ImGui::SameLine(x + ui.px(dx)); };
        ImGui::SetCursorPosX(x + ui.px(4));
        ImGui::TextColored(kLabelBlue, "Date:");
        at(40);
        ImGui::TextUnformatted(formatDate(r.turn).c_str());
        at(132);
        ImGui::TextColored(kLabelBlue, "Coord:");
        at(180);
        ImGui::TextUnformatted(std::format("({},{})", c.location.sector.x, c.location.sector.y).c_str());
        ImGui::Spacing();
        ImGui::SetCursorPosX(x + ui.px(4));
        ImGui::TextColored(kLabelBlue, "Combat Forces");
        at(200);
        ImGui::TextColored(kLabelBlue, "Damage");
        ImGui::BeginChild("##forces", ImVec2(0, 0));
        for (const CombatDamageRow& row : combatDamageRows(ui.rules(), s, c)) {
            const float y = ImGui::GetCursorPosY();
            if (row.header) {
                empireLabel(ui, row.empire);
            } else {
                ImGui::SetCursorPosX(ui.px(16));
                ImGui::TextUnformatted(row.name.c_str());
                ImGui::SameLine(ui.px(200));
                ImGui::TextColored(row.damage == "Dead" ? kTextBad : row.damage == "Taken" ? kTextWarn : ImVec4(1, 1, 1, 1), "%s",
                                   row.damage.c_str());
            }
            ImGui::SetCursorPosY(y + ui.px(20));
        }
        ImGui::EndChild();
    }

    void messageDetails(UiContext& ui, const game::DiplomaticMessage& m) {
        const game::GameState& s = ui.state();
        labelValue(ui, "From", s.empire(m.from).name, 60);
        labelValue(ui, "Tone", std::string(toneName(m.tone)), 60);
        if (messageNeeds(m.type).treaty || m.type == game::MessageType::AcceptTreaty)
            labelValue(ui, "Treaty", std::string(game::displayName(m.treaty)), 60);
        if (m.thirdEmpire.valid() && m.thirdEmpire.index() < s.empires.size()) labelValue(ui, "About", s.empire(m.thirdEmpire).name, 60);
        if (m.planet.valid() && m.planet.index() < s.galaxy.objects.size()) labelValue(ui, "Planet", s.galaxy.object(m.planet).name, 60);
        ImGui::Spacing();
        wrappedText(m.text.empty() ? std::string("(no text)") : m.text);
        auto items = [&](const char* title, const std::vector<game::PackageItem>& list) {
            if (list.empty()) return;
            ImGui::Spacing();
            ImGui::TextColored(kLabelBlue, "%s", title);
            for (const auto& i : list) ImGui::BulletText("%s", packageItemText(ui.rules(), s, i).c_str());
        };
        items("They give", m.offer);
        items("They ask for", m.request);
        ImGui::Spacing();
        if (m.answered) ImGui::TextColored(kTextDim, "Answered.");
        else if (answerable(m.type)) ImGui::TextColored(kTextWarn, "Awaiting our answer: use Send Reply.");
    }

    bool opened_ = false;
    uint8_t filter_ = 0;          // 0 All, else the category + 1
    int selected_ = 0;            // row of the filtered list
    int scrollRows_ = 0;
    bool restoreScroll_ = false;
};

} // namespace

std::unique_ptr<Screen> makeLog(const ScreenArgs&) { return std::make_unique<LogScreen>(); }

} // namespace opense4::client::classic
