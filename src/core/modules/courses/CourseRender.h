#pragma once
// Rendering helpers of the courses: level of detail of a ring from its projected size, and the rim of a ring
// (or the frame of a gate) as 3D lines, arches standing on the floor or a wall clipped at that surface.
// The renderer clips every line at the camera near plane and handles the ball occluder.

#include "core/common/DrawList.h"
#include "core/modules/courses/CourseGeom.h"

namespace rlt::courses {

// Projected radius in pixels: R * focal / depth, focal = (W/2) / tan(hfov/2).
float projectedRadiusPx(float radius, float depth, float hfovDeg, float screenW);
// Polyline segment count keeping the chord-to-arc gap under maxErrPx: N >= pi sqrt(r / (2 e)), clamped.
int circleSegments(float radiusPx, float maxErrPx = 1.f, int nMin = 8, int nMax = 64);
// Signed height of a point above the surface a ring stands on (Air: always positive).
float heightAboveSurface(const Ring& r, const Vec3& p);
// Appends the visible rim (disk) or frame (gate) of the ring. Returns the number of lines added.
int appendRingLines(const Ring& r, int segments, Color c, float width, const Occluder& occ, DrawList& out);
// Dashed line from a to b (`pieces` pieces, every other one drawn). Returns the number of lines added.
int appendDashedLine(const Vec3& a, const Vec3& b, int pieces, Color c, float width, DrawList& out);

} // namespace rlt::courses
