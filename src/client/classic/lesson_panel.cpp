#include "client/classic/lesson_panel.hpp"

#include <cmath>
#include <numeric>

namespace opense4::client::classic::panel {

float overlap(const Box& a, const Box& b) {
    const float w = std::min(a.max.x, b.max.x) - std::max(a.min.x, b.min.x);
    const float h = std::min(a.max.y, b.max.y) - std::max(a.min.y, b.min.y);
    return w > 0 && h > 0 ? w * h : 0.0f;
}

std::vector<Slot> flowButtons(std::span<const float> minWidths, float width, float gap) {
    std::vector<Slot> out(minWidths.size());
    size_t first = 0;
    int row = 0;
    while (first < minWidths.size()) {
        // As many as fit, at least one.
        size_t end = first + 1;
        float used = minWidths[first];
        while (end < minWidths.size() && used + gap + minWidths[end] <= width) used += gap + minWidths[end++];
        const size_t n = end - first;
        const float room = width - gap * float(n - 1);
        const float widest = *std::max_element(minWidths.begin() + std::ptrdiff_t(first), minWidths.begin() + std::ptrdiff_t(end));
        const float sum = std::accumulate(minWidths.begin() + std::ptrdiff_t(first), minWidths.begin() + std::ptrdiff_t(end), 0.0f);
        float x = 0;
        for (size_t i = first; i < end; ++i) {
            // Equal widths when every label fits in one; else each grows by its share.
            float w = widest * float(n) <= room ? room / float(n) : minWidths[i] + (room - sum) * (sum > 0 ? minWidths[i] / sum : 0.0f);
            if (n == 1) w = std::max(room, 0.0f);
            out[i] = {x, std::floor(w), row};
            x += std::floor(w) + gap;
        }
        first = end;
        ++row;
    }
    return out;
}

int rowCount(std::span<const Slot> slots) { return slots.empty() ? 0 : slots.back().row + 1; }

float hiddenShare(const Box& panel, std::span<const Box> parts) {
    float share = 0;
    for (const Box& p : parts)
        if (const float all = p.area(); all > 0) share += overlap(panel, p) / all;
    return share;
}

float spotScore(const Box& panel, const Avoid& avoid) {
    return hiddenShare(panel, avoid.targets) + 0.5f * hiddenShare(panel, avoid.allowed) + hiddenShare(panel, avoid.prompts);
}

size_t bestSpot(std::span<const Spot> spots, const Avoid& avoid, std::optional<int> previous) {
    if (spots.empty()) return 0;
    size_t best = 0;
    float bestScore = spotScore(spots[0].box, avoid);
    for (size_t i = 1; i < spots.size(); ++i)
        if (const float s = spotScore(spots[i].box, avoid); s < bestScore - 1e-4f) {
            best = i;
            bestScore = s;
        }
    if (previous)
        for (size_t i = 0; i < spots.size(); ++i)
            if (spots[i].id == *previous && spotScore(spots[i].box, avoid) <= bestScore + 0.02f) return i;
    return best;
}

Box keepInside(Box box, const Box& bounds) {
    const ImVec2 size(box.width(), box.height());
    box.min.x = std::max(bounds.min.x, std::min(box.min.x, bounds.max.x - size.x));
    box.min.y = std::max(bounds.min.y, std::min(box.min.y, bounds.max.y - size.y));
    box.max = ImVec2(box.min.x + size.x, box.min.y + size.y);
    return box;
}

} // namespace opense4::client::classic::panel
