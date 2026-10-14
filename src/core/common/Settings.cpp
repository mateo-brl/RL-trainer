#include "core/common/Settings.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace rlt {

static std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

bool parseBoolValue(const std::string& raw, bool& out) {
    std::string s = trim(raw);
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (s == "1" || s == "true" || s == "on" || s == "yes") { out = true; return true; }
    if (s == "0" || s == "false" || s == "off" || s == "no" || s.empty()) { out = false; return true; }
    float f;
    if (parseFloatValue(s, f)) { out = f != 0.f; return true; }
    return false;
}

bool parseFloatValue(const std::string& raw, float& out) {
    const std::string s = trim(raw);
    if (s.empty()) return false;
    try {
        size_t used = 0;
        const float f = std::stof(s, &used);
        if (used != s.size() || !std::isfinite(f)) return false;
        out = f;
        return true;
    } catch (...) {
        return false;
    }
}

bool parseIntValue(const std::string& raw, int& out) {
    float f;
    if (!parseFloatValue(raw, f)) return false;
    if (f > 2.0e9f || f < -2.0e9f) return false;
    out = static_cast<int>(std::lround(f));
    return true;
}

std::string boolToCvar(bool v) { return v ? "1" : "0"; }
std::string intToCvar(int v) { return std::to_string(v); }
std::string floatToCvar(float v) {
    char buf[48];
    std::snprintf(buf, sizeof(buf), "%.4f", static_cast<double>(v));
    std::string s = buf;
    // Trim trailing zeros but keep one decimal ("2.0").
    while (s.size() > 1 && s.back() == '0' && s[s.size() - 2] != '.') s.pop_back();
    return s;
}

const SettingDef* SettingsSchema::find(const std::string& cvar) const {
    for (const auto& d : defs_)
        if (d.cvar == cvar) return &d;
    return nullptr;
}

bool SettingsSchema::apply(Settings& s, const std::string& cvar, const std::string& value) const {
    const SettingDef* d = find(cvar);
    if (!d || !d->apply) return false;
    return d->apply(s, value);
}

void SettingsSchema::applyDefaults(Settings& s) const {
    for (const auto& d : defs_)
        if (d.apply) d.apply(s, d.defaultValue);
}

std::vector<const SettingDef*> SettingsSchema::forModule(const std::string& module) const {
    std::vector<const SettingDef*> out;
    for (const auto& d : defs_)
        if (d.module == module) out.push_back(&d);
    return out;
}

std::vector<std::string> SettingsSchema::sections(const std::string& module) const {
    std::vector<std::string> out;
    for (const auto& d : defs_)
        if (d.module == module && std::find(out.begin(), out.end(), d.section) == out.end()) out.push_back(d.section);
    return out;
}

void SettingsSchema::add(SettingDef def) {
    if (find(def.cvar)) throw std::logic_error("duplicate cvar in settings schema: " + def.cvar);
    defs_.push_back(std::move(def));
}

void describeGeneralSettings(SettingsSchema& schema) {
    auto w = schema.section(&Settings::general, "general", "Général");
    w.toggle("rlt_enabled", &GeneralConfig::enabled, true, "Activer RL Trainer",
             "Interrupteur principal. Désactivé, le plugin n'affiche et ne modifie plus rien.");
    w.number("rlt_hud_scale", &GeneralConfig::hudScale, 1.f, 0.5f, 3.f, "Taille de l'interface",
             "Multiplie la taille de tous les textes et panneaux affichés en jeu.", 2);
    w.toggle("rlt_chat_feedback", &GeneralConfig::chatFeedback, true, "Messages dans le chat",
             "Autorise les messages du plugin dans le chat (résultats, records).");
    w.toggle("rlt_toast_feedback", &GeneralConfig::toastFeedback, true, "Notifications",
             "Autorise les notifications BakkesMod (records battus, objectifs atteints).");
    w.toggle("rlt_show_in_replays", &GeneralConfig::showInReplays, true, "Afficher dans les replays",
             "Le cercle de contact et la sphère restent visibles quand tu regardes un replay.");
    w.toggle("rlt_check_updates", &GeneralConfig::checkUpdates, true, "Vérifier les mises à jour",
             "Au lancement, demande à GitHub si une nouvelle version de RL Trainer est disponible.");

    auto h = schema.section(&Settings::general, "general", "HUD");
    h.toggle("rlt_hud_compact", &GeneralConfig::hudCompact, true, "HUD compact",
             "Chaque panneau garde ses lignes essentielles : la note et le conseil principal. Les statistiques "
             "détaillées restent dans ce menu.");
    h.toggle("rlt_hud_focus", &GeneralConfig::hudFocus, true, "Un seul panneau à la fois",
             "Pendant un exercice, seul son panneau s'affiche. Sinon, seul celui du dernier résultat (touche, méca). "
             "L'écart de vitesse, le chrono et le panneau des maps restent affichés.");
    h.number("rlt_hud_text", &GeneralConfig::hudTextSize, 1.3f, 0.8f, 2.5f, "Taille du texte des panneaux",
             "Taille du texte des panneaux en jeu, en plus de la taille de l'interface.", 2);
    h.number("rlt_hud_opacity", &GeneralConfig::hudOpacity, 0.85f, 0.3f, 1.f, "Opacité du fond des panneaux",
             "Plus le fond est opaque, plus le texte reste lisible sur un décor clair.", 2);
    h.text("rlt_menu_key", &GeneralConfig::menuKey, "F3", "Touche du menu",
           "Touche qui ouvre la fenêtre RL Trainer (nom BakkesMod : F3, F7, NumPadOne...). Elle n'est assignée que "
           "si elle est libre. Vide : aucune touche.");

    auto a = schema.section(&Settings::general, "general", "Avancé");
    a.advanced();
    a.toggle("rlt_hud_accents", &GeneralConfig::hudAccents, false, "Accents dans le HUD",
             "Envoie les accents tels quels au texte en jeu. Laisse désactivé si tu vois des caractères bizarres.");
    a.toggle("rlt_control_workshop", &GeneralConfig::controlInWorkshop, false, "Exercices sur les maps workshop",
             "Autorise le plugin à déplacer la balle et la voiture sur une map workshop. Peut casser la logique de la map.");
    a.toggle("rlt_control_training", &GeneralConfig::controlInTraining, false, "Exercices en entraînement personnalisé",
             "Autorise le plugin à déplacer la balle et la voiture pendant un pack d'entraînement.");
}

} // namespace rlt
