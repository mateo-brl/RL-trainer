#include "core/modules/mechanics/Demos.h"

#include <algorithm>
#include <cmath>

#include "core/modules/mechanics/MechCommon.h"

namespace rlt::mech {

namespace {

constexpr float kBallZ = 92.75f;
// Up vector height above which the car counts as "nearly upright" for the landing correction.
constexpr float kUprightZ = 0.5f;
// The flip's pitch torque lasts 0.65 s (78 ticks): the cancel stick must be held all along, never corrected.
constexpr int kFlipTorqueTicks = 80;

int toUnr(float deg) { return static_cast<int>(std::lround(deg * 65536.f / 360.f)); }

Vec3 flipXY(const Vec3& v) { return {-v.x, -v.y, v.z}; }

Inputs drive(float throttle = 1.f, bool boost = false) {
    Inputs in;
    in.throttle = throttle;
    in.boost = boost;
    in.holdingBoost = boost;
    return in;
}

void fillDodge(Inputs& in) {
    in.dodgeForward = clampf(-in.pitch, -1.f, 1.f);
    in.dodgeStrafe = clampf(in.yaw + in.roll, -1.f, 1.f);
}

// The returned key stays valid until the next one is added (makeDemo reserves room for every key anyway).
struct Builder {
    DemoScript& d;
    DemoKey& key(int at, const Inputs& in, int step) {
        DemoKey k;
        k.at = at;
        k.in = in;
        k.step = step;
        d.keys.push_back(k);
        return d.keys.back();
    }
};

void place(DemoScript& d, Vec3 pos, float yawDeg, Vec3 vel, float boost) {
    d.car.pos = pos;
    d.car.rot = {0, toUnr(yawDeg), 0};
    d.car.vel = vel;
    d.car.angVel = {};
    d.car.boost = boost;
}

void mirrorForOrange(DemoScript& d) {
    d.car.pos = flipXY(d.car.pos);
    d.car.vel = flipXY(d.car.vel);
    d.car.rot.yaw += toUnr(180.f);
    if (d.car.rot.yaw > 32768) d.car.rot.yaw -= 65536;
    d.ball.pos = flipXY(d.ball.pos);
}

// ---- Timelines (ticks after GO, 120 per second of game time) -------------------------------------------------
// Speedflip from the right diagonal kickoff spawn, without the ball: SpeedFlipTrainer's bot timeline (boost from GO,
// first jump at 59 ticks held 11, flip one tick after the release, stick pulled 5 ticks after the flip), flip to the
// left at the angle the module grades, air roll left during the cancel, then wheels down on the first jump's heading.
void speedflip(DemoScript& d, float angleDeg) {
    Builder b{d};
    place(d, {-2048.f, -2560.f, 17.f}, 45.f, {}, 33.33f);
    d.kickoffBoost = true;
    d.variant = "départ de kickoff en diagonale, sans balle";
    d.steps = {"Boost et accélérateur à fond", "Petit saut", "Flip en diagonale : stick en haut à gauche",
               "Annule : stick vers toi, air roll gauche", "Roues au sol, garde le boost"};
    const float a = -std::fabs(angleDeg) * kDegToRad;  // left flip
    Inputs go = drive(1.f, true);
    go.steer = 0.03f;
    b.key(0, go, 0);
    Inputs jump = drive(1.f, true);
    jump.jump = true;
    jump.steer = jump.yaw = 0.25f;  // a touch to the right before a left flip keeps the path straight
    b.key(59, jump, 1);
    b.key(70, drive(1.f, true), 1);
    Inputs flip = drive(1.f, true);
    flip.jump = true;
    flip.yaw = std::sin(a);
    flip.pitch = -std::cos(a);
    b.key(71, flip, 2);
    d.flipAt = 71;
    Inputs cancel = drive(1.f, true);
    cancel.pitch = 1.f;
    cancel.roll = -1.f;
    b.key(76, cancel, 3);
    DemoKey& land = b.key(140, cancel, 4);
    land.assist = DemoAssist::Level;
    land.afterInverted = true;  // the barrel roll goes on until the car comes back from upside down
    land.levelYaw = true;
    d.length = 260;
}

// Front flip cancelled two ticks after the flip (the module's cancel window is 6 ticks at the top level), stick held
// back through the flip's torque time.
void flipCancel(DemoScript& d) {
    Builder b{d};
    place(d, {0.f, -3000.f, 17.f}, 90.f, {0.f, 1000.f, 0.f}, 33.f);
    d.variant = "ligne droite";
    d.steps = {"Roule tout droit", "Petit saut", "Flip avant : stick vers l'avant", "Annule : stick vers toi, à fond",
               "Roues au sol"};
    b.key(0, drive(), 0);
    Inputs jump = drive();
    jump.jump = true;
    b.key(30, jump, 1);
    b.key(34, drive(), 1);
    Inputs flip = drive();
    flip.jump = true;
    flip.pitch = -1.f;
    b.key(40, flip, 2);
    d.flipAt = 40;
    Inputs cancel = drive();
    cancel.jump = true;  // the flip press lasts 3 ticks
    cancel.pitch = 1.f;
    b.key(42, cancel, 3);
    cancel.jump = false;
    b.key(43, cancel, 3);
    DemoKey& land = b.key(125, cancel, 4);
    land.assist = DemoAssist::Level;
    d.length = 200;
}

// Half-flip while reversing: jump, back flip 10 ticks later, stick pushed 20 ticks after the flip (the module's ideal
// window is 14 to 30), air roll once the car is upside down, then back on the wheels the other way round.
void halfFlip(DemoScript& d) {
    Builder b{d};
    place(d, {0.f, -1500.f, 17.f}, -90.f, {0.f, 800.f, 0.f}, 33.f);
    d.variant = "marche arrière";
    d.steps = {"Recule", "Petit saut", "Flip arrière : stick vers toi", "Nez en l'air : stick vers l'avant, à fond",
               "Air roll pour revenir sur les roues", "Repars en avant"};
    b.key(0, drive(-1.f), 0);
    Inputs jump = drive(-1.f);
    jump.jump = true;
    b.key(30, jump, 1);
    b.key(34, drive(-1.f), 1);
    Inputs flip = drive(-1.f);
    flip.jump = true;
    flip.pitch = 1.f;
    b.key(40, flip, 2);
    d.flipAt = 40;
    Inputs up = drive(-1.f);
    up.pitch = 1.f;
    b.key(43, up, 2);
    Inputs cancel = drive(-1.f);
    cancel.pitch = -1.f;
    b.key(60, cancel, 3);
    DemoKey& roll = b.key(90, cancel, 4);
    roll.assist = DemoAssist::Level;
    roll.afterInverted = true;  // never roll before the car is upside down (the roll would turn into a yaw)
    roll.rollDir = 1.f;
    roll.levelYaw = true;
    roll.headingDeg = 180.f;
    DemoKey& away = b.key(205, drive(1.f), 5);
    away.assist = DemoAssist::Level;
    away.rollDir = 1.f;
    away.levelYaw = true;
    away.headingDeg = 180.f;
    d.length = 260;
}

// Wavedash at the end of a drop from 3 m (like the drill): nose raised a little, flip forward 5 ticks before the rear
// wheels touch (the module counts the contact within 10 ticks of the flip at its top level).
void wavedash(DemoScript& d) {
    Builder b{d};
    place(d, {0.f, -3000.f, 300.f}, 90.f, {0.f, 1000.f, 0.f}, 33.f);
    d.variant = "chute de 3 m";
    d.steps = {"Lève un peu le nez", "Attends le sol, nez levé", "Juste avant le sol : flip avant",
               "Garde l'accélérateur"};
    b.key(0, drive(), 0);
    Inputs nose = drive();
    nose.pitch = 1.f;
    b.key(5, nose, 0);
    b.key(14, drive(), 1);
    Inputs flip = drive();
    flip.jump = true;
    flip.pitch = -1.f;
    b.key(106, flip, 2);
    d.flipAt = 106;
    b.key(109, drive(), 3);
    d.length = 175;
}

// Fast aerial from a still car: first jump held 24 ticks (0.2 s, FastAerialTrainer's green window) with boost and the
// stick pulled, released 3 ticks, second press with the stick centred, then boost upward and land on the wheels.
void fastAerial(DemoScript& d) {
    Builder b{d};
    place(d, {0.f, -2500.f, 17.f}, 90.f, {}, 100.f);
    d.variant = "voiture arrêtée";
    d.steps = {"Saute en gardant le saut, boost, stick vers toi", "Lâche le saut", "Rappuie tout de suite, stick au centre",
               "Boost vers le haut", "Lâche le boost, retombe sur les roues"};
    b.key(0, Inputs{}, 0);
    Inputs first = drive(1.f, true);
    first.jump = true;
    first.pitch = 1.f;
    b.key(10, first, 0);
    Inputs release = drive(1.f, true);
    release.pitch = 1.f;
    b.key(34, release, 1);
    Inputs second = drive(1.f, true);
    second.jump = true;
    b.key(37, second, 2);
    DemoKey& climb = b.key(39, drive(1.f, true), 3);
    climb.assist = DemoAssist::Level;
    climb.noseDeg = 55.f;
    DemoKey& land = b.key(75, drive(), 4);
    land.assist = DemoAssist::Level;
    d.length = 220;  // the car is flat by then: it lands on its wheels by itself
}

// Powerslide cut to the left at 1400 uu/s: steer, slide 12 ticks (0.1 s), release and straighten.
void powerslide(DemoScript& d) {
    Builder b{d};
    place(d, {0.f, -2500.f, 17.f}, 90.f, {0.f, 1400.f, 0.f}, 33.f);
    d.variant = "virage à gauche";
    d.steps = {"Roule vite, tout droit", "Tourne à gauche", "Powerslide court : l'arrière glisse",
               "Relâche, remets les roues droites"};
    b.key(0, drive(), 0);
    Inputs turn = drive();
    turn.steer = -1.f;
    b.key(30, turn, 1);
    Inputs slide = turn;
    slide.handbrake = true;
    b.key(34, slide, 2);
    b.key(46, turn, 3);
    b.key(56, drive(), 3);
    d.length = 150;
}

// Stall from the drill's top level start (released 5 m up, rising at 200 uu/s): air roll left and stick right 12
// ticks before the press (opposite sides: the flip has no direction, so the car floats instead of flipping), jump
// pressed 1.25 s after GO with both held, then everything released and wheels down (tests/mechanics stallBot).
void stall(DemoScript& d) {
    Builder b{d};
    place(d, {0.f, -1000.f, 500.f}, 90.f, {0.f, 500.f, 200.f}, 33.f);
    d.variant = "en l'air";
    d.steps = {"Attends en l'air", "Stick à droite, air roll à gauche", "Saute en gardant les deux : elle flotte",
               "Lâche tout, retombe sur les roues"};
    b.key(0, Inputs{}, 0);
    Inputs side;
    side.yaw = 1.f;
    side.roll = -1.f;
    b.key(138, side, 1);
    Inputs press = side;
    press.jump = true;
    b.key(150, press, 2);
    d.flipAt = 150;
    b.key(153, side, 2);
    DemoKey& land = b.key(165, Inputs{}, 3);
    land.assist = DemoAssist::Level;
    d.length = 300;
}

} // namespace

bool hasDemo(Focus f) {
    switch (f) {
    case Focus::SpeedFlip:
    case Focus::FlipCancel:
    case Focus::HalfFlip:
    case Focus::Wavedash:
    case Focus::FastAerial:
    case Focus::Powerslide:
    case Focus::Stall: return true;
    default: return false;
    }
}

DemoScript makeDemo(Focus f, int team, float speedflipAngleDeg) {
    DemoScript d;
    d.focus = f;
    d.ball.pos = {3300.f, 4300.f, kBallZ};  // far corner of the opponent half, away from every demo
    d.keys.reserve(16);
    switch (f) {
    case Focus::SpeedFlip: speedflip(d, clampf(speedflipAngleDeg, 15.f, 60.f)); break;
    case Focus::FlipCancel: flipCancel(d); break;
    case Focus::HalfFlip: halfFlip(d); break;
    case Focus::Wavedash: wavedash(d); break;
    case Focus::FastAerial: fastAerial(d); break;
    case Focus::Powerslide: powerslide(d); break;
    case Focus::Stall: stall(d); break;
    default: return d;
    }
    for (DemoKey& k : d.keys) fillDodge(k.in);
    if (team == 1) mirrorForOrange(d);
    return d;
}

int demoKeyIndex(const DemoScript& d, int t) {
    int i = 0;
    for (size_t k = 0; k < d.keys.size(); ++k)
        if (d.keys[k].at <= t) i = static_cast<int>(k);
    return i;
}

Inputs demoInputs(const DemoScript& d, int t) {
    if (d.keys.empty() || t < 0 || t >= d.length) return Inputs{};
    Inputs in = d.keys[static_cast<size_t>(demoKeyIndex(d, t))].in;
    fillDodge(in);
    return in;
}

int demoStep(const DemoScript& d, int t) {
    if (d.keys.empty()) return 0;
    return d.keys[static_cast<size_t>(demoKeyIndex(d, std::max(0, t)))].step;
}

void DemoPilot::start(int64_t goFrame) {
    *this = DemoPilot{};
    goFrame_ = goFrame;
}

Inputs DemoPilot::update(const DemoScript& d, const Snapshot& s) {
    const int t = tick(s.frame);
    Inputs in = demoInputs(d, t);
    const CarState& c = s.car;
    const Basis& b = c.basis;
    // Attitude from the basis only: roll error > 0 when the right side is up (roll right), nose angle, heading.
    const float roll = std::atan2(b.right.z, b.up.z);
    const float nose = std::asin(clampf(b.fwd.z, -1.f, 1.f));
    const float heading = std::atan2(b.fwd.y, b.fwd.x);
    float dRoll = 0.f, dNose = 0.f, dHeading = 0.f;
    if (hasPrev_ && s.frame > prevFrame_) {
        const float dt = static_cast<float>(s.frame - prevFrame_) / k::kTickRateF;
        dRoll = wrapPi(roll - prevRoll_) / dt;
        dNose = (nose - prevNose_) / dt;
        dHeading = wrapPi(heading - prevHeading_) / dt;
    }
    hasPrev_ = true;
    prevFrame_ = s.frame;
    prevRoll_ = roll;
    prevNose_ = nose;
    prevHeading_ = heading;
    if (!c.valid || t < 0 || t >= d.length || d.keys.empty()) return in;
    if (b.up.z < -0.3f) inverted_ = true;
    if (!hasJumpHeading_ && in.jump) {
        hasJumpHeading_ = true;
        jumpHeading_ = heading;
    }
    const int idx = demoKeyIndex(d, t);
    const DemoKey& key = d.keys[static_cast<size_t>(idx)];
    // A landing key that still waits (not upside down yet, or still rolling with the previous inputs) shows the step
    // before it.
    const bool waiting = key.assist == DemoAssist::Level && key.afterInverted &&
                         (!inverted_ || (key.rollDir == 0.f && b.up.z <= kUprightZ && !grounded(c)));
    step_ = waiting && idx > 0 ? d.keys[static_cast<size_t>(idx - 1)].step : key.step;
    if (key.assist != DemoAssist::Level || (key.afterInverted && !inverted_)) return in;
    if (grounded(c)) {
        in.roll = in.pitch = in.yaw = 0.f;  // on the wheels: nothing to correct
        fillDodge(in);
        return in;
    }
    if (b.up.z <= kUprightZ) {
        if (key.rollDir != 0.f) in.roll = key.rollDir;  // keep rolling the same way until nearly upright
    } else {
        in.roll = clampf(4.f * roll + 0.6f * dRoll, -1.f, 1.f);
    }
    const bool flipTorque = d.flipAt >= 0 && t < d.flipAt + kFlipTorqueTicks;
    if (key.levelPitch && !flipTorque && b.up.z > 0.6f)
        in.pitch = clampf(8.f * (key.noseDeg * kDegToRad - nose) - 1.6f * dNose, -1.f, 1.f);
    if (key.levelYaw && hasJumpHeading_ && b.up.z > 0.6f) {
        const float target = jumpHeading_ + key.headingDeg * kDegToRad;
        in.yaw = clampf(1.5f * wrapPi(target - heading) - 0.5f * dHeading, -1.f, 1.f);
    }
    fillDodge(in);
    return in;
}

} // namespace rlt::mech
