#pragma once
// "Parcours complet" of the ball mechanics drills: the ordered steps (shared StepTracker, cross-cutting fix 2) that
// come before and around the mechanic itself (climb the wall before the ceiling shot, pick the ball up before the
// flick...). The checks read a ParcoursState the module refreshes on every tick (wheel contacts, jump edges, the
// carried jump of the flick family, the ceiling drop...), so they stay pure and testable.
//
// The step of the mechanic itself has no check of its own: the module validates it (StepTracker::complete) when the
// detector's grade passes the drill criteria, and fails it with the grade's tips otherwise.

#include <string>
#include <vector>

#include "core/common/Steps.h"
#include "core/modules/ballmech/Drills.h"
#include "core/modules/ballmech/Mechanics.h"

namespace rlt::bm {

// Live facts the step checks read. The module calls parcoursBegin() at GO and parcoursTick() on every tick before
// StepTracker::update().
struct ParcoursState {
    // ---- Fixed for the attempt
    Mech mech = Mech::Ceiling;
    int niveau = 1;
    float side = 0.f;          // wall used by the drill (+1 / -1, after the team mirror)
    int team = 0;
    float sign = 1.f;          // attack direction along Y
    Vec3 target;               // aerial: where the centre is met
    bool standardArena = true;

    // ---- Refreshed every tick
    bool hasPrev = false;
    Snapshot prev;
    int worldWheels = 0, ballWheels = 0;
    bool jumpEdge = false;     // jump button pressed on this tick
    bool jumpedEdge = false;   // the car's "jumped" flag rose on this tick
    bool touch = false;        // the car touched the ball on this tick (touch frame changed, or the module's event)
    bool resetSeen = false;    // a flip reset (game event, or 3 wheels on the ball in the air) since GO

    // Carried jump (flick family, air dribble pop).
    float carryTime = 0.f;     // seconds the ball has been carried on the roof, car on the ground
    float lastCarry = -1e9f;   // time of the last carried tick
    bool airPhase = false;     // in the air after a jump with the ball carried
    bool landedAfterJump = false;
    float jumpTime = -1.f;
    float jumpHeld = 0.f;      // seconds the jump button stayed held after that jump
    bool jumpReleased = false;
    float ballZAtJump = 0.f;
    float rollAccum = 0.f;     // rad around the nose since that jump
    int airTicks = 0, airRollTicks = 0, keptTicks = 0;

    // Free flight.
    float noWheelSince = -1.f; // time the car lost every arena wheel (-1 while it has some)
    bool wasGrounded = true;
    bool takeoffNow = false;   // first tick without arena wheels after a jump from the ground
    float takeoffTime = -1.f;

    // Ceiling.
    float leaveTime = -1.f;    // the car left the ceiling (no wheel)
    bool tooFast = false;      // reached the ceiling above 1800 uu/s: a tip, not a failure

    // Double tap.
    int64_t dtEvalFrame = -1;
    bool dtShotHigh = false;   // an air touch sent the ball high on the backboard
    bool bounceNow = false;    // the ball bounced off the attacked backboard on this tick
};

// Resets the state for an attempt starting now (fixed fields from the setup).
void parcoursBegin(ParcoursState& st, Mech m, int niveau, const DrillSetup& d, int team, bool standardArena);
// Refreshes the per-tick facts. `touchEvent`: the module saw a car touch on this tick; `resetEvent`: the game
// reported a flip reset since the last tick.
void parcoursTick(ParcoursState& st, const Snapshot& s, bool touchEvent, bool resetEvent);

// The steps of the full sequence of `m` at `niveau` (1..10). The checks keep a pointer to `st`.
std::vector<Step> parcoursSteps(Mech m, int niveau, const DrillSetup& d, ParcoursState& st);

// Ball resting on the roof for the pickup steps: the module's carry box (bm::carryGeometry), with the ball really up
// on the car (not pushed along the floor in front of the bumper, which the carry box also accepts).
bool carriedOnRoof(const Snapshot& s);

// Generic checks of the module, switched on only from the step that needs them (fix 2).
bool landingCheckOn(Mech m, const StepTracker& t);
bool ballLostCheckOn(Mech m, const StepTracker& t);
const char* landingCode(Mech m);  // "DRILL_LANDED", "AD_SHORT" for the air dribble
// The tracker completed on its own checks (no grade needed): what the module does then.
enum class CompleteAction : int { Resolve, AwaitGrade };
CompleteAction onChecksComplete(Mech m);

// Id of the last required step whose failure means "the mechanic was done but not well enough" (power, target):
// the module completes the mechanic step and fails this one. "" when the mechanic step is the last.
std::string criteriaStep(Mech m, int niveau);

// Plain step labels of the full sequence at `niveau` (tab guide, tests), numbered like the HUD ("1/5 ...").
std::vector<std::string> parcoursLabels(Mech m, int niveau, const std::string& flickVariant = "");

} // namespace rlt::bm
