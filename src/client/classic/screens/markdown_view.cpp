#include "client/classic/screens/markdown_view.hpp"

#include "client/script/items.hpp"

#include <algorithm>
#include <cmath>
#include <format>

namespace opense4::client::classic {

namespace {

using learn::Align;
using learn::Block;
using learn::Inline;
using learn::Span;

constexpr uint32_t kBody = 0xe4e4e4;
constexpr uint32_t kItalic = 0xe8d898;
constexpr uint32_t kCodeBg = 0x101c40;
constexpr uint32_t kLink = 0x8fd0ff;
constexpr uint32_t kLinkHot = 0xd8f0ff;
constexpr uint32_t kTipBg = 0x0a1430;

// A piece of a word in one style, and its place once laid out.
struct Frag {
    std::string_view text;
    const Span* span = nullptr;
    float width = 0;
    ImVec2 at;
};
struct Word {
    std::vector<Frag> frags;
    float width = 0;
    bool spaceBefore = false;
};

class Renderer {
public:
    Renderer(const Painter& p, MarkdownOptions& options) : p_(p), opt_(options) {
        body_ = p.fonts.readingFont();  // OpenSE4's own text font (docs/spec/06 §5.4)
        size_ = p.textPx(kTextSize);
        ImGui::PushFont(body_, size_);
        lineH_ = ImGui::GetTextLineHeight() + p.px(2);
        space_ = body_->CalcTextSizeA(size_, FLT_MAX, 0.0f, " ").x;
        ImGui::PopFont();
        boldOffset_ = std::max(1.0f, std::round(size_ / 13.0f));
    }

    std::optional<std::string> clicked;

    void blocks(const std::vector<Block>& list, float indent, bool inTip = false) {
        for (size_t i = 0; i < list.size(); ++i) {
            if (i > 0) gap(list[i]);
            block(list[i], indent, inTip);
        }
    }

private:
    float width() const { return ImGui::GetContentRegionAvail().x; }

    void gap(const Block& b) {
        const float g = b.kind == Block::Kind::Heading ? (b.level <= 2 ? lineH_ * 0.9f : lineH_ * 0.6f) : lineH_ * 0.45f;
        ImGui::Dummy(ImVec2(0, g));
    }

    void block(const Block& b, float indent, bool inTip) {
        switch (b.kind) {
            case Block::Kind::Heading: heading(b, indent); break;
            case Block::Kind::Paragraph: paragraph(b.text, indent, imColor(kBody)); break;
            case Block::Kind::List: list(b.list, indent); break;
            case Block::Kind::Table:
                // Tables use draw channels, which a tip box already splits.
                if (!inTip) table(b.table, indent);
                break;
            case Block::Kind::Tip:
                if (!inTip) tip(b, indent);
                break;
        }
    }

    void heading(const Block& b, float indent) {
        if (!opt_.scrollTo.empty() && b.anchor == opt_.scrollTo) {
            ImGui::SetScrollHereY(0.0f);
            opt_.scrollTo.clear();
        }
        ImFont* font = b.level <= 2 ? p_.fonts.bold : body_;
        const float size = b.level <= 2 ? p_.textPx(kTitleSize) : size_;
        const uint32_t color = b.level == 1 ? 0xffffff : b.level == 2 ? palette::kLabel : palette::kHeading;
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const float w = width() - indent;
        const float h = flow(b.text, font, size, imColor(color), b.level == 3, Align::Left, pos.x + indent, w, pos.y);
        ImGui::Dummy(ImVec2(w, h));
        // Input scripts see which sections are in view (anchor:<id>).
        if (!b.anchor.empty() && script::collectingItems()) script::reportItem("anchor:" + b.anchor);
        if (b.level == 1) {
            const ImVec2 at = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddLine(ImVec2(at.x + indent, at.y), ImVec2(at.x + indent + w, at.y), imColor(palette::kFrame), 1.0f);
            ImGui::Dummy(ImVec2(0, p_.px(3)));
        }
    }

    void paragraph(const Inline& text, float indent, ImU32 color) {
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const float w = std::max(p_.px(40), width() - indent);
        const float h = flow(text, body_, size_, color, false, Align::Left, pos.x + indent, w, pos.y);
        ImGui::Dummy(ImVec2(w, h));
    }

    void list(const learn::List& l, float indent) {
        const float step = p_.px(20);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        int number = l.start;
        for (const learn::ListItem& item : l.items) {
            const ImVec2 pos = ImGui::GetCursorScreenPos();
            if (l.numbered) {
                const std::string n = std::format("{}.", number++);
                const float nw = body_->CalcTextSizeA(size_, FLT_MAX, 0.0f, n.c_str()).x;
                dl->AddText(body_, size_, ImVec2(pos.x + indent + step - nw - p_.px(4), pos.y), imColor(palette::kLabel), n.c_str());
            } else {
                const ImVec2 dot(pos.x + indent + step * 0.45f, pos.y + lineH_ * 0.45f);
                dl->AddCircleFilled(dot, std::max(1.5f, p_.px(2.2f)), imColor(palette::kLabel));
            }
            paragraph(item.text, indent + step, imColor(kBody));
            for (const learn::List& sub : item.sub) list(sub, indent + step);
        }
    }

    void tip(const Block& b, float indent) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 start = ImGui::GetCursorScreenPos();
        const float w = width() - indent;
        dl->ChannelsSplit(2);
        dl->ChannelsSetCurrent(1);
        ImGui::Dummy(ImVec2(0, p_.px(4)));
        blocks(b.tip, indent + p_.px(12), true);
        ImGui::Dummy(ImVec2(0, p_.px(4)));
        const float end = ImGui::GetCursorScreenPos().y - ImGui::GetStyle().ItemSpacing.y;
        dl->ChannelsSetCurrent(0);
        const ImVec2 a{start.x + indent, start.y}, z{start.x + indent + w, end};
        dl->AddRectFilled(a, z, imColor(kTipBg));
        dl->AddRectFilled(a, ImVec2(a.x + p_.px(3), z.y), imColor(palette::kLabel));
        dl->ChannelsMerge();
    }

    void table(const learn::Table& t, float indent) {
        const size_t columns = t.header.size();
        if (columns == 0) return;
        // Columns share the width in proportion to their widest cell.
        std::vector<float> natural(columns, p_.px(30));
        auto measure = [&](const Inline& cell, size_t c) {
            float w = 0;
            for (const Span& s : cell) w += body_->CalcTextSizeA(size_, FLT_MAX, 0.0f, s.text.data(), s.text.data() + s.text.size()).x;
            natural[c] = std::max(natural[c], std::min(w, p_.px(320)));
        };
        for (size_t c = 0; c < columns; ++c) measure(t.header[c], c);
        for (const auto& row : t.rows)
            for (size_t c = 0; c < columns && c < row.size(); ++c) measure(row[c], c);
        if (indent > 0) ImGui::Indent(indent);
        ImGui::PushID(&t);
        if (ImGui::BeginTable("##table", static_cast<int>(columns), ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchProp)) {
            for (size_t c = 0; c < columns; ++c)
                ImGui::TableSetupColumn(std::format("##c{}", c).c_str(), ImGuiTableColumnFlags_WidthStretch, natural[c]);
            auto cell = [&](const Inline& text, Align align, ImU32 color, bool bold) {
                ImGui::TableNextColumn();
                const ImVec2 pos = ImGui::GetCursorScreenPos();
                const float w = std::max(p_.px(10), ImGui::GetContentRegionAvail().x);
                const float h = flow(text, body_, size_, color, bold, align, pos.x, w, pos.y);
                ImGui::Dummy(ImVec2(w, h));
            };
            ImGui::TableNextRow();
            ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, imColor(0x101c40));
            for (size_t c = 0; c < columns; ++c) cell(t.header[c], t.align[c], imColor(palette::kLabel), true);
            for (const auto& row : t.rows) {
                ImGui::TableNextRow();
                for (size_t c = 0; c < columns; ++c) cell(c < row.size() ? row[c] : Inline{}, t.align[c], imColor(kBody), false);
            }
            ImGui::EndTable();
        }
        ImGui::PopID();
        if (indent > 0) ImGui::Unindent(indent);
    }

    // Breaks the text into words (runs of non-space characters, styles mixed;
    // spaces inside `code` do not break).
    static std::vector<Word> words(const Inline& text) {
        std::vector<Word> out;
        bool space = false;
        bool open = false;
        for (const Span& s : text) {
            const std::string_view t = s.text;
            size_t i = 0;
            while (i < t.size()) {
                if (t[i] == ' ' && !s.code) {
                    space = true;
                    open = false;
                    ++i;
                    continue;
                }
                size_t j = i;
                while (j < t.size() && (s.code || t[j] != ' ')) ++j;
                if (!open) {
                    out.push_back(Word{{}, 0, space && !out.empty()});
                    open = true;
                    space = false;
                }
                out.back().frags.push_back(Frag{t.substr(i, j - i), &s, 0, {}});
                i = j;
            }
        }
        return out;
    }

    ImU32 colorOf(const Span& s, ImU32 base, bool hot) const {
        if (!s.link.empty()) {
            if (opt_.canFollow && !opt_.canFollow(learn::parseLink(s.link))) return imColor(palette::kDim);
            return imColor(hot ? kLinkHot : kLink);
        }
        if (s.code) return imColor(palette::kLabel);
        if (s.italic) return imColor(kItalic);
        return base;
    }

    // Lays the text out in lines `width` wide from (x0, y) and draws it.
    // Returns the height used.
    float flow(const Inline& text, ImFont* font, float size, ImU32 base, bool bold, Align align, float x0, float width, float y) {
        std::vector<Word> ws = words(text);
        const float space = font == body_ && size == size_ ? space_ : font->CalcTextSizeA(size, FLT_MAX, 0.0f, " ").x;
        ImGui::PushFont(font, size);
        const float lineH = std::max(lineH_, ImGui::GetTextLineHeight() + p_.px(2));
        ImGui::PopFont();
        auto fragWidth = [&](const Frag& f) {
            float w = font->CalcTextSizeA(size, FLT_MAX, 0.0f, f.text.data(), f.text.data() + f.text.size()).x;
            if (bold || f.span->bold) w += boldOffset_;
            if (f.span->code) w += p_.px(4);
            return w;
        };
        // Lines of words.
        std::vector<std::pair<size_t, size_t>> lines;   // [first, last) word
        std::vector<float> lineWidth;
        float x = 0;
        size_t first = 0;
        for (size_t i = 0; i < ws.size(); ++i) {
            Word& w = ws[i];
            for (Frag& f : w.frags) {
                f.width = fragWidth(f);
                w.width += f.width;
            }
            const float lead = (i > first && w.spaceBefore) ? space : 0.0f;
            if (i > first && x + lead + w.width > width) {
                lines.emplace_back(first, i);
                lineWidth.push_back(x);
                first = i;
                x = w.width;
            } else {
                x += lead + w.width;
            }
        }
        if (first < ws.size() || ws.empty()) {
            lines.emplace_back(first, ws.size());
            lineWidth.push_back(x);
        }
        // Places, then the link under the mouse.
        for (size_t l = 0; l < lines.size(); ++l) {
            float cx = x0 + (align == Align::Center ? (width - lineWidth[l]) * 0.5f : align == Align::Right ? width - lineWidth[l] : 0.0f);
            const float cy = y + float(l) * lineH;
            for (size_t i = lines[l].first; i < lines[l].second; ++i) {
                if (i > lines[l].first && ws[i].spaceBefore) cx += space;
                for (Frag& f : ws[i].frags) {
                    f.at = ImVec2(cx, cy);
                    cx += f.width;
                }
            }
        }
        // Input scripts find a link by its target ("link:economy#minerals").
        if (script::collectingItems())
            for (const Word& w : ws)
                for (const Frag& f : w.frags)
                    if (!f.span->link.empty()) script::reportItem("link:" + f.span->link, f.at, ImVec2(f.at.x + f.width, f.at.y + lineH));
        const std::string* hot = nullptr;
        if (ImGui::IsWindowHovered())
            for (const Word& w : ws)
                for (const Frag& f : w.frags)
                    if (!f.span->link.empty() && ImGui::IsMouseHoveringRect(f.at, ImVec2(f.at.x + f.width, f.at.y + lineH))) hot = &f.span->link;
        if (hot) {
            const bool follow = !opt_.canFollow || opt_.canFollow(learn::parseLink(*hot));
            if (follow) {
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) clicked = *hot;
            } else {
                ImGui::SetTooltip("%s", opt_.cannotFollow.c_str());
            }
        }
        // Drawing.
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float textH = size;
        for (const Word& w : ws)
            for (const Frag& f : w.frags) {
                const Span& s = *f.span;
                const bool isHot = hot && s.link == *hot;
                const ImU32 color = colorOf(s, base, isHot);
                float tx = f.at.x;
                if (s.code) {
                    dl->AddRectFilled(ImVec2(f.at.x, f.at.y), ImVec2(f.at.x + f.width, f.at.y + textH + p_.px(1)), imColor(kCodeBg));
                    tx += p_.px(2);
                }
                const char* b = f.text.data();
                const char* e = b + f.text.size();
                dl->AddText(font, size, ImVec2(tx, f.at.y), color, b, e);
                if (bold || s.bold) dl->AddText(font, size, ImVec2(tx + boldOffset_, f.at.y), color, b, e);
                if (isHot) dl->AddLine(ImVec2(f.at.x, f.at.y + textH), ImVec2(f.at.x + f.width, f.at.y + textH), color, 1.0f);
            }
        return float(lines.size()) * lineH;
    }

    const Painter& p_;
    MarkdownOptions& opt_;
    ImFont* body_ = nullptr;
    float size_ = 13.0f;
    float lineH_ = 16.0f;
    float space_ = 4.0f;
    float boldOffset_ = 1.0f;
};

} // namespace

std::optional<std::string> drawMarkdown(const Painter& p, const std::vector<learn::Block>& blocks, MarkdownOptions& options) {
    Renderer r(p, options);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, 0.0f));
    r.blocks(blocks, 0.0f);
    ImGui::PopStyleVar();
    return r.clicked;
}

} // namespace opense4::client::classic
