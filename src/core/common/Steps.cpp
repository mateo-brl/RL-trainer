#include "core/common/Steps.h"

#include <algorithm>
#include <utility>

namespace rlt {

StepCheck stepWhen(std::function<bool(const StepInput&)> condition) {
    return [c = std::move(condition)](const StepInput& in) {
        return c && c(in) ? StepVerdict::done() : StepVerdict::pending();
    };
}

StepCheck stepWhenSnap(std::function<bool(const Snapshot&)> condition) {
    return [c = std::move(condition)](const StepInput& in) {
        return c && c(in.snap) ? StepVerdict::done() : StepVerdict::pending();
    };
}

// ---- Definition ------------------------------------------------------------------------------------------------
void StepTracker::clear() {
    steps_.clear();
    live_.clear();
    reset();
}

void StepTracker::add(Step step) {
    steps_.push_back(std::move(step));
    live_.push_back({});
}

void StepTracker::setSteps(std::vector<Step> steps) {
    steps_ = std::move(steps);
    live_.assign(steps_.size(), Live{});
    reset();
}

// ---- Attempt ---------------------------------------------------------------------------------------------------
void StepTracker::reset() {
    live_.assign(steps_.size(), Live{});
    status_ = Status::Idle;
    start_ = 0.f;
    clock_ = 0.f;
    lastTime_ = -1.f;
    tickFrame_ = -1;
    lastDone_ = -1;
    lastDoneTime_ = -1e9f;
    failure_ = Failure{};
}

void StepTracker::start(float now) {
    reset();
    status_ = Status::Running;
    start_ = now;
    clock_ = now;
    finishIfComplete(now);  // an empty list (or only info steps) is complete at once
}

StepVerdict StepTracker::evaluate(int i, const Snapshot& s, const TickContext* ctx, float now, float dt) {
    const Step& st = steps_[static_cast<size_t>(i)];
    Live& lv = live_[static_cast<size_t>(i)];
    if (!st.check) return StepVerdict::pending();
    StepInput in{s, ctx, now, now - clock_, now - start_};
    StepVerdict v = st.check(in);
    if (v.state == StepState::Done) {
        lv.held += dt;
        lv.gap = 0.f;
        // A hold of 0.5 s at 120 Hz takes 60 ticks: the small epsilon absorbs float rounding of the sum.
        if (lv.held + 1e-4f < st.hold) return StepVerdict::pending();
        return v;
    }
    if (lv.held > 0.f && lv.gap + dt <= st.holdGrace + 1e-4f) {
        lv.gap += dt;  // a short break: the hold keeps its time but does not grow
    } else {
        lv.held = 0.f;
        lv.gap = 0.f;
    }
    if (v.state == StepState::Skipped) v.state = StepState::Pending;  // a check cannot skip itself
    return v;
}

void StepTracker::markDone(int i, float now, int64_t frame) {
    Live& lv = live_[static_cast<size_t>(i)];
    lv.state = StepState::Done;
    lv.doneAt = now;
    lv.doneFrame = frame;
    lastDone_ = i;
    lastDoneTime_ = now;
}

void StepTracker::finishIfComplete(float now) {
    if (status_ != Status::Running || currentRequired() < size()) return;
    // Info steps left behind no longer matter.
    for (auto& lv : live_)
        if (lv.state == StepState::Pending) lv.state = StepState::Skipped;
    status_ = Status::Complete;
    (void)now;
}

int StepTracker::update(const Snapshot& s, const TickContext* ctx) {
    if (status_ != Status::Running) return 0;
    const float now = s.time;
    float dt = s.dt > 0.f ? s.dt : kTickDt;
    if (lastTime_ >= 0.f && now > lastTime_) dt = std::min(now - lastTime_, 0.05f);
    lastTime_ = now;
    tickFrame_ = s.frame;

    int validated = 0;
    std::vector<char> evaluated(steps_.size(), 0);
    while (status_ == Status::Running) {
        const int req = currentRequired();
        if (req >= size()) break;
        // Info steps between the frontier and the required step: they can turn green, never block, never fail.
        for (int i = current(); i < req; ++i) {
            if (live_[static_cast<size_t>(i)].state != StepState::Pending || evaluated[static_cast<size_t>(i)]) continue;
            evaluated[static_cast<size_t>(i)] = 1;
            if (evaluate(i, s, ctx, now, dt).state == StepState::Done) {
                markDone(i, now, s.frame);
                ++validated;
            }
        }
        if (evaluated[static_cast<size_t>(req)]) break;  // each check runs once per tick
        evaluated[static_cast<size_t>(req)] = 1;
        const StepVerdict v = evaluate(req, s, ctx, now, dt);
        if (v.state == StepState::Done) {
            for (int i = 0; i < req; ++i)
                if (live_[static_cast<size_t>(i)].state == StepState::Pending) live_[static_cast<size_t>(i)].state = StepState::Skipped;
            markDone(req, now, s.frame);
            ++validated;
            clock_ = now;  // the next required step's time starts now
            finishIfComplete(now);
            continue;      // the next steps may already be true on this tick
        }
        if (v.state == StepState::Failed) {
            fail(now, v.code, v.tip, false);
            break;
        }
        const Step& st = steps_[static_cast<size_t>(req)];
        if (st.timeout > 0.f && now - clock_ > st.timeout + 1e-4f) fail(now, {}, {}, true);
        break;
    }
    return validated;
}

bool StepTracker::complete(float now) {
    if (status_ != Status::Running) return false;
    const int req = currentRequired();
    if (req >= size()) return false;
    for (int i = 0; i < req; ++i)
        if (live_[static_cast<size_t>(i)].state == StepState::Pending) live_[static_cast<size_t>(i)].state = StepState::Skipped;
    // Counted as validated on the tick of the last update (doneThisTick).
    markDone(req, now, tickFrame_);
    clock_ = now;
    finishIfComplete(now);
    return true;
}

void StepTracker::fail(float now, const std::string& code, const std::string& tip, bool timedOut) {
    if (status_ != Status::Running) return;
    const int req = currentRequired();
    failure_ = Failure{};
    failure_.time = now;
    failure_.timedOut = timedOut;
    failure_.code = code;
    failure_.tip = tip;
    if (req < size()) {
        const Step& st = steps_[static_cast<size_t>(req)];
        failure_.index = req;
        failure_.id = st.id;
        failure_.label = st.label;
        failure_.group = st.group;
        if (failure_.code.empty()) failure_.code = st.failCode;
        if (failure_.tip.empty()) failure_.tip = st.tip;
        live_[static_cast<size_t>(req)].state = StepState::Failed;
    }
    status_ = Status::Failed;
}

// ---- State -----------------------------------------------------------------------------------------------------
int StepTracker::current() const {
    for (int i = 0; i < size(); ++i) {
        const StepState st = live_[static_cast<size_t>(i)].state;
        if (st == StepState::Pending || st == StepState::Failed) return i;
    }
    return size();
}

int StepTracker::currentRequired() const {
    for (int i = 0; i < size(); ++i) {
        if (steps_[static_cast<size_t>(i)].optional) continue;
        const StepState st = live_[static_cast<size_t>(i)].state;
        if (st == StepState::Pending || st == StepState::Failed) return i;
    }
    return size();
}

const Step* StepTracker::currentStep() const {
    const int i = current();
    return i < size() ? &steps_[static_cast<size_t>(i)] : nullptr;
}

StepState StepTracker::state(int index) const {
    if (index < 0 || index >= size()) return StepState::Pending;
    return live_[static_cast<size_t>(index)].state;
}

StepState StepTracker::state(const std::string& id) const { return state(indexOf(id)); }

int StepTracker::indexOf(const std::string& id) const {
    for (int i = 0; i < size(); ++i)
        if (steps_[static_cast<size_t>(i)].id == id) return i;
    return -1;
}

bool StepTracker::done(const std::string& id) const { return state(id) == StepState::Done; }

bool StepTracker::reached(const std::string& id) const {
    const int i = indexOf(id);
    if (i < 0 || status_ == Status::Idle) return false;
    const StepState st = live_[static_cast<size_t>(i)].state;
    return st != StepState::Pending || i <= currentRequired();
}

bool StepTracker::doneThisTick(const std::string& id) const {
    const int i = indexOf(id);
    if (i < 0) return false;
    const Live& lv = live_[static_cast<size_t>(i)];
    return lv.state == StepState::Done && lv.doneFrame == tickFrame_ && tickFrame_ >= 0;
}

bool StepTracker::onLastStep() const {
    if (status_ != Status::Running) return false;
    const int req = currentRequired();
    if (req >= size()) return false;
    for (int i = req + 1; i < size(); ++i)
        if (!steps_[static_cast<size_t>(i)].optional) return false;
    return true;
}

bool StepTracker::groupDone(const std::string& group) const {
    bool any = false;
    for (int i = 0; i < size(); ++i) {
        if (steps_[static_cast<size_t>(i)].group != group) continue;
        any = true;
        if (live_[static_cast<size_t>(i)].state != StepState::Done) return false;
    }
    return any;
}

int StepTracker::doneCount() const {
    return static_cast<int>(std::count_if(live_.begin(), live_.end(), [](const Live& l) { return l.state == StepState::Done; }));
}

float StepTracker::stepElapsed(float now) const { return status_ == Status::Idle ? 0.f : now - clock_; }

float StepTracker::timeLeft(float now) const {
    const int req = currentRequired();
    if (status_ != Status::Running || req >= size()) return 1e9f;
    const float limit = steps_[static_cast<size_t>(req)].timeout;
    return limit > 0.f ? limit - (now - clock_) : 1e9f;
}

float StepTracker::holdProgress() const {
    const int i = current();
    if (i >= size()) return 0.f;
    const float hold = steps_[static_cast<size_t>(i)].hold;
    if (hold <= 0.f) return 0.f;
    return std::min(1.f, live_[static_cast<size_t>(i)].held / hold);
}

std::string StepTracker::missingText() const {
    if (status_ == Status::Failed) return failure_.label.empty() ? std::string() : "Il manquait : " + failure_.label;
    if (status_ != Status::Running) return {};
    const int req = currentRequired();
    if (req >= size()) return {};
    return "Il manquait : " + steps_[static_cast<size_t>(req)].label;
}

Tip StepTracker::missedTip(float weight) const {
    Tip t;
    t.weight = weight;
    if (status_ == Status::Failed) {
        t.code = failure_.code;
        t.text = failure_.tip;
    } else {
        const int req = currentRequired();
        if (status_ == Status::Running && req < size()) {
            t.code = steps_[static_cast<size_t>(req)].failCode;
            t.text = steps_[static_cast<size_t>(req)].tip;
        }
    }
    if (t.text.empty()) {
        t.text = missingText();
        if (!t.text.empty()) t.text += ".";
    }
    return t;
}

std::string StepTracker::number(int index) const {
    return std::to_string(index + 1) + "/" + std::to_string(size()) + " ";
}

std::string StepTracker::currentLine() const {
    const int i = current();
    if (i >= size()) return {};
    return number(i) + steps_[static_cast<size_t>(i)].label;
}

// ---- HUD -------------------------------------------------------------------------------------------------------
void appendStepLines(std::vector<PanelLine>& out, const StepTracker& t, float now, const StepHudStyle& style) {
    if (t.empty() || t.idle()) return;
    const int cur = t.running() ? t.current() : -1;
    // While an info step is current, the required step after it is current too (it is what ends the attempt).
    const int req = t.running() ? t.currentRequired() : -1;
    const bool flashing = style.flashSeconds > 0.f && t.lastDoneIndex() >= 0 && now - t.lastDoneTime() >= -0.05f &&
                          now - t.lastDoneTime() < style.flashSeconds;
    for (int i = 0; i < t.size(); ++i) {
        const Step& st = t.steps()[static_cast<size_t>(i)];
        const std::string text = (style.numbered ? t.number(i) : std::string()) + st.label;
        PanelLine l;
        l.scale = 1.f;
        switch (t.state(i)) {
        case StepState::Done: {
            const bool flash = flashing && i == t.lastDoneIndex();
            if (!flash && !style.showDone) continue;
            l.text = text + " : " + style.doneMark;
            l.color = style.done;
            l.detail = !flash;
            break;
        }
        case StepState::Failed:
            l.text = text;
            l.color = style.failed;
            break;
        case StepState::Skipped:
            if (!style.showUpcoming) continue;
            l.text = text;
            l.color = style.upcoming;
            l.detail = true;
            break;
        case StepState::Pending:
        default:
            if (i == cur || i == req) {
                l.text = text;
                l.color = style.current;
                const float p = i == cur ? t.holdProgress() : 0.f;
                if (st.hold > 0.f && p > 0.f) {
                    l.bar = p;
                    l.barColor = style.done;
                }
            } else {
                if (!style.showUpcoming) continue;
                l.text = text;
                l.color = style.upcoming;
                l.detail = true;
            }
            break;
        }
        if (l.detail) l.scale = style.detailScale;
        out.push_back(std::move(l));
    }
}

} // namespace rlt
