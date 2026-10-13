#include "core/modules/ballmech/Arbiter.h"

#include <algorithm>

namespace rlt::bm {

namespace {
constexpr size_t kMaxHeld = 16;
constexpr size_t kMaxRecent = 32;
} // namespace

const char* familyId(Family f) {
    switch (f) {
    case Family::FlipReset: return "flip_reset";
    case Family::Ceiling: return "ceiling_shot";
    case Family::DoubleTap: return "double_tap";
    case Family::Pinch: return "pinch";
    case Family::Flick: return "flick";
    case Family::AirDribble: return "air_dribble";
    case Family::Aerial: return "aerial_touch";
    case Family::PowerShot: return "power_shot";
    default: return "";
    }
}

void Arbiter::reset() {
    held_.clear();
    recent_.clear();
}

bool Arbiter::overlaps(const MechGrade& a, const MechGrade& b) {
    return a.tStart - kSlack <= b.tEnd && b.tStart - kSlack <= a.tEnd;
}

bool Arbiter::beats(const MechGrade& a, const MechGrade& b) {
    if (a.weak != b.weak) return !a.weak;
    return static_cast<int>(a.family) < static_cast<int>(b.family);
}

void Arbiter::submit(MechGrade g, float now) {
    recent_.erase(std::remove_if(recent_.begin(), recent_.end(),
                                 [now](const Recent& r) { return now - r.at > kMemory || r.at > now; }),
                  recent_.end());
    for (const Recent& r : recent_) {
        if (r.family == g.family) continue;  // one detector reports one grade per attempt
        MechGrade probe;
        probe.family = r.family;
        probe.tStart = r.tStart;
        probe.tEnd = r.tEnd;
        probe.weak = r.weak;
        if (overlaps(g, probe) && !beats(g, probe)) return;  // already reported by a more specific mechanic
    }
    for (auto it = held_.begin(); it != held_.end();) {
        if (it->g.family != g.family && overlaps(g, it->g)) {
            if (beats(g, it->g)) {
                it = held_.erase(it);
                continue;
            }
            return;  // the held grade wins
        }
        ++it;
    }
    if (held_.size() >= kMaxHeld) held_.erase(held_.begin());
    held_.push_back({std::move(g), now});
}

void Arbiter::release(float now, const EngagedState& engaged, std::vector<MechGrade>& out) {
    for (size_t i = 0; i < held_.size();) {
        const MechGrade& h = held_[i].g;
        bool blocked = false;
        if (now - held_[i].since < kMaxHold && now >= held_[i].since) {
            for (int f = 0; f < kFamilyCount && !blocked; ++f) {
                if (!engaged.on[static_cast<size_t>(f)] || f == static_cast<int>(h.family)) continue;
                MechGrade probe;  // the pending attempt could still end strong
                probe.family = static_cast<Family>(f);
                if (!beats(probe, h)) continue;
                if (engaged.since[static_cast<size_t>(f)] - kSlack <= h.tEnd) blocked = true;
            }
        }
        if (blocked) {
            ++i;
            continue;
        }
        if (recent_.size() >= kMaxRecent) recent_.erase(recent_.begin());
        recent_.push_back({h.family, h.tStart, h.tEnd, now, h.weak});
        out.push_back(std::move(held_[i].g));
        held_.erase(held_.begin() + static_cast<long>(i));
    }
}

} // namespace rlt::bm
