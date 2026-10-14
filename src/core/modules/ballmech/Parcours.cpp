#include "core/modules/ballmech/Parcours.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "core/common/Pickup.h"
#include "core/modules/ballmech/Primitives.h"

namespace rlt::bm {

namespace {

constexpr float kCarryReach = 280.f;      // ball "near the car" in the air (same reach as the flick detector)
constexpr float kTornadoDeg = 230.f;      // breezi spin (flick detector gate)
constexpr float kMustyPitchDeg = -35.f;   // musty nose-down gate

StepVerdict pending() { return StepVerdict::pending(); }
StepVerdict done() { return StepVerdict::done(); }
StepVerdict fail(const char* code) { return StepVerdict::fail(code); }

Step makeStep(const char* id, const char* label, StepCheck check, float timeout, const char* failCode) {
    Step s;
    s.id = id;
    s.label = label;
    s.check = std::move(check);
    s.timeout = timeout;
    s.failCode = failCode;
    return s;
}

Step infoStep(const char* id, const char* label, StepCheck check, float hold = 0.f) {
    Step s;
    s.id = id;
    s.label = label;
    s.check = std::move(check);
    s.optional = true;
    s.hold = hold;
    return s;
}

float pitchDeg(const CarState& c) { return std::asin(clampf(c.basis.fwd.z, -1.f, 1.f)) * kRadToDeg; }

float angleDeg(const Vec3& a, const Vec3& b) {
    const float la = length(a), lb = length(b);
    if (la < 1e-3f || lb < 1e-3f) return 180.f;
    return std::acos(clampf(dot(a, b) / (la * lb), -1.f, 1.f)) * kRadToDeg;
}

const char* flickLabel(const std::string& v) {
    if (v == "front") return "Flip avant dans la balle";
    if (v == "45") return "Huitième de tour, puis flip en diagonale";
    if (v == "90") return "Quart de tour, puis flip";
    if (v == "backflip") return "Flip arrière";
    if (v == "side") return "Flip sur le côté";
    if (v == "reverse") return "Retourne la voiture, puis flip arrière";
    return "Flippe dans la balle";
}

// The two "Ramassage" steps with the ballmech roof test and the drill's codes.
void addPickup(std::vector<Step>& out, Mech m, const DrillSetup& d) {
    pickup::Options o = d.pickupChase ? pickup::chaseOptions() : pickup::options(d.pickupRolling);
    o.onRoof = [](const Snapshot& s) { return carriedOnRoof(s); };
    // Module codes (short HUD lines in Tips.cpp); the step's own tip text stays the helper's (rolling or resting ball).
    o.roofCode = m == Mech::AirDribble ? "AD_NO_CATCH" : "PU_NO_ROOF";
    o.settleCode = "PU_UNSTABLE";
    o.dropCode = "PU_DROPPED";
    if (m != Mech::AirDribble) o.jumpCode = "FL_JUMP_UNSETTLED";  // jumping before the ball is stable fails the pickup
    out.push_back(pickup::roofStep(o));
    out.push_back(pickup::settleStep(o));
}

// "Saute" after the pickup: the car left the ground with the ball carried (flick detector's Air state).
Step carriedJumpStep(ParcoursState* p) {
    return makeStep("fl.jump", "Saute", [p](const StepInput&) { return p->airPhase ? done() : pending(); }, 5.f,
                    "FL_STEP_JUMP");
}

} // namespace

bool carriedOnRoof(const Snapshot& s) {
    Vec3 L;
    if (!carryGeometry(s, &L)) return false;
    return L.z >= roofBallHeight(s.car, s.ball.radius) - 25.f;
}

void parcoursBegin(ParcoursState& st, Mech m, int niveau, const DrillSetup& d, int team, bool standardArena) {
    st = ParcoursState{};
    st.mech = m;
    st.niveau = niveau;
    st.side = d.side;
    st.team = team;
    st.sign = attackSign(team);
    st.target = d.target;
    st.standardArena = standardArena;
}

void parcoursTick(ParcoursState& st, const Snapshot& s, bool touchEvent, bool resetEvent) {
    const CarState& c = s.car;
    const BallState& b = s.ball;
    const float now = s.time;
    const float dt = st.hasPrev && now > st.prev.time ? std::min(now - st.prev.time, 0.05f) : kTickDt;
    st.worldWheels = worldWheelContacts(s, st.standardArena);
    st.ballWheels = ballWheelContacts(s, st.standardArena);
    st.jumpEdge = s.in.jump && (!st.hasPrev || !st.prev.in.jump);
    st.jumpedEdge = c.jumped && st.hasPrev && !st.prev.car.jumped;
    st.touch = touchEvent || (st.hasPrev && c.lastBallTouchFrame > 0 && c.lastBallTouchFrame != st.prev.car.lastBallTouchFrame);
    if (resetEvent || (st.ballWheels >= 3 && st.worldWheels == 0 && c.pos.z > 120.f)) st.resetSeen = true;

    const bool grounded = st.worldWheels >= 3;
    // Carried ball on the ground; a jump with it starts the air phase (flick detector: >= 0.10 s of carry).
    if (grounded && carriedOnRoof(s)) {
        st.carryTime += dt;
        st.lastCarry = now;
    } else if (grounded && now - st.lastCarry > 0.12f) {
        st.carryTime = 0.f;
    }
    if (!grounded && st.wasGrounded && st.carryTime >= 0.10f && st.worldWheels == 0) {
        st.airPhase = true;
        st.landedAfterJump = false;
        st.jumpTime = now;
        st.jumpHeld = 0.f;
        st.jumpReleased = false;
        st.ballZAtJump = b.pos.z;
        st.rollAccum = 0.f;
        st.airTicks = st.airRollTicks = st.keptTicks = 0;
    }
    if (st.airPhase) {
        if (grounded && now - st.jumpTime > 0.1f) {
            st.airPhase = false;
            st.landedAfterJump = true;
        } else {
            ++st.airTicks;
            st.rollAccum += dot(c.angVel, c.basis.fwd) * dt;
            if (std::fabs(s.in.roll) >= 0.5f) ++st.airRollTicks;
            if (distance(b.pos, c.pos) <= kCarryReach) ++st.keptTicks;
            if (!s.in.jump) st.jumpReleased = true;
            if (!st.jumpReleased) st.jumpHeld += dt;
        }
    }

    // Free flight.
    st.takeoffNow = false;
    if (st.worldWheels == 0) {
        if (st.noWheelSince < 0.f) st.noWheelSince = now;
        if (st.wasGrounded && (c.jumped || s.in.jump)) {
            st.takeoffNow = true;
            st.takeoffTime = now;
        }
    } else {
        st.noWheelSince = -1.f;
    }
    if (grounded) st.wasGrounded = true;
    else if (st.worldWheels == 0) st.wasGrounded = false;

    // Double tap: where an air touch sends the ball (evaluated 2 ticks later, when its new velocity is known), and
    // the bounce off the attacked backboard.
    if (st.mech == Mech::DoubleTap) {
        if (st.touch && st.worldWheels == 0) st.dtEvalFrame = s.frame + 2;
        if (st.dtEvalFrame >= 0 && s.frame >= st.dtEvalFrame) {
            st.dtEvalFrame = -1;
            const GoalProjection g = projectToGoal(b.pos, b.vel, st.team, b.radius, st.standardArena);
            if (g.reaches && !g.onTarget && g.z >= 750.f) st.dtShotHigh = true;
        }
        st.bounceNow = st.hasPrev && st.prev.ball.vel.y * st.sign > 0.f && b.vel.y * st.sign <= 0.f &&
                       b.pos.y * st.sign > 4600.f;
    }

    st.prev = s;
    st.hasPrev = true;
}

std::vector<Step> parcoursSteps(Mech m, int n, const DrillSetup& d, ParcoursState& st) {
    ParcoursState* p = &st;
    std::vector<Step> out;
    switch (m) {
    case Mech::Ceiling: {
        const float side = d.side;
        out.push_back(makeStep("ceil.wall", "Va au mur de côté, boost",
                               [p, side](const StepInput& in) {
                                   const CarState& c = in.snap.car;
                                   return p->worldWheels >= 3 && std::fabs(c.pos.x) > 3700.f && c.basis.up.x * side < -0.7f
                                              ? done()
                                              : pending();
                               },
                               n <= 3 ? 6.f : 4.f, "CEIL_STEP_WALL"));
        out.push_back(makeStep("ceil.ceiling", "Monte au plafond, lâche le boost à mi-mur",
                               [p](const StepInput& in) {
                                   const CarState& c = in.snap.car;
                                   if (p->noWheelSince >= 0.f && in.now - p->noWheelSince >= 0.3f && c.pos.z < 1800.f)
                                       return fail("CEIL_STEP_FELL_WALL");
                                   if (p->worldWheels >= 3 && arena::kCeilingZ - c.pos.z <= 90.f && c.basis.up.z <= -0.72f) {
                                       p->tooFast = length(c.vel) > 1800.f;
                                       return done();
                                   }
                                   return pending();
                               },
                               4.f, "CEIL_STEP_CEILING"));
        // Leaving the ceiling: no jump on the way out (the flip given back by the ceiling is kept), still free 0.3 s
        // later with the flip available. The ceiling shot detector applies the same 0.15 s jump rule.
        out.push_back(makeStep("ceil.drop", "Lâche-toi, sans sauter",
                               [p](const StepInput& in) {
                                   const Snapshot& s = in.snap;
                                   if (p->worldWheels > 0) {
                                       if (p->worldWheels >= 3) p->leaveTime = -1.f;
                                       return pending();
                                   }
                                   if (p->leaveTime < 0.f) {
                                       p->leaveTime = in.now;
                                       if (s.in.jump || (p->hasPrev && p->prev.in.jump)) return fail("CEIL_JUMPED_OFF");
                                   }
                                   const float since = in.now - p->leaveTime;
                                   if (since <= 0.15f && (p->jumpEdge || p->jumpedEdge)) return fail("CEIL_JUMPED_OFF");
                                   if (since >= 0.3f) return s.car.hasFlip ? done() : fail("CEIL_JUMPED_OFF");
                                   return pending();
                               },
                               // A car keeps to the ceiling while the throttle is held: niveaux 1-3 (the ball waits
                               // for the drop) leave time to settle before letting go.
                               n <= 3 ? 3.f : 1.8f, "CEIL_STEP_DROP"));
        // Upside down the stick is reversed: pulling it back points the nose toward the floor (Snapshot.h: pitch +1
        // turns the nose toward the car's roof side). Green while the ball is right in front of the nose.
        out.push_back(infoStep("ceil.aim", "Nez vers la balle (à l'envers : stick vers toi)", [](const StepInput& in) {
            const CarState& c = in.snap.car;
            const Vec3 L = c.basis.toLocal(in.snap.ball.pos - hitboxCenter(c));
            return L.x >= 100.f && L.x <= 400.f && std::fabs(L.y) < 120.f && std::fabs(L.z) < 120.f ? done() : pending();
        }));
        out.push_back(makeStep("ceil.shot", n >= kTargetLevel ? "Flippe dans la balle, vers le but" : "Flippe dans la balle",
                               nullptr, 2.5f, "CEIL_MISSED"));
        break;
    }
    case Mech::FlipReset: {
        out.push_back(makeStep("fr.takeoff", "Décolle vers la balle",
                               [p](const StepInput& in) {
                                   return p->worldWheels == 0 && in.snap.car.pos.z > 250.f ? done() : pending();
                               },
                               n <= 3 ? 5.f : 3.f, "FR_STEP_TAKEOFF"));
        out.push_back(makeStep("fr.under", "Passe sous la balle",
                               [p](const StepInput& in) {
                                   const Snapshot& s = in.snap;
                                   return p->resetSeen || (distance(s.car.pos, s.ball.pos) < 700.f && s.ball.pos.z - s.car.pos.z > 100.f)
                                              ? done()
                                              : pending();
                               },
                               3.f, "FR_STEP_UNDER"));
        out.push_back(makeStep("fr.align", "Roues vers la balle, lâche le boost",
                               [p](const StepInput& in) {
                                   const Snapshot& s = in.snap;
                                   const bool aligned = undersideAlignment(s.car, s.ball) > 0.8f &&
                                                        distance(hitboxCenter(s.car), s.ball.pos) < 400.f;
                                   return aligned || p->resetSeen ? done() : pending();
                               },
                               2.f, "FR_POOR_ALIGNMENT"));
        out.push_back(makeStep("fr.reset", "Touche-la avec les 4 roues",
                               [p](const StepInput&) { return p->resetSeen ? done() : pending(); }, 1.5f, "FR_NO_RESET"));
        if (n >= kFrConvertLevel)
            out.push_back(makeStep("fr.flip", n >= kFrTargetLevel ? "Flippe dans la balle, vers le but" : "Flippe dans la balle",
                                   nullptr, 6.f, "FR_RESET_WASTED"));
        break;
    }
    case Mech::Flick: {
        addPickup(out, m, d);
        out.push_back(carriedJumpStep(p));
        Step flick = makeStep("fl.flick", flickLabel(d.flickTarget),
                              [p](const StepInput&) { return p->landedAfterJump ? fail("FL_NO_CONTACT") : pending(); }, 1.5f,
                              "FL_NO_CONTACT");
        out.push_back(flick);
        break;
    }
    case Mech::Musty: {
        addPickup(out, m, d);
        out.push_back(carriedJumpStep(p));
        out.push_back(makeStep("mu.nose", "Stick vers l'avant : nez en bas",
                               [p](const StepInput& in) {
                                   const Snapshot& s = in.snap;
                                   if (p->landedAfterJump) return fail("FL_MUSTY_NOSE_NOT_DOWN");
                                   return p->airPhase && pitchDeg(s.car) <= kMustyPitchDeg &&
                                                  distance(s.ball.pos, s.car.pos) <= kCarryReach
                                              ? done()
                                              : pending();
                               },
                               1.f, "FL_MUSTY_NOSE_NOT_DOWN"));
        out.push_back(makeStep("mu.flip", "Flip arrière",
                               [p](const StepInput&) { return p->landedAfterJump ? fail("FL_NO_CONTACT") : pending(); }, 1.f,
                               "FL_NO_CONTACT"));
        break;
    }
    case Mech::Breezi: {
        addPickup(out, m, d);
        out.push_back(makeStep("bz.jump", "Saut maintenu",
                               [p](const StepInput&) {
                                   if (!p->airPhase) return pending();
                                   if (p->jumpHeld >= 0.15f) return done();
                                   return p->jumpReleased ? fail("BZ_STEP_HOLD") : pending();
                               },
                               5.f, "BZ_STEP_HOLD"));
        out.push_back(makeStep("bz.spin", "Air roll + stick opposé : tornade",
                               [p](const StepInput& in) {
                                   const Snapshot& s = in.snap;
                                   if (p->landedAfterJump) return fail("FL_BREEZI_NO_SPIN");
                                   return std::fabs(p->rollAccum) * kRadToDeg >= kTornadoDeg &&
                                                  distance(s.ball.pos, s.car.pos) <= kCarryReach
                                              ? done()
                                              : pending();
                               },
                               1.5f, "FL_BREEZI_NO_SPIN"));
        if (n >= kBzTornadoLevel)
            out.push_back(makeStep("bz.flip", "Flip arrière quand le nez remonte",
                                   [p](const StepInput&) { return p->landedAfterJump ? fail("FL_NO_CONTACT") : pending(); },
                                   1.2f, "FL_NO_CONTACT"));
        break;
    }
    case Mech::AirDribble: {
        addPickup(out, m, d);
        out.push_back(makeStep("ad.pop", "Saute en levant un peu le nez",
                               [p](const StepInput& in) {
                                   if (!p->airPhase) return pending();
                                   if (in.snap.ball.pos.z >= p->ballZAtJump + 250.f) return done();
                                   return in.now - p->jumpTime > 0.6f ? fail("AD_LOW_POP") : pending();
                               },
                               6.f, "AD_STEP_POP"));
        out.push_back(makeStep("ad.touch", "Nez vers la balle, petite touche",
                               [p](const StepInput& in) {
                                   return p->worldWheels == 0 && in.snap.ball.pos.z >= 300.f && p->touch ? done() : pending();
                               },
                               1.2f, "AD_STEP_TOUCH"));
        if (n >= 2) out.push_back(makeStep("ad.keep", "Garde-la en l'air", nullptr, 8.f, "AD_SHORT"));
        if (n >= kAdTargetLevel) out.push_back(makeStep("ad.finish", "Finis vers le but", nullptr, 8.f, "AD_NO_FINISH"));
        break;
    }
    case Mech::DoubleTap: {
        out.push_back(makeStep("dt.shot", "Tire haut sur le mur du fond",
                               [p](const StepInput&) { return p->dtShotHigh ? done() : pending(); }, 4.f, "DT_LOW_BACKBOARD"));
        out.push_back(makeStep("dt.follow", "Suis la balle, en l'air",
                               [p](const StepInput& in) {
                                   if (!p->bounceNow) return pending();
                                   const Snapshot& s = in.snap;
                                   return p->worldWheels == 0 && distance(s.car.pos, s.ball.pos) <= 1200.f ? done()
                                                                                                          : fail("DT_LATE_FOLLOW");
                               },
                               3.f, "DT_LATE_FOLLOW"));
        out.push_back(makeStep("dt.second", "Reprends le rebond", nullptr, 2.5f, "DT_NO_SECOND_TOUCH"));
        if (n >= kDtTargetLevel) out.push_back(makeStep("dt.target", "Vers le but", nullptr, 2.5f, "DT_OFF_TARGET"));
        break;
    }
    case Mech::Pinch: {
        out.push_back(makeStep("pi.speed", "Prends de la vitesse vers le mur",
                               [](const StepInput& in) { return length(in.snap.car.vel) >= 1800.f ? done() : pending(); }, 3.5f,
                               "PI_STEP_SPEED"));
        out.push_back(infoStep("pi.aim", "Vise où la balle touche le mur", [p](const StepInput& in) {
            const Snapshot& s = in.snap;
            const float vx = s.ball.vel.x * p->side;
            if (vx < 1.f || length(s.car.vel) < 1800.f) return pending();
            const float t = std::max(0.f, (arena::kSideWallX - s.ball.radius - std::fabs(s.ball.pos.x)) / vx);
            const Vec3 hit = s.ball.pos + s.ball.vel * t;
            return angleDeg(flat(s.car.basis.fwd), flat(hit - s.car.pos)) <= 25.f ? done() : pending();
        }));
        out.push_back(makeStep("pi.pinch", "Frappe-la contre le mur", nullptr, 4.f, "PI_TIMING"));
        out.push_back(makeStep("pi.exit", n >= kTargetLevel ? "Assez fort, vers le but" : "Assez fort", nullptr, 4.f,
                               "DRILL_NEED_POWER"));
        break;
    }
    case Mech::PowerShot: {
        out.push_back(makeStep("ps.align", "Mets la balle entre toi et le but",
                               [p](const StepInput& in) {
                                   const Snapshot& s = in.snap;
                                   const Vec3 goal{0.f, p->sign * arena::kBackWallY, 0.f};
                                   const float dist = length2D(s.ball.pos - s.car.pos);
                                   const bool aligned = angleDeg(flat(s.ball.pos - s.car.pos), flat(goal - s.ball.pos)) < 20.f;
                                   if (aligned && dist >= 800.f) return done();
                                   return dist < 800.f ? fail("PS_STEP_ALIGN") : pending();
                               },
                               6.f, "PS_STEP_ALIGN"));
        out.push_back(makeStep("ps.speed", "Prends de la vitesse",
                               [](const StepInput& in) {
                                   const Snapshot& s = in.snap;
                                   if (length2D(s.ball.pos - s.car.pos) >= 400.f) return pending();
                                   return length(s.car.vel) >= 1600.f ? done() : fail("PS_WEAK");
                               },
                               5.f, "PS_WEAK"));
        out.push_back(makeStep("ps.flip", "Flippe juste avant la balle", nullptr, 3.f, "PS_NO_FLIP"));
        out.push_back(makeStep("ps.hit", "Frappe avec l'avant, vers le but", nullptr, 3.f, "PS_WEAK"));
        break;
    }
    case Mech::AirRoll: {
        out.push_back(makeStep("ar.takeoff", "Décolle vers la balle",
                               [p](const StepInput& in) {
                                   return p->worldWheels == 0 && in.snap.car.pos.z > 250.f ? done() : pending();
                               },
                               4.f, "AE_STEP_TAKEOFF"));
        Step roll = makeStep("ar.roll", "Air roll dans un seul sens",
                             [p](const StepInput& in) {
                                 return p->worldWheels == 0 && std::fabs(in.snap.in.roll) >= 0.5f ? done() : pending();
                             },
                             2.5f, "AE_NO_AIR_ROLL");
        roll.hold = 0.3f;
        roll.holdGrace = 0.05f;
        out.push_back(roll);
        out.push_back(makeStep("ar.centre", "Vise le centre de la balle", nullptr, 3.f, "AE_OFF_CENTER"));
        out.push_back(makeStep("ar.hit", n >= kArTargetLevel ? "Frappe vers le but" : "Frappe-la fort", nullptr, 3.f,
                               n >= kArTargetLevel ? "AE_OFF_TARGET" : "AE_WEAK"));
        break;
    }
    case Mech::Aerial: {
        out.push_back(makeStep("ae.axis", "Place-toi dans l'axe balle-but",
                               [p](const StepInput& in) {
                                   if (!p->takeoffNow) return pending();
                                   const Vec3 goal{0.f, p->sign * arena::kBackWallY, 0.f};
                                   const float a = angleDeg(flat(p->target - in.snap.car.pos), flat(goal - p->target));
                                   return a < 25.f ? done() : fail("AE_STEP_AXIS");
                               },
                               5.f, "AE_STEP_AXIS"));
        out.push_back(makeStep("ae.takeoff", "Saute, lève le nez et boost",
                               [p](const StepInput& in) {
                                   const Snapshot& s = in.snap;
                                   if (p->worldWheels == 0 && s.in.boost && pitchDeg(s.car) > 30.f) return done();
                                   return p->takeoffTime >= 0.f && in.now - p->takeoffTime > 0.4f ? fail("AE_STEP_TAKEOFF")
                                                                                                 : pending();
                               },
                               2.f, "AE_STEP_TAKEOFF"));
        out.push_back(infoStep("ae.aim", "Nez vers le centre de la balle", [](const StepInput& in) {
            const CarState& c = in.snap.car;
            return angleDeg(c.basis.fwd, in.snap.ball.pos - c.pos) < 15.f ? done() : pending();
        }, 0.3f));
        out.push_back(makeStep("ae.hit", "Garde le boost, frappe vers le but", nullptr, 3.f, "AE_OFF_TARGET"));
        break;
    }
    default: break;
    }
    return out;
}

bool landingCheckOn(Mech m, const StepTracker& t) {
    switch (m) {
    case Mech::Ceiling: return t.reached("ceil.drop");
    case Mech::FlipReset: return t.done("fr.takeoff") && !t.done("fr.reset");
    case Mech::AirDribble: return t.done("ad.pop");
    case Mech::DoubleTap: return t.done("dt.shot");
    case Mech::AirRoll: return t.done("ar.takeoff");
    case Mech::Aerial: return t.done("ae.takeoff");
    default: return false;
    }
}

bool ballLostCheckOn(Mech m, const StepTracker& t) {
    switch (m) {
    case Mech::Flick:
    case Mech::Musty:
    case Mech::Breezi: return t.done(pickup::kStepSettle);
    default: return false;
    }
}

const char* landingCode(Mech m) { return m == Mech::AirDribble ? "AD_SHORT" : "DRILL_LANDED"; }

CompleteAction onChecksComplete(Mech m) {
    // Flip reset niveaux 1-4 and air dribble niveau 1 end on a check (reset obtained, first air touch): the detector's
    // grade comes a little later (the reset is used or lost, the ball falls) and gives the measures.
    return m == Mech::FlipReset || m == Mech::AirDribble ? CompleteAction::AwaitGrade : CompleteAction::Resolve;
}

std::string criteriaStep(Mech m, int n) {
    switch (m) {
    case Mech::Pinch: return "pi.exit";
    case Mech::PowerShot: return "ps.hit";
    case Mech::AirRoll: return "ar.hit";
    case Mech::DoubleTap: return n >= kDtTargetLevel ? "dt.target" : "";
    case Mech::AirDribble: return n >= kAdTargetLevel ? "ad.finish" : "";
    default: return "";
    }
}

std::vector<std::string> parcoursLabels(Mech m, int niveau, const std::string& flickVariant) {
    Rng rng(1);
    SetupOptions o;
    o.mode = DrillMode::Full;
    o.flickVariant = flickVariant;
    CarState car;
    const DrillSetup d = makeDrillSetup(m, levelOfPlayer(niveau), 0, car, 91.25f, rng, o);
    ParcoursState st;
    StepTracker t;
    t.setSteps(parcoursSteps(m, niveau, d, st));
    std::vector<std::string> out;
    for (int i = 0; i < t.size(); ++i) out.push_back(t.number(i) + t.steps()[static_cast<size_t>(i)].label);
    return out;
}

} // namespace rlt::bm
