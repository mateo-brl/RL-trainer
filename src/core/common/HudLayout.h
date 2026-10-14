#pragma once
// HUD panel policy shared by the engine (pure, tested) and the renderer: which module may show its panels,
// compact filtering, readable colours, and a layout that stacks panels without overlap and keeps them off the
// boost gauge and the scoreboard.

#include <string>
#include <vector>

#include "core/common/DrawList.h"

namespace rlt {

// Which module's panels the focused HUD shows when no activity runs: the one that produced the latest result.
// A flick is also a touch, so a more specific analysis (mechanics, ball mechanics...) is not replaced by the
// first-touch result that lands right after it (0.75 s window).
struct HudFocus {
    std::string module;
    int priority = 0;
    float time = -1e9f;

    void note(const std::string& resultModule, float now);
    void clear() { *this = HudFocus{}; }
    static int priorityOf(const std::string& module);
};

// Applies the general HUD options to a panel: drops `detail` lines in compact mode, sets the background opacity
// (0..1), multiplies the text size, and lightens text colours too dark to read on the panel.
void styleHudPanel(DrawPanel& p, bool compact, float textSize, float opacity);

// WCAG contrast ratio between two opaque colours (alpha ignored), 1..21.
float contrastRatio(const Color& a, const Color& b);

// Lightens `c` towards white until it reaches 4.5:1 against the panel background in the worst case: black at
// `bgAlpha` (0..255) over a white scene. The alpha of `c` is kept.
Color readableOnPanel(const Color& c, float bgAlpha);

struct HudRect {
    float x = 0.f, y = 0.f, w = 0.f, h = 0.f;
};

// Moves measured panel rectangles (in pixels, top-left origin) so that panels of the same screen column (left,
// centre, right third) do not overlap, nothing covers the boost gauge (bottom right) or the scoreboard (top
// centre), and everything stays on screen when it fits. Panels keep their order inside a column.
void layoutHudPanels(std::vector<HudRect>& rects, float screenW, float screenH);

} // namespace rlt
