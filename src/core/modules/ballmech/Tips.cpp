#include "core/modules/ballmech/Tips.h"

#include <algorithm>
#include <cctype>

#include "core/common/Controls.h"

namespace rlt::bm {

namespace {
struct TipDef {
    const char* code;
    const char* text;
    float weight;
};

const TipDef kTips[] = {
    // ---- Flip reset
    {"FR_NO_RESET", "Présente tout le dessous de la voiture face à la balle pour poser les 4 roues en même temps.", 35.f},
    {"FR_POOR_ALIGNMENT", "Aligne le dessous de la voiture sur le centre de la balle avec {airroll}.", 20.f},
    {"FR_TOO_FAST_CONTACT", "Arrive plus doucement : lâche le boost juste avant le contact.", 15.f},
    {"FR_RESET_WASTED", "Ton reset n'expire pas : garde-le et flippe dans la balle avant de retomber.", 30.f},
    {"FR_FLIP_MISSED_BALL", "Attends d'être à une longueur de voiture de la balle avant de flipper.", 30.f},
    {"FR_WEAK_SHOT", "Frappe plus fort : flippe avec le nez dans la balle en avançant vers elle.", 15.f},
    {"FR_OFF_TARGET", "Oriente le nez de la voiture vers le but avant de flipper.", 15.f},
    // ---- Ceiling shot
    {"CEIL_JUMPED_OFF", "Ne saute pas en quittant le plafond : laisse-toi tomber pour garder ton flip.", 35.f},
    {"CEIL_NO_FLIP_SHOT", "Frappe avec ton flip : c'est tout l'intérêt du ceiling shot.", 25.f},
    {"CEIL_FLIP_WASTED", "Flip trop tôt : attends d'être à une longueur de voiture de la balle.", 25.f},
    {"CEIL_MISSED", "Quitte le plafond plus tôt ou plus tard pour tomber juste derrière la balle.", 25.f},
    // ---- Flicks
    {"FL_JUMP_UNSETTLED", "Stabilise la balle sur le toit (environ 0,3 s) avant de sauter.", 15.f},
    {"FL_LOST_BALL", "Saute avec la balle bien centrée, légèrement vers l'avant du toit.", 30.f},
    {"FL_FLIP_TOO_EARLY", "Flip trop tôt : laisse la voiture finir de tourner avant de flipper.", 15.f},
    {"FL_FLIP_TOO_LATE", "Flip trop tard : flippe dès que le nez est en position.", 15.f},
    {"FL_UNDER_ROTATED", "Tourne un peu plus avant le flip diagonal : vise environ 45°.", 20.f},
    {"FL_OVER_ROTATED", "Trop tourné : au-delà de 65° ton 45 devient un 90, relâche plus tôt.", 20.f},
    {"FL_MUSTY_NOSE_NOT_DOWN", "Bascule d'abord le nez vers le bas (stick avant), puis fais le flip arrière.", 30.f},
    {"FL_BREEZI_NO_SPIN", "Fais une vraie tornade avec {airroll} avant le flip arrière.", 30.f},
    {"FL_SUPERSONIC_START", "Pars moins vite : en supersonique la balle ne tient pas sur le toit.", 15.f},
    {"FL_NO_CONTACT", "Flippe pendant que la balle est encore collée à la voiture.", 30.f},
    {"FL_WEAK", "Pousse le stick à fond dans la direction du flip et frappe avec l'avant.", 20.f},
    {"FL_OFF_TARGET", "Oriente la voiture vers le but avant de sauter.", 15.f},
    // ---- Double tap
    {"DT_LANDED", "Garde de l'altitude en suivant la balle : dose {boost}.", 30.f},
    {"DT_LATE_FOLLOW", "Suis la balle de plus près : une longueur de voiture derrière elle au rebond.", 20.f},
    {"DT_LOW_BACKBOARD", "Vise plus haut sur le mur du fond pour avoir un rebond lisible.", 15.f},
    {"DT_NO_SECOND_TOUCH", "Place-toi sur la trajectoire de retour avant que la balle touche le mur.", 30.f},
    {"DT_OFF_TARGET", "Frappe le rebond par l'arrière et un peu par le haut, vers le but.", 20.f},
    // ---- Air dribble
    {"AD_SHORT", "Monte avec la balle en douceur avant d'accélérer.", 30.f},
    {"AD_BALL_TOO_FAR", "Touches plus douces : tapote la balle avec le nez et dose le boost.", 20.f},
    {"AD_UNDER_BALL", "Garde le nez pointé vers la balle, pas vers le ciel.", 20.f},
    {"AD_BOOST_OUT", "Pars avec plus de boost ou dose-le pour tenir plus longtemps.", 15.f},
    {"AD_NO_FINISH", "Termine l'action : pousse la balle ou flippe dedans vers le but.", 15.f},
    // ---- Aerial / air roll
    {"AE_OFF_TARGET", "Place-toi dans l'axe balle-but avant de décoller.", 25.f},
    {"AE_HIT_SIDE", "Frappe la balle du côté opposé au but : ton point d'impact décide de la direction.", 15.f},
    {"AE_OFF_CENTER", "Vise le centre de la balle avec le nez de la voiture.", 20.f},
    {"AE_WEAK", "Garde le boost jusqu'au contact pour frapper plus fort.", 15.f},
    {"AE_ROLL_MIXED", "Garde un seul sens d'air roll pour un contrôle plus précis.", 10.f},
    {"AE_NO_AIR_ROLL", "Utilise {airroll} pour orienter le nez vers la balle avant le contact.", 25.f},
    // ---- Pinch
    {"PI_NOT_PINCHED", "Frappe la balle quand elle est collée à la surface, un peu à côté du centre.", 30.f},
    {"PI_TIMING", "Mauvais timing : la balle n'était pas encore contre la surface.", 25.f},
    {"PI_OFF_TARGET", "Change ton angle d'approche : la balle part à l'opposé du point d'impact.", 15.f},
    // ---- Power shot
    {"PS_WEAK", "Arrive plus vite et flippe dans la balle environ 0,1 s avant le contact.", 25.f},
    {"PS_FLIP_EARLY", "Flip trop tôt : le nez doit toucher la balle pendant la rotation.", 20.f},
    {"PS_SIDE_HIT", "Frappe avec l'avant de la voiture, pas avec le côté.", 15.f},
    {"PS_SLIDE_HELD", "Relâche {slide} avant l'impact : la glissade sert juste à pivoter.", 15.f},
    {"PS_OFF_TARGET", "Aligne voiture, balle et but avant l'impact.", 20.f},
    // ---- Drills
    {"DRILL_TIMEOUT", "Temps écoulé : lance-toi plus tôt.", 30.f},
    {"DRILL_LANDED", "Tu es retombé avant de réussir : reste en l'air plus longtemps.", 30.f},
    {"DRILL_BALL_LOST", "La balle t'a échappé : garde-la près de la voiture.", 30.f},
    {"DRILL_WRONG_MECH", "Ce n'était pas la méca demandée : relis la consigne de l'exercice.", 40.f},
    {"DRILL_NEED_POWER", "Bonne base ! Pour valider ce niveau, frappe plus fort.", 18.f},
    {"DRILL_NEED_TARGET", "Bonne base ! Pour valider ce niveau, cadre ton tir.", 18.f},
    {"DRILL_NEED_DURATION", "Bonne base ! Pour valider ce niveau, garde la balle en l'air plus longtemps.", 18.f},
    {"DRILL_NEED_CONVERT", "Reset obtenu ! Pour valider ce niveau, flippe dans la balle avec {jump}.", 18.f},
    {"DRILL_NEED_SCORE", "Presque ! Soigne l'exécution pour dépasser le score demandé.", 12.f},
};

std::string replaceAll(std::string s, const std::string& from, const std::string& to) {
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
    return s;
}

std::string withKey(const char* noun, const std::string& key) { return key.empty() ? noun : std::string(noun) + " (" + key + ")"; }
} // namespace

const char* tipTemplate(const std::string& error) {
    for (const auto& t : kTips)
        if (error == t.code) return t.text;
    return nullptr;
}

float tipWeight(const std::string& error) {
    for (const auto& t : kTips)
        if (error == t.code) return t.weight;
    return 10.f;
}

std::string tipCode(const std::string& error) {
    std::string c = "bm.";
    for (char ch : error) c.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    return c;
}

std::string airRollPrompt(const PlayerProfile& p, KeyStyle style) {
    const std::string left = actionKeyLabel(p, GameAction::AirRollLeft, style);
    const std::string right = actionKeyLabel(p, GameAction::AirRollRight, style);
    if (!left.empty() && !right.empty()) return "l'air roll gauche (" + left + ") ou droit (" + right + ")";
    if (!left.empty()) return "l'air roll gauche (" + left + ")";
    if (!right.empty()) return "l'air roll droit (" + right + ")";
    const std::string free = actionKeyLabel(p, GameAction::AirRoll, style);
    if (!free.empty()) return "l'air roll (" + free + ")";
    return withKey("l'air roll", actionKeyLabel(p, GameAction::Powerslide, style));
}

std::string fillButtons(const std::string& text, const PlayerProfile& p, KeyStyle style) {
    if (text.find('{') == std::string::npos) return text;
    std::string s = text;
    s = replaceAll(s, "{jump}", withKey("le saut", actionKeyLabel(p, GameAction::Jump, style)));
    s = replaceAll(s, "{boost}", withKey("le boost", actionKeyLabel(p, GameAction::Boost, style)));
    s = replaceAll(s, "{slide}", withKey("la glissade", actionKeyLabel(p, GameAction::Powerslide, style)));
    s = replaceAll(s, "{airroll}", airRollPrompt(p, style));
    return s;
}

std::vector<Tip> buildTips(const std::vector<std::string>& errors, const PlayerProfile& profile, KeyStyle style) {
    std::vector<Tip> tips;
    for (const auto& e : errors) {
        const std::string code = tipCode(e);
        if (std::any_of(tips.begin(), tips.end(), [&](const Tip& t) { return t.code == code; })) continue;
        const char* tmpl = tipTemplate(e);
        if (!tmpl) continue;
        Tip t;
        t.code = code;
        t.text = fillButtons(tmpl, profile, style);
        t.weight = tipWeight(e);
        tips.push_back(std::move(t));
    }
    std::stable_sort(tips.begin(), tips.end(), [](const Tip& a, const Tip& b) { return a.weight > b.weight; });
    return tips;
}

} // namespace rlt::bm
