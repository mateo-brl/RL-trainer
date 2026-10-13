#pragma once
// Run timing on custom maps: start / splits / finish from the map's Kismet variables when a timing profile
// is known, otherwise own timing (start when the car moves after a reset, stop on goal / reset / unload).
// Time base: Snapshot::time (physics frames / 120), so pauses and hitches never count.

#include <algorithm>
#include <string>
#include <vector>

#include "core/common/Context.h"
#include "core/common/Snapshot.h"
#include "core/modules/maps/KismetProfiles.h"

namespace rlt {

enum class RunPhase : int { Idle = 0, Armed = 1, Running = 2 };
enum class RunSource : int { None = 0, Kismet = 1, Own = 2 };

struct RunSplit {
    int index = 0;        // 1-based split number
    int level = 0;        // level / counter value reached
    float time = 0.f;     // seconds since the run start
    float delta = 0.f;    // vs the personal-best split with the same index
    bool hasDelta = false;
};

struct RunEvent {
    enum class Kind : int { Started, Split, Finished, Aborted, Cancelled };
    Kind kind = Kind::Started;
    float time = 0.f;     // seconds since the run start (0 for Started)
    int level = 0;
    int splitIndex = 0;   // Split only
    float delta = 0.f;    // Split only
    bool hasDelta = false;
};

// What happened during the last ended run (finished or aborted).
struct RunSummary {
    bool valid = false;
    bool finished = false;
    bool recordEligible = true;   // clean start, no skipped levels
    RunSource source = RunSource::None;
    float time = 0.f;             // seconds
    float endedAt = 0.f;          // game time
    std::vector<RunSplit> splits;
    int level = 0;                // last level / counter value reached
    bool hasLevel = false;
    int deaths = 0;               // map's own death counter during the run (Kismet)
    bool hasDeaths = false;
    int resets = 0;               // reset presses during a Kismet-timed run (respawns at checkpoints)
    int respawns = 0;             // teleports seen during the run
};

struct RunOptions {
    bool ownTiming = true;          // time runs ourselves when the map gives no start signal
    float startSpeed = 100.f;       // uu/s: own timing starts when the car goes faster than this
    bool resetFinishes = false;     // own timing: a reset ends the run as finished (not aborted)
    float armGrace = 0.5f;          // s: car never at rest after arming -> the run starts at the arm time
    float teleportDistance = 500.f; // uu between two consecutive ticks = teleport / respawn
};

class RunTracker {
public:
    static constexpr size_t kMaxSplits = 256;

    void setOptions(const RunOptions& o) { opt_ = o; }
    const RunOptions& options() const { return opt_; }

    // Timing profile (copied). nullptr = own timing only. Forgets previous Kismet values (no edge on the
    // first sample). An own-timed run in progress is cancelled when the new profile has a Kismet start.
    void setProfile(const KismetProfile* p);
    bool hasProfile() const { return hasProfile_; }
    const KismetProfile& profile() const { return profile_; }

    // Personal best of the current map, for split deltas.
    void setPersonalBest(const std::vector<float>& splits, float total, bool hasTotal);

    // Map changed: idle, no profile, no values.
    void clear();

    // Ready for an own-timed run (after a reset / map load). No effect when the map gives the start.
    void arm(float now, bool clean = true);
    void onTick(const Snapshot& s);
    void onKismet(const std::vector<KismetVar>& vars, float now);
    void onReset(float now);
    void onGoal(float now);
    void onUnload(float now);
    void abort(float now);   // player asked to stop the current run

    const std::vector<RunEvent>& events() const { return events_; }
    void clearEvents() { events_.clear(); }

    RunPhase phase() const { return phase_; }
    bool running() const { return phase_ == RunPhase::Running; }
    RunSource source() const { return source_; }
    bool ownStartActive() const;
    float startTime() const { return start_; }
    float elapsed(float now) const { return running() ? std::max(0.f, now - start_) : 0.f; }
    bool recordEligible() const { return eligible_; }

    bool hasLevel() const { return hasLevel_; }
    int level() const { return level_; }
    int finalLevel() const { return hasProfile_ ? profile_.finalLevel : 0; }
    bool hasMapTimer() const { return hasMapTimer_; }
    const std::string& mapTimer() const { return mapTimer_; }
    bool hasDeaths() const { return hasDeaths_; }
    int deathsInRun() const { return running() && hasDeaths_ ? std::max(0, mapDeaths_ - deathsAtStart_) : 0; }
    int resetsInRun() const { return resets_; }

    const std::vector<RunSplit>& splits() const { return splits_; }
    const RunSummary& last() const { return last_; }
    float lastSplitAt() const { return lastSplitAt_; }  // game time of the newest split (-1 when none)
    float lastEndAt() const { return last_.valid ? last_.endedAt : -1.f; }

    // Behind the personal-best pace: elapsed time past the PB split expected next (or past the PB total).
    bool liveDelta(float now, float& delta) const;

private:
    void beginRun(float startAt, RunSource src, bool clean);
    void endRun(float now, bool finished, bool cancelled = false);
    void addSplit(int level, float now, bool skipped);
    bool finishOnLevel(int lvl, int prev) const;

    RunOptions opt_;
    KismetProfile profile_;
    bool hasProfile_ = false;

    RunPhase phase_ = RunPhase::Idle;
    RunSource source_ = RunSource::None;
    float start_ = 0.f;
    bool eligible_ = true;
    std::vector<RunSplit> splits_;
    int resets_ = 0;
    int respawns_ = 0;
    float lastSplitAt_ = -1.f;

    // Arming (own timing)
    float armTime_ = 0.f;
    bool armClean_ = true;
    bool sawRest_ = false;

    // Teleport detection
    Vec3 lastPos_;
    float lastPosTime_ = 0.f;
    bool hasLastPos_ = false;
    float now_ = 0.f;

    // Kismet values (previous sample)
    bool hasPrevStart_ = false, prevStart_ = false;
    bool hasPrevLevel_ = false;
    int prevLevel_ = 0;
    bool hasPrevCounter_ = false;
    int prevCounter_ = 0;
    bool hasPrevTimer_ = false;
    std::string prevTimer_;
    float prevTimerSeconds_ = -1.f;

    bool hasLevel_ = false;
    int level_ = 0;
    bool hasMapTimer_ = false;
    std::string mapTimer_;
    bool hasDeaths_ = false;
    int mapDeaths_ = 0;
    int deathsAtStart_ = 0;

    std::vector<float> pbSplits_;
    float pbTotal_ = 0.f;
    bool hasPbTotal_ = false;

    RunSummary last_;
    std::vector<RunEvent> events_;
};

const char* runSourceLabel(RunSource s);  // French

} // namespace rlt
