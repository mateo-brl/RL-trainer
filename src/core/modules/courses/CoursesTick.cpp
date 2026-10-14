// CoursesModule: per-tick logic (setup, countdown, run, result, chaining) and grading of every attempt.

#include <algorithm>
#include <cmath>

#include "core/common/Text.h"
#include "core/modules/courses/CoursesModule.h"

namespace rlt {

namespace {

float kph(float uus) { return uus * kUusToKph; }

void say(Actions& out, const std::string& text) { out.chat.push_back(ChatMessage{text, "RL Trainer"}); }

// Catch first contact (uu/s of avoidable closing speed, see endCatch) and roof offset (uu).
constexpr float kCatchSoftGood = 200.f;
constexpr float kCatchSoftBad = 800.f;
constexpr float kCatchHardTip = 500.f;
constexpr float kCatchOffCentre = 60.f;

constexpr const char* kTipSlalomWeave =
    "Fais le slalom : décale la balle d'un côté puis de l'autre pour passer au centre de chaque porte.";
constexpr const char* kTipRingCentre = "Vise le centre de l'anneau suivant avant d'accélérer.";

std::string signedSeconds(float v) {
    const std::string n = formatNumber(std::fabs(v), 2);
    return (v < 0.f ? "-" : "+") + n;
}

const char* medalFor(double ratio) {
    if (ratio <= 1.0) return "médaille d'or";
    if (ratio <= 1.25) return "médaille d'argent";
    if (ratio <= 1.5) return "médaille de bronze";
    return "";
}

void sortTips(std::vector<Tip>& tips) {
    std::stable_sort(tips.begin(), tips.end(), [](const Tip& a, const Tip& b) { return a.weight > b.weight; });
}

Metric metric(const char* key, const char* label, float value, const char* unit) {
    Metric m;
    m.key = key;
    m.label = label;
    m.value = value;
    m.unit = unit;
    return m;
}

Metric metricIdeal(const char* key, const char* label, float value, const char* unit, float ideal) {
    Metric m = metric(key, label, value, unit);
    m.ideal = ideal;
    m.hasIdeal = true;
    return m;
}

// "Out of boost" tip, only where boost management is part of the exercise (never in the slalom).
void addBoostTip(CoursesKind k, bool emptied, std::vector<Tip>& tips) {
    if (!emptied) return;
    if (k == CoursesKind::RingsAir || k == CoursesKind::RingsAirDribble)
        tips.push_back({"courses.rings_boost_out", "Économise le boost : alterne boost et plané entre les anneaux.", 6.f});
    else if (k == CoursesKind::RingsGround)
        tips.push_back({"courses.rings_boost_out",
                        "Économise le boost : garde ta vitesse dans les virages au lieu de réaccélérer.", 6.f});
}

// Coaching for a ball lost off the roof, from how it left (roof dribble and slalom).
void addFallTip(courses::FallSide side, bool slalom, std::vector<Tip>& tips) {
    switch (side) {
    case courses::FallSide::Front:
        tips.push_back({slalom ? "courses.slalom_front" : "courses.dribble_front",
                        "Accélère un peu : la balle glisse vers l'avant du capot.", 8.f});
        break;
    case courses::FallSide::Back:
        tips.push_back({slalom ? "courses.slalom_back" : "courses.dribble_back",
                        "Accélère plus doucement : la balle glisse vers l'arrière.", 8.f});
        break;
    case courses::FallSide::Side:
        tips.push_back({slalom ? "courses.slalom_side" : "courses.dribble_side",
                        slalom ? "Tourne plus progressivement entre les portes : la balle part sur le côté."
                               : "Tourne plus progressivement : la balle part sur le côté.",
                        8.f});
        break;
    case courses::FallSide::Up:
        tips.push_back({slalom ? "courses.slalom_pop" : "courses.dribble_pop",
                        "Touches plus douces : la balle rebondit trop haut sur le toit.", 7.f});
        break;
    default: break;
    }
}

} // namespace

// ---- Tick dispatch -----------------------------------------------------------------------------------------
void CoursesModule::onTick(const TickContext& ctx, Actions& out) {
    // Ticks only come from a running game: config.cfg is loaded.
    if (ctx.hasSnapshot && !defaultsV2_) migrateDefaults(out);
    if (phase_ == CoursesPhase::Idle) return;
    if (!cfg_.enabled) {
        stopExercise(true);
        return;
    }
    if (!ctx.ownsActivity || !ctx.worldControlAllowed) {  // lost the activity without being told: stand down
        phase_ = CoursesPhase::Idle;
        resetTransient();
        return;
    }
    if (!ctx.hasSnapshot) return;
    const Snapshot& s = ctx.snap;
    if (s.frame == lastFrame_) return;
    if (s.paused) {  // per-tick flag (the GameContext copy is refreshed only every 0.25 s)
        // Frames that pass while paused never count in the attempt time.
        if (phase_ == CoursesPhase::Running && lastFrame_ >= 0 && s.frame > lastFrame_) startFrame_ += s.frame - lastFrame_;
        lastFrame_ = s.frame;
        return;
    }
    lastFrame_ = s.frame;
    if (flashTicks_ > 0) --flashTicks_;
    if (!s.car.valid) {
        havePrev_ = false;
        return;
    }
    switch (phase_) {
    case CoursesPhase::Setup: {
        // Get-ready phase (fix 1): the first attempt of an exercise waits until the player presses the throttle, then
        // counts 3 s down; the next attempts count `countdown` seconds down. The car and the ball are held still and
        // re-placed on every tick until GO.
        countdownTotal_ = std::max(0, static_cast<int>(std::lround(cfg_.countdown * 120.f)));
        phaseTicks_ = 0;
        lastCountdownMsg_ = 0;
        ReadyPhase::Options o = firstAttempt_ ? ReadyPhase::firstAttempt()
                                              : ReadyPhase::Options{false, std::max(0.f, cfg_.countdown), true, false};
        // Ball balanced on a stopped car: a throttle still held at GO would throw it off the roof (the kickoff habit),
        // so GO waits until the throttle is let go.
        o.waitThrottleRelease = roofStartAtRest(ctx);
        ready_.start(s.time, o);
        firstAttempt_ = false;
        phase_ = CoursesPhase::Countdown;
        countdownTick(ctx, out);
        break;
    }
    case CoursesPhase::Countdown: countdownTick(ctx, out); break;
    case CoursesPhase::Running: runningTick(ctx, out); break;
    case CoursesPhase::Result: resultTick(ctx, out); break;
    default: break;
    }
}

void CoursesModule::startPose(const TickContext& ctx, CarPlacement& cp, std::optional<BallPlacement>& ball) const {
    const CarState& car = ctx.snap.car;
    Vec3 carPos;
    Vec3 carVel;
    float yawRad = 0.f;
    if (full_) {
        carPos = fullCarPos_;
        yawRad = fullYawDeg_ * kDegToRad;
    } else {
        switch (kind_) {
        case CoursesKind::Dribble:
            carPos = dribbleCarPos_;
            yawRad = kPi / 2.f;
            break;
        case CoursesKind::Catch:
            carPos = catchPlan_.carPos;
            yawRad = catchPlan_.carYawRad;
            break;
        default:
            carPos = courses::toF(course_.start.carPos);
            yawRad = static_cast<float>(course_.start.carYaw);
            carVel = courses::toF(course_.start.carVel);
            break;
        }
    }
    cp = CarPlacement{};
    cp.pos = carPos;
    cp.rot = rotFromYawDeg(yawRad * kRadToDeg);
    cp.vel = carVel;
    cp.angVel = {};
    if (kindIsRings(kind_)) cp.boost = cfg_.unlimitedBoost ? 100.f : static_cast<float>(std::clamp(cfg_.boostStart, 0, 100));
    else cp.boost = 100.f;
    if (full_) cp.boost = 100.f;  // the full sequences always start with a full tank

    const Basis b = basisFromRot(cp.rot);
    const float radius = ctx.snap.ball.valid ? ctx.snap.ball.radius : 91.25f;
    const Vec3 roof = courses::roofSpawn(carPos, b, car.hitboxOffset, car.hitboxExtent, radius);
    ball.reset();
    if (full_) {
        if (kindUsesPickup(kind_)) ball = pickupSetup_.ball;
        else if (kind_ == CoursesKind::Catch) ball = BallPlacement{fullBallPos_, fullBallVel_, {}};
        else if (cfg_.parkBall) ball = BallPlacement{parkPos_, {}, {}};
        return;
    }
    switch (kind_) {
    case CoursesKind::RingsAir:
    case CoursesKind::RingsGround:
        if (cfg_.parkBall) ball = BallPlacement{parkPos_, {}, {}};
        break;
    case CoursesKind::RingsAirDribble: {
        // Generator v2 stores the start in the course (and its code); version 1 courses follow the setting.
        const bool popped = course_.version >= 2 ? !course_.start.ballOnRoof : cfg_.airDribbleStart == 1;
        if (popped) ball = BallPlacement{courses::toF(course_.start.ballPos), courses::toF(course_.start.ballVel), {}};
        else ball = BallPlacement{roof, carVel, {}};
        break;
    }
    case CoursesKind::Slalom:
    case CoursesKind::Dribble: ball = BallPlacement{roof, carVel, {}}; break;
    case CoursesKind::Catch: ball = BallPlacement{catchPlan_.ballPos, catchPlan_.ballVel, {}}; break;
    }
}

bool CoursesModule::roofStartAtRest(const TickContext& ctx) const {
    if (full_) return false;  // the full sequences start with a pickup, the ball on the floor
    if (kind_ != CoursesKind::Dribble && kind_ != CoursesKind::Slalom && kind_ != CoursesKind::RingsAirDribble) return false;
    CarPlacement cp;
    std::optional<BallPlacement> ball;
    startPose(ctx, cp, ball);
    // Ball on the roof: same velocity as the car (a popped air dribble start throws it up instead).
    return ball && length(cp.vel) < 1.f && length(ball->vel) < 1.f && ball->pos.z > cp.pos.z + 50.f;
}

void CoursesModule::applyStartPose(const TickContext& ctx, Actions& out, bool release) {
    CarPlacement cp;
    std::optional<BallPlacement> ball;
    startPose(ctx, cp, ball);
    if (release) {
        out.setCar = cp;
        if (ball) out.setBall = *ball;
        return;
    }
    // Held: zero velocities, re-sent on every tick (gravity still acts on a car or a ball placed only once).
    HoldPose hold;
    hold.car = cp;
    hold.ball = ball;
    hold.boost = cp.boost;
    applyHold(hold, &ctx.snap, out);
}

void CoursesModule::countdownTick(const TickContext& ctx, Actions& out) {
    if (ready_.update(ctx.snap, out)) {  // GO tick: real velocities, no freeze (the player's inputs go through)
        applyStartPose(ctx, out, true);
        out.goMessage = true;
        phase_ = CoursesPhase::Running;
        phaseTicks_ = 0;
        startFrame_ = ctx.snap.frame;
        havePrev_ = false;
        prevT_ = 0.0;
        runTime_ = 0.f;
        lastBoost_ = -1.f;
        // The first sample is checked against the start pose (catches a teleport on the very first tick).
        if (kindUsesCourse(kind_) && !full_) {
            if (course_.ballCourse && out.setBall) {
                prevPos_ = courses::toD(out.setBall->pos);
                prevVel_ = courses::toD(out.setBall->vel);
                havePrev_ = true;
            } else if (!course_.ballCourse && out.setCar) {
                const Basis b = basisFromRot(out.setCar->rot);
                prevPos_ = courses::toD(out.setCar->pos + b.toWorld(ctx.snap.car.hitboxOffset));
                prevVel_ = courses::toD(out.setCar->vel);
                havePrev_ = true;
            }
        }
        if (full_) fullRelease(ctx);
        return;
    }
    applyStartPose(ctx, out, false);
    ++phaseTicks_;
}

void CoursesModule::trackCar(const Snapshot& s) {
    speedSum_ += static_cast<double>(speed(s.car));
    ++speedSamples_;
    if (lastBoost_ >= 0.f && s.car.boost < lastBoost_) boostUsed_ += lastBoost_ - s.car.boost;
    lastBoost_ = s.car.boost;
    // "Ran out of boost" only when there was real boost to spend (a start with 0 boost is not a mistake).
    if (s.car.boost > 5.f) hadBoost_ = true;
    if (!cfg_.unlimitedBoost && hadBoost_ && s.car.boost <= 0.5f) boostEmptied_ = true;
}

void CoursesModule::trackRoof(const Snapshot& s, double t, float dt) {
    const bool onRoof = courses::ballOnRoof(s.ball, s.car);
    const bool onFloor = courses::ballOnFloor(s.ball);
    ballOnRoofNow_ = onRoof;
    if (!s.ball.valid) return;
    if (wasOnRoof_ && !onRoof) {
        departRelVel_ = s.car.basis.toLocal(s.ball.vel - s.car.vel);
        departRelPos_ = s.car.basis.toLocal(s.ball.pos - hitboxCenter(s.car));
    }
    wasOnRoof_ = onRoof;
    keeper_.update(onRoof, onFloor, static_cast<float>(t), dt);
}

void CoursesModule::parkBallIfNeeded(Actions& out) const {
    if ((kind_ == CoursesKind::RingsAir || kind_ == CoursesKind::RingsGround) && cfg_.parkBall)
        out.setBall = BallPlacement{parkPos_, {}, {}};
}

void CoursesModule::runningTick(const TickContext& ctx, Actions& out) {
    const Snapshot& s = ctx.snap;
    const double t = static_cast<double>(s.frame - startFrame_) / 120.0;
    const float dt = std::clamp(static_cast<float>(t) - runTime_, 0.f, 1.f);
    runTime_ = static_cast<float>(t);
    trackCar(s);
    if (full_) {
        fullTick(ctx, out, t, dt);
        return;
    }
    switch (kind_) {
    case CoursesKind::Dribble: dribbleTick(ctx, out, t, dt); break;
    case CoursesKind::Catch: catchTick(ctx, out, t, dt); break;
    default: ringsTick(ctx, out, t, dt); break;
    }
}

// ---- Rings and slalom --------------------------------------------------------------------------------------
void CoursesModule::ringsTick(const TickContext& ctx, Actions& out, double t, float dt) {
    const Snapshot& s = ctx.snap;
    const bool ballCourse = course_.ballCourse;
    const bool valid = ballCourse ? s.ball.valid : s.car.valid;
    if (valid) {
        const courses::DVec3 p1 = courses::toD(ballCourse ? s.ball.pos : hitboxCenter(s.car));
        const courses::DVec3 v1 = courses::toD(ballCourse ? s.ball.vel : s.car.vel);
        if (havePrev_) {
            const double dt = t - prevT_;
            const double allowed = std::max(courses::length(prevVel_), courses::length(v1)) * dt * 1.5 + 120.0;
            if (courses::length(p1 - prevPos_) > allowed) {
                ++teleports_;  // teleport (reset, another plugin): never test this segment
            } else {
                const int nextBefore = run_.nextRing();
                const size_t splitsBefore = run_.splits().size();
                const courses::RunEvent ev = run_.step(prevPos_, p1, prevT_, t, &prevVel_, &v1);
                // New splits: boost refills and delta with the personal best.
                const PbEntry* pb = run_.splits().size() > splitsBefore ? findPb(code_) : nullptr;
                for (size_t i = splitsBefore; i < run_.splits().size(); ++i) {
                    const courses::RunSplit& sp = run_.splits()[i];
                    if (pb && pb->time > 0.f && static_cast<size_t>(sp.ring) < pb->splits.size()) {
                        lastDelta_ = static_cast<float>(sp.time) - pb->splits[static_cast<size_t>(sp.ring)];
                        hasDelta_ = true;
                    }
                    const courses::Ring& ring = course_.rings[static_cast<size_t>(sp.ring)];
                    if (!sp.missed && (ring.flags & courses::kFlagBoostRefill) && cfg_.boostRefills && !cfg_.unlimitedBoost) {
                        out.setBoost = 100.f;
                        flash("Boost rechargé !", colors::Orange, 1.f);
                    }
                }
                const bool penalty = missPolicy() == courses::MissPolicy::Penalty;
                const bool gate = gateCourse();
                const std::string num = std::to_string(nextBefore + 1);
                const std::string plus = " : +" + formatNumber(cfg_.missPenalty, 1) + " s";
                switch (ev) {
                case courses::RunEvent::Missed:
                    if (penalty) flash((gate ? "Porte " + num + " ratée" : "Anneau " + num + " raté") + plus, colors::Red);
                    else flash(gate ? "Ratée : repasse par la porte " + num + " !"
                                    : "Raté : repasse dans l'anneau " + num + " !",
                               colors::Red);
                    break;
                case courses::RunEvent::Skipped:
                    if (penalty) flash((gate ? "Porte " + num + " sautée" : "Anneau " + num + " sauté") + plus, colors::Red);
                    else flash(gate ? "Porte " + num + " sautée : reviens la prendre !"
                                    : "Anneau " + num + " sauté : reviens le prendre !",
                               colors::Red);
                    break;
                case courses::RunEvent::WrongWay: flash("Mauvais sens !", colors::Orange); break;
                default: break;
                }
            }
        }
        prevPos_ = p1;
        prevVel_ = v1;
        prevT_ = t;
        havePrev_ = true;
    } else {
        havePrev_ = false;
    }

    if (run_.finished()) {
        finishRings(ctx, out);
        return;
    }
    if (run_.failed()) {
        if (gateCourse())
            failRun(ctx, out, "porte ratée (mode échec immédiat)", "courses.rings_missed", kTipSlalomWeave);
        else
            failRun(ctx, out, "anneau raté (mode échec immédiat)", "courses.rings_missed", kTipRingCentre);
        return;
    }
    if (cfg_.unlimitedBoost) out.setBoost = 100.f;
    if (ballCourse && t > 0.1 && courses::ballOnFloor(s.ball)) {
        if (kind_ == CoursesKind::Slalom)
            failRun(ctx, out, "la balle a touché le sol", "courses.slalom_ball_dropped",
                    "Ralentis avant chaque porte : la balle tombe quand tu tournes trop fort.");
        else
            failRun(ctx, out, "la balle a touché le sol", "courses.ad_ball_dropped",
                    "Garde la balle près du nez : petites touches de boost, pas de gros coups.");
        return;
    }
    if (kind_ == CoursesKind::Slalom) {
        // The slalom is a roof dribble: the ball may leave the roof only briefly (same tolerance as the dribble).
        trackRoof(s, t, dt);
        if (keeper_.lost()) {
            std::vector<Tip> tips;
            addFallTip(courses::classifyFall(departRelVel_, departRelPos_), true, tips);
            if (tips.empty())
                tips.push_back({"courses.slalom_off_roof",
                                "Garde la balle posée sur le toit : vire en douceur entre les portes.", 8.f});
            failRun(ctx, out, "la balle est tombée du toit", tips.front().code, tips.front().text);
            return;
        }
    }
    parkBallIfNeeded(out);
    if (t > static_cast<double>(timeLimit())) {
        failRun(ctx, out, "temps écoulé", "courses.rings_timeout",
                gateCourse() ? "Vise une trajectoire plus directe vers la porte suivante."
                             : "Vise une trajectoire plus directe vers l'anneau suivant.");
        return;
    }
}

void CoursesModule::finishRings(const TickContext& ctx, Actions& out) {
    const float time = static_cast<float>(run_.finishTime());
    const double par = std::max(1.0, course_.parTime);
    const int n = run_.ringCount();
    const int misses = run_.misses();
    const int passed = run_.ringsPassed();
    const bool gate = gateCourse();
    // Penalty mode gives missed rings up and goes on: reaching the end with most rings missed is not a success.
    if (n - passed > maxMissesAllowed(n)) {
        if (gate)
            failRun(ctx, out, "trop de portes ratées", "courses.rings_missed", kTipSlalomWeave);
        else
            failRun(ctx, out, "trop d'anneaux ratés", "courses.rings_missed",
                    "Vise le centre de chaque anneau : ralentis un peu pour mieux viser.");
        return;
    }
    PbEntry& pb = touchPb(code_, ctx.game.localDate);
    const bool hadBest = pb.time > 0.f;
    const float oldBest = pb.time;
    const bool teleported = teleports_ > 0;  // a teleport during the run: finished, but never a record
    // Rings given up (penalty mode) only cost the penalty: with a tiny one a sloppy run would beat every clean one.
    const bool fairTime = passed == n || cfg_.missPenalty >= kMinPenaltyForRecord;
    // One rule for both records: the personal best of this code and the record value reported to the coach.
    const bool recordEligible = !teleported && fairTime;
    const bool faster = !hadBest || time < oldBest - 1e-3f;
    const bool isPb = recordEligible && faster;
    ++pb.attempts;
    ++pb.finishes;
    std::vector<float> segments;
    float prev = 0.f;
    for (const auto& sp : run_.splits()) {
        segments.push_back(std::max(0.f, static_cast<float>(sp.time) - prev));
        prev = static_cast<float>(sp.time);
    }
    if (isPb) {
        pb.time = time;
        pb.splits.clear();
        for (const auto& sp : run_.splits())
            if (pb.splits.size() < static_cast<size_t>(kMaxSplits)) pb.splits.push_back(static_cast<float>(sp.time));
    }

    const double ratio = time / par;
    const float timeScore = rampScore(static_cast<float>(ratio), 0.9f, 1.9f);
    const float pbScore = hadBest ? rampScore(time / oldBest, 1.f, 1.3f) : timeScore;
    // Scaled by the share of rings actually passed; each miss (also a come-back) costs a little more.
    const float passFrac = n > 0 ? static_cast<float>(passed) / static_cast<float>(n) : 1.f;
    float score = (50.f + 35.f * timeScore + 15.f * pbScore) * passFrac - 2.f * static_cast<float>(misses);
    score = std::clamp(score, 45.f, 100.f);

    AttemptResult r;
    r.module = "courses";
    r.skill = kindSkill(kind_);
    r.title = kindTitle(kind_);
    r.score = score;
    r.success = true;
    r.verdict = verdictFromScore(score);
    r.metrics.push_back(metricIdeal("time", "Temps", time, "s", static_cast<float>(par)));
    r.metrics.push_back(metricIdeal("rings", gate ? "Portes passées" : "Anneaux passés", static_cast<float>(passed), "",
                                    static_cast<float>(n)));
    r.metrics.push_back(metricIdeal("misses", "Ratés", static_cast<float>(misses), "", 0.f));
    const float avgSpeed = speedSamples_ > 0 ? static_cast<float>(speedSum_ / speedSamples_) : 0.f;
    r.metrics.push_back(metric("avg_speed", "Vitesse moyenne", kph(avgSpeed), "km/h"));
    r.metrics.push_back(metric("boost_used", "Boost utilisé", boostUsed_, ""));
    if (hadBest) r.metrics.push_back(metricIdeal("pb_delta", "Écart avec le record", time - oldBest, "s", 0.f));
    double centring = 0.0;
    int counted = 0;
    for (const auto& sp : run_.splits())
        if (!sp.missed) {
            centring += sp.centring;
            ++counted;
        }
    if (counted > 0)
        r.metrics.push_back(metric("centring", "Centrage", 100.f * (1.f - static_cast<float>(centring / counted)), "%"));

    if (misses > 0)
        r.tips.push_back({"courses.rings_missed", gate ? kTipSlalomWeave : kTipRingCentre, 5.f * static_cast<float>(misses)});
    if (run_.grazes() >= 2)
        r.tips.push_back({"courses.rings_grazed",
                          gate ? "Passe plus au centre des portes : tu frôles les poteaux."
                               : "Passe plus au centre des anneaux : tu frôles les bords.",
                          2.f * static_cast<float>(run_.grazes())});
    if (ratio > 1.3)
        r.tips.push_back({"courses.rings_slow",
                          gate ? "Garde ta vitesse : prépare le virage suivant avant de passer la porte."
                               : "Garde ta vitesse : prépare le virage suivant avant de passer l'anneau.",
                          static_cast<float>((ratio - 1.3) * 40.0)});
    addBoostTip(kind_, boostEmptied_, r.tips);
    if (run_.wrongWays() > 0)
        r.tips.push_back({"courses.rings_wrong_way",
                          gate ? "Passe les portes dans le sens de la flèche." : "Passe les anneaux dans le sens de la flèche.",
                          3.f});
    sortTips(r.tips);
    r.time = ctx.snap.time;
    // Record value: the finish time (penalties included, the same time as the personal best of the code).
    r.valueForRecord = time;
    r.lowerIsBetter = true;
    r.hasRecordValue = recordEligible;

    const bool adaptiveSuccess = ratio <= 1.75;
    recordAdaptive(adaptiveSuccess);
    ++attempts_[static_cast<size_t>(kind_)];
    ++successes_[static_cast<size_t>(kind_)];

    const char* medal = medalFor(ratio);
    std::string headline = "Terminé en " + formatTime(time);
    if (isPb && hadBest) headline += " : nouveau record (" + signedSeconds(time - oldBest) + " s) !";
    else if (isPb) headline += " : premier chrono !";
    else if (hadBest) headline += " (record " + formatTime(oldBest) + ", " + signedSeconds(time - oldBest) + ")";
    if (teleported) headline += " - téléportation détectée, pas de record";
    else if (!fairTime && faster) headline += " - pénalité sous 1 s avec des ratés, pas de record";
    resultMedal_ = medal;
    resultHint_ = r.tips.empty() ? std::string() : r.tips.front().text;

    if (isPb && hadBest) {
        out.toasts.push_back({"Nouveau record !", std::string(kindTitle(kind_)) + " : " + formatTime(time) + " (" +
                                                      signedSeconds(time - oldBest) + " s)",
                              ToastKind::Ok, 4.f});
        say(out, "Record battu : " + formatTime(time) + " (" + signedSeconds(time - oldBest) + " s) sur " + code_);
    } else if (isPb) {
        say(out, "Premier chrono : " + formatTime(time) + " sur " + code_ + ". À battre !");
    }

    last_ = CoursesLastAttempt{};
    last_.valid = true;
    last_.kind = kind_;
    last_.title = kindTitle(kind_);
    last_.code = code_;
    last_.success = true;
    last_.score = score;
    last_.verdict = verdictLabel(r.verdict);
    last_.headline = headline;
    last_.time = time;
    last_.newRecord = isPb;
    last_.segments = std::move(segments);
    emitResult(std::move(r), ctx, out);
    enterResult(true);
    out.requestSave = true;
}

void CoursesModule::failRun(const TickContext& ctx, Actions& out, const std::string& reason, const std::string& tipCode,
                            const std::string& tipText) {
    const int n = std::max(1, run_.ringCount());
    const int passed = run_.ringsPassed();
    const float frac = static_cast<float>(passed) / static_cast<float>(n);
    const float score = std::clamp(40.f * frac, 0.f, 40.f);
    PbEntry& pb = touchPb(code_, ctx.game.localDate);
    ++pb.attempts;

    AttemptResult r;
    r.module = "courses";
    r.skill = kindSkill(kind_);
    r.title = kindTitle(kind_);
    r.score = score;
    r.success = false;
    r.verdict = Verdict::Fail;
    r.metrics.push_back(metric("time", "Temps", runTime_ + static_cast<float>(run_.penaltyTime()), "s"));
    r.metrics.push_back(metricIdeal("rings", gateCourse() ? "Portes passées" : "Anneaux passés", static_cast<float>(passed), "",
                                    static_cast<float>(n)));
    r.metrics.push_back(metricIdeal("misses", "Ratés", static_cast<float>(run_.misses()), "", 0.f));
    r.tips.push_back({tipCode, tipText, 10.f});
    if (run_.misses() > 0 && tipCode != "courses.rings_missed")
        r.tips.push_back({"courses.rings_missed",
                          gateCourse() ? "Passe au centre de chaque porte : la balle ne doit pas toucher les poteaux."
                                       : kTipRingCentre,
                          5.f * static_cast<float>(run_.misses())});
    addBoostTip(kind_, boostEmptied_, r.tips);
    sortTips(r.tips);
    r.time = ctx.snap.time;
    // Not finished: no finish time, so no record value (the score is never one).
    r.valueForRecord = 0.f;
    r.lowerIsBetter = true;
    r.hasRecordValue = false;

    recordAdaptive(false);
    ++attempts_[static_cast<size_t>(kind_)];

    last_ = CoursesLastAttempt{};
    last_.valid = true;
    last_.kind = kind_;
    last_.title = kindTitle(kind_);
    last_.code = code_;
    last_.success = false;
    last_.score = score;
    last_.verdict = verdictLabel(Verdict::Fail);
    last_.headline = "Raté : " + reason + " (" + std::to_string(passed) + "/" + std::to_string(n) + ")";
    last_.time = runTime_;
    resultHint_ = tipText;
    float prev = 0.f;
    for (const auto& sp : run_.splits()) {
        last_.segments.push_back(std::max(0.f, static_cast<float>(sp.time) - prev));
        prev = static_cast<float>(sp.time);
    }
    emitResult(std::move(r), ctx, out);
    enterResult(false);
    out.requestSave = true;
}

// ---- Roof dribble ------------------------------------------------------------------------------------------
void CoursesModule::dribbleTick(const TickContext& ctx, Actions& out, double t, float dt) {
    trackRoof(ctx.snap, t, dt);
    if (!targetReached_ && keeper_.heldTime() >= dribbleTarget_) {
        targetReached_ = true;
        flash("Objectif atteint ! Continue pour ton record.", colors::Green, 2.5f);
    }
    if (keeper_.lost() || t > 600.0) endDribble(ctx, out);
}

void CoursesModule::endDribble(const TickContext& ctx, Actions& out) {
    const float held = keeper_.heldTime();
    const float avg = speedSamples_ > 0 ? static_cast<float>(speedSum_ / speedSamples_) : 0.f;
    const bool speedOk = avg >= dribbleMinSpeed_;
    const bool success = held >= dribbleTarget_ && speedOk;
    const float ratio = dribbleTarget_ > 0.f ? held / dribbleTarget_ : 0.f;
    float score = 85.f * std::min(1.f, ratio) + 15.f * clamp01(dribbleMinSpeed_ > 0.f ? avg / dribbleMinSpeed_ : 1.f);
    if (ratio > 1.f) score += std::min(10.f, 10.f * (ratio - 1.f));
    score = std::clamp(score, 0.f, 100.f);
    if (!success) score = std::min(score, 65.f);
    // Same rule for the module best and the record value reported to the coach (time held, higher is better).
    const bool recordEligible = held >= kMinDribbleRecord;
    const bool isRecord = recordEligible && held > dribbleBest_ + 0.01f;
    const float oldBest = dribbleBest_;
    if (isRecord) dribbleBest_ = held;

    AttemptResult r;
    r.module = "courses";
    r.skill = kindSkill(kind_);
    r.title = kindTitle(kind_);
    r.score = score;
    r.success = success;
    r.verdict = success ? verdictFromScore(score) : (score >= 45.f ? Verdict::Ok : Verdict::Fail);
    r.metrics.push_back(metricIdeal("held", "Temps tenu", held, "s", dribbleTarget_));
    r.metrics.push_back(metricIdeal("avg_speed", "Vitesse moyenne", kph(avg), "km/h", kph(dribbleMinSpeed_)));
    r.metrics.push_back(metric("touches", "Touches", static_cast<float>(touches_), ""));
    const SpeedUnit unit = resolveUnit(SpeedUnit::Auto, ctx.profile.metric);
    if (!speedOk)
        r.tips.push_back({"courses.dribble_slow", "Roule plus vite : vise au moins " + formatSpeed(dribbleMinSpeed_, unit, 0) + " de moyenne.", 6.f});
    if (!success || keeper_.lost()) {
        if (keeper_.loss() == courses::RoofKeeper::Loss::Floor && !keeper_.everOnRoof()) {
            r.tips.push_back({"courses.dribble_lost", "Démarre en douceur : la balle doit rester posée sur le toit.", 8.f});
        } else {
            addFallTip(courses::classifyFall(departRelVel_, departRelPos_), false, r.tips);
        }
    }
    sortTips(r.tips);
    r.time = ctx.snap.time;
    r.valueForRecord = held;
    r.lowerIsBetter = false;
    r.hasRecordValue = recordEligible;

    recordAdaptive(success);
    ++attempts_[static_cast<size_t>(kind_)];
    if (success) ++successes_[static_cast<size_t>(kind_)];
    if (isRecord && oldBest > 0.f) {
        out.toasts.push_back({"Nouveau record !", "Dribble sur le toit : " + formatNumber(held, 1) + " s", ToastKind::Ok, 4.f});
        say(out, "Record de dribble battu : " + formatNumber(held, 1) + " s (avant " + formatNumber(oldBest, 1) + " s).");
    }

    last_ = CoursesLastAttempt{};
    last_.valid = true;
    last_.kind = kind_;
    last_.title = kindTitle(kind_);
    last_.success = success;
    last_.score = score;
    last_.verdict = verdictLabel(r.verdict);
    last_.headline = success ? "Objectif atteint : " + formatNumber(held, 1) + " s tenues"
                             : "Balle perdue après " + formatNumber(held, 1) + " s";
    if (isRecord) last_.headline += " (record !)";
    last_.time = held;
    last_.newRecord = isRecord;
    resultHint_ = r.tips.empty() ? std::string() : r.tips.front().text;
    emitResult(std::move(r), ctx, out);
    enterResult(success);
    out.requestSave = true;
}

// ---- Catch -------------------------------------------------------------------------------------------------
bool CoursesModule::catchLooseOnRoof(const Snapshot& s) const {
    if (!s.ball.valid || !s.car.valid) return false;
    return courses::ballOnRoof(s.ball.pos, s.car.pos, s.car.basis, s.car.hitboxOffset, s.car.hitboxExtent, s.ball.radius,
                               courses::kCatchLooseTolUp);
}

void CoursesModule::catchTick(const TickContext& ctx, Actions& out, double t, float dt) {
    const Snapshot& s = ctx.snap;
    // Tiny hops still count as on the roof, and a short break (kCatchHopGrace) does not restart the hold.
    const bool onRoof = catchLooseOnRoof(s);
    ballOnRoofNow_ = onRoof;
    if (onRoof) {
        catchOnRoof_ += dt;
        catchGap_ = 0.f;
    } else if (catchOnRoof_ > 0.f) {
        catchGap_ += dt;
        if (catchGap_ > courses::kCatchHopGrace) {
            catchOnRoof_ = 0.f;
            catchGap_ = 0.f;
        }
    }
    // HUD: the ball hops ahead of the roof, the player should roll forward under it.
    if (!onRoof && haveFirstTouch_ && s.ball.valid) {
        const Vec3 local = s.car.basis.toLocal(s.ball.pos - hitboxCenter(s.car));
        const Vec3 rel = s.car.basis.toLocal(s.ball.vel - s.car.vel);
        catchHopFwd_ = local.x > s.car.hitboxExtent.x * 0.25f || rel.x > 80.f;
    } else {
        catchHopFwd_ = false;
    }
    if (catchOnRoof_ + 1e-4f >= cfg_.catchHold) {
        endCatch(ctx, out, true, std::string());
        return;
    }
    if (courses::ballOnFloor(s.ball)) {
        endCatch(ctx, out, false, "la balle est tombée au sol");
        return;
    }
    if (t > static_cast<double>(catchPlan_.timeLimit) && catchOnRoof_ <= 0.f) {
        endCatch(ctx, out, false, "trop tard");
        return;
    }
}

void CoursesModule::endCatch(const TickContext& ctx, Actions& out, bool success, const std::string& reason) {
    const float settle = success ? std::max(0.f, runTime_ - catchOnRoof_) : 0.f;
    // Softness of the first contact: only what the player controls (speed matching, not jumping into the ball).
    // The fall speed itself (800 to 1300 uu/s on the roof) is the same for everyone waiting under the ball.
    const float avoidable = std::sqrt(firstTouchFlat_ * firstTouchFlat_ + firstTouchJump_ * firstTouchJump_);
    const float softness = haveFirstTouch_ ? rampScore(avoidable, kCatchSoftGood, kCatchSoftBad) : 1.f;
    float score = 5.f;
    if (success) {
        const float window = std::max(0.5f, catchPlan_.timeLimit - catchPlan_.fallTime);
        const float speedPart = 1.f - clamp01((settle - catchPlan_.fallTime) / window);
        score = 60.f + 25.f * speedPart + 15.f * softness;
    } else if (touches_ > 0) {
        score = 25.f;
    }
    score = std::clamp(score, 0.f, 100.f);
    // Fastest catch: a plain stat of the tab, not a record (see the record value below).
    const bool isFastest = success && (catchBest_ <= 0.f || settle < catchBest_ - 0.005f);
    if (isFastest) catchBest_ = settle;

    AttemptResult r;
    r.module = "courses";
    r.skill = kindSkill(kind_);
    r.title = kindTitle(kind_);
    r.score = score;
    r.success = success;
    r.verdict = success ? verdictFromScore(score) : Verdict::Fail;
    if (success) r.metrics.push_back(metric("settle_time", "Temps pour contrôler", settle, "s"));
    r.metrics.push_back(metric("touches", "Touches", static_cast<float>(touches_), ""));
    if (haveFirstTouch_) {
        r.metrics.push_back(metricIdeal("first_touch_speed_gap", "Écart de vitesse au 1er contact", kph(firstTouchFlat_), "km/h", 0.f));
        r.metrics.push_back(metricIdeal("first_touch_offset", "Décalage sur le toit au 1er contact", firstTouchOffset_, "uu", 0.f));
        if (firstTouchJump_ > 100.f)
            r.metrics.push_back(metricIdeal("first_touch_jump", "Montée vers la balle au contact", kph(firstTouchJump_), "km/h", 0.f));
    }
    r.metrics.push_back(metric("drop_height", "Hauteur de départ", catchPlan_.ballPos.z, "uu"));
    if (haveFirstTouch_ && firstTouchJump_ > kCatchHardTip && firstTouchJump_ >= firstTouchFlat_)
        r.tips.push_back({"courses.catch_jump_into", "Ne saute pas vers la balle : attends-la roues au sol, juste sous elle.",
                          (firstTouchJump_ - kCatchHardTip) / 50.f + 4.f});
    else if (haveFirstTouch_ && firstTouchFlat_ > kCatchHardTip)
        r.tips.push_back({"courses.catch_hard_touch", "Cale ta vitesse sur celle de la balle : arrive sous elle sans la percuter.",
                          (firstTouchFlat_ - kCatchHardTip) / 50.f + 4.f});
    if (!success) {
        if (touches_ == 0)
            r.tips.push_back({"courses.catch_no_touch", "Place-toi sous le point de chute plus tôt : suis le cercle au sol.", 9.f});
        else if (reason == "trop tard")
            r.tips.push_back({"courses.catch_late", "Pose la balle plus vite : freine sous elle au lieu de la pousser.", 7.f});
        else if (haveFirstTouch_ && firstTouchOffset_ > kCatchOffCentre)
            r.tips.push_back({"courses.catch_off_centre", "Mets le centre du toit pile sous la balle : sur le bord, elle glisse.", 8.f});
        else
            r.tips.push_back({"courses.catch_fell", "Garde la voiture sous la balle après le contact : petits ajustements.", 8.f});
    }
    sortTips(r.tips);
    r.time = ctx.snap.time;
    // No record value: the settle time mostly measures how far the ball falls, and every attempt drops it from
    // somewhere else (a low drop would beat any clean catch). The score is never a record either.
    r.valueForRecord = 0.f;
    r.lowerIsBetter = false;
    r.hasRecordValue = false;

    recordAdaptive(success);
    ++attempts_[static_cast<size_t>(kind_)];
    if (success) ++successes_[static_cast<size_t>(kind_)];

    last_ = CoursesLastAttempt{};
    last_.valid = true;
    last_.kind = kind_;
    last_.title = kindTitle(kind_);
    last_.success = success;
    last_.score = score;
    last_.verdict = verdictLabel(r.verdict);
    last_.headline = success ? "Balle contrôlée en " + formatNumber(settle, 1) + " s" : "Raté : " + reason;
    if (isFastest && attempts_[static_cast<size_t>(kind_)] > 1) last_.headline += " (le plus rapide !)";
    last_.time = success ? settle : runTime_;
    last_.newRecord = false;  // the catch has no record
    resultHint_ = r.tips.empty() ? std::string() : r.tips.front().text;
    emitResult(std::move(r), ctx, out);
    enterResult(success);
    out.requestSave = true;
}

// ---- Result and chaining -----------------------------------------------------------------------------------
void CoursesModule::enterResult(bool success) {
    phase_ = CoursesPhase::Result;
    phaseTicks_ = 0;
    lastSuccess_ = success;
}

void CoursesModule::recordAdaptive(bool success) {
    if (cfg_.difficultyMode != 0 || fromCode_) return;
    stairs_[static_cast<size_t>(kind_)].record(success);
}

void CoursesModule::emitResult(AttemptResult r, const TickContext& ctx, Actions& out) {
    (void)ctx;
    out.results.push_back(std::move(r));
}

void CoursesModule::resultTick(const TickContext& ctx, Actions& out) {
    ++phaseTicks_;
    parkBallIfNeeded(out);
    const int hold = std::max(1, static_cast<int>(std::lround(cfg_.resultHold * 120.f)));
    if (phaseTicks_ < hold) return;
    switch (cfg_.chainMode) {
    case 1: restartAttempt(); break;
    case 2:
        if (lastSuccess_) nextAttempt(ctx, out);
        else restartAttempt();
        break;
    case 3: stopExercise(true); break;
    default:
        // Full sequences and control drills: a new start at the current level after every attempt.
        if (full_ || kind_ == CoursesKind::Catch || kind_ == CoursesKind::Dribble) nextAttempt(ctx, out);
        else if (lastSuccess_ && cfg_.difficultyMode == 0 && !fromCode_) nextAttempt(ctx, out);
        else restartAttempt();
        break;
    }
}

} // namespace rlt
