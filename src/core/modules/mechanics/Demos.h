#pragma once
// Automatic demonstrations ("Voir la démo"): the module drives the player's own car through a mechanic, in slow
// motion, and writes the current step on the HUD (MechanicsModule runs them, freeplay only).
//
// A demo is a timeline of keys in physics ticks (120 Hz) after GO: each key holds its inputs until the next one and
// names the HUD step shown from that tick. Every timing the module grades (jump to flip delay, flip angle, cancel
// delay, first jump held for the fast aerial, stick centred at the second press...) is a fixed number of ticks inside
// the module's own windows, from the community trainers its detectors follow (SpeedFlipTrainer bot timeline,
// FastAerialTrainer windows): tests/mechanics/test_mechanics_demo.cpp feeds the timelines through the detectors and
// the drill's top level checks. Nothing here simulates the car: only the landings get a small correction read from the
// car's attitude in the snapshot (air roll toward the wheels, then nose and heading), because a real car may turn a
// little more or less than expected. The game reads a flip direction from the DodgeForward / DodgeStrafe fields of
// the input, so demoInputs fills them like the game's controller does.

#include <cstdint>
#include <string>
#include <vector>

#include "core/common/Actions.h"
#include "core/common/Snapshot.h"
#include "core/modules/mechanics/MechCatalog.h"

namespace rlt::mech {

// Landing correction of a key, from the car's basis in the snapshot (and the previous tick's for the rates; never the
// angular velocity, whose axes convention is not needed then).
enum class DemoAssist : uint8_t {
    None = 0,
    // Wheels down: air roll toward the floor; once nearly upright, nose at `noseDeg` and heading on the first jump's.
    Level,
};

struct DemoKey {
    int at = 0;                  // ticks after GO
    Inputs in;                   // held until the next key
    int step = 0;                // index of the HUD step shown from this key
    DemoAssist assist = DemoAssist::None;
    bool afterInverted = false;  // Level: only once the car has been upside down since GO (speedflip, half-flip roll)
    float rollDir = 0.f;         // Level: air roll used while the car is still far from upright (0: the key's roll)
    float noseDeg = 0.f;         // Level: nose angle held once upright (0 = flat)
    bool levelPitch = true;      // Level: pitch corrected too (never during the flip's torque time)
    bool levelYaw = false;       // Level: heading corrected too, toward the first jump's heading + headingDeg
    float headingDeg = 0.f;      // half-flip: 180 (the car comes back the other way)
};

struct DemoScript {
    Focus focus = Focus::None;
    std::string variant;          // where the demo starts, plain words
    CarPlacement car;             // start pose, held still during the countdown; vel applied on GO
    BallPlacement ball;           // ball parked out of the way
    bool kickoffBoost = false;    // kickoff conditions: 33 boost, limited (the engine restores the rules afterwards)
    float speed = 0.5f;           // game speed during the demo
    float countdown = 2.f;        // seconds the car is held before GO
    std::vector<std::string> steps;  // HUD steps, plain words (numbered when shown)
    std::vector<DemoKey> keys;    // sorted by `at`, keys[0].at == 0
    int flipAt = -1;              // tick of the flip (second jump press), -1 without one
    int length = 0;               // ticks after GO, then the controls go back to the player
};

// Families with a demo: speedflip, flip cancel, half-flip, wavedash, fast aerial, powerslide, stall.
bool hasDemo(Focus f);
// Demo of a family for the player's team (orange: positions turned 180 degrees). The speedflip uses the flip angle
// the module grades (MechanicsConfig::speedflipAngle).
DemoScript makeDemo(Focus f, int team, float speedflipAngleDeg = 30.f);

// Key active `t` ticks after GO (the last one once the timeline is over).
int demoKeyIndex(const DemoScript& d, int t);
// Timeline inputs `t` ticks after GO, without the landing correction. DodgeForward = -pitch and DodgeStrafe = yaw + roll,
// clamped, like the game's controller (RocketSim and the plugin's own dodge formula).
Inputs demoInputs(const DemoScript& d, int t);
// Step of the timeline `t` ticks after GO (the key's step).
int demoStep(const DemoScript& d, int t);

// Plays a demo tick by tick: the timeline inputs plus the landing correction of the active key.
class DemoPilot {
public:
    void start(int64_t goFrame);
    // Inputs for the snapshot's tick (call once per tick, in order).
    Inputs update(const DemoScript& d, const Snapshot& s);
    int tick(int64_t frame) const { return static_cast<int>(frame - goFrame_); }
    bool seenInverted() const { return inverted_; }
    // HUD step of the last update: the key's step, except that a landing key waiting for the car (upside down first,
    // then nearly upright) keeps the step before it, so the HUD says what the car is really doing.
    int step() const { return step_; }

private:
    int64_t goFrame_ = 0;
    int step_ = 0;
    bool inverted_ = false;
    bool hasJumpHeading_ = false;
    float jumpHeading_ = 0.f;
    bool hasPrev_ = false;
    int64_t prevFrame_ = 0;
    float prevRoll_ = 0.f, prevNose_ = 0.f, prevHeading_ = 0.f;
};

} // namespace rlt::mech
