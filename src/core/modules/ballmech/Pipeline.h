#pragma once
// Detection pipeline: turns the engine's tick history (plus touches and game events) into Frames 2 ticks after
// the fact (a TouchEvent is resolved 2 ticks late), feeds the enabled detectors and arbitrates their grades.
// Robustness: invalid car/ball, frame gaps, teleports (resets, drills) and paused ticks abort attempts in progress.

#include <array>
#include <cstdint>
#include <utility>
#include <vector>

#include "core/common/History.h"
#include "core/modules/ballmech/Arbiter.h"
#include "core/modules/ballmech/Detectors.h"

namespace rlt::bm {

class Pipeline {
public:
    static constexpr int kDelay = 2;           // ticks between a snapshot and its processing
    static constexpr int64_t kMaxGap = 12;     // frames without data before attempts are aborted
    static constexpr float kTeleport = 300.f;  // uu off the extrapolated position = teleport

    struct Acquisition {
        int64_t frame = 0;
        float time = 0.f;
        int source = 0;   // 1 game event, 2 wheels on the ball, 3 flip regained near the ball
    };
    struct Output {
        std::vector<MechGrade> released;
        std::vector<Acquisition> acquisitions;
        void clear() {
            released.clear();
            acquisitions.clear();
        }
    };

    Pipeline();

    void setParams(const DetectParams& p) { params_ = p; }
    void setBuildOptions(const BuildOptions& o) { opts_ = o; }
    void setEnabled(Family f, bool on);
    bool enabled(Family f) const { return enabled_[static_cast<size_t>(f)]; }

    // Map change / reload: forget everything, including pending marks and the processed frame counter.
    void restart();
    // Abort the attempts in progress (teleport, drill launch, reset shot). Held grades are kept.
    void resetDetectors();
    // Drop the grades waiting in the arbiter (a drill launch must not be resolved by earlier play).
    void clearHeld() { arbiter_.clearHeld(); }

    void addTouch(const TouchMark& t);
    void addSdkReset(int64_t frame);
    void addGoal() { goalPending_ = true; }
    // Frames up to `frame` are treated as a discontinuity (the drill moved the car / ball).
    void ignoreUntil(int64_t frame) { ignoreUntil_ = frame; }
    // The drill moved only the ball, on purpose, while an attempt runs (ceiling parcours: ball relaunched in front of
    // the falling car): up to `frame`, a ball jump is not a reset, the detectors keep their state.
    void allowBallMoveUntil(int64_t frame) { ballMoveUntil_ = frame; }
    // Double tap "read" mode at the next processed frame (drill sent the ball to the backboard).
    void armDoubleTapRead() { armDtRead_ = true; }
    // The drill placed the car on the ceiling: the ceiling detector starts in OnCeiling at the next processed frame
    // (insurance: the first frames after a launch are ignored, see ignoreUntil).
    void armCeiling() { armCeiling_ = true; }

    // Processes the snapshots of `h` that are now kDelay ticks old (oldest first).
    void process(const TickHistory& h, bool hasSnapshot, Output& out);
    // Feeds one snapshot with its neighbours (also used by tests).
    void feed(const Snapshot& s, const Snapshot* prev, const Snapshot* next1, const Snapshot* next2, Output& out);

    bool resetAvailable() const { return enabled(Family::FlipReset) && fr_.armed(); }
    FlickDetector& flick() { return flick_; }
    const Frame& lastFrame() const { return prevFrame_; }
    bool hasFrame() const { return hasPrevFrame_; }
    int64_t lastProcessed() const { return lastProcessed_; }
    size_t heldGrades() const { return arbiter_.heldCount(); }

private:
    Detector* detector(Family f);

    DetectParams params_;
    BuildOptions opts_;
    FrameBuilder builder_;
    Arbiter arbiter_;
    FlipResetDetector fr_;
    CeilingShotDetector ceiling_;
    DoubleTapDetector dt_;
    PinchDetector pinch_;
    FlickDetector flick_;
    AirDribbleDetector ad_;
    AerialDetector aerial_;
    PowerShotDetector power_;
    std::array<bool, kFamilyCount> enabled_{};

    Frame prevFrame_;
    bool hasPrevFrame_ = false;
    int64_t lastProcessed_ = INT64_MIN;
    int64_t ignoreUntil_ = INT64_MIN;
    int64_t ballMoveUntil_ = INT64_MIN;
    bool goalPending_ = false;
    bool armDtRead_ = false;
    bool armCeiling_ = false;
    std::array<TouchMark, 8> marks_{};
    int markCount_ = 0;
    std::array<int64_t, 4> sdk_{};
    int sdkCount_ = 0;
};

} // namespace rlt::bm
