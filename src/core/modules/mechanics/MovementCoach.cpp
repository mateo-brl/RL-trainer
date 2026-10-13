#include "core/modules/mechanics/MovementCoach.h"

namespace rlt::mech {

void MovementCoach::reset() {
    mt_.clear();
    dt_.reset();
    if (!ko_.manual()) ko_.clear();
    finished_.clear();
    wd_ = WavedashDetector{};
    chain_.reset();
    sf_ = SpeedFlipDetector{};
    hf_ = HalfFlipDetector{};
    fc_ = FlipCancelDetector{};
    combo_.reset();
    fa_ = FastAerialDetector{};
    st_ = StallDetector{};
    spin_.reset();
    rec_.reset();
    ps_.reset();
    sm_.clear();
}

void MovementCoach::step(const Snapshot& s, std::vector<MechGrade>& out) {
    // Previous tick (the tracker keeps its own copy; take it before stepping).
    if (mt_.hasPrev()) prev_ = mt_.prev();
    else prev_ = s;
    const TickEvents ev = mt_.step(s);
    if (ev.reset) {
        dt_.reset();
        prev_ = s;
    }
    const Ctx c{s, prev_, ev, mt_, dt_, cfg};
    if (ev.valid && ev.firstJump) dt_.noteFirstJump(s);
    ko_.step(c);
    finished_.clear();
    dt_.step(s, prev_, ev, mt_, finished_);
    const size_t first = out.size();

    // The fast aerial detector runs first so that the dodge it claims is never graded twice.
    fa_.onTick(c, out);
    for (const DodgeRecord& r : finished_) {
        if (r.tick == fa_.claimedDodgeTick()) continue;
        if (st_.onDodge(r, c, out)) continue;
        if (wd_.onDodge(r, c, out)) continue;
        if (sf_.onDodge(r, c, ko_, out)) continue;
        if (hf_.onDodge(r, c, out)) continue;
        fc_.onDodge(r, c, out);
    }
    sf_.onTick(c, ko_, out);
    wd_.onTick(c, out);
    st_.onTick(c, out);
    spin_.onTick(c, out);
    rec_.onTick(c, out);
    ps_.onTick(c, out);
    sm_.onTick(c, out);

    // Grades produced this tick feed the chain and combo detectors (copies: `out` may grow).
    const size_t n = out.size();
    for (size_t i = first; i < n; ++i) {
        const MechGrade g = out[i];
        chain_.onGrade(g, c, out);
        combo_.onGrade(g, out);
    }
    chain_.onTick(c, out);

    // Recovery arbitration: a dash that ended a long aerial phase is part of the recovery.
    const bool dashFocus = cfg.focus == Focus::Wavedash || cfg.focus == Focus::ZapDash ||
                           cfg.focus == Focus::ChainDash || cfg.focus == Focus::WallDash;
    for (size_t i = first; i < out.size();) {
        const MechGrade& g = out[i];
        if (g.recoveryLanding && isDash(g.mech)) {
            if (cfg.recoveryEnabled && !dashFocus && rec_.absorbDash(g)) {
                out.erase(out.begin() + static_cast<long>(i));
                continue;
            }
            rec_.skipLanding(g.tickEnd);
        }
        ++i;
    }
}

void MovementCoach::flushPending(const Snapshot& s, std::vector<MechGrade>& out, bool boostWindow) {
    const TickEvents ev;  // invalid: detectors only flush
    const Ctx c{s, s, ev, mt_, dt_, cfg};
    const size_t first = out.size();
    sf_.flush(out);
    const size_t n = out.size();
    for (size_t i = first; i < n; ++i) {
        const MechGrade g = out[i];
        combo_.onGrade(g, out);
    }
    chain_.flush(c, out);
    if (boostWindow) sm_.flush(c, out, true);
}

} // namespace rlt::mech
