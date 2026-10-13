#include "plugin/RLTrainer.h"

#include <chrono>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <sstream>

#include "core/common/Text.h"
#include "imgui.h"
#include "plugin/Version.h"
#include "plugin/ui/Ui.h"

BAKKESMOD_PLUGIN(RLTrainer, "RL Trainer", kPluginVersionString, PLUGINTYPE_FREEPLAY | PLUGINTYPE_CUSTOM_TRAINING | PLUGINTYPE_REPLAY)

std::shared_ptr<CVarManagerWrapper> _globalCvarManager;

using namespace rlt;

namespace {
constexpr float kPeriod = 0.25f;  // seconds between periodic updates (game thread timer)

// OnHitBall params (x64): ABall_TA* Ball @0x00; FVector HitLocation @0x08; FVector HitNormal @0x14.
struct OnHitBallParams {
    uintptr_t ball;
    Vector hitLocation;
    Vector hitNormal;
};

// An exception escaping into the game crashes Rocket League: every callback runs through this.
template <class Fn>
void guarded(const std::shared_ptr<CVarManagerWrapper>& cvars, const char* where, Fn&& fn) {
    try {
        fn();
    } catch (const std::exception& e) {
        if (cvars) cvars->log(std::string("[RL Trainer] erreur dans ") + where + " : " + e.what());
    } catch (...) {
        if (cvars) cvars->log(std::string("[RL Trainer] erreur inconnue dans ") + where);
    }
}
} // namespace

// ---- Lifecycle -------------------------------------------------------------------------------------------
void RLTrainer::onLoad() {
    _globalCvarManager = cvarManager;
    engine_ = std::make_unique<Engine>();
    bridge_ = std::make_unique<GameBridge>(gameWrapper, cvarManager);
    profile_ = std::make_unique<ProfileService>(gameWrapper, cvarManager);
    maps_ = std::make_unique<MapsService>(gameWrapper, cvarManager);
    maps_->init();
    updates_ = std::make_unique<UpdateService>(gameWrapper, RLT_REPO_SLUG, kPluginVersionString);
    updates_->cleanup();
    ui_ = std::unique_ptr<UiContext>(new UiContext{*engine_, cvarManager, gameWrapper, profile_.get(), maps_.get(), false, &configDirty_});
    ui_->updateService = updates_.get();

    registerCvars();
    loadProgress();
    registerHooks();
    registerCommands();

    gameWrapper->RegisterDrawable([alive = alive_, this](CanvasWrapper canvas) {
        if (*alive) guarded(cvarManager, "draw", [&] { onDraw(canvas); });
    });

    refreshContext();
    refreshProfile(true);
    schedulePeriodic();
    gameWrapper->SetTimeout([alive = alive_, this](GameWrapper*) {
        if (!*alive) return;
        bool check = true;
        {
            auto lk = engine_->lock();
            check = engine_->settings().general.checkUpdates;
        }
        if (check) guarded(cvarManager, "update check", [&] { updates_->checkAsync(); });
    }, 8.f);
    cvarManager->log(std::string("RL Trainer ") + kPluginVersionString + " charge.");
}

void RLTrainer::onUnload() {
    *alive_ = false;
    // Only cvars, notifiers and drawables are documented as auto-removed: unhook explicitly.
    for (const auto& name : hookedPre_) gameWrapper->UnhookEvent(name);
    for (const auto& name : hookedPost_) gameWrapper->UnhookEventPost(name);
    saveProgress();
    if (configDirty_) cvarManager->executeCommand("writeconfig", false);
    // Restore normal driving / game speed in case a drill was running.
    if (engine_) engine_->stopActivity();
    CarWrapper car = gameWrapper->GetLocalCar();
    if (!car.IsNull()) car.SetbDriving(1);
}

// ---- Cvars and commands ----------------------------------------------------------------------------------
void RLTrainer::registerCvars() {
    for (const SettingDef& d : engine_->schema().all()) {
        const bool numeric = d.type == SettingType::Int || d.type == SettingType::Float || d.type == SettingType::Choice ||
                             d.type == SettingType::Bool;
        CVarWrapper cv = cvarManager->registerCvar(d.cvar, d.defaultValue, d.label, true, numeric, d.minValue, numeric,
                                                   d.maxValue, d.persist);
        const std::string name = d.cvar;
        cv.addOnValueChanged([alive = alive_, this, name](std::string, CVarWrapper c) {
            if (*alive && engine_) guarded(cvarManager, "cvar", [&] { engine_->applySetting(name, c.getStringValue()); });
        });
        engine_->applySetting(name, cv.getStringValue());
    }
}

void RLTrainer::registerCommands() {
    cvarManager->registerNotifier(
        "rlt",
        [alive = alive_, this](std::vector<std::string> args) {
            if (!*alive) return;
            Command c;
            if (Engine::parseCommandLine(args, c)) engine_->post(std::move(c));
            else cvarManager->log("Usage : rlt <module> <commande> [arguments]. Ex : rlt kickoff start");
        },
        "RL Trainer : rlt <module> <commande> [arguments]", PERMISSION_ALL);
    cvarManager->registerNotifier(
        "rlt_menu", [this](std::vector<std::string>) { cvarManager->executeCommand("togglemenu " + GetMenuName()); },
        "Ouvre ou ferme la fenêtre RL Trainer", PERMISSION_ALL);
}

// ---- Hooks -----------------------------------------------------------------------------------------------
void RLTrainer::hookPre(const std::string& name, std::function<void(std::string)> fn) {
    hookedPre_.push_back(name);
    gameWrapper->HookEvent(name, [alive = alive_, this, name, fn = std::move(fn)](std::string ev) {
        if (*alive) guarded(cvarManager, name.c_str(), [&] { fn(ev); });
    });
}

void RLTrainer::hookPost(const std::string& name, std::function<void(std::string)> fn) {
    hookedPost_.push_back(name);
    gameWrapper->HookEventPost(name, [alive = alive_, this, name, fn = std::move(fn)](std::string ev) {
        if (*alive) guarded(cvarManager, name.c_str(), [&] { fn(ev); });
    });
}

void RLTrainer::registerHooks() {
    auto alive = alive_;
    const std::string vehicleInput = "Function TAGame.Car_TA.SetVehicleInput";
    hookedPre_.push_back(vehicleInput);
    gameWrapper->HookEventWithCaller<CarWrapper>(vehicleInput, [alive, this](CarWrapper car, void* params, std::string) {
        if (*alive) guarded(cvarManager, "SetVehicleInput", [&] { onVehicleInput(car, params); });
    });
    const std::string hitBall = "Function TAGame.Car_TA.OnHitBall";
    hookedPost_.push_back(hitBall);
    gameWrapper->HookEventWithCallerPost<CarWrapper>(hitBall, [alive, this](CarWrapper car, void* params, std::string) {
        if (*alive) guarded(cvarManager, "OnHitBall", [&] { onHitBall(car, params); });
    });
    const std::string flipReset = "Function TAGame.Car_TA.EventPerformedFlipReset";
    hookedPost_.push_back(flipReset);
    gameWrapper->HookEventWithCallerPost<CarWrapper>(flipReset, [alive, this](CarWrapper car, void*, std::string) {
        if (*alive) guarded(cvarManager, "FlipReset", [&] {
            if (bridge_->isLocalCar(car)) sendEvent(GameEventKind::FlipReset, "sdk");
        });
    });

    // "Reset shot": freeplay uses FreeplayCommands_TA.ResetTraining, custom training PlayerResetTraining.
    auto onReset = [this](std::string) {
        const int64_t frame = bridge_->physicsFrame();
        if (frame >= 0 && std::llabs(frame - lastResetFrame_) < 12) return;  // same reset seen by both hooks (0.1 s)
        lastResetFrame_ = frame;
        sendEvent(GameEventKind::ResetShot);
    };
    hookPost("Function TAGame.FreeplayCommands_TA.ResetTraining", onReset);
    hookPost("Function TAGame.GameEvent_TA.PlayerResetTraining", onReset);
    hookPre("Function TAGame.Ball_TA.OnHitGoal", [this](std::string) { sendEvent(GameEventKind::Goal); });

    // Context changes: refresh a little later, once the game finished setting things up.
    auto delayedRefresh = [alive, this](std::string) {
        gameWrapper->SetTimeout([alive, this](GameWrapper*) {
            if (*alive) guarded(cvarManager, "refreshContext", [&] { refreshContext(); });
        }, 0.3f);
    };
    hookPost("Function TAGame.Mutator_Freeplay_TA.Init", delayedRefresh);
    hookPost("Function TAGame.GameEvent_Soccar_TA.InitGame", delayedRefresh);
    hookPost("Function GameEvent_TrainingEditor_TA.WaitingToPlayTest.OnTrainingModeLoaded", delayedRefresh);
    hookPost("Function TAGame.GameInfo_Replay_TA.InitGame", delayedRefresh);
    hookPre("Function TAGame.GameEvent_Soccar_TA.Destroyed", [alive, this](std::string) {
        sendEvent(GameEventKind::MapUnloaded);
        maps_->clearKismetCache();
        saveProgress();
        gameWrapper->SetTimeout([alive, this](GameWrapper*) {
            if (*alive) guarded(cvarManager, "refreshContext", [&] { refreshContext(); });
        }, 0.5f);
    });
}

void RLTrainer::onVehicleInput(CarWrapper car, void* params) {
    if (!bridge_->isLocalCar(car)) return;
    if (gameWrapper->IsInOnlineGame()) return;
    Snapshot snap;
    if (!bridge_->buildSnapshot(car, static_cast<const ControllerInput*>(params), snap)) return;
    Actions actions;
    engine_->tick(snap, actions);

    // Custom maps: forward Kismet variables at ~30 Hz.
    if (engine_->context().isCustomMap && (++kismetCounter_ % 4) == 0) {
        std::vector<KismetVar> vars = maps_->readKismet();
        if (!vars.empty()) engine_->kismet(vars, actions);
    }
    run(actions, params);
}

void RLTrainer::onHitBall(CarWrapper car, void* params) {
    if (!params || !bridge_->isLocalCar(car)) return;
    const auto* p = static_cast<const OnHitBallParams*>(params);
    engine_->reportTouch(bridge_->physicsFrame(), true, sdk::toVec(p->hitLocation), sdk::toVec(p->hitNormal));
}

void RLTrainer::sendEvent(GameEventKind kind, const std::string& detail) {
    GameEvent ev;
    ev.kind = kind;
    ev.frame = bridge_->physicsFrame();
    ev.detail = detail;
    Actions actions;
    engine_->gameEvent(ev, actions);
    run(actions);
}

void RLTrainer::run(Actions& actions, void* params) {
    if (actions.empty() && !params) return;
    bool accents = false;
    {
        auto lk = engine_->lock();  // settings may be changed from the render thread (UI)
        accents = engine_->settings().general.hudAccents;
    }
    bridge_->execute(actions, params, accents);
    // Settings resets requested by a module (already filtered by the engine to the module's own settings).
    for (const std::string& target : actions.resetSettings) {
        const size_t slash = target.find('/');
        const std::string module = target.substr(0, slash);
        const std::string section = slash == std::string::npos ? std::string() : target.substr(slash + 1);
        for (const SettingDef* d : engine_->schema().forModule(module)) {
            if (!section.empty() && d->section != section) continue;
            CVarWrapper cv = cvarManager->getCvar(d->cvar);
            if (!cv.IsNull()) cv.setValue(d->defaultValue);
        }
        configDirty_ = true;
    }
    if (actions.requestSave) saveTimer_ = 1e9f;  // save at the next periodic update
}

// ---- Drawing ---------------------------------------------------------------------------------------------
void RLTrainer::onDraw(CanvasWrapper canvas) {
    if (gameWrapper->IsInOnlineGame()) return;
    GeneralConfig general;
    {
        auto lk = engine_->lock();
        general = engine_->settings().general;
    }
    if (!general.enabled) return;
    ViewContext view = bridge_->buildView(canvas, general.hudScale);
    const auto now = std::chrono::steady_clock::now();
    if (hasLastDraw_) {
        const float dt = std::chrono::duration<float>(now - lastDraw_).count();
        view.frameDt = dt < 0.f ? 0.f : (dt > 0.25f ? 0.25f : dt);
    }
    lastDraw_ = now;
    hasLastDraw_ = true;
    DrawList list;
    engine_->draw(view, list);
    if (!list.empty()) renderer_.render(canvas, list, view, general.hudAccents);
}

// ---- Periodic work (game thread) -------------------------------------------------------------------------
void RLTrainer::schedulePeriodic() {
    gameWrapper->SetTimeout([alive = alive_, this](GameWrapper*) {
        if (!*alive) return;
        guarded(cvarManager, "periodic", [&] { periodic(); });
        schedulePeriodic();
    }, kPeriod);
}

void RLTrainer::periodic() {
    refreshContext();

    Actions actions;
    engine_->pump(actions);
    run(actions);

    profileTimer_ += kPeriod;
    if (profileTimer_ >= 3.f) {
        profileTimer_ = 0.f;
        refreshProfile(false);
    }

    if (configDirty_) {
        configDirtyTimer_ += kPeriod;
        if (configDirtyTimer_ >= 2.f) {
            cvarManager->executeCommand("writeconfig", false);
            configDirty_ = false;
            configDirtyTimer_ = 0.f;
        }
    }

    saveTimer_ += kPeriod;
    if (engine_->consumeSaveRequest() || saveTimer_ >= 60.f) {
        saveTimer_ = 0.f;
        saveProgress();
    }
}

void RLTrainer::refreshContext() {
    GameContext ctx;
    ctx.mapName = gameWrapper->GetCurrentMap();
    const bool stock = maps_->isStockMap(ctx.mapName);
    ctx.mode = bridge_->detectMode(stock);
    ctx.isCustomMap = ctx.mode == GameMode::Workshop;
    if (ctx.isCustomMap) maps_->describeCurrentMap(ctx.mapName, ctx);
    ctx.paused = gameWrapper->IsPaused();
    {
        const std::time_t now = std::time(nullptr);
        std::tm tm{};
#ifdef _WIN32
        localtime_s(&tm, &now);
#else
        localtime_r(&now, &tm);
#endif
        char buf[16];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm);
        ctx.localDate = buf;
    }

    const GameContext& cur = engine_->context();
    if (ctx.paused != lastPaused_) {
        lastPaused_ = ctx.paused;
        sendEvent(ctx.paused ? GameEventKind::Paused : GameEventKind::Unpaused);
    }
    if (ctx.mode != cur.mode || ctx.mapName != cur.mapName || ctx.workshopId != cur.workshopId ||
        ctx.category != cur.category || ctx.isCustomMap != cur.isCustomMap || ctx.localDate != cur.localDate) {
        Actions actions;
        engine_->setContext(ctx, actions);
        run(actions);
    }
}

void RLTrainer::refreshProfile(bool force) {
    if (!force && !profile_->quickChanged(engine_->profile())) return;
    PlayerProfile p = profile_->read(engine_->profile());
    Actions actions;
    engine_->setProfile(p, actions);
    run(actions);
}

// ---- Persistence -----------------------------------------------------------------------------------------
std::filesystem::path RLTrainer::dataDir() const { return gameWrapper->GetDataFolder() / "rltrainer"; }

void RLTrainer::loadProgress() {
    const auto file = dataDir() / "progress.txt";
    std::ifstream in(file, std::ios::binary);
    if (!in) return;
    std::stringstream ss;
    ss << in.rdbuf();
    Store store;
    store.parse(ss.str());
    engine_->load(store);
}

void RLTrainer::saveProgress() {
    if (!engine_) return;
    Store store;
    engine_->save(store);
    std::error_code ec;
    std::filesystem::create_directories(dataDir(), ec);
    const auto file = dataDir() / "progress.txt";
    const auto tmp = dataDir() / "progress.tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return;
        out << store.serialize();
        if (!out) return;
    }
    std::filesystem::rename(tmp, file, ec);
    if (ec) {
        std::filesystem::remove(file, ec);
        std::filesystem::rename(tmp, file, ec);
    }
}

// ---- ImGui -----------------------------------------------------------------------------------------------
void RLTrainer::RenderSettings() {
    if (ui_) renderUi(*ui_);
}

std::string RLTrainer::GetPluginName() { return "RL Trainer"; }

void RLTrainer::SetImGuiContext(uintptr_t ctx) { ImGui::SetCurrentContext(reinterpret_cast<ImGuiContext*>(ctx)); }

void RLTrainer::Render() {
    ImGui::SetNextWindowSize(ImVec2(720, 560), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(GetMenuTitle().c_str(), &windowOpen_, ImGuiWindowFlags_None)) {
        ImGui::End();
        return;
    }
    if (ui_) renderUi(*ui_);
    ImGui::End();
    if (!windowOpen_) {
        gameWrapper->Execute([alive = alive_, this](GameWrapper*) {
            if (*alive) cvarManager->executeCommand("togglemenu " + GetMenuName());
        });
    }
}

std::string RLTrainer::GetMenuName() { return "rltrainer"; }
std::string RLTrainer::GetMenuTitle() { return "RL Trainer"; }
bool RLTrainer::ShouldBlockInput() { return ImGui::GetIO().WantCaptureMouse || ImGui::GetIO().WantCaptureKeyboard; }
bool RLTrainer::IsActiveOverlay() { return true; }
void RLTrainer::OnOpen() { windowOpen_ = true; }
void RLTrainer::OnClose() { windowOpen_ = false; }
