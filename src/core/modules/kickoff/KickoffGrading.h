#pragma once
// Kickoff grade 0..100 (kickoff.md 4.2): 40 % time to the ball, 15 % contact speed, 15 % centred contact,
// 15 % outcome (duel against the ghost, or ball exit direction), 15 % execution (reaction, boost held, path,
// flip timings). Every criterion's point loss is attributed to a French tip; the top 3 are reported.
// The speedflip timings (jump, flip angle, cancel, landing) are only judged when the reference level asks for a
// speedflip (Pro and up) or when the kickoff was a real speedflip: below, a diagonal flip is a normal flip.

#include <string>
#include <vector>

#include "core/common/Grade.h"
#include "core/modules/kickoff/KickoffGhost.h"
#include "core/modules/kickoff/KickoffRecorder.h"

namespace rlt {

enum class KickoffTip : int {
    NoTouch = 0,
    LateArrival,
    NoBoostAtGo,
    SlowReaction,
    BoostReleased,
    EarlyFirstJump,
    LateFirstJump,
    SlowDodge,
    NoCancel,
    LateCancel,
    DodgeTooStraight,
    DodgeTooWide,
    LateLanding,
    PathDeviation,
    OffCenterContact,
    LowContactSpeed,
    BallToOwnHalf,
    FlipIntoBall,
    MissedPad,
    TrySpeedflip,
    GreatKickoff,
    Count
};
const char* kickoffTipText(KickoffTip t);    // French, imperative, plain words (HUD, chat, coach)
const char* kickoffTipDetail(KickoffTip t);  // the same advice with the measured thresholds (tab, detail lines)
const char* kickoffTipCode(KickoffTip t);  // stable "kickoff.<error>"

struct KickoffTipLoss {
    KickoffTip tip = KickoffTip::GreatKickoff;
    float loss = 0.f;  // points of the total this mistake cost
};

struct KickoffGradeWeights {
    float time = 0.40f, speed = 0.15f, contact = 0.15f, outcome = 0.15f, exec = 0.15f;
};

struct KickoffGrade {
    int total = 0;
    float sTime = 0.f, sSpeed = 0.f, sContact = 0.f, sOutcome = 0.f, sExec = 0.f;  // each 0..100
    float sReact = 0.f, sBoost = 0.f, sPath = 0.f, sFlip = 0.f;                    // execution parts
    bool speedflipAttempt = false;
    std::vector<KickoffTipLoss> tips;  // most important first, at most 3
};

// `duel` is null when no ghost took part (the outcome then comes from the ball exit direction).
KickoffGrade kickoffGrade(const KickoffMetrics& m, KickoffLevel target, const KickoffDuelResult* duel,
                          const KickoffGradeWeights& w = {});

// ---- Split times ---------------------------------------------------------------------------------------------
struct KickoffSplit {
    float fraction = 0.f;
    bool reached = false;
    float playerTime = -1.f;  // s from GO
    float refTime = 0.f;      // reference (ghost) time at the same distance
    float delta = 0.f;        // > 0: player slower
};
// Time (s from GO) at which the per-tick `along` samples first reach distance `d`, -1 when never.
float kickoffTimeAtDistance(const std::vector<float>& along, float d);
// Splits at 25/50/75/100 % of the path against a reference ghost (the 100 % split uses the touch time).
std::vector<KickoffSplit> kickoffSplits(const KickoffMetrics& m, const KickoffGhost& ref);

// ---- AttemptResult -------------------------------------------------------------------------------------------
std::string kickoffAttemptTitle(KickoffSpawn s);  // "Kickoff (diagonale gauche)"
std::string kickoffSkillId(KickoffSpawn s);       // "kickoff.diagonal"
AttemptResult kickoffMakeResult(const KickoffMetrics& m, const KickoffGrade& g, const KickoffDuelResult* duel,
                                KickoffLevel target, float gameTime);

} // namespace rlt
