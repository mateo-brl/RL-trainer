#pragma once
// Records one kickoff attempt from GO to the first touch: reaction, jumps and dodges (speedflip detection),
// boost economy, path deviation, distance along the path per tick (splits, personal-best ghost), contact
// (time, speed, lateral offset) and the ball exit direction. Fed with Snapshots in frame order.

#include <cstdint>
#include <vector>

#include "core/common/Snapshot.h"
#include "core/modules/kickoff/KickoffData.h"

namespace rlt {

enum class KickoffType : int {
    Unknown = 0,
    Speedflip,
    FrontFlip,
    DoubleFlip,
    DiagonalFlip,
    Wavedash,
    BoostOnly,
    Delayed,
    Fake,
    BoostRoute,
    Missed,
    Count
};
constexpr int kKoTypeCount = static_cast<int>(KickoffType::Count);
const char* kickoffTypeName(KickoffType t);  // French

struct KickoffMetrics {
    KickoffSpawn spawn = KickoffSpawn::BackCenter;
    KickoffTeam team = KickoffTeam::Blue;
    bool touched = false;
    bool estimated = false;    // no real touch: arrival estimated when the opponent hit the ball first
    float timeToTouch = 0.f;   // s from GO (frames / 120)
    int ticksToTouch = 0;      // physics frames from GO to the touch frame
    float speedAtTouch = 0.f;  // |v|
    float speedToBall = 0.f;   // horizontal: flat(v) . unit(flat(ball - car))
    float boostAtTouch = 0.f;  // boost remaining at the touch
    float boostUsed = 0.f;     // boost spent from GO to the touch
    float boostPicked = 0.f;
    int padsPicked = 0;
    bool boostLooksUnlimited = false;  // boost held for a while without the tank going down
    float lateralOffset = 0.f;         // ball centre in the car frame, + = right (uu)
    float contactHeight = 0.f;         // ball centre above the car origin (car frame)
    bool flipIntoBall = false;         // a dodge started <= 0.35 s before the touch
    // Timings in s from GO; negative = did not happen.
    float reaction = -1.f;  // first throttle (> 0.5) or boost input
    float firstJump = -1.f, dodge = -1.f, cancel = -1.f, landing = -1.f, secondDodge = -1.f;
    float dodgeAngleDeg = 0.f;  // 0 = straight forward, + = right
    float dodgeForward = 0.f, dodgeSide = 0.f;
    int dodges = 0;
    float boostHeldRatio = 0.f;  // ticks with boost held / ticks with boost available, until the touch
    float pathDevRms = 0.f, pathDevMax = 0.f;
    float minDistToBall = 1e9f;
    float progress = 0.f;  // max distance along the path
    // Ball after the touch (TouchEvent::post).
    bool hasBallExit = false;
    Vec3 ballExitVel;
    float ballExitAngleDeg = 0.f;  // 0 = straight to the opponent goal, 90 = sideways, 180 = own goal
    float projectedY = 0.f;        // team-normalised (+ = opponent half): y + 0.5 * vy
    // Distance along the path per tick from GO (splits, personal-best ghost).
    std::vector<float> along;
    KickoffType type = KickoffType::Unknown;
};

struct KickoffRecorderParams {
    float dodgeDeadzone = 0.5f;  // PlayerProfile::dodgeDeadzone
    float cancelPitch = 0.5f;    // stick back threshold
    float cancelWindow = 0.25f;  // s after the dodge
    int maxSamples = 1200;       // 10 s of along samples
};

class KickoffRecorder {
public:
    void begin(KickoffSpawn spawn, KickoffTeam team, int64_t goFrame, const KickoffRecorderParams& p = {});
    void reset() { started_ = false; }
    bool started() const { return started_; }
    bool touched() const { return m_.touched; }
    int64_t goFrame() const { return goFrame_; }
    int64_t lastFrame() const { return lastFrame_; }
    const KickoffMetrics& metrics() const { return m_; }
    const KickoffPath& path() const { return path_; }
    float timeOf(int64_t frame) const { return static_cast<float>(frame - goFrame_) * kTickDt; }

    // One tick (frames strictly increasing; earlier frames and frames before GO are ignored).
    void feed(const Snapshot& sn);
    // First touch of the attempt: `pre` = state at the start of the contact tick, `post` = after the hit.
    void touch(const Snapshot& pre, const Snapshot& post);
    // Arrival estimate when the attempt ends without a touch while the car was heading to the ball.
    void estimate(const Snapshot& now);
    // Finalises derived metrics (RMS, ratios, type). Safe to call several times.
    const KickoffMetrics& finish();

    static KickoffType classify(const KickoffMetrics& m);

private:
    void registerDodge(float t, float fwd, float side, int64_t frame);

    KickoffRecorderParams prm_{};
    KickoffMetrics m_{};
    KickoffPath path_{};
    Snapshot prev_{};
    bool havePrev_ = false, started_ = false, prevJump_ = false, prevDodging_ = false;
    bool airborneFromJump_ = false, usedSecond_ = false;
    int64_t goFrame_ = 0, lastFrame_ = -1, lastDodgeFrame_ = -1000;
    int devN_ = 0, boostTicks_ = 0, boostHeld_ = 0, boostHeldWithTank_ = 0;
    float devSq_ = 0.f, lastDodge_ = -1.f;
};

} // namespace rlt
