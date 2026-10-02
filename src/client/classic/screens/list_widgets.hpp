#pragma once

// The classic list pieces of the four list windows (Planets, Colonies,
// Ships\Units, Construction Queues; docs/spec/06 §1.8, §2.1.1, §5.4): column
// edges, the sortable column headings and the rows' height.

#include "client/classic/ui.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace opense4::client::classic {

struct ListColumn {
    const char* label;
    float width;               // frame pixels; 0 takes what is left
    uint32_t color = 0;        // a fixed heading colour (the resource columns); 0: the heading colours below
    bool sortable = true;      // false: drawn in grey and not clickable
    int icon = -1;             // an Icon drawn after the label (the resource columns' "Value"), or -1
};
// Left edges of the columns, in ImGui units from the list's left, plus the right edge.
std::vector<float> columnEdges(UiContext& ui, std::span<const ListColumn> cols, float width);
// The column headings at the cursor (spec 06 §5.4, confirmed: binary): label
// blue, brighter under the pointer and while held, both over the RowGrid.bmp
// texture; grey for a column that cannot be clicked; a resource column always
// in its colour. Over `width` ImGui units, 20 frame pixels tall; returns the
// column clicked, or -1.
int listHeader(UiContext& ui, const char* id, std::span<const ListColumn> cols, float width);
// The same headings as the first row of an ImGui table (in place of
// TableHeadersRow); `cols` gives one entry per table column. Returns the
// table column clicked, or -1.
int tableHeadings(UiContext& ui, std::span<const ListColumn> cols);
// One heading in [min, max]: the texture behind it under the pointer or while held, the label in its colour.
void drawHeading(UiContext& ui, ImDrawList* dl, ImVec2 min, ImVec2 max, const ListColumn& col, bool hovered, bool held);

// The height of a list window's rows (spec 06 §2.1.1, confirmed: binary): the
// window's height less 265 frame pixels, so 210 (five rows of 36) in a
// 475-high window. Call inside the window's content.
float listRowsHeight(UiContext& ui);
// Rows of the four list windows are 36 frame pixels tall (spec 06 §1.8).
inline constexpr float kListRowH = 36.0f;

// The heading over a picture column (observed, spec 07 session 3).
inline constexpr const char* kPicHeading = "Pic";

// ---- Lists with the arrow column ----------------------------------------------------------------
//
// The classic lists scroll with an up arrow and a down arrow in a narrow
// column at their right, not with a scroll bar (spec 06 conventions; observed
// in spec 07 session 3). The column is kListArrowW frame pixels wide, the up
// arrow at its top and the down arrow at its bottom, each kListArrowW tall; a
// click scrolls one step, holding repeats, and the mouse wheel scrolls as
// before. The arrows' size and places and the step are OpenSE4's (inferred,
// spec 06 §7 Q89).
inline constexpr float kListArrowW = 16.0f;

// Opens a list `size` big (ImGui units, as for BeginChild: 0 fills, a negative
// value leaves that much): a box (bordered unless `border` is false) that holds
// the rows' child and the arrow column. Draw the rows, then call endList().
// The rows' child takes the current WindowPadding and `rowsFlags`; `step` is
// one click's scroll in frame pixels (a row). After endList() the last item is
// the whole list, so ui.tagItem() after it tags the list.
void beginList(const Painter& p, const char* id, ImVec2 size, float step = kListRowH, ImGuiChildFlags rowsFlags = ImGuiChildFlags_None,
               bool border = true);
void endList(const Painter& p);
// The same for a table that scrolls itself (ImGuiTableFlags_ScrollY is added):
// the table takes `size` less the arrow column and draws no scroll bar. Call
// endListTable() only when this returned true, in place of EndTable().
bool beginListTable(const Painter& p, const char* id, int columns, ImGuiTableFlags flags, ImVec2 size = ImVec2(0, 0), float step = kListRowH);
void endListTable(const Painter& p);
// The step of lists of text lines: one line of the body font, in frame pixels.
inline constexpr float kListLineStep = 16.0f;
// The width the rows get in a list `width` wide (ImGui units): the arrow column taken off.
float listRowsWidth(const Painter& p, float width);
// One arrow button of the column at the cursor (`up` or down), `size` in
// frame pixels; dim and inert when `enabled` is false. Returns true on a
// click and, while held, on each repeat.
bool listArrow(const Painter& p, const char* id, bool up, Vec2 size, bool enabled);
// The same small button with a left or right arrow, or a square (a stop button).
enum class ArrowGlyph { Up, Down, Left, Right, Stop };
bool arrowButton(const Painter& p, const char* id, ArrowGlyph glyph, Vec2 size, bool enabled);

// The same in a game's windows.
inline void beginList(UiContext& ui, const char* id, ImVec2 size, float step = kListRowH, ImGuiChildFlags rowsFlags = ImGuiChildFlags_None,
                      bool border = true) {
    beginList(ui.painter(), id, size, step, rowsFlags, border);
}
inline void endList(UiContext& ui) { endList(ui.painter()); }
inline bool beginListTable(UiContext& ui, const char* id, int columns, ImGuiTableFlags flags, ImVec2 size = ImVec2(0, 0), float step = kListRowH) {
    return beginListTable(ui.painter(), id, columns, flags, size, step);
}
inline void endListTable(UiContext& ui) { endListTable(ui.painter()); }
inline float listRowsWidth(UiContext& ui, float width) { return listRowsWidth(ui.painter(), width); }
inline bool listArrow(UiContext& ui, const char* id, bool up, Vec2 size, bool enabled) { return listArrow(ui.painter(), id, up, size, enabled); }
inline bool arrowButton(UiContext& ui, const char* id, ArrowGlyph glyph, Vec2 size, bool enabled) {
    return arrowButton(ui.painter(), id, glyph, size, enabled);
}

} // namespace opense4::client::classic
