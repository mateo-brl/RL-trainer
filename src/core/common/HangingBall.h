#pragma once
// Hanging ball (cross-cutting fix 3): a ball that waits, perfectly still, at a point in the air until the drill lets
// it go (a step validated, the player jumped...) or the player hits it. Gravity would make a ball placed once fall,
// so the ball is put back with zero velocity and zero spin on EVERY tick, but never on a tick whose snapshot shows a
// car touch: a hit is never erased. Once let go (release or touch) nothing is sent any more until hang() again.
//
// Touch detection on a tick (any of them):
// - the car's last ball touch frame (VehicleWrapper::GetLastBallTouchFrame) changed since the hanging started, the
//   same test the engine uses to resolve touches;
// - the ball moved although it was put back still on the previous frame: speed > kTouchSpeed or more than
//   kTouchDrift from the point it was put at (one tick of gravity only gives ~5.4 uu/s and 0.05 uu);
// - the caller says so (`touchSeen`, e.g. a module that already knows about a contact).
//
// Usage, once per tick in onTick (while the module owns the activity):
//   if (stepJustValidated) ball.release({-600, 0, 250});   // released on this same tick
//   ball.update(ctx.snap, out);                            // fills out.setBall when needed
// Pure: fills Actions::setBall only.

#include <cstdint>

#include "core/common/Actions.h"
#include "core/common/Math.h"
#include "core/common/Snapshot.h"

namespace rlt {

// True when the snapshot shows a car touch since `prevTouchFrame` (the last ball touch frame changed and is valid).
bool touchFrameChanged(const Snapshot& s, int prevTouchFrame);

class HangingBall {
public:
    enum class Event : int {
        None = 0,   // idle or already let go: nothing sent
        Held,       // setBall {pos, 0, 0} sent
        Released,   // the release setBall {pos, vel, angVel} sent on this tick
        Touched,    // the snapshot shows a touch: nothing sent, the ball is free from now on
    };

    static constexpr float kTouchSpeed = 40.f;  // uu/s
    static constexpr float kTouchDrift = 15.f;  // uu

    // Starts (or moves) the hanging point: from the next update the ball is held there. Clears a pending release.
    // Call it once when the attempt starts (or to move the point): after a release or a touch it hangs the ball again.
    void hang(const Vec3& pos);
    // Lets the ball go with this velocity on the next update (call it before update() to release on this tick).
    // Ignored when the ball is not hanging.
    void release(const Vec3& vel, const Vec3& angVel = {});
    // Stops holding without sending anything (the ball keeps whatever physics it has). Back to idle.
    void stop();

    // Once per tick. Never sends a setBall on a tick whose snapshot shows a touch.
    Event update(const Snapshot& s, Actions& out, bool touchSeen = false);

    bool hanging() const { return state_ == State::Hanging; }   // held still (setBall every tick)
    bool releasePending() const { return state_ == State::Hanging && pendingRelease_; }
    bool released() const { return state_ == State::Released; }
    bool touched() const { return state_ == State::Touched; }
    bool free() const { return released() || touched(); }      // let go, one way or the other
    bool idle() const { return state_ == State::Idle; }
    const Vec3& position() const { return pos_; }
    int64_t freeFrame() const { return freeFrame_; }            // frame of the release or touch (-1 before)

private:
    enum class State : int { Idle = 0, Hanging, Released, Touched };

    State state_ = State::Idle;
    Vec3 pos_;
    Vec3 sentPos_;
    bool pendingRelease_ = false;
    Vec3 releaseVel_;
    Vec3 releaseAngVel_;
    bool haveTouchBase_ = false;
    int touchBase_ = -1;
    int64_t lastSentFrame_ = -1;
    int64_t freeFrame_ = -1;
};

} // namespace rlt
