#include "core/modules/shooting/ShootingFull.h"

#include <algorithm>
#include <cmath>

namespace rlt::shoot {

namespace {

float randomSign(Rng& rng) { return rng.chance(0.5f) ? 1.f : -1.f; }

float yawToward(const Vec3& dir) { return std::atan2(dir.y, dir.x) * kRadToDeg; }

CarPlacement restingCar(const Vec3& pos, const Vec3& dir) {
    CarPlacement c;
    c.pos = Vec3{pos.x, pos.y, kCarRestZ};
    c.rot = rotFromYawDeg(yawToward(dir));
    return c;
}

float headingErrorTo(const CarState& car, const Vec3& p) {
    const Vec3 f = normalize(flat(car.basis.fwd), Vec3{1.f, 0.f, 0.f});
    const Vec3 d = flat(p - car.pos);
    if (length(d) < 1.f) return 0.f;
    return angleBetweenDeg(f, d);
}

bool ballLost(const Snapshot& s) {
    return s.ball.valid && s.car.valid && s.ball.pos.z < s.ball.radius + 25.f &&
           distance(s.ball.pos, hitboxCenter(s.car)) > 250.f;
}

Step eventStep(const char* id, const char* label, const char* tip, float timeout = 0.f) {
    Step s;
    s.id = id;
    s.label = label;
    s.failCode = std::string("shooting.full.") + id;
    s.tip = tip;
    s.timeout = timeout;
    return s;
}

Step checkStep(const char* id, const char* label, StepCheck check, const char* tip, float timeout = 0.f, float hold = 0.f,
               float grace = 0.f) {
    Step s = eventStep(id, label, tip, timeout);
    s.check = std::move(check);
    s.hold = hold;
    s.holdGrace = grace;
    return s;
}

} // namespace

bool shootBallOnRoof(const Snapshot& s) {
    if (!s.car.valid || !s.ball.valid || s.car.basis.up.z < 0.6f) return false;
    const float r = s.ball.radius > 10.f ? s.ball.radius : kBallRadius;
    return ballOnRoof(s.ball.pos, s.car.pos, s.car.basis, hitboxOf(s.car), r);
}

pickup::Options pickupOptions(bool rolling) {
    pickup::Options o = pickup::options(rolling);
    o.onRoof = shootBallOnRoof;
    o.roofCode = "shooting.pickup_no_roof";
    o.settleCode = "shooting.pickup_unstable";
    o.dropCode = "shooting.pickup_dropped";
    o.jumpCode = "shooting.flick_unsettled";  // a jump before the ball is stable fails the pickup
    return o;
}

// ---- Setups -------------------------------------------------------------------------------------------------
FullSetup makeFullSetup(ShootDrill kind, float level, int pickupNiveau, const GoalFrame& goal, const RoofParams& roof,
                        Rng& rng) {
    const float d = clamp01(level);
    const float s = goal.sign;
    const Vec3 toGoal{0.f, s, 0.f};
    FullSetup su;
    su.kind = kind;
    switch (kind) {
    case ShootDrill::Flick:
    case ShootDrill::Roof: {
        // Shared Ramassage start: car at rest facing the goal, 3500 uu from it for the flick, 4500 for the roof
        // (room for the dribble).
        const float dist = kind == ShootDrill::Flick ? 3500.f : 4500.f;
        const Vec3 carPos{rng.range(-300.f, 300.f), s * (kGoalLineY - dist), kCarRestZ};
        const pickup::Setup p =
            pickup::makeSetup(carPos, yawToward(toGoal), pickupNiveau, rng, roof.ballRadius > 10.f ? roof.ballRadius : kBallRadius);
        su.car = p.car;
        su.ball = p.ball;
        su.pickup = true;
        su.rolling = p.rolling;
        su.timeLimit = kind == ShootDrill::Flick ? 26.f : 30.f;
        break;
    }
    case ShootDrill::Rolling: {
        // Conduite et tir: ball at rest at midfield, car 2500 uu behind it at rest.
        su.ball.pos = Vec3{0.f, 0.f, kBallRestZ};
        su.car = restingCar(Vec3{0.f, -2500.f * s, kCarRestZ}, toGoal);
        su.timeLimit = 16.f;
        break;
    }
    case ShootDrill::Bouncing: {
        // Rebond devant le but: dropped from 900 uu, 2500 uu from the goal; car 3000 uu behind at rest.
        su.ball.pos = Vec3{rng.range(-300.f, 300.f), s * (kGoalLineY - 2500.f), 900.f};
        su.ball.vel = Vec3{0.f, -s * rng.range(0.f, lerpf(100.f, 300.f, d)), 0.f};
        const Vec3 carPos{su.ball.pos.x * 0.5f, su.ball.pos.y - s * 3000.f, kCarRestZ};
        su.car = restingCar(carPos, normalize(flat(su.ball.pos - carPos), toGoal));
        su.bounces = predictFloorBounces(su.ball.pos, su.ball.vel, Vec3{}, 3);
        if (su.bounces.size() >= 2) {
            su.target = su.bounces[1].pos;
            su.targetTime = su.bounces[1].time;
        }
        su.timeLimit = 12.f;
        break;
    }
    case ShootDrill::Pass: {
        // Appel de balle: car at rest on the far-post side, cross from the opposite corner toward the penalty spot;
        // lifted above mid level.
        const float side = randomSign(rng);
        const Vec3 carPos{-side * 1500.f, s * (kGoalLineY - 4000.f), kCarRestZ};
        su.car = restingCar(carPos, toGoal);
        su.ball.pos = Vec3{side * 3300.f, s * (kGoalLineY - 1000.f), kBallRestZ};
        su.aim = Vec3{0.f, s * (kGoalLineY - 1200.f), kBallRestZ};
        const Vec3 dir = normalize(flat(su.aim - su.ball.pos), Vec3{-side, 0.f, 0.f});
        su.ball.vel = dir * lerpf(900.f, 1300.f, d);
        const float lift = d > 0.5f ? lerpf(0.f, 500.f, (d - 0.5f) * 2.f) : 0.f;
        su.ball.vel.z = lift;
        if (lift <= 0.f) su.ball.angVel = rollingSpin(su.ball.vel, roof.ballRadius);
        // Meeting point: the predicted ball point at the middle of the goal (x 0).
        const FloorBounce m = crossMeetingPoint(su.ball.pos, su.ball.vel, su.ball.angVel, Vec3{0.f, 0.f, 0.f});
        su.target = m.pos;
        su.targetTime = m.time;
        su.timeLimit = su.targetTime + 6.f;
        break;
    }
    case ShootDrill::Power: {
        // Frappe de dégagement: the ball comes from the opponent half, rolling at the car.
        const Vec3 carPos{rng.range(-300.f, 300.f), -s * 2500.f, kCarRestZ};
        su.car = restingCar(carPos, toGoal);
        su.ball.pos = Vec3{carPos.x + rng.range(-400.f, 400.f), s * 1500.f, kBallRestZ};
        su.ball.vel = normalize(flat(carPos - su.ball.pos), -toGoal) * lerpf(700.f, 1000.f, d);
        su.ball.angVel = rollingSpin(su.ball.vel, roof.ballRadius);
        su.target = powerMeetingPoint(carPos, su.ball.pos, su.ball.vel);
        su.timeLimit = 10.f;
        break;
    }
    default: break;
    }
    return su;
}

// ---- Live state ---------------------------------------------------------------------------------------------
void fullBegin(FullState& st, const FullSetup& setup, int niveau, const GoalFrame& goal, const ZoneRect& zone,
               const Hitbox& hb, float ballRadius, float goTime) {
    st = FullState{};
    st.setup = setup;
    st.kind = setup.kind;
    st.niveau = std::clamp(niveau, 1, 10);
    st.goal = goal;
    st.zone = zone;
    st.hb = hb;
    st.ballRadius = ballRadius;
    st.goTime = goTime;
    st.now = goTime;
}

void fullTick(FullState& st, const Snapshot& s) {
    st.now = s.time;
    if (!s.ball.valid) return;
    if (st.hasPrev && st.prevBallVz < -20.f && s.ball.vel.z > st.prevBallVz + 40.f && s.ball.vel.z >= -5.f &&
        s.ball.pos.z < kBallRestZ + 40.f) {
        ++st.floorBounces;
        st.lastBounceTime = s.time;
    }
    st.prevBallVz = s.ball.vel.z;
    st.hasPrev = true;
}

// ---- Steps --------------------------------------------------------------------------------------------------
std::vector<Step> fullSteps(FullState& st) {
    FullState* p = &st;
    std::vector<Step> v;
    switch (st.kind) {
    case ShootDrill::Flick: {
        const pickup::Options o = pickupOptions(st.setup.rolling);
        v.push_back(pickup::roofStep(o));
        v.push_back(pickup::settleStep(o));
        v.push_back(checkStep(
            "carry", "Porte-la en roulant",
            [](const StepInput& in) {
                if (ballLost(in.snap)) return StepVerdict::fail("shooting.full.carry_lost", pickup::kDropTip);
                return shootBallOnRoof(in.snap) && length2D(in.snap.car.vel) >= kCarryMinSpeed ? StepVerdict::done()
                                                                                                  : StepVerdict::pending();
            },
            "Roule doucement vers le but avec la balle sur le toit, sans à-coups.", 8.f, 1.f, 0.3f));
        Step flick = eventStep(kStepFlick, "Flick", "Saute puis flip pendant que la balle est encore sur ton toit.", 8.f);
        flick.check = [](const StepInput& in) {
            return ballLost(in.snap) ? StepVerdict::fail("shooting.flick_lost_ball",
                                                         "Garde la balle au centre du toit avant de flick.")
                                     : StepVerdict::pending();
        };
        v.push_back(flick);
        v.push_back(eventStep(kStepCadre, "Cadre-le, assez fort",
                              "Flick vers le but, stick à fond dans la direction du flip."));
        break;
    }
    case ShootDrill::Roof: {
        const pickup::Options o = pickupOptions(st.setup.rolling);
        v.push_back(pickup::roofStep(o));
        v.push_back(pickup::settleStep(o));
        v.push_back(checkStep(
            "dribble", "Dribble vers le but",
            [p](const StepInput& in) {
                const Snapshot& s = in.snap;
                if (ballLost(s)) return StepVerdict::fail("shooting.full.carry_lost", pickup::kDropTip);
                const float v = length2D(s.car.vel);
                const Vec3 zoneWorld = p->goal.toWorld(p->zone.u, p->zone.z);
                const bool ok = shootBallOnRoof(s) && v >= kDribbleMinSpeed && v <= kDribbleMaxSpeed &&
                                headingErrorTo(s.car, zoneWorld) < 30.f;
                return ok ? StepVerdict::done() : StepVerdict::pending();
            },
            "Roule vers le but avec la balle sur le toit, ni trop lent ni trop vite, le nez vers la zone.", 10.f, 1.5f,
            0.3f));
        Step flick = eventStep(kStepFlick, "Flick", "Saute puis flip pendant que la balle est encore sur ton toit.", 8.f);
        flick.check = [](const StepInput& in) {
            return ballLost(in.snap) ? StepVerdict::fail("shooting.flick_lost_ball",
                                                         "Garde la balle au centre du toit avant de flick.")
                                     : StepVerdict::pending();
        };
        v.push_back(flick);
        v.push_back(eventStep(kStepZone, "Dans la zone", "Oriente ta voiture vers la zone avant de sauter."));
        break;
    }
    case ShootDrill::Rolling: {
        v.push_back(eventStep(kStepFirst, "Va à la balle sans la frapper",
                              "Arrive sur la balle à peine plus vite qu'elle : la première touche la garde devant toi.",
                              6.f));
        v.push_back(checkStep(
            "push", "Pousse-la vers le but",
            [p](const StepInput& in) {
                const Snapshot& s = in.snap;
                if (!s.ball.valid || !s.car.valid) return StepVerdict::pending();
                const float dist = p->goal.distanceToPlane(s.ball.pos);
                const Vec3 toGoal = flat(Vec3{0.f, p->goal.planeY(), 0.f} - s.ball.pos);
                const bool towardGoal = length2D(s.ball.vel) > 300.f &&
                                        dot(normalize(flat(s.ball.vel), toGoal), normalize(toGoal, toGoal)) > 0.8f;
                const bool behind = p->goal.distanceToPlane(s.car.pos) > dist;
                if (towardGoal && behind && dist >= 2000.f && dist <= 3500.f) return StepVerdict::done();
                if (dist < 1500.f) return StepVerdict::fail();
                return StepVerdict::pending();
            },
            "Pousse d'abord la balle vers le but en douceur, en restant derrière elle.", 10.f, 0.2f));
        v.push_back(eventStep(kStepShot, "Tire", "Tire fort avec le nez, en accélérant.", 8.f));
        v.push_back(eventStep(kStepZone, "Dans la zone", "Vise la zone : touche la balle du côté opposé à la zone."));
        break;
    }
    case ShootDrill::Bouncing: {
        v.push_back(checkStep(
            "read", "Lis les rebonds",
            [p](const StepInput& in) {
                if (!in.snap.car.valid) return StepVerdict::pending();
                return headingErrorTo(in.snap.car, p->setup.target) < 30.f ? StepVerdict::done() : StepVerdict::pending();
            },
            "Tourne ton nez vers le deuxième cercle dès le départ.", 1.f));
        v.push_back(checkStep(
            "arrive", "Arrive au deuxième rebond",
            [p](const StepInput& in) {
                if (p->floorBounces < 2 || !in.snap.car.valid) return StepVerdict::pending();
                return length2D(hitboxCenter(in.snap.car) - p->setup.target) < 300.f ? StepVerdict::done()
                                                                                       : StepVerdict::fail();
            },
            "Arrive au deuxième cercle juste quand la balle y retombe."));
        v.push_back(eventStep(kStepVolley, "Tire quand elle remonte",
                              "Tire juste après le rebond, quand la balle remonte, en accélérant.", 2.f));
        v.push_back(eventStep(kStepZone, "Dans la zone", "Vise la zone en couleur."));
        break;
    }
    case ShootDrill::Pass: {
        v.push_back(checkStep(
            "start", "Démarre au bon moment",
            [p](const StepInput& in) {
                if (!in.snap.car.valid) return StepVerdict::pending();
                if (length2D(in.snap.car.vel) > 500.f) return StepVerdict::done();
                return p->elapsed() > 0.5f * p->setup.targetTime ? StepVerdict::fail() : StepVerdict::pending();
            },
            "Pars avant que le centre soit à mi-chemin."));
        v.push_back(checkStep(
            "cut", "Coupe la trajectoire",
            [p](const StepInput& in) {
                if (!in.snap.car.valid) return StepVerdict::pending();
                const float t = p->elapsed();
                const bool there = length2D(hitboxCenter(in.snap.car) - p->setup.target) < 300.f;
                if (there && t >= p->setup.targetTime - 0.5f) return StepVerdict::done();
                return t > p->setup.targetTime + 0.2f ? StepVerdict::fail() : StepVerdict::pending();
            },
            "Règle ta course pour arriver au point de rencontre juste avant la balle."));
        v.push_back(eventStep(kStepOneTouch, "Tire du premier coup",
                              "Tire dès la première touche, fort, vers le but.", 3.f));
        v.push_back(eventStep(kStepZone, "Dans la zone", "Vise la zone en couleur."));
        break;
    }
    case ShootDrill::Power: {
        v.push_back(checkStep(
            "runup", "Prends de l'élan",
            [](const StepInput& in) {
                const Snapshot& s = in.snap;
                if (!s.car.valid || !s.ball.valid) return StepVerdict::pending();
                if (distance(hitboxCenter(s.car), s.ball.pos) > 800.f) return StepVerdict::pending();
                return length2D(s.car.vel) > kRunUpSpeed ? StepVerdict::done() : StepVerdict::fail();
            },
            "Boost à fond dès le départ : tu dois être presque au maximum en arrivant sur la balle."));
        v.push_back(checkStep(
            "align", "Aligne balle et but",
            [p](const StepInput& in) {
                const Snapshot& s = in.snap;
                if (!s.car.valid || !s.ball.valid) return StepVerdict::pending();
                const Vec3 line = flat(Vec3{0.f, p->goal.planeY(), 0.f} - s.ball.pos);
                return angleBetweenDeg(flat(s.car.basis.fwd), line) < 15.f ? StepVerdict::done() : StepVerdict::pending();
            },
            "Place ton nez, la balle et le but sur la même ligne avant de frapper."));
        v.push_back(eventStep(kStepDodge, "Flip dans la balle", "Fais un flip avant juste avant le contact."));
        v.push_back(eventStep(kStepPower, "Puissance", "Arrive plus vite et flip au dernier moment."));
        break;
    }
    default: break;
    }
    return v;
}

std::vector<std::string> fullLabels(ShootDrill kind, int niveau) {
    FullState st;
    st.kind = kind;
    st.niveau = std::clamp(niveau, 1, 10);
    const auto steps = fullSteps(st);
    std::vector<std::string> out;
    for (size_t i = 0; i < steps.size(); ++i)
        out.push_back(std::to_string(i + 1) + "/" + std::to_string(steps.size()) + " " + steps[i].label);
    return out;
}

} // namespace rlt::shoot
