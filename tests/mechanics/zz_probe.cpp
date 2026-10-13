#include <cstdio>
#include "tests/mechanics/MechRig.h"
using namespace rlt;
using namespace rlt::mech;
using namespace rlt_test_mech;
TEST(zz_probe_hf) {
    Rig rig;
    rig.command("drill", {"halfflip"});
    rig.tick(Inputs{});
    auto inverted = std::make_shared<bool>(false);
    auto landed = std::make_shared<int>(-1);
    for (int t = 0; t < 700; ++t) {
        Inputs in;
        MiniSim& c = rig.sim;
        in.throttle = -1.f;
        const int J = 10, D = 20;
        if (t >= J && t < J + 4) in.jump = true;
        if (t >= D && t < D + 3) { in.jump = true; in.pitch = 1.f; }
        if (t >= D + 3) {
            const bool cancelling = t >= D + 24;
            in.pitch = cancelling ? -1.f : 1.f;
            if (cancelling) {
                if (c.up().z < 0.f) *inverted = true;
                if (*inverted) in.roll = c.up().z > 0.6f ? rollToLevel(c) : 1.f;
            }
            if (t > D + 10 && c.wheelCount() >= 3 && *landed < 0) *landed = t;
            if (*landed >= 0) { in.pitch = 0.f; in.roll = 0.f; in.throttle = 1.f; in.boost = true; }
        }
        rig.tick(in);
        if (t % 20 == 0) std::printf("t=%d car %.0f %.0f fwd %.2f %.2f touched=%d res=%zu phase=%s\n", t, rig.last.car.pos.x, rig.last.car.pos.y, rig.last.car.basis.fwd.x, rig.last.car.basis.fwd.y, rig.touched, rig.results.size(), rig.mod.status().phase.c_str());
    }
    std::printf("ball %.0f %.0f\n", rig.ballGame.x, rig.ballGame.y);
    for (auto& r : rig.results) { std::printf("%s score=%.0f succ=%d:", r.skill.c_str(), r.score, r.success); for (auto& m : r.metrics) std::printf(" %s=%.2f", m.key.c_str(), m.value); for (auto& tp : r.tips) std::printf(" [%s]", tp.code.c_str()); std::printf("\n"); }
}
