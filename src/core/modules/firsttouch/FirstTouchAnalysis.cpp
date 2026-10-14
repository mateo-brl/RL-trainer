#include "core/modules/firsttouch/FirstTouchAnalysis.h"

#include <algorithm>
#include <cmath>

#include "core/common/Controls.h"
#include "core/common/Text.h"

namespace rlt::ft {

// ---- Labels ---------------------------------------------------------------------------------------------
const char* touchTypeId(TouchType t) {
    switch (t) {
    case TouchType::Ground: return "ground";
    case TouchType::Bounce: return "bounce";
    case TouchType::Air: return "air";
    case TouchType::Wall: return "wall";
    case TouchType::Dribble: return "dribble";
    }
    return "ground";
}

const char* touchTypeLabel(TouchType t) {
    switch (t) {
    case TouchType::Ground: return "sol";
    case TouchType::Bounce: return "rebond";
    case TouchType::Air: return "aérien";
    case TouchType::Wall: return "mur";
    case TouchType::Dribble: return "dribble";
    }
    return "sol";
}

const char* touchTypeTitle(TouchType t) {
    switch (t) {
    case TouchType::Ground: return "Sol";
    case TouchType::Bounce: return "Rebond";
    case TouchType::Air: return "Aérien";
    case TouchType::Wall: return "Mur";
    case TouchType::Dribble: return "Dribble";
    }
    return "Sol";
}

const char* carPartLabel(CarPart p) {
    switch (p) {
    case CarPart::Nose: return "nez";
    case CarPart::Roof: return "toit";
    case CarPart::Side: return "côté";
    case CarPart::Back: return "arrière";
    case CarPart::Under: return "dessous";
    }
    return "nez";
}

const char* contactHeightLabel(ContactHeight h) {
    switch (h) {
    case ContactHeight::Low: return "bas";
    case ContactHeight::Middle: return "centre";
    case ContactHeight::High: return "haut";
    }
    return "centre";
}

SpeedZone speedZone(float speedDiff, float goodZone, float okZone) {
    const float a = std::fabs(speedDiff);
    if (a <= goodZone) return SpeedZone::Good;
    if (a <= std::max(goodZone, okZone)) return SpeedZone::Ok;
    return SpeedZone::Bad;
}

const char* zoneTag(SpeedZone z) {
    switch (z) {
    case SpeedZone::Good: return "[BON]";
    case SpeedZone::Ok: return "[MOYEN]";
    case SpeedZone::Bad: return "[MAUVAIS]";
    }
    return "[MOYEN]";
}

// ---- Hitbox ---------------------------------------------------------------------------------------------
Obb carHitbox(const CarState& car) {
    Obb b;
    b.basis = car.basis;
    Vec3 ext = car.hitboxExtent;
    if (ext.x < 1.f || ext.y < 1.f || ext.z < 1.f) ext = Vec3{118.01f, 84.20f, 36.16f};  // Octane fallback
    b.half = ext * 0.5f;
    b.center = car.pos + car.basis.toWorld(car.hitboxOffset);
    return b;
}

Vec3 closestPointOnObb(const Obb& box, const Vec3& p) {
    const Vec3 d = p - box.center;
    const float u = clampf(dot(d, box.basis.fwd), -box.half.x, box.half.x);
    const float v = clampf(dot(d, box.basis.right), -box.half.y, box.half.y);
    const float w = clampf(dot(d, box.basis.up), -box.half.z, box.half.z);
    return box.center + box.basis.fwd * u + box.basis.right * v + box.basis.up * w;
}

bool ballOnRoof(const CarState& car, const BallState& ball) {
    if (!car.valid || !ball.valid) return false;
    const Obb b = carHitbox(car);
    const Vec3 roof = b.center + b.basis.up * b.half.z;
    const Vec3 d = ball.pos - roof;
    const float up = dot(d, b.basis.up);
    const Vec3 planar = d - b.basis.up * up;
    const float gap = up - ball.radius;  // roof plane to ball bottom
    return length(planar) <= b.half.x + 10.f && gap >= -15.f && gap <= 60.f;
}

bool ballCarriedOnRoof(const CarState& car, const BallState& ball, float maxRelVz) {
    // Only a driving car carries the ball: an airborne car rising into a ball above it is a soft air touch.
    return !airborne(car) && ballOnRoof(car, ball) && std::fabs(ball.vel.z - car.vel.z) < maxRelVz;
}

bool carOnWall(const CarState& car) {
    if (!car.valid) return false;
    if (car.onWall) return true;
    return (car.onGround || car.wheelContacts >= 3) && std::fabs(car.basis.up.z) < 0.5f;
}

// ---- Metrics --------------------------------------------------------------------------------------------
namespace {
constexpr float kCos25 = 0.906307787f;
constexpr float kSin25 = 0.422618262f;

Vec3 horizontalOr(const Vec3& v, const Vec3& fallback, float minLen) {
    const Vec3 f = flat(v);
    return length(f) >= minLen ? normalize(f) : fallback;
}
} // namespace

Vec3 desiredContactDir(TouchType type, const Vec3& approach, const Vec3& up, const Vec3& ballTravel) {
    const Vec3 z{0.f, 0.f, 1.f};
    switch (type) {
    case TouchType::Ground:
    case TouchType::Wall: return normalize(approach * -kCos25 - up * kSin25, -up);
    case TouchType::Bounce: return normalize(approach * -0.5f - up * 0.8660254f, -up);
    case TouchType::Air: return normalize(-z - ballTravel * 0.3f, -z);
    case TouchType::Dribble: return -up;
    }
    return -z;
}

TouchMetrics computeMetrics(const TouchEvent& touch, TouchType type) {
    TouchMetrics m;
    const Snapshot& pre = touch.pre;
    if (!pre.car.valid || !pre.ball.valid) return m;
    m.valid = true;

    const CarState& car = pre.car;
    const BallState& ball = pre.ball;
    const Vec3 vc = car.vel, vb = ball.vel;
    const float r = ball.radius > 10.f ? ball.radius : 91.25f;

    m.speedDiff = length2D(vc) - length2D(vb);
    const Vec3 rel = vb - vc;
    m.relSpeed = length(rel);
    const Vec3 carFwdFlat = horizontalOr(car.basis.fwd, Vec3{1.f, 0.f, 0.f}, 1e-3f);
    const Vec3 carToBallFlat = horizontalOr(ball.pos - car.pos, carFwdFlat, 1e-3f);
    // Ball travel: its own horizontal direction only when it really moves. A nearly still ball (a few uu/s
    // of residual roll) uses the car -> ball direction, so charging into it reads as "car faster".
    const bool ballMoving = length2D(vb) > kMovingBallSpeed;
    const Vec3 travel = ballMoving ? normalize(flat(vb), carToBallFlat) : carToBallFlat;
    const Vec3 dv = flat(vc - vb);
    m.along = dot(dv, travel);
    m.cross = length(dv - travel * m.along);
    m.vertDiff = vc.z - vb.z;
    m.approachAngleDeg = angleBetweenDeg(flat(vc), flat(vb));
    // Coming / going away: the ball travel against the car <-> ball line (60 degree cone). Crossing balls are
    // neither: their mismatch shows in `cross` and their sign in `along` says nothing about "too slow".
    m.ballComing = ballMoving && dot(travel, -carToBallFlat) > 0.5f;
    m.ballAway = ballMoving && dot(travel, carToBallFlat) > 0.5f;

    // Contact point: hook hit info when plausible, else the hitbox point closest to the ball centre.
    const Obb box = carHitbox(car);
    Vec3 u;
    bool haveU = false;
    if (touch.hasHitInfo) {
        const Vec3 d = touch.hitLocation - ball.pos;
        const float dl = length(d);
        if (dl >= 0.5f * r && dl <= 1.6f * r) {
            u = d / dl;
            haveU = true;
        } else if (length(touch.hitNormal) > 0.5f) {
            u = -normalize(touch.hitNormal);
            haveU = true;
        }
        m.fromHitInfo = haveU;
    }
    if (!haveU) {
        const Vec3 cp = closestPointOnObb(box, ball.pos);
        Vec3 d = cp - ball.pos;
        if (length(d) < 1e-3f) d = box.center - ball.pos;  // ball centre inside the box: degenerate
        u = normalize(d, Vec3{0.f, 0.f, -1.f});
    }
    m.contactDir = u;
    m.contactPoint = ball.pos + u * r;
    m.normal = -u;
    m.contactHeight01 = clamp01((u.z + 1.f) * 0.5f);
    m.height = u.z < -0.33f ? ContactHeight::Low : (u.z > 0.33f ? ContactHeight::High : ContactHeight::Middle);

    // Which face of the car hit the ball: dominant axis of the contact normal in the car frame.
    const Vec3 nl = box.basis.toLocal(m.normal);
    const float ax = std::fabs(nl.x), ay = std::fabs(nl.y), az = std::fabs(nl.z);
    if (ax >= ay && ax >= az) m.part = nl.x >= 0.f ? CarPart::Nose : CarPart::Back;
    else if (ay >= az) m.part = CarPart::Side;
    else m.part = nl.z >= 0.f ? CarPart::Roof : CarPart::Under;

    m.lateralOffset = dot(ball.pos - box.center, box.basis.right);
    m.closingSpeed = dot(vc - vb, m.normal);

    // Ideal contact for this type (wall touches use the wall as "up").
    const Vec3 up = type == TouchType::Wall ? normalize(car.basis.up, Vec3{0.f, 0.f, 1.f}) : Vec3{0.f, 0.f, 1.f};
    m.surfaceUp = up;
    m.contactUp = dot(u, up);
    Vec3 approach;
    {
        const Vec3 cb = vc - vb;
        Vec3 planar = cb - up * dot(cb, up);
        if (length(planar) < 50.f) {
            const Vec3 toBall = ball.pos - car.pos;
            planar = toBall - up * dot(toBall, up);
        }
        approach = normalize(planar);
    }
    m.desiredDir = desiredContactDir(type, approach, up, horizontalOr(vb, Vec3{}, 50.f));
    m.contactErrDeg = angleBetweenDeg(u, m.desiredDir);

    const Snapshot& post = touch.post;
    if (post.car.valid && post.ball.valid) {
        m.ballPop = post.ball.vel.z - post.car.vel.z;
        m.separation = length(post.ball.vel - post.car.vel);
    }
    const Vec3 nose = box.center + box.basis.fwd * box.half.x;
    m.noseAimErrDeg = angleBetweenDeg(box.basis.fwd, ball.pos - nose);
    m.boostAtContact = (pre.in.boost || pre.in.holdingBoost || touch.post1.in.boost) && car.boost > 0.5f;

    // Heatmap frame: x along the horizontal approach of the car (relative to the ball), y to its right, z up.
    const Vec3 a = horizontalOr(vc - vb, carToBallFlat, 50.f);
    const Vec3 right{-a.y, a.x, 0.f};
    m.heatX = dot(u, a);
    m.heatY = dot(u, right);
    m.heatZ = u.z;
    return m;
}

// ---- Classification -------------------------------------------------------------------------------------
bool ballBouncedRecently(const TickHistory& history, int64_t frame, int ticks) {
    const Snapshot* newer = nullptr;
    for (size_t i = 0; i < history.size(); ++i) {
        const Snapshot& s = history.back(i);
        if (s.frame > frame) continue;
        if (s.frame < frame - ticks) break;
        if (newer && newer->ball.valid && s.ball.valid) {
            const float restZ = s.ball.radius + 1.9f;
            if (s.ball.vel.z < -60.f && newer->ball.vel.z > 30.f && s.ball.pos.z < restZ + 50.f) return true;
        }
        newer = &s;
    }
    return false;
}

TouchType classifyTouch(const Snapshot& pre, bool newContact, int ticksSincePrevTouch, bool recentBounce,
                        int dribbleGapTicks) {
    if (!newContact || ticksSincePrevTouch <= dribbleGapTicks) return TouchType::Dribble;
    if (ballCarriedOnRoof(pre.car, pre.ball)) return TouchType::Dribble;  // carry / ball put on the roof, not a catch
    if (carOnWall(pre.car)) return TouchType::Wall;
    const float restZ = pre.ball.radius + 1.9f;
    const float height = pre.ball.pos.z - restZ;
    const bool lowBall = height < kGroundBallHeight;
    const bool carAir = airborne(pre.car);
    if (!lowBall) return carAir ? TouchType::Air : TouchType::Bounce;
    const bool rolling = height < 40.f && std::fabs(pre.ball.vel.z) < 150.f && !recentBounce;
    return rolling ? TouchType::Ground : TouchType::Bounce;
}

// ---- Control window -------------------------------------------------------------------------------------
void ControlWindow::start(const TouchEvent& touch, int windowTicks, float radius, float maxRelSpeed,
                          float requiredFraction) {
    active_ = true;
    startFrame_ = touch.pre.frame;
    lastFrame_ = touch.pre.frame;
    windowTicks_ = std::max(1, windowTicks);
    elapsedTicks_ = 0;
    radius_ = radius;
    maxRel_ = maxRelSpeed;
    required_ = requiredFraction;
    startBallZ_ = touch.pre.ball.pos.z;
    maxBallZ_ = startBallZ_;
    ticks_ = 0;
    controlled_ = 0;
    trailingOut_ = 0;
    lastIn_ = true;
    maxSteer_ = 0.f;
    maxDist_ = 0.f;
    feed(touch.post1);
    feed(touch.post);
}

bool ControlWindow::feed(const Snapshot& s) {
    if (!active_) return false;
    if (s.frame <= lastFrame_ || elapsedTicks_ >= windowTicks_) return complete();
    lastFrame_ = s.frame;
    const int64_t elapsed = s.frame - startFrame_;
    elapsedTicks_ = static_cast<int>(std::min<int64_t>(elapsed, windowTicks_));
    if (s.paused || !s.car.valid || !s.ball.valid) return complete();
    const Vec3 center = hitboxCenter(s.car);
    const float dist = distance(s.ball.pos, center);
    const float rel = length(s.ball.vel - s.car.vel);
    const bool inside = dist <= radius_ && rel <= maxRel_;
    ++ticks_;
    if (inside) {
        ++controlled_;
        trailingOut_ = 0;
    } else {
        ++trailingOut_;
    }
    lastIn_ = inside;
    maxBallZ_ = std::max(maxBallZ_, s.ball.pos.z);
    maxDist_ = std::max(maxDist_, dist);
    if (elapsed <= 60) maxSteer_ = std::max(maxSteer_, std::fabs(s.in.steer));
    return complete();
}

ControlSummary ControlWindow::summary() const {
    ControlSummary c;
    c.ticks = ticks_;
    c.controlledTicks = controlled_;
    c.fraction = ticks_ > 0 ? static_cast<float>(controlled_) / static_cast<float>(ticks_) : 0.f;
    c.controlled = ticks_ > 0 && c.fraction >= required_;
    const int lostTicks = std::max(6, windowTicks_ / 4);
    c.lost = ticks_ > 0 && !lastIn_ && trailingOut_ >= lostTicks;
    c.popHeight = std::max(0.f, maxBallZ_ - startBallZ_);
    c.maxSteerAfter = maxSteer_;
    c.maxDistance = maxDist_;
    return c;
}

// ---- Score ----------------------------------------------------------------------------------------------
namespace {
struct Weights {
    float s, c, k;
};
Weights normalisedWeights(const ScoreParams& p) {
    float s = std::max(0.f, p.wSpeed), c = std::max(0.f, p.wContact), k = std::max(0.f, p.wControl);
    const float sum = s + c + k;
    if (sum <= 1e-6f) return {0.35f, 0.25f, 0.40f};
    return {s / sum, c / sum, k / sum};
}
} // namespace

TouchScore scoreTouch(const TouchMetrics& m, const ControlSummary& c, const ScoreParams& p) {
    TouchScore s;
    s.sSpeed = rampScore(m.relSpeed, p.speedGood, std::max(p.speedGood + 1.f, p.speedBad));
    s.sContact = rampScore(m.contactErrDeg, p.contactGoodDeg, std::max(p.contactGoodDeg + 1.f, p.contactBadDeg));
    s.sControl = clamp01(c.fraction);
    const Weights w = normalisedWeights(p);
    float q = 100.f * (w.s * s.sSpeed + w.c * s.sContact + w.k * s.sControl);
    if (c.lost) q = std::min(q, p.lostCap);
    s.score = std::round(clampf(q, 0.f, 100.f));
    return s;
}

float touchOnlyScore(const TouchScore& s, const ScoreParams& p) {
    const Weights w = normalisedWeights(p);
    const float sum = w.s + w.c;
    if (sum <= 1e-6f) return 0.f;
    return std::round(clampf(100.f * (w.s * s.sSpeed + w.c * s.sContact) / sum, 0.f, 100.f));
}

const char* scoreHeadline(float score) {
    if (score >= 90.f) return "Touche parfaite !";
    if (score >= 85.f) return "Belle touche !";
    if (score >= 70.f) return "Bonne touche";
    if (score >= 45.f) return "Touche correcte";
    return "Touche ratée";
}

// ---- Diagnosis ------------------------------------------------------------------------------------------
std::vector<Tip> diagnose(TouchType type, const TouchMetrics& m, const ControlSummary& c, const TouchScore& s,
                          const ScoreParams& sp, const DiagnosisParams& dp, const PlayerProfile& profile,
                          KeyStyle style) {
    std::vector<Tip> tips;
    if (!m.valid || type == TouchType::Dribble) return tips;
    const Weights w = normalisedWeights(sp);
    const float lossSpeed = 100.f * w.s * (1.f - s.sSpeed);
    const float lossContact = 100.f * w.c * (1.f - s.sContact);
    const float lossControl = 100.f * w.k * (1.f - s.sControl);
    // Share of each component of the relative velocity (along/cross horizontal, vertical) in relSpeed^2.
    const float rel2 = std::max(1.f, m.along * m.along + m.cross * m.cross + m.vertDiff * m.vertDiff);
    const float shareAlong = m.along * m.along / rel2;
    const float shareCross = m.cross * m.cross / rel2;
    const float shareVert = m.vertDiff * m.vertDiff / rel2;
    const bool air = type == TouchType::Air;
    const bool groundLike = !air;
    const std::string boostKey = actionPrompt(profile, GameAction::Boost, style);

    auto add = [&](const char* code, std::string text, float weight) {
        tips.push_back(Tip{code, std::move(text), std::max(0.5f, weight)});
    };

    // Speed along the ball travel. `along` < 0 alone does not mean "too slow": a ball coming at the car gives a
    // large negative value too (head-on), and a crossing ball gives -|v_ball| whatever the car speed.
    // Twice the threshold reads as "much too fast": the size of the mistake in words, without a number.
    const bool crossingFaster = !m.ballComing && !m.ballAway && m.speedDiff < -dp.tooFast;
    if (groundLike && m.along > dp.tooFast) {
        add("ft.too_fast",
            std::string(m.along > 2.f * dp.tooFast ? "Tu arrives beaucoup trop vite" : "Tu arrives trop vite") +
                " : relâche " + boostKey + " plus tôt.",
            lossSpeed * shareAlong + 1.f);
    } else if (groundLike && m.along < -dp.tooFast && m.ballComing) {
        add("ft.head_on", "La balle arrive de face : freine ou recule au moment du contact.",
            lossSpeed * shareAlong + 1.f);
    } else if (groundLike && m.along < -dp.tooFast && (m.ballAway || crossingFaster)) {
        add("ft.too_slow", "Accélère avant le contact : la balle va plus vite que toi.",
            lossSpeed * shareAlong + 1.f);
    } else if (air && m.relSpeed > sp.speedGood && m.closingSpeed > dp.tooFast) {
        add("ft.too_fast", "Arrive plus doucement sous la balle : dose " + boostKey + ".", lossSpeed + 1.f);
    }
    if (groundLike && m.cross > dp.angle)
        add("ft.angle", "Aligne ta trajectoire sur celle de la balle avant de la toucher.", lossSpeed * shareCross + 1.f);
    if (type == TouchType::Bounce && m.vertDiff > dp.tooFast)  // ball falling fast onto the car
        add("ft.drop_speed", "Prends la balle plus près du sommet de son rebond : elle tombe trop vite sur toi.",
            lossSpeed * shareVert + 1.f);

    // Contact geometry (height measured along the surface the car drives on).
    const float uz = m.contactUp;
    bool contactLowFired = false;
    if (groundLike && uz > dp.contactHighZ && m.part != CarPart::Roof)
        add("ft.contact_high", "Prends la balle plus bas : vise sous son centre.", lossContact + 1.f);
    if ((type == TouchType::Ground || type == TouchType::Bounce) && m.part == CarPart::Nose && uz < -0.6f &&
        m.ballPop > dp.pop) {
        add("ft.contact_low", "Touche la balle moins par-dessous : elle s'envole au lieu de rester devant toi.",
            lossContact + 0.3f * lossControl + 1.f);
        contactLowFired = true;
    }
    if (groundLike && (m.part == CarPart::Side || std::fabs(m.lateralOffset) > dp.lateral)) {
        const char* side = m.lateralOffset >= 0.f ? "droite" : "gauche";
        add("ft.side", std::string("Centre la balle sur ton capot : elle part sur ta ") + side + ".",
            3.f + std::min(10.f, std::fabs(m.lateralOffset) / 10.f));
    }
    if (groundLike && !contactLowFired && (m.ballPop > dp.pop || c.popHeight > dp.popHeight))
        add("ft.ball_popped", "Amortis la balle : arrive un peu plus tôt pour qu'elle ne rebondisse pas sur toi.",
            0.5f * lossControl + 3.f);
    if (groundLike && m.boostAtContact) add("ft.boost", "Relâche " + boostKey + " avant le contact.", 0.3f * lossSpeed + 4.f);
    if (air && m.contactErrDeg > sp.contactGoodDeg)
        add("ft.air_contact", "Touche plus sous la balle : vise son dessous avec le nez.", lossContact + 1.f);
    if (air && m.noseAimErrDeg > 25.f)
        add("ft.nose_aim", "Garde le nez de la voiture pointé sous la balle.", 0.5f * lossContact + 2.f);
    if (groundLike && c.lost && c.maxSteerAfter > 0.7f)
        add("ft.oversteer", "Braque moins après la touche : fais de petites corrections.", 0.6f * lossControl + 1.f);
    if (!c.controlled) {
        // A ball lost after a speed or contact mistake is its consequence: the cause comes first (the HUD shows
        // one tip only).
        bool causeFound = false;
        for (const auto& t : tips)
            if (t.code != "ft.side" && t.code != "ft.boost" && t.code != "ft.oversteer") causeFound = true;
        const float w = causeFound ? 0.5f * lossControl : lossControl;
        if (air) add("ft.lost_control", "Après la touche, reste sous la balle pour la reprendre.", w);
        else add("ft.lost_control", "Reste collé à la balle après la touche : suis-la sans la repousser.", w);
    }
    std::stable_sort(tips.begin(), tips.end(), [](const Tip& a, const Tip& b) { return a.weight > b.weight; });
    return tips;
}

// ---- Text -----------------------------------------------------------------------------------------------
const char* unitLabel(SpeedUnit unit) {
    switch (unit) {
    case SpeedUnit::Mph: return "mph";
    case SpeedUnit::Uus: return "uu/s";
    case SpeedUnit::Kph:
    case SpeedUnit::Auto:
    default: return "km/h";
    }
}

std::string formatSpeedValue(float uus, SpeedUnit unit, int decimals, bool withSign) {
    if (!std::isfinite(uus) || std::fabs(uus) <= 1.f) uus = 0.f;
    std::string n = formatNumber(convertSpeed(uus, unit), unit == SpeedUnit::Uus ? 0 : decimals);
    if (withSign && uus > 0.f) n = "+" + n;
    return n + " " + unitLabel(unit);
}

std::string chatTouchLine(float speedDiff, float score, SpeedZone zone, SpeedUnit unit, bool withScore) {
    std::string s = std::string(zoneTag(zone)) + " Écart " + formatSpeedValue(speedDiff, unit);
    if (withScore) s += ", note " + std::to_string(static_cast<int>(std::lround(clampf(score, 0.f, 100.f))));
    return s;
}

std::string chatFlipResetLine(float speedDiff, SpeedZone zone, SpeedUnit unit) {
    return "[FLIP RESET] Écart " + formatSpeedValue(speedDiff, unit) + " " + zoneTag(zone);
}

} // namespace rlt::ft
