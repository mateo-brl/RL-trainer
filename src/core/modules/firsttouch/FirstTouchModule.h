#pragma once
// Première touche: analysis and grading of every touch (type, speed match, contact point, post-touch
// control), session statistics with a contact heatmap, optional chat log (parity P21) and adaptive
// first-touch drills (freeplay only).

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/common/Adaptive.h"
#include "core/common/HangingBall.h"
#include "core/common/Hold.h"
#include "core/common/Module.h"
#include "core/common/Rng.h"
#include "core/common/Steps.h"
#include "core/modules/firsttouch/FirstTouchAnalysis.h"
#include "core/modules/firsttouch/FirstTouchConfig.h"
#include "core/modules/firsttouch/FirstTouchDrills.h"
#include "core/modules/firsttouch/FirstTouchFull.h"
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

    // Ready: car and ball held still before the release (get-ready hold, fix 1); the first ball of a drill waits for
    // the throttle, then counts down.
    enum class DrillPhase : int { Idle = 0, Place = 1, WaitTouch = 2, Evaluate = 3, Pause = 4, Ready = 5 };

    struct DrillView {
        bool active = false;
        bool full = false;                           // "parcours complet"
        ft::DrillType type = ft::DrillType::Mix;     // requested drill
        ft::DrillType current = ft::DrillType::RollIn;  // concrete drill of the current rep
        DrillPhase phase = DrillPhase::Idle;
        float level = 0.f;           // level of the current rep
        int playerLevel = 1;         // the same, as shown to the player (1..10)
        ft::DrillCriteria criteria;  // what the current rep must reach
        int reps = 0, successes = 0;
        float timeLeft = 0.f;        // seconds before timeout (WaitTouch) or next rep (Pause)
        std::string instruction;
        std::string lastOutcome;     // verdict of the last rep ("Réussi ! 72/100", "Temps écoulé.")
        std::string lastTip;         // the one tip of the last failed rep, in words ("" after a success)
        bool lastSuccess = false;
        std::string readyLine;       // "Appuie sur accélérer quand tu es prêt" / "Départ dans 2" ("" when not holding)
        std::string nextBall;        // Mix: name of the next ball
        std::vector<std::string> steps;  // parcours complet: the step lines ("2/4 Ralentis : OK")
        int matchBall = 0, matchPassed = 0;  // séquence de match: balls played / passed in the current sequence
        bool waitingWall = false;    // Chute du mur without car reset: no side wall close enough yet
    };

    struct DrillLevelView {
        float level = 0.f;           // 0..1
        float best = 0.f;
        int attempts = 0, successes = 0;  // all-time (staircase)
        float recentRate = 0.f;
        int sessionReps = 0, sessionSuccesses = 0;
        bool mixUnlocked = false;    // the drill is in the Mix pool
        float fullLevel = 0.f;       // parcours complet (0..1)
        int fullAttempts = 0, fullSuccesses = 0;
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
        bool full = false;                 // parcours complet
        ft::DrillType type = ft::DrillType::Mix;
        ft::DrillType current = ft::DrillType::RollIn;
        DrillPhase phase = DrillPhase::Idle;
        bool havePose = false;
        ft::DrillPose pose;                // where the player started the drill
        ft::DrillPose repPose;             // pose of the current rep (anchored, or placed by the drill)
        Rng rng{0x853c49e6748fea9bULL};
        int64_t placeFrame = 0;
        float deadline = 0.f;
        float now = 0.f;
        float goTime = 0.f;
        float repLevel = 0.f;
        ft::DrillCriteria repCriteria;
        bool repInput = false;
        int reps = 0, successes = 0, idleReps = 0;
        std::string lastOutcome;
        std::string lastTip;
        bool lastSuccess = false;
        // Get-ready hold.
        bool firstRep = true;
        bool carHeld = false;              // the car is placed / held by the drill this rep
        ReadyPhase ready;
        HoldPose hold;
        ft::DrillSpawn spawn;
        float wallSide = 1.f;              // Chute du mur: side of the next rep (alternates)
        bool waitingWall = false;
        // What is drawn: the predicted path (hold), the landing / meeting circle.
        ft::BallTrack path;
        bool showLanding = false;
        Vec3 landing;
        bool showMeet = false;
        Vec3 meet;
        // Hanging ball of "Balle haute" (niveaux 1 to 6).
        HangingBall hang;
        ft::HangRelease hangMode = ft::HangRelease::None;
        float hangVz = 0.f;
        float underSince = -1.f;
        bool prevJump = false;
        bool underBall = false;            // ball over the front half of the roof on the last tick
        // Mix: next ball and the séquence de match.
        ft::DrillType nextType = ft::DrillType::RollIn;
        bool haveNext = false;
        int matchBall = 0, matchPassed = 0;
        // Parcours complet.
        ft::FullSetup fullSetup;
        ft::FullState fullState;
        StepTracker steps;
        bool touchPending = false;         // first touch seen by onTouch, handled on the next onTick
        ft::TouchMetrics touchMetrics;
        Snapshot touchPre;
        // Live facts for the HUD.
        float ballDist = 1e9f;             // hitbox centre to ball centre (uu)
        float speedDiff = 0.f;             // roulante qui s'éloigne: car speed - ball speed along its path
        int floorBounces = 0;
        float prevBallVz = 0.f;
        bool havePrevBall = false;
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
    void startDrill(ft::DrillType type, bool full, const TickContext& ctx, Actions& out);
    void stopDrill(Actions* out, const std::string& message);
    void tickDrill(const TickContext& ctx, Actions& out);
    void placeRep(const TickContext& ctx, Actions& out);
    void placeFullRep(const TickContext& ctx, Actions& out);
    void placeMatchBall(const TickContext& ctx, Actions& out);
    void launchRep(const Snapshot& s, Actions& out);
    void tickHanging(const Snapshot& s, Actions& out);
    void tickFull(const TickContext& ctx, Actions& out);
    void updateLiveFacts(const Snapshot& s);
    void drillRepDone(bool success, float score, const std::string& tip, Actions& out);
    void fullRepDone(bool success, Actions& out);
    void matchBallDone(bool success, const std::string& outcome, const std::string& tip, Actions& out);
    ft::DrillCriteria criteriaFor(float level) const;
    void recordDrillRep(bool success);
    void rebuildStaircases();
    std::array<float, ft::kDrillKinds> mixWeights(bool matchOnly, const ft::DrillPose* pose) const;
    std::string currentPanelLine(const DrawContext& ctx) const;

    // Drawing.
    // Compact HUD (ctx.compactHud): at most 3 main lines per panel, secondary lines flagged `detail`, no heatmap,
    // and no stats panel while a drill runs (the drill panel already shows the verdict and the tip).
    void drawStatsPanel(const ViewContext& view, const DrawContext& ctx, DrawList& out) const;
    void drawHeatmap(float anchorX, float topY, bool anchorRight, float scale, DrawList& out) const;
    void drawDrillPanel(const ViewContext& view, const DrawContext& ctx, DrawList& out) const;
    void drawDrillWorld(const DrawContext& ctx, DrawList& out) const;
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
    std::array<Staircase, ft::kDrillKinds + 1> fullStairs_;  // parcours complet of each drill, + the séquence de match
    int64_t touchFrame_ = -1;  // frame of the last car touch event (hanging ball: never re-placed on that tick)
    std::array<float, ft::kDrillKinds> drillBest_{};
    std::array<int, ft::kDrillKinds> drillSessionReps_{};
    std::array<int, ft::kDrillKinds> drillSessionSuccesses_{};
    uint64_t startCounter_ = 0;
    PlayerProfile profile_;
};

} // namespace rlt
