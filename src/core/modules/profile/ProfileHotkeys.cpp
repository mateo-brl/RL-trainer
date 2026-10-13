#include "core/modules/profile/ProfileHotkeys.h"

#include <algorithm>

#include "core/common/Controls.h"

namespace rlt::pf {

namespace {
std::string lower(std::string s) {
    for (auto& c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

bool contains(const std::vector<std::string>& v, const std::string& key) {
    for (const auto& x : v)
        if (sameName(x, key)) return true;
    return false;
}

// BakkesMod bind state of a key: 0 unknown, 1 free, 2 bound to `command`, 3 bound to something else.
int bakkesState(const BakkesBinds& bakkes, const std::string& key, const std::string& command) {
    const auto it = bakkes.find(bindMapKey(key));
    if (it == bakkes.end()) return 0;
    if (it->second.empty()) return 1;
    return it->second == command ? 2 : 3;
}
} // namespace

const std::vector<HotkeyCommand>& allHotkeyCommands() {
    static const std::vector<HotkeyCommand> all = {
        {"menu", "rlt_menu", "Ouvrir RL Trainer"},
        {"stop", "rlt engine stop", "Arrêter l'exercice"},
        {"firsttouch", "rlt firsttouch drill mix", "Premières touches (mix)"},
        {"kickoff", "rlt kickoff start random", "Kickoff aléatoire"},
    };
    return all;
}

std::vector<HotkeyCommand> hotkeyCommands(const ProfileConfig& cfg) {
    std::vector<HotkeyCommand> out;
    const auto& all = allHotkeyCommands();
    if (cfg.hotkeyMenu) out.push_back(all[0]);
    if (cfg.hotkeyStop) out.push_back(all[1]);
    if (cfg.hotkeyFirstTouch) out.push_back(all[2]);
    if (cfg.hotkeyKickoff) out.push_back(all[3]);
    return out;
}

bool isKnownHotkeyCommand(const std::string& command) {
    for (const auto& c : allHotkeyCommands())
        if (c.command == command) return true;
    return false;
}

std::vector<std::string> hotkeyCandidates(bool includeGamepad) {
    std::vector<std::string> keys = {"F5",          "F7",          "F8",          "F9",          "F10",
                                     "F3",          "F4",          "F11",         "NumPadOne",   "NumPadTwo",
                                     "NumPadThree", "NumPadFour",  "NumPadFive",  "NumPadSix",   "NumPadSeven",
                                     "NumPadEight", "NumPadNine",  "NumPadZero",  "F12"};
    if (includeGamepad) {
        for (const char* k : {"XboxTypeS_DPad_Up", "XboxTypeS_DPad_Down", "XboxTypeS_DPad_Left", "XboxTypeS_DPad_Right",
                              "XboxTypeS_Back"})
            keys.emplace_back(k);
    }
    return keys;
}

bool isReservedHotkey(const std::string& key) {
    // F1 (help overlays), F2 (BakkesMod settings), F6 (BakkesMod console), Escape (pause menu).
    for (const char* k : {"F1", "F2", "F6", "Escape"})
        if (sameName(key, k)) return true;
    return key.empty();
}

std::string bindMapKey(const std::string& key) { return lower(key); }

bool keyUsedByGame(const PlayerProfile& p, const std::string& key) {
    for (const auto& b : p.pcBindings)
        if (sameName(b.key, key)) return true;
    for (const auto& b : p.gamepadBindings)
        if (sameName(b.key, key)) return true;
    return false;
}

std::vector<HotkeyProposal> pickHotkeys(const PlayerProfile& p, const std::vector<HotkeyCommand>& commands,
                                        const std::vector<std::string>& candidates, const BakkesBinds& bakkes,
                                        const std::vector<AppliedHotkey>& applied, bool allowUnchecked) {
    std::vector<HotkeyProposal> out;
    out.reserve(commands.size());
    std::vector<std::string> taken;

    for (const auto& c : commands) {
        HotkeyProposal hp;
        hp.id = c.id;
        hp.command = c.command;
        hp.label = c.label;
        out.push_back(std::move(hp));
    }

    // 1) Keys already holding our command (applied by us, or bound by hand) stay, if the game does not use them
    //    and BakkesMod still runs the command there (state 2) or was not checked yet (state 0).
    for (auto& hp : out) {
        std::string found;
        bool verified = false;
        for (const auto& a : applied) {
            if (a.command != hp.command || contains(taken, a.key) || isReservedHotkey(a.key)) continue;
            const int st = bakkesState(bakkes, a.key, hp.command);
            if (st == 3) continue;  // the player rebound it to something else: not ours any more
            if (keyUsedByGame(p, a.key)) {
                // The game took the key: a new one is proposed. Warn while our bind may still be there.
                if (st != 1 && hp.previousKey.empty()) {
                    hp.previousKey = a.key;
                    hp.previousConflict = true;
                }
                continue;
            }
            if (st == 1) {
                // Bind removed outside RL Trainer (BakkesMod tab, unbind, binds not saved): propose it again.
                if (hp.previousKey.empty() || hp.previousConflict) {
                    hp.previousKey = a.key;
                    hp.previousMissing = true;
                    hp.previousConflict = false;
                }
                continue;
            }
            found = a.key;
            verified = st == 2;
            break;
        }
        if (found.empty()) {
            for (const auto& k : candidates) {
                if (contains(taken, k) || keyUsedByGame(p, k) || isReservedHotkey(k)) continue;
                if (bakkesState(bakkes, k, hp.command) == 2) {
                    found = k;
                    verified = true;
                    break;
                }
            }
        }
        if (!found.empty()) {
            hp.key = found;
            hp.state = HotkeyState::Applied;
            hp.verified = verified;
            hp.note = verified ? "Déjà en place." : "Installé par RL Trainer.";
            hp.previousKey.clear();
            hp.previousMissing = hp.previousConflict = false;
            taken.push_back(found);
        }
    }

    // 2) Free keys for the others: first the key applied earlier for the same command (bind lost), then the
    //    candidates in order.
    for (auto& hp : out) {
        if (hp.state == HotkeyState::Applied) continue;
        const auto usable = [&](const std::string& k) {
            if (contains(taken, k) || keyUsedByGame(p, k) || isReservedHotkey(k)) return false;
            const int st = bakkesState(bakkes, k, hp.command);
            if (st == 3) return false;
            return st != 0 || allowUnchecked;
        };
        std::string chosen;
        if (hp.previousMissing && usable(hp.previousKey)) chosen = hp.previousKey;
        if (chosen.empty()) {
            for (const auto& k : candidates) {
                if (usable(k)) {
                    chosen = k;
                    break;
                }
            }
        }
        if (!chosen.empty()) {
            hp.key = chosen;
            hp.state = HotkeyState::Proposed;
            hp.verified = bakkesState(bakkes, chosen, hp.command) == 1;
            if (hp.previousMissing && sameName(chosen, hp.previousKey))
                hp.note = "Ton raccourci n'est plus dans BakkesMod : réapplique-le.";
            else if (hp.previousConflict)
                hp.note = "Ton ancienne touche sert maintenant dans le jeu : nouvelle touche proposée. Retire "
                          "l'ancienne avec \"Retirer les conflits\".";
            else
                hp.note = hp.verified ? "Libre dans le jeu et dans BakkesMod." : "Libre dans le jeu, BakkesMod à vérifier.";
            taken.push_back(chosen);
        } else {
            hp.state = HotkeyState::NoFreeKey;
            hp.note = hp.previousConflict ? "Ton ancienne touche sert maintenant dans le jeu et aucune autre touche "
                                            "candidate n'est libre."
                                          : "Aucune touche libre parmi les candidates.";
        }
    }

    for (auto& hp : out) hp.keyText = keyLabel(hp.key);
    return out;
}

bool bakkesKnownFree(const BakkesBinds& bakkes, const std::string& key) {
    const auto it = bakkes.find(bindMapKey(key));
    return it != bakkes.end() && it->second.empty();
}

std::vector<AppliedStatus> appliedStatus(const PlayerProfile& p, const BakkesBinds& bakkes,
                                         const std::vector<AppliedHotkey>& applied, bool bindingsKnown) {
    std::vector<AppliedStatus> out;
    out.reserve(applied.size());
    for (const auto& a : applied) {
        AppliedStatus s;
        s.key = a.key;
        s.command = a.command;
        const int st = bakkesState(bakkes, a.key, a.command);
        if (st == 1) s.state = AppliedState::Missing;
        else if (st == 3) s.state = AppliedState::Rebound;
        else if (bindingsKnown && keyUsedByGame(p, a.key)) s.state = AppliedState::GameConflict;
        else s.state = st == 2 ? AppliedState::Active : AppliedState::Unknown;
        out.push_back(std::move(s));
    }
    return out;
}

} // namespace rlt::pf
