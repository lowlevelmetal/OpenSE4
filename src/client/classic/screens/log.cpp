// Log (F10): this turn's entries in the order they were made (diplomatic
// messages are entries too), with category filters, a details pane with the
// event picture, a mini-map, Send Reply, Combat Replay, Constr. Queues and
// Goto (docs/spec/06 §4.1-§4.3, §7 Q41-Q43, confirmed: binary).
//
// The filter, the selected entry (its index in the empire's whole log) and
// the scroll position belong to the empire (game::InterfaceOptions, saved
// with the game) and come back on every opening.

#include "client/classic/screens/empire_widgets.hpp"
#include "client/classic/screens/list_widgets.hpp"
#include "client/classic/screens/screens.hpp"
#include "client/classic/widgets.hpp"
#include "client/classic/screens/item_reports.hpp"
#include "game/log_picture.hpp"

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
    uint32_t turn = 0;
    LogCategory category = LogCategory::Misc;
    std::string title;
    int32_t index = 0;   // the entry's index in the empire's whole log (notices: after the log)
    const game::LogEntry* entry = nullptr;
    const game::DiplomaticMessage* message = nullptr;   // a message's entry: the message
    const std::string* notice = nullptr;
};

// The categories' ids for lessons (learn/ids.hpp windowTabs), in LogCategory order.
constexpr std::array<const char*, kLogCategories> kCategoryIds{{"construction", "research", "intelligence", "events", "politics", "combat", "misc"}};

class LogScreen final : public Screen {
public:
    // `--open=log:<category id>` (automation) opens on that filter, once, without storing it.
    explicit LogScreen(std::string_view category) {
        for (int c = 0; c < kLogCategories; ++c)
            if (category == kCategoryIds[size_t(c)]) startFilter_ = uint8_t(c + 1);
    }

    bool draw(UiContext& ui) override {
        const game::GameState& s = ui.state();
        const std::vector<Row> all = rows(ui);
        std::vector<int> counts(kLogCategories, 0);
        for (const Row& r : all) ++counts[size_t(r.category)];
        std::vector<const Row*> shown;
        auto filterRows = [&] {
            shown.clear();
            for (const Row& r : all)
                if (filter_ == 0 || int(r.category) == filter_ - 1) shown.push_back(&r);
        };
        if (!opened_) {
            // The stored filter, else All, which is then stored, when its
            // category is empty now; the stored entry when the filtered list
            // shows it, else the first row (spec 06 §4.1, §7 Q42).
            opened_ = true;
            const game::InterfaceOptions& o = ui.options();
            filter_ = logOpeningFilter(startFilter_ ? startFilter_ : o.logFilter, counts);
            if (!startFilter_ && filter_ != o.logFilter) {
                game::InterfaceOptions changed = o;
                changed.logFilter = filter_;
                ui.setOptions(changed);
            }
            filterRows();
            std::vector<int32_t> indices;
            for (const Row* r : shown) indices.push_back(r->index);
            selected_ = logOpeningRow(startFilter_ ? -1 : ui.options().logPosition, indices);
            scrollRows_ = ui.options().logScroll;
            restoreScroll_ = true;
        } else {
            filterRows();
        }
        if (selected_ >= int(shown.size())) selected_ = shown.empty() ? -1 : 0;
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
        ui.tagItem("log:details");   // for lessons: the entry selected, to read
        cannotReplyPopup(ui);

        // Button column (spec 06 §4.1): All, the seven filters, an empty slot,
        // Send Reply, Combat Replay, Constr. Queues, Goto, Close.
        d.beginButtons();
        if (d.tab("All", filter_ == 0)) setFilter(ui, 0);
        ui.tagTab("all", filter_ == 0);
        const ImVec2 categoriesMin = ImGui::GetItemRectMin();
        for (int c = 0; c < kLogCategories; ++c) {
            if (d.tab(filterLabel(c), filter_ == c + 1, counts[size_t(c)] > 0)) setFilter(ui, uint8_t(c + 1));
            ui.tagTab(kCategoryIds[size_t(c)], filter_ == c + 1);
        }
        ui.tag("log:categories", categoriesMin, ImGui::GetItemRectMax());
        d.spacer();
        // Only a diplomatic message can be answered (spec 06 §4.1).
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
        // Goto follows the target of the selected entry (spec 06 §7 Q41): lit
        // when it has one, dim when not. With no entry selected nothing changes,
        // so it keeps the state it last had, lit when no entry was ever
        // selected, and a click does nothing (spec 06 §7 Q91, confirmed: binary).
        const game::LogGoto target = sel && sel->entry ? sel->entry->target : game::LogGoto::None;
        if (sel) gotoLit_ = target != game::LogGoto::None;
        bool close = false;
        if (d.button("Goto", gotoLit_) && sel) {
            remember(ui);
            if (target == game::LogGoto::Location) {
                // A location closes the Log and shows the sector in the main window
                // (a system without a sector, as received star charts give, shows
                // the system only, spec 06 §7 Q70); an entry that names no system
                // does nothing, and the Log stays open.
                if (const auto at = goTo(ui, sel)) {
                    if (at->sector.valid()) {
                        ui.requests.focus = *at;
                    } else {
                        ui.requests.showSystem = at->system;
                        ui.requests.showSystemEmptiesReport = true;
                    }
                    close = true;
                }
            } else if (const auto window = logWindowTarget(target)) {
                ui.open(windowScreen(*window));   // over the Log, which stays open
            }
        }
        if (d.close()) remember(ui);
        if (close) d.requestClose();
        return d.keepOpen();
    }

private:
    // This turn's entries in the order they were made (spec 06 §4.1, §7 Q42):
    // one list, the delivered diplomatic messages among them (each an entry
    // titled "Message"). Commands the host refused are OpenSE4's own case (a
    // network or play-by-e-mail turn): they follow at the end as Misc rows
    // without a Goto (inferred).
    std::vector<Row> rows(const UiContext& ui) const {
        const game::GameState& s = ui.state();
        const game::Empire& me = ui.me();
        std::vector<Row> out;
        auto thisTurn = [&](uint32_t turn) { return turn + 1 >= s.turn; };
        for (size_t i = 0; i < me.log.size(); ++i) {
            const game::LogEntry& l = me.log[i];
            if (!thisTurn(l.turn)) continue;
            Row r;
            r.turn = l.turn;
            r.category = l.category;
            r.title = l.title.empty() ? std::string(game::displayName(l.category)) : l.title;
            r.index = int32_t(i);
            r.entry = &l;
            if (l.message.valid())
                for (const game::DiplomaticMessage& m : s.messages)
                    if (m.id == l.message && m.to == me.id && m.from.index() < s.empires.size()) r.message = &m;
            out.push_back(std::move(r));
        }
        const auto& notices = ui.session.notices();
        for (size_t k = 0; k < notices.size(); ++k) {
            Row r;
            r.turn = s.turn == 0 ? 0 : s.turn - 1;
            r.category = LogCategory::Misc;
            r.title = "Order not carried out";
            r.index = int32_t(me.log.size() + k);
            r.notice = &notices[k];
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
        // The arrow column at the list's right (spec 06 conventions).
        beginList(ui, "##list", ui.size(kListSize), kRowH);
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
        // An empty log shows an empty list (observed, spec 07 session 3).
        scrollRows_ = int(std::lround(ImGui::GetScrollY() / ui.px(kRowH)));
        endList(ui);
        ImGui::PopStyleVar(2);
    }

    // A filter click shows that list from the top with its first row
    // selected, and is stored with the empire (spec 06 §4.1).
    void setFilter(UiContext& ui, uint8_t f) {
        filter_ = f;
        selected_ = 0;
        restoreScroll_ = true;
        scrollRows_ = 0;
        game::InterfaceOptions o = ui.options();
        o.logFilter = f;
        ui.setOptions(o);
    }

    // On Close and Goto the selected entry (its index in the whole log, -1
    // for none) and the scroll position are stored with the empire.
    void remember(UiContext& ui) {
        const std::vector<Row> all = rows(ui);
        std::vector<const Row*> shown;
        for (const Row& r : all)
            if (filter_ == 0 || int(r.category) == filter_ - 1) shown.push_back(&r);
        game::InterfaceOptions o = ui.options();
        o.logFilter = filter_;
        o.logPosition = selected_ >= 0 && size_t(selected_) < shown.size() ? shown[size_t(selected_)]->index : -1;
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

    // Where the entry happened (the mini-map's highlight).
    static std::optional<game::Location> location(const Row* r) {
        if (!r || !r->entry) return std::nullopt;
        if (r->entry->location) return r->entry->location;
        if (r->message && r->message->system.valid()) return game::Location{r->message->system, {}};
        return std::nullopt;
    }

    // A location Goto can show: the entry's, when it names a system of the galaxy.
    static std::optional<game::Location> goTo(const UiContext& ui, const Row* r) {
        const std::optional<game::Location> at = r && r->entry ? r->entry->location : std::nullopt;
        if (!at || !at->system.valid() || at->system.index() >= ui.state().galaxy.systems.size()) return std::nullopt;
        return at;
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

    // The picture of an entry, resolved now (spec 06 §4.1 "Picture" and
    // "Pictures of the other entries", confirmed: binary): none when the
    // entry has none, its file is missing or its subject is gone.
    static Sprite pictureOf(UiContext& ui, const Row& row) {
        using Kind = game::logpicture::Parsed::Kind;
        const game::GameState& s = ui.state();
        const game::Rules& r = ui.rules();
        if (row.notice) return ui.art.eventPicture("OrdersNotCompleted");
        if (!row.entry) return {};
        // A message logged before entries carried pictures: the sender's portrait.
        if (row.entry->picture.empty() && row.message) return ui.art.racePortrait(s.empire(row.message->from).race.style);
        const game::logpicture::Parsed p = game::logpicture::parse(row.entry->picture);
        auto style = [&](game::EmpireId e) -> const std::string* {
            return e.valid() && e.index() < s.empires.size() ? &s.empire(e).race.style : nullptr;
        };
        switch (p.kind) {
            case Kind::Event: return ui.art.eventPicture(p.name);
            case Kind::Race:
                if (const std::string* st = style(p.empire)) return ui.art.racePortrait(*st);
                return {};
            case Kind::Hull: {
                const game::DesignId d{p.id};
                if (d.index() >= s.designs.size() || s.design(d).hull >= r.data().vehicleSizes.size()) return {};
                const std::string* st = style(s.design(d).owner);
                return ui.art.shipPortrait(st ? *st : std::string{}, r.hull(s.design(d).hull));
            }
            case Kind::Planet: {
                const game::ObjectId o{p.id};
                if (o.index() >= s.galaxy.objects.size()) return {};
                const game::SpaceObject& obj = s.galaxy.object(o);
                const bool there = obj.system.valid() && obj.system.index() < s.galaxy.systems.size() &&
                                   std::ranges::find(s.galaxy.system(obj.system).objects, o) != s.galaxy.system(obj.system).objects.end();
                if (!there || obj.sectorType >= r.data().sectorObjectTypes.size()) return {};
                return ui.art.planetPortrait(r.data().sectorObjectTypes[obj.sectorType].picture);
            }
            case Kind::Facility:
                if (p.id < r.data().facilities.size()) return ui.art.facilityPortrait(r.facility(p.id).picture);
                return {};
            case Kind::Group: {
                const std::string* st = style(p.empire);
                if (!st) return {};
                const char* suffix = p.name == "fighter" ? "Portrait_FighterGroup.bmp" : p.name == "mine" ? "Portrait_MineGroup.bmp"
                                                                                                          : "Portrait_SatelliteGroup.bmp";
                return ui.art.raceImage(*st, suffix, false);
            }
            case Kind::Fleet:
                if (const std::string* st = style(p.empire)) return ui.art.raceImage(*st, "Portrait_Fleet.bmp", true);
                return {};
            case Kind::Developed:
            case Kind::None: break;
        }
        return {};
    }

    // An item developed or an intelligence project now available: the pane
    // shows the item's details instead of a picture, title and date (spec 06 §4.1).
    static std::optional<ItemRef> developedItem(const UiContext& ui, const Row& row) {
        if (!row.entry) return std::nullopt;
        const game::logpicture::Parsed p = game::logpicture::parse(row.entry->picture);
        if (p.kind != game::logpicture::Parsed::Kind::Developed) return std::nullopt;
        const auto& d = ui.rules().data();
        if (p.name == "component" && p.id < d.components.size()) return ItemRef{ItemRef::Kind::Component, p.id};
        if (p.name == "facility" && p.id < d.facilities.size()) return ItemRef{ItemRef::Kind::Facility, p.id};
        if (p.name == "hull" && p.id < d.vehicleSizes.size()) return ItemRef{ItemRef::Kind::Hull, p.id};
        if (p.name == "intel" && p.id < d.intelProjects.size()) return ItemRef{ItemRef::Kind::IntelProject, p.id};
        return std::nullopt;
    }

    // The details pane (spec 06 §4.1 "Picture", confirmed: binary): the
    // selected entry's picture at the top-left corner at its own size,
    // unframed; the title in the button font to its right, from picture
    // width + 10 to 2 px short of the right edge, wrapped within the
    // picture's height; "Date:" at (4, picture height + 10) with the date at
    // x 40; the body from (4, picture height + 30), 8 px narrower than the
    // area and at most 300 px tall. An entry without a picture keeps the one
    // shown before; with none shown since the Log opened, the text starts at
    // the top left (the title at x 10).
    void details(UiContext& ui, const Row* r) {
        const game::GameState& s = ui.state();
        if (!r) return;
        if (r->index != pictureFor_) {
            // Looked up only when the selected entry changes.
            pictureFor_ = r->index;
            if (const Sprite found = pictureOf(ui, *r)) picture_ = found;
        }
        if (const auto item = developedItem(ui, *r)) {
            itemDetail(ui, *item, DetailStyle::Full);
            return;
        }
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const float areaW = ImGui::GetContentRegionAvail().x / ui.k();
        const Vec2 pic = picture_ ? picture_.size : Vec2{0, 0};
        if (picture_) drawSprite(dl, picture_, origin, {origin.x + ui.px(pic.x), origin.y + ui.px(pic.y)});
        // The title to the picture's right, wrapped within its height.
        ImFont* bold = ui.fonts.bold ? ui.fonts.bold : ImGui::GetFont();
        const float titleX = pic.x + 10;
        const float titleW = std::max(20.0f, areaW - titleX - 2);
        const ImVec2 titleAt{origin.x + ui.px(titleX), origin.y};
        const float titleH = picture_ ? pic.y : ui.px(kTitleSize) * 3.0f / ui.k();
        dl->PushClipRect(titleAt, {titleAt.x + ui.px(titleW), origin.y + ui.px(titleH)}, true);
        dl->AddText(bold, ui.fontPx(kTitleSize), titleAt, IM_COL32_WHITE, r->title.c_str(), nullptr, ui.px(titleW));
        dl->PopClipRect();
        // Without a picture, the date and the body under the title (inferred: the original's
        // places would put them on the title's line).
        const float top = picture_ ? pic.y : bold->CalcTextSizeA(ui.fontPx(kTitleSize), FLT_MAX, ui.px(titleW), r->title.c_str()).y / ui.k();
        const float dateY = top + 10;
        dl->AddText({origin.x + ui.px(4), origin.y + ui.px(dateY)}, ImGui::ColorConvertFloat4ToU32(kLabelBlue), "Date:");
        const std::string date = formatDate(r->turn);
        dl->AddText({origin.x + ui.px(40), origin.y + ui.px(dateY)}, IM_COL32_WHITE, date.c_str());
        const int combat = combatIndex(ui, r);
        ImGui::SetCursorScreenPos({origin.x + ui.px(4), origin.y + ui.px(top + 30)});
        if (combat >= 0) {
            combatDetails(ui, *r, s.combats[size_t(combat)]);
            return;
        }
        if (r->message) {
            messageDetails(ui, *r->message);
            return;
        }
        const std::string& body = r->entry ? r->entry->text : r->notice ? *r->notice : std::string{};
        if (body.empty()) return;
        ImGui::BeginChild("##body", ui.size({areaW - 8, std::min(300.0f, ImGui::GetContentRegionAvail().y / ui.k())}), ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoBackground);
        {
            // Reading text: the Text size setting enlarges it, and the body scrolls.
            const ReadingText reading(ui.painter());
            wrappedText(body);
        }
        ImGui::EndChild();
    }

    // A combat entry (spec 06 §4.1, §7 Q43): its picture, title and date as
    // every entry's, then "Combat in <system>" and the sector, then each
    // empire (flag at x 4, name at x 34) and one 20 px row per piece it had
    // at the start: the name 12 px in, cut before the Damage column at x 200
    // without an ellipsis, and the damage fixed when the battle ended, "Dead"
    // or "Taken". All of it white.
    void combatDetails(UiContext& ui, const Row& r, const game::CombatRecord& c) {
        const game::GameState& s = ui.state();
        (void)r;
        const bool known = c.location.system.index() < s.galaxy.systems.size();
        const float x = ImGui::GetCursorPosX() - ui.px(4);
        auto at = [&](float dx) { ImGui::SameLine(x + ui.px(dx)); };
        ImGui::SetCursorPosX(x + ui.px(4));
        ImGui::TextUnformatted(std::format("Combat in {}", known ? s.galaxy.system(c.location.system).name : std::string("?")).c_str());
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
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float rowH = ui.px(20);
        const float textDy = (rowH - ImGui::GetTextLineHeight()) * 0.5f;
        for (const CombatDamageRow& row : combatDamageRows(ui.rules(), s, c)) {
            const ImVec2 a = ImGui::GetCursorScreenPos();
            if (row.header) {
                if (row.empire.valid() && row.empire.index() < s.empires.size())
                    if (const Sprite flag = ui.art.flag(s.empire(row.empire).race.style, true))
                        drawSprite(dl, flag, {a.x + ui.px(4), a.y + ui.px(1)}, {a.x + ui.px(30), a.y + ui.px(19)});
                dl->AddText({a.x + ui.px(34), a.y + textDy}, IM_COL32_WHITE, row.name.c_str());
            } else {
                dl->PushClipRect({a.x + ui.px(12), a.y}, {a.x + ui.px(200), a.y + rowH}, true);
                dl->AddText({a.x + ui.px(12), a.y + textDy}, IM_COL32_WHITE, row.name.c_str());
                dl->PopClipRect();
                dl->AddText({a.x + ui.px(200), a.y + textDy}, IM_COL32_WHITE, row.damage.c_str());
            }
            ImGui::Dummy(ImVec2(ui.px(260), rowH));
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
        {
            const ReadingText reading(ui.painter());
            wrappedText(m.text.empty() ? std::string("(no text)") : m.text);
        }
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
    Sprite picture_;              // the picture the pane shows: the last entry's that had one since the Log opened
    int32_t pictureFor_ = -1;     // the entry it was looked up for (the whole log's index)
    uint8_t startFilter_ = 0;     // automation's filter for the first opening (0: the stored one)
    uint8_t filter_ = 0;          // 0 All, else the category + 1
    bool gotoLit_ = true;         // Goto is created lit and follows the entries selected (spec 06 §7 Q91)
    int selected_ = 0;            // row of the filtered list (-1: none)
    int scrollRows_ = 0;
    bool restoreScroll_ = false;
};

} // namespace

std::unique_ptr<Screen> makeLog(const ScreenArgs& args) { return std::make_unique<LogScreen>(args.text); }

} // namespace opense4::client::classic
