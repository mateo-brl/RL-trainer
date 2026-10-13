#pragma once
// Première touche: analysis and grading of every touch (type, speed match, contact point, post-touch
// control), session statistics with a contact heatmap, optional chat log (parity P21) and adaptive
// first-touch drills (freeplay only).

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/common/Adaptive.h"
#include "core/common/Module.h"
#include "core/common/Rng.h"
#include "core/modules/firsttouch/FirstTouchAnalysis.h"
#include "core/modules/firsttouch/FirstTouchConfig.h"
#include "core/modules/firsttouch/FirstTouchDrills.h"
#include "core/modules/firsttouch/FirstTouchStats.h"

namespace rlt {

class FirstTouchModule : public Module {
public:
    FirstTouchModule();

    const char* id() const override { return "firsttouch"; }
    const char* title() const override { return "Première touche"; }

    static void describeSettings(SettingsSchema& schema);

    void onSettingsChanged(const Settings& settings) override;
    void onProfileChanged(const PlayerProfile& profile) override;
    void onTick(const TickContext& ctx, Actions& out) override;
    void onTouch(const TouchEvent& touch, const TickContext& ctx, Actions& out) override;
    void onGameEvent(const GameEvent& ev, const TickContext& ctx, Actions& out) override;
    void onCommand(const Command& cmd, const TickContext& ctx, Actions& out) override;
    void onActivityStopped() override;
    void draw(const ViewContext& view, const DrawContext& ctx, DrawList& out) override;
    void save(Store& store) const override;
    void load(const Store& store) override;

    const FirstTouchConfig& config() const { return cfg_; }

    // ---- Read-only views for the UI tab (copy them under the engine lock) ---------------------------------
    struct LastTouchView {
        bool valid = false;
        ft::TouchType type = ft::TouchType::Ground;
        float time = 0.f;
        float score = 0.f;
        bool success = false;
        bool controlled = false;
        Verdict verdict = Verdict::None;
        std::string headline;
        float speedDiff = 0.f;      // uu/s, + = car faster
        float relSpeed = 0.f;
        float along = 0.f, cross = 0.f;
        float closingSpeed = 0.f;
        float separation = 0.f;
        float ballPop = 0.f;
        float popHeight = 0.f;
        float controlFraction = 0.f;
        float contactErrDeg = 0.f;
        float lateralOffset = 0.f;
        ft::ContactHeight height = ft::ContactHeight::Middle;
        ft::CarPart part = ft::CarPart::Nose;
        ft::SpeedZone zone = ft::SpeedZone::Good;
        float sSpeed = 0.f, sContact = 0.f, sControl = 0.f;
        bool fromHitInfo = false;
        bool inDrill = false;
        std::vector<Tip> tips;
    };

    enum class DrillPhase : int { Idle = 0, Place = 1, WaitTouch = 2, Evaluate = 3, Pause = 4 };

    struct DrillView {
        bool active = false;
        ft::DrillType type = ft::DrillType::Mix;     // requested drill
        ft::DrillType current = ft::DrillType::RollIn;  // concrete drill of the current rep
        DrillPhase phase = DrillPhase::Idle;
        float level = 0.f;           // level of the current rep
        int reps = 0, successes = 0;
        float timeLeft = 0.f;        // seconds before timeout (WaitTouch) or next rep (Pause)
        std::string instruction;
        std::string lastOutcome;
        bool lastSuccess = false;
    };

    struct DrillLevelView {
        float level = 0.f;           // 0..1
        float best = 0.f;
        int attempts = 0, successes = 0;  // all-time (staircase)
        float recentRate = 0.f;
        int sessionReps = 0, sessionSuccesses = 0;
    };

    struct DribbleView {
        bool active = false;
        int contacts = 0;
        float seconds = 0.f;
        float cadence = 0.f;         // new contacts per second in the current / last chain
    };

    const LastTouchView& lastTouch() const { return last_; }
    const ft::TouchStats& sessionStats() const { return session_; }
    const ft::TouchStats& totalStats() const { return total_; }
    std::vector<float> recentScores() const { return recentScores_.toVector(); }
    std::vector<ft::HeatPoint> heatmap() const { return heat_.toVector(); }
    DrillView drillStatus() const;
    DrillLevelView drillLevel(ft::DrillType t) const;
    DribbleView dribble() const;
    bool analysisPending() const { return pending_.window.active(); }
    SpeedUnit displayUnit() const { return unit_; }
    KeyStyle keyStyle() const { return keyStyle_; }  // button names style from "Ma config" (Xbox / PlayStation)
    std::string persistentTipCode() const;
    std::string trend() const;

    // Diagnosis/score parameters derived from the configuration (exposed for tests).
    ft::ScoreParams scoreParams() const;
    ft::DiagnosisParams diagnosisParams() const;

private:
    struct Pending {
        ft::ControlWindow window;
        ft::TouchType type = ft::TouchType::Ground;
        ft::TouchMetrics metrics;
        float time = 0.f;
        bool inDrill = false;
        float drillLevel = 0.f;
        ft::DrillType drillType = ft::DrillType::RollIn;
    };

    struct Chain {
        bool active = false;
        float start = 0.f;
        float last = 0.f;
        int contacts = 0;
    };

    struct Drill {
        bool active = false;
        ft::DrillType type = ft::DrillType::Mix;
        ft::DrillType current = ft::DrillType::RollIn;
        DrillPhase phase = DrillPhase::Idle;
        bool havePose = false;
        ft::DrillPose pose;
        Rng rng{0x853c49e6748fea9bULL};
        int64_t placeFrame = 0;
        float deadline = 0.f;
        float now = 0.f;
        float repLevel = 0.f;
        bool repInput = false;
        int reps = 0, successes = 0, idleReps = 0;
        std::string lastOutcome;
        bool lastSuccess = false;
    };

    // Analysis.
    void startPending(const TouchEvent& t, ft::TouchType type, const TickContext& ctx);
    void finalizePending(const TickContext& ctx, Actions& out);
    void cancelPending();
    void updateChain(float now, bool newContact);
    void endChain();
    bool detectTeleport(const Snapshot& s);
    void resetTransient();
    void refreshUnit();

    // Drills.
    void startDrill(ft::DrillType type, const TickContext& ctx, Actions& out);
    void stopDrill(Actions* out, const std::string& message);
    void tickDrill(const TickContext& ctx, Actions& out);
    void drillRepDone(bool success, float score, const std::vector<Tip>& tips, Actions& out);
    void recordDrillRep(bool success);
    void rebuildStaircases();

    // Drawing.
    void drawStatsPanel(const ViewContext& view, const DrawContext& ctx, DrawList& out) const;
    void drawHeatmap(float anchorX, float topY, bool anchorRight, float scale, DrawList& out) const;
    void drawDrillPanel(const ViewContext& view, const DrawContext& ctx, DrawList& out) const;
    void drawContactMarker(const ViewContext& view, const DrawContext& ctx, DrawList& out) const;
    Color scoreColor(float score) const;
    Color zoneColor(ft::SpeedZone z) const;

    FirstTouchConfig cfg_;
    bool profileMetric_ = true;
    SpeedUnit unit_ = SpeedUnit::Kph;
    KeyStyle keyStyle_ = KeyStyle::Auto;

    Pending pending_;
    LastTouchView last_;
    ft::TouchStats session_;
    ft::TouchStats total_;
    ft::Ring<float, 50> recentScores_;
    ft::Ring<ft::HeatPoint, 40> heat_;
    std::vector<std::string> recentTipCodes_;  // last 5 main tips (bandwidth feedback)
    Chain chain_;
    Chain lastChain_;
    int gradedSinceSave_ = 0;

    // Last contact marker (3D) and teleport detection.
    bool markerValid_ = false;
    Vec3 markerDir_;
    float markerTime_ = 0.f;
    float markerScore_ = -1.f;
    int64_t ignoreTouchesUntil_ = -1;  // touches whose pre state is at or before this frame predate a teleport
    bool havePrev_ = false;
    int64_t prevFrame_ = 0;
    Vec3 prevCarPos_, prevBallPos_;
    bool prevCarValid_ = false, prevBallValid_ = false;

    // Flip reset de-duplication.
    bool haveFlipReset_ = false;
    int64_t lastFlipResetFrame_ = 0;

    // Context.
    bool haveContext_ = false;
    GameMode mode_ = GameMode::Menu;
    std::string mapName_;
    MapCategory category_ = MapCategory::Standard;

    // Drills.
    Drill drill_;
    std::array<Staircase, ft::kDrillKinds> stairs_;
    std::array<float, ft::kDrillKinds> drillBest_{};
    std::array<int, ft::kDrillKinds> drillSessionReps_{};
    std::array<int, ft::kDrillKinds> drillSessionSuccesses_{};
    uint64_t startCounter_ = 0;
    PlayerProfile profile_;
};

} // namespace rlt
