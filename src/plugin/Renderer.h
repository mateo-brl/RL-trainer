#pragma once
// Executes a core DrawList on the BakkesMod canvas: projects 3D lines, clips them against the camera near
// plane, hides parts occluded by a sphere, lays out HUD panels with real text measurements.

#include <string>
#include <vector>

#include "core/common/Context.h"
#include "core/common/DrawList.h"
#include "plugin/Sdk.h"

namespace rlt {

class Renderer {
public:
    void render(CanvasWrapper& canvas, const DrawList& list, const ViewContext& view, bool hudAccents);

private:
    void line3D(CanvasWrapper& canvas, const DrawLine3D& l, const ViewContext& view);
    void drawLineClipped(CanvasWrapper& canvas, Vec3 a, Vec3 b, const ViewContext& view, float width);
    struct PanelMetrics {
        float scale = 1.f, pad = 0.f, barH = 0.f, titleH = 0.f, width = 0.f, height = 0.f;
        std::string title;
        std::vector<std::string> lines;
        std::vector<float> lineH;
    };
    PanelMetrics measurePanel(CanvasWrapper& canvas, const DrawPanel& p, bool hudAccents) const;
    void drawPanel(CanvasWrapper& canvas, const DrawPanel& p, const PanelMetrics& m, float x, float y) const;
    void panels(CanvasWrapper& canvas, const std::vector<DrawPanel>& list, const ViewContext& view, bool hudAccents) const;
    void text(CanvasWrapper& canvas, Vec2 pos, const std::string& s, const Color& c, float scale, bool shadow, Align align,
              bool hudAccents);
};

} // namespace rlt
