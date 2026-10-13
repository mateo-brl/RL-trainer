#pragma once
// Ma config: automatic analysis of the player's own Rocket League configuration (device, bindings, camera,
// deadzones, car hitbox), advice in French, conflict-free plugin hotkeys, one toast when the config is
// detected or changes, optional in-game hitbox wireframe.
//
// The PlayerProfile itself is read by the glue (plugin/services/ProfileService) and given to every module;
// this module only analyses it. It never takes the world-control activity.
//
// Commands ("rlt profile <cmd> [args]", also posted by the "Ma config" tab):
//   refresh            ask for a new read of the config (the tab does the read itself, then posts "refresh done";
//                      the glue may do it too, see sharedChangeRequests)
//   resume             toast + chat summary of the detected config and its main advice
//   hotkeys            lists the proposed hotkeys in the chat
//   bmbinds k v ...    (tab) current BakkesMod binds of the candidate keys ("" = free)
//   applied k c ...    (tab) hotkeys just bound by the tab: kept in the undo list
//   undone k ...       (tab) hotkeys just removed by the tab (followed by "bmbinds" with the real binds)
//   forget             empties the undo list without touching the binds
//   resettoast         forgets the announced config: the detection toast shows again

#include <string>
#include <vector>

#include "core/common/Controls.h"
#include "core/common/Module.h"
#include "core/modules/profile/ProfileAnalysis.h"
#include "core/modules/profile/ProfileConfig.h"
#include "core/modules/profile/ProfileHotkeys.h"

namespace rlt {

// Everything the UI shows, rebuilt when the profile, the settings or the hotkey state change (never per tick).
// Plain data: the tab copies it under the engine lock.
struct ProfileReport {
    bool valid = false;                          // a profile was read by the glue
    PlayerProfile profile;                       // last profile received
    KeyStyle style = KeyStyle::Auto;
    std::string device;                          // "Manette" / "Clavier et souris"
    std::string summary;                         // "manette, Octane, caméra balle"
    std::string car;                             // "Fennec (hitbox Octane)"
    pf::HitboxMatch hitbox;
    std::vector<pf::SettingCheck> camera;
    std::vector<pf::SettingCheck> controller;    // deadzones + sensitivities
    std::string dodgeExplanation;
    std::vector<pf::BindingRow> bindings;
    std::vector<pf::AuditItem> audit;
    std::vector<pf::MechanicReadiness> mechanics;   // empty when deviceBindingsReadable is false
    bool deviceBindingsReadable = false;         // keys of the device in use known (pf::deviceBindingsReadable)
    bool showCameraAdvice = true, showControllerAdvice = true, showBindingAdvice = true;
    int adviceCount = 0;                         // "Conseil" lines in the enabled categories

    std::vector<pf::HotkeyProposal> hotkeys;
    std::vector<pf::AppliedHotkey> applied;      // undo list (persisted)
    std::vector<pf::AppliedStatus> appliedStatus;  // same entries with their current state
    int hotkeyConflicts = 0;                     // applied keys now used by the game (bind maybe still active)
    int hotkeysMissing = 0;                      // applied keys whose BakkesMod bind disappeared
    std::vector<std::string> hotkeyCandidates;   // keys whose BakkesMod bind the tab should check
    bool hotkeysReady = false;                   // the game bindings are known (needed to pick safe keys)
    bool bakkesChecked = false;                  // every candidate's BakkesMod bind is known
    int pendingProposals = 0;                    // proposals not applied yet

    int reads = 0;                               // profiles received since the plugin started
    int forcedReads = 0;                         // reads forced from the tab
    bool refreshPending = false;                 // "refresh" asked, no read since
    std::string lastChange;                      // what changed at the last read ("caméra modifiée"...)
};

class ProfileModule : public Module {
public:
    static constexpr size_t kMaxApplied = 16;
    static constexpr int kDetectionWaitTicks = 600;  // wait up to 5 s of play for the car before the first toast

    const char* id() const override { return "profile"; }
    const char* title() const override { return "Ma config"; }

    static void describeSettings(SettingsSchema& schema);

    void onSettingsChanged(const Settings& settings) override;
    void onProfileChanged(const PlayerProfile& profile) override;
    void onTick(const TickContext& ctx, Actions& out) override;
    void onGameEvent(const GameEvent& ev, const TickContext& ctx, Actions& out) override;
    void onCommand(const Command& cmd, const TickContext& ctx, Actions& out) override;
    void draw(const ViewContext& view, const DrawContext& ctx, DrawList& out) override;
    void save(Store& store) const override;
    void load(const Store& store) override;

    const ProfileConfig& config() const { return cfg_; }
    const ProfileReport& report() const { return report_; }
    bool toastPending() const { return pendingDetection_ || !pendingChanges_.empty(); }

private:
    void rebuild();
    void rebuildHotkeys();
    void flush(Actions& out, bool fromTick, GameMode mode);
    void queueDetection();
    void addChange(const std::string& change);
    std::string adviceSuffix() const;
    void toast(Actions& out, const std::string& text, ToastKind kind, float seconds = 5.f) const;
    void chat(Actions& out, const std::string& text) const;

    ProfileConfig cfg_;
    PlayerProfile profile_;
    ProfileReport report_;

    // Hotkeys.
    pf::BakkesBinds bakkes_;
    std::vector<pf::AppliedHotkey> applied_;

    // Change tracking (persisted).
    pf::ProfileSignature sig_;
    int announcedDevices_ = 0;   // bit 0: keyboard/mouse announced, bit 1: gamepad announced
    bool announced_ = false;     // the first detection toast was shown

    // Transient.
    bool pendingDetection_ = false;
    std::vector<std::string> pendingChanges_;
    int waitTicks_ = 0;
    bool lastReadQueuedToast_ = false;
    std::string lastReadChanges_;   // what the last read changed (empty when nothing)
    std::string lastChange_;        // last non-empty change, shown in the tab
    bool saveDirty_ = false;
    int reads_ = 0;
    int forcedReads_ = 0;
    bool refreshPending_ = false;
};

} // namespace rlt
