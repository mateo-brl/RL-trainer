#include "core/engine/Engine.h"

#include <algorithm>

#include "core/modules/ballmech/BallMechModule.h"
#include "core/modules/coach/CoachModule.h"
#include "core/modules/courses/CoursesModule.h"
#include "core/modules/firsttouch/FirstTouchModule.h"
#include "core/modules/kickoff/KickoffModule.h"
#include "core/modules/maps/MapsModule.h"
#include "core/modules/mechanics/MechanicsModule.h"
#include "core/modules/overlay/OverlayModule.h"
#include "core/modules/prediction/PredictionModule.h"
#include "core/modules/profile/ProfileModule.h"
#include "core/modules/shooting/ShootingModule.h"

namespace rlt {

namespace {
constexpr int kNewContactGapTicks = 6;  // 50 ms without contact => a new touch
constexpr int kMaxRouteDepth = 3;
} // namespace

// ---- Construction --------------------------------------------------------------------------------------
Engine::Engine() {
    // Order = draw order (later modules draw on top) and dispatch order.
    modules_.push_back(std::make_unique<ProfileModule>());
    modules_.push_back(std::make_unique<MapsModule>());
    modules_.push_back(std::make_unique<CoursesModule>());
    modules_.push_back(std::make_unique<ShootingModule>());
    modules_.push_back(std::make_unique<KickoffModule>());
    modules_.push_back(std::make_unique<PredictionModule>());
    modules_.push_back(std::make_unique<OverlayModule>());
    modules_.push_back(std::make_unique<FirstTouchModule>());
    modules_.push_back(std::make_unique<MechanicsModule>());
    modules_.push_back(std::make_unique<BallMechModule>());
    modules_.push_back(std::make_unique<CoachModule>());
    for (auto& m : modules_) m->attach(this);

    describeGeneralSettings(schema_);
    OverlayModule::describeSettings(schema_);
    FirstTouchModule::describeSettings(schema_);
    PredictionModule::describeSettings(schema_);
    MechanicsModule::describeSettings(schema_);
    BallMechModule::describeSettings(schema_);
    KickoffModule::describeSettings(schema_);
    CoursesModule::describeSettings(schema_);
    ShootingModule::describeSettings(schema_);
    MapsModule::describeSettings(schema_);
    CoachModule::describeSettings(schema_);
    ProfileModule::describeSettings(schema_);

    schema_.applyDefaults(settings_);
    notifySettings();
}

Engine::~Engine() = default;

// ---- Settings ------------------------------------------------------------------------------------------
bool Engine::applySetting(const std::string& cvar, const std::string& value) {
    auto lk = lock();
    if (!schema_.apply(settings_, cvar, value)) return false;
    notifySettings();
    Actions ignored;
    recomputeWorldControl(ignored);
    return true;
}

std::string Engine::settingValue(const std::string& cvar) const {
    auto lk = lock();
    const SettingDef* d = schema_.find(cvar);
    return (d && d->read) ? d->read(settings_) : std::string();
}

void Engine::notifySettings() {
    for (auto& m : modules_) m->onSettingsChanged(settings_);
}

// ---- Context / profile ---------------------------------------------------------------------------------
void Engine::setContext(const GameContext& ctx, Actions& out) {
    auto lk = lock();
    const bool mapChanged = ctx.mode != context_.mode || ctx.mapName != context_.mapName;
    context_ = ctx;
    if (mapChanged) {
        history_.clear();
        hasSnapshot_ = false;
        pendingTouches_.clear();
        lastSeenTouchFrame_ = -1;
        lastTouchFrame_ = -1000000;
    }
    recomputeWorldControl(out);
    GameEvent ev;
    ev.kind = GameEventKind::ContextChanged;
    ev.frame = latest_.frame;
    ev.detail = ctx.mapName;
    dispatch(out, [&](Module* m, Actions& a) { m->onGameEvent(ev, makeTickContext(m), a); });
}

void Engine::setProfile(const PlayerProfile& profile, Actions& out) {
    auto lk = lock();
    profile_ = profile;
    for (auto& m : modules_) m->onProfileChanged(profile_);
    GameEvent ev;
    ev.kind = GameEventKind::ProfileChanged;
    ev.frame = latest_.frame;
    dispatch(out, [&](Module* m, Actions& a) { m->onGameEvent(ev, makeTickContext(m), a); });
}

void Engine::recomputeWorldControl(Actions& out) {
    (void)out;
    bool allowed = settings_.general.enabled;
    switch (context_.mode) {
    case GameMode::Freeplay: break;
    case GameMode::CustomTraining: allowed = allowed && settings_.general.controlInTraining; break;
    case GameMode::Workshop: allowed = allowed && settings_.general.controlInWorkshop; break;
    default: allowed = false; break;
    }
    worldControlAllowed_ = allowed;
    context_.worldControlAllowed = allowed;
    if (!allowed && !activityOwner_.empty()) stopActivity();
}

// ---- Tick ----------------------------------------------------------------------------------------------
void Engine::tick(const Snapshot& snap, Actions& out) {
    auto lk = lock();
    if (context_.mode == GameMode::Online) return;
    if (hasSnapshot_ && snap.frame <= latest_.frame) return;  // de-duplicate (one call per physics frame)

    history_.push(snap);
    latest_ = snap;
    hasSnapshot_ = true;

    pump(out);
    if (!settings_.general.enabled) {
        pendingTouches_.clear();
        return;
    }
    resolveTouches(out);
    dispatch(out, [&](Module* m, Actions& a) { m->onTick(makeTickContext(m), a); });
}

void Engine::reportTouch(int64_t frame, bool hasHitInfo, const Vec3& hitLocation, const Vec3& hitNormal) {
    auto lk = lock();
    (void)frame;
    if (!hasSnapshot_) return;
    // The hook fires during the physics step that follows the newest snapshot: key the touch by that snapshot.
    const int64_t after = latest_.frame;
    for (auto& p : pendingTouches_) {
        if (p.frame == after) {
            if (hasHitInfo && !p.hasHit) {
                p.hasHit = true;
                p.loc = hitLocation;
                p.normal = hitNormal;
            }
            return;
        }
    }
    pendingTouches_.push_back({after, hasHitInfo, hitLocation, hitNormal});
}

void Engine::resolveTouches(Actions& out) {
    // Fallback detection: the car's last-touch frame changed during the step between the two newest snapshots.
    const int seen = latest_.car.lastBallTouchFrame;
    if (latest_.car.valid && history_.size() >= 2) {
        if (lastSeenTouchFrame_ >= 0 && seen != lastSeenTouchFrame_ && seen > 0) {
            const int64_t prevFrame = history_.back(1).frame;
            const bool covered = std::any_of(pendingTouches_.begin(), pendingTouches_.end(),
                                             [&](const PendingTouch& p) { return p.frame == prevFrame; });
            if (!covered) pendingTouches_.push_back({prevFrame, false, {}, {}});
        }
    }
    if (latest_.car.valid) lastSeenTouchFrame_ = seen;

    if (pendingTouches_.empty()) return;
    std::vector<PendingTouch> still;
    for (const auto& p : pendingTouches_) {
        const Snapshot* pre = history_.atOrBefore(p.frame);
        const Snapshot* post1 = history_.after(p.frame);
        const Snapshot* post2 = post1 ? history_.after(post1->frame) : nullptr;
        if (!post2) {  // not resolvable yet (needs two more ticks)
            if (history_.newest().frame - p.frame < 8) still.push_back(p);
            continue;
        }
        if (!pre || pre->frame != p.frame) continue;  // history cleared or gap: drop
        TouchEvent t;
        t.frame = pre->frame;
        t.pre = *pre;
        t.post1 = *post1;
        t.post = *post2;
        t.hasHitInfo = p.hasHit;
        t.hitLocation = p.loc;
        t.hitNormal = p.normal;
        const int64_t gap = pre->frame - lastTouchFrame_;
        t.ticksSincePrevTouch = gap > (1 << 30) ? (1 << 30) : static_cast<int>(gap);
        t.newContact = gap >= kNewContactGapTicks;
        lastTouchFrame_ = pre->frame;
        dispatch(out, [&](Module* m, Actions& a) { m->onTouch(t, makeTickContext(m), a); });
    }
    pendingTouches_.swap(still);
}

void Engine::gameEvent(const GameEvent& ev, Actions& out) {
    auto lk = lock();
    if (context_.mode == GameMode::Online || !settings_.general.enabled) return;
    if (ev.kind == GameEventKind::Paused) context_.paused = true;
    if (ev.kind == GameEventKind::Unpaused) context_.paused = false;
    if (ev.kind == GameEventKind::MapUnloaded) {
        history_.clear();
        hasSnapshot_ = false;
        pendingTouches_.clear();
        lastSeenTouchFrame_ = -1;
        stopActivity();
    }
    dispatch(out, [&](Module* m, Actions& a) { m->onGameEvent(ev, makeTickContext(m), a); });
}

void Engine::kismet(const std::vector<KismetVar>& vars, Actions& out) {
    auto lk = lock();
    if (context_.mode == GameMode::Online || !settings_.general.enabled) return;
    dispatch(out, [&](Module* m, Actions& a) { m->onKismet(vars, makeTickContext(m), a); });
}

void Engine::draw(const ViewContext& view, DrawList& out) {
    auto lk = lock();
    if (!settings_.general.enabled) return;
    if (context_.mode == GameMode::Online || context_.mode == GameMode::Menu) return;
    if (context_.mode == GameMode::Replay && !settings_.general.showInReplays) return;
    for (auto& m : modules_) m->draw(view, makeDrawContext(m.get(), view), out);
}

// ---- Commands ------------------------------------------------------------------------------------------
void Engine::post(Command cmd) {
    auto lk = lock();
    commands_.push_back(std::move(cmd));
}

void Engine::pump(Actions& out) {
    auto lk = lock();
    while (!commands_.empty()) {
        Command c = std::move(commands_.front());
        commands_.pop_front();
        if (c.module == "engine") {
            if (c.name == "stop") stopActivity();
            else if (c.name == "save") saveRequested_ = true;
            continue;
        }
        Module* target = find(c.module);
        if (!target) continue;
        Actions a;
        target->onCommand(c, makeTickContext(target), a);
        route(target, std::move(a), out, 0);
    }
}

bool Engine::parseCommandLine(const std::vector<std::string>& words, Command& out) {
    // words[0] is the notifier name itself ("rlt"), as BakkesMod passes it.
    size_t i = 0;
    if (!words.empty() && (words[0] == "rlt" || words[0] == "rltrainer")) i = 1;
    if (words.size() < i + 2) return false;
    out.module = words[i];
    out.name = words[i + 1];
    out.args.assign(words.begin() + static_cast<long>(i + 2), words.end());
    return true;
}

// ---- Activities ----------------------------------------------------------------------------------------
bool Engine::requestActivity(Module* m, const std::string& name) {
    auto lk = lock();
    if (!m || !worldControlAllowed_) return false;
    if (activityOwner_ == m->id()) {
        activityName_ = name;
        return true;
    }
    if (!activityOwner_.empty()) {
        Module* old = find(activityOwner_);
        activityOwner_.clear();
        activityName_.clear();
        if (old) old->onActivityStopped();
    }
    activityOwner_ = m->id();
    activityName_ = name;
    return true;
}

void Engine::releaseActivity(Module* m) {
    auto lk = lock();
    if (m && activityOwner_ == m->id()) {
        activityOwner_.clear();
        activityName_.clear();
    }
}

bool Engine::ownsActivity(const Module* m) const {
    auto lk = lock();
    return m && !activityOwner_.empty() && activityOwner_ == m->id();
}

void Engine::stopActivity() {
    auto lk = lock();
    if (activityOwner_.empty()) return;
    Module* old = find(activityOwner_);
    activityOwner_.clear();
    activityName_.clear();
    if (old) old->onActivityStopped();
}

// ---- Persistence ---------------------------------------------------------------------------------------
void Engine::save(Store& store) const {
    auto lk = lock();
    for (const auto& m : modules_) m->save(store);
}

void Engine::load(const Store& store) {
    auto lk = lock();
    for (auto& m : modules_) m->load(store);
}

bool Engine::consumeSaveRequest() {
    auto lk = lock();
    const bool r = saveRequested_;
    saveRequested_ = false;
    return r;
}

// ---- Internals -----------------------------------------------------------------------------------------
Module* Engine::find(const std::string& id) const {
    for (const auto& m : modules_)
        if (id == m->id()) return m.get();
    return nullptr;
}

TickContext Engine::makeTickContext(const Module* m) const {
    return TickContext{latest_,  history_, settings_, context_, profile_, hasSnapshot_, ownsActivity(m),
                       worldControlAllowed_};
}

DrawContext Engine::makeDrawContext(const Module* m, const ViewContext& view) const {
    const float screenFactor = view.screenH > 0.f ? view.screenH / 1080.f : 1.f;
    return DrawContext{settings_, context_, profile_, latest_, hasSnapshot_, ownsActivity(m),
                       settings_.general.hudScale * screenFactor};
}

template <class Fn>
void Engine::dispatch(Actions& out, Fn&& fn, const Module* skip) {
    for (auto& m : modules_) {
        if (m.get() == skip) continue;
        Actions a;
        fn(m.get(), a);
        if (!a.empty()) route(m.get(), std::move(a), out, 0);
    }
}

void Engine::route(Module* source, Actions&& a, Actions& out, int depth) {
    if (!worldControlAllowed_ || !source || activityOwner_ != source->id()) a.clearWorldControl();
    if (!settings_.general.chatFeedback) a.chat.clear();
    if (!settings_.general.toastFeedback) a.toasts.clear();
    if (a.requestSave) saveRequested_ = true;
    // A module may only reset its own settings.
    if (!a.resetSettings.empty()) {
        const std::string own = source ? source->id() : std::string();
        std::vector<std::string> kept;
        for (auto& r : a.resetSettings)
            if (!own.empty() && (r == own || r.rfind(own + "/", 0) == 0)) kept.push_back(std::move(r));
        a.resetSettings.swap(kept);
    }

    std::vector<AttemptResult> results;
    results.swap(a.results);
    std::vector<GameEvent> events;
    events.swap(a.events);
    out.merge(std::move(a));

    if (depth >= kMaxRouteDepth) return;
    for (const auto& r : results) {
        for (auto& m : modules_) {
            Actions b;
            m->onResult(r, makeTickContext(m.get()), b);
            if (!b.empty()) route(m.get(), std::move(b), out, depth + 1);
        }
        out.results.push_back(r);
    }
    for (const auto& e : events) {
        for (auto& m : modules_) {
            if (m.get() == source) continue;
            Actions b;
            m->onGameEvent(e, makeTickContext(m.get()), b);
            if (!b.empty()) route(m.get(), std::move(b), out, depth + 1);
        }
    }
}

} // namespace rlt
