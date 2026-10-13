#pragma once
// One physics tick (120 Hz) of game state, built by the plugin glue in the
// `Car_TA.SetVehicleInput` pre-hook, i.e. BEFORE the physics step of that tick.
// Every module reads only this struct, never the SDK, so all game logic is testable on Linux.

#include <cstdint>

#include "core/common/Math.h"

namespace rlt {

constexpr float kTickRate = 120.f;
constexpr float kTickDt = 1.f / kTickRate;

// Raw controller state for this tick (ControllerInput). Sign conventions:
// pitch +1 = nose up (stick pulled back), yaw/steer +1 = right, roll +1 = roll right.
struct Inputs {
    float throttle = 0.f, steer = 0.f, pitch = 0.f, yaw = 0.f, roll = 0.f;
    float dodgeForward = 0.f, dodgeStrafe = 0.f;
    bool jump = false;       // jump button held
    bool boost = false;      // ActivateBoost
    bool holdingBoost = false;
    bool handbrake = false;  // powerslide / air roll button held
    bool jumped = false;     // engine flag "Jumped" from ControllerInput
};

struct CarState {
    bool valid = false;
    Vec3 pos;                // physics (RB) location, centre of mass
    Vec3 vel;
    Vec3 angVel;             // rad/s, world space
    Rot rot;
    Basis basis;             // fwd / right / up derived from rot
    int wheelContacts = 0;   // GetNumWheelContacts (includes contact with the ball / cars)
    bool onGround = false;   // engine IsOnGround (>= 3 wheels)
    bool onWall = false;
    Vec3 groundNormal{0.f, 0.f, 1.f};
    bool hasFlip = false;    // dodge / double jump still available
    bool jumped = false;     // first jump used since last landing
    bool doubleJumped = false;
    bool dodging = false;    // a dodge is in progress
    Vec3 dodgeDir;           // local dodge direction (x forward, y right) when dodging
    bool supersonic = false;
    float boost = 0.f;       // 0..100
    int team = 0;            // 0 blue, 1 orange
    Vec3 hitboxExtent{118.01f, 84.20f, 36.16f};  // full size (Octane by default), local axes
    Vec3 hitboxOffset{13.88f, 0.f, 20.75f};      // local offset of the hitbox centre from pos
    int lastBallTouchFrame = -1;                 // VehicleWrapper::GetLastBallTouchFrame
};

struct BallState {
    bool valid = false;
    Vec3 pos{0.f, 0.f, 93.15f};
    Vec3 vel;
    Vec3 angVel;
    float radius = 91.25f;    // collision radius vs cars (BallWrapper::GetRadius)
};

struct Snapshot {
    int64_t frame = 0;   // physics frame number (EngineTA::GetPhysicsFrame), strictly increasing
    float time = 0.f;    // game seconds = frame / 120 (stable under slow motion)
    float dt = kTickDt;
    CarState car;
    BallState ball;
    Inputs in;
    bool paused = false;
};

// Convenience queries shared by modules.
inline bool airborne(const CarState& c) { return !c.onGround && c.wheelContacts < 3; }
inline float speed(const CarState& c) { return length(c.vel); }
inline Vec3 hitboxCenter(const CarState& c) { return c.pos + c.basis.toWorld(c.hitboxOffset); }

} // namespace rlt
