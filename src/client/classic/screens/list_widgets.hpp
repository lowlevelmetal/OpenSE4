#pragma once

// The classic list pieces of the four list windows (Planets, Colonies,
// Ships\Units, Construction Queues; docs/spec/06 §1.8, §2.1.1, §5.4): column
// edges, the sortable column headings and the rows' height.

#include "client/classic/ui.hpp"

#include <cstdint>
#include <span>
#include <string_view>
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
// Every standard list is a box with a 1 px #647EC7 frame; its rows take (2,2)
// to (W − 28, H − 2) and a 24 px column from x W − 26 holds the up arrow at
// its top (y 2), the down arrow at its bottom (y H − 26), both 24×24 cells of
// `Game/Buttons/Arrows.bmp` (column 1 up, column 0 down; row 0 normal, 1 under
// the pointer, 2 held, 3 dim when the list cannot scroll that way), and
// between them a track with a thumb: an outlined box in the frame colour
// (#2D2D2D in a disabled list), as long as the visible share of the rows
// (never under 6 px), placed by the scroll position. One click on an arrow
// scrolls one row; holding it repeats every 100 ms; pressing or dragging in
// the track scrolls straight there; the mouse wheel scrolls a row per notch
// (spec 06 §7 Q89, confirmed: binary).
//
// In a game the column's parts are UI tags: `<window id>:<list id>:up`,
// `:down`, `:track` and `:thumb` for every list (the list id without its
// "##"), and `<tag>:up` ... for a list the window tags `<tag>` right after
// endList().
inline constexpr float kListArrowW = 24.0f;
// The box's frame and the gap between the rows and the column, frame pixels:
// the rows start 2 px inside the box and end 28 px from its right edge.
inline constexpr float kListInset = 2.0f;
inline constexpr float kListColumnW = 28.0f;

// Opens a list `size` big (ImGui units, as for BeginChild: 0 fills, a negative
// value leaves that much): a box (framed unless `border` is false) that holds
// the rows' child and the arrow column. Draw the rows, then call endList().
// The rows' child takes the current WindowPadding and `rowsFlags`; `step` is
// one click's scroll in frame pixels (a row). After endList() the last item is
// the whole list, so ui.tagItem() after it tags the list.
void beginList(const Painter& p, const char* id, ImVec2 size, float step = kListRowH, ImGuiChildFlags rowsFlags = ImGuiChildFlags_None,
               bool border = true);
void endList(const Painter& p);
// The same for a table that scrolls itself (ImGuiTableFlags_ScrollY is added,
// the outer borders are the box's): the table takes the rows' place and draws
// no scroll bar. Call endListTable() only when this returned true, in place
// of EndTable().
bool beginListTable(const Painter& p, const char* id, int columns, ImGuiTableFlags flags, ImVec2 size = ImVec2(0, 0), float step = kListRowH);
void endListTable(const Painter& p);
// The step of lists of text lines: one line of the body font, in frame pixels.
inline constexpr float kListLineStep = 16.0f;
// The width the rows get in a list `width` wide (ImGui units): the arrow column taken off.
float listRowsWidth(const Painter& p, float width);
// One arrow button of a list's column at the cursor (`up` or down), 24×24
// from Arrows.bmp; dim and inert when `enabled` is false. Returns the number
// of steps to scroll this frame: 1 on the click, then 1 every 100 ms while held.
int listArrow(const Painter& p, const char* id, bool up, bool enabled);
// A small outlined button of OpenSE4's drawing with an arrow or a square (a
// stop button), `size` in frame pixels; dim and inert when `enabled` is false.
// True on a click and, while held, on each repeat.
enum class ArrowGlyph { Up, Down, Left, Right, Stop };
bool arrowButton(const Painter& p, const char* id, ArrowGlyph glyph, Vec2 size, bool enabled);

// In a tutorial: call right after drawing a row of a scrolling list or table
// (the row is the last item). When the active step names the row
// (UiContext::lessonRows, its {design:<type>} tokens) and the list shows it
// for the first time during that step, a row out of view is scrolled to the
// middle of the list. `done` keeps the step that was done for (one per list),
// so the player can scroll away again (docs/LEARNING.md "Text tokens").
void lessonRow(UiContext& ui, std::string_view name, uint64_t& done);

// The same in a game's windows, which also tag the column's parts.
inline Painter listPainter(UiContext& ui) {
    Painter p = ui.painter();
    p.tagger = &ui;
    return p;
}
inline void beginList(UiContext& ui, const char* id, ImVec2 size, float step = kListRowH, ImGuiChildFlags rowsFlags = ImGuiChildFlags_None,
                      bool border = true) {
    beginList(listPainter(ui), id, size, step, rowsFlags, border);
}
inline void endList(UiContext& ui) { endList(listPainter(ui)); }
inline bool beginListTable(UiContext& ui, const char* id, int columns, ImGuiTableFlags flags, ImVec2 size = ImVec2(0, 0), float step = kListRowH) {
    return beginListTable(listPainter(ui), id, columns, flags, size, step);
}
inline void endListTable(UiContext& ui) { endListTable(listPainter(ui)); }
inline float listRowsWidth(UiContext& ui, float width) { return listRowsWidth(ui.painter(), width); }
inline int listArrow(UiContext& ui, const char* id, bool up, bool enabled) { return listArrow(ui.painter(), id, up, enabled); }
inline bool arrowButton(UiContext& ui, const char* id, ArrowGlyph glyph, Vec2 size, bool enabled) {
    return arrowButton(ui.painter(), id, glyph, size, enabled);
}

} // namespace opense4::client::classic
