#include "plugin/RLTrainer.h"

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
} // namespace

// ---- Lifecycle -------------------------------------------------------------------------------------------
void RLTrainer::onLoad() {
    _globalCvarManager = cvarManager;
    engine_ = std::make_unique<Engine>();
    bridge_ = std::make_unique<GameBridge>(gameWrapper, cvarManager);
    profile_ = std::make_unique<ProfileService>(gameWrapper, cvarManager);
    maps_ = std::make_unique<MapsService>(gameWrapper, cvarManager);
    maps_->init();
    ui_ = std::unique_ptr<UiContext>(new UiContext{*engine_, cvarManager, gameWrapper, profile_.get(), maps_.get(), false, &configDirty_});

    registerCvars();
    loadProgress();
    registerHooks();
    registerCommands();

    gameWrapper->RegisterDrawable([alive = alive_, this](CanvasWrapper canvas) {
        if (*alive) onDraw(canvas);
    });

    refreshContext();
    refreshProfile(true);
    schedulePeriodic();
    cvarManager->log(std::string("RL Trainer ") + kPluginVersionString + " charge.");
}

void RLTrainer::onUnload() {
    *alive_ = false;
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
            if (*alive && engine_) engine_->applySetting(name, c.getStringValue());
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
            if (Engine::parseCommandLine(args, c)) engine_->post(c);
            else cvarManager->log("Usage : rlt <module> <commande> [arguments]. Ex : rlt kickoff start");
        },
        "RL Trainer : rlt <module> <commande> [arguments]", PERMISSION_ALL);
    cvarManager->registerNotifier(
        "rlt_menu", [this](std::vector<std::string>) { cvarManager->executeCommand("togglemenu " + GetMenuName()); },
        "Ouvre ou ferme la fenêtre RL Trainer", PERMISSION_ALL);
}

// ---- Hooks -----------------------------------------------------------------------------------------------
void RLTrainer::registerHooks() {
    auto alive = alive_;
    gameWrapper->HookEventWithCaller<CarWrapper>(
        "Function TAGame.Car_TA.SetVehicleInput", [alive, this](CarWrapper car, void* params, std::string) {
            if (*alive) onVehicleInput(car, params);
        });
    gameWrapper->HookEventWithCallerPost<CarWrapper>(
        "Function TAGame.Car_TA.OnHitBall", [alive, this](CarWrapper car, void* params, std::string) {
            if (*alive) onHitBall(car, params);
        });
    gameWrapper->HookEventPost("Function TAGame.GameEvent_TA.PlayerResetTraining", [alive, this](std::string) {
        if (*alive) sendEvent(GameEventKind::ResetShot);
    });
    gameWrapper->HookEvent("Function TAGame.Ball_TA.OnHitGoal", [alive, this](std::string) {
        if (*alive) sendEvent(GameEventKind::Goal);
    });
    gameWrapper->HookEventWithCallerPost<CarWrapper>(
        "Function TAGame.Car_TA.EventPerformedFlipReset", [alive, this](CarWrapper car, void*, std::string) {
            if (*alive && bridge_->isLocalCar(car)) sendEvent(GameEventKind::FlipReset, "sdk");
        });

    // Context changes: refresh a little later, once the game finished setting things up.
    auto delayedRefresh = [alive, this](std::string) {
        if (!*alive) return;
        gameWrapper->SetTimeout([alive, this](GameWrapper*) {
            if (*alive) refreshContext();
        }, 0.3f);
    };
    gameWrapper->HookEventPost("Function TAGame.Mutator_Freeplay_TA.Init", delayedRefresh);
    gameWrapper->HookEventPost("Function TAGame.GameEvent_Soccar_TA.InitGame", delayedRefresh);
    gameWrapper->HookEventPost("Function GameEvent_TrainingEditor_TA.WaitingToPlayTest.OnTrainingModeLoaded",
                               delayedRefresh);
    gameWrapper->HookEventPost("Function TAGame.GameInfo_Replay_TA.InitGame", delayedRefresh);
    gameWrapper->HookEvent("Function TAGame.GameEvent_Soccar_TA.Destroyed", [alive, this](std::string) {
        if (!*alive) return;
        sendEvent(GameEventKind::MapUnloaded);
        maps_->clearKismetCache();
        saveProgress();
        gameWrapper->SetTimeout([alive, this](GameWrapper*) {
            if (*alive) refreshContext();
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
    DrawList list;
    engine_->draw(view, list);
    if (!list.empty()) renderer_.render(canvas, list, view, general.hudAccents);
}

// ---- Periodic work (game thread) -------------------------------------------------------------------------
void RLTrainer::schedulePeriodic() {
    gameWrapper->SetTimeout([alive = alive_, this](GameWrapper*) {
        if (!*alive) return;
        periodic();
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

    const GameContext& cur = engine_->context();
    if (ctx.paused != lastPaused_) {
        lastPaused_ = ctx.paused;
        sendEvent(ctx.paused ? GameEventKind::Paused : GameEventKind::Unpaused);
    }
    if (ctx.mode != cur.mode || ctx.mapName != cur.mapName || ctx.workshopId != cur.workshopId ||
        ctx.category != cur.category || ctx.isCustomMap != cur.isCustomMap) {
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
