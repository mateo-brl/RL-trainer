#include "core/modules/ballmech/Tips.h"

#include <algorithm>
#include <cctype>

#include "core/common/Controls.h"

namespace rlt::bm {

namespace {
struct TipDef {
    const char* code;
    const char* text;
    const char* hud;  // one short line for the HUD, no button names
    float weight;
};

const TipDef kTips[] = {
    // ---- Flip reset
    {"FR_NO_RESET", "Présente tout le dessous de la voiture face à la balle pour poser les 4 roues en même temps.",
     "Pose les 4 roues sur la balle en même temps", 35.f},
    {"FR_POOR_ALIGNMENT", "Aligne le dessous de la voiture sur le centre de la balle avec {airroll}.",
     "Mets bien le dessous de la voiture face à la balle", 20.f},
    {"FR_TOO_FAST_CONTACT", "Arrive plus doucement : lâche le boost juste avant le contact.",
     "Arrive plus doucement : lâche le boost avant", 15.f},
    {"FR_RESET_WASTED", "Ton reset n'expire pas : garde-le et flippe dans la balle avant de retomber.",
     "Reset obtenu : flippe dans la balle avant de retomber", 30.f},
    {"FR_FLIP_MISSED_BALL", "Attends d'être à une longueur de voiture de la balle avant de flipper.",
     "Flippe plus près de la balle", 30.f},
    {"FR_WEAK_SHOT", "Frappe plus fort : flippe avec le nez dans la balle en avançant vers elle.",
     "Flippe dans la balle en avançant vers elle", 15.f},
    {"FR_OFF_TARGET", "Oriente le nez de la voiture vers le but avant de flipper.",
     "Tourne le nez vers le but avant de flipper", 15.f},
    {"FR_STEP_TAKEOFF", "Saute avec {jump}, lève le nez et boost tout de suite vers la balle.",
     "Saute et boost vers la balle plus tôt", 40.f},
    {"FR_STEP_UNDER", "Vise un point juste sous la balle, pas la balle elle-même : les roues sont sous la voiture.",
     "Vise juste sous la balle", 40.f},
    // ---- Ceiling shot
    {"CEIL_JUMPED_OFF", "Ne saute pas en quittant le plafond : laisse-toi tomber pour garder ton flip.",
     "Ne saute pas en quittant le plafond", 35.f},
    {"CEIL_NO_FLIP_SHOT", "Frappe avec ton flip : c'est tout l'intérêt du ceiling shot.",
     "Frappe la balle avec ton flip", 25.f},
    {"CEIL_FLIP_WASTED", "Flip trop tôt : attends d'être à une longueur de voiture de la balle.",
     "Tu flippes trop tôt : attends d'être près de la balle", 25.f},
    {"CEIL_MISSED", "Quitte le plafond plus tôt ou plus tard pour tomber juste derrière la balle.",
     "Quitte le plafond pour tomber juste derrière la balle", 25.f},
    {"CEIL_STEP_WALL", "Roule vers le mur de côté, un peu en biais vers le but adverse, et boost avec {boost}.",
     "Va au mur de côté en boostant", 40.f},
    {"CEIL_STEP_CEILING", "Monte presque tout droit sur le mur jusqu'au plafond, en gardant l'accélérateur.",
     "Monte tout droit jusqu'au plafond", 40.f},
    {"CEIL_STEP_FELL_WALL", "Tombé du mur : pas assez de vitesse. Boost jusqu'au milieu du mur et monte plus droit.",
     "Pas assez de vitesse : boost jusqu'au milieu du mur", 40.f},
    {"CEIL_TOO_FAST", "Trop vite au plafond : lâche le boost plus tôt dans le mur, sinon tu passes au-dessus de la balle.",
     "Lâche le boost plus tôt dans le mur", 20.f},
    {"CEIL_STEP_DROP", "Au plafond, lâche l'accélérateur : la voiture ne tient pas et tombe toute seule.",
     "Lâche l'accélérateur au plafond pour tomber", 40.f},
    // ---- Flicks
    {"FL_JUMP_UNSETTLED", "Stabilise la balle sur le toit (environ 0,3 s) avant de sauter.",
     "Attends que la balle soit stable sur le toit", 15.f},
    {"FL_LOST_BALL", "Saute avec la balle bien centrée, légèrement vers l'avant du toit.",
     "Saute avec la balle bien au centre du toit", 30.f},
    {"FL_FLIP_TOO_EARLY", "Flip trop tôt : laisse la voiture finir de tourner avant de flipper.",
     "Tu flippes trop tôt : laisse la voiture tourner", 15.f},
    {"FL_FLIP_TOO_LATE", "Flip trop tard : flippe dès que le nez est en position.",
     "Tu flippes trop tard : flippe dès que le nez est placé", 15.f},
    {"FL_UNDER_ROTATED", "Tourne un peu plus avant le flip diagonal : vise environ 45°.",
     "Tourne un peu plus avant de flipper", 20.f},
    {"FL_OVER_ROTATED", "Trop tourné : au-delà de 65° ton 45 devient un 90, relâche plus tôt.",
     "Tu tournes trop : relâche le stick plus tôt", 20.f},
    {"FL_MUSTY_NOSE_NOT_DOWN", "Bascule d'abord le nez vers le bas (stick avant), puis fais le flip arrière.",
     "Baisse d'abord le nez, puis flip arrière", 30.f},
    {"FL_BREEZI_NO_SPIN", "Fais une vraie tornade avec {airroll} avant le flip arrière.",
     "Fais une vraie tornade avant le flip arrière", 30.f},
    {"FL_SUPERSONIC_START", "Pars moins vite : en supersonique la balle ne tient pas sur le toit.",
     "Pars moins vite : la balle tombe du toit", 15.f},
    {"FL_NO_CONTACT", "Flippe pendant que la balle est encore collée à la voiture.",
     "Flippe tant que la balle touche encore le toit", 30.f},
    {"FL_WEAK", "Pousse le stick à fond dans la direction du flip et frappe avec l'avant.",
     "Pousse le stick à fond quand tu flippes", 20.f},
    {"FL_OFF_TARGET", "Oriente la voiture vers le but avant de sauter.",
     "Tourne la voiture vers le but avant de sauter", 15.f},
    {"FL_STEP_JUMP", "Saute avec {jump} quand la balle ne bouge plus au centre du toit.",
     "Saute quand la balle est stable sur le toit", 40.f},
    {"BZ_STEP_HOLD", "Garde {jump} enfoncé au saut : tu montes plus haut et tu as le temps de tourner.",
     "Garde le saut enfoncé", 40.f},
    // ---- Double tap
    {"DT_LANDED", "Garde de l'altitude en suivant la balle : dose {boost}.",
     "Dose le boost pour rester en l'air", 30.f},
    {"DT_LATE_FOLLOW", "Suis la balle de plus près : une longueur de voiture derrière elle au rebond.",
     "Suis la balle de plus près", 20.f},
    {"DT_LOW_BACKBOARD", "Vise plus haut sur le mur du fond pour avoir un rebond lisible.",
     "Vise plus haut sur le mur du fond", 15.f},
    {"DT_NO_SECOND_TOUCH", "Place-toi sur la trajectoire de retour avant que la balle touche le mur.",
     "Place-toi sur le retour de la balle avant le rebond", 30.f},
    {"DT_OFF_TARGET", "Frappe le rebond par l'arrière et un peu par le haut, vers le but.",
     "Frappe le rebond par l'arrière, vers le but", 20.f},
    // ---- Air dribble
    {"AD_SHORT", "Monte avec la balle en douceur avant d'accélérer.",
     "Monte avec la balle en douceur avant d'accélérer", 30.f},
    {"AD_BALL_TOO_FAR", "Touches plus douces : tapote la balle avec le nez et dose le boost.",
     "Touches plus douces, dose le boost", 20.f},
    {"AD_UNDER_BALL", "Garde le nez pointé vers la balle, pas vers le ciel.",
     "Garde le nez pointé vers la balle", 20.f},
    {"AD_BOOST_OUT", "Pars avec plus de boost ou dose-le pour tenir plus longtemps.",
     "Économise le boost pour tenir plus longtemps", 15.f},
    {"AD_NO_FINISH", "Termine l'action : pousse la balle ou flippe dedans vers le but.",
     "Termine : pousse la balle vers le but", 15.f},
    {"AD_NO_CATCH", "Roule doucement vers la balle, nez bien droit : elle monte sur le capot puis sur le toit.",
     "Ramasse la balle doucement, nez bien droit", 40.f},
    // ---- Shared pickup ("Ramassage") of the full sequences
    {"PU_NO_ROOF", "Roule doucement vers la balle, nez bien droit : elle monte sur le capot puis sur le toit.",
     "Ramasse la balle doucement, nez bien droit", 40.f},
    {"PU_UNSTABLE", "Garde la balle au centre du toit : si elle glisse vers l'arrière, lâche un peu l'accélérateur ; "
     "si elle glisse vers l'avant, accélère un peu.",
     "Garde la balle au centre du toit", 40.f},
    {"PU_DROPPED", "La balle est tombée du toit : roule plus doucement pour la garder.",
     "Roule plus doucement pour garder la balle", 40.f},
    {"AD_LOW_POP", "Saute plus franchement en levant un peu le nez : la balle doit monter au-dessus de ta voiture.",
     "Saute plus franchement avec la balle", 35.f},
    {"AD_STEP_POP", "Saute avec {jump} quand la balle est stable au centre du toit.",
     "Saute quand la balle est stable sur le toit", 40.f},
    {"AD_STEP_TOUCH", "Après le saut, pointe le nez vers la balle et boost par petites touches pour la toucher.",
     "Nez vers la balle, boost par petites touches", 40.f},
    // ---- Aerial / air roll
    {"AE_OFF_TARGET", "Place-toi dans l'axe balle-but avant de décoller.",
     "Place-toi dans l'axe balle-but avant de décoller", 25.f},
    {"AE_HIT_SIDE", "Frappe la balle du côté opposé au but : ton point d'impact décide de la direction.",
     "Frappe la balle du côté opposé au but", 15.f},
    {"AE_OFF_CENTER", "Vise le centre de la balle avec le nez de la voiture.",
     "Vise le centre de la balle avec le nez", 20.f},
    {"AE_WEAK", "Garde le boost jusqu'au contact pour frapper plus fort.",
     "Garde le boost jusqu'au contact", 15.f},
    {"AE_ROLL_MIXED", "Garde un seul sens d'air roll pour un contrôle plus précis.",
     "Garde un seul sens d'air roll", 10.f},
    {"AE_NO_AIR_ROLL", "Utilise {airroll} pour orienter le nez vers la balle avant le contact.",
     "Utilise l'air roll pour viser la balle", 25.f},
    {"AE_STEP_TAKEOFF", "Saute avec {jump}, lève le nez vers la balle (stick vers toi) et boost tout de suite.",
     "Saute, lève le nez et boost", 40.f},
    {"AE_STEP_AXIS", "Avant de sauter, place-toi pour que la balle soit entre toi et le but.",
     "Place-toi dans l'axe avant de sauter", 40.f},
    // ---- Pinch
    {"PI_NOT_PINCHED", "Frappe la balle quand elle est collée à la surface, un peu à côté du centre.",
     "Frappe la balle quand elle touche la surface", 30.f},
    {"PI_TIMING", "Mauvais timing : la balle n'était pas encore contre la surface.",
     "Frappe un peu plus tard, balle contre la surface", 25.f},
    {"PI_OFF_TARGET", "Change ton angle d'approche : la balle part à l'opposé du point d'impact.",
     "Change d'angle : la balle part à l'opposé du choc", 15.f},
    {"PI_STEP_SPEED", "Prends de la vitesse : boost en ligne droite vers l'endroit où la balle va toucher le mur.",
     "Prends de la vitesse vers le mur", 40.f},
    // ---- Power shot
    {"PS_WEAK", "Arrive plus vite et flippe dans la balle environ 0,1 s avant le contact.",
     "Arrive plus vite et flippe juste avant la balle", 25.f},
    {"PS_FLIP_EARLY", "Flip trop tôt : le nez doit toucher la balle pendant la rotation.",
     "Tu flippes trop tôt : flippe juste avant la balle", 20.f},
    {"PS_SIDE_HIT", "Frappe avec l'avant de la voiture, pas avec le côté.",
     "Frappe avec l'avant de la voiture", 15.f},
    {"PS_SLIDE_HELD", "Relâche {slide} avant l'impact : la glissade sert juste à pivoter.",
     "Relâche la glissade avant de toucher la balle", 15.f},
    {"PS_OFF_TARGET", "Aligne voiture, balle et but avant l'impact.",
     "Aligne voiture, balle et but avant de frapper", 20.f},
    {"PS_STEP_ALIGN", "Avant d'arriver sur la balle, place-toi pour qu'elle soit entre toi et le but.",
     "Mets la balle entre toi et le but", 40.f},
    {"PS_NO_FLIP", "Flippe juste avant de toucher la balle, à environ une longueur de voiture.",
     "Flippe juste avant la balle", 30.f},
    // ---- Drills
    {"DRILL_TIMEOUT", "Temps écoulé : lance-toi plus tôt.",
     "Temps écoulé : lance-toi plus tôt", 30.f},
    {"DRILL_LANDED", "Tu es retombé avant de réussir : reste en l'air plus longtemps.",
     "Tu es retombé trop tôt : reste en l'air", 30.f},
    {"DRILL_BALL_LOST", "La balle t'a échappé : garde-la près de la voiture.",
     "Garde la balle près de la voiture", 30.f},
    {"DRILL_WRONG_MECH", "Ce n'était pas la méca demandée : relis la consigne de l'exercice.",
     "Ce n'était pas la méca demandée", 40.f},
    {"DRILL_NEED_POWER", "Bonne base ! Pour valider ce niveau, frappe plus fort.",
     "Presque : frappe plus fort pour ce niveau", 18.f},
    {"DRILL_NEED_TARGET", "Bonne base ! Pour valider ce niveau, cadre ton tir.",
     "Presque : cadre ton tir pour ce niveau", 18.f},
    {"DRILL_NEED_DURATION", "Bonne base ! Pour valider ce niveau, garde la balle en l'air plus longtemps.",
     "Presque : garde la balle en l'air plus longtemps", 18.f},
    {"DRILL_NEED_CONVERT", "Reset obtenu ! Pour valider ce niveau, flippe dans la balle avec {jump}.",
     "Reset obtenu : flippe maintenant dans la balle", 18.f},
    {"DRILL_NEED_SCORE", "Presque ! Soigne l'exécution pour dépasser le score demandé.",
     "Presque : soigne l'exécution", 12.f},
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

std::string hudTipText(const Tip& tip) {
    for (const auto& t : kTips)
        if (tip.code == tipCode(t.code)) return t.hud;
    return tip.text;
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
