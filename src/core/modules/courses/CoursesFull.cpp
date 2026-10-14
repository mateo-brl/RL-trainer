// CoursesModule: the "parcours complet" (full sequence) of every exercise (cross-cutting fix 2). The steps before the
// skill are checked one after the other with the shared StepTracker: each has its own time limit, turns green on the
// HUD when done, and a failed attempt names the step that was missing with its tip. Generic checks (ball lost, ring
// missed, landing) only run from the step that needs them.
//
//  - Anneaux aériens, "décollage": one ring 25 m ahead, car at rest. Saute, lève le nez, boost, passe l'anneau,
//    atterris sur les roues.
//  - Anneaux sol et murs, "monter au mur": one arch on a side wall. Arrive en biais, monte sur le mur, passe l'arche,
//    redescends en roulant.
//  - Air dribble, "du sol à l'air dribble": shared pickup (ball on the floor), carry, pop, follow, push the ball
//    through one ring placed in front of the car at the pop.
//  - Dribble sur le toit, "ramasser puis dribbler": shared pickup, then the roof dribble of the level.
//  - Slalom, "ramasser puis slalomer": shared pickup, then the gates of a generated slalom.
//  - Rattrapage, "rattrape une chandelle": a ball lobbed from the floor 12 m ahead. Lis la chute, place-toi dessous,
//    amortis sans sauter, suis les rebonds, garde-la.
// The pickup ("Ramassage") is the shared helper of core/common/Pickup.h with its own staircase (pickup_): a missed
// pickup never lowers the exercise. Each full sequence has its own staircase (fullStairs_).

#include <algorithm>
#include <cmath>

#include "core/common/Text.h"
#include "core/modules/courses/CoursesModule.h"

namespace rlt {

namespace {

// Step ids.
constexpr const char* kAirJump = "air.jump";
constexpr const char* kAirNose = "air.nose";
constexpr const char* kAirBoost = "air.boost";
constexpr const char* kAirRing = "air.ring";
constexpr const char* kAirLand = "air.land";
constexpr const char* kWallApproach = "wall.approach";
constexpr const char* kWallOn = "wall.on";
constexpr const char* kWallArch = "wall.arch";
constexpr const char* kWallDown = "wall.down";
constexpr const char* kAdCarry = "ad.carry";
constexpr const char* kAdPop = "ad.pop";
constexpr const char* kAdFollow = "ad.follow";
constexpr const char* kAdRing = "ad.ring";
constexpr const char* kDribbleNose = "dribble.nose";
constexpr const char* kDribbleHold = "dribble.hold";
constexpr const char* kCatchRead = "catch.read";
constexpr const char* kCatchUnder = "catch.under";
constexpr const char* kCatchCushion = "catch.cushion";
constexpr const char* kCatchHops = "catch.hops";
constexpr const char* kCatchHold = "catch.hold";

constexpr float kOnFloorZ = 20.f;  // ball centre lower than radius + this: on the floor

bool ballOnFloorNow(const Snapshot& s) { return s.ball.valid && s.ball.pos.z < s.ball.radius + kOnFloorZ; }

float flatDistance(const Vec3& a, const Vec3& b) { return std::hypot(a.x - b.x, a.y - b.y); }

Step makeStep(const char* id, const char* label, StepCheck check, float timeout, const char* failCode, const char* tip,
              float hold = 0.f, float grace = 0.f) {
    Step st;
    st.id = id;
    st.label = label;
    st.check = std::move(check);
    st.timeout = timeout;
    st.failCode = failCode;
    st.tip = tip;
    st.hold = hold;
    st.holdGrace = grace;
    return st;
}

std::string gateId(const char* prefix, size_t k) { return std::string(prefix) + std::to_string(k); }

Metric metric(const char* key, const char* label, float value, const char* unit, float ideal) {
    Metric m;
    m.key = key;
    m.label = label;
    m.value = value;
    m.unit = unit;
    m.ideal = ideal;
    m.hasIdeal = ideal > 0.f;
    return m;
}

} // namespace

// ---- Setup -------------------------------------------------------------------------------------------------------
void CoursesModule::prepareFull(const TickContext& ctx) {
    (void)ctx;
    const float d = fullD();
    Rng rng(mix64(seed_ ^ 0xF0115EED5EEDULL), 0x5EEDF011ULL + static_cast<uint64_t>(kind_));
    if (kind_ != CoursesKind::Slalom) course_ = courses::Course{};
    switch (kind_) {
    case CoursesKind::RingsAir: {
        // One ring 25 m ahead of a car at rest: enough room to jump, lift the nose and boost up to it.
        const float x = rng.range(-1500.f, 1500.f);
        fullCarPos_ = {x, -3600.f, 17.f};
        fullYawDeg_ = 90.f;
        courses::Course c;
        c.type = courses::CourseType::Aerial;
        c.objRadius = courses::phys::kCarPassRadius;
        c.start.carPos = courses::toD(fullCarPos_);
        c.start.carYaw = courses::phys::kPi / 2;
        const double radius = std::round(lerpf(550.f, 400.f, d));
        const double z = std::round(lerpf(700.f, 900.f, d));
        c.rings.push_back(courses::makeDisk({x, -3600.0 + 2500.0, z}, {0, 1, 0}, radius));
        c.rings.back().flags |= courses::kFlagFinal;
        c.estLength = 2500.0;
        c.parTime = 4.0;
        course_ = std::move(c);
        break;
    }
    case CoursesKind::RingsGround: {
        // Car at rest 19 m from a side wall, heading 35 degrees off the wall normal; one arch on the wall where that
        // climb arrives (centre 6 m high).
        fullSide_ = rng.chance(0.5f) ? 1 : -1;
        const float side = static_cast<float>(fullSide_);
        const float y0 = rng.range(-2500.f, -1500.f);
        const float a = 35.f * kDegToRad;
        fullCarPos_ = {side * (4096.f - 1900.f), y0, 17.f};
        fullYawDeg_ = std::atan2(std::sin(a), side * std::cos(a)) * kRadToDeg;
        const double radius = std::round(lerpf(450.f, 350.f, d));
        const double hc = 0.55 * radius;
        const double zc = std::round(lerpf(600.f, 750.f, d));
        const double yc = std::round(y0 + (1900.0 - 150.0) * std::tan(a) + (zc + 150.0) * std::tan(a));
        const courses::Surface surf = fullSide_ > 0 ? courses::Surface::WallPosX : courses::Surface::WallNegX;
        courses::Course c;
        c.type = courses::CourseType::GroundWall;
        c.objRadius = courses::phys::kCarPassRadius;
        c.start.carPos = courses::toD(fullCarPos_);
        courses::Ring r = courses::makeDisk({side * (4096.0 - hc), yc, zc}, {0, std::sin(a), std::cos(a)}, radius, surf);
        r.up = {fullSide_ > 0 ? -1.0 : 1.0, 0, 0};
        r.flags |= courses::kFlagFinal;
        c.rings.push_back(r);
        c.estLength = 3000.0;
        c.parTime = 4.0;
        course_ = std::move(c);
        break;
    }
    case CoursesKind::RingsAirDribble: {
        fullCarPos_ = {rng.range(-1500.f, 1500.f), -4000.f, 17.f};
        fullYawDeg_ = 90.f;
        courses::Course c;
        c.type = courses::CourseType::AirDribble;
        c.objRadius = courses::phys::kBallRadius;
        c.ballCourse = true;
        c.start.carPos = courses::toD(fullCarPos_);
        // Placed for real at the pop (fullTick); drawn in front of the car until then.
        c.rings.push_back(courses::makeDisk({fullCarPos_.x, fullCarPos_.y + 1500.0, 600.0}, {0, 1, 0},
                                            std::round(lerpf(450.f, 320.f, d))));
        c.rings.back().flags |= courses::kFlagFinal;
        c.estLength = 1500.0;
        c.parTime = 4.0;
        course_ = std::move(c);
        pickupSetup_ = pickup::makeSetup(fullCarPos_, fullYawDeg_, pickup_.niveau(), rng);
        break;
    }
    case CoursesKind::Dribble: {
        const DribbleParams dp = dribbleParams(difficulty_, cfg_.roofTolerance);
        dribbleTarget_ = dp.target;
        dribbleMinSpeed_ = dp.minSpeed;
        dribbleTolerance_ = dp.tolerance;
        fullCarPos_ = {static_cast<float>(rng.rangeInt(-1500, 1500)), -3600.f, 17.f};
        fullYawDeg_ = 90.f;
        pickupSetup_ = pickup::makeSetup(fullCarPos_, fullYawDeg_, pickup_.niveau(), rng);
        break;
    }
    case CoursesKind::Slalom: {
        // The gates of a generated slalom (its code still gives the plain slalom). The car starts 5 m behind the
        // normal start, so the pickup happens before the first gate.
        bool ok = false;
        for (int i = 0; i < 4 && !ok; ++i) ok = buildCourse(kind_, i == 0 ? seed_ : mix64(seed_ + i), difficulty_, false);
        if (!ok) {
            course_ = courses::Course{};
            return;
        }
        const float dirY = std::sin(static_cast<float>(course_.start.carYaw)) >= 0.f ? 1.f : -1.f;
        fullCarPos_ = courses::toF(course_.start.carPos) - Vec3{0.f, dirY * 500.f, 0.f};
        fullYawDeg_ = static_cast<float>(course_.start.carYaw) * kRadToDeg;
        pickupSetup_ = pickup::makeSetup(fullCarPos_, fullYawDeg_, pickup_.niveau(), rng);
        break;
    }
    case CoursesKind::Catch: {
        // A ball resting 12 m in front of the car, lobbed toward it at GO: up at 1000 uu/s, 200 uu/s toward the car
        // (top about 8.6 m, back at roof height after 3 s, about 6 m in front of the car), more to the side later.
        for (int attempt = 0; attempt < 8; ++attempt) {
            fullCarPos_ = {rng.range(-2000.f, 2000.f), rng.range(-2500.f, 0.f), 17.f};
            fullYawDeg_ = rng.range(-180.f, 180.f);
            const float yaw = fullYawDeg_ * kDegToRad;
            const Vec3 fwd{std::cos(yaw), std::sin(yaw), 0.f}, right{-std::sin(yaw), std::cos(yaw), 0.f};
            const float lateral = lerpf(0.f, 250.f, d) * rng.range(0.6f, 1.f) * (rng.chance(0.5f) ? 1.f : -1.f);
            fullBallPos_ = fullCarPos_ + fwd * 1200.f;
            fullBallPos_.z = 93.15f;
            fullBallVel_ = fwd * -200.f + right * lateral + Vec3{0.f, 0.f, 1000.f};
            const Vec3 land = fullBallPos_ + Vec3{fullBallVel_.x, fullBallVel_.y, 0.f} * 3.1f;
            if (std::fabs(fullBallPos_.x) < 3600.f && std::fabs(fullBallPos_.y) < 4400.f && std::fabs(land.x) < 3600.f &&
                std::fabs(land.y) < 4400.f)
                break;
        }
        break;
    }
    }
}

// ---- Steps -------------------------------------------------------------------------------------------------------
void CoursesModule::buildFullSteps() {
    steps_.clear();
    pickup_.beginAttempt();
    fullStarted_ = false;
    ringFrozen_ = false;
    fullAirborne_ = false;
    popJumpTime_ = -1.f;
    boostAtPop_ = 0.f;
    ringPassed_ = false;
    ringMissed_ = false;
    ballWasHigh_ = false;
    fullSpeedSum_ = 0.f;
    fullSpeedSamples_ = 0;
    fullStartT_ = 0.f;
    bodyOnFloor_ = 0.f;
    lowTouch_ = false;
    const float d = fullD();
    switch (kind_) {
    case CoursesKind::RingsAir: {
        steps_.add(makeStep(kAirJump, "Saute",
                            stepWhenSnap([](const Snapshot& s) {
                                return s.car.valid && !s.car.onGround && s.car.wheelContacts == 0 && (s.car.jumped || s.in.jump);
                            }),
                            lerpf(4.f, 2.5f, d), "courses.full_air_no_jump",
                            "Saute avant d'arriver sous l'anneau : il faut un peu de temps pour monter."));
        const float noseZ = std::sin(lerpf(25.f, 30.f, d) * kDegToRad);
        steps_.add(makeStep(kAirNose, "Lève le nez", stepWhenSnap([noseZ](const Snapshot& s) { return s.car.basis.fwd.z > noseZ; }),
                            lerpf(0.8f, 0.35f, d), "courses.full_air_nose",
                            "Tire le stick vers toi dès que tu sautes : le nez doit monter vers l'anneau."));
        const float zMin = lerpf(400.f, 500.f, d);
        steps_.add(makeStep(kAirBoost, "Boost vers l'anneau",
                            stepWhenSnap([zMin](const Snapshot& s) {
                                return (s.in.boost || s.in.holdingBoost) && s.car.basis.fwd.z > 0.3f && hitboxCenter(s.car).z > zMin;
                            }),
                            lerpf(2.f, 1.2f, d), "courses.full_air_boost",
                            "Reste sur le boost avec le nez levé : c'est le boost qui te fait monter."));
        steps_.add(makeStep(kAirRing, "Passe l'anneau",
                            [this](const StepInput& in) {
                                if (ringPassed_) return StepVerdict::done();
                                if (ringMissed_)
                                    return StepVerdict::fail("courses.full_air_ring",
                                                             "Garde le nez vers le centre de l'anneau en boostant.");
                                if (fullAirborne_ && in.snap.car.onGround)
                                    return StepVerdict::fail("courses.full_air_fell",
                                                             "Garde le boost jusqu'à l'anneau, nez vers son centre.");
                                return StepVerdict::pending();
                            },
                            lerpf(4.f, 3.f, d), "courses.full_air_ring", "Garde le nez vers le centre de l'anneau en boostant."));
        steps_.add(makeStep(kAirLand, "Atterris sur les roues",
                            [this](const StepInput& in) {
                                const CarState& c = in.snap.car;
                                if (c.onGround && c.basis.up.z > 0.7f) return StepVerdict::done();
                                if (bodyOnFloor_ > 0.2f) return StepVerdict::fail();
                                return StepVerdict::pending();
                            },
                            5.f, "courses.full_air_landing",
                            "Lâche le boost après l'anneau et redresse la voiture : roues vers le sol avant de toucher."));
        break;
    }
    case CoursesKind::RingsGround: {
        const float lo = lerpf(10.f, 20.f, d), hi = lerpf(60.f, 45.f, d);
        steps_.add(makeStep(kWallApproach, "Roule vers le mur en biais",
                            [this, lo, hi](const StepInput& in) {
                                const CarState& c = in.snap.car;
                                const float side = static_cast<float>(fullSide_);
                                if (side * c.pos.x < 4096.f - 380.f) return StepVerdict::pending();
                                Vec3 dir{c.vel.x, c.vel.y, 0.f};
                                if (length(dir) < 200.f) dir = Vec3{c.basis.fwd.x, c.basis.fwd.y, 0.f};
                                const float l = length(dir);
                                if (l < 1e-3f || side * dir.x <= 0.f) return StepVerdict::pending();
                                const float off = std::acos(std::clamp(side * dir.x / l, 0.f, 1.f)) * kRadToDeg;
                                if (off < lo)
                                    return StepVerdict::fail("courses.full_wall_straight",
                                                             "Arrive plus en biais : tourne un peu vers l'arche avant le mur.");
                                if (off > hi)
                                    return StepVerdict::fail("courses.full_wall_flat",
                                                             "Arrive plus face au mur : trop de côté, tu ne montes pas.");
                                return StepVerdict::done();
                            },
                            lerpf(6.f, 4.f, d), "courses.full_wall_approach", "Roule vers le mur, un peu en biais vers l'arche."));
        steps_.add(makeStep(kWallOn, "Monte sur le mur",
                            stepWhenSnap([](const Snapshot& s) { return s.car.wheelContacts >= 3 && std::fabs(s.car.basis.up.x) > 0.7f; }),
                            lerpf(3.f, 2.f, d), "courses.full_wall_climb",
                            "Garde l'accélérateur dans la courbe du mur : roues collées, tu montes tout seul."));
        steps_.add(makeStep(kWallArch, "Passe l'arche",
                            [this](const StepInput& in) {
                                if (ringPassed_) return StepVerdict::done();
                                const CarState& c = in.snap.car;
                                if (ringMissed_ || (c.onGround && c.basis.up.z > 0.7f && c.pos.z < 60.f))
                                    return StepVerdict::fail("courses.full_wall_arch", "Vise le centre de l'arche en montant le mur.");
                                return StepVerdict::pending();
                            },
                            lerpf(4.f, 3.f, d), "courses.full_wall_arch", "Vise le centre de l'arche en montant le mur."));
        const float vMin = lerpf(300.f, 500.f, d);
        steps_.add(makeStep(kWallDown, "Redescends en roulant",
                            stepWhenSnap([vMin](const Snapshot& s) {
                                return s.car.onGround && s.car.basis.up.z > 0.7f && s.car.pos.z < 60.f && speed(s.car) > vMin;
                            }),
                            lerpf(4.f, 3.f, d), "courses.full_wall_down",
                            "Redescends du mur en roulant vers le sol, sans sauter dans le vide : tu gardes ta vitesse."));
        break;
    }
    case CoursesKind::RingsAirDribble: {
        pickup::addSteps(steps_, pickup::options(pickupSetup_.rolling));
        const float vLo = lerpf(300.f, 500.f, d), vHi = lerpf(1100.f, 900.f, d);
        steps_.add(makeStep(kAdCarry, "Roule avec la balle sur le toit",
                            [vLo, vHi](const StepInput& in) {
                                const Snapshot& s = in.snap;
                                if (ballOnFloorNow(s)) return StepVerdict::fail("pickup.dropped", pickup::kDropTip);
                                const float v = speed(s.car);
                                return pickup::ballOnRoof(s) && v >= vLo && v <= vHi ? StepVerdict::done() : StepVerdict::pending();
                            },
                            lerpf(6.f, 4.f, d), "courses.full_ad_carry",
                            "Roule avec la balle sur le toit, ni trop lentement ni trop vite.", lerpf(0.8f, 1.f, d), 0.3f));
        const float vz = lerpf(200.f, 300.f, d), window = lerpf(0.5f, 0.3f, d);
        steps_.add(makeStep(kAdPop, "Saute pour soulever la balle",
                            [this, vz, window](const StepInput& in) {
                                const Snapshot& s = in.snap;
                                if (ballOnFloorNow(s)) return StepVerdict::fail("pickup.dropped", pickup::kDropTip);
                                if (popJumpTime_ < 0.f) return StepVerdict::pending();
                                const Vec3 local = s.car.basis.toLocal(s.ball.pos - hitboxCenter(s.car));
                                const bool above = local.z > s.car.hitboxExtent.z * 0.5f + s.ball.radius - 10.f;
                                if (above && s.ball.vel.z > vz) return StepVerdict::done();
                                if (runTime_ - popJumpTime_ > window)
                                    return StepVerdict::fail("courses.full_ad_pop",
                                                             "Saute franchement avec la balle bien posée : elle doit décoller "
                                                             "juste au-dessus de toi.");
                                return StepVerdict::pending();
                            },
                            lerpf(5.f, 3.f, d), "courses.full_ad_pop", "Saute avec la balle sur le toit pour la soulever."));
        const float maxDist = lerpf(350.f, 250.f, d);
        steps_.add(makeStep(kAdFollow, "Suis la balle au boost",
                            [this, maxDist](const StepInput& in) {
                                const Snapshot& s = in.snap;
                                if (ballOnFloorNow(s)) return StepVerdict::fail("courses.ad_ball_dropped", "Suis la balle tout de suite : petits coups de boost sous elle.");
                                const bool used = s.in.boost || s.in.holdingBoost || boostAtPop_ - s.car.boost > 1.f;
                                const bool close = distance(s.ball.pos, hitboxCenter(s.car)) < maxDist;
                                return airborne(s.car) && used && close ? StepVerdict::done() : StepVerdict::pending();
                            },
                            2.5f, "courses.full_ad_follow", "Suis la balle tout de suite : boost doucement sous elle, nez un peu levé.",
                            lerpf(0.4f, 0.6f, d), 0.15f));
        steps_.add(makeStep(kAdRing, "Pousse la balle dans l'anneau",
                            [this](const StepInput& in) {
                                if (ringPassed_) return StepVerdict::done();
                                if (ringMissed_ || ballOnFloorNow(in.snap))
                                    return StepVerdict::fail("courses.full_ad_ring",
                                                             "Vise l'anneau avec la balle : pousse-la par en dessous, vers son centre.");
                                return StepVerdict::pending();
                            },
                            lerpf(4.f, 3.f, d), "courses.full_ad_ring",
                            "Vise l'anneau avec la balle : pousse-la par en dessous, vers son centre."));
        break;
    }
    case CoursesKind::Dribble: {
        Step nose = makeStep(kDribbleNose, "Mets ton nez sous la balle",
                             [this](const StepInput&) { return lowTouch_ ? StepVerdict::done() : StepVerdict::pending(); }, 0.f,
                             "", "");
        nose.optional = true;
        steps_.add(nose);
        pickup::addSteps(steps_, pickup::options(pickupSetup_.rolling));
        Step hold = makeStep(kDribbleHold, "Roule en gardant la balle sur le toit",
                             [this](const StepInput&) {
                                 if (keeper_.lost()) {
                                     std::string code = "courses.dribble_lost", tip = "Accélère tout doucement : la balle doit rester posée.";
                                     switch (courses::classifyFall(departRelVel_, departRelPos_)) {
                                     case courses::FallSide::Front:
                                         code = "courses.dribble_front";
                                         tip = "Accélère un peu : la balle glisse vers l'avant du capot.";
                                         break;
                                     case courses::FallSide::Back:
                                         code = "courses.dribble_back";
                                         tip = "Accélère plus doucement : la balle glisse vers l'arrière.";
                                         break;
                                     case courses::FallSide::Side:
                                         code = "courses.dribble_side";
                                         tip = "Tourne plus progressivement : la balle part sur le côté.";
                                         break;
                                     case courses::FallSide::Up:
                                         code = "courses.dribble_pop";
                                         tip = "Touches plus douces : la balle rebondit trop haut sur le toit.";
                                         break;
                                     default: break;
                                     }
                                     return StepVerdict::fail(code, tip);
                                 }
                                 const float avg = fullSpeedSamples_ > 0 ? fullSpeedSum_ / static_cast<float>(fullSpeedSamples_) : 0.f;
                                 return keeper_.heldTime() + 1e-4f >= dribbleTarget_ && avg >= dribbleMinSpeed_ ? StepVerdict::done()
                                                                                                                 : StepVerdict::pending();
                             },
                             dribbleTarget_ + 20.f, "courses.dribble_slow", "Roule un peu plus vite en gardant la balle au centre du toit.");
        steps_.add(hold);
        break;
    }
    case CoursesKind::Slalom: {
        pickup::addSteps(steps_, pickup::options(pickupSetup_.rolling));
        const float laneX = static_cast<float>(course_.start.carPos.x);
        const float gateTime = lerpf(9.f, 6.f, d);
        for (size_t k = 0; k < course_.rings.size(); ++k) {
            const float gx = static_cast<float>(course_.rings[k].center.x);
            const float sign = gx >= laneX ? 1.f : -1.f;
            Step weave = makeStep("", "", [laneX, sign](const StepInput& in) {
                return sign * (in.snap.ball.pos.x - laneX) >= 120.f ? StepVerdict::done() : StepVerdict::pending();
            }, 0.f, "", "");
            weave.id = gateId("slalom.weave.", k);
            weave.label = "Décale-toi vers la porte " + std::to_string(k + 1);
            weave.optional = true;
            steps_.add(weave);
            Step gate = makeStep("", "", [this, k](const StepInput& in) {
                if (run_.nextRing() > static_cast<int>(k)) return StepVerdict::done();
                if (run_.failed())
                    return StepVerdict::fail("courses.rings_missed",
                                             "Décale-toi tôt du côté de la porte : passe en son centre, sans frôler les poteaux.");
                if (keeper_.lost() || ballOnFloorNow(in.snap))
                    return StepVerdict::fail("courses.slalom_off_roof", "Garde la balle posée sur le toit : vire en douceur entre les portes.");
                return StepVerdict::pending();
            }, gateTime, "courses.slalom_slow", "");
            gate.id = gateId("slalom.gate.", k);
            gate.label = "Passe la porte " + std::to_string(k + 1);
            gate.tip = "Avance plus vite vers la porte suivante, la balle bien centrée.";
            steps_.add(gate);
        }
        break;
    }
    case CoursesKind::Catch: {
        const float maxAngle = lerpf(40.f, 25.f, d);
        steps_.add(makeStep(kCatchRead, "Roule vers le cercle",
                            [maxAngle](const StepInput& in) {
                                const Snapshot& s = in.snap;
                                Vec3 land;
                                float t = 0.f;
                                if (!courses::predictDownCrossing(s.ball.pos, s.ball.vel, courses::kCatchRoofZ, 5.f, land, t))
                                    return StepVerdict::pending();
                                const Vec3 c = hitboxCenter(s.car);
                                const Vec3 to{land.x - c.x, land.y - c.y, 0.f};
                                if (length(to) < 150.f) return StepVerdict::done();
                                const Vec3 v{s.car.vel.x, s.car.vel.y, 0.f};
                                if (length(v) < 100.f) return StepVerdict::pending();
                                const float cosA = dot(v, to) / (length(v) * length(to));
                                return cosA >= std::cos(maxAngle * kDegToRad) ? StepVerdict::done() : StepVerdict::pending();
                            },
                            lerpf(1.5f, 0.8f, d), "courses.full_catch_read",
                            "Dès le départ, roule vers le cercle au sol : c'est là que la balle va tomber."));
        const float tol = lerpf(120.f, 80.f, d), vMax = lerpf(300.f, 200.f, d), tMin = lerpf(0.2f, 0.3f, d);
        steps_.add(makeStep(kCatchUnder, "Arrête-toi sous la balle",
                            [tol, vMax, tMin](const StepInput& in) {
                                const Snapshot& s = in.snap;
                                Vec3 land;
                                float t = 0.f;
                                if (!courses::predictDownCrossing(s.ball.pos, s.ball.vel, courses::kCatchRoofZ, 5.f, land, t) ||
                                    t < tMin) {
                                    if (s.ball.vel.z < 0.f && s.ball.pos.z < courses::kCatchRoofZ + 400.f)
                                        return StepVerdict::fail("courses.full_catch_late",
                                                                 "Va sous la balle plus tôt : sois dans le cercle avant qu'elle descende.");
                                    return StepVerdict::pending();
                                }
                                const bool close = flatDistance(hitboxCenter(s.car), land) < tol;
                                return close && speed(s.car) < vMax ? StepVerdict::done() : StepVerdict::pending();
                            },
                            0.f, "courses.full_catch_late", "Va sous la balle plus tôt : sois dans le cercle avant qu'elle descende."));
        const float maxOff = lerpf(80.f, 60.f, d), maxVz = lerpf(150.f, 50.f, d);
        steps_.add(makeStep(kCatchCushion, "Ne saute pas, laisse-la tomber",
                            [this, maxOff, maxVz](const StepInput& in) {
                                if (ballWasHigh_ && ballOnFloorNow(in.snap))
                                    return StepVerdict::fail("courses.catch_no_touch",
                                                             "Place-toi sous le point de chute plus tôt : suis le cercle au sol.");
                                if (!haveFirstTouch_) return StepVerdict::pending();
                                if (firstTouchJump_ > maxVz)
                                    return StepVerdict::fail("courses.catch_jump_into",
                                                             "Ne saute pas vers la balle : attends-la roues au sol, juste sous elle.");
                                if (firstTouchOffset_ > maxOff)
                                    return StepVerdict::fail("courses.catch_off_centre",
                                                             "Mets le centre du toit pile sous la balle : sur le bord, elle glisse.");
                                return StepVerdict::done();
                            },
                            2.5f, "courses.catch_no_touch", "Place-toi sous le point de chute plus tôt : suis le cercle au sol."));
        const float settle = courses::catchSettleTime(courses::catchImpactSpeed({0.f, 0.f, 862.f}, {}));
        steps_.add(makeStep(kCatchHops, "Suis les rebonds",
                            [this](const StepInput& in) {
                                if (ballOnFloorNow(in.snap))
                                    return StepVerdict::fail("courses.catch_fell",
                                                             "Avance doucement sous la balle quand elle rebondit vers l'avant.");
                                return catchLooseOnRoof(in.snap) ? StepVerdict::done() : StepVerdict::pending();
                            },
                            settle + 2.f, "courses.catch_fell", "Avance doucement sous la balle quand elle rebondit vers l'avant.",
                            0.3f, courses::kCatchHopGrace));
        steps_.add(makeStep(kCatchHold, "Garde-la sur le toit",
                            [this](const StepInput& in) {
                                if (ballOnFloorNow(in.snap))
                                    return StepVerdict::fail("courses.catch_fell",
                                                             "Garde la voiture sous la balle : petits ajustements, pas de coups de volant.");
                                return catchLooseOnRoof(in.snap) ? StepVerdict::done() : StepVerdict::pending();
                            },
                            cfg_.catchHold + 2.f, "courses.catch_fell",
                            "Garde la voiture sous la balle : petits ajustements, pas de coups de volant.", cfg_.catchHold,
                            courses::kCatchHopGrace));
        break;
    }
    }
}

// ---- Run ---------------------------------------------------------------------------------------------------------
void CoursesModule::fullRelease(const TickContext& ctx) {
    steps_.start(ctx.snap.time);
    havePrev_ = false;
}

bool CoursesModule::ringStep(const Snapshot& s, double t, bool carObject) {
    if (!run_.active()) return run_.finished();
    const bool valid = carObject ? s.car.valid : s.ball.valid;
    if (!valid) {
        havePrev_ = false;
        return false;
    }
    const courses::DVec3 p1 = courses::toD(carObject ? hitboxCenter(s.car) : s.ball.pos);
    const courses::DVec3 v1 = courses::toD(carObject ? s.car.vel : s.ball.vel);
    if (havePrev_) {
        const double dtRun = t - prevT_;
        const double allowed = std::max(courses::length(prevVel_), courses::length(v1)) * dtRun * 1.5 + 120.0;
        if (courses::length(p1 - prevPos_) > allowed) ++teleports_;
        else run_.step(prevPos_, p1, prevT_, t, &prevVel_, &v1);
    }
    prevPos_ = p1;
    prevVel_ = v1;
    prevT_ = t;
    havePrev_ = true;
    if (run_.failed()) ringMissed_ = true;
    if (run_.finished()) ringPassed_ = true;
    return run_.finished();
}

void CoursesModule::fullTick(const TickContext& ctx, Actions& out, double t, float dt) {
    const Snapshot& s = ctx.snap;
    const float now = s.time;
    switch (kind_) {
    case CoursesKind::RingsAir:
        if (airborne(s.car)) fullAirborne_ = true;
        bodyOnFloor_ = (s.car.pos.z < 80.f && s.car.basis.up.z < 0.5f) ? bodyOnFloor_ + dt : 0.f;
        ringStep(s, t, true);
        parkBallIfNeeded(out);
        break;
    case CoursesKind::RingsGround:
        ringStep(s, t, true);
        parkBallIfNeeded(out);
        break;
    case CoursesKind::RingsAirDribble: {
        const bool onRoof = pickup::ballOnRoof(s);
        if (!ringFrozen_ && !course_.rings.empty()) {
            // The ring waits in front of the car (1500 ahead, 450 above the roof ball) until the pop fixes it.
            const float d = fullD();
            const Vec3 fwd = normalize(Vec3{s.car.basis.fwd.x, s.car.basis.fwd.y, 0.f}, Vec3{0.f, 1.f, 0.f});
            const Vec3 base = s.car.pos + fwd * lerpf(1500.f, 1700.f, d);
            courses::Ring& r = course_.rings[0];
            const double radius = r.radius;
            const uint8_t flags = r.flags;
            r = courses::makeDisk({base.x, base.y, std::round(courses::roofBallOffset().z + 17.0 + lerpf(450.f, 550.f, d))},
                                  courses::toD(normalize(fwd + Vec3{0.f, 0.f, 0.25f}, fwd)), radius);
            r.flags = flags;
            if (steps_.reached(kAdPop) && popJumpTime_ < 0.f && wasOnRoof_ && (s.car.jumped || s.in.jump) && !s.car.onGround) {
                popJumpTime_ = runTime_;
                boostAtPop_ = s.car.boost;
            }
        }
        wasOnRoof_ = onRoof;
        if (steps_.done(kAdPop) && !ringFrozen_) {
            ringFrozen_ = true;  // the ring stays where it is now; the ball path is tested from here
            havePrev_ = false;
        }
        if (ringFrozen_) ringStep(s, t, false);
        break;
    }
    case CoursesKind::Dribble:
    case CoursesKind::Slalom: {
        if (!fullStarted_ && steps_.done(pickup::kStepSettle)) {
            fullStarted_ = true;
            fullStartT_ = static_cast<float>(t);
            keeper_.reset(kind_ == CoursesKind::Dribble ? dribbleTolerance_ : cfg_.roofTolerance);
            wasOnRoof_ = true;
            havePrev_ = false;
        }
        if (fullStarted_) {
            trackRoof(s, t - fullStartT_, dt);
            fullSpeedSum_ += speed(s.car);
            ++fullSpeedSamples_;
            if (kind_ == CoursesKind::Slalom) ringStep(s, t, false);
        } else {
            ballOnRoofNow_ = pickup::ballOnRoof(s);
        }
        break;
    }
    case CoursesKind::Catch:
        if (s.ball.valid && s.ball.pos.z > 200.f) ballWasHigh_ = true;
        ballOnRoofNow_ = catchLooseOnRoof(s);
        break;
    }

    steps_.update(s, &ctx);
    pickup_.update(steps_);
    if (steps_.isComplete()) {
        endFull(ctx, out, true);
        return;
    }
    if (!steps_.failed() && t > 180.0) steps_.failOnTimeout(now);
    if (steps_.failed()) endFull(ctx, out, false);
}

void CoursesModule::endFull(const TickContext& ctx, Actions& out, bool success) {
    const float now = ctx.snap.time;
    int required = 0, requiredDone = 0;
    float limitSum = 0.f;
    for (int i = 0; i < steps_.size(); ++i) {
        const Step& st = steps_.steps()[static_cast<size_t>(i)];
        if (st.optional) continue;
        ++required;
        if (steps_.state(i) == StepState::Done) ++requiredDone;
        limitSum += st.timeout > 0.f ? st.timeout : 3.f;
    }
    const float elapsed = steps_.attemptElapsed(now);
    float score = 0.f;
    if (success) score = 80.f + 20.f * clamp01(1.f - elapsed / std::max(1.f, limitSum));
    else score = required > 0 ? 40.f * static_cast<float>(requiredDone) / static_cast<float>(required) : 0.f;
    score = std::clamp(score, 0.f, 100.f);

    AttemptResult r;
    r.module = "courses";
    r.skill = std::string(kindSkill(kind_)) + ".full";
    r.title = kindFullTitle(kind_);
    r.score = score;
    r.success = success;
    r.verdict = success ? verdictFromScore(score) : Verdict::Fail;
    r.metrics.push_back(metric("steps", "Étapes réussies", static_cast<float>(requiredDone), "", static_cast<float>(required)));
    r.metrics.push_back(metric("time", "Temps", elapsed, "s", 0.f));
    if (kindUsesPickup(kind_))
        r.metrics.push_back(metric("pickup_level", "Niveau du ramassage", static_cast<float>(pickup_.niveau()), "", 0.f));
    if (!success) r.tips.push_back(steps_.missedTip(10.f));
    r.time = now;
    r.valueForRecord = 0.f;  // no record: the start changes every attempt
    r.lowerIsBetter = false;
    r.hasRecordValue = false;

    // A missed pickup only feeds the pickup's own staircase (pickup_.update did it), never the exercise's.
    const bool pickupFailure = !success && steps_.failure().group == pickup::kGroup;
    if (cfg_.difficultyMode == 0 && !pickupFailure) fullStairs_[static_cast<size_t>(kind_)].record(success);
    recordAttemptCounters(success);

    last_ = CoursesLastAttempt{};
    last_.valid = true;
    last_.kind = kind_;
    last_.title = kindFullTitle(kind_);
    last_.success = success;
    last_.score = score;
    last_.verdict = verdictLabel(r.verdict);
    last_.headline = success ? "Parcours complet réussi en " + formatNumber(elapsed, 1) + " s" : "Raté. " + steps_.missingText();
    last_.time = elapsed;
    resultHint_ = r.tips.empty() ? std::string() : r.tips.front().text;
    emitResult(std::move(r), ctx, out);
    enterResult(success);
    out.requestSave = true;
}

// ---- HUD ---------------------------------------------------------------------------------------------------------
void CoursesModule::drawFullHud(const ViewContext& view, const DrawContext& ctx, std::vector<PanelLine>& lines) const {
    (void)view;
    StepHudStyle style;
    appendStepLines(lines, steps_, ctx.snap.time, style);
    if (kind_ == CoursesKind::Dribble && fullStarted_) {
        PanelLine l;
        const float held = keeper_.heldTime();
        l.text = "Tenue " + formatNumber(held, 1) + " / " + formatNumber(dribbleTarget_, 0) + " s";
        l.bar = dribbleTarget_ > 0.f ? clamp01(held / dribbleTarget_) : 0.f;
        l.barColor = colors::Cyan;
        lines.push_back(std::move(l));
    }
}

} // namespace rlt
