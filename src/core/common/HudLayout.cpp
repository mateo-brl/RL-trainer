#include "core/common/HudLayout.h"

#include <algorithm>
#include <cmath>

namespace rlt {

namespace {

float channel(float v) {
    const float c = std::clamp(v, 0.f, 255.f) / 255.f;
    return c <= 0.03928f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

float luminance(const Color& c) { return 0.2126f * channel(c.r) + 0.7152f * channel(c.g) + 0.0722f * channel(c.b); }

struct Zone {
    float x0, y0, x1, y1;
    bool overlapsX(const HudRect& r) const { return r.x < x1 && r.x + r.w > x0; }
};

} // namespace

int HudFocus::priorityOf(const std::string& module) {
    if (module == "firsttouch") return 1;
    if (module == "mechanics") return 2;
    return 3;  // ball mechanics, shooting, kickoff, courses, maps: an explicit exercise or a precise mechanic
}

void HudFocus::note(const std::string& resultModule, float now) {
    if (resultModule.empty()) return;
    const int prio = priorityOf(resultModule);
    const bool sameModule = resultModule == module;
    const bool expired = now - time > 0.75f || now < time;  // `now` goes back when the map changes
    if (sameModule || expired || prio >= priority) {
        module = resultModule;
        priority = prio;
        time = now;
    }
}

float contrastRatio(const Color& a, const Color& b) {
    const float la = luminance(a), lb = luminance(b);
    const float hi = std::max(la, lb), lo = std::min(la, lb);
    return (hi + 0.05f) / (lo + 0.05f);
}

Color readableOnPanel(const Color& c, float bgAlpha) {
    const float k = 1.f - std::clamp(bgAlpha, 0.f, 255.f) / 255.f;
    const Color worstBackground{255.f * k, 255.f * k, 255.f * k, 255.f};
    Color out = c;
    for (int step = 1; step <= 10 && contrastRatio(out, worstBackground) < 4.5f; ++step) {
        out = mix(c, colors::White, 0.1f * static_cast<float>(step));
        out.a = c.a;
    }
    return out;
}

void styleHudPanel(DrawPanel& p, bool compact, float textSize, float opacity) {
    if (compact) {
        p.lines.erase(std::remove_if(p.lines.begin(), p.lines.end(), [](const PanelLine& l) { return l.detail; }),
                      p.lines.end());
    }
    const float size = std::clamp(textSize, 0.5f, 3.f);
    p.scale *= size;
    p.minWidth *= size;
    if (p.background.a > 0.f) p.background.a = 255.f * std::clamp(opacity, 0.f, 1.f);
    const float bg = p.background.a;
    if (bg <= 0.f) return;  // text straight on the scene: no background to measure against
    p.titleColor = readableOnPanel(p.titleColor, bg);
    for (auto& l : p.lines) l.color = readableOnPanel(l.color, bg);
}

void layoutHudPanels(std::vector<HudRect>& rects, float screenW, float screenH) {
    if (rects.empty() || screenW <= 0.f || screenH <= 0.f) return;
    const float unit = screenH / 1080.f;
    const float gap = 6.f * unit;
    const float margin = 4.f * unit;
    // Boost gauge: bottom-right corner. Scoreboard and clock: top centre.
    const Zone boost{0.80f * screenW, 0.70f * screenH, screenW, screenH};
    const Zone score{0.38f * screenW, 0.f, 0.62f * screenW, 0.11f * screenH};

    std::vector<int> column(rects.size());
    for (size_t i = 0; i < rects.size(); ++i) {
        const float cx = rects[i].x + rects[i].w * 0.5f;
        column[i] = cx < screenW / 3.f ? 0 : (cx > 2.f * screenW / 3.f ? 2 : 1);
    }

    for (int col = 0; col < 3; ++col) {
        std::vector<size_t> idx;
        for (size_t i = 0; i < rects.size(); ++i)
            if (column[i] == col) idx.push_back(i);
        if (idx.empty()) continue;
        std::stable_sort(idx.begin(), idx.end(), [&](size_t a, size_t b) { return rects[a].y < rects[b].y; });

        // Down: below the scoreboard and below the previous panel of the column.
        float prevBottom = -1e9f;
        for (size_t i : idx) {
            HudRect& r = rects[i];
            r.x = std::clamp(r.x, margin, std::max(margin, screenW - margin - r.w));
            float top = std::max(r.y, margin);
            if (score.overlapsX(r) && top < score.y1 + gap) top = score.y1 + gap;
            r.y = std::max(top, prevBottom + gap);
            prevBottom = r.y + r.h;
        }
        // Up: above the boost gauge and the bottom edge, pushing the panels above when needed.
        float limit = screenH - margin;
        for (auto it = idx.rbegin(); it != idx.rend(); ++it) {
            HudRect& r = rects[*it];
            float bottom = limit;
            if (boost.overlapsX(r)) bottom = std::min(bottom, boost.y0 - gap);
            if (r.y + r.h > bottom) r.y = bottom - r.h;
            r.y = std::max(r.y, margin);
            limit = r.y - gap;
        }
    }
}

} // namespace rlt
