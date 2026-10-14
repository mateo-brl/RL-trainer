#include "core/common/HangingBall.h"

namespace rlt {

bool touchFrameChanged(const Snapshot& s, int prevTouchFrame) {
    return s.car.valid && s.car.lastBallTouchFrame > 0 && s.car.lastBallTouchFrame != prevTouchFrame;
}

void HangingBall::hang(const Vec3& pos) {
    if (state_ != State::Hanging) {
        // A new hanging: the touch baseline is taken on the first update, the movement test waits for a first send.
        haveTouchBase_ = false;
        lastSentFrame_ = -1;
        freeFrame_ = -1;
    } else if (pos != pos_) {
        lastSentFrame_ = -1;  // moved on purpose: the next snapshot is not compared with the old point
    }
    state_ = State::Hanging;
    pos_ = pos;
    pendingRelease_ = false;
}

void HangingBall::release(const Vec3& vel, const Vec3& angVel) {
    if (state_ != State::Hanging) return;
    pendingRelease_ = true;
    releaseVel_ = vel;
    releaseAngVel_ = angVel;
}

void HangingBall::stop() {
    state_ = State::Idle;
    pendingRelease_ = false;
    haveTouchBase_ = false;
    lastSentFrame_ = -1;
}

HangingBall::Event HangingBall::update(const Snapshot& s, Actions& out, bool touchSeen) {
    if (state_ != State::Hanging) return Event::None;

    bool touch = touchSeen;
    if (s.car.valid) {
        if (haveTouchBase_ && touchFrameChanged(s, touchBase_)) touch = true;
        if (!haveTouchBase_) {
            touchBase_ = s.car.lastBallTouchFrame;
            haveTouchBase_ = true;
        }
    }
    // The ball was put still on the previous frame: anything more than one tick of gravity is a hit.
    if (s.ball.valid && lastSentFrame_ >= 0 && s.frame == lastSentFrame_ + 1) {
        if (length(s.ball.vel) > kTouchSpeed || distance(s.ball.pos, sentPos_) > kTouchDrift) touch = true;
    }
    if (touch) {
        state_ = State::Touched;
        pendingRelease_ = false;
        freeFrame_ = s.frame;
        return Event::Touched;
    }

    BallPlacement b;
    b.pos = pos_;
    if (pendingRelease_) {
        b.vel = releaseVel_;
        b.angVel = releaseAngVel_;
        out.setBall = b;
        state_ = State::Released;
        pendingRelease_ = false;
        freeFrame_ = s.frame;
        return Event::Released;
    }
    out.setBall = b;  // vel 0, angVel 0
    sentPos_ = pos_;
    lastSentFrame_ = s.frame;
    return Event::Held;
}

} // namespace rlt
