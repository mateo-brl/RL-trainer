#include "core/modules/profile/ProfileAnalysis.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace rlt::pf {

namespace {

float roundTo(float v, int decimals) {
    const float scale = std::pow(10.f, static_cast<float>(std::clamp(decimals, 0, 4)));
    return std::round(v * scale) / scale;
}

float presetDistance(const Vec3& size, const Vec3& offset, const HitboxPreset& p, float pad) {
    const float dl = size.x - (p.size.x + pad), dw = size.y - (p.size.y + pad), dh = size.z - (p.size.z + pad);
    const float ox = offset.x - p.offset.x, oz = offset.z - p.offset.z;
    return std::sqrt(dl * dl + dw * dw + dh * dh + ox * ox + oz * oz);
}

bool finiteVec(const Vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

std::string joinLabels(const std::vector<std::string>& keys, KeyStyle style) {
    std::vector<std::string> labels;
    for (const auto& k : keys) {
        const std::string l = keyLabel(k, style);
        if (!l.empty() && std::find(labels.begin(), labels.end(), l) == labels.end()) labels.push_back(l);
    }
    std::string out;
    for (const auto& l : labels) {
        if (!out.empty()) out += " / ";
        out += l;
    }
    return out;
}

bool sharesKey(const std::vector<std::string>& a, const std::vector<std::string>& b) {
    for (const auto& x : a)
        for (const auto& y : b)
            if (sameName(x, y)) return true;
    return false;
}

bool isFaceButton(const std::string& key) {
    return sameName(key, "XboxTypeS_A") || sameName(key, "XboxTypeS_B") || sameName(key, "XboxTypeS_X") ||
           sameName(key, "XboxTypeS_Y");
}

std::string lower(std::string s) {
    for (auto& c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

SettingCheck rangeCheck(const char* id, const char* label, float value, float lo, float hi, int decimals, float margin,
                        const char* adviceLow, const char* adviceHigh) {
    SettingCheck c;
    c.id = id;
    c.label = label;
    c.value = value;
    c.lo = lo;
    c.hi = hi;
    c.decimals = decimals;
    c.valueText = formatValue(value, decimals);
    c.rangeText = hi > lo ? formatValue(lo, decimals) + " à " + formatValue(hi, decimals) : formatValue(lo, decimals);
    const float shown = roundTo(value, decimals);
    // A single-value range (FOV 110) widens as if it were 5 % of its value wide.
    const float span = hi > lo ? hi - lo : std::max(1.f, std::fabs(hi) * 0.05f);
    const float widen = std::max(0.f, margin) * span;
    const float eps = 1e-4f;
    if (!std::isfinite(value)) {
        c.status = CheckStatus::Info;
        c.advice = "Valeur illisible pour l'instant.";
    } else if (shown < lo - widen - eps) {
        c.status = CheckStatus::Advice;
        c.advice = adviceLow;
    } else if (shown > hi + widen + eps) {
        c.status = CheckStatus::Advice;
        c.advice = adviceHigh;
    }
    return c;
}

SettingCheck infoCheck(const char* id, const char* label, float value, int decimals, const char* text) {
    SettingCheck c;
    c.id = id;
    c.label = label;
    c.value = value;
    c.decimals = decimals;
    c.valueText = formatValue(value, decimals);
    c.status = CheckStatus::Info;
    c.advice = text;
    return c;
}

} // namespace

// ---- Hitbox ------------------------------------------------------------------------------------------------
const std::vector<HitboxPreset>& hitboxPresets() {
    static const std::vector<HitboxPreset> presets = {
        {HitboxFamily::Octane, "Octane", {118.0074f, 84.19941f, 36.15907f}, {13.87566f, 0.f, 20.75499f},
         "Octane, Fennec", "Haute et équilibrée, la plus jouée."},
        {HitboxFamily::Dominus, "Dominus", {127.9268f, 83.27995f, 31.3f}, {9.f, 0.f, 15.75f}, "Dominus",
         "Longue et plate, appréciée pour les flicks."},
        {HitboxFamily::Plank, "Plank", {128.8198f, 84.67036f, 29.3944f}, {9.008572f, 0.f, 12.0942f},
         "Paladin, Batmobile", "La plus plate, toit très bas."},
        {HitboxFamily::Breakout, "Breakout", {131.4924f, 80.521f, 30.3f}, {12.5f, 0.f, 11.75f}, "Breakout",
         "Longue, étroite et basse, nez très en avant."},
        {HitboxFamily::Hybrid, "Hybrid", {127.0192f, 82.18787f, 34.15907f}, {13.87566f, 0.f, 20.75499f}, "Venom, Endo",
         "Entre Octane et Dominus."},
        {HitboxFamily::Merc, "Merc", {120.7202f, 76.71031f, 41.6591f}, {11.37566f, 0.f, 21.50499f}, "Merc",
         "La plus haute, toit court."},
    };
    return presets;
}

const HitboxPreset* findPreset(HitboxFamily f) {
    for (const auto& p : hitboxPresets())
        if (p.family == f) return &p;
    return nullptr;
}

const char* hitboxFamilyName(HitboxFamily f) {
    const HitboxPreset* p = findPreset(f);
    return p ? p->name : "Inconnue";
}

const char* bodyNameFromId(int bodyId) {
    switch (bodyId) {
    case 21: return "Backfire";
    case 22: return "Breakout";
    case 23: return "Octane";
    case 24: return "Paladin";
    case 25: return "Road Hog";
    case 26: return "Gizmo";
    case 27: return "Sweet Tooth";
    case 28: return "X-Devil";
    case 29: return "Hotshot";
    case 30: return "Merc";
    case 31: return "Venom";
    case 402: return "Takumi";
    case 403: return "Dominus";
    case 404: return "Scarab";
    case 523: return "Zippy";
    case 597: return "DeLorean";
    case 600: return "Ripper";
    case 607: return "Grog";
    case 625: return "Armadillo";
    case 723: return "Warthog";
    case 803: return "Batmobile";
    case 1018: return "Dominus GT";
    case 1159: return "X-Devil Mk2";
    case 1171: return "Masamune";
    case 1172: return "Marauder";
    case 1286: return "Aftershock";
    case 1295: return "Takumi RX-T";
    case 1300: return "Road Hog XL";
    case 1317: return "Esper";
    case 1416: return "Breakout Type-S";
    case 1475: return "Proteus";
    case 1478: return "Triton";
    case 1533: return "Vulcan";
    case 1568: return "Octane ZSR";
    case 1603: return "Twinmill III";
    case 1623: return "Bone Shaker";
    case 1624: return "Endo";
    case 1675: return "Ice Charger";
    case 1691: return "Mantis";
    case 1856: return "Jäger 619 RS";
    case 1919: return "Centio";
    case 1932: return "Animus GP";
    default: return nullptr;
    }
}

HitboxMatch classifyHitbox(const Vec3& extent, const Vec3& offset, int bodyId, const std::string& bodyName) {
    HitboxMatch m;
    m.bodyId = bodyId;
    if (!bodyName.empty()) m.bodyName = bodyName;
    else if (const char* n = bodyNameFromId(bodyId)) m.bodyName = n;

    Vec3 size = extent;
    if (size.x > 1.f && size.x < 90.f) size = size * 2.f;  // half extents (as the SDK returns them)
    m.size = size;
    m.offset = offset;
    if (!finiteVec(size) || !finiteVec(offset) || size.x < 1.f || size.y < 1.f || size.z < 1.f) return m;

    m.roofAboveOrigin = offset.z + size.z * 0.5f;
    m.noseAhead = offset.x + size.x * 0.5f;
    m.tailBehind = size.x * 0.5f - offset.x;

    float best = 1e9f, second = 1e9f;
    HitboxFamily bestF = HitboxFamily::Unknown, secondF = HitboxFamily::Unknown;
    bool bestRs = false;
    for (const auto& p : hitboxPresets()) {
        const float d0 = presetDistance(size, offset, p, 0.f);
        const float d1 = presetDistance(size, offset, p, kRocketSimPad);
        const float d = std::min(d0, d1);
        if (d < best) {
            second = best;
            secondF = bestF;
            best = d;
            bestF = p.family;
            bestRs = d1 < d0;
        } else if (d < second) {
            second = d;
            secondF = p.family;
        }
    }
    m.distance = best;
    m.runnerUp = secondF;
    m.margin = second - best;
    m.rocketSimScale = bestRs;
    if (best <= kHitboxExactTol) m.quality = MatchQuality::Exact;
    else if (best <= kHitboxCloseTol) m.quality = MatchQuality::Close;
    else m.quality = MatchQuality::Far;
    m.family = m.quality == MatchQuality::Far ? HitboxFamily::Unknown : bestF;
    if (m.quality == MatchQuality::Far) m.runnerUp = bestF;  // nearest preset, for the label
    return m;
}

HitboxMatch classifyHitbox(const PlayerProfile& p) {
    return classifyHitbox(p.hitboxExtent, p.hitboxOffset, p.carBodyId, p.carBodyName);
}

std::string carLabel(const HitboxMatch& m) {
    if (m.quality == MatchQuality::None)
        return m.bodyName.empty() ? std::string("Voiture pas encore lue") : m.bodyName + " (hitbox pas encore lue)";
    if (m.quality == MatchQuality::Far) {
        const std::string who = m.bodyName.empty() ? std::string("Voiture") : m.bodyName;
        return who + " (hitbox non standard, proche " + hitboxFamilyName(m.runnerUp) + ")";
    }
    const std::string fam = hitboxFamilyName(m.family);
    if (m.bodyName.empty() || sameName(m.bodyName, fam)) return fam;
    return m.bodyName + " (hitbox " + fam + ")";
}

// ---- Camera ------------------------------------------------------------------------------------------------
std::vector<SettingCheck> analyzeCamera(const CameraProfile& cam, float margin) {
    std::vector<SettingCheck> out;
    out.reserve(8);
    out.push_back(rangeCheck("camera.fov", "FOV", cam.fov, 110.f, 110.f, 0, margin,
                             "FOV réduit : presque tous les pros jouent à 110, tu verrais plus de terrain autour de toi.",
                             "FOV au-dessus de 110 : valeur inhabituelle."));
    out.push_back(rangeCheck("camera.distance", "Distance", cam.distance, 260.f, 290.f, 0, margin,
                             "Caméra proche : entre 260 et 290, tu vois mieux ta voiture et la balle ensemble.",
                             "Caméra éloignée : entre 260 et 290, les touches fines se jugent mieux."));
    out.push_back(rangeCheck("camera.height", "Hauteur", cam.height, 90.f, 110.f, 0, margin,
                             "Caméra basse : entre 90 et 110, tu lis mieux le terrain derrière la balle.",
                             "Caméra haute : entre 90 et 110, la hauteur de la balle se juge mieux."));
    out.push_back(rangeCheck("camera.angle", "Angle", cam.pitch, -5.f, -3.f, 0, margin,
                             "Angle très plongeant : entre -5 et -3, tu gardes plus d'horizon visible.",
                             "Angle presque à plat : entre -5 et -3, tu vois mieux le sol devant toi."));
    out.push_back(rangeCheck("camera.stiffness", "Rigidité", cam.stiffness, 0.35f, 0.6f, 2, margin,
                             "Rigidité basse : la caméra recule beaucoup à pleine vitesse. Entre 0.35 et 0.6, l'image "
                             "reste plus stable.",
                             "Rigidité haute : la caméra colle à la voiture et la vitesse se sent moins. 0.35 à 0.6 est "
                             "plus courant."));
    out.push_back(rangeCheck("camera.swivel", "Vitesse de pivot", cam.swivelSpeed, 3.f, 6.f, 1, margin,
                             "Pivot lent : entre 3 et 6, tu regardes autour de toi plus vite.",
                             "Pivot rapide : la caméra peut sembler nerveuse. 3 à 6 est plus courant."));
    out.push_back(rangeCheck("camera.transition", "Transition", cam.transitionSpeed, 1.f, 1.3f, 1, margin,
                             "Transition lente : la caméra balle met du temps à se recaler. 1 à 1.3 est plus courant.",
                             "Transition rapide : passer de caméra balle à libre peut désorienter. 1 à 1.3 est plus "
                             "courant."));
    SettingCheck ball;
    ball.id = "camera.ball_cam";
    ball.label = "Caméra balle";
    ball.isFlag = true;
    ball.flag = cam.ballCam;
    ball.value = cam.ballCam ? 1.f : 0.f;
    ball.rangeText = "Activée";
    ball.valueText = cam.ballCam ? "Activée" : "Coupée";
    if (!cam.ballCam) {
        ball.status = CheckStatus::Advice;
        ball.advice = "Caméra balle coupée en ce moment : la plupart des joueurs la gardent activée et la coupent "
                      "pour dribbler ou se replacer.";
    }
    out.push_back(std::move(ball));
    return out;
}

// ---- Controller --------------------------------------------------------------------------------------------
std::vector<SettingCheck> analyzeController(const PlayerProfile& p, float margin) {
    std::vector<SettingCheck> out;
    out.reserve(4);
    if (!p.usingGamepad) {
        out.push_back(infoCheck("pad.dodge_deadzone", "Dodge deadzone", p.dodgeDeadzone, 2,
                                "Au clavier, tes directions sont toujours à fond : la dodge deadzone ne joue pas."));
        out.push_back(infoCheck("pad.deadzone", "Deadzone manette", p.controllerDeadzone, 2,
                                "Sans effet au clavier-souris."));
        return out;
    }
    out.push_back(rangeCheck("pad.dodge_deadzone", "Dodge deadzone", p.dodgeDeadzone, 0.5f, 0.7f, 2, margin,
                             "Dodge deadzone basse : un stick à peine incliné donne un flip au lieu d'un double saut. "
                             "0.5 à 0.7 évite les flips involontaires.",
                             "Dodge deadzone haute : il faut pousser le stick très loin pour flipper. 0.5 à 0.7 rend "
                             "les flicks et speedflips plus faciles à déclencher."));
    out.push_back(rangeCheck("pad.deadzone", "Deadzone manette", p.controllerDeadzone, 0.05f, 0.12f, 2, margin,
                             "Deadzone très basse : si ton stick a du jeu, la voiture peut tourner toute seule.",
                             "Deadzone haute : les petites corrections (dribble, air roll fin) sont ignorées. 0.05 à "
                             "0.12 est plus courant."));
    out.push_back(infoCheck("pad.steering", "Sensibilité direction", p.steeringSensitivity, 2,
                            "Multiplie l'effet de ton stick au sol."));
    out.push_back(infoCheck("pad.aerial", "Sensibilité aérienne", p.airControlSensitivity, 2,
                            "Multiplie l'effet de ton stick en l'air."));
    return out;
}

std::string dodgeDeadzoneExplanation(const PlayerProfile& p) {
    if (!p.usingGamepad)
        return "Au clavier, chaque direction est à 100 % : un saut avec une direction donne toujours un flip.";
    const int pct = static_cast<int>(std::lround(clamp01(p.dodgeDeadzone) * 100.f));
    return "Ton stick doit dépasser " + std::to_string(pct) +
           " % de son inclinaison pour qu'un second saut devienne un flip ; en dessous, c'est un double saut. "
           "Le plugin lit tes flips exactement comme le jeu.";
}

// ---- Bindings ----------------------------------------------------------------------------------------------
std::vector<BindingRow> bindingRows(const PlayerProfile& p, KeyStyle style) {
    std::vector<BindingRow> rows;
    rows.reserve(static_cast<size_t>(GameAction::Count));
    for (int i = 0; i < static_cast<int>(GameAction::Count); ++i) {
        const GameAction a = static_cast<GameAction>(i);
        BindingRow r;
        r.action = a;
        r.name = actionName(a);
        r.keys = joinLabels(actionKeys(p, a, p.usingGamepad), style);
        r.otherKeys = joinLabels(actionKeys(p, a, !p.usingGamepad), style);
        r.bound = !r.keys.empty();
        rows.push_back(std::move(r));
    }
    return rows;
}

bool deviceBindingsReadable(const PlayerProfile& p) {
    const bool pad = p.usingGamepad;
    return !actionKeys(p, GameAction::Jump, pad).empty() || !actionKeys(p, GameAction::Boost, pad).empty() ||
           !actionKeys(p, GameAction::Throttle, pad).empty();
}

std::vector<AuditItem> auditBindings(const PlayerProfile& p, KeyStyle style) {
    std::vector<AuditItem> out;
    if (p.pcBindings.empty() && p.gamepadBindings.empty()) {
        out.push_back({"pf.bindings_unknown", CheckStatus::Info, "Touches",
                       "Tes touches ne sont pas encore lues. Lance le freeplay ou clique sur Relire ma config."});
        return out;
    }
    if (!deviceBindingsReadable(p)) {
        // The lists only hold keys of the other device (older BakkesMod reads no keyboard binding, some setups
        // report an empty gamepad list): judging them would only produce false advice.
        out.push_back({"pf.device_bindings_unknown", CheckStatus::Info, "Touches",
                       std::string("Les touches de ton appareil actuel (") +
                           (p.usingGamepad ? "manette" : "clavier et souris") +
                           ") ne sont pas lisibles : rien n'est conseillé sur tes touches pour l'instant."});
        return out;
    }
    const bool pad = p.usingGamepad;
    const auto keys = [&](GameAction a) { return actionKeys(p, a, pad); };
    const auto label = [&](const std::vector<std::string>& k) { return joinLabels(k, style); };

    const auto jump = keys(GameAction::Jump), boost = keys(GameAction::Boost), slide = keys(GameAction::Powerslide);
    const auto roll = keys(GameAction::AirRoll), left = keys(GameAction::AirRollLeft), right = keys(GameAction::AirRollRight);
    const auto cam = keys(GameAction::BallCam), reset = keys(GameAction::ResetShot);

    if (jump.empty())
        out.push_back({"pf.no_jump", CheckStatus::Advice, "Saut", "Aucune touche de saut trouvée sur ton appareil actuel."});
    if (boost.empty())
        out.push_back({"pf.no_boost", CheckStatus::Advice, "Boost", "Aucune touche de boost trouvée sur ton appareil actuel."});

    // Directional air roll.
    if (left.empty() && right.empty()) {
        out.push_back({"pf.no_directional_air_roll", CheckStatus::Advice, "Air roll directionnel",
                       "Pas d'air roll gauche ni droit : assigne-en un. Il sert au tornado spin, aux flip resets, aux "
                       "stalls et à mieux contrôler la voiture en l'air."});
    } else if (!left.empty() && !right.empty()) {
        out.push_back({"pf.directional_air_roll", CheckStatus::Ok, "Air roll directionnel",
                       "Air roll gauche (" + label(left) + ") et droit (" + label(right) + ") assignés."});
    } else {
        const bool isLeft = !left.empty();
        out.push_back({"pf.directional_air_roll", CheckStatus::Ok, "Air roll directionnel",
                       std::string("Air roll ") + (isLeft ? "gauche" : "droit") + " sur " + label(isLeft ? left : right) +
                           " : un seul côté suffit, c'est le choix de la plupart des joueurs."});
    }

    // Free air roll vs powerslide.
    if (slide.empty())
        out.push_back({"pf.no_powerslide", CheckStatus::Advice, "Glissade",
                       "Aucune touche de glissade (powerslide) : elle sert à tourner serré et à garder ta vitesse."});
    if (roll.empty()) {
        if (left.empty() && right.empty())
            out.push_back({"pf.no_free_air_roll", CheckStatus::Advice, "Air roll libre",
                           "Aucun air roll assigné : tu ne pourras pas tourner sur toi-même en l'air."});
        else
            out.push_back({"pf.no_free_air_roll", CheckStatus::Info, "Air roll libre",
                           "Pas d'air roll libre : aucun souci, ton air roll directionnel le remplace."});
    } else if (!slide.empty() && sharesKey(roll, slide)) {
        out.push_back({"pf.air_roll_shared", CheckStatus::Advice, "Air roll et glissade",
                       "Air roll libre et glissade sur le même bouton (" + label(roll) +
                           ") : beaucoup de joueurs les séparent pour enchaîner glissade au sol et air roll en l'air "
                           "sans conflit."});
    } else {
        out.push_back({"pf.air_roll_separate", CheckStatus::Ok, "Air roll et glissade",
                       "Air roll libre (" + label(roll) + ") séparé de la glissade" +
                           (slide.empty() ? std::string(".") : " (" + label(slide) + ").")});
    }

    // Ball cam.
    if (cam.empty())
        out.push_back({"pf.no_ball_cam", CheckStatus::Advice, "Caméra balle",
                       "Aucune touche pour la caméra balle : assigne-la, tu en auras besoin pour dribbler et te "
                       "replacer."});
    else
        out.push_back({"pf.ball_cam", CheckStatus::Ok, "Caméra balle", "Caméra balle sur " + label(cam) + "."});

    // Reset for training.
    if (reset.empty())
        out.push_back({"pf.no_reset", CheckStatus::Info, "Reset",
                       "Pas de touche Reset : assigne-la pour relancer un tir ou un exercice d'un seul bouton."});
    else
        out.push_back({"pf.reset", CheckStatus::Ok, "Reset", "Reset sur " + label(reset) + " : pratique pour relancer."});

    // Thumb usage on a gamepad: jump and boost both on face buttons.
    if (pad && !jump.empty() && !boost.empty() && isFaceButton(jump.front()) && isFaceButton(boost.front()) &&
        !sameName(jump.front(), boost.front())) {
        out.push_back({"pf.thumb_jump_boost", CheckStatus::Info, "Pouce droit",
                       "Saut (" + keyLabel(jump.front(), style) + ") et boost (" + keyLabel(boost.front(), style) +
                           ") sont sous ton pouce droit : certains joueurs passent le boost sur une gâchette pour "
                           "sauter et booster en même temps sans effort."});
    }
    return out;
}

std::vector<MechanicReadiness> mechanicsReadiness(const PlayerProfile& p, KeyStyle style) {
    if (!deviceBindingsReadable(p)) return {};  // keys of the device in use unknown: nothing can be judged
    const bool pad = p.usingGamepad;
    const auto keys = [&](GameAction a) { return actionKeys(p, a, pad); };
    const auto left = keys(GameAction::AirRollLeft), right = keys(GameAction::AirRollRight);
    const auto roll = keys(GameAction::AirRoll), cam = keys(GameAction::BallCam);
    const bool jump = !keys(GameAction::Jump).empty(), boost = !keys(GameAction::Boost).empty();
    const bool dir = !left.empty() || !right.empty();
    const std::string dirText =
        dir ? std::string("Air roll ") + (!left.empty() ? "gauche" : "droit") + " (" +
                  joinLabels(!left.empty() ? left : right, style) + ")"
            : std::string();
    const std::string camText = cam.empty() ? std::string() : joinLabels(cam, style);
    // Any air roll (free first, else directional) to roll back onto the wheels after a half-flip.
    const bool anyRoll = dir || !roll.empty();
    const std::string rollText =
        !roll.empty() ? joinLabels(roll, style) : (dir ? joinLabels(!left.empty() ? left : right, style) : std::string());

    std::vector<MechanicReadiness> out;
    out.reserve(9);
    out.push_back({"Tornado spin", dir,
                   dir ? dirText + " : tu as ce qu'il faut." : "Assigne un air roll directionnel (gauche ou droit)."});
    out.push_back({"Stall", dir,
                   dir ? "Air roll directionnel et lacet opposé : c'est jouable." : "Il faut un air roll directionnel."});
    out.push_back({"Flip reset", dir || !roll.empty(),
                   dir ? "L'air roll directionnel aide à tourner tes roues vers la balle."
                       : (!roll.empty() ? "Possible avec l'air roll libre, plus simple en directionnel."
                                        : "Assigne un air roll pour orienter tes roues vers la balle.")});
    out.push_back({"Air dribble et ceiling shot", !cam.empty() && boost,
                   !cam.empty() ? "Caméra balle sur " + camText + " pour la couper au bon moment."
                                : "Assigne la caméra balle pour pouvoir la couper en l'air."});
    out.push_back({"Dribble et flicks", !cam.empty(),
                   !cam.empty() ? "Coupe la caméra balle (" + camText + ") en dribble pour mieux voir la balle sur ton toit."
                                : "Assigne la caméra balle pour pouvoir la couper en dribble."});
    out.push_back({"Speedflip", jump && boost,
                   dir ? "Saut, boost et " + dirText + " : tout est là."
                       : "Saut et boost suffisent ; un air roll directionnel aide à l'annuler proprement."});
    out.push_back({"Wavedash", jump, jump ? "Seulement le saut et le stick." : "Il faut une touche de saut."});
    out.push_back({"Half-flip", jump && anyRoll,
                   !jump ? std::string("Il faut une touche de saut.")
                         : (anyRoll ? "Saut, puis air roll (" + rollText + ") pour te remettre sur les roues."
                                    : std::string("Assigne un air roll (libre ou directionnel) pour finir le "
                                                  "half-flip sur les roues."))});
    out.push_back({"Musty flick", jump, jump ? "Saut et stick vers l'arrière, rien de spécial à assigner."
                                             : "Il faut une touche de saut."});
    return out;
}

const char* deviceLabel(const PlayerProfile& p) { return p.usingGamepad ? "Manette" : "Clavier et souris"; }

std::string detectionSummary(const PlayerProfile& p, const HitboxMatch& m) {
    std::string s = p.usingGamepad ? "manette" : "clavier-souris";
    if (p.carBodyId != 0 || !p.carBodyName.empty()) s += ", " + carLabel(m);
    s += p.camera.ballCam ? ", caméra balle" : ", caméra libre";
    return s;
}

// ---- Signatures --------------------------------------------------------------------------------------------
std::string stableHash(const std::string& text) {
    uint64_t h = 14695981039346656037ULL;
    for (unsigned char c : text) {
        h ^= c;
        h *= 1099511628211ULL;
    }
    char buf[20];
    std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(h));
    return buf;
}

std::string formatValue(float v, int decimals) {
    decimals = std::clamp(decimals, 0, 4);
    if (!std::isfinite(v)) return "?";
    float r = roundTo(v, decimals);
    if (r == 0.f) r = 0.f;  // no "-0"
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.*f", decimals, static_cast<double>(r));
    return buf;
}

ProfileSignature profileSignature(const PlayerProfile& p) {
    ProfileSignature s;
    if (p.carBodyId != 0) s.car = std::to_string(p.carBodyId) + "|" + hitboxFamilyName(classifyHitbox(p).family);
    if (!p.pcBindings.empty() || !p.gamepadBindings.empty()) {
        std::vector<std::string> lines;
        lines.reserve(p.pcBindings.size() + p.gamepadBindings.size());
        for (const auto& b : p.pcBindings) lines.push_back("pc:" + lower(b.action) + "=" + lower(b.key));
        for (const auto& b : p.gamepadBindings) lines.push_back("pad:" + lower(b.action) + "=" + lower(b.key));
        std::sort(lines.begin(), lines.end());
        std::string all;
        for (const auto& l : lines) all += l + "\n";
        s.bindings = stableHash(all);
    }
    s.controller = formatValue(p.dodgeDeadzone, 2) + "|" + formatValue(p.controllerDeadzone, 2) + "|" +
                   formatValue(p.steeringSensitivity, 2) + "|" + formatValue(p.airControlSensitivity, 2);
    const CameraProfile& c = p.camera;
    s.camera = formatValue(c.fov, 0) + "|" + formatValue(c.distance, 0) + "|" + formatValue(c.height, 0) + "|" +
               formatValue(c.pitch, 0) + "|" + formatValue(c.stiffness, 2) + "|" + formatValue(c.swivelSpeed, 1) + "|" +
               formatValue(c.transitionSpeed, 1);
    return s;
}

} // namespace rlt::pf
