#include "core/modules/maps/MapRun.h"

namespace rlt {

namespace {
constexpr float kTeleportMaxGap = 0.05f;  // s: only compare positions of consecutive ticks
constexpr float kTimerZero = 0.05f;       // s: a map timer below this reads as "just reset"
} // namespace

const char* runSourceLabel(RunSource s) {
    switch (s) {
    case RunSource::Kismet: return "variables de la map";
    case RunSource::Own: return "chrono automatique";
    case RunSource::None: break;
    }
    return "aucun";
}

bool RunTracker::ownStartActive() const { return opt_.ownTiming && (!hasProfile_ || !profile_.hasKismetStart()); }

void RunTracker::setProfile(const KismetProfile* p) {
    hasProfile_ = p != nullptr;
    profile_ = p ? *p : KismetProfile{};
    hasPrevStart_ = hasPrevLevel_ = hasPrevCounter_ = hasPrevTimer_ = false;
    prevTimer_.clear();
    prevTimerSeconds_ = -1.f;
    timerAnchorPending_ = false;
    hasLevel_ = false;
    hasMapTimer_ = false;
    mapTimer_.clear();
    hasDeaths_ = false;
    if (running() && source_ == RunSource::Own && !ownStartActive()) endRun(now_, false, true);
    // A run that goes on under a new profile missed the splits before this point: its split indices no
    // longer line up with the personal best (and must not become the record's splits).
    if (running()) partialSplits_ = true;
    if (phase_ == RunPhase::Armed && !ownStartActive()) phase_ = RunPhase::Idle;
}

void RunTracker::setPersonalBest(const std::vector<float>& splits, float total, bool hasTotal) {
    pbSplits_ = splits;
    pbTotal_ = total;
    hasPbTotal_ = hasTotal;
}

void RunTracker::clear() {
    const RunOptions keep = opt_;
    *this = RunTracker{};
    opt_ = keep;
}

void RunTracker::arm(float now, bool clean) {
    if (running()) return;
    if (!ownStartActive()) {
        phase_ = RunPhase::Idle;
        return;
    }
    phase_ = RunPhase::Armed;
    armTime_ = now;
    armClean_ = clean;
    armAnchored_ = false;
    sawRest_ = false;
}

void RunTracker::beginRun(float startAt, RunSource src, bool clean) {
    if (running()) endRun(now_, false);
    phase_ = RunPhase::Running;
    source_ = src;
    start_ = startAt;
    eligible_ = clean;
    cleanStart_ = clean;
    partialSplits_ = false;
    timerAnchorPending_ = false;
    splits_.clear();
    resets_ = 0;
    respawns_ = 0;
    deathsAtStart_ = mapDeaths_;
    lastSplitAt_ = -1.f;
    RunEvent e;
    e.kind = RunEvent::Kind::Started;
    e.level = level_;
    events_.push_back(e);
}

void RunTracker::endRun(float now, bool finished, bool cancelled, bool byReset) {
    if (!running()) return;
    const float t = std::max(0.f, now - start_);
    if (!cancelled) {
        last_.valid = true;
        last_.finished = finished;
        last_.recordEligible = eligible_;
        last_.cleanStart = cleanStart_;
        last_.partialSplits = partialSplits_;
        last_.endedByReset = byReset;
        last_.source = source_;
        last_.time = t;
        last_.endedAt = now;
        last_.splits = splits_;
        last_.level = level_;
        last_.hasLevel = hasLevel_;
        last_.deaths = deathsInRun();
        last_.hasDeaths = hasDeaths_;
        last_.resets = resets_;
        last_.respawns = respawns_;
    }
    RunEvent e;
    e.kind = cancelled ? RunEvent::Kind::Cancelled : (finished ? RunEvent::Kind::Finished : RunEvent::Kind::Aborted);
    e.time = t;
    e.level = level_;
    events_.push_back(e);
    phase_ = RunPhase::Idle;
}

void RunTracker::addSplit(int level, float now, bool skipped) {
    if (skipped) eligible_ = false;
    RunSplit s;
    s.index = static_cast<int>(splits_.size()) + 1;
    s.level = level;
    s.time = std::max(0.f, now - start_);
    if (!partialSplits_ && static_cast<size_t>(s.index) <= pbSplits_.size()) {
        s.delta = s.time - pbSplits_[static_cast<size_t>(s.index) - 1];
        s.hasDelta = true;
    }
    if (splits_.size() < kMaxSplits) splits_.push_back(s);
    lastSplitAt_ = now;
    RunEvent e;
    e.kind = RunEvent::Kind::Split;
    e.time = s.time;
    e.level = level;
    e.splitIndex = s.index;
    e.delta = s.delta;
    e.hasDelta = s.hasDelta;
    events_.push_back(e);
}

void RunTracker::abort(float now) {
    if (running()) endRun(now, false);
    if (ownStartActive()) arm(now, true);
}

// ---- Ticks: own timing start, teleports ----------------------------------------------------------------
void RunTracker::onTick(const Snapshot& s) {
    if (!s.car.valid) {
        hasLastPos_ = false;
        return;
    }
    const float now = s.time;
    now_ = now;
    if (hasLastPos_ && now - lastPosTime_ <= kTeleportMaxGap && now >= lastPosTime_ &&
        distance(s.car.pos, lastPos_) > opt_.teleportDistance) {
        if (phase_ == RunPhase::Running) ++respawns_;
        else if (phase_ == RunPhase::Armed) arm(now, armClean_);
        else if (ownStartActive()) arm(now, true);  // respawn after a finished run: ready for the next one
    }
    lastPos_ = s.car.pos;
    lastPosTime_ = now;
    hasLastPos_ = true;
    if (s.paused) return;

    if (phase_ == RunPhase::Armed && ownStartActive()) {
        const float spd = speed(s.car);
        if (!armAnchored_) {
            // First tick since arming. An arm time from before the ticks resumed (map entry: the engine has no
            // snapshot yet, module re-enabled) or from another time base (new level) is anchored here, so a
            // run never starts in the past. A car already moving at that point started unseen: no record.
            if (now < armTime_ || now - armTime_ > opt_.armAnchorGap) {
                armTime_ = now;
                if (spd >= opt_.startSpeed) armClean_ = false;
            }
            armAnchored_ = true;
        }
        if (spd < opt_.startSpeed) sawRest_ = true;
        else if (sawRest_) beginRun(now, RunSource::Own, armClean_);
        else if (now - armTime_ >= opt_.armGrace) beginRun(armTime_, RunSource::Own, armClean_);
    }
}

// ---- Kismet: map-driven start / splits / finish --------------------------------------------------------
bool RunTracker::finishOnLevel(int lvl, int prev) const {
    const int fin = profile_.finalLevel;
    if (fin <= 0) return false;
    if (lvl > fin && prev <= fin) return true;                   // past the last level
    return profile_.finishOnReach && lvl >= fin && prev < fin;   // counters: reaching the last ring
}

void RunTracker::onKismet(const std::vector<KismetVar>& vars, float now) {
    if (!hasProfile_) return;
    now_ = now;
    const KismetProfile& p = profile_;

    bool start = false;
    const bool hasStart = kismetBool(vars, p.startBool, start);
    int lvl = 0;
    const bool hasLvl = kismetInt(vars, p.levelInt, lvl);
    int cnt = 0;
    const bool hasCnt = kismetInt(vars, p.counterInt, cnt);
    const KismetVar* timerVar = findKismetVar(vars, p.timerString);
    const bool hasTmr = timerVar && timerVar->type == KismetVar::Type::String;
    int deaths = 0;
    if (kismetInt(vars, p.deathsInt, deaths)) {
        mapDeaths_ = deaths;
        if (!hasDeaths_) deathsAtStart_ = deaths;
        if (running() && mapDeaths_ < deathsAtStart_) deathsAtStart_ = mapDeaths_;  // counter reset by the map
        hasDeaths_ = true;
    }
    if (hasLvl) {
        level_ = lvl;
        hasLevel_ = true;
    } else if (hasCnt) {
        level_ = cnt;
        hasLevel_ = true;
    }

    // (1) Run start signals.
    if (hasStart && hasPrevStart_ && start && !prevStart_) beginRun(now, RunSource::Kismet, true);
    if (hasCnt && hasPrevCounter_ && cnt != prevCounter_) {
        if (cnt == 1 && (prevCounter_ == 0 || prevCounter_ > 1)) {
            beginRun(now, RunSource::Kismet, true);  // first ring of a new run
        } else if (running()) {
            if (cnt > prevCounter_) {
                addSplit(cnt, now, cnt > prevCounter_ + 1);
                if (finishOnLevel(cnt, prevCounter_)) endRun(now, true);
            } else {
                endRun(now, false);  // counter reset by the map
            }
        }
    }
    if (hasTmr) {
        const std::string& tmr = timerVar->s;
        if (!hasPrevTimer_ || tmr != prevTimer_) {
            float secs = -1.f;
            const bool parsed = parseTimerText(tmr, secs);
            if (hasPrevTimer_ && parsed) {
                if (secs < kTimerZero) {
                    beginRun(now, RunSource::Kismet, true);
                    timerAnchorPending_ = true;
                } else if (running() && source_ == RunSource::Kismet && prevTimerSeconds_ >= 0.f &&
                           secs + 1.f < prevTimerSeconds_) {
                    endRun(now, false);  // the map's timer went backwards without passing zero: reset
                } else if (timerAnchorPending_ && running() && source_ == RunSource::Kismet) {
                    // First change after the zero: the map may hold 0:00 until the player crosses its start
                    // line, so the run really started `secs` ago (never earlier than the zero itself).
                    timerAnchorPending_ = false;
                    if (splits_.empty()) start_ = std::max(start_, now - secs);
                }
            }
            prevTimerSeconds_ = parsed ? secs : -1.f;
            prevTimer_ = tmr;
            mapTimer_ = tmr.size() > 32 ? tmr.substr(0, 32) : tmr;
            hasMapTimer_ = true;
        }
    }

    // (2) Level changes while running.
    if (hasLvl && hasPrevLevel_ && lvl != prevLevel_ && running()) {
        if (lvl > prevLevel_) {
            addSplit(lvl, now, lvl > prevLevel_ + 1);
            if (finishOnLevel(lvl, prevLevel_)) endRun(now, true);
        } else if (p.finishOnWrap && p.finalLevel > 0 && prevLevel_ >= p.finalLevel && lvl <= 1) {
            endRun(now, true);  // course completed, the map sent the player back to level 1
        } else if (lvl <= 1) {
            endRun(now, false);  // course restarted
            if (ownStartActive()) arm(now, true);
        } else {
            eligible_ = false;  // went back to an earlier level (practice): no record for this run
        }
    }

    // (3) Start signal dropped.
    if (hasStart && hasPrevStart_ && !start && prevStart_ && running() && source_ == RunSource::Kismet) {
        const bool atEnd = p.finishOnFall || (p.finalLevel > 0 && hasLevel_ && level_ >= p.finalLevel);
        endRun(now, atEnd);
    }

    if (hasStart) {
        prevStart_ = start;
        hasPrevStart_ = true;
    }
    if (hasLvl) {
        prevLevel_ = lvl;
        hasPrevLevel_ = true;
    }
    if (hasCnt) {
        prevCounter_ = cnt;
        hasPrevCounter_ = true;
    }
    if (hasTmr) hasPrevTimer_ = true;
}

// ---- Discrete events -----------------------------------------------------------------------------------
void RunTracker::onReset(float now) {
    now_ = now;
    hasLastPos_ = false;  // the reset teleports the car: not a respawn
    if (running()) {
        if (source_ == RunSource::Own) {
            // "Goal or reset" mode: the reset ends the attempt (counted as finished) but the time cannot be a
            // record, otherwise the shortest attempt the player gave up on would become it.
            endRun(now, opt_.resetFinishes, false, opt_.resetFinishes);
            arm(now, true);
        } else {
            ++resets_;  // Kismet-timed: the map respawns the player at its checkpoint, the run goes on
        }
        return;
    }
    arm(now, true);
}

void RunTracker::onGoal(float now) {
    now_ = now;
    if (!running()) return;
    if (source_ == RunSource::Own) {
        endRun(now, true);
        return;
    }
    if (hasProfile_ && profile_.finishOnGoal &&
        (profile_.finalLevel <= 0 || (hasLevel_ && level_ >= profile_.finalLevel)))
        endRun(now, true);
}

void RunTracker::onUnload(float now) {
    if (running()) endRun(now, false);
    phase_ = RunPhase::Idle;
    hasPrevStart_ = hasPrevLevel_ = hasPrevCounter_ = hasPrevTimer_ = false;
    prevTimer_.clear();
    prevTimerSeconds_ = -1.f;
    timerAnchorPending_ = false;
    hasLevel_ = false;
    hasMapTimer_ = false;
    mapTimer_.clear();
    hasDeaths_ = false;
    hasLastPos_ = false;
}

bool RunTracker::liveDelta(float now, float& delta) const {
    if (!running()) return false;
    const float e = elapsed(now);
    const size_t next = splits_.size();
    if (!partialSplits_ && next < pbSplits_.size()) {
        if (e > pbSplits_[next]) {
            delta = e - pbSplits_[next];
            return true;
        }
        return false;
    }
    if (hasPbTotal_ && e > pbTotal_) {
        delta = e - pbTotal_;
        return true;
    }
    return false;
}

} // namespace rlt
