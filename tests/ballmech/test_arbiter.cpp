// Arbitration contract: one attempt reported once, by the most specific mechanic.
#include "core/modules/ballmech/Arbiter.h"
#include "tests/TestFramework.h"

using namespace rlt;
using namespace rlt::bm;

namespace {
MechGrade grade(Family f, float t0, float t1, bool weak = false) {
    MechGrade g;
    g.family = f;
    g.tStart = t0;
    g.tEnd = t1;
    g.weak = weak;
    return g;
}
EngagedState none() { return EngagedState{}; }
EngagedState engaged(Family f, float since) {
    EngagedState e;
    e.on[static_cast<size_t>(f)] = true;
    e.since[static_cast<size_t>(f)] = since;
    return e;
}
} // namespace

TEST(bm_arbiter_rules) {
    const MechGrade fr = grade(Family::FlipReset, 1.f, 2.f);
    const MechGrade ae = grade(Family::Aerial, 1.9f, 1.92f);
    CHECK(Arbiter::overlaps(fr, ae));
    CHECK(Arbiter::beats(fr, ae));
    CHECK(!Arbiter::beats(ae, fr));
    CHECK(!Arbiter::overlaps(grade(Family::Aerial, 3.f, 3.1f), fr));
    CHECK(Arbiter::overlaps(grade(Family::Aerial, 2.05f, 2.1f), fr));  // 0.1 s slack
    // Weak loses against strong, whatever the priority.
    const MechGrade weakFr = grade(Family::FlipReset, 1.f, 1.1f, true);
    const MechGrade strongPs = grade(Family::PowerShot, 1.f, 1.1f);
    CHECK(Arbiter::beats(strongPs, weakFr));
    CHECK(!Arbiter::beats(weakFr, strongPs));
}

TEST(bm_arbiter_specific_wins_same_frame) {
    Arbiter a;
    std::vector<MechGrade> out;
    a.submit(grade(Family::Aerial, 1.9f, 1.92f), 1.92f);
    a.submit(grade(Family::FlipReset, 1.f, 1.92f), 1.92f);
    a.release(1.92f, none(), out);
    CHECK_EQ(out.size(), 1u);
    if (!out.empty()) CHECK(out[0].family == Family::FlipReset);
    // Order does not matter.
    Arbiter b;
    out.clear();
    b.submit(grade(Family::FlipReset, 1.f, 1.92f), 1.92f);
    b.submit(grade(Family::Aerial, 1.9f, 1.92f), 1.92f);
    b.release(1.92f, none(), out);
    CHECK_EQ(out.size(), 1u);
    if (!out.empty()) CHECK(out[0].family == Family::FlipReset);
}

TEST(bm_arbiter_holds_while_specific_engaged) {
    Arbiter a;
    std::vector<MechGrade> out;
    // An aerial shot while the double tap detector waits for the bounce: held.
    a.submit(grade(Family::Aerial, 1.0f, 1.02f), 1.02f);
    a.release(1.02f, engaged(Family::DoubleTap, 1.0f), out);
    CHECK(out.empty());
    CHECK_EQ(a.heldCount(), 1u);
    // The double tap resolves and claims the window: the aerial grade is dropped.
    a.submit(grade(Family::DoubleTap, 1.0f, 2.0f), 2.0f);
    a.release(2.0f, none(), out);
    CHECK_EQ(out.size(), 1u);
    if (!out.empty()) CHECK(out[0].family == Family::DoubleTap);
    CHECK_EQ(a.heldCount(), 0u);
}

TEST(bm_arbiter_releases_when_engagement_ends) {
    Arbiter a;
    std::vector<MechGrade> out;
    a.submit(grade(Family::Aerial, 1.0f, 1.02f), 1.02f);
    a.release(1.02f, engaged(Family::DoubleTap, 1.0f), out);
    CHECK(out.empty());
    a.release(1.1f, none(), out);  // the double tap aborted without a grade
    CHECK_EQ(out.size(), 1u);
    // A detector engaged only after the grade ended does not block it.
    Arbiter b;
    out.clear();
    b.submit(grade(Family::Aerial, 1.0f, 1.02f), 1.02f);
    b.release(1.5f, engaged(Family::FlipReset, 1.4f), out);
    CHECK_EQ(out.size(), 1u);
    // A less specific engaged detector does not block a strong grade.
    Arbiter c;
    out.clear();
    c.submit(grade(Family::Flick, 1.0f, 1.5f), 1.5f);
    c.release(1.5f, engaged(Family::AirDribble, 1.0f), out);
    CHECK_EQ(out.size(), 1u);
    // But it blocks a weak one (it may still end strong).
    Arbiter d;
    out.clear();
    d.submit(grade(Family::FlipReset, 1.0f, 1.1f, true), 1.1f);
    d.release(1.1f, engaged(Family::Aerial, 1.05f), out);
    CHECK(out.empty());
}

TEST(bm_arbiter_max_hold) {
    Arbiter a;
    std::vector<MechGrade> out;
    a.submit(grade(Family::PowerShot, 1.0f, 1.02f), 1.02f);
    a.release(2.0f, engaged(Family::FlipReset, 0.5f), out);
    CHECK(out.empty());
    a.release(1.02f + Arbiter::kMaxHold + 0.01f, engaged(Family::FlipReset, 0.5f), out);
    CHECK_EQ(out.size(), 1u);
}

TEST(bm_arbiter_drops_after_report_and_keeps_same_family) {
    Arbiter a;
    std::vector<MechGrade> out;
    a.submit(grade(Family::Pinch, 1.0f, 1.05f), 1.05f);
    a.release(1.05f, none(), out);
    CHECK_EQ(out.size(), 1u);
    // A late, less specific grade of the same touch is dropped.
    a.submit(grade(Family::PowerShot, 1.0f, 1.02f), 1.1f);
    a.release(1.1f, none(), out);
    CHECK_EQ(out.size(), 1u);
    // Two grades of the same family are two attempts.
    a.submit(grade(Family::Pinch, 1.05f, 1.2f), 1.2f);
    a.release(1.2f, none(), out);
    CHECK_EQ(out.size(), 2u);
    // Memory expires.
    a.submit(grade(Family::PowerShot, 1.0f, 1.02f), 1.0f + Arbiter::kMemory + 2.f);
    a.release(1.0f + Arbiter::kMemory + 2.f, none(), out);
    CHECK_EQ(out.size(), 3u);
    a.reset();
    CHECK_EQ(a.heldCount(), 0u);
}

TEST(bm_arbiter_weak_vs_strong) {
    // A weak near miss and a strong aerial shot of the same touch: the shot wins.
    Arbiter a;
    std::vector<MechGrade> out;
    a.submit(grade(Family::FlipReset, 1.0f, 1.1f, true), 1.1f);
    a.submit(grade(Family::Aerial, 1.0f, 1.02f), 1.1f);
    a.release(1.1f, none(), out);
    CHECK_EQ(out.size(), 1u);
    if (!out.empty()) CHECK(out[0].family == Family::Aerial);
    // Weak air dribble arriving after a strong aerial was held: the aerial stays, the weak one is dropped.
    Arbiter b;
    out.clear();
    b.submit(grade(Family::Aerial, 1.0f, 1.02f), 1.02f);
    b.release(1.02f, engaged(Family::AirDribble, 0.5f), out);
    CHECK(out.empty());
    b.submit(grade(Family::AirDribble, 0.5f, 2.5f, true), 2.5f);
    b.release(2.5f, none(), out);
    CHECK_EQ(out.size(), 1u);
    if (!out.empty()) CHECK(out[0].family == Family::Aerial);
}
