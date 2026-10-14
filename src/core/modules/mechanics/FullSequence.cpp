#include "core/modules/mechanics/FullSequence.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace rlt::mech {

namespace {

using LivePtr = std::shared_ptr<FullLive>;

StepVerdict pending() { return StepVerdict::pending(); }
StepVerdict done() { return StepVerdict::done(); }
StepVerdict fail(const char* code) { return StepVerdict::fail(code); }

Step makeStep(const char* id, const char* label, const char* failCode, StepCheck check, float timeout = 0.f) {
    Step st;
    st.id = id;
    st.label = label;
    st.failCode = failCode;
    st.check = std::move(check);
    st.timeout = timeout;
    return st;
}

float fwdSpeed(const CarState& c) { return dot(c.vel, c.basis.fwd); }
float noseDegOn(const CarState& c, const Vec3& n) { return safeAsinDeg(dot(c.basis.fwd, n)); }
// Signed yaw (deg) from a to b around +Z (positive: counter-clockwise seen from above).
float signedYawDeg(const Vec3& a, const Vec3& b) {
    const float ca = std::atan2(a.y, a.x), cb = std::atan2(b.y, b.x);
    return wrapPi(cb - ca) * kRadToDeg;
}
bool isDashGrade(const MechGrade& g) { return g.variant != "late" && g.variant != "nostick" && g.variant != "early"; }
float boostPerTick() { return k::kBoostUsedPerSecond * k::kDt; }

// ---- Speedflip: a real kickoff --------------------------------------------------------------------------------
std::vector<Step> speedflip(float level, const LivePtr& live) {
    auto L = [level](float a, float b) { return levelLerp(level, a, b); };
    struct S {
        int64_t dodgeTick = -1;
    };
    auto st = std::make_shared<S>();
    const int64_t boostTicks = static_cast<int64_t>(std::lround(L(12.f, 6.f)));
    const float aimDeg = L(10.f, 4.f), jumpToFlip = L(24.f, 14.f), angleTol = L(20.f, 15.f);
    const float cancelMax = L(10.f, 8.f), headingMax = L(25.f, 15.f), strengthMin = L(0.6f, 0.8f);
    const float ttbMax = kickoffTimeToBall(live->spotKind, level * 0.75f);
    std::vector<Step> v;
    v.push_back(makeStep("sf.boost", "Boost et accélérateur", "SF_NO_BOOST", [live, boostTicks](const StepInput&) {
        const FullLive& l = *live;
        if (l.s.in.boost) return done();
        return l.sinceGo() > boostTicks ? fail("SF_NO_BOOST") : pending();
    }));
    v.push_back(makeStep("sf.aim", "Tourne un peu vers la balle", "SF_NO_JUMP", [live, aimDeg](const StepInput&) {
        const FullLive& l = *live;
        if (!l.jumpedNow()) return l.sinceGo() > 96 ? fail("KO_FIRST_JUMP_LATE") : pending();
        // Aim of the nose just before the jump (in the air nothing corrects it any more).
        const float err = planarAngleDeg(l.p.car.basis.fwd, l.s.ball.pos - l.p.car.pos, {0.f, 0.f, 1.f});
        return err <= aimDeg ? done() : fail("SF_NOT_AIMED");
    }));
    v.push_back(makeStep("sf.jump", "Petit saut", "SF_NO_JUMP", [live](const StepInput&) {
        const FullLive& l = *live;
        const MotionTracker& mt = l.motion();
        if (mt.phaseJumpTick < 0) return l.sinceGo() > 96 ? fail("KO_FIRST_JUMP_LATE") : pending();
        const int64_t at = mt.phaseJumpTick - l.goFrame;
        if (at < 48) return fail("KO_FIRST_JUMP_EARLY");
        if (at > 96) return fail("KO_FIRST_JUMP_LATE");
        const int64_t hold = mt.firstJumpHoldTicks();
        if (hold < 0) return l.s.frame - mt.phaseJumpPressTick > 12 ? fail("SF_JUMP_HELD") : pending();
        return hold <= 12 ? done() : fail("SF_JUMP_HELD");
    }));
    v.push_back(makeStep("sf.flip", "Flip en diagonale", "SF_NO_FLIP", [live, st, jumpToFlip, angleTol](const StepInput&) {
        const FullLive& l = *live;
        if (const DodgeRecord* r = l.dodgeNow()) {
            st->dodgeTick = r->tick;
            if (r->kind == DodgeKind::Side) return fail("SF_SIDE_FLIP");
            if (r->kind == DodgeKind::DiagBack || r->kind == DodgeKind::Back) return fail("SF_BACK_FLIP");
            if (static_cast<float>(r->ticksSinceFirstJump) > jumpToFlip) return fail("SF_JUMP_LONG");
            const float err = std::fabs(r->angleDeg) - l.coach->cfg.speedflipTargetAngleDeg;
            if (err < -angleTol) return fail("SF_ANGLE_STRAIGHT");
            if (err > angleTol) return fail("SF_ANGLE_WIDE");
            return done();
        }
        const MotionTracker& mt = l.motion();
        if (l.doubleJumpedNow() || l.landedNow()) return fail("SF_NO_FLIP");
        if (mt.phaseJumpTick >= 0 && static_cast<float>(l.s.frame - mt.phaseJumpTick) > jumpToFlip) return fail("SF_JUMP_LONG");
        return pending();
    }));
    v.push_back(makeStep("sf.cancel", "Stick vers toi tout de suite", "SF_NO_CANCEL", [live, st, cancelMax](const StepInput&) {
        const FullLive& l = *live;
        const DodgeRecord* r = l.dodge(st->dodgeTick);
        if (!r) return fail("SF_NO_CANCEL");
        if (r->cancelTicks >= 0) return static_cast<float>(r->cancelTicks) <= cancelMax ? done() : fail("SF_LATE_CANCEL");
        return static_cast<float>(l.s.frame - r->tick) > cancelMax ? fail("SF_NO_CANCEL") : pending();
    }));
    v.push_back(makeStep("sf.land", "Roues au sol, fonce", "SF_BAD_LANDING", [live, st, headingMax, strengthMin](const StepInput&) {
        const FullLive& l = *live;
        const DodgeRecord* r = l.dodge(st->dodgeTick);
        if (!r) return fail("SF_BAD_LANDING");
        if (r->landingTicks < 0) return pending();
        if (!r->landedOnWheels) return fail("SF_BAD_LANDING");
        const float strength = r->cancelSamples > 0 ? r->cancelStrength / static_cast<float>(r->cancelSamples) : 0.f;
        if (strength < strengthMin) return fail("FC_WEAK");
        if (planarAngleDeg(r->fwdAtLanding, r->fwdAtFirstJump, {0.f, 0.f, 1.f}) > headingMax) return fail("SF_HEADING_OFF");
        const Vec3 right = normalize(Vec3{r->rightAtFirstJump.x, r->rightAtFirstJump.y, 0.f});
        if (std::fabs(dot(r->posAtLanding - r->posAtFirstJump, right)) > 425.f) return fail("SF_DRIFT");
        return done();
    }));
    v.push_back(makeStep("sf.touch", "Touche la balle", "DRILL_NO_TOUCH", [live, ttbMax](const StepInput&) {
        const FullLive& l = *live;
        if (l.touchTime < 0.f) return pending();
        return l.touchTime - l.goTime <= ttbMax ? done() : fail("SF_SLOW_TO_BALL");
    }));
    return v;
}

// ---- Half-flip: reverse, back flip, cancel, air roll, drive ----------------------------------------------------
std::vector<Step> halfflip(float level, const LivePtr& live) {
    auto L = [level](float a, float b) { return levelLerp(level, a, b); };
    struct S {
        int64_t dodgeTick = -1;
    };
    auto st = std::make_shared<S>();
    const float jumpToFlip = L(24.f, 20.f), cancelLo = L(6.f, 10.f), cancelHi = L(48.f, 36.f);
    const float headingTol = L(60.f, 25.f), strengthMin = L(0.6f, 0.85f), ttbMax = L(6.f, 2.6f);
    std::vector<Step> v;
    v.push_back(makeStep("hf.reverse", "Recule vers la balle", "HF_NO_REVERSE", [live](const StepInput&) {
        const FullLive& l = *live;
        if (l.s.car.jumped) return fail("HF_NO_REVERSE");
        return (l.s.in.throttle <= -0.5f && -fwdSpeed(l.s.car) >= 250.f) ? done() : pending();
    }, L(2.f, 1.5f)));
    v.push_back(makeStep("hf.flip", "Saute, puis flip arrière", "HF_NO_FLIP", [live, st, jumpToFlip](const StepInput&) {
        const FullLive& l = *live;
        if (const DodgeRecord* r = l.dodgeNow()) {
            st->dodgeTick = r->tick;
            const float a = std::fabs(r->angleDeg);
            if (r->kind == DodgeKind::Stall || a < 105.f) return fail("HF_WRONG_FLIP");
            if (a < 150.f) return fail("HF_NOT_STRAIGHT");
            if (static_cast<float>(r->ticksSinceFirstJump) > jumpToFlip) return fail("HF_HIGH_JUMP");
            return done();
        }
        const MotionTracker& mt = l.motion();
        if (l.doubleJumpedNow() || (l.landedNow() && l.p.car.jumped)) return fail("HF_NO_FLIP");
        if (mt.phaseJumpTick >= 0 && static_cast<float>(l.s.frame - mt.phaseJumpTick) > jumpToFlip) return fail("HF_HIGH_JUMP");
        return pending();
    }, 3.f));
    v.push_back(makeStep("hf.cancel", "Nez en l'air : stick vers l'avant", "HF_NO_CANCEL", [live, st, cancelLo, cancelHi](const StepInput&) {
        const FullLive& l = *live;
        const DodgeRecord* r = l.dodge(st->dodgeTick);
        if (!r) return fail("HF_NO_CANCEL");
        if (r->cancelTicks >= 0) {
            if (static_cast<float>(r->cancelTicks) < cancelLo) return fail("HF_EARLY_CANCEL");
            return static_cast<float>(r->cancelTicks) <= cancelHi ? done() : fail("HF_LATE_CANCEL");
        }
        if (r->landingTicks >= 0) return fail("HF_NO_CANCEL");
        return static_cast<float>(l.s.frame - r->tick) > cancelHi ? fail("HF_LATE_CANCEL") : pending();
    }));
    v.push_back(makeStep("hf.roll", "Air roll, roues au sol", "HF_BAD_LANDING", [live, st, headingTol, strengthMin](const StepInput&) {
        const FullLive& l = *live;
        const DodgeRecord* r = l.dodge(st->dodgeTick);
        if (!r) return fail("HF_BAD_LANDING");
        if (r->landingTicks < 0) return pending();
        if (r->rollStartTicks < 0) return fail("HF_NO_ROLL");
        if (r->upDotNAtRollStart > 0.3f) return fail("HF_EARLY_ROLL");
        if (!r->landedOnWheels) return fail("HF_BAD_LANDING");
        const float strength = r->cancelSamples > 0 ? r->cancelStrength / static_cast<float>(r->cancelSamples) : 0.f;
        if (strength < strengthMin) return fail("HF_WEAK_CANCEL");
        const float change = planarAngleDeg(r->fwdAtLanding, r->fwdAtDodge, r->landingNormal);
        return std::fabs(180.f - change) <= headingTol ? done() : fail("HF_HEADING_OFF");
    }));
    v.push_back(makeStep("hf.touch", "Fonce sur la balle", "DRILL_NO_TOUCH", [live, ttbMax](const StepInput&) {
        const FullLive& l = *live;
        if (l.touchTime < 0.f) return pending();
        return l.touchTime - l.goTime <= ttbMax ? done() : fail("HF_SLOW_TO_BALL");
    }, 2.5f));
    return v;
}

// ---- Flip cancel: speed, jump, front flip, cancel, landing ----------------------------------------------------
std::vector<Step> flipcancel(float level, const LivePtr& live) {
    auto L = [level](float a, float b) { return levelLerp(level, a, b); };
    struct S {
        int64_t dodgeTick = -1;
    };
    auto st = std::make_shared<S>();
    const float cancelMax = L(20.f, 6.f), strengthMin = L(0.5f, 0.9f);
    std::vector<Step> v;
    v.push_back(makeStep("fc.speed", "Roule tout droit", "DRILL_NO_SPEED", [live](const StepInput&) {
        const FullLive& l = *live;
        if (l.s.car.jumped) return fail("DRILL_NO_SPEED");
        return fwdSpeed(l.s.car) >= 800.f ? done() : pending();
    }, 3.f));
    v.push_back(makeStep("fc.jump", "Petit saut", "FC_NO_FLIP", [live](const StepInput&) {
        return live->motion().phaseJumpTick >= 0 ? done() : pending();
    }, 3.f));
    v.push_back(makeStep("fc.flip", "Flip avant", "FC_NO_FLIP", [live, st](const StepInput&) {
        const FullLive& l = *live;
        if (const DodgeRecord* r = l.dodgeNow()) {
            st->dodgeTick = r->tick;
            if (r->dir.x < 0.7f) return fail("FC_WRONG_FLIP");
            return r->ticksSinceFirstJump <= 30 ? done() : fail("FC_JUMP_LONG");
        }
        const MotionTracker& mt = l.motion();
        if (l.doubleJumpedNow() || l.landedNow()) return fail("FC_NO_FLIP");
        if (mt.phaseJumpTick >= 0 && l.s.frame - mt.phaseJumpTick > 30) return fail("FC_JUMP_LONG");
        return pending();
    }));
    v.push_back(makeStep("fc.cancel", "Stick vers toi tout de suite", "FC_NO_CANCEL", [live, st, cancelMax, strengthMin](const StepInput&) {
        const FullLive& l = *live;
        const DodgeRecord* r = l.dodge(st->dodgeTick);
        if (!r) return fail("FC_NO_CANCEL");
        if (r->cancelTicks < 0) return static_cast<float>(l.s.frame - r->tick) > cancelMax ? fail("FC_NO_CANCEL") : pending();
        if (static_cast<float>(r->cancelTicks) > cancelMax) return fail("FC_LATE");
        // Strength: mean opposing stick over the 24 ticks after the cancel (or until the landing).
        if (r->cancelSamples < 24 && r->landingTicks < 0) return pending();
        const float strength = r->cancelSamples > 0 ? r->cancelStrength / static_cast<float>(r->cancelSamples) : 0.f;
        return strength >= strengthMin ? done() : fail("FC_WEAK");
    }));
    v.push_back(makeStep("fc.land", "Roues au sol", "FC_BAD_LANDING", [live, st](const StepInput&) {
        const FullLive& l = *live;
        const DodgeRecord* r = l.dodge(st->dodgeTick);
        if (!r) return fail("FC_BAD_LANDING");
        if (r->landingTicks < 0) return pending();
        return r->landedOnWheels ? done() : fail("FC_BAD_LANDING");
    }));
    return v;
}

// ---- Wavedash from the floor: hop, nose up, dash at the floor, speed gain -----------------------------------
std::vector<Step> wavedash(float level, const LivePtr& live) {
    auto L = [level](float a, float b) { return levelLerp(level, a, b); };
    struct S {
        int64_t dodgeTick = -1;
    };
    auto st = std::make_shared<S>();
    const float apexMax = L(150.f, 90.f), noseLo = L(0.f, 8.f), noseHi = L(40.f, 25.f), contactMax = L(16.f, 10.f);
    const float gainMin = L(0.25f, 0.5f), speedUp = L(150.f, 400.f);
    std::vector<Step> v;
    v.push_back(makeStep("wd.hop", "Petit saut", "WD_NO_HOP", [live, apexMax](const StepInput&) {
        const FullLive& l = *live;
        const MotionTracker& mt = l.motion();
        if (mt.phaseJumpTick < 0) return pending();
        const int64_t hold = mt.firstJumpHoldTicks();
        if (hold < 0) return l.s.frame - mt.phaseJumpPressTick > 12 ? fail("WD_HIGH_HOP") : pending();
        if (hold > 12 || mt.phaseMaxHeight > apexMax) return fail("WD_HIGH_HOP");
        return (l.s.car.vel.z <= 0.f || l.dodgeNow()) ? done() : pending();  // decided at the top of the hop
    }, 2.f));
    v.push_back(makeStep("wd.nose", "Lève un peu le nez", "WD_NO_FLIP", [live, st, noseLo, noseHi](const StepInput&) {
        const FullLive& l = *live;
        if (const DodgeRecord* r = l.dodgeNow()) {
            st->dodgeTick = r->tick;
            if (r->pitchAtDodgeDeg < noseLo) return fail("WD_NOSE_DOWN");
            return r->pitchAtDodgeDeg <= noseHi ? done() : fail("WD_NOSE_UP");
        }
        if (l.landedNow() || l.doubleJumpedNow()) return fail("WD_NO_FLIP");
        return pending();
    }));
    v.push_back(makeStep("wd.dash", "Au ras du sol : flip avant", "WD_NO_FLIP", [live, st, contactMax](const StepInput&) {
        const FullLive& l = *live;
        const DodgeRecord* r = l.dodge(st->dodgeTick);
        if (!r) return fail("WD_TOO_EARLY");
        if (std::fabs(r->angleDeg) > 30.f) return fail("WD_NOT_FORWARD");
        if (r->firstContactTicks < 0)
            return static_cast<float>(l.s.frame - r->tick) > contactMax ? fail("WD_TOO_EARLY") : pending();
        if (static_cast<float>(r->firstContactTicks) > contactMax) return fail("WD_TOO_EARLY");
        if (r->landingTicks < 0) return l.s.frame - r->tick > 20 ? fail("WD_TOO_EARLY") : pending();
        return r->landingTicks <= 20 ? done() : fail("WD_TOO_EARLY");
    }));
    v.push_back(makeStep("wd.gain", "Garde l'accélérateur", "WD_LOW_GAIN", [live, st, gainMin, speedUp](const StepInput&) {
        const FullLive& l = *live;
        const MechGrade* g = l.gradeSince(Mech::Wavedash, st->dodgeTick);
        if (!g || g->dodgeTick != st->dodgeTick) return pending();
        if (g->hasError("WD_CRASH_LANDING")) return fail("WD_CRASH_LANDING");
        if (g->metric("gainRatio", 0.f) < gainMin) return fail("WD_LOW_GAIN");
        if (g->metric("speedAfter", 0.f) - g->metric("speedBefore", 0.f) < speedUp) return fail("WD_LOW_GAIN");
        return done();
    }, 1.f));
    return v;
}

// ---- Chain wavedash from a standstill --------------------------------------------------------------------------
std::vector<Step> chaindash(float level, const LivePtr& live) {
    auto L = [level](float a, float b) { return levelLerp(level, a, b); };
    struct S {
        int dashes = 0;
        int64_t lastEnd = -1;
        size_t seen = 0;           // grades already counted
        int ticks = 0, slide = 0;  // since the first dash: ticks, ticks with the powerslide held
        float maxSpeed = 0.f;
    };
    auto st = std::make_shared<S>();
    const float gapMax = L(24.f, 10.f), slideMin = L(0.3f, 0.5f), speedMin = L(1700.f, 2200.f);
    const int dashesMin = static_cast<int>(std::lround(L(2.f, 4.f)));
    // Counts the new dashes of this tick. Returns a failure code, or nullptr.
    auto scan = [live, st, gapMax]() -> const char* {
        const FullLive& l = *live;
        if (l.s.in.boost) return "CHAIN_BOOST_USED";
        if (st->dashes > 0) {
            st->ticks++;
            if (l.s.in.handbrake) st->slide++;
            st->maxSpeed = std::max(st->maxSpeed, length(l.s.car.vel));
        }
        for (; st->seen < l.grades.size(); ++st->seen) {
            const MechGrade& g = l.grades[st->seen];
            if (g.mech != Mech::Wavedash) continue;
            if (!isDashGrade(g)) return g.topError() ? g.topError() : "WD_LOW_GAIN";
            if (g.hasError("WD_CRASH_LANDING")) return "WD_CRASH_LANDING";
            if (st->dashes > 0 && static_cast<float>(g.tickStart - st->lastEnd) > gapMax) return "CHAIN_GROUND_TIME";
            st->dashes++;
            st->lastEnd = g.tickEnd;
            st->maxSpeed = std::max(st->maxSpeed, g.metric("speedAfter", 0.f));
        }
        // On the ground too long after the last dash: the chain is broken.
        if (st->dashes > 0 && grounded(l.s.car) && static_cast<float>(l.s.frame - st->lastEnd) > gapMax + 10.f &&
            l.motion().takeoffTick <= st->lastEnd)
            return "CHAIN_GROUND_TIME";
        return nullptr;
    };
    std::vector<Step> v;
    v.push_back(makeStep("cd.speed", "Accélère sans boost", "DRILL_NO_SPEED", [live](const StepInput&) {
        const FullLive& l = *live;
        if (l.s.in.boost) return fail("CHAIN_BOOST_USED");
        return length2D(l.s.car.vel) >= 600.f ? done() : pending();
    }, 3.f));
    v.push_back(makeStep("cd.first", "Petit saut, flip avant au ras du sol", "WD_NO_FLIP", [scan, st](const StepInput&) {
        if (const char* code = scan()) return fail(code);
        return st->dashes >= 1 ? done() : pending();
    }));
    v.push_back(makeStep("cd.rhythm", "Powerslide gardé, ressaute tout de suite", "CHAIN_GROUND_TIME", [scan, st, slideMin](const StepInput&) {
        if (const char* code = scan()) return fail(code);
        if (st->dashes < 2) return pending();
        const float ratio = st->ticks > 0 ? static_cast<float>(st->slide) / static_cast<float>(st->ticks) : 1.f;
        return ratio >= slideMin ? done() : fail("CHAIN_NO_POWERSLIDE");
    }));
    v.push_back(makeStep("cd.speedup", "Enchaîne jusqu'à la vitesse max", "CHAIN_SLOW", [scan, st, dashesMin, speedMin](const StepInput&) {
        if (const char* code = scan()) return fail(code);
        return (st->dashes >= dashesMin && st->maxSpeed >= speedMin) ? done() : pending();
    }));
    return v;
}

// ---- Zap dash from the floor: hop, nose down, bounce, flip ----------------------------------------------------
std::vector<Step> zapdash(float level, const LivePtr& live) {
    auto L = [level](float a, float b) { return levelLerp(level, a, b); };
    struct S {
        int64_t landTick = -1, dodgeTick = -1;
        bool bounced = false;
    };
    auto st = std::make_shared<S>();
    const float gainMin = L(0.25f, 0.5f);
    std::vector<Step> v;
    v.push_back(makeStep("zap.hop", "Petit saut", "WD_NO_HOP", [live](const StepInput&) {
        const FullLive& l = *live;
        const MotionTracker& mt = l.motion();
        if (mt.phaseJumpTick < 0) return pending();
        const int64_t hold = mt.firstJumpHoldTicks();
        if (hold < 0) return l.s.frame - mt.phaseJumpPressTick > 12 ? fail("WD_HIGH_HOP") : pending();
        if (hold > 12 || mt.phaseMaxHeight > 150.f) return fail("WD_HIGH_HOP");
        if (l.s.car.vel.z > 0.f) return pending();
        return mt.phaseMaxHeight >= 50.f ? done() : fail("WD_NO_HOP");
    }, 2.f));
    v.push_back(makeStep("zap.nose", "Nez un peu vers le bas", "ZAP_NOSE", [live](const StepInput&) {
        const FullLive& l = *live;
        if (l.dodgeNow()) return fail("ZAP_NO_BOUNCE");
        if (!(l.s.car.wheelContacts > 0 && l.p.car.wheelContacts == 0)) return pending();
        const float nose = noseDegOn(l.p.car, {0.f, 0.f, 1.f});  // attitude just before the first contact
        return (nose >= -20.f && nose <= -3.f) ? done() : fail("ZAP_NOSE");
    }));
    v.push_back(makeStep("zap.bounce", "Rebond", "ZAP_LATE", [live, st](const StepInput&) {
        const FullLive& l = *live;
        if (l.dodgeNow()) return fail("ZAP_NO_BOUNCE");
        if (l.landedNow()) st->landTick = l.s.frame;
        if (st->landTick < 0) return pending();
        if (l.s.car.jumped && !l.p.car.jumped) return fail("WD_LATE_JUMPED");
        // The suspension bounce: under 3 wheels again within 20 ticks of the landing, without a jump (UNVERIFIED).
        if (!grounded(l.s.car)) return done();
        return l.s.frame - st->landTick > WavedashDetector::kZapMaxBounceTicks ? fail("ZAP_LATE") : pending();
    }));
    v.push_back(makeStep("zap.flip", "Au rebond : flip avant", "ZAP_NO_FLIP", [live, st](const StepInput&) {
        const FullLive& l = *live;
        if (const DodgeRecord* r = l.dodgeNow()) {
            st->dodgeTick = r->tick;
            if (r->phaseHadJump) return fail("WD_LATE_JUMPED");
            return std::fabs(r->angleDeg) <= 30.f ? done() : fail("WD_NOT_FORWARD");
        }
        if (l.s.car.jumped && !l.p.car.jumped) return fail("WD_LATE_JUMPED");
        if (grounded(l.s.car) && st->landTick >= 0 && l.s.frame - st->landTick > WavedashDetector::kZapMaxBounceTicks) return fail("ZAP_LATE");
        return pending();
    }));
    v.push_back(makeStep("zap.gain", "Garde l'accélérateur", "WD_LOW_GAIN", [live, st, gainMin](const StepInput&) {
        const FullLive& l = *live;
        const MechGrade* g = l.gradeSince(Mech::ZapDash, st->dodgeTick);
        if (!g || g->dodgeTick != st->dodgeTick) {
            if (const MechGrade* w = l.gradeSince(Mech::Wavedash, st->dodgeTick); w && w->dodgeTick == st->dodgeTick)
                return fail("ZAP_NO_BOUNCE");
            return pending();
        }
        if (g->hasError("WD_CRASH_LANDING")) return fail("WD_CRASH_LANDING");
        return g->metric("gainRatio", 0.f) >= gainMin ? done() : fail("WD_LOW_GAIN");
    }, 1.f));
    return v;
}

// ---- Wall dash from the floor: climb, ride, dash, chain ------------------------------------------------------
std::vector<Step> walldash(float level, const DrillSetup& d, const LivePtr& live) {
    auto L = [level](float a, float b) { return levelLerp(level, a, b); };
    struct S {
        int dashes = 0;
        size_t seen = 0;
    };
    auto st = std::make_shared<S>();
    const float tapMax = L(30.f, 11.f);
    const int needed = std::max(1, d.runDashes);
    const float sx = static_cast<float>(d.side);  // +1: wall x = -4096 (normal +X in blue axes)
    auto onWall = [live, sx]() {
        const FullLive& l = *live;
        return grounded(l.s.car) && l.blue(contactNormal(l.s.car)).x * sx >= 0.7f;
    };
    // Counts the new wall dashes. Returns a failure code, or nullptr.
    auto scan = [live, st, tapMax]() -> const char* {
        const FullLive& l = *live;
        for (; st->seen < l.grades.size(); ++st->seen) {
            const MechGrade& g = l.grades[st->seen];
            if (g.mech != Mech::WallDash) continue;
            if (g.hasError("WALL_FELL_OFF")) return "WALL_FELL_OFF";
            if (g.metric("jumpToDodgeTicks", 0.f) > tapMax) return "WALL_TAP_SLOW";
            st->dashes++;
        }
        return nullptr;
    };
    std::vector<Step> v;
    v.push_back(makeStep("wall.climb", "Monte sur le mur", "WALL_NO_CLIMB", [live, onWall](const StepInput&) {
        return (onWall() && live->s.car.pos.z >= 250.f) ? done() : pending();
    }, 3.f));
    Step ride = makeStep("wall.ride", "Longe le mur sans monter ni descendre", "WALL_NOT_RIDING", [live, onWall](const StepInput&) {
        const FullLive& l = *live;
        const Vec3 vb = l.blue(l.s.car.vel);
        return (onWall() && vb.y >= 500.f && std::fabs(vb.z) <= 300.f) ? done() : pending();
    }, 3.f);
    ride.hold = 0.25f;
    ride.holdGrace = 0.05f;
    v.push_back(std::move(ride));
    v.push_back(makeStep("wall.dash", "Tape saut, flip vers l'avant côté mur", "WALL_NO_DASH", [scan, st](const StepInput&) {
        if (const char* code = scan()) return fail(code);
        return st->dashes >= 1 ? done() : pending();
    }, 3.f));
    v.push_back(makeStep("wall.chain", "Recommence dès que les roues retouchent", "WALL_FELL_OFF", [live, scan, st, needed](const StepInput&) {
        if (const char* code = scan()) return fail(code);
        if (st->dashes >= needed) return done();
        const FullLive& l = *live;
        const bool floor = grounded(l.s.car) && contactNormal(l.s.car).z > 0.7f;
        return (floor || l.s.car.pos.z < 150.f) ? fail("WALL_FELL_OFF") : pending();
    }));
    return v;
}

// ---- Fast aerial under a lob ------------------------------------------------------------------------------------
std::vector<Step> fastaerial(float level, const LivePtr& live) {
    auto L = [level](float a, float b) { return levelLerp(level, a, b); };
    struct S {
        int64_t jumpTick = -1, releaseTick = -1, boostTick = -1;
        float pitchSum = 0.f;
        int pitchSamples = 0;
    };
    auto st = std::make_shared<S>();
    const float holdMin = L(8.f, 20.f), gapMax = L(30.f, 12.f), touchZ = L(400.f, 900.f);
    std::vector<Step> v;
    Step place = makeStep("fa.place", "Place-toi sous la balle", "FA_NOT_PLACED", [live](const StepInput&) {
        const FullLive& l = *live;
        if (!l.jumpedNow()) return pending();
        return std::fabs(l.blue(l.s.car.pos).x - l.blue(l.s.ball.pos).x) <= 300.f ? done() : fail("FA_NOT_PLACED");
    });
    place.optional = displayLevel(level) <= 1;
    v.push_back(std::move(place));
    v.push_back(makeStep("fa.takeoff", "Saut tenu, boost, stick vers toi", "FA_NO_JUMP", [live, st, holdMin](const StepInput&) {
        const FullLive& l = *live;
        const MotionTracker& mt = l.motion();
        if (mt.phaseJumpTick < 0 && st->jumpTick < 0) return pending();
        if (st->jumpTick < 0) st->jumpTick = mt.phaseJumpTick;
        if (st->boostTick < 0 && l.s.in.boost) st->boostTick = l.s.frame;
        if (l.s.in.jump) {
            st->pitchSum += clamp01(l.s.in.pitch);
            st->pitchSamples++;
            return pending();
        }
        st->releaseTick = l.s.frame;
        const int64_t hold = mt.firstJumpHoldTicks() >= 0 ? mt.firstJumpHoldTicks() : l.s.frame - mt.phaseJumpPressTick;
        if (st->boostTick < 0 || st->boostTick - st->jumpTick > 6) return fail("FA_BOOST_LATE");
        if (st->pitchSamples > 0 && st->pitchSum / static_cast<float>(st->pitchSamples) < 0.3f) return fail("FA_NO_TILT");
        return static_cast<float>(hold) >= holdMin ? done() : fail("FA_HOLD_SHORT");
    }, 4.f));
    v.push_back(makeStep("fa.dj", "Lâche, rappuie, stick au centre", "FA_NO_DOUBLE_JUMP", [live, st, gapMax](const StepInput&) {
        const FullLive& l = *live;
        if (l.dodgeNow()) return fail("FA_ACCIDENTAL_DODGE");
        if (l.doubleJumpedNow()) return static_cast<float>(l.s.frame - st->releaseTick) <= gapMax ? done() : fail("FA_GAP_SLOW");
        if (l.landedNow()) return fail("FA_NO_DOUBLE_JUMP");
        return static_cast<float>(l.s.frame - st->releaseTick) > gapMax ? fail("FA_GAP_SLOW") : pending();
    }));
    v.push_back(makeStep("fa.touch", "Vise la balle et touche-la", "DRILL_NO_TOUCH", [live, touchZ](const StepInput&) {
        const FullLive& l = *live;
        if (l.touchTime < 0.f) return l.landedNow() ? fail("DRILL_NO_TOUCH") : pending();
        return l.touchBallZ >= touchZ ? done() : fail("FA_TOUCH_LOW");
    }));
    return v;
}

// ---- Stall from the floor -------------------------------------------------------------------------------------
std::vector<Step> stall(float level, const LivePtr& live) {
    auto L = [level](float a, float b) { return levelLerp(level, a, b); };
    struct S {
        int64_t dodgeTick = -1;
    };
    auto st = std::make_shared<S>();
    const float hoverMin = L(8.f, 40.f);
    auto common = [live]() -> const char* {
        const FullLive& l = *live;
        if (l.s.car.doubleJumped) return "ST_DOUBLE_JUMP";
        if (const MechGrade* g = l.gradeSince(Mech::Stall, l.goFrame); g && g->hasError("ST_NO_FLIP")) return "ST_NO_FLIP";
        if (l.landedNow()) return "ST_NO_STALL";
        return nullptr;
    };
    std::vector<Step> v;
    v.push_back(makeStep("st.jump", "Saute une seule fois, saut tenu", "ST_NO_STALL", [live](const StepInput&) {
        return live->motion().phaseJumpTick >= 0 ? done() : pending();
    }, 2.f));
    v.push_back(makeStep("st.inputs", "Stick d'un côté, air roll de l'autre", "ST_NO_STALL", [live, common](const StepInput&) {
        const FullLive& l = *live;
        if (const char* code = common()) return fail(code);
        if (l.dodgeNow()) return fail("ST_YAW_ROLL_MISMATCH");
        const Inputs& in = l.s.in;
        const bool ok = in.yaw * in.roll < 0.f && std::fabs(in.yaw) >= 0.4f && std::fabs(in.roll) >= 0.4f && std::fabs(in.pitch) < 0.1f;
        return ok ? done() : pending();
    }));
    v.push_back(makeStep("st.press", "Saut, avant que le flip disparaisse", "ST_NO_STALL", [live, st, common](const StepInput&) {
        const FullLive& l = *live;
        if (const DodgeRecord* r = l.dodgeNow()) {
            st->dodgeTick = r->tick;
            if (r->kind != DodgeKind::Stall)
                return fail(std::fabs(r->inputAtDodge.pitch) >= 0.1f ? "ST_PITCH_LEAK" : "ST_YAW_ROLL_MISMATCH");
            return l.s.car.pos.z >= 150.f ? done() : fail("ST_TOO_LOW");
        }
        if (const char* code = common()) return fail(code);
        return pending();
    }));
    v.push_back(makeStep("st.hover", "Laisse flotter", "ST_SHORT_HOVER", [live, st, hoverMin](const StepInput&) {
        const FullLive& l = *live;
        const MechGrade* g = l.gradeSince(Mech::Stall, st->dodgeTick);
        if (!g || g->dodgeTick != st->dodgeTick) return pending();
        return g->metric("hoverTicks", 0.f) >= hoverMin ? done() : fail("ST_SHORT_HOVER");
    }));
    return v;
}

// ---- Helicopter from the floor --------------------------------------------------------------------------------
std::vector<Step> helicopter(float level, const LivePtr& live) {
    auto L = [level](float a, float b) { return levelLerp(level, a, b); };
    const float tiltMax = L(45.f, 20.f);
    std::vector<Step> v;
    v.push_back(makeStep("heli.takeoff", "Décolle", "AS_NO_TAKEOFF", [live](const StepInput&) {
        return live->s.car.pos.z >= 300.f ? done() : pending();
    }, 3.f));
    Step spin = makeStep("heli.spin", "Air roll et stick du même côté, garde-les", "AS_NO_SPIN", [live](const StepInput&) {
        const FullLive& l = *live;
        if (l.landedNow() || l.s.car.wheelContacts > 0) return fail("AS_SHORT");
        const Inputs& in = l.s.in;
        const bool on = std::fabs(in.roll) >= 0.5f && std::fabs(in.yaw) >= 0.5f && sgn(in.roll) == sgn(in.yaw);
        return on ? done() : pending();
    });
    spin.hold = L(60.f, 150.f) * k::kDt;
    spin.holdGrace = 0.05f;
    v.push_back(std::move(spin));
    v.push_back(makeStep("heli.land", "Lâche avant le sol, roues au sol", "RC_CRASHED", [live, tiltMax](const StepInput&) {
        const FullLive& l = *live;
        if (const MechGrade* g = l.gradeSince(Mech::Recovery, l.goFrame); g && g->hasError("RC_CRASHED")) return fail("RC_CRASHED");
        if (!(l.s.car.wheelContacts > 0 && l.p.car.wheelContacts == 0)) return pending();
        const float tilt = safeAcosDeg(dot(l.s.car.basis.up, contactNormal(l.s.car)));
        return tilt <= tiltMax ? done() : fail(tilt > 90.f ? "RC_ON_ROOF" : "RC_TILTED");
    }));
    return v;
}

// ---- Recovery after a jump off the side wall ------------------------------------------------------------------
std::vector<Step> recovery(float level, const DrillSetup& d, const LivePtr& live) {
    auto L = [level](float a, float b) { return levelLerp(level, a, b); };
    const float sx = static_cast<float>(d.side);
    const float climbZ = L(400.f, 700.f), tiltMax = L(45.f, 15.f), alignMax = L(60.f, 30.f), keepMin = L(0.4f, 0.85f);
    auto onWall = [live, sx](const CarState& c) {
        return c.wheelContacts > 0 && live->blue(contactNormal(c)).x * sx >= 0.7f;
    };
    std::vector<Step> v;
    v.push_back(makeStep("rc.climb", "Monte sur le mur", "RC_NO_CLIMB", [live, onWall, climbZ](const StepInput&) {
        const FullLive& l = *live;
        return (grounded(l.s.car) && onWall(l.s.car) && l.s.car.pos.z >= climbZ) ? done() : pending();
    }, 4.f));
    v.push_back(makeStep("rc.jump", "Saute du mur", "RC_NO_WALL_JUMP", [live, onWall](const StepInput&) {
        const FullLive& l = *live;
        if (l.jumpedNow()) return onWall(l.p.car) ? done() : fail("RC_NO_WALL_JUMP");
        if (l.s.car.wheelContacts == 0 && !l.s.car.jumped) return fail("RC_NO_WALL_JUMP");
        return pending();
    }, 3.f));
    auto airborne = std::make_shared<bool>(false);
    Step air = makeStep("rc.air", "En l'air", "RC_SHORT_AIR", [live, airborne](const StepInput&) {
        const FullLive& l = *live;
        if (l.s.car.wheelContacts == 0) {
            *airborne = true;
            return done();  // held for half a second
        }
        return *airborne ? fail("RC_SHORT_AIR") : pending();  // still leaving the wall on the jump tick
    });
    air.hold = 0.5f;
    v.push_back(std::move(air));
    v.push_back(makeStep("rc.land", "Retombe sur les roues, nez devant", "RC_TILTED", [live, tiltMax, alignMax, keepMin](const StepInput&) {
        const FullLive& l = *live;
        const MechGrade* g = l.gradeSince(Mech::Recovery, l.goFrame);
        if (!g) return pending();
        if (g->hasError("RC_CRASHED")) return fail("RC_CRASHED");
        if (g->hasError("RC_ON_ROOF")) return fail("RC_ON_ROOF");
        if (g->metric("tiltAtTouchDeg", 0.f) > tiltMax) return fail("RC_TILTED");
        if (g->metric("velocityAlignDeg", 0.f) > alignMax) return fail("RC_MISALIGNED");
        return g->metric("speedRetained", 0.f) >= keepMin ? done() : fail("RC_SPEED_LOST");
    }));
    return v;
}

// ---- Powerslide toward the ball from a standstill ------------------------------------------------------------
std::vector<Step> powerslide(float level, const DrillSetup& d, const LivePtr& live) {
    auto L = [level](float a, float b) { return levelLerp(level, a, b); };
    struct S {
        int slideTicks = 0;
        float turn = 0.f;            // signed yaw accumulated while sliding (deg)
        float ballSide = 0.f;        // sign of the yaw from the nose to the ball when the slide started
        Vec3 lastFwd;
        int64_t startTick = -1, releaseTick = -1;
    };
    auto st = std::make_shared<S>();
    const float aimMax = L(25.f, 10.f), keepMin = L(0.5f, 0.85f);
    std::vector<Step> v;
    v.push_back(makeStep("ps.speed", "Accélère", "DRILL_NO_SPEED", [live](const StepInput&) {
        return fwdSpeed(live->s.car) >= 900.f ? done() : pending();
    }, 3.f));
    v.push_back(makeStep("ps.slide", "Tourne et powerslide court", "PS_NO_SLIDE", [live, st](const StepInput&) {
        const FullLive& l = *live;
        const bool sliding = l.s.in.handbrake && grounded(l.s.car);
        if (sliding) {
            if (st->slideTicks == 0) {
                st->turn = 0.f;
                st->startTick = l.s.frame;
                st->lastFwd = l.s.car.basis.fwd;
                st->ballSide = sgn(signedYawDeg(l.s.car.basis.fwd, l.s.ball.pos - l.s.car.pos));
            }
            st->turn += signedYawDeg(st->lastFwd, l.s.car.basis.fwd);
            st->lastFwd = l.s.car.basis.fwd;
            st->slideTicks++;
            return pending();
        }
        if (st->slideTicks == 0) return pending();
        const int ticks = st->slideTicks;
        st->slideTicks = 0;
        if (ticks < 4 || std::fabs(st->turn) < 20.f) return pending();  // a tap: not the slide yet
        // Toward the ball: the slide turned the nose to the side the ball was on.
        st->releaseTick = l.s.frame;
        return sgn(st->turn) == st->ballSide ? done() : fail("PS_NOT_AIMED");
    }, 3.f));
    v.push_back(makeStep("ps.aim", "Relâche, nez sur la balle", "PS_NOT_AIMED", [live, st, aimMax](const StepInput&) {
        const FullLive& l = *live;
        if (l.s.frame - st->releaseTick < 30) return pending();  // measured 0.25 s after the release
        const float err = planarAngleDeg(l.s.car.basis.fwd, l.s.ball.pos - l.s.car.pos, {0.f, 0.f, 1.f});
        return err <= aimMax ? done() : fail("PS_NOT_AIMED");
    }));
    v.push_back(makeStep("ps.keep", "Garde ta vitesse", "PS_SPEED_LOSS", [live, st, keepMin](const StepInput&) {
        const FullLive& l = *live;
        const MechGrade* g = l.gradeSince(Mech::Powerslide, st->startTick);
        if (!g) return pending();
        return g->metric("speedRetained", 0.f) >= keepMin ? done() : fail("PS_SPEED_LOSS");
    }, 1.f));
    v.push_back(makeStep("ps.touch", "Touche la balle", "DRILL_NO_TOUCH", [live](const StepInput&) {
        return live->touchTime >= 0.f ? done() : pending();
    }, 2.f));
    (void)d;
    return v;
}

// ---- Boost from a standstill ----------------------------------------------------------------------------------
std::vector<Step> boost(float level, const LivePtr& live) {
    auto L = [level](float a, float b) { return levelLerp(level, a, b); };
    struct S {
        int earlyBoost = 0;      // boost ticks under 1300 uu/s
        int wasted = 0;          // boost ticks at max speed (or over 2150 after the release)
        int keepTicks = 0, keepSuper = 0;
    };
    auto st = std::make_shared<S>();
    const float wasteMax = L(5.f, 1.5f);
    auto speed = [live]() {
        const CarState& c = live->s.car;
        return grounded(c) ? length(tangent(c.vel, contactNormal(c))) : length(c.vel);
    };
    std::vector<Step> v;
    v.push_back(makeStep("bo.throttle", "Accélérateur seul au début", "DRILL_NO_SPEED", [live, st, speed](const StepInput&) {
        const FullLive& l = *live;
        if (l.s.in.boost && speed() < 1300.f && ++st->earlyBoost > 12) return fail("SM_EARLY_BOOST");
        return speed() >= 1400.f ? done() : pending();
    }, 4.f));
    v.push_back(makeStep("bo.super", "Boost jusqu'au max", "SM_SLOW_TO_SUPERSONIC", [live, speed](const StepInput&) {
        return (live->s.car.supersonic || speed() >= k::kSupersonicStartSpeed) ? done() : pending();
    }, 3.f));
    v.push_back(makeStep("bo.release", "Lâche le boost au max", "SM_BOOST_AT_MAX", [live, st, speed, wasteMax](const StepInput&) {
        const FullLive& l = *live;
        const bool boosting = l.s.in.boost && l.s.car.boost > 0.f;
        if (boosting && speed() >= k::kCarMaxSpeed - 10.f) st->wasted++;
        if (static_cast<float>(st->wasted) * boostPerTick() > wasteMax) return fail("SM_BOOST_AT_MAX");
        return (!l.s.in.boost && speed() >= k::kSupersonicMaintainMinSpeed) ? done() : pending();
    }, 2.f));
    Step keep = makeStep("bo.keep", "Reste supersonique, accélérateur appuyé", "SM_NOT_SUPERSONIC", [live, st, speed, wasteMax](const StepInput& in) {
        const FullLive& l = *live;
        const bool boosting = l.s.in.boost && l.s.car.boost > 0.f;
        if (boosting && speed() >= 2150.f) st->wasted++;
        if (static_cast<float>(st->wasted) * boostPerTick() > wasteMax) return fail("SM_BOOST_AT_MAX");
        st->keepTicks++;
        if (l.s.car.supersonic || speed() >= k::kSupersonicStartSpeed) st->keepSuper++;
        if (in.stepTime < 3.f) return pending();
        const float ratio = static_cast<float>(st->keepSuper) / static_cast<float>(std::max(1, st->keepTicks));
        return ratio >= 0.85f ? done() : fail("SM_NOT_SUPERSONIC");
    });
    v.push_back(std::move(keep));
    return v;
}

} // namespace

std::vector<Step> fullSteps(Focus f, float level, const DrillSetup& d, const std::shared_ptr<FullLive>& live) {
    level = clamp01(level);
    switch (f) {
    case Focus::SpeedFlip: return speedflip(level, live);
    case Focus::HalfFlip: return halfflip(level, live);
    case Focus::FlipCancel: return flipcancel(level, live);
    case Focus::Wavedash: return wavedash(level, live);
    case Focus::ChainDash: return chaindash(level, live);
    case Focus::ZapDash: return zapdash(level, live);
    case Focus::WallDash: return walldash(level, d, live);
    case Focus::FastAerial: return fastaerial(level, live);
    case Focus::Stall: return stall(level, live);
    case Focus::Helicopter: return helicopter(level, live);
    case Focus::Recovery: return recovery(level, d, live);
    case Focus::Powerslide: return powerslide(level, d, live);
    case Focus::Boost: return boost(level, live);
    default: return {};
    }
}

} // namespace rlt::mech
