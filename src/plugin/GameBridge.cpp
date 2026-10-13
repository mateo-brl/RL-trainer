#include "plugin/GameBridge.h"

#include <algorithm>

#include "core/common/Text.h"

namespace rlt {

GameBridge::GameBridge(std::shared_ptr<GameWrapper> gw, std::shared_ptr<CVarManagerWrapper> cvars)
    : gw_(std::move(gw)), cvars_(std::move(cvars)) {}

ServerWrapper GameBridge::currentServer() const {
    // GetCurrentGameState covers freeplay, training, workshop and offline matches.
    return gw_->GetCurrentGameState();
}

bool GameBridge::isLocalCar(CarWrapper car) const {
    if (car.IsNull()) return false;
    CarWrapper local = gw_->GetLocalCar();
    return !local.IsNull() && local.memory_address == car.memory_address;
}

int64_t GameBridge::physicsFrame() const {
    EngineTAWrapper engine = gw_->GetEngine();
    if (engine.IsNull()) return -1;
    return static_cast<int64_t>(engine.GetPhysicsFrame());
}

bool GameBridge::buildSnapshot(CarWrapper car, const ControllerInput* input, Snapshot& s) const {
    if (car.IsNull()) return false;
    const int64_t frame = physicsFrame();
    if (frame < 0) return false;
    s = Snapshot{};
    s.frame = frame;
    s.time = static_cast<float>(frame) / kTickRate;
    s.paused = gw_->IsPaused();

    // ---- Car
    CarState& c = s.car;
    RBState rb = car.GetRBState();
    c.valid = true;
    c.pos = sdk::toVec(rb.Location);
    c.vel = sdk::toVec(rb.LinearVelocity);
    c.angVel = sdk::toVec(rb.AngularVelocity);
    c.rot = sdk::toRot(car.GetRotation());
    c.basis = basisFromRot(c.rot);
    c.wheelContacts = car.GetNumWheelContacts();
    c.onGround = car.IsOnGround();
    c.onWall = car.IsOnWall();
    c.groundNormal = sdk::toVec(car.GetGroundNormal());
    c.hasFlip = car.HasFlip() != 0;
    c.jumped = car.GetbJumped() != 0;
    c.doubleJumped = car.GetbDoubleJumped() != 0;
    c.dodging = car.IsDodging();
    if (c.dodging) {
        DodgeComponentWrapper dodge = car.GetDodgeComponent();
        if (!dodge.IsNull()) c.dodgeDir = sdk::toVec(dodge.GetDodgeDirection());
    }
    c.supersonic = car.GetbSuperSonic() != 0;
    BoostWrapper boost = car.GetBoostComponent();
    c.boost = boost.IsNull() ? 0.f : boost.GetCurrentBoostAmount() * 100.f;
    c.team = car.GetTeamNum2();
    Vec3 extent = sdk::toVec(car.GetLocalCollisionExtent());
    if (extent.x > 1.f && extent.y > 1.f && extent.z > 1.f) {
        // The SDK returns half extents (HitboxPlugin draws corners at +-extent); store full sizes.
        if (extent.x < 90.f) extent = extent * 2.f;
        c.hitboxExtent = extent;
        c.hitboxOffset = sdk::toVec(car.GetLocalCollisionOffset());
    }
    c.lastBallTouchFrame = car.GetLastBallTouchFrame();

    // ---- Inputs
    if (input) {
        Inputs& in = s.in;
        in.throttle = input->Throttle;
        in.steer = input->Steer;
        in.pitch = input->Pitch;
        in.yaw = input->Yaw;
        in.roll = input->Roll;
        in.dodgeForward = input->DodgeForward;
        in.dodgeStrafe = input->DodgeStrafe;
        in.jump = input->Jump != 0;
        in.boost = input->ActivateBoost != 0;
        in.holdingBoost = input->HoldingBoost != 0;
        in.handbrake = input->Handbrake != 0;
        in.jumped = input->Jumped != 0;
    }

    // ---- Ball
    ServerWrapper server = currentServer();
    if (!server.IsNull()) {
        BallWrapper ball = server.GetBall();
        if (!ball.IsNull()) {
            RBState brb = ball.GetRBState();
            s.ball.valid = true;
            s.ball.pos = sdk::toVec(brb.Location);
            s.ball.vel = sdk::toVec(brb.LinearVelocity);
            s.ball.angVel = sdk::toVec(brb.AngularVelocity);
            const float r = ball.GetRadius();
            if (r > 10.f) s.ball.radius = r;
        }
    }
    return true;
}

ViewContext GameBridge::buildView(CanvasWrapper& canvas, float hudScale) const {
    ViewContext v;
    const Vector2 size = canvas.GetSize();
    v.screenW = static_cast<float>(size.X);
    v.screenH = static_cast<float>(size.Y);
    v.uiScale = hudScale;
    CameraWrapper cam = gw_->GetCamera();
    if (!cam.IsNull()) {
        v.valid = true;
        v.camPos = sdk::toVec(cam.GetLocation());
        v.camBasis = basisFromRot(sdk::toRot(cam.GetRotation()));
        const float fov = cam.GetFOV();
        v.fovDeg = fov > 1.f ? fov : 90.f;
    }
    CarWrapper car = gw_->GetLocalCar();
    if (!car.IsNull()) {
        v.carValid = true;
        v.carVisualPos = sdk::toVec(car.GetLocation());
        v.carVisualBasis = basisFromRot(sdk::toRot(car.GetRotation()));
        v.carVel = sdk::toVec(car.GetVelocity());
    }
    ServerWrapper server = gw_->IsInReplay() ? ServerWrapper(gw_->GetGameEventAsReplay().memory_address) : currentServer();
    if (!server.IsNull()) {
        BallWrapper ball = server.GetBall();
        if (!ball.IsNull()) {
            v.ballValid = true;
            v.ballVisualPos = sdk::toVec(ball.GetLocation());
            v.ballVel = sdk::toVec(ball.GetVelocity());
            const float r = ball.GetRadius();
            if (r > 10.f) v.ballRadius = r;
        }
    }
    return v;
}

GameMode GameBridge::detectMode(bool isStockMap) const {
    if (gw_->IsInOnlineGame()) return GameMode::Online;
    if (gw_->IsInReplay()) return GameMode::Replay;
    if (gw_->IsInCustomTraining()) return GameMode::CustomTraining;
    if (gw_->IsInFreeplay()) return isStockMap ? GameMode::Freeplay : GameMode::Workshop;
    if (gw_->IsInGame()) return isStockMap ? GameMode::OfflineMatch : GameMode::Workshop;
    return GameMode::Menu;
}

void GameBridge::restoreBoostRules() {
    if (!boostSaved_) return;
    boostSaved_ = false;
    CarWrapper car = gw_->GetLocalCar();
    if (car.IsNull()) return;
    BoostWrapper b = car.GetBoostComponent();
    if (b.IsNull()) return;
    b.SetUnlimitedBoostRefCount(savedUnlimitedRefCount_);
    b.SetbNoBoost(savedNoBoost_);
}

void GameBridge::runConsole(const std::string& cmd) {
    // Allow-list: only map navigation commands may come from the core.
    auto starts = [&](const char* p) { return cmd.rfind(p, 0) == 0; };
    if (starts("load_workshop ") || cmd == "load_freeplay") {
        cvars_->executeCommand(cmd, false);
    } else if (starts("unreal:servertravel") || starts("unreal:start MENU_Main_p")) {
        gw_->ExecuteUnrealCommand(cmd.substr(7));
    } else if (starts("writeconfig")) {
        cvars_->executeCommand("writeconfig", false);
    }
}

void GameBridge::execute(const Actions& a, void* params, bool hudAccents) {
    CarWrapper car = gw_->GetLocalCar();

    // ---- Inputs (only meaningful inside the SetVehicleInput pre-hook)
    if (params) {
        auto* ci = static_cast<ControllerInput*>(params);
        if (a.freezeCar) {
            *ci = ControllerInput{};
            gw_->OverrideParams(ci, sizeof(ControllerInput));
        } else if (a.overrideInputs) {
            const Inputs& in = *a.overrideInputs;
            ControllerInput o{};
            o.Throttle = in.throttle;
            o.Steer = in.steer;
            o.Pitch = in.pitch;
            o.Yaw = in.yaw;
            o.Roll = in.roll;
            o.DodgeForward = in.dodgeForward;
            o.DodgeStrafe = in.dodgeStrafe;
            o.Jump = in.jump ? 1 : 0;
            o.ActivateBoost = in.boost ? 1 : 0;
            o.HoldingBoost = in.holdingBoost ? 1 : 0;
            o.Handbrake = in.handbrake ? 1 : 0;
            *ci = o;
            gw_->OverrideParams(ci, sizeof(ControllerInput));
        }
        if (!car.IsNull() && a.freezeCar != frozen_) {
            car.SetbDriving(a.freezeCar ? 0 : 1);
            frozen_ = a.freezeCar;
        }
    }

    // ---- World placement
    if (a.setCar && !car.IsNull()) {
        const CarPlacement& p = *a.setCar;
        car.SetLocation(sdk::toVector(p.pos));
        car.SetRotation(sdk::toRotator(p.rot));
        car.SetVelocity(sdk::toVector(p.vel));
        car.SetAngularVelocity(sdk::toVector(p.angVel), false);
        if (p.boost) {
            BoostWrapper b = car.GetBoostComponent();
            if (!b.IsNull()) b.SetBoostAmount(std::clamp(*p.boost, 0.f, 100.f) / 100.f);
        }
    }
    if (a.restoreBoostRules) restoreBoostRules();
    if (a.boostRules && !car.IsNull()) {
        BoostWrapper b = car.GetBoostComponent();
        if (!b.IsNull()) {
            if (!boostSaved_) {
                savedUnlimitedRefCount_ = b.GetUnlimitedBoostRefCount();
                savedNoBoost_ = b.GetbNoBoost();
                boostSaved_ = true;
            }
            b.SetUnlimitedBoostRefCount(a.boostRules->unlimited ? 1 : 0);
            b.SetbNoBoost(a.boostRules->noBoost ? 1 : 0);
        }
    }
    if (a.resetPickups) {
        ServerWrapper server = currentServer();
        if (!server.IsNull()) server.ResetPickups();
    }
    if (a.setBoost && !car.IsNull()) {
        BoostWrapper b = car.GetBoostComponent();
        if (!b.IsNull()) b.SetBoostAmount(std::clamp(*a.setBoost, 0.f, 100.f) / 100.f);
    }
    if (a.setBall) {
        ServerWrapper server = currentServer();
        if (!server.IsNull()) {
            BallWrapper ball = server.GetBall();
            if (!ball.IsNull()) {
                ball.SetLocation(sdk::toVector(a.setBall->pos));
                ball.SetVelocity(sdk::toVector(a.setBall->vel));
                ball.SetAngularVelocity(sdk::toVector(a.setBall->angVel), false);
            }
        }
    }
    if (a.setGameSpeed) {
        CVarWrapper speed = cvars_->getCvar("sv_soccar_gamespeed");
        if (!speed.IsNull()) speed.setValue(std::clamp(*a.setGameSpeed, 0.05f, 2.f));
    }

    // ---- Native countdown messages
    if (a.countdownMessage || a.goMessage) {
        ServerWrapper server = currentServer();
        PlayerControllerWrapper pc = gw_->GetPlayerController();
        if (!server.IsNull() && !pc.IsNull()) {
            if (a.countdownMessage) server.SendCountdownMessage(*a.countdownMessage, pc);
            if (a.goMessage) server.SendGoMessage(pc);
        }
    }

    // ---- Feedback
    for (const auto& m : a.chat) gw_->LogToChatbox(hudText(m.text, hudAccents), hudText(m.sender, hudAccents));
    for (const auto& t : a.toasts)
        gw_->Toast(hudText(t.title, hudAccents), hudText(t.text, hudAccents), "default", t.seconds,
                   static_cast<uint8_t>(t.kind));
    for (const auto& c : a.consoleCommands) runConsole(c);
}

} // namespace rlt
