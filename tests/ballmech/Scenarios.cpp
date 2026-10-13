// Scenario builders shared by the ball mechanics tests (ported from the research reference test, adapted to the
// engine touch semantics: the touch tick is sampled before the hit).
#include "tests/ballmech/Sim.h"

namespace bmtest {

namespace {
void mirror(std::vector<SimTick>& ticks) {
    auto m = [](Vec3& v) {
        v.x = -v.x;
        v.y = -v.y;
    };
    for (auto& t : ticks) {
        CarState& c = t.s.car;
        m(c.pos);
        m(c.vel);
        m(c.angVel);
        m(c.basis.fwd);
        m(c.basis.right);
        m(c.basis.up);
        c.rot.yaw += 32768;
        c.team = 1;
        m(t.s.ball.pos);
        m(t.s.ball.vel);
        m(t.s.ball.angVel);
    }
}
} // namespace

std::vector<SimTick> flipResetScenario(FR kind, bool sdk) {
    Sim sim;
    Snapshot& s = sim.s;
    setAir(s.car);
    s.car.hasFlip = false;
    s.car.jumped = true;
    float roll = PI;
    if (kind == FR::NoReset) roll = std::acos(-0.70f);
    if (kind == FR::FastMissed) roll = std::acos(-0.72f);
    orient(s.car, PI / 2, 0, roll);  // facing +Y, upside down (or tilted)
    s.ball.pos = {0, 0, 900};
    s.ball.vel = {0, 150, 80};
    s.car.pos = {0, -260, 640};
    const Vec3 rel = kind == FR::FastMissed ? Vec3{0, 900, 950} : Vec3{0, 120, 150};
    s.car.vel = s.ball.vel + rel;
    sim.ballGravity = true;
    sim.step(20);
    // Contact: car right under the ball, underside facing it.
    sim.step(1, [&](Snapshot& x, int) {
        x.car.pos = x.ball.pos + Vec3{0, 0, -(x.ball.radius + 30.f)};
        sim.touchNow = true;
        if (kind == FR::NoReset) {
            x.car.wheelContacts = 1;
        } else {
            x.car.wheelContacts = 4;
            x.car.hasFlip = true;
            sim.sdkNow = sdk;
        }
    });
    sim.step(2, [&](Snapshot& x, int) {
        x.car.pos = x.ball.pos + Vec3{0, 0, -(x.ball.radius + 30.f)};
        if (kind != FR::NoReset) x.car.wheelContacts = 4;
    });
    sim.step(1, [&](Snapshot& x, int) {
        setAir(x.car);
        x.car.vel = x.ball.vel + Vec3{0, -100, -200};
    });
    sim.carGravity = true;
    if (kind == FR::NoReset) {
        sim.step(40);
        return sim.out;
    }
    if (kind == FR::Wasted) {
        // Fall back to the floor without using the reset.
        sim.step(1, [&](Snapshot& x, int) { x.car.vel = {0, 100, -900}; });
        for (int i = 0; i < 200; ++i) {
            bool landed = false;
            sim.step(1, [&](Snapshot& x, int) {
                if (x.car.pos.z <= bm::arena::kCarRestZ) {
                    x.car.pos.z = bm::arena::kCarRestZ;
                    x.car.vel = {0, 100, 0};
                    orient(x.car, PI / 2, 0, 0);
                    setGround(x.car);
                    landed = true;
                }
            });
            if (landed) break;
        }
        sim.carGravity = false;
        sim.step(20, [&](Snapshot& x, int) {
            setGround(x.car);
            x.car.pos.z = bm::arena::kCarRestZ;
            x.car.vel.z = 0;
        });
        return sim.out;
    }
    sim.step(70);  // about 0.6 s later: line up behind the ball, upright
    sim.step(1, [&](Snapshot& x, int) {
        orient(x.car, PI / 2, 0, 0);
        if (kind == FR::Good) {
            x.car.pos = x.ball.pos + Vec3{0, -170, 0};
            x.car.vel = x.ball.vel + Vec3{0, 400, 0};
        } else {
            x.car.pos = x.ball.pos + Vec3{0, -900, -300};
        }
    });
    sim.dodge(1.f, 0.f);
    if (kind == FR::Good) sim.hit({0, 2200, 100});
    sim.step(60);
    return sim.out;
}

std::vector<SimTick> flipResetChain() {
    Sim sim;
    Snapshot& s = sim.s;
    setAir(s.car);
    s.car.hasFlip = false;
    s.car.jumped = true;
    orient(s.car, PI / 2, 0, PI);
    s.ball.pos = {0, 0, 1000};
    s.ball.vel = {0, 200, 0};
    s.car.pos = {0, -200, 800};
    s.car.vel = {0, 300, 100};
    for (int r = 0; r < 2; ++r) {
        sim.step(r == 0 ? 20 : 45);  // second reset about 0.4 s after the first (beyond the 0.3 s de-dup window)
        sim.step(3, [&](Snapshot& x, int i) {
            x.car.pos = x.ball.pos + Vec3{0, 0, -(x.ball.radius + 30.f)};
            x.car.wheelContacts = 4;
            x.car.hasFlip = true;
            if (i == 0) {
                sim.sdkNow = true;
                sim.touchNow = true;
            }
        });
        sim.step(1, [&](Snapshot& x, int) { setAir(x.car); });
    }
    sim.step(30);
    sim.step(1, [&](Snapshot& x, int) {
        orient(x.car, PI / 2, 0, 0);
        x.car.pos = x.ball.pos + Vec3{0, -170, 0};
    });
    sim.dodge(1.f, 0.f);
    sim.hit({0, 2400, 0});
    sim.step(10);
    return sim.out;
}

std::vector<SimTick> ceilingScenario(bool good) {
    Sim sim;
    Snapshot& s = sim.s;
    setGround(s.car);
    s.car.hasFlip = true;
    orient(s.car, PI / 2, 0, PI);  // facing +Y, wheels on the ceiling
    s.car.pos = {0, 1500, bm::arena::kCeilingZ - bm::arena::kCarRestZ};
    s.car.vel = {0, 900, 0};
    // The ball travels with the car, ahead and below it (no teleport needed for the shot).
    sim.attached = true;
    sim.attachOffset = {0, 160, -100};
    s.ball.pos = s.car.pos + sim.attachOffset;
    s.ball.vel = s.car.vel;
    sim.step(60);
    sim.step(1, [&](Snapshot& x, int) {
        setAir(x.car);
        x.car.vel.z = -400;
        if (!good) {
            x.in.jump = true;
            x.car.jumped = true;
        }
    });
    sim.step(1, [&](Snapshot& x, int) { x.in.jump = false; });
    sim.carGravity = true;
    sim.step(good ? 60 : 30);
    if (good) {
        sim.step(1, [&](Snapshot& x, int) { orient(x.car, PI / 2, 0, 0); });
        sim.dodge(1.f, 0.f);
        sim.attached = false;
        sim.hit({0, 2200, -900});
    } else {
        sim.attached = false;
        sim.s.ball.vel = {0, 300, 0};
        sim.dodge(1.f, 0.f);  // flips into nothing
    }
    sim.ballGravity = true;
    sim.step(70);
    return sim.out;
}

std::vector<SimTick> flickScenario(FK kind) {
    Sim sim;
    Snapshot& s = sim.s;
    setGround(s.car);
    orient(s.car, PI / 2, 0, 0);
    s.car.pos = {0, -3000, bm::arena::kCarRestZ};
    s.car.vel = {0, 1000, 0};
    sim.attached = true;
    sim.attachOffset = {0, 20, 125};
    s.ball.pos = s.car.pos + sim.attachOffset;
    s.ball.vel = s.car.vel;
    sim.step(48);  // 0.4 s stable carry on the roof
    // Jump (hold 0.1 s).
    sim.step(1, [&](Snapshot& x, int) {
        setAir(x.car);
        x.car.jumped = true;
        x.in.jump = true;
        x.car.vel.z = 292;
    });
    sim.carGravity = true;
    sim.step(11, [&](Snapshot& x, int) { x.in.jump = true; });
    sim.step(1, [&](Snapshot& x, int) { x.in.jump = false; });
    const float yaw0 = PI / 2;
    float dodgeF = 1, dodgeS = 0;
    switch (kind) {
    case FK::F45:
    case FK::F90:
    case FK::OverRotated45: {
        const float target = kind == FK::F45 ? 45.f : (kind == FK::F90 ? 90.f : 80.f);
        const float tr = target * PI / 180.f;
        sim.step(18, [&](Snapshot& x, int i) {
            orient(x.car, yaw0 - tr * static_cast<float>(i + 1) / 18.f, 0, 0);
            x.in.roll = -1;
        });
        sim.step(4, [&](Snapshot& x, int) { x.in.roll = 0; });
        dodgeF = 0.7f;
        dodgeS = 0.7f;
        break;
    }
    case FK::Musty:
        sim.step(36, [&](Snapshot& x, int i) {
            orient(x.car, yaw0, -70.f * PI / 180.f * static_cast<float>(i + 1) / 36.f, 0);
            x.in.pitch = -1;
        });
        sim.step(6, [&](Snapshot& x, int) { x.in.pitch = 0; });
        dodgeF = -1;
        break;
    case FK::MustyNoseLevel:
        sim.step(40);
        dodgeF = -1;
        break;
    case FK::Breezi: {
        float rollAng = 0;
        sim.step(96, [&](Snapshot& x, int i) {
            rollAng += 5.5f * DT;
            const float pitch = -60.f * PI / 180.f * std::min(1.f, static_cast<float>(i + 1) / 96.f);
            orient(x.car, yaw0, pitch, rollAng);
            x.car.angVel = x.car.basis.fwd * 5.5f;
            x.in.roll = 1;
            x.in.yaw = -1;
        });
        sim.step(4, [&](Snapshot& x, int) {
            x.car.angVel = {};
            x.in.roll = 0;
            x.in.yaw = 0;
        });
        dodgeF = -1;
        break;
    }
    case FK::FrontDelayed:
        sim.step(84);
        dodgeF = 1;
        break;
    case FK::Reverse:
        sim.step(20, [&](Snapshot& x, int i) {
            orient(x.car, yaw0, -10.f * PI / 180.f, 0.45f * static_cast<float>(i + 1) / 20.f);
        });
        dodgeF = -0.6f;
        dodgeS = 0.8f;
        break;
    case FK::LostBall:
        sim.attached = false;
        sim.s.ball.vel = sim.s.car.vel + Vec3{0, 700, 150};
        sim.step(40);
        dodgeF = 1;
        break;
    }
    sim.dodge(dodgeF, dodgeS);
    if (kind != FK::LostBall) {
        sim.attached = false;
        Vec3 impulse = {0, 1500, 500};
        if (kind == FK::Musty || kind == FK::Breezi) impulse = {0, 1300, 700};
        if (kind == FK::MustyNoseLevel) impulse = {0, 250, 1100};  // plain backflip pops it up
        if (kind == FK::Reverse) impulse = {0, 1500, 450};
        sim.hit(sim.s.ball.vel + impulse);
        sim.ballGravity = true;
    }
    sim.step(60);
    return sim.out;
}

std::vector<SimTick> doubleTapScenario(DTK kind) {
    Sim sim;
    Snapshot& s = sim.s;
    setAir(s.car);
    orient(s.car, PI / 2, 0.3f, 0);
    s.car.pos = {0, kind == DTK::LateNoSecond ? 2200.f : 3600.f, 700};
    s.car.vel = {0, 1200, 300};
    s.ball.pos = {0, 3750, 760};
    s.ball.vel = {0, 0, 0};
    sim.step(3);
    sim.hit({0, 2400, 500}, [](Snapshot& x) { x.car.pos = x.ball.pos + Vec3{0, -150, 0}; });
    if (kind == DTK::LateNoSecond) {
        sim.step(1, [&](Snapshot& x, int) {
            x.car.pos = {0, 2200, 700};
            x.car.vel = {0, 300, 0};
        });
    }
    sim.ballGravity = true;
    bool bounced = false;
    for (int i = 0; i < 400 && !bounced; ++i) {
        sim.step(1, [&](Snapshot& x, int) {
            if (x.ball.pos.y >= bm::arena::kBackWallY - x.ball.radius) {
                x.ball.vel.y = -0.6f * x.ball.vel.y;
                bounced = true;
            }
            if (kind != DTK::LateNoSecond) x.car.vel = {0, 1300, 380};
        });
    }
    if (kind == DTK::Landed) {
        sim.step(10);
        sim.step(30, [&](Snapshot& x, int) {
            setGround(x.car);
            x.car.pos.z = bm::arena::kCarRestZ;
            x.car.vel = {};
        });
        return sim.out;
    }
    if (kind == DTK::LateNoSecond) {
        sim.step(240);
        return sim.out;
    }
    sim.step(40);
    sim.hit({0, 1800, -900}, [](Snapshot& x) {
        x.ball.pos = {0, 4650, 800};
        x.car.pos = x.ball.pos + Vec3{0, -150, 20};
    });
    sim.step(30);
    return sim.out;
}

std::vector<SimTick> airDribbleScenario(AD kind) {
    Sim sim;
    Snapshot& s = sim.s;
    if (kind == AD::WallBump) {
        setGround(s.car);
        orient(s.car, PI / 2, 0, PI / 2);  // on the right wall
        s.car.onWall = true;
        s.car.pos = {4000, -1000, 400};
        s.car.vel = {0, 900, 300};
        s.ball.pos = {3900, -880, 520};
        s.ball.vel = s.car.vel;
    } else {
        setGround(s.car);
        orient(s.car, PI / 2, 0, 0);
        s.car.pos = {0, -3000, bm::arena::kCarRestZ};
        s.car.vel = {0, 800, 0};
        s.ball.pos = {0, -2940, 180};
        s.ball.vel = s.car.vel;
    }
    s.car.boost = 100;
    sim.step(5);
    sim.step(1, [&](Snapshot&, int) { sim.touchNow = true; });  // ground / wall pop
    sim.step(1, [&](Snapshot& x, int) {
        setAir(x.car);
        x.car.onWall = false;
        x.car.jumped = true;
        x.car.vel = kind == AD::WallBump ? Vec3{-300, 900, 500} : Vec3{0, 800, 600};
        x.car.pos.z = std::max(x.car.pos.z, 300.f);
    });
    orient(sim.s.car, PI / 2, 0.6f, 0);
    sim.attached = true;
    sim.attachOffset = {0, 60, 110};
    const int interval = kind == AD::GroundCarry ? 12 : 30;
    const int nTouches = kind == AD::GroundCarry ? 17 : (kind == AD::WallBump ? 6 : 1);
    for (int t = 0; t < nTouches; ++t) {
        sim.step(interval - 1, [&](Snapshot& x, int) { x.car.boost = std::max(0.f, x.car.boost - 0.28f); });
        sim.step(1, [&](Snapshot&, int) { sim.touchNow = true; });
    }
    sim.attached = false;
    sim.ballGravity = true;
    if (kind == AD::GroundCarry) {
        sim.hit({0, 2600, 0});
        sim.step(200);
    } else {
        sim.s.ball.vel = {0, 300, -200};
        sim.step(240);
    }
    return sim.out;
}

std::vector<SimTick> airDribbleFlipFinish(int bumps) {
    Sim sim;
    Snapshot& s = sim.s;
    setGround(s.car);
    orient(s.car, PI / 2, 0, 0);
    s.car.pos = {0, -3000, bm::arena::kCarRestZ};
    s.car.vel = {0, 900, 0};
    s.car.boost = 100;
    sim.attached = true;
    sim.attachOffset = {0, 20, 125};
    s.ball.pos = s.car.pos + sim.attachOffset;
    s.ball.vel = s.car.vel;
    sim.step(60, [&](Snapshot&, int) { sim.touchNow = true; });  // 0.5 s carry: the game reports every tick
    // Pop: jump with the ball on the roof (still the same contact).
    sim.step(1, [&](Snapshot& x, int) {
        setAir(x.car);
        x.car.jumped = true;
        x.in.jump = true;
        x.car.vel = {0, 900, 350};
        sim.touchNow = true;
    });
    sim.step(1, [&](Snapshot& x, int) { x.in.jump = false; });
    orient(sim.s.car, PI / 2, 0.5f, 0);
    sim.attachOffset = {0, 60, 115};
    // Separate bumps in the air (12 ticks apart: new contacts).
    for (int b = 0; b < bumps; ++b) {
        sim.step(11, [&](Snapshot& x, int) { x.car.boost = std::max(0.f, x.car.boost - 0.3f); });
        sim.step(1, [&](Snapshot&, int) { sim.touchNow = true; });
    }
    // Front dodge into the ball.
    sim.step(4);
    sim.dodge(1.f, 0.f);
    sim.attached = false;
    sim.hit({0, 2800, 0});  // flipped into the ball toward the goal
    sim.ballGravity = true;
    sim.step(240);
    return sim.out;
}

std::vector<SimTick> aerialScenario(AE kind, int team) {
    Sim sim;
    Snapshot& s = sim.s;
    setAir(s.car);
    orient(s.car, PI / 2, 0.2f, 0);
    s.car.pos = kind == AE::Redirect ? Vec3{500, 3500, 400} : Vec3{0, 2000, 900};
    s.car.vel = {0, 1000, 200};
    s.ball.vel = kind == AE::Redirect ? Vec3{-1800, 0, 0} : Vec3{0, 0, 0};
    s.ball.pos = s.car.pos + s.car.basis.fwd * 140.f;
    sim.step(40, [&](Snapshot& x, int) {
        if (kind == AE::AirRoll) x.in.roll = 1;
        const Vec3 off = kind == AE::OffCenter ? x.car.basis.fwd * 100.f + x.car.basis.right * 110.f : x.car.basis.fwd * 140.f;
        x.ball.pos = x.car.pos + off;
    });
    const Vec3 v = kind == AE::OffCenter ? Vec3{1800, 600, 0} : (kind == AE::Redirect ? Vec3{0, 2200, -300} : Vec3{0, 2600, -400});
    sim.hit(v, [&](Snapshot& x) {
        x.in.roll = 0;
        const Vec3 off = kind == AE::OffCenter ? x.car.basis.fwd * 100.f + x.car.basis.right * 110.f : x.car.basis.fwd * 140.f;
        x.ball.pos = x.car.pos + off;
    });
    sim.ballGravity = true;
    sim.step(20);
    if (team == 1) mirror(sim.out);
    return sim.out;
}

std::vector<SimTick> pinchScenario(PI_ kind) {
    Sim sim;
    Snapshot& s = sim.s;
    setAir(s.car);
    orient(s.car, PI / 2, -0.4f, 0);
    if (kind == PI_::Kuxir) {
        s.ball.pos = {4005, 2000, 400};
        s.car.pos = {3850, 1850, 400};
        s.car.vel = {900, 1200, 0};
    } else {
        s.ball.pos = {0, 0, 93.15f};
        s.car.pos = {0, -120, 180};
        s.car.vel = {0, 1400, -600};
    }
    const Vec3 carPos0 = s.car.pos;
    sim.step(5, [&](Snapshot& x, int) { x.car.pos = carPos0; });
    const Vec3 v = kind == PI_::Ground ? Vec3{0, 4800, 600} : (kind == PI_::Kuxir ? Vec3{-3800, 3200, 200} : Vec3{0, 2300, 300});
    sim.hit(v, [&](Snapshot& x) { x.car.pos = carPos0; });
    sim.step(20);
    return sim.out;
}

std::vector<SimTick> powerShotScenario(PS kind, int team) {
    Sim sim;
    Snapshot& s = sim.s;
    setGround(s.car);
    orient(s.car, PI / 2, 0, 0);
    const float speed = kind == PS::WeakSide ? 900.f : (kind == PS::SlideShot ? 1500.f : 1900.f);
    s.car.pos = {0, -400, bm::arena::kCarRestZ};
    s.car.vel = {0, speed, 0};
    s.ball.pos = {0, 300, 93.15f};
    if (kind == PS::SlideShot) {
        sim.step(20);
        sim.step(36, [&](Snapshot& x, int) {
            x.in.handbrake = true;
            x.car.angVel = {0, 0, 3.0f};
        });
        sim.step(10, [&](Snapshot& x, int) {
            x.in.handbrake = false;
            x.car.angVel = {};
        });
    } else {
        sim.step(30);
    }
    if (kind == PS::FlipShot) {
        sim.step(1, [&](Snapshot& x, int) {
            setAir(x.car);
            x.car.vel.z = 100;
            x.car.jumped = true;
        });
        sim.dodge(1.f, 0.f);
        sim.step(8);
    }
    const Vec3 v = kind == PS::WeakSide ? Vec3{900, 900, 0} : (kind == PS::SlideShot ? Vec3{0, 2400, 200} : Vec3{0, 3300, 300});
    sim.hit(v, [&](Snapshot& x) {
        const Vec3 off = kind == PS::WeakSide ? x.car.basis.fwd * 10.f + x.car.basis.right * 110.f : x.car.basis.fwd * 120.f;
        x.ball.pos = Vec3{x.car.pos.x, x.car.pos.y, 0} + Vec3{off.x, off.y, 93.15f};
    });
    sim.ballGravity = true;
    sim.step(20);
    if (team == 1) mirror(sim.out);
    return sim.out;
}

} // namespace bmtest
