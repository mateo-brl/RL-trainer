#include "core/modules/courses/CourseGen.h"

#include <algorithm>
#include <cstring>

namespace rlt::courses {

double rngUniform01(Rng& rng) {
    const uint64_t hi = rng.next() >> 5;  // 27 bits
    const uint64_t lo = rng.next() >> 6;  // 26 bits
    return static_cast<double>((hi << 26) | lo) * (1.0 / 9007199254740992.0);
}

double rngRange(Rng& rng, double a, double b) { return a + (b - a) * rngUniform01(rng); }

uint64_t splitmix64(uint64_t x) { return mix64(x); }

DVec3 quantize(const DVec3& v) { return {std::round(v.x), std::round(v.y), std::round(v.z)}; }

double maxGroundCurvature(double v) {
    static constexpr double tbl[6][2] = {{0, 0.00690},    {500, 0.00398},  {1000, 0.00235},
                                         {1500, 0.00138}, {1750, 0.00110}, {2300, 0.00088}};
    v = std::clamp(std::fabs(v), 0.0, 2300.0);
    for (int i = 0; i < 5; ++i)
        if (v <= tbl[i + 1][0]) return dlerp(tbl[i][1], tbl[i + 1][1], (v - tbl[i][0]) / (tbl[i + 1][0] - tbl[i][0]));
    return tbl[5][1];
}

double requiredThrust(const DVec3& dirA, const DVec3& dirB, double s, double v) {
    const DVec3 ac = (dirB - dirA) * (v * v / s);
    return length(ac + DVec3{0, 0, phys::kGravity});
}

int seedRingCount(uint64_t seed) { return static_cast<int>(seed >> 56); }

uint64_t seedWithRingCount(uint64_t seed, int count) {
    const uint64_t c = static_cast<uint64_t>(std::clamp(count, 0, 255));
    return (seed & 0x00FFFFFFFFFFFFFFULL) | (c << 56);
}

int minRingCount(CourseType t) { return t == CourseType::Aerial || t == CourseType::GroundWall ? 4 : 3; }

int maxRingCount(CourseType t, double d) {
    d = dclamp01(d);
    switch (t) {
    case CourseType::Aerial: return static_cast<int>(std::floor(dlerp(20, 30, dclamp01(d / 0.6))));
    case CourseType::GroundWall: return static_cast<int>(std::floor(dlerp(11, 24, dclamp01(d / 0.7))));
    case CourseType::AirDribble: return 9;     // limited by the field length (the course heads to the orange goal)
    case CourseType::DribbleGates: return 8;   // also limited by the field length (7600 uu / spacing)
    }
    return 16;
}

namespace {

int countFor(uint64_t seed, CourseType type, int automatic, double d) {
    const int forced = seedRingCount(seed);
    if (forced <= 0) return automatic;
    return std::clamp(forced, minRingCount(type), maxRingCount(type, d));
}

bool farFromOthers(const std::vector<Ring>& rings, const DVec3& p, double minDist, size_t skipLast) {
    const size_t n = rings.size() >= skipLast ? rings.size() - skipLast : 0;
    for (size_t j = 0; j < n; ++j)
        if (length(rings[j].center - p) < minDist) return false;
    return true;
}

DVec3 pickWaypoint(Rng& rng, const AirParams& p) {
    const double x = rngRange(rng, -p.wpX, p.wpX);
    const double y = rngRange(rng, p.wpYMin, p.wpYMax);
    const double z = rngRange(rng, p.wpZMin, p.wpZMax);
    return {x, y, z};
}

// "Turtle + waypoint" walker shared by the air courses, with thrust feasibility and backtracking.
bool genAir(Rng& rng, const AirParams& p, const DVec3& startPoint, DVec3 firstC, std::vector<Ring>& out,
            double& estBoost, GenStats& st) {
    out.clear();
    estBoost = 0;
    const double radius = std::round(rngRange(rng, p.rMin, p.rMax));
    firstC.z = std::clamp(firstC.z, radius + p.clearance + 20.0, phys::kCeilingZ - p.clearance - radius - 20.0);
    const DVec3 d0 = normalize(firstC - startPoint);
    struct WState {
        DVec3 pos, dir;
        double yaw, pitch, boostSince, boostTotal;
    };
    WState w0;
    w0.yaw = std::atan2(d0.y, d0.x);
    w0.pitch = std::clamp(std::asin(std::clamp(d0.z, -1.0, 1.0)), p.pitchMin, p.pitchMax);
    w0.dir = dirFromYawPitch(w0.yaw, w0.pitch);
    w0.boostSince = w0.boostTotal = 0;
    Ring first = makeDisk(quantize(firstC), w0.dir, radius);
    if (!ringInsideArena(first, p.clearance)) return false;
    w0.pos = first.center;
    out.push_back(first);
    std::vector<WState> stack{w0};
    DVec3 wp = pickWaypoint(rng, p);
    int backtracks = 0;
    const double tLimit = p.eta * phys::kBoostAccelAir;
    while (static_cast<int>(out.size()) < p.count) {
        const WState cur = stack.back();
        bool ok = false;
        for (int attempt = 0; attempt < 48 && !ok; ++attempt) {
            ++st.attempts;
            if (attempt == 16 || attempt == 32 || length(wp - cur.pos) < 1800.0) wp = pickWaypoint(rng, p);
            const double horiz = std::hypot(wp.x - cur.pos.x, wp.y - cur.pos.y);
            const double wantYaw = std::atan2(wp.y - cur.pos.y, wp.x - cur.pos.x);
            const double wantPitch =
                std::clamp(std::atan2(wp.z - cur.pos.z, std::max(horiz, 1.0)), p.pitchMin, p.pitchMax);
            double s = rngRange(rng, p.sMin, p.sMax);
            double dyaw = (1.0 - p.noise) * wrapAngle(wantYaw - cur.yaw);
            dyaw += p.noise * p.yawStep * rngRange(rng, -1.0, 1.0);
            double dpitch = (1.0 - p.noise) * (wantPitch - cur.pitch);
            dpitch += p.noise * p.pitchStep * rngRange(rng, -1.0, 1.0);
            if (attempt >= 40) {  // fallback: shortest hop, turn hard towards the arena centre, level out
                ++st.fallbacks;
                dyaw = wrapAngle(std::atan2(-cur.pos.y, -cur.pos.x) - cur.yaw);
                dpitch = -cur.pitch * 0.5;
                s = p.sMin;
            }
            dyaw = std::clamp(dyaw, -p.yawStep, p.yawStep);
            dpitch = std::clamp(dpitch, -p.pitchStep, p.pitchStep);
            // Physical feasibility: shrink the turn until |v^2 dDir / s + g| <= eta * a_boost.
            double nyaw = 0, npitch = 0, thrust = 1e9;
            DVec3 dirNew;
            for (double k : {1.0, 0.75, 0.5, 0.3, 0.15, 0.0}) {
                nyaw = cur.yaw + dyaw * k;
                npitch = std::clamp(cur.pitch + dpitch * k, p.pitchMin, p.pitchMax);
                if (p.biasPlusY) {
                    const double off = wrapAngle(nyaw - phys::kPi / 2);
                    if (std::fabs(off) > p.yawCone) nyaw = phys::kPi / 2 + std::clamp(off, -p.yawCone, p.yawCone);
                }
                dirNew = dirFromYawPitch(nyaw, npitch);
                thrust = requiredThrust(cur.dir, dirNew, s, p.vRef);
                if (thrust <= tLimit) break;
            }
            if (thrust > tLimit) {
                ++st.rejThrust;
                continue;
            }
            const DVec3 chord = normalize(cur.dir + dirNew);
            const DVec3 cand = quantize(cur.pos + chord * s);
            Ring r = makeDisk(cand, dirNew, radius);
            if (!ringInsideArena(r, p.clearance)) {
                ++st.rejBounds;
                continue;
            }
            if (!farFromOthers(out, cand, p.minSep, 1)) {
                ++st.rejSep;
                continue;
            }
            // Boost estimate for this segment: time * 33.3/s * duty, duty = |T| / a_boost.
            const double segBoost = (s / p.vRef) * phys::kBoostPerSecond * std::min(1.0, thrust / phys::kBoostAccelAir);
            WState nw{cand, dirNew, nyaw, npitch, cur.boostSince + segBoost, cur.boostTotal + segBoost};
            if (nw.boostSince > 70.0) {
                r.flags |= kFlagBoostRefill;
                nw.boostSince = 0;
            }
            out.push_back(r);
            stack.push_back(nw);
            ok = true;
        }
        if (!ok) {  // dead end: backtrack up to two rings and try another branch
            if (++backtracks > 40 || out.size() <= 1) return false;
            ++st.backtracks;
            const size_t pop = std::min<size_t>(2, out.size() - 1);
            for (size_t k = 0; k < pop; ++k) {
                out.pop_back();
                stack.pop_back();
            }
            wp = pickWaypoint(rng, p);
        }
    }
    estBoost = stack.back().boostTotal;
    return true;
}

double pathLength(const DVec3& start, const std::vector<Ring>& rings) {
    if (rings.empty()) return 0;
    double len = length(rings[0].center - start);
    for (size_t i = 1; i < rings.size(); ++i) len += length(rings[i].center - rings[i - 1].center);
    return len;
}

bool genGroundWall(Rng& rng, double d, uint64_t seed, Course& c, GenStats& st, uint32_t version) {
    // Path on the "unrolled" surface: u in [-(4096+H), 4096+H]; |u| <= 4096 is the floor x = u, |u| > 4096 is
    // the side wall x = sign(u)*4096 at height |u| - 4096. Rings are arches standing on the surface.
    const int autoCount = static_cast<int>(std::lround(dlerp(8, 16, d)));
    const int count = countFor(seed, CourseType::GroundWall, autoCount, d);
    const double sepExtra = count > autoCount ? 100.0 : 300.0;  // long custom courses: tighter packing
    // Version 2: floor arches only (radius 450) up to difficulty 30, then the version 1 arches with wall arches from
    // niveau 4 (e: 0..1 over the rest of the range, for the wall height).
    const bool v2 = version >= 2;
    const bool floorOnly = v2 && d <= kGroundFloorOnlyPct / 100.0 + 1e-9;
    const double e = v2 ? dclamp01((d - kGroundFloorOnlyPct / 100.0) / (1.0 - kGroundFloorOnlyPct / 100.0)) : d;
    const double radius = floorOnly ? 450.0 : std::round(rngRange(rng, dlerp(400, 250, d), dlerp(440, 280, d)));
    const double sMin = dlerp(1100, 800, d), sMax = dlerp(1500, 1300, d);
    const double vRef = dlerp(1200, 1500, d);
    const double turnStep = degToRad(dlerp(30, 75, d));
    const double noise = dlerp(0.35, 0.8, d);
    const double wallH = dlerp(800, 1400, e);    // max ring height on walls (centre)
    // Share of waypoints placed on a side wall.
    const double pWall = floorOnly ? 0.0 : (v2 ? 0.45 : dlerp(0.15, 0.45, d));
    const double band = 350;                     // keep rings off the floor/wall curve
    const double hc = 0.55 * radius;             // arch: centre above the surface
    const double rMinTurn = 1.25 / maxGroundCurvature(vRef);
    c.start = {};
    double u = rngRange(rng, -2000, 2000), y = -4000;
    double phi = phys::kPi / 2 + rngRange(rng, -degToRad(20), degToRad(20));  // heading in the (u, y) plane
    c.start.carPos = quantize({u, y, phys::kCarRestZ});
    c.start.carYaw = phi;
    c.start.carVel = dirFromYawPitch(phi, 0) * 900.0;
    auto toWorld = [&](double uu, double yy, double ph, DVec3& center, DVec3& dir, Surface& surf) {
        const double cph = std::cos(ph), sph = std::sin(ph);
        if (std::fabs(uu) <= phys::kArenaX) {
            surf = Surface::Floor;
            center = {uu, yy, hc};
            dir = {cph, sph, 0};
        } else if (uu > 0) {
            surf = Surface::WallPosX;
            center = {phys::kArenaX - hc, yy, uu - phys::kArenaX};
            dir = {0, sph, cph};
        } else {
            surf = Surface::WallNegX;
            center = {-phys::kArenaX + hc, yy, -uu - phys::kArenaX};
            dir = {0, sph, -cph};
        }
    };
    auto pickWp = [&](double& wu, double& wy) {
        if (rngUniform01(rng) < pWall) {
            const double side = rngUniform01(rng) < 0.5 ? -1.0 : 1.0;
            wu = side * (phys::kArenaX + rngRange(rng, band + 150, wallH));
            wy = rngRange(rng, -3000, 3000);
        } else {
            wu = rngRange(rng, -3200, 3200);
            wy = rngRange(rng, -4200, 4200);
        }
    };
    double wu = 0, wy = 0;
    pickWp(wu, wy);
    c.rings.clear();
    double len = 0;
    DVec3 prevWorld = c.start.carPos;
    for (int i = 0; i < count; ++i) {
        bool placed = false;
        for (int attempt = 0; attempt < 48 && !placed; ++attempt) {
            ++st.attempts;
            if (attempt == 16 || attempt == 32 || std::hypot(wu - u, wy - y) < 1500.0) pickWp(wu, wy);
            const double s = (i == 0) ? rngRange(rng, 1300, 1700) : rngRange(rng, sMin, sMax);
            double dphi = (1.0 - noise) * wrapAngle(std::atan2(wy - y, wu - u) - phi);
            dphi += noise * turnStep * rngRange(rng, -1.0, 1.0);
            if (attempt >= 40) {  // fallback: steer towards the field centre
                ++st.fallbacks;
                dphi = wrapAngle(std::atan2(-y, -u) - phi);
            }
            dphi = std::clamp(dphi, -turnStep, turnStep);
            const double alpha = std::fabs(dphi);
            // Ground feasibility: radius of the arc through both rings >= 1.25 * min turn radius.
            if (alpha > 1e-6 && s / (2.0 * std::sin(alpha / 2.0)) < rMinTurn) continue;
            const double nphi = phi + dphi;
            const double cu = u + std::cos(phi + dphi / 2) * s, cy = y + std::sin(phi + dphi / 2) * s;
            const double au = std::fabs(cu);
            if (au > phys::kArenaX - band && au < phys::kArenaX + band) continue;  // floor/wall curve band
            if (floorOnly && au > phys::kArenaX) continue;                         // v2 niveaux 1 to 3: floor only
            if (au > phys::kArenaX + wallH) continue;                              // too high on the wall
            if (au > phys::kArenaX && std::fabs(cy) > 3968.0 - 500.0) continue;    // flat part of the wall only
            DVec3 center, dir;
            Surface surf = Surface::Floor;
            toWorld(cu, cy, nphi, center, dir, surf);
            Ring r = makeDisk(quantize(center), dir, radius, surf);
            r.up = (surf == Surface::Floor) ? DVec3{0, 0, 1} : DVec3{surf == Surface::WallPosX ? -1.0 : 1.0, 0, 0};
            if (!ringInsideArena(r, 120)) continue;
            if (!farFromOthers(c.rings, r.center, 2 * radius + sepExtra, 1)) continue;
            len += length(r.center - prevWorld);
            prevWorld = r.center;
            c.rings.push_back(r);
            u = cu;
            y = cy;
            phi = nphi;
            placed = true;
        }
        if (!placed) return false;
    }
    c.objRadius = phys::kCarPassRadius;
    c.estLength = len;
    c.parTime = len / vRef;
    c.estBoost = 0;
    return true;
}

// Slalom, generator v2: start at rest with the ball on the roof, first gate 1500 uu ahead, and a real weave: a clean
// pass asks the ball to move `weave` = 150..300 uu sideways from the lane. The ball path is ~ a sinusoid of amplitude
// `weave` and half-wavelength s: v^2 weave (pi/s)^2 <= aLat, so the spacing grows and the reference speed (target
// time) is capped by what that weave allows.
bool genDribbleGatesV2(Rng& rng, double d, uint64_t seed, Course& c) {
    const double aLat = dlerp(300, 700, d);  // comfort limit for a roof dribble (to calibrate in game)
    const double s = std::round(rngRange(rng, dlerp(1550, 1300, d), dlerp(1650, 1400, d)));
    const double w = std::round(dlerp(500, 250, d));  // gate half-width
    const double hh = 230;                             // gate half-height (ball centre <= 460)
    const double weave = dlerp(kSlalomWeaveMin, kSlalomWeaveMax, d);
    const double amp = std::ceil(w - phys::kBallRadius + weave);
    const double vMax = std::sqrt(aLat / (weave * (phys::kPi / s) * (phys::kPi / s)));
    const double vRef = std::min(dlerp(900, 650, d), std::floor(vMax));
    const double firstGate = kSlalomFirstGate;
    const int fit = 1 + static_cast<int>((8900.0 - firstGate) / s);
    const int count = std::min(countFor(seed, CourseType::DribbleGates, static_cast<int>(std::lround(dlerp(5, 9, d))), d), fit);
    const double dirY = (rng.next() & 1u) ? 1.0 : -1.0;
    const double laneX = std::round(rngRange(rng, -2200, 2200));
    const double y0 = -dirY * 4200.0;
    const double firstSign = (rng.next() & 1u) ? 1.0 : -1.0;
    c.start = {};
    c.start.carPos = {laneX, y0, phys::kCarRestZ};
    c.start.carYaw = dirY > 0 ? phys::kPi / 2 : -phys::kPi / 2;
    c.start.carVel = {};
    c.start.hasBall = true;
    c.start.ballOnRoof = true;
    std::vector<DVec3> pts;
    pts.reserve(static_cast<size_t>(std::max(count, 0)));
    for (int i = 0; i < count; ++i)
        pts.push_back(quantize({laneX + firstSign * ((i % 2) ? -amp : amp), y0 + dirY * (firstGate + i * s), hh}));
    c.rings.clear();
    for (int i = 0; i < count; ++i) {
        const DVec3 prev = (i == 0) ? DVec3{laneX, y0, hh} : pts[static_cast<size_t>(i - 1)];
        const DVec3 next = (i + 1 < count) ? pts[static_cast<size_t>(i + 1)] : pts[static_cast<size_t>(i)] + DVec3{0, dirY * s, 0};
        c.rings.push_back(makeGate(pts[static_cast<size_t>(i)], next - prev, w, hh));
    }
    bool ok = count > 0;
    for (const auto& r : c.rings) ok = ok && ringInsideArena(r, 120);
    c.objRadius = phys::kBallRadius;
    c.ballCourse = true;
    c.estLength = firstGate + (count - 1) * s;
    c.parTime = c.estLength / vRef;
    c.estBoost = 0;
    return ok;
}

// Aerial rings, generator v2 "Initiation" (niveaux 1 to 3): a few big rings low over the floor, gentle turns, the
// car released at rest with ring 1 at least 2500 uu ahead. Stage 1: passable on the floor; stage 2: a single jump;
// stage 3: the first real aerial.
struct InitBand {
    int count;
    double radius, zMin, zMax, sMin, sMax, turnDeg, vRef, duty;
};

InitBand initBand(int stage) {
    switch (stage) {
    case 1: return {4, 600, 280, 380, 1500, 1800, 15, 1100, 0.15};
    case 2: return {5, 450, 400, 600, 1400, 1700, 20, 1000, 0.3};
    default: return {6, 400, 500, 900, 1300, 1600, 25, 1000, 0.45};
    }
}

bool genAerialInit(Rng& rng, int stage, uint64_t seed, Course& c, GenStats& st) {
    const InitBand b = initBand(stage);
    const int minCount = minRingCount(CourseType::Aerial);
    const int forced = seedRingCount(seed);
    const int count = forced > 0 ? std::clamp(forced, minCount, std::max(minCount, b.count)) : b.count;
    const double turn = degToRad(b.turnDeg);
    c.start = {};
    const double sx = rngRange(rng, -2000, 2000);
    const double sy = rngRange(rng, -4300, -3700);
    c.start.carPos = quantize({sx, sy, phys::kCarRestZ});
    // Roughly towards +Y, a little towards the middle of the field.
    c.start.carYaw = phys::kPi / 2 + rngRange(rng, -degToRad(10), degToRad(10)) - degToRad(8) * (sx / 2000.0);
    c.start.carVel = {};  // released at rest
    double phi = c.start.carYaw;
    double px = c.start.carPos.x, py = c.start.carPos.y;
    DVec3 aim{rngRange(rng, -1500, 1500), rngRange(rng, 0, 2500), 0};
    c.rings.clear();
    double len = 0;
    DVec3 prev = c.start.carPos;
    for (int i = 0; i < count; ++i) {
        bool placed = false;
        for (int attempt = 0; attempt < 48 && !placed; ++attempt) {
            ++st.attempts;
            double s = rngRange(rng, b.sMin, b.sMax);
            double dphi = 0;
            if (i == 0) {
                s = rngRange(rng, kAerialFirstRingMin, kAerialFirstRingMin + 300);
            } else {
                if (attempt == 16 || std::hypot(aim.x - px, aim.y - py) < 1500.0)
                    aim = {rngRange(rng, -2000, 2000), rngRange(rng, -2500, 2500), 0};
                dphi = 0.6 * wrapAngle(std::atan2(aim.y - py, aim.x - px) - phi) + 0.4 * turn * rngRange(rng, -1.0, 1.0);
                if (attempt >= 32) {  // fallback: steer towards the field centre
                    ++st.fallbacks;
                    dphi = wrapAngle(std::atan2(-py, -px) - phi);
                }
                dphi = std::clamp(dphi, -turn, turn);
            }
            const double nphi = phi + dphi;
            const double hx = px + std::cos(phi + dphi / 2) * s, hy = py + std::sin(phi + dphi / 2) * s;
            const DVec3 center = quantize({hx, hy, rngRange(rng, b.zMin, b.zMax)});
            // A ring whose bottom would be under the floor is drawn as an arch standing on it.
            const Surface surf = center.z - b.radius < 150.0 ? Surface::Floor : Surface::Air;
            Ring r = makeDisk(center, dirFromYawPitch(nphi, 0), b.radius, surf);
            if (surf == Surface::Floor) r.up = {0, 0, 1};
            if (!ringInsideArena(r, 150)) {
                ++st.rejBounds;
                continue;
            }
            if (!farFromOthers(c.rings, r.center, 2 * b.radius + 400, 1)) {
                ++st.rejSep;
                continue;
            }
            len += length(r.center - prev);
            prev = r.center;
            c.rings.push_back(r);
            px = hx;
            py = hy;
            phi = nphi;
            placed = true;
        }
        if (!placed) return false;
    }
    c.objRadius = phys::kCarPassRadius;
    c.ballCourse = false;
    c.estLength = len;
    c.parTime = len / b.vRef;
    c.estBoost = c.parTime * phys::kBoostPerSecond * b.duty;
    return true;
}

bool genDribbleGates(Rng& rng, double d, uint64_t seed, Course& c) {
    // Slalom: gates alternate +-A around a lane. The ball path is ~ a sinusoid of amplitude A_eff and
    // half-wavelength s: peak curvature k = A_eff (pi/s)^2; v^2 k must stay <= aLat (ball stays on the roof).
    const double vRef = dlerp(900, 650, d);
    const double aLat = dlerp(300, 700, d);  // comfort limit for a roof dribble (to calibrate in game)
    const double s = std::round(rngRange(rng, dlerp(1250, 900, d), dlerp(1350, 1000, d)));
    const double w = std::round(dlerp(500, 250, d));  // gate half-width
    const double hh = 230;                             // gate half-height (ball centre <= 460)
    const double aEffMax = aLat / (vRef * vRef * (phys::kPi / s) * (phys::kPi / s));
    const double amp = std::round(rngRange(rng, 0.6, 1.0) * aEffMax + w - phys::kBallRadius);
    const int fit = static_cast<int>(7600.0 / s);
    const int count = std::min(countFor(seed, CourseType::DribbleGates, static_cast<int>(std::lround(dlerp(5, 9, d))), d), fit);
    const double dirY = (rng.next() & 1u) ? 1.0 : -1.0;
    const double laneX = std::round(rngRange(rng, -2200, 2200));
    const double y0 = -dirY * 4200.0;
    const double firstSign = (rng.next() & 1u) ? 1.0 : -1.0;
    c.start = {};
    c.start.carPos = {laneX, y0, phys::kCarRestZ};
    c.start.carYaw = dirY > 0 ? phys::kPi / 2 : -phys::kPi / 2;
    c.start.carVel = {0, dirY * 300.0, 0};
    c.start.hasBall = true;
    c.start.ballOnRoof = true;
    std::vector<DVec3> pts;
    pts.reserve(static_cast<size_t>(std::max(count, 0)));
    for (int i = 0; i < count; ++i)
        pts.push_back(quantize({laneX + firstSign * ((i % 2) ? -amp : amp), y0 + dirY * (1200.0 + i * s), hh}));
    c.rings.clear();
    for (int i = 0; i < count; ++i) {
        const DVec3 prev = (i == 0) ? DVec3{laneX, y0, hh} : pts[static_cast<size_t>(i - 1)];
        const DVec3 next = (i + 1 < count) ? pts[static_cast<size_t>(i + 1)] : pts[static_cast<size_t>(i)] + DVec3{0, dirY * s, 0};
        c.rings.push_back(makeGate(pts[static_cast<size_t>(i)], next - prev, w, hh));
    }
    bool ok = count > 0;
    for (const auto& r : c.rings) ok = ok && ringInsideArena(r, 120);
    c.objRadius = phys::kBallRadius;
    c.ballCourse = true;
    c.estLength = 1200.0 + (count - 1) * s;
    c.parTime = c.estLength / vRef;
    c.estBoost = 0;
    return ok;
}

} // namespace

AirParams aerialParams(double d) {
    AirParams p;
    p.count = static_cast<int>(std::lround(dlerp(8, 16, d)));
    p.rMin = dlerp(480, 230, d);
    p.rMax = dlerp(540, 280, d);
    p.sMin = dlerp(1100, 900, d);
    p.sMax = dlerp(1500, 1400, d);
    p.yawStep = degToRad(dlerp(30, 55, d));
    p.pitchStep = degToRad(dlerp(10, 30, d));
    p.pitchMin = degToRad(-dlerp(20, 45, d));
    p.pitchMax = degToRad(dlerp(20, 40, d));
    // Easy courses assume a slow cruise (1000 uu/s) and 75 % of the boost acceleration, hard ones 1150 and 95 %.
    p.vRef = dlerp(1000, 1150, d);
    p.eta = dlerp(0.75, 0.95, d);
    p.clearance = 150;
    p.minSep = 2 * p.rMax + 400;
    p.noise = dlerp(0.3, 0.55, d);
    p.wpX = 3000;
    p.wpYMin = -4000;
    p.wpYMax = 4000;
    p.wpZMin = dlerp(700, 400, d);
    p.wpZMax = dlerp(1200, 1650, d);
    p.biasPlusY = false;
    p.yawCone = phys::kPi;
    return p;
}

AirParams airDribbleParams(double d) {
    AirParams p;
    p.count = static_cast<int>(std::lround(dlerp(5, 8, d)));
    p.rMin = dlerp(400, 250, d);
    p.rMax = dlerp(440, 280, d);
    p.sMin = dlerp(750, 600, d);
    p.sMax = dlerp(1000, 900, d);
    p.yawStep = degToRad(dlerp(8, 25, d));
    p.pitchStep = degToRad(dlerp(6, 15, d));
    p.pitchMin = degToRad(-10);
    p.pitchMax = degToRad(dlerp(35, 45, d));
    p.vRef = dlerp(900, 1300, d);
    p.eta = dlerp(0.70, 0.85, d);
    p.clearance = 120;
    p.minSep = 2 * p.rMax + 200;
    p.noise = dlerp(0.3, 0.7, d);
    p.wpX = 700;
    p.wpYMin = 3600;
    p.wpYMax = 4400;
    p.wpZMin = dlerp(900, 700, d);
    p.wpZMax = dlerp(1300, 1600, d);
    p.biasPlusY = true;
    p.yawCone = degToRad(dlerp(25, 45, d));
    return p;
}

AirParams airParamsFor(CourseType type, double d, uint64_t seed) {
    AirParams p = type == CourseType::AirDribble ? airDribbleParams(d) : aerialParams(d);
    const int autoCount = p.count;
    p.count = countFor(seed, type, autoCount, d);
    if (p.count > autoCount) p.minSep = 2 * p.rMax + 100;  // rings still never overlap (centres > 2 R apart)
    return p;
}

int niveauOf(int difficultyPct) {
    return 1 + static_cast<int>(std::lround(std::clamp(difficultyPct, 0, 100) * 9 / 100.0));
}

int beginnerStage(CourseType type, int difficultyPct, uint32_t version) {
    if (version < 2) return 0;
    const int n = niveauOf(difficultyPct);
    if (type == CourseType::Aerial && n <= 3) return n;
    if (type == CourseType::AirDribble && n <= 2) return n;
    return 0;
}

double regularD(CourseType type, int difficultyPct, uint32_t version) {
    const int pct = std::clamp(difficultyPct, 0, 100);
    if (version >= 2) {
        // First pct of niveau 4 (aerial) and niveau 3 (air dribble): the niveaux below are the beginner bands.
        if (type == CourseType::Aerial) return dclamp01((pct - 28) / 72.0);
        if (type == CourseType::AirDribble) return dclamp01((pct - 17) / 83.0);
    }
    return pct / 100.0;
}

DVec3 roofBallOffset() {
    // Octane hitbox: offset (13.88, 0, 20.75), full height 36.16; ball radius 91.25 plus the 2 uu gap of roofSpawn.
    return {0.0, 13.88, 20.75 + 36.16 * 0.5 + phys::kBallRadius + 2.0};
}

Course generateCourse(uint64_t seed, CourseType type, int difficultyPct, GenStats* statsOut, uint32_t version) {
    Course c;
    c.seed = seed;
    c.type = type;
    c.version = std::clamp(version, kOldestGeneratorVersion, kGeneratorVersion);
    c.difficultyPct = std::clamp(difficultyPct, 0, 100);
    const bool v2 = c.version >= 2;
    const int stage = beginnerStage(type, c.difficultyPct, c.version);
    const double d = regularD(type, c.difficultyPct, c.version);
    GenStats st;
    for (int restart = 0; restart < 64; ++restart) {
        Rng rng(splitmix64(seed ^ (static_cast<uint64_t>(restart) << 56)), 0xDA3E39CB94B95BDBULL + static_cast<uint64_t>(type));
        bool ok = false;
        switch (type) {
        case CourseType::Aerial: {
            if (stage > 0) {
                ok = genAerialInit(rng, stage, seed, c, st);
                break;
            }
            const AirParams p = airParamsFor(type, d, seed);
            // Start on the floor in the blue half, facing roughly +Y, rolling at 800 uu/s.
            c.start = {};
            const double sx = rngRange(rng, -2500, 2500);
            const double sy = rngRange(rng, -4300, -3500);
            c.start.carPos = quantize({sx, sy, phys::kCarRestZ});
            c.start.carYaw = phys::kPi / 2 + rngRange(rng, -degToRad(15), degToRad(15));
            c.start.carVel = dirFromYawPitch(c.start.carYaw, 0) * 800.0;
            // Version 2: ring 1 at least 2500 uu ahead, so the take-off is not due right after the start.
            const double dist0 = v2 ? rngRange(rng, kAerialFirstRingMin, kAerialFirstRingMin + 500) : rngRange(rng, 1500, 2000);
            const double h0 = rngRange(rng, dlerp(650, 450, d), dlerp(850, 800, d));
            const DVec3 firstC = c.start.carPos + dirFromYawPitch(c.start.carYaw, 0) * dist0 + DVec3{0, 0, h0};
            ok = genAir(rng, p, c.start.carPos, firstC, c.rings, c.estBoost, st);
            c.objRadius = phys::kCarPassRadius;
            c.ballCourse = false;
            if (ok) {
                c.estLength = pathLength(c.start.carPos, c.rings);
                c.parTime = c.estLength / p.vRef;
            }
            break;
        }
        case CourseType::AirDribble: {
            AirParams p = airParamsFor(type, d, seed);
            c.start = {};
            const double sx = rngRange(rng, -1800, 1800);
            c.start.carPos = quantize({sx, -4500, phys::kCarRestZ});
            c.start.carYaw = phys::kPi / 2;
            c.start.hasBall = true;
            // Version 1 always measured ring 1 from the popped ball (whatever the start setting); version 2 from the
            // real start, chosen by the seed bit.
            const bool popped = !v2 || (seed & kSeedPoppedStart) != 0;
            DVec3 firstC;
            if (popped) {
                // Ball popped ahead of the car (rising, slowly moving away): the player chases it and takes it up.
                c.start.carVel = {0, 600, 0};
                c.start.ballPos = c.start.carPos + DVec3{0, 650, 280};
                c.start.ballVel = {0, 450, 500};
                const double fx = rngRange(rng, -200, 200);
                const double fy = rngRange(rng, 1100, 1500);
                const double fz = rngRange(rng, dlerp(300, 350, d), dlerp(400, 450, d));
                firstC = c.start.ballPos + DVec3{fx, fy, fz};
            } else {
                // Ball resting on the roof of a car at rest.
                c.start.carVel = {};
                c.start.ballOnRoof = true;
                c.start.ballPos = c.start.carPos + roofBallOffset();
                c.start.ballVel = {};
                const double fx = rngRange(rng, -200, 200);
                const double fy = rngRange(rng, 1300, 1700);
                const double fz = rngRange(rng, 350, 450);
                firstC = c.start.ballPos + DVec3{fx, fy, fz};
            }
            if (stage > 0) {
                // Niveau 1: one ring ("pop et pousse"), niveau 2: two. Big rings (the ball must still be popped: a
                // ball carried on the roof passes under the opening).
                p.count = stage;
                p.rMin = p.rMax = stage == 1 ? 450 : 430;
                if (!popped) firstC = c.start.ballPos + DVec3{0, 1500, 450};
            }
            ok = genAir(rng, p, c.start.ballPos, firstC, c.rings, c.estBoost, st);
            c.objRadius = phys::kBallRadius;
            c.ballCourse = true;
            if (ok) {
                c.estLength = pathLength(c.start.ballPos, c.rings);
                // A roof start at rest (v2) first needs the pop: one more second in the target time.
                c.parTime = c.estLength / p.vRef + (popped ? 0.0 : 1.0);
            }
            break;
        }
        case CourseType::GroundWall:
            ok = genGroundWall(rng, d, seed, c, st, c.version);
            c.ballCourse = false;
            break;
        case CourseType::DribbleGates:
            ok = v2 ? genDribbleGatesV2(rng, d, seed, c) : genDribbleGates(rng, d, seed, c);
            break;
        }
        if (ok && !c.rings.empty()) {
            for (size_t i = 0; i < c.rings.size(); ++i)
                if (i > 0 && i % 5 == 4) c.rings[i].flags |= kFlagCheckpoint;
            c.rings.back().flags |= kFlagFinal;
            st.restarts = restart;
            if (statsOut) *statsOut = st;
            return c;
        }
    }
    if (seedRingCount(seed) > 0) {
        // The requested ring count did not fit: same course seed with the automatic count (still deterministic).
        Course f = generateCourse(seed & 0x00FFFFFFFFFFFFFFULL, type, difficultyPct, statsOut, c.version);
        f.seed = seed;
        return f;
    }
    if (statsOut) *statsOut = st;
    c.rings.clear();  // generation failed (never observed in tests)
    return c;
}

uint64_t courseHash(const Course& c) {
    uint64_t h = 1469598103934665603ULL;
    auto byte = [&](uint8_t b) {
        h ^= b;
        h *= 1099511628211ULL;
    };
    auto dbl = [&](double v) {  // explicit little-endian byte order of the IEEE-754 bits
        uint64_t bits = 0;
        std::memcpy(&bits, &v, sizeof(bits));
        for (int i = 0; i < 8; ++i) byte(static_cast<uint8_t>((bits >> (8 * i)) & 0xFFu));
    };
    for (const auto& r : c.rings) {
        dbl(r.center.x);
        dbl(r.center.y);
        dbl(r.center.z);
        dbl(r.radius);
        byte(static_cast<uint8_t>(r.shape));
        byte(r.flags);
    }
    return h;
}

} // namespace rlt::courses
