#include "client/classic/mod_ui.hpp"

#include "client/app_settings.hpp"
#include "client/classic/screens/list_widgets.hpp"
#include "client/classic/settings.hpp"
#include "client/classic/session.hpp"
#include "client/script/items.hpp"
#include "core/log.hpp"
#include "sdk/players.hpp"

#include <algorithm>
#include <format>
#include <memory>

namespace opense4::client::classic {

namespace {

// The packages' identity and the language: what the cached ModUi was made for.
std::string modUiKey(std::span<const mods::Package> packages) {
    std::string key = settings().modLanguage;
    for (const mods::Package& p : packages) key += std::format("|{}@{}:{}", p.id(), p.hash, p.root.string());
    return key;
}

struct ModUiCache {
    std::string key;
    std::unique_ptr<ModUi> ui;
    bool actionsSet = false;
};
ModUiCache& modUiCache() {
    static ModUiCache cache;
    return cache;
}

const mods::Package* packageOf(const game::Rules& r, std::string_view mod) {
    for (const mods::Package& p : sdk::gamePackages(r))
        if (p.id() == mod) return &p;
    return nullptr;
}

} // namespace

const ModUi& modUi(const game::Rules& r) {
    ModUiCache& c = modUiCache();
    const std::span<const mods::Package> packages = sdk::gamePackages(r);
    std::string key = modUiKey(packages);
    if (!c.ui || c.key != key) {
        auto made = std::make_unique<ModUi>();
        made->ext = sdk::loadUiExtensions(packages);
        for (const mods::Package& p : packages) made->texts.add(p);
        made->texts.setLanguage(settings().modLanguage);
        for (const auto& [mod, problem] : made->ext.problems) log::warn("Interface: {}", problem);
        for (const std::string& problem : made->texts.problems()) log::warn("Interface: {}", problem);
        c.ui = std::move(made);
        c.key = std::move(key);
        c.actionsSet = false;
    }
    return *c.ui;
}

// ---- Text ------------------------------------------------------------------------------------------

std::string modText(const game::Rules& r, std::string_view mod, std::string_view key, std::string_view fallback) {
    return modUi(r).texts.get(mod, key, fallback);
}

std::string modName(const game::Rules& r, std::string_view mod) {
    const mods::Package* p = packageOf(r, mod);
    const std::string fallback = p && !p->manifest.name.empty() ? p->manifest.name : std::string(mod);
    return modText(r, mod, "mod.name", fallback);
}

std::string modOrderLabel(const game::Rules& r, std::string_view mod, const mods::ModOrderDecl& o) {
    return modText(r, mod, std::format("order.{}.label", o.name), o.label.empty() ? o.name : o.label);
}

std::string modOrderDescription(const game::Rules& r, std::string_view mod, const mods::ModOrderDecl& o) {
    return modText(r, mod, std::format("order.{}.description", o.name), o.description);
}

std::string modOptionLabel(const game::Rules& r, std::string_view mod, const mods::ModOptionDecl& o) {
    return modText(r, mod, std::format("option.{}.label", o.name), o.label.empty() ? o.name : o.label);
}

std::string modOptionDescription(const game::Rules& r, std::string_view mod, const mods::ModOptionDecl& o) {
    return modText(r, mod, std::format("option.{}.description", o.name), o.description);
}

std::string modPanelTitle(const game::Rules& r, const sdk::UiPanel& p) {
    return modText(r, p.mod, std::format("panel.{}.title", p.name), p.title.empty() ? modName(r, p.mod) : p.title);
}

std::string modRowLabel(const game::Rules& r, const sdk::UiPanel& p, const sdk::UiRow& row, size_t index) {
    const std::string name = row.name.empty() ? std::to_string(index + 1) : row.name;
    return modText(r, p.mod, std::format("panel.{}.{}", p.name, name), row.label);
}

std::string modColumnLabel(const game::Rules& r, const sdk::UiColumn& c) {
    return modText(r, c.mod, std::format("column.{}.label", c.name), c.label.empty() ? c.name : c.label);
}

std::string modPageTitle(const game::Rules& r, const sdk::UiEmpirePage& p) {
    return modText(r, p.mod, std::format("page.{}.title", p.name), p.title.empty() ? p.name : p.title);
}

std::string modPageColumnLabel(const game::Rules& r, const sdk::UiEmpirePage& p, const sdk::UiColumn& c, size_t index) {
    const std::string name = c.name.empty() ? std::to_string(index + 1) : c.name;
    return modText(r, p.mod, std::format("page.{}.{}", p.name, name), c.label);
}

// ---- Keys --------------------------------------------------------------------------------------------

std::vector<ModAction> modActionsOf(const game::Rules& r) {
    std::vector<ModAction> out;
    const ModUi& ui = modUi(r);
    for (const mods::Package& p : sdk::gamePackages(r)) {
        const std::string group = modName(r, p.id());
        if (p.tiers & mods::kTierScripts)
            for (const mods::ModOrderDecl& o : p.manifest.rules.orders) {
                const sdk::UiOrderStyle* style = ui.ext.order(p.id(), o.name);
                out.push_back(ModAction{std::format("{}:order:{}", p.id(), o.name), group, modOrderLabel(r, p.id(), o), style ? style->key : std::string()});
            }
        for (const sdk::UiPanel& panel : ui.ext.panels)
            if (panel.mod == p.id() && !panel.key.empty())
                out.push_back(ModAction{std::format("{}:panel:{}", p.id(), panel.name), group, "Show or hide the panel " + modPanelTitle(r, panel), panel.key});
        for (const sdk::UiEmpirePage& page : ui.ext.pages)
            if (page.mod == p.id() && !page.key.empty())
                out.push_back(ModAction{std::format("{}:page:{}", p.id(), page.name), group, "Empires: " + modPageTitle(r, page), page.key});
    }
    return out;
}

void useModActions(const game::Rules& r) {
    (void)modUi(r);
    ModUiCache& c = modUiCache();
    if (c.actionsSet) return;
    std::vector<ModAction> actions = modActionsOf(r);
    // A suggestion that cannot be used is said once in the log too.
    const std::vector<ModKeys> keys = resolveModKeys(appSettings().controls.bindings, appSettings().controls.modKeys, actions);
    for (size_t i = 0; i < actions.size(); ++i)
        if (!keys[i].conflict.empty()) log::info("Interface: {} suggests {} for \"{}\": not used, {}", actions[i].group, actions[i].suggested, actions[i].label, keys[i].conflict);
    setModActions(std::move(actions));
    c.actionsSet = true;
}

bool modActionPressed(std::string_view id) {
    static int frame = -1;
    static std::vector<ModKeys> keys;
    if (frame != ImGui::GetFrameCount()) {
        frame = ImGui::GetFrameCount();
        keys = resolveModKeys(appSettings().controls.bindings, appSettings().controls.modKeys, modActions());
    }
    const std::span<const ModAction> actions = modActions();
    for (size_t i = 0; i < actions.size() && i < keys.size(); ++i)
        if (actions[i].id == id) return chordPressed(keys[i].chords[0]) || chordPressed(keys[i].chords[1]);
    return false;
}

// ---- Values ------------------------------------------------------------------------------------------

namespace {

struct ValuesCache {
    uint64_t serial = 0, revision = 0;
    game::EmpireId player;
    const game::Rules* rules = nullptr;
    std::unique_ptr<sdk::UiValues> values;
    bool pending = false;
};
ValuesCache& valuesCache() {
    static ValuesCache c;
    return c;
}

} // namespace

std::vector<sdk::UiShown> modValues(UiContext& ui, std::span<const sdk::UiValueRequest> requests) {
    ValuesCache& c = valuesCache();
    if (!c.values || c.serial != ui.session.serial() || c.revision != ui.session.revision() || c.player != ui.session.player() || c.rules != &ui.rules()) {
        c.values.reset();
        c.values = std::make_unique<sdk::UiValues>(ui.rules(), ui.state(), ui.session.player(), sdk::gamePackages(ui.rules()));
        c.serial = ui.session.serial();
        c.revision = ui.session.revision();
        c.player = ui.session.player();
        c.rules = &ui.rules();
    }
    std::vector<sdk::UiShown> out = c.values->get(requests);
    c.pending = c.values->pending();
    return out;
}

bool modValuesPending() { return valuesCache().pending; }

// ---- Drawing -----------------------------------------------------------------------------------------

void modErrorBox(UiContext& ui, std::string_view what, const sdk::UiShown& failed, float width) {
    const float w = width > 0 ? width : ImGui::GetContentRegionAvail().x;
    const std::string text = std::format("{} failed: {}", what, failed.error);
    ImFont* font = ui.fonts.regular ? ui.fonts.regular : ImGui::GetFont();
    const float size = ui.fontPx(kTextSize);
    const float pad = ui.px(4);
    const ImVec2 extent = font->CalcTextSizeA(size, FLT_MAX, w - 2 * pad, text.c_str());
    const ImVec2 at = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##moderror", ImVec2(w, extent.y + 2 * pad));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(at, ImVec2(at.x + w, at.y + extent.y + 2 * pad), IM_COL32(40, 6, 6, 230));
    dl->AddRect(at, ImVec2(at.x + w, at.y + extent.y + 2 * pad), IM_COL32(255, 110, 90, 255));
    dl->AddText(font, size, ImVec2(at.x + pad, at.y + pad), IM_COL32(255, 200, 180, 255), text.c_str(), nullptr, w - 2 * pad);
    script::reportItem("mod-error:" + text);   // input scripts find it by what it says
    if (ImGui::IsItemHovered() && !failed.traceback.empty()) ImGui::SetTooltip("%s", failed.traceback.c_str());
}

std::vector<const sdk::UiPanel*> modPanelsFor(UiContext& ui, sdk::UiReport report, game::EmpireId owner) {
    std::vector<const sdk::UiPanel*> out;
    for (const sdk::UiPanel* p : modUi(ui.rules()).ext.panelsFor(report)) {
        const bool mine = owner.valid() && owner == ui.session.player();
        if (p->whose == sdk::UiWhose::Mine && !mine) continue;
        if (p->whose == sdk::UiWhose::Others && (mine || !owner.valid())) continue;
        // A rules mod's buttons need the mod in the game; the rows show anyway.
        out.push_back(p);
    }
    return out;
}

std::optional<sdk::UiButton> drawModPanels(UiContext& ui, std::span<const sdk::UiPanel* const> panels, const sdk::UiThing& thing) {
    std::optional<sdk::UiButton> clicked;
    const game::Rules& r = ui.rules();
    for (size_t pi = 0; pi < panels.size(); ++pi) {
        const sdk::UiPanel& p = *panels[pi];
        ImGui::PushID(int(pi));
        std::vector<sdk::UiValueRequest> requests;
        for (const sdk::UiRow& row : p.rows) requests.push_back({&row.source, thing});
        const std::vector<sdk::UiShown> values = modValues(ui, requests);
        const std::string title = modPanelTitle(r, p);
        const auto failed = std::find_if(values.begin(), values.end(), [](const sdk::UiShown& v) { return !v.error.empty(); });
        if (pi > 0) ImGui::Dummy(ImVec2(0, ui.px(4)));
        if (failed != values.end()) {
            // The panel's place holds what went wrong, never a part of it.
            modErrorBox(ui, std::format("{} ({})", title, modName(r, p.mod)), *failed);
            ImGui::PopID();
            continue;
        }
        heading(ui, title.c_str());
        script::reportItem("mod-panel:" + title, ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
        for (size_t i = 0; i < p.rows.size(); ++i) {
            const std::string label = modRowLabel(r, p, p.rows[i], i);
            labelValue(ui, label.c_str(), values[i].text, 120);
            script::reportItem(std::format("mod-row:{}:{}", label, values[i].text), ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
        }
        for (size_t i = 0; i < p.buttons.size(); ++i) {
            const sdk::UiButton& b = p.buttons[i];
            std::string label = b.label;
            if (const mods::Package* mod = packageOf(r, b.mod); label.empty() && mod)
                if (const mods::ModOrderDecl* o = mod->manifest.rules.order(b.order)) label = modOrderLabel(r, b.mod, *o);
            if (label.empty()) label = b.order;
            ImGui::PushID(int(i));
            if (classicButton(ui, label.c_str(), {std::min(250.0f, ImGui::GetContentRegionAvail().x / ui.k()), 22})) clicked = b;
            ImGui::PopID();
        }
        ImGui::PopID();
    }
    return clicked;
}

std::vector<std::vector<sdk::UiShown>> modColumnValues(UiContext& ui, std::span<const sdk::UiColumn* const> columns, std::span<const sdk::UiThing> things) {
    std::vector<sdk::UiValueRequest> requests;
    requests.reserve(columns.size() * things.size());
    for (const sdk::UiThing& t : things)
        for (const sdk::UiColumn* c : columns) requests.push_back({&c->source, t});
    const std::vector<sdk::UiShown> flat = modValues(ui, requests);
    std::vector<std::vector<sdk::UiShown>> out(things.size());
    for (size_t i = 0; i < things.size(); ++i)
        out[i].assign(flat.begin() + std::ptrdiff_t(i * columns.size()), flat.begin() + std::ptrdiff_t((i + 1) * columns.size()));
    return out;
}

std::vector<const sdk::UiColumn*> modColumnsFor(UiContext& ui, sdk::UiList list) { return modUi(ui.rules()).ext.columnsFor(list); }

void modCell(UiContext& ui, const sdk::UiShown& v) {
    (void)ui;
    if (v.error.empty()) {
        ImGui::TextUnformatted(v.text.c_str());
        return;
    }
    ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "error");
    script::reportItem("mod-error:" + v.error, ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", (v.error + (v.traceback.empty() ? "" : "\n" + v.traceback)).c_str());
}

ModListClick drawModList(UiContext& ui, const char* id, std::span<const ModListRow> rows, sdk::UiList list, ImVec2 size, float nameWidth) {
    ModListClick out;
    const game::Rules& r = ui.rules();
    const std::vector<const sdk::UiColumn*> columns = modColumnsFor(ui, list);
    std::vector<sdk::UiThing> things;
    for (const ModListRow& row : rows) things.push_back(row.thing);
    const std::vector<std::vector<sdk::UiShown>> values = modColumnValues(ui, columns, things);
    const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingFixedFit;
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(ui.px(2), ui.px(2)));
    if (!beginListTable(ui, id, int(columns.size()) + 2, flags, size)) {
        ImGui::PopStyleVar();
        return out;
    }
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn(kPicHeading, ImGuiTableColumnFlags_WidthFixed, ui.px(36 - 4));
    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, ui.px(nameWidth - 4));
    std::vector<std::string> labels;
    for (const sdk::UiColumn* c : columns) labels.push_back(modColumnLabel(r, *c));
    for (size_t i = 0; i < columns.size(); ++i) {
        if (i + 1 == columns.size()) ImGui::TableSetupColumn(labels[i].c_str(), ImGuiTableColumnFlags_WidthStretch, 1.0f);
        else ImGui::TableSetupColumn(labels[i].c_str(), ImGuiTableColumnFlags_WidthFixed, ui.px(float(columns[i]->width) - 4));
    }
    // The mods' columns are not sorted: their headings are grey.
    std::vector<ListColumn> headings{{kPicHeading, 0, 0, false}, {"Name", 0, 0, false}};
    for (const std::string& l : labels) headings.push_back({l.c_str(), 0, 0, false});
    (void)tableHeadings(ui, headings);
    const float rowH = ui.px(kListRowH);
    const float textH = ImGui::GetTextLineHeight();
    for (size_t i = 0; i < rows.size(); ++i) {
        const ModListRow& row = rows[i];
        ImGui::TableNextRow(ImGuiTableRowFlags_None, rowH);
        ImGui::TableSetColumnIndex(0);
        ImGui::PushID(int(i));
        const ImVec2 p0 = ImGui::GetCursorScreenPos();
        const float inner = rowH - 2.0f * ImGui::GetStyle().CellPadding.y;
        if (ImGui::Selectable("##row", false, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap, ImVec2(0, inner))) out.left = i;
        if (ImGui::IsItemHovered()) out.hovered = i;
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) out.right = i;
        ImGui::PopID();
        if (row.picture) {
            const float ps = ui.px(32);
            const ImVec2 a{p0.x + ui.px(3), p0.y + (inner - ps) * 0.5f};
            ImGui::GetWindowDrawList()->AddImage(ImTextureRef(static_cast<ImTextureID>(row.picture.tex.value)), a, ImVec2(a.x + ps, a.y + ps),
                                                 ImVec2(row.picture.uv.min.x, row.picture.uv.min.y), ImVec2(row.picture.uv.max.x, row.picture.uv.max.y));
        }
        ImGui::TableSetColumnIndex(1);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (inner - textH) * 0.5f);
        ImGui::TextUnformatted(row.name.c_str());
        for (size_t c = 0; c < columns.size(); ++c) {
            ImGui::TableSetColumnIndex(int(c) + 2);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (inner - textH) * 0.5f);
            modCell(ui, values[i][c]);
            script::reportItem(std::format("mod-cell:{}:{}:{}", row.name, labels[c], values[i][c].text), ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
        }
    }
    if (rows.empty()) {
        ImGui::TableNextRow(ImGuiTableRowFlags_None, rowH);
        ImGui::TableSetColumnIndex(1);
        ImGui::TextColored(kDimText, "Nothing to show.");
    }
    endListTable(ui);
    ImGui::PopStyleVar();
    if (modValuesPending()) ImGui::TextColored(kDimText, "Working out the mods' values...");
    return out;
}

void drawModEmpirePage(UiContext& ui, const sdk::UiEmpirePage& page, std::span<const game::EmpireId> empires) {
    const game::Rules& r = ui.rules();
    const game::GameState& s = ui.state();
    std::vector<const sdk::UiColumn*> columns;
    for (const sdk::UiColumn& c : page.columns) columns.push_back(&c);
    std::vector<sdk::UiThing> things;
    for (game::EmpireId e : empires) things.push_back({"empire", static_cast<int64_t>(e.value)});
    const std::vector<std::vector<sdk::UiShown>> values = modColumnValues(ui, columns, things);
    const std::string title = modPageTitle(r, page);
    heading(ui, title.c_str());
    script::reportItem("mod-page:" + title, ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    for (const auto& row : values)
        for (const sdk::UiShown& v : row)
            if (!v.error.empty()) {
                modErrorBox(ui, std::format("{} ({})", title, modName(r, page.mod)), v);
                return;
            }
    const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingFixedFit |
                                  ImGuiTableFlags_ScrollY;
    if (!ImGui::BeginTable("##modpage", int(columns.size()) + 1, flags, ImVec2(0, 0))) return;
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Empire", ImGuiTableColumnFlags_WidthFixed, ui.px(150));
    for (size_t i = 0; i < columns.size(); ++i)
        ImGui::TableSetupColumn(modPageColumnLabel(r, page, *columns[i], i).c_str(), ImGuiTableColumnFlags_WidthFixed, ui.px(float(columns[i]->width)));
    ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
    for (int i = 0; i <= int(columns.size()); ++i) {
        ImGui::TableSetColumnIndex(i);
        const std::string label = i == 0 ? std::string("Empire") : modPageColumnLabel(r, page, *columns[size_t(i - 1)], size_t(i - 1));
        ImGui::TextColored(kLabelBlue, "%s", label.c_str());
    }
    for (size_t row = 0; row < empires.size(); ++row) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        const game::EmpireId e = empires[row];
        const std::string name = e.index() < s.empires.size() ? s.empire(e).name : std::string("?");
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(empireColor(s, e)), "%s", name.c_str());
        for (size_t c = 0; c < columns.size(); ++c) {
            ImGui::TableSetColumnIndex(int(c) + 1);
            modCell(ui, values[row][c]);
            script::reportItem(std::format("mod-cell:{}:{}:{}", name, modPageColumnLabel(r, page, *columns[c], c), values[row][c].text),
                               ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
        }
    }
    ImGui::EndTable();
}

Sprite modOrderIcon(UiContext& ui, std::string_view mod, std::string_view order) {
    const sdk::UiOrderStyle* style = modUi(ui.rules()).ext.order(mod, order);
    if (!style || style->icon.empty()) return {};
    return ui.art.image(style->icon, false);
}

} // namespace opense4::client::classic
