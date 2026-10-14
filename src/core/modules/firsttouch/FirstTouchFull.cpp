#include "core/modules/firsttouch/FirstTouchFull.h"

#include <algorithm>
#include <cmath>

namespace rlt::ft {

namespace {
constexpr float kBallZ = 93.15f;
constexpr float kGravity = 650.f;
constexpr float kTouchZ = 170.f;  // ball centre a car on the ground can touch

float randomSign(Rng& rng) { return rng.chance(0.5f) ? 1.f : -1.f; }

Vec3 flatDir(const Vec3& v, const Vec3& fallback) { return normalize(flat(v), fallback); }

// Angle (degrees) between the car's nose and the direction to `p`, on the floor plane.
float headingErrorTo(const CarState& car, const Vec3& p) {
    const Vec3 f = flatDir(car.basis.fwd, Vec3{1.f, 0.f, 0.f});
    const Vec3 d = flat(p - car.pos);
    if (length(d) < 1.f) return 0.f;
    return angleBetweenDeg(f, d);
}

float horizontalDistance(const Vec3& a, const Vec3& b) { return length2D(a - b); }

// Lob thrown from `startZ` toward the car so that it lands near `land` after reaching `apex`.
BallPlacement lobToward(const Vec3& land, const Vec3& towardCar, float apex, float vh, float startZ = 150.f) {
    const float vz = std::sqrt(2.f * kGravity * std::max(10.f, apex - startZ));
    const float t = vz / kGravity + std::sqrt(2.f * std::max(1.f, apex - kBallZ) / kGravity);
    BallPlacement b;
    b.pos = land - towardCar * (vh * t);
    b.pos.z = startZ;
    b.vel = towardCar * vh;
    b.vel.z = vz;
    return b;
}

// First predicted sample where the ball comes down through kTouchZ after at least `minBounces` floor bounces, with a
// downward speed under `maxVz` (0 = any). Returns the time (s), -1 when none.
float descentPoint(const BallTrack& tr, int minBounces, float maxVz, Vec3& out, int& bouncesBefore) {
    const float dt = 2.f / kTickRate;  // track points every 2 ticks
    int bounces = 0;
    size_t b = 0;
    for (size_t i = 1; i < tr.points.size(); ++i) {
        const float t = static_cast<float>(i) * dt;
        while (b < tr.floorBounceTimes.size() && tr.floorBounceTimes[b] <= t) {
            ++bounces;
            ++b;
        }
        if (bounces < minBounces) continue;
        const Vec3& p0 = tr.points[i - 1];
        const Vec3& p1 = tr.points[i];
        if (!(p0.z > kTouchZ && p1.z <= kTouchZ)) continue;
        const float vz = (p0.z - p1.z) / dt;
        if (maxVz > 0.f && vz > maxVz) continue;
        out = p1;
        bouncesBefore = bounces;
        return t;
    }
    return -1.f;
}

// Highest point between the first and the second floor bounce.
float bounceApex(const BallTrack& tr, Vec3& out) {
    if (tr.floorBounceTimes.empty()) return -1.f;
    const float dt = 2.f / kTickRate;
    const float t0 = tr.floorBounceTimes[0];
    const float t1 = tr.floorBounceTimes.size() > 1 ? tr.floorBounceTimes[1] : 1e9f;
    float best = -1.f, bestT = -1.f;
    for (size_t i = 0; i < tr.points.size(); ++i) {
        const float t = static_cast<float>(i) * dt;
        if (t <= t0 || t >= t1) continue;
        if (tr.points[i].z > best) {
            best = tr.points[i].z;
            bestT = t;
            out = tr.points[i];
        }
    }
    return bestT;
}

void fillLanding(FullSetup& su) {
    if (!su.track.floorBounces.empty()) {
        su.landing = su.track.floorBounces.front();
        su.landingTime = su.track.floorBounceTimes.front();
    }
}

Step makeStep(const char* id, const char* label, StepCheck check, float timeout, const char* tip, float hold = 0.f) {
    Step s;
    s.id = id;
    s.label = label;
    s.check = std::move(check);
    s.timeout = timeout;
    s.hold = hold;
    s.failCode = std::string("ft.full.") + id;
    s.tip = tip;
    return s;
}

Step touchStep(const char* label) {
    Step s;
    s.id = kFullTouchStep;
    s.label = label;
    s.failCode = "ft.full.touch";
    s.tip = "Touche la balle tout doucement, avec le nez.";
    return s;
}

Step keepStep() {
    Step s;
    s.id = kFullKeepStep;
    s.label = "Garde-la près de toi";
    s.failCode = "ft.full.keep";
    s.tip = "Après la touche, roule avec la balle pour la garder devant ton capot.";
    return s;
}

// Fails the step when a car touch happened while it was the current one (the module also does it from the event).
bool touchedBefore(const FullState& st) { return st.touched; }

} // namespace

// ---- Setups -------------------------------------------------------------------------------------------------
FullSetup makeFullSetup(DrillType t, const DrillPose& player, int team, float level, Rng& rng) {
    const float l = clamp01(level);
    const float s = teamSign(team);
    FullSetup su;
    su.type = t == DrillType::Mix ? DrillType::RollIn : t;
    switch (su.type) {
    case DrillType::RollIn: {
        // Renvoi adverse: the ball rests 3500 uu ahead and 400..600 uu to the side, then rolls at the car's start.
        su.car = anchorPose(player, team, kAnchorY);
        Vec3 b = su.car.pos + su.car.fwd * 3500.f + su.car.right * (randomSign(rng) * rng.range(400.f, 600.f));
        b.z = kBallZ;
        b = clampFloorBall(b);
        const Vec3 dir = flatDir(su.car.pos - b, -su.car.fwd);
        su.ball.pos = b;
        su.ball.vel = dir * rng.range(500.f, 700.f);
        su.timeLimit = 12.f;
        break;
    }
    case DrillType::RollAway: {
        // Récupère un dégagement: the ball rests 200 uu ahead, then leaves at 900..1200 uu/s within 20 degrees.
        su.car = anchorPose(player, team, kAwayAnchorY);
        Vec3 b = su.car.pos + su.car.fwd * 200.f;
        b.z = kBallZ;
        const float a = randomSign(rng) * rng.range(0.f, 20.f) * kDegToRad;
        const Vec3 dir = normalize(su.car.fwd * std::cos(a) + su.car.right * std::sin(a), su.car.fwd);
        su.ball.pos = b;
        su.ball.vel = dir * rng.range(900.f, 1200.f);
        su.timeLimit = 10.f;
        break;
    }
    case DrillType::Pass: {
        // Passe d'un coéquipier: car on a wing (x +-1500, own half), meeting point 1800 uu ahead; the pass comes from
        // the other wing, 20 degrees forward, and reaches the meeting point after about 4 s. (The audit's start at
        // (-+3000, -1000) sends the ball square across the car's route, which the "accompagne" step, under 35 degrees
        // between the two routes, cannot accept.)
        const float sigma = randomSign(rng);
        su.car = poseFromYaw(Vec3{sigma * 1500.f, -3000.f * s, 17.01f}, s > 0.f ? 90.f : -90.f);
        const Vec3 meet = su.car.pos + su.car.fwd * 1800.f;
        const float a = 20.f * kDegToRad;
        const Vec3 dir = normalize(Vec3{sigma * std::cos(a), 0.f, 0.f} + su.car.fwd * std::sin(a), su.car.fwd);
        const float v = rng.range(700.f, 900.f);
        Vec3 b = meet - dir * (v * 3.4f);
        b.z = kBallZ;
        su.ball.pos = clampFloorBall(b);
        su.ball.vel = dir * v;
        su.target = Vec3{meet.x, meet.y, kBallZ};
        break;
    }
    case DrillType::Lob: {
        // Lob adverse: the car rests 1800 uu (niveau 1) to 2500 uu (niveau 10) from the landing point, facing it. The
        // lob peaks at 550..620: higher, the ball would come down after its bounce too fast for a soft touch (the
        // touch step asks under 450 uu/s), and a longer flight needs a higher lob.
        su.car = anchorPose(player, team, kAwayAnchorY);
        const Vec3 land = su.car.pos + su.car.fwd * lerpf(1800.f, 2500.f, l) + su.car.right * rng.range(-150.f, 150.f);
        su.ball = lobToward(land, -su.car.fwd, rng.range(550.f, 620.f), lerpf(250.f, 500.f, l) * rng.range(0.9f, 1.1f));
        break;
    }
    case DrillType::Wall: {
        // Balle qui sort du mur: car on the anchor (x 0), ball held high on a side wall 2000 uu ahead, then pushed
        // off. Falling from that high it comes down too fast after its first bounce for a soft touch: the target is
        // where it comes down slowly, after its bounces.
        su.car = poseFromYaw(Vec3{0.f, -3000.f * s, 17.01f}, s > 0.f ? 90.f : -90.f);
        const float sigma = randomSign(rng);
        su.ball.pos = Vec3{sigma * 3985.f, su.car.pos.y + su.car.fwd.y * 2000.f, rng.range(900.f, 1100.f)};
        su.ball.vel = Vec3{-sigma * lerpf(rng.range(200.f, 250.f), rng.range(250.f, 300.f), l), 0.f, 0.f};
        break;
    }
    case DrillType::High: {
        // Lob haut adverse: thrown from 1500 uu (niveau 1) to 2000 uu (niveau 10) ahead, apex 700..800, landing about
        // 600 uu in front of the car's start; after the bounce it rises to about 330, where a held single jump meets
        // it. (From 2500 uu, the audit's figure, the ball keeps about 490 uu/s along the floor after its bounce and the
        // jump step, under 400 uu/s of difference, cannot pass.)
        su.car = anchorPose(player, team, kAnchorY);
        const float startDist = lerpf(1500.f, 2000.f, l);
        const float apex = rng.range(700.f, 800.f);
        const float vz = std::sqrt(2.f * kGravity * (apex - 150.f));
        const float tFlight = vz / kGravity + std::sqrt(2.f * (apex - kBallZ) / kGravity);
        su.ball = lobToward(su.car.pos + su.car.fwd * 600.f, -su.car.fwd, apex, (startDist - 600.f) / tFlight);
        break;
    }
    case DrillType::Mix: break;
    }
    su.ball.angVel = Vec3{};
    su.track = predictBall(su.ball.pos, su.ball.vel, su.ball.angVel, 6.f);
    fillLanding(su);
    switch (su.type) {
    case DrillType::Pass: {
        // Time the pass is closest to the meeting point.
        float best = 1e9f;
        for (size_t i = 0; i < su.track.points.size(); ++i) {
            const float d = horizontalDistance(su.track.points[i], su.target);
            if (d < best) {
                best = d;
                su.targetTime = static_cast<float>(i) * 2.f / kTickRate;
            }
        }
        su.timeLimit = su.targetTime + 4.f;
        break;
    }
    case DrillType::Lob: {
        int before = 1;
        su.targetTime = descentPoint(su.track, 1, 0.f, su.target, before);
        su.bouncesBeforeTarget = 1;
        if (su.targetTime < 0.f) {
            su.target = su.landing;
            su.targetTime = su.landingTime;
        }
        su.timeLimit = su.targetTime + 3.f;
        break;
    }
    case DrillType::Wall: {
        int before = 1;
        su.targetTime = descentPoint(su.track, 1, 420.f, su.target, before);
        su.bouncesBeforeTarget = before;
        if (su.targetTime < 0.f) {
            su.target = su.landing;
            su.targetTime = su.landingTime;
            su.bouncesBeforeTarget = 1;
        }
        su.timeLimit = su.targetTime + 3.f;
        break;
    }
    case DrillType::High: {
        su.targetTime = bounceApex(su.track, su.target);
        if (su.targetTime < 0.f) {
            su.target = su.landing;
            su.targetTime = su.landingTime;
        }
        su.bouncesBeforeTarget = 1;
        su.timeLimit = su.targetTime + 3.5f;
        break;
    }
    default: break;
    }
    su.target.z = std::max(su.target.z, kBallZ);
    return su;
}

// ---- Live state ---------------------------------------------------------------------------------------------
void fullBegin(FullState& st, const FullSetup& setup, DrillType t, int niveau, float goTime) {
    st = FullState{};
    st.setup = setup;
    st.type = t;
    st.niveau = std::clamp(niveau, 1, 10);
    st.goTime = goTime;
    st.now = goTime;
}

void fullTick(FullState& st, const Snapshot& s) {
    st.now = s.time;
    if (s.ball.valid) {
        // A floor bounce: falling ball near the floor whose vertical speed flips.
        if (st.hasPrev && st.prevBallVz < -20.f && s.ball.vel.z > st.prevBallVz + 40.f && s.ball.vel.z >= -5.f &&
            s.ball.pos.z < kBallZ + 40.f)
            ++st.floorBounces;
        st.prevBallVz = s.ball.vel.z;
        st.hasPrev = true;
    }
    if (st.touched && s.car.valid && !st.landedAfterTouch) {
        if (airborne(s.car)) {
            st.airborneSinceTouch = true;
        } else if (st.airborneSinceTouch) {
            st.landedAfterTouch = true;
            st.ballDistAtLanding = s.ball.valid ? distance(s.ball.pos, hitboxCenter(s.car)) : 1e9f;
        }
    }
}

void fullOnTouch(FullState& st, const Snapshot& pre) {
    if (st.touched) return;
    st.touched = true;
    st.bouncesAtTouch = st.floorBounces;
    st.touchTime = pre.time;
    st.airborneAtTouch = pre.car.valid && airborne(pre.car);
    st.airborneSinceTouch = st.airborneAtTouch;
}

// ---- Steps --------------------------------------------------------------------------------------------------
std::vector<Step> fullSteps(FullState& st) {
    FullState* p = &st;
    std::vector<Step> v;
    const int n = st.niveau;
    switch (st.type) {
    case DrillType::RollIn: {
        v.push_back(makeStep(
            "align", "Aligne-toi sur la balle",
            [p](const StepInput& in) {
                const Snapshot& s = in.snap;
                if (!s.car.valid || !s.ball.valid) return StepVerdict::pending();
                const Vec3 dir = flatDir(length2D(s.ball.vel) > 100.f ? s.ball.vel : p->setup.ball.vel, -s.car.basis.fwd);
                const bool facing = dot(flatDir(s.car.basis.fwd, Vec3{1.f, 0.f, 0.f}), -dir) > 0.94f;
                const Vec3 rel = flat(hitboxCenter(s.car) - s.ball.pos);
                const float off = length(rel - dir * dot(rel, dir));
                return facing && off < 120.f ? StepVerdict::done() : StepVerdict::pending();
            },
            2.f, "Tourne-toi tout de suite face à la balle et place-toi sur sa route."));
        v.push_back(makeStep(
            "slow", "Ralentis avant qu'elle arrive",
            [](const StepInput& in) {
                const Snapshot& s = in.snap;
                if (!s.car.valid || !s.ball.valid) return StepVerdict::pending();
                const float d = horizontalDistance(hitboxCenter(s.car), s.ball.pos);
                const float fwdSpeed = dot(s.car.vel, s.car.basis.fwd);
                return d <= 600.f && fwdSpeed < 300.f ? StepVerdict::done() : StepVerdict::pending();
            },
            0.f, "Ralentis presque à l'arrêt avant que la balle arrive : ta vitesse s'ajoute à la sienne."));
        v.push_back(touchStep("Touche douce"));
        break;
    }
    case DrillType::RollAway: {
        auto behind = [](const Snapshot& s, float& along, float& side) {
            const Vec3 dir = flatDir(s.ball.vel, s.car.basis.fwd);
            const Vec3 rel = flat(s.ball.pos - hitboxCenter(s.car));
            along = dot(rel, dir);
            side = length(rel - dir * along);
        };
        v.push_back(makeStep(
            "catch", "Rattrape-la",
            [behind](const StepInput& in) {
                const Snapshot& s = in.snap;
                if (!s.car.valid || !s.ball.valid) return StepVerdict::pending();
                float along = 0.f, side = 0.f;
                behind(s, along, side);
                return along >= 200.f && along <= 500.f && side < 250.f ? StepVerdict::done() : StepVerdict::pending();
            },
            4.f, "Accélère fort tout de suite : la balle file, il faut combler l'écart."));
        v.push_back(makeStep(
            "match", "Cale ta vitesse sur la sienne",
            [behind](const StepInput& in) {
                const Snapshot& s = in.snap;
                if (!s.car.valid || !s.ball.valid) return StepVerdict::pending();
                float along = 0.f, side = 0.f;
                behind(s, along, side);
                const bool close = along >= 200.f && along <= 500.f && side < 250.f;
                return close && std::fabs(length2D(s.car.vel) - length2D(s.ball.vel)) < 150.f ? StepVerdict::done()
                                                                                                : StepVerdict::pending();
            },
            4.f, "Juste derrière elle, lâche l'accélérateur jusqu'à rouler à sa vitesse.", 0.5f));
        v.back().holdGrace = 0.1f;
        v.push_back(touchStep("Touche douce, même vitesse"));
        break;
    }
    case DrillType::Pass: {
        v.push_back(makeStep(
            "read", "Regarde le point de rencontre",
            [p](const StepInput& in) {
                if (!in.snap.car.valid) return StepVerdict::pending();
                return headingErrorTo(in.snap.car, p->setup.target) < 30.f ? StepVerdict::done() : StepVerdict::pending();
            },
            1.f, "Dès le départ, tourne ton nez vers le point de rencontre dessiné au sol."));
        v.push_back(makeStep(
            "meet", "Va au point de rencontre",
            [p](const StepInput& in) {
                if (!in.snap.car.valid) return StepVerdict::pending();
                if (horizontalDistance(hitboxCenter(in.snap.car), p->setup.target) < 400.f) return StepVerdict::done();
                if (p->elapsed() > p->setup.targetTime - 0.4f) return StepVerdict::fail();
                return StepVerdict::pending();
            },
            0.f, "Va au point de rencontre tout de suite : tu dois y être avant la balle."));
        v.push_back(touchStep("Roule avec elle au contact"));
        break;
    }
    case DrillType::Lob: {
        v.push_back(makeStep(
            "read", "Repère le point de chute",
            [p](const StepInput& in) {
                if (!in.snap.car.valid) return StepVerdict::pending();
                return headingErrorTo(in.snap.car, p->setup.landing) < 25.f ? StepVerdict::done() : StepVerdict::pending();
            },
            1.5f, "Tourne ton nez vers le cercle dès que la balle part."));
        v.push_back(makeStep(
            "place", "Place-toi là où elle redescend",
            [p](const StepInput& in) {
                const Snapshot& s = in.snap;
                if (!s.car.valid) return StepVerdict::pending();
                if (p->floorBounces >= 1) return StepVerdict::fail();
                const bool there = horizontalDistance(hitboxCenter(s.car), p->setup.target) < 300.f;
                return there && length2D(s.car.vel) < 400.f ? StepVerdict::done() : StepVerdict::pending();
            },
            0.f, "Va au cercle et arrête-toi avant que la balle touche le sol.", 0.1f));
        v.push_back(makeStep(
            "bounce", "Laisse-la rebondir",
            [p](const StepInput&) {
                if (touchedBefore(*p)) return StepVerdict::fail();
                return p->floorBounces >= 1 ? StepVerdict::done() : StepVerdict::pending();
            },
            0.f, "Laisse-la rebondir une fois avant de la toucher : le rebond lui enlève de la vitesse."));
        Step touch = touchStep("Touche-la quand elle redescend");
        touch.check = [p](const StepInput&) {
            return p->floorBounces >= 2 ? StepVerdict::fail("ft.full.second_bounce",
                                                            "Touche-la avant son deuxième rebond, quand elle redescend.")
                                        : StepVerdict::pending();
        };
        v.push_back(touch);
        break;
    }
    case DrillType::Wall: {
        v.push_back(makeStep(
            "look", "Regarde le cercle",
            [p](const StepInput& in) {
                if (!in.snap.car.valid) return StepVerdict::pending();
                return headingErrorTo(in.snap.car, p->setup.target) < 30.f ? StepVerdict::done() : StepVerdict::pending();
            },
            n <= 3 ? 1.2f : 0.8f, "Tourne-toi vers le cercle dès que la balle quitte le mur."));
        v.push_back(makeStep(
            "go", "Va au cercle sans foncer",
            [p](const StepInput& in) {
                if (!in.snap.car.valid) return StepVerdict::pending();
                if (horizontalDistance(hitboxCenter(in.snap.car), p->setup.target) < 500.f) return StepVerdict::done();
                if (p->elapsed() > p->setup.targetTime - 0.3f) return StepVerdict::fail();
                return StepVerdict::pending();
            },
            0.f, "Pars tout de suite vers le cercle : tu dois y être avant la balle."));
        v.push_back(makeStep(
            "brake", "Freine",
            [](const StepInput& in) {
                if (!in.snap.car.valid) return StepVerdict::pending();
                return length2D(in.snap.car.vel) < 500.f ? StepVerdict::done() : StepVerdict::pending();
            },
            0.f, "Freine en arrivant au cercle : arrivé vite, tu frappes la balle au lieu de l'amortir.", 0.2f));
        v.push_back(touchStep("Touche douce après le rebond"));
        break;
    }
    case DrillType::High: {
        v.push_back(makeStep(
            "read", "Repère le point de chute",
            [p](const StepInput& in) {
                if (!in.snap.car.valid) return StepVerdict::pending();
                return headingErrorTo(in.snap.car, p->setup.landing) < 25.f ? StepVerdict::done() : StepVerdict::pending();
            },
            1.f, "Tourne ton nez vers le cercle dès que la balle part."));
        v.push_back(makeStep(
            "place", "Place-toi sous le rebond",
            [p](const StepInput& in) {
                const Snapshot& s = in.snap;
                if (!s.car.valid) return StepVerdict::pending();
                if (p->floorBounces >= 1) return StepVerdict::fail();
                const bool there = horizontalDistance(hitboxCenter(s.car), p->setup.target) < 200.f;
                return there && length2D(s.car.vel) < 300.f ? StepVerdict::done() : StepVerdict::pending();
            },
            0.f, "Arrête-toi sur le cercle de reprise avant que la balle touche le sol.", 0.1f));
        v.push_back(makeStep(
            "bounce", "Laisse-la rebondir",
            [p](const StepInput&) {
                if (touchedBefore(*p)) return StepVerdict::fail();
                return p->floorBounces >= 1 ? StepVerdict::done() : StepVerdict::pending();
            },
            0.f, "Laisse-la rebondir avant de sauter."));
        Step touch = touchStep("Saute pour la prendre en haut");
        touch.check = [p](const StepInput&) {
            return p->floorBounces >= 2 ? StepVerdict::fail("ft.full.second_bounce",
                                                            "Saute au rebond : prends-la en l'air avant qu'elle retombe.")
                                        : StepVerdict::pending();
        };
        v.push_back(touch);
        v.push_back(makeStep(
            "land", "Retombe avec la balle",
            [p](const StepInput&) {
                if (!p->landedAfterTouch) return StepVerdict::pending();
                return p->ballDistAtLanding < 400.f ? StepVerdict::done() : StepVerdict::fail();
            },
            2.5f, "Touche-la doucement en haut du saut : elle doit retomber près de toi."));
        return v;  // no "Garde-la": landing with the ball is the last step
    }
    case DrillType::Mix: return v;
    }
    if (n >= kControlLevel) v.push_back(keepStep());
    return v;
}

std::vector<std::string> fullLabels(DrillType t, int niveau) {
    FullState st;
    st.type = t;
    st.niveau = std::clamp(niveau, 1, 10);
    std::vector<std::string> out;
    if (t == DrillType::Mix) {
        out.push_back("1/" + std::to_string(kMatchBalls) + " Balle par balle, comme dans son exercice");
        return out;
    }
    const auto steps = fullSteps(st);
    for (size_t i = 0; i < steps.size(); ++i)
        out.push_back(std::to_string(i + 1) + "/" + std::to_string(steps.size()) + " " + steps[i].label);
    return out;
}

StepVerdict fullTouchVerdict(const FullState& st, const TouchMetrics& m, const Snapshot& pre) {
    const float rel = m.relSpeed;
    switch (st.type) {
    case DrillType::RollIn: {
        const float maxRel = st.niveau >= kControlLevel ? 300.f : 450.f;
        if (rel >= maxRel)
            return StepVerdict::fail("ft.full.touch_hard",
                                     "Arrive presque arrêté et recule un peu au contact : la balle doit s'arrêter "
                                     "contre ton nez.");
        if (m.contactErrDeg >= 45.f)
            return StepVerdict::fail("ft.full.touch_contact", "Touche la balle avec le bas de ton nez, bien au centre.");
        return StepVerdict::done();
    }
    case DrillType::RollAway: {
        if (rel >= 300.f)
            return StepVerdict::fail("ft.full.touch_hard", "Roule à sa vitesse avant de la toucher, sans accélérer.");
        if (m.contactErrDeg >= 35.f)
            return StepVerdict::fail("ft.full.touch_contact", "Touche le bas de la balle avec le nez, bien au centre.");
        return StepVerdict::done();
    }
    case DrillType::Pass: {
        const Vec3 carDir = length2D(pre.car.vel) > 100.f ? flat(pre.car.vel) : flat(pre.car.basis.fwd);
        const float angle = length2D(pre.ball.vel) > 50.f ? angleBetweenDeg(carDir, flat(pre.ball.vel)) : 0.f;
        if (angle >= 35.f)
            return StepVerdict::fail("ft.full.touch_angle",
                                     "Tourne pour rouler dans le sens de la balle avant de la toucher.");
        if (rel >= 400.f)
            return StepVerdict::fail("ft.full.touch_hard", "Roule à sa vitesse au contact : tu arrives trop vite.");
        return StepVerdict::done();
    }
    case DrillType::Lob: {
        if (st.bouncesAtTouch < 1)
            return StepVerdict::fail("ft.full.no_bounce", "Laisse-la rebondir une fois avant de la toucher.");
        if (pre.ball.vel.z >= 0.f)
            return StepVerdict::fail("ft.full.touch_rising", "Attends qu'elle redescende après le rebond.");
        if (rel >= 450.f)
            return StepVerdict::fail("ft.full.touch_hard",
                                     "Arrive presque arrêté : la balle doit tomber doucement sur ton nez.");
        return StepVerdict::done();
    }
    case DrillType::Wall: {
        if (st.bouncesAtTouch < std::max(1, st.setup.bouncesBeforeTarget))
            return StepVerdict::fail("ft.full.no_bounce",
                                     "Laisse-la rebondir devant toi et touche-la sur le cercle, quand elle redescend "
                                     "doucement.");
        if (rel >= 450.f)
            return StepVerdict::fail("ft.full.touch_hard", "Freine avant le contact : touche-la presque arrêté.");
        return StepVerdict::done();
    }
    case DrillType::High: {
        if (!st.airborneAtTouch)
            return StepVerdict::fail("ft.full.no_jump", "Saute pour la prendre en l'air, en haut de ton saut.");
        if (std::fabs(pre.ball.vel.z) >= 250.f)
            return StepVerdict::fail("ft.full.touch_timing",
                                     "Saute au moment du rebond : touche-la quand elle est en haut, presque arrêtée.");
        if (rel >= 400.f)
            return StepVerdict::fail("ft.full.touch_hard", "Touche-la en haut de ton saut, quand vous êtes presque "
                                                           "immobiles tous les deux.");
        return StepVerdict::done();
    }
    case DrillType::Mix: break;
    }
    return StepVerdict::done();
}

std::string fullEarlyTouchTip(DrillType t, const std::string& stepId) {
    if (stepId == "slow") return "Ralentis avant que la balle arrive : tu l'as touchée trop vite.";
    if (stepId == "catch" || stepId == "match") return "Cale ta vitesse sur la sienne avant de la toucher.";
    if (stepId == "meet") return "Va au point de rencontre avant la balle, puis touche-la.";
    if (stepId == "place" || stepId == "bounce")
        return t == DrillType::High ? "Laisse-la rebondir, puis saute pour la prendre en haut."
                                    : "Laisse-la rebondir une fois avant de la toucher.";
    if (stepId == "go" || stepId == "brake") return "Freine sur le cercle avant de toucher la balle.";
    return "Prends le temps de te placer avant de toucher la balle.";
}

// ---- Séquence de match --------------------------------------------------------------------------------------
DrillPose matchPose(const CarState& car) {
    DrillPose base = poseFromCar(car);
    Vec3 dir = length2D(car.vel) > 300.f ? flatDir(car.vel, base.fwd) : base.fwd;
    // Keep the car's direction when it has room; otherwise the closest direction (16 tried) that has.
    const float a0 = std::atan2(dir.y, dir.x);
    float best = 1e9f;
    Vec3 chosen = dir;
    for (int k = 0; k <= 8; ++k) {
        for (int sgn = -1; sgn <= 1; sgn += 2) {
            const float a = a0 + static_cast<float>(sgn * k) * kPi / 8.f;
            DrillPose p = poseFromYaw(base.pos, a * kRadToDeg);
            if (freeAhead(p) >= kMatchRoom && static_cast<float>(k) < best) {
                best = static_cast<float>(k);
                chosen = p.fwd;
            }
            if (k == 0) break;
        }
        if (best < 1e8f) break;
    }
    if (best > 1e8f) chosen = flatDir(Vec3{0.f, 0.f, 0.f} - base.pos, base.fwd);  // toward the centre
    return poseFromYaw(base.pos, std::atan2(chosen.y, chosen.x) * kRadToDeg);
}

} // namespace rlt::ft
