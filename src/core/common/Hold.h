#pragma once
// Get-ready phase of every drill (cross-cutting fix 1).
//
// `Actions::freezeCar` only cuts the driving inputs (the glue sets bDriving 0): gravity still acts, so a car or a
// ball placed in the air ONCE falls during the countdown. While a drill holds its start position it re-sends the
// car pose with zero velocity, and the ball, on EVERY tick: applyHold().
//
// ReadyPhase sequences the start of an attempt: on the first attempt the car is held until the player presses the
// throttle (HUD "Appuie sur accélérer quand tu es prêt"), then a 3 s countdown; on an automatic reset a 2 s
// countdown without waiting. The native 3, 2, 1 and GO messages are sent on the right ticks. A start where a held
// throttle spoils everything (ball balanced on a stopped car) also waits, at the end of the countdown, until the
// player lets go of the throttle (HUD "Lâche l'accélérateur pour partir").
//
// Usage, once per tick in onTick:
//   if (ready.active()) {
//       if (ready.update(ctx.snap, out)) launch(out);   // GO tick: real velocities, no freeze, no hold
//       else applyHold(hold, &ctx.snap, out);           // every other tick of the phase
//   }
// On the GO tick do not call applyHold: its freezeCar would swallow the inputs the player already holds.
// Pure: fills Actions only.

#include <optional>
#include <string>

#include "core/common/Actions.h"
#include "core/common/Snapshot.h"

namespace rlt {

struct HoldPose {
    CarPlacement car;                   // pose; velocity and angular velocity are always sent as zero
    std::optional<BallPlacement> ball;  // empty: the ball is left alone; velocities are always sent as zero
    std::optional<float> boost;         // 0..100, re-sent every tick (holding boost drains the tank even while frozen)
    bool freeze = true;                 // also zero the player's inputs (Actions::freezeCar)
    // A car placed on the floor keeps the height its suspension settled at (within kSettleBand of the pose), so it
    // does not bounce on every re-placement. Needs the snapshot.
    bool keepSettledHeight = true;
};

constexpr float kSettleBand = 5.f;     // uu around the pose height
constexpr float kSettleMaxZ = 60.f;    // a pose at or below this height, roof up, counts as "on the floor"

// Fills `out` for one held tick: setCar (pose, zero velocity, boost), setBall (zero velocity) when the pose has one,
// setBoost, freezeCar. `snap` may be null (no settled height then).
void applyHold(const HoldPose& hold, const Snapshot* snap, Actions& out);

// Throttle press that ends the wait: at least this much throttle (a key, or half a trigger), after the throttle was
// seen released (below kReadyThrottleRelease) once since the phase started.
constexpr float kReadyThrottle = 0.5f;
constexpr float kReadyThrottleRelease = 0.2f;

class ReadyPhase {
public:
    enum class Phase : int { Off = 0, Waiting = 1, Countdown = 2, Done = 3 };

    struct Options {
        bool waitForThrottle = true;  // hold until the player presses the throttle
        float countdown = 3.f;        // seconds of countdown after the press (or from start() without waiting)
        bool nativeMessages = true;   // the game's own 3, 2, 1 and GO messages
        // GO waits until the throttle is let go (below kReadyThrottleRelease): holding throttle through a countdown
        // is every player's kickoff habit, and it would throw a ball balanced on a stopped car off its roof at GO.
        bool waitThrottleRelease = false;
    };
    // First attempt and manual retry: wait for the throttle, then 3 s.
    static Options firstAttempt() { return Options{true, 3.f, true, false}; }
    // Automatic reset after an attempt: 2 s, no waiting.
    static Options autoReset() { return Options{false, 2.f, true, false}; }
    // Manual retry (the player asked for it, so he is ready): 3 s, no waiting.
    static Options manualRetry() { return Options{false, 3.f, true, false}; }
    // Plain timed hold (e.g. Mécas balle "Ready" 1.5 s at niveaux 1-3), no messages.
    static Options timed(float seconds) { return Options{false, seconds, false, false}; }

    void start(float now, const Options& options);
    void stop() { phase_ = Phase::Off; }

    // Once per tick while active(). Emits the countdown messages and, on the GO tick, goMessage. Returns true
    // exactly once, on the GO tick: the caller launches the attempt on that tick and stops holding.
    bool update(const Snapshot& s, Actions& out);

    Phase phase() const { return phase_; }
    bool active() const { return phase_ == Phase::Waiting || phase_ == Phase::Countdown; }
    bool waiting() const { return phase_ == Phase::Waiting; }
    bool counting() const { return phase_ == Phase::Countdown; }
    // The countdown is over but GO waits for the throttle to be let go (Options::waitThrottleRelease).
    bool waitingRelease(float now) const {
        return phase_ == Phase::Countdown && opt_.waitThrottleRelease && goTime_ - now <= 1e-4f;
    }
    // Seconds until GO during the countdown (the whole countdown while waiting, 0 after GO).
    float remaining(float now) const;
    float goTime() const { return goTime_; }  // game time of GO (valid in Countdown and Done)
    // HUD action line: "Appuie sur accélérer quand tu es prêt" while waiting; during the countdown "Prêt ?" when the
    // game shows its own 3, 2, 1 (Options::nativeMessages), else "Départ dans 3"; "Lâche l'accélérateur pour partir"
    // while GO waits for the throttle to be let go; "" otherwise.
    std::string hudLine(float now) const;

private:
    Phase phase_ = Phase::Off;
    Options opt_;
    bool armed_ = false;     // the throttle was seen released since start()
    float goTime_ = 0.f;
    int lastMessage_ = -1;
};

} // namespace rlt
