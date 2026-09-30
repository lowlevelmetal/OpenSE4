// Log (F10): this turn's news, messages from other empires and rejected
// orders, with category filters, a details pane with the event picture, a
// mini-map, Goto, Send Reply and Combat Replay (docs/spec/06 §4.1-§4.3).

#include "client/classic/screens/empire_widgets.hpp"
#include "client/classic/screens/screens.hpp"

#include <algorithm>
#include <format>
#include <map>

namespace opense4::client::classic {

namespace {

using game::LogCategory;

constexpr int kCategories = int(LogCategory::Misc) + 1;

ImVec4 categoryColor(LogCategory c) {
    switch (c) {
        case LogCategory::Construction: return ImVec4(1.0f, 0.66f, 0.30f, 1);
        case LogCategory::Research: return ImVec4(0.70f, 0.55f, 1.0f, 1);
        case LogCategory::Intelligence: return ImVec4(0.75f, 0.78f, 0.85f, 1);
        case LogCategory::Events: return ImVec4(1.0f, 0.88f, 0.35f, 1);
        case LogCategory::Politics: return ImVec4(0.45f, 0.90f, 0.50f, 1);
        case LogCategory::Combat: return ImVec4(1.0f, 0.42f, 0.38f, 1);
        case LogCategory::Misc: return ImVec4(0.50f, 0.68f, 1.0f, 1);
    }
    return ImVec4(1, 1, 1, 1);
}

const char* filterLabel(int f) {
    if (f < 0) return "All";
    if (f == int(LogCategory::Misc)) return "Misc";
    static std::array<std::string, kCategories> names = [] {
        std::array<std::string, kCategories> a;
        for (int i = 0; i < kCategories; ++i) a[size_t(i)] = std::string(game::displayName(static_cast<LogCategory>(i)));
        return a;
    }();
    return names[size_t(f)].c_str();
}

struct Row {
    enum class Kind : uint8_t { Entry, Message, Notice };
    Kind kind = Kind::Entry;
    size_t key = 0;  // log index, message id or notice index
    uint32_t turn = 0;
    LogCategory category = LogCategory::Misc;
    std::string title;
    const game::LogEntry* entry = nullptr;
    const game::DiplomaticMessage* message = nullptr;
    const std::string* notice = nullptr;
};

// The window remembers its selection and filter while the turn lasts.
struct Memory {
    uint32_t turn = UINT32_MAX;
    Row::Kind kind = Row::Kind::Entry;
    size_t key = 0;
    int filter = -1;
    bool earlier = false;
};
Memory gMemory;

class LogScreen final : public Screen {
public:
    bool draw(UiContext& ui) override {
        const game::GameState& s = ui.state();
        if (!restored_) {
            restored_ = true;
            if (gMemory.turn == s.turn) {
                filter_ = gMemory.filter;
                earlier_ = gMemory.earlier;
                selectedKind_ = gMemory.kind;
                selectedKey_ = gMemory.key;
                haveSelection_ = true;
            }
        }
        const std::vector<Row> all = rows(ui);
        std::array<int, kCategories> counts{};
        for (const Row& r : all) ++counts[size_t(r.category)];
        std::vector<const Row*> shown;
        for (const Row& r : all)
            if (filter_ < 0 || int(r.category) == filter_) shown.push_back(&r);
        const Row* sel = nullptr;
        for (const Row* r : shown)
            if (haveSelection_ && r->kind == selectedKind_ && r->key == selectedKey_) sel = r;
        if (!sel && !shown.empty()) sel = shown.front();

        Dialog d(ui, "Log", DialogSize::Large);
        if (!d.open()) return d.keepOpen();
        d.beginContent();
        const float leftW = ui.px(330);
        ImGui::BeginChild("##left", ImVec2(leftW, 0));
        heading(ui, "Log Messages");
        ImGui::SameLine();
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - ui.px(118)));
        ImGui::Checkbox("Earlier turns", &earlier_);
        const float mapH = ui.px(230);
        ImGui::BeginChild("##list", ImVec2(0, ImGui::GetContentRegionAvail().y - mapH - ImGui::GetStyle().ItemSpacing.y),
                          ImGuiChildFlags_Borders);
        if (shown.empty()) ImGui::TextColored(kTextDim, "%s", all.empty() ? "Nothing to report this turn." : "No entries in this category.");
        for (size_t i = 0; i < shown.size(); ++i) {
            const Row& r = *shown[i];
            ImGui::PushID(int(i));
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const float lh = ImGui::GetTextLineHeight();
            ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(p.x + ui.px(5), p.y + lh * 0.5f), ui.px(3.5f),
                                                        ImGui::ColorConvertFloat4ToU32(categoryColor(r.category)));
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ui.px(14));
            const std::string label = r.turn + 1 < s.turn ? std::format("{}  {}", formatDate(r.turn), r.title) : r.title;
            if (ImGui::Selectable(label.c_str(), &r == sel)) select(r);
            if (&r == sel && scrollToSelection_) {
                ImGui::SetScrollHereY();
                scrollToSelection_ = false;
            }
            ImGui::PopID();
        }
        ImGui::EndChild();
        MiniMapStyle style;
        const std::optional<game::Location> where = location(sel);
        if (where) style.highlight.push_back(where->system);
        miniMap(ui, "##logMap", {330, 230}, style);
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("##details", ImVec2(0, 0), ImGuiChildFlags_Borders);
        details(ui, sel);
        ImGui::EndChild();

        d.beginButtons();
        if (d.tab("All", filter_ < 0)) setFilter(-1);
        for (int c = 0; c < kCategories; ++c)
            if (d.tab(filterLabel(c), filter_ == c, counts[size_t(c)] > 0)) setFilter(c);
        d.spacer();
        const game::DiplomaticMessage* msg = sel ? sel->message : nullptr;
        if (d.button("Send Reply", msg != nullptr)) {
            ScreenArgs a;
            a.empire = msg->from;
            a.index = static_cast<int>(msg->id.value);
            ui.open(ScreenId::Communicate, a);
        }
        const int combat = combatIndex(ui, sel);
        if (d.button("Combat Replay", combat >= 0)) {
            ScreenArgs a;
            a.index = combat;
            if (where) a.location = where;
            ui.open(ScreenId::CombatReplay, a);
        }
        if (d.button("Constr. Queues")) ui.open(ScreenId::Queues);
        bool close = false;
        if (d.button("Goto", where.has_value())) {
            ui.requests.focus = *where;
            close = true;
        }
        d.close();
        remember(ui, sel);
        if (close) d.requestClose();
        return d.keepOpen();
    }

private:
    std::vector<Row> rows(const UiContext& ui) {
        const game::GameState& s = ui.state();
        const game::Empire& me = ui.me();
        std::vector<Row> out;
        auto recent = [&](uint32_t turn) { return earlier_ || turn + 1 >= s.turn; };
        for (size_t i = 0; i < me.log.size(); ++i) {
            const game::LogEntry& l = me.log[i];
            if (!recent(l.turn)) continue;
            Row r;
            r.kind = Row::Kind::Entry;
            r.key = i;
            r.turn = l.turn;
            r.category = l.category;
            r.title = l.title.empty() ? std::string(game::displayName(l.category)) : l.title;
            r.entry = &l;
            out.push_back(std::move(r));
        }
        for (const game::DiplomaticMessage& m : s.messages) {
            if (m.to != me.id || !m.delivered || m.from.index() >= s.empires.size()) continue;
            if (!recent(m.sentTurn) && m.answered) continue;
            Row r;
            r.kind = Row::Kind::Message;
            r.key = m.id.value;
            r.turn = m.sentTurn;
            r.category = LogCategory::Politics;
            r.title = std::format("{}: {}", s.empire(m.from).name, game::displayName(m.type));
            r.message = &m;
            out.push_back(std::move(r));
        }
        const auto& notices = ui.session.notices();
        for (size_t i = 0; i < notices.size(); ++i) {
            Row r;
            r.kind = Row::Kind::Notice;
            r.key = i;
            r.turn = s.turn == 0 ? 0 : s.turn - 1;
            r.category = LogCategory::Misc;
            r.title = "Order not carried out";
            r.notice = &notices[i];
            out.push_back(std::move(r));
        }
        // Newest first, then in the order they happened.
        std::stable_sort(out.begin(), out.end(), [](const Row& a, const Row& b) { return a.turn > b.turn; });
        return out;
    }

    void select(const Row& r) {
        selectedKind_ = r.kind;
        selectedKey_ = r.key;
        haveSelection_ = true;
    }

    void setFilter(int f) {
        filter_ = f;
        scrollToSelection_ = true;
    }

    void remember(const UiContext& ui, const Row* sel) {
        gMemory.turn = ui.state().turn;
        gMemory.filter = filter_;
        gMemory.earlier = earlier_;
        if (sel) {
            gMemory.kind = sel->kind;
            gMemory.key = sel->key;
        }
    }

    static std::optional<game::Location> location(const Row* r) {
        if (!r) return std::nullopt;
        if (r->entry) return r->entry->location;
        if (r->message && r->message->system.valid()) return game::Location{r->message->system, {}};
        return std::nullopt;
    }

    // The battle a combat entry reports (battles of the last processed turn).
    int combatIndex(const UiContext& ui, const Row* r) const {
        if (!r || !r->entry || r->entry->category != LogCategory::Combat || !r->entry->location) return -1;
        const auto& combats = ui.state().combats;
        for (size_t i = 0; i < combats.size(); ++i)
            if (combats[i].location == *r->entry->location) return int(i);
        return -1;
    }

    void details(UiContext& ui, const Row* r) {
        const game::GameState& s = ui.state();
        heading(ui, "Log Details");
        ImGui::SameLine();
        ImGui::TextColored(kTextDim, "Game Date %s", formatDate(s.turn).c_str());
        ImGui::Separator();
        if (!r) {
            ImGui::TextColored(kTextDim, "Select an entry.");
            return;
        }
        Sprite picture;
        if (r->entry) picture = ui.art.eventPicture(r->entry->picture);
        else if (r->message) picture = ui.art.racePortrait(s.empire(r->message->from).race.style);
        else if (r->notice) picture = ui.art.eventPicture("OrdersNotCompleted");
        framedImage(ui, picture, {128, 128});
        ImGui::SameLine(0, ui.px(12));
        ImGui::BeginGroup();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::PushFont(ui.fonts.bold, ui.fontPx(kTitleSize));
        ImGui::TextUnformatted(r->title.c_str());
        ImGui::PopFont();
        ImGui::PopTextWrapPos();
        ImGui::TextColored(categoryColor(r->category), "%s", std::string(game::displayName(r->category)).c_str());
        ImGui::TextColored(kTextDim, "%s", formatDate(r->turn).c_str());
        if (const auto where = location(r)) {
            if (where->system.valid() && where->system.index() < s.galaxy.systems.size()) {
                const bool known = ui.me().hasExplored(where->system);
                ImGui::TextColored(kTextBlue, "At");
                ImGui::SameLine();
                ImGui::TextUnformatted(known ? s.galaxy.system(where->system).name.c_str() : "an unexplored system");
            }
        }
        if (r->message) {
            ImGui::TextColored(kTextBlue, "From");
            ImGui::SameLine();
            empireLabel(ui, r->message->from);
        }
        ImGui::EndGroup();
        ImGui::Spacing();
        if (r->entry) {
            if (!r->entry->text.empty()) wrappedText(r->entry->text);
            if (const int c = combatIndex(ui, r); c >= 0) combatForces(ui, s.combats[size_t(c)]);
        } else if (r->message) {
            messageDetails(ui, *r->message);
        } else if (r->notice) {
            wrappedText(*r->notice);
        }
    }

    void messageDetails(UiContext& ui, const game::DiplomaticMessage& m) {
        const game::GameState& s = ui.state();
        labelValue(ui, "Tone", std::string(toneName(m.tone)));
        if (messageNeeds(m.type).treaty || m.type == game::MessageType::AcceptTreaty) labelValue(ui, "Treaty", std::string(game::displayName(m.treaty)));
        if (m.thirdEmpire.valid() && m.thirdEmpire.index() < s.empires.size()) labelValue(ui, "About", s.empire(m.thirdEmpire).name);
        if (m.planet.valid() && m.planet.index() < s.galaxy.objects.size()) labelValue(ui, "Planet", s.galaxy.object(m.planet).name);
        ImGui::Spacing();
        wrappedText(m.text.empty() ? std::string("(no text)") : m.text);
        auto list = [&](const char* title, const std::vector<game::PackageItem>& items) {
            if (items.empty()) return;
            ImGui::Spacing();
            heading(ui, title);
            for (const auto& i : items) ImGui::BulletText("%s", packageItemText(ui.rules(), s, i).c_str());
        };
        list("They give", m.offer);
        list("They ask for", m.request);
        ImGui::Spacing();
        if (m.answered) ImGui::TextColored(kTextDim, "Answered.");
        else if (answerable(m.type)) ImGui::TextColored(kTextWarn, "Awaiting our answer: use Send Reply.");
    }

    // Combat Forces: per empire, each kind of vehicle or planet with start and lost counts.
    void combatForces(UiContext& ui, const game::CombatRecord& c) {
        const game::GameState& s = ui.state();
        std::vector<uint8_t> lost(c.pieces.size(), 0);
        for (const game::CombatEvent& ev : c.events)
            if ((ev.kind == game::CombatEvent::Kind::Destroyed || ev.kind == game::CombatEvent::Kind::Captured) && ev.piece < lost.size())
                lost[ev.piece] = 1;
        ImGui::Spacing();
        heading(ui, "Combat Forces");
        if (!ImGui::BeginTable("##forces", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) return;
        ImGui::TableSetupColumn("Vehicle", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Start", ImGuiTableColumnFlags_WidthFixed, ui.px(48));
        ImGui::TableSetupColumn("Lost", ImGuiTableColumnFlags_WidthFixed, ui.px(48));
        ImGui::TableHeadersRow();
        for (game::EmpireId e : c.participants) {
            std::map<std::string, std::pair<int, int>> counts;
            for (size_t i = 0; i < c.pieces.size(); ++i) {
                const game::CombatPiece& p = c.pieces[i];
                if (p.owner != e || p.kind == game::CombatPiece::Kind::Seeker) continue;
                std::string name = p.name;
                if (p.kind != game::CombatPiece::Kind::Planet && p.design.valid() && p.design.index() < s.designs.size())
                    name = s.design(p.design).name;
                auto& [start, gone] = counts[name];
                ++start;
                gone += lost[i];
            }
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            empireLabel(ui, e);
            for (const auto& [name, n] : counts) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Indent(ui.px(12));
                ImGui::TextUnformatted(name.c_str());
                ImGui::Unindent(ui.px(12));
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%d", n.first);
                ImGui::TableSetColumnIndex(2);
                ImGui::TextColored(n.second > 0 ? kTextBad : kTextDim, "%d", n.second);
            }
        }
        ImGui::EndTable();
        for (const std::string& line : c.summary) wrappedText(line, kTextDim);
    }

    bool restored_ = false;
    int filter_ = -1;
    bool earlier_ = false;
    bool haveSelection_ = false;
    bool scrollToSelection_ = true;
    Row::Kind selectedKind_ = Row::Kind::Entry;
    size_t selectedKey_ = 0;
};

} // namespace

std::unique_ptr<Screen> makeLog(const ScreenArgs&) { return std::make_unique<LogScreen>(); }

} // namespace opense4::client::classic
