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

} // namespace opense4::client::classic
