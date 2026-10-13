#pragma once
// Executes a core DrawList on the BakkesMod canvas: projects 3D lines, clips them against the camera near
// plane, hides parts occluded by a sphere, lays out HUD panels with real text measurements.

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
    void panel(CanvasWrapper& canvas, const DrawPanel& p, bool hudAccents);
    void text(CanvasWrapper& canvas, Vec2 pos, const std::string& s, const Color& c, float scale, bool shadow, Align align,
              bool hudAccents);
};

} // namespace rlt
