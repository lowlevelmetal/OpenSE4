#include "client/ui/bitmap_font.hpp"

#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <deque>

namespace opense4::client {

namespace {

// The parsed font rides in FontData (ImGui reserves FontLoaderData for the loader's own use after init).
const assets::BitmapFont& fontOf(const ImFontConfig* src) { return *static_cast<const assets::BitmapFont*>(src->FontData); }

const assets::BitmapGlyph* glyphFor(const assets::BitmapFont& f, ImWchar codepoint) {
    const uint8_t b = assets::unicodeToCp1252(char32_t(codepoint));
    if (b == 0 || b < f.firstChar || b > f.lastChar) return nullptr;
    return f.glyph(b);
}

// Font units (source pixels) to logical and physical pixels for one baked size.
struct Scale {
    float logical;   // per source pixel, in ImGui units
    float physical;  // per source pixel, in texture pixels
    float density;
};
Scale scaleFor(const ImFontConfig* src, const ImFontBaked* baked) {
    const float logical = baked->Size / bitmapFontNominalSize(fontOf(src));
    const float density = src->RasterizerDensity * baked->RasterizerDensity;
    return {logical, logical * density, density};
}

bool containsGlyph(ImFontAtlas*, ImFontConfig* src, ImWchar codepoint) { return glyphFor(fontOf(src), codepoint) != nullptr; }

bool bakedInit(ImFontAtlas*, ImFontConfig* src, ImFontBaked* baked, void*) {
    if (src->MergeMode) return true;
    const assets::BitmapFont& f = fontOf(src);
    const Scale s = scaleFor(src, baked);
    baked->Ascent = std::ceil(float(f.ascent - f.internalLeading) * s.logical);
    baked->Descent = -std::ceil(float(f.pixelHeight - f.ascent) * s.logical);
    return true;
}

bool loadGlyph(ImFontAtlas* atlas, ImFontConfig* src, ImFontBaked* baked, void*, ImWchar codepoint, ImFontGlyph* out, float* outAdvance) {
    const assets::BitmapFont& f = fontOf(src);
    const assets::BitmapGlyph* g = glyphFor(f, codepoint);
    if (!g) return false;
    const Scale s = scaleFor(src, baked);
    // Whole physical pixels keep every glyph on the pixel grid.
    const int w = std::max(g->width > 0 ? 1 : 0, int(std::lround(float(g->width) * s.physical)));
    const int h = std::max(1, int(std::lround(float(f.pixelHeight) * s.physical)));
    const float advance = float(w) / s.density;
    if (outAdvance) {
        *outAdvance = advance;
        return true;
    }
    out->Codepoint = codepoint;
    out->AdvanceX = advance;
    if (w == 0) return true;

    const ImFontAtlasRectId id = ImFontAtlasPackAddRect(atlas, w, h);
    if (id == ImFontAtlasRectId_Invalid) return false;
    ImTextureRect* r = ImFontAtlasPackGetRect(atlas, id);
    ImVector<unsigned char>& buf = atlas->Builder->TempBuffer;
    buf.resize(w * h);
    std::memset(buf.Data, 0, size_t(w * h));

    // Area coverage: each destination pixel averages the source pixels it overlaps.
    const float sx = float(g->width) / float(w), sy = float(f.pixelHeight) / float(h);
    bool any = false;
    for (int dy = 0; dy < h; ++dy) {
        const float y0 = float(dy) * sy, y1 = y0 + sy;
        for (int dx = 0; dx < w; ++dx) {
            const float x0 = float(dx) * sx, x1 = x0 + sx;
            float ink = 0.0f;
            for (int yy = int(y0); yy < f.pixelHeight && float(yy) < y1; ++yy) {
                const float oy = std::min(y1, float(yy + 1)) - std::max(y0, float(yy));
                if (oy <= 0) continue;
                for (int xx = int(x0); xx < g->width && float(xx) < x1; ++xx) {
                    const float ox = std::min(x1, float(xx + 1)) - std::max(x0, float(xx));
                    if (ox > 0 && f.ink(*g, xx, yy)) ink += ox * oy;
                }
            }
            const float a = std::clamp(ink / (sx * sy), 0.0f, 1.0f);
            if (a > 0) any = true;
            buf.Data[dy * w + dx] = static_cast<unsigned char>(std::lround(a * 255.0f));
        }
    }

    // The glyph box starts the internal leading above the line top.
    const float top = -std::round(float(f.internalLeading) * s.physical) / s.density;
    out->X0 = 0;
    out->Y0 = top;
    out->X1 = float(w) / s.density;
    out->Y1 = top + float(h) / s.density;
    out->Visible = any;
    out->PackId = id;
    ImFontAtlasBakedSetFontGlyphBitmap(atlas, baked, src, out, r, buf.Data, ImTextureFormat_Alpha8, w);
    return true;
}

const ImFontLoader* loader() {
    static ImFontLoader l = [] {
        ImFontLoader x;
        x.Name = "bitmap font";
        x.FontSrcContainsGlyph = containsGlyph;
        x.FontBakedInit = bakedInit;
        x.FontBakedLoadGlyph = loadGlyph;
        return x;
    }();
    return &l;
}

} // namespace

float bitmapFontNominalSize(const assets::BitmapFont& font) { return float(std::max(1, font.pixelHeight - font.internalLeading)); }

ImFont* addBitmapFont(ImFontAtlas* atlas, assets::BitmapFont font) {
    static std::deque<assets::BitmapFont> keep;  // ImGui keeps a pointer for the atlas's lifetime
    const assets::BitmapFont& f = keep.emplace_back(std::move(font));
    ImFontConfig cfg;
    cfg.FontLoader = loader();
    cfg.FontData = const_cast<assets::BitmapFont*>(&f);
    cfg.FontDataSize = int(sizeof(assets::BitmapFont));
    cfg.FontDataOwnedByAtlas = false;
    cfg.SizePixels = bitmapFontNominalSize(f);
    cfg.PixelSnapH = true;
    ImFormatString(cfg.Name, IM_ARRAYSIZE(cfg.Name), "%s", f.face.c_str());
    return atlas->AddFont(&cfg);
}

} // namespace opense4::client
