#include "core/modules/mechanics/Tips.h"

#include <algorithm>
#include <cmath>
#include <cstdio>


namespace rlt::mech {

namespace {

std::string num2(float v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.2f", static_cast<double>(v));
    std::string s = buf;
    for (char& c : s)
        if (c == '.') c = ',';  // French decimal separator
    return s;
}

std::string num0(float v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.0f", static_cast<double>(v));
    return buf;
}

// Stick direction of a flip angle as clock positions: 30 deg -> "11 h ou 1 h", 45 deg -> "10 h 30 ou 1 h 30".
std::string clockPositions(float angleDeg) {
    const int minutes = static_cast<int>(std::lround(std::max(0.f, std::min(angleDeg, 90.f)) * 2.f));
    auto fmt = [](int m) {
        const int h = ((m / 60) + 11) % 12 + 1, mm = m % 60;
        return std::to_string(h) + " h" + (mm ? " " + std::to_string(mm) : std::string());
    };
    return fmt(12 * 60 - minutes) + " ou " + fmt(12 * 60 + minutes);
}

int sideOfVariant(const std::string& v) {
    if (v.find("left") != std::string::npos) return -1;
    if (v.find("right") != std::string::npos) return 1;
    return 0;
}

struct Plain {
    const char* code;
    const char* fr;
};

// Lines that do not depend on the player's bindings.
const Plain kPlain[] = {
    {"WD_CRASH_LANDING", "Garde les roues vers le sol avant d'appuyer sur saut : tu as atterri sur le toit ou le côté."},
    {"WD_NOSE_DOWN", "Lève un peu plus le nez avant le dash, ou flippe un poil plus tard : tu bascules vers l'avant."},
    {"WD_NOSE_UP", "Flippe un poil plus tôt ou cabre moins : tu atterris le nez en l'air."},
    {"WD_ROLL_TILT", "Remets la voiture à plat avec l'air roll avant le dash : tu atterris penché."},
    {"WD_LOW_GAIN", "Flippe dans le sens de ton déplacement, juste avant le contact : le dash n'a presque rien rapporté."},
    {"WD_LATE_JUMPED", "Appuie un poil plus tôt : tes roues étaient déjà au sol, tu as refait un saut au lieu d'un dash."},
    {"WALL_HOLD_LONG", "Tape le premier saut au lieu de le garder, sinon tu t'éloignes du mur."},
    {"CHAIN_GROUND_TIME", "Relance le saut dès que les roues touchent : chaque frame au sol te coûte de la vitesse."},
    {"CHAIN_BROKEN", "Vise la régularité avant la vitesse : un dash raté casse l'élan de la chaîne."},
    {"CHAIN_NOT_SUPERSONIC", "Enchaîne au moins 3 wavedashes propres sans pause pour atteindre le supersonique."},
    {"SF_NO_CANCEL", "Annule le flip : tire le stick à fond vers toi juste après le flip."},
    {"SF_LATE_CANCEL", "Annule plus vite : tire le stick vers toi dès le flip."},
    {"SF_JUMP_LONG", "Tu flippes trop tard : flippe juste après le saut."},
    {"SF_HEADING_OFF", "Corrige ton orientation avec l'air roll et le yaw avant de toucher le sol : tu retombes de travers."},
    {"SF_DRIFT", "Braque légèrement à l'opposé avant le saut : ta trajectoire dérive sur le côté."},
    {"KO_STARTED_IN_AIR", "Attends que la voiture soit posée avant de démarrer."},
    {"KO_FIRST_JUMP_EARLY", "Saute un peu plus tard : laisse d'abord la voiture prendre de la vitesse."},
    {"KO_FIRST_JUMP_LATE", "Saute plus tôt après le GO : le flip te lancera plus vite vers la balle."},
    {"HF_NO_CANCEL", "Pousse le stick à fond vers l'avant pendant le flip arrière pour l'annuler."},
    {"HF_EARLY_CANCEL", "Laisse le nez monter jusqu'à la verticale avant de pousser le stick."},
    {"HF_LATE_CANCEL", "Pousse le stick vers l'avant dès que la voiture est à la verticale."},
    {"HF_WEAK_CANCEL", "Pousse le stick À FOND vers l'avant : une annulation partielle laisse le flip continuer."},
    {"HF_NOT_STRAIGHT", "Tire le stick bien droit vers le bas pour le flip arrière, sans diagonale."},
    {"HF_EARLY_ROLL", "Attends que la voiture soit à l'envers avant l'air roll, sinon tu repars de travers."},
    {"HF_LATE_ROLL", "Enchaîne l'air roll dès que la voiture est à l'envers, sans attendre."},
    {"HF_HEADING_OFF", "Arrête l'air roll quand le capot pointe vers ta cible pour repartir dans l'axe."},
    {"HF_BAD_LANDING", "Finis ta rotation avant de toucher le sol."},
    {"HF_HIGH_JUMP", "Fais le flip arrière juste après le premier saut, sans monter haut."},
    {"FC_NO_CANCEL", "Tire le stick à fond dans le sens opposé au flip juste après l'avoir lancé."},
    {"FC_LATE", "Annule plus tôt : stick opposé dès que le flip part."},
    {"FC_WEAK", "Pousse le stick À FOND à l'opposé : une annulation partielle laisse la voiture tourner."},
    {"FC_BAD_LANDING", "Prépare ta réception après l'annulation : roues vers le sol."},
    {"FA_NO_DOUBLE_JUMP", "Fais le double saut : c'est lui qui donne la vitesse verticale du fast aerial."},
    {"FA_HOLD_SHORT", "Garde le premier saut appuyé un peu plus longtemps pour profiter de toute la poussée."},
    {"FA_HOLD_LONG", "Relâche le premier saut plus tôt : le garder longtemps ne sert à rien."},
    {"FA_NO_TILT", "Tire le stick vers l'arrière pendant le premier saut pour cabrer la voiture."},
    {"DJA_BOOST_LATE", "Commence à booster juste après le double saut."},
    {"ST_PITCH_LEAK", "Garde le stick parfaitement horizontal (ni haut ni bas) au moment du stall."},
    {"ST_NO_FLIP", "Fais le stall peu après le saut : ton flip doit encore être disponible."},
    {"ST_SHORT_HOVER", "Déclenche le stall quand tu commences à redescendre pour flotter plus longtemps."},
    {"AS_UNSTABLE_AXIS", "Garde le yaw et l'air roll constants pendant toute la vrille."},
    {"AS_SLOW_SPIN", "Mets le stick et l'air roll à fond pour tourner plus vite."},
    {"RC_TILTED", "Prépare ton atterrissage plus tôt : roues parallèles au sol avant l'impact."},
    {"RC_MISALIGNED", "Aligne le nez avec ta direction de déplacement avant de toucher le sol."},
    {"RC_SPEED_LOST", "Atterris dans le sens du mouvement (ou fais un wavedash) pour garder ta vitesse."},
    {"RC_SLOW_SETTLE", "Relâche les rotations juste avant d'atterrir pour ne pas rebondir."},
    {"RC_MISSED_WAVEDASH", "Fais un wavedash à la réception quand tu as encore ton flip : tu gagnes beaucoup de vitesse."},
    {"RC_WEAK_WAVEDASH", "Soigne le wavedash de réception : flippe quand les roues arrière touchent, nez un peu cabré."},
    {"PS_SPEED_LOSS", "Raccourcis le powerslide : quelques frames suffisent pour pivoter sans perdre ta vitesse."},
    {"SM_LONG_HOLDS", "Fais des petits taps de boost (feathering) au lieu de le garder appuyé."},
    {"COMBO_SLOW_LINK", "Enchaîne la deuxième méca plus vite après la réception."},
    {"WALL_LOW_GAIN", "Flippe en diagonale vers l'avant et vers le mur : ton wall dash n'a presque pas accéléré."},
    {"ZAP_NO_BOUNCE", "Laisse d'abord la voiture toucher le sol nez baissé : le zap dash part du rebond, sans saut."},
    {"RC_CRASHED", "Retourne-toi plus tôt avec l'air roll : tu as fini sur le toit ou sur le côté."},
    {"WCHAIN_SLOW", "Relance le wall dash dès que les roues retouchent le mur, sans pause."},
    {"DRILL_NO_TOUCH", "Va toucher la balle : l'exercice compte le temps jusqu'au contact."},
    {"DRILL_CHAIN_SHORT", "Enchaîne plus de wavedashes à la suite, sans pause entre deux."},
    {"DRILL_SLOW_TO_BALL", "Va plus vite à la balle : boost dès le premier saut et double saut tout de suite."},
    {"AS_SHORT", "Tiens la rotation plus longtemps : garde le stick et l'air roll à fond."},
    {"SM_NOT_SUPERSONIC", "Reste supersonique : un petit coup de boost dès que tu ralentis."},
    {"SM_TOO_MUCH_BOOST", "Dépense moins de boost : petits coups seulement, et lâche-le à vitesse max."},
    {"SF_SLOW_TO_BALL", "Tu es arrivé trop tard : vise la balle avant de sauter et garde le boost jusqu'à elle."},
    {"SF_NOT_AIMED", "Avant de sauter, tourne un peu vers la balle : ton capot doit pointer sur elle."},
    {"HF_NO_REVERSE", "Recule d'abord vers la balle, accélérateur en arrière, avant de sauter."},
    {"DRILL_NO_SPEED", "Prends d'abord de la vitesse : roule tout droit, accélérateur appuyé."},
    {"HF_SLOW_TO_BALL", "Enchaîne plus vite : air roll dès que la voiture est à l'envers, puis fonce vers la balle."},
    {"RC_SHORT_AIR", "Saute franchement du mur : reste au moins une demi-seconde en l'air avant de retomber."},
    {"FC_JUMP_LONG", "Tu flippes trop tard : flippe juste après le saut."},
    {"FC_WRONG_FLIP", "Fais un flip vers l'avant (ou l'arrière), stick bien droit : un flip de côté ne s'annule pas."},
    {"WD_NOT_FORWARD", "Flippe vers l'avant : stick poussé vers l'avant au moment du flip."},
    {"ZAP_NOSE", "Baisse un peu le nez avant de toucher le sol : stick un poil vers l'avant."},
    {"ZAP_LATE", "Flippe plus tôt : dès que l'avant touche le sol, pas quand la voiture est posée."},
    {"CHAIN_SLOW", "Enchaîne les wavedashes sans pause : chaque dash ajoute de la vitesse."},
    {"WALL_NO_CLIMB", "Roule vers le mur et monte dessus sans sauter, en gardant l'accélérateur."},
    {"WALL_NOT_RIDING", "Longe le mur sans monter ni redescendre vite."},
    {"FA_TOUCH_LOW", "Décolle plus tôt : tu as touché la balle trop bas."},
    {"FA_NOT_PLACED", "Place-toi sous la trajectoire de la balle avant de décoller."},
    {"AS_NO_TAKEOFF", "Décolle d'abord : saut puis double saut, ou boost, avant de tourner."},
    {"RC_NO_CLIMB", "Roule vers le mur et monte dessus avant de sauter."},
    {"RC_NO_WALL_JUMP", "Saute depuis le mur, pas depuis le sol."},
    {"PS_NOT_AIMED", "Relâche le powerslide quand ton nez vise la balle : tu es ressorti à côté."},
    {"SM_SLOW_TO_SUPERSONIC", "Boost tout de suite et garde-le jusqu'à la traînée supersonique."},
    {"SM_EARLY_BOOST", "Accélérateur seul au début : garde le boost pour la fin de l'accélération."},
};

// Lines with the player's buttons: {jump}, {boost}, {slide}, {throttle}, {airroll} (fillButtons).
const Plain kButtons[] = {
    {"SF_NO_BOOST", "Garde {boost} et l'accélérateur appuyés dès le départ."},
    {"SF_NO_JUMP", "Saute avec un appui court sur {jump} : le speedflip part d'un petit saut."},
    {"SF_NO_FLIP", "Fais le flip juste après le saut : un deuxième appui sur {jump} avec le stick penché en diagonale vers l'avant."},
    {"SF_SIDE_FLIP", "Penche le stick en diagonale vers l'avant au deuxième appui sur {jump}, pas sur le côté : tu as fait un flip de côté."},
    {"SF_BACK_FLIP", "Pousse le stick en diagonale vers l'avant au deuxième appui sur {jump} : tu as fait un flip arrière."},
    {"SF_JUMP_HELD", "Saute avec un appui court : relâche {jump} tout de suite."},
    {"HF_NO_FLIP", "Saute, puis flip arrière : stick tiré vers toi et deuxième appui sur {jump}."},
    {"HF_WRONG_FLIP", "Fais un flip arrière : tire le stick vers toi au deuxième appui sur {jump}."},
    {"FC_NO_FLIP", "Saute, puis flip avant : stick vers l'avant et deuxième appui sur {jump}."},
    {"WD_NO_FLIP", "Juste avant que les roues touchent le sol, flip avant : stick vers l'avant et {jump}."},
    {"WD_NO_HOP", "Fais d'abord un petit saut : un appui court sur {jump}."},
    {"ZAP_NO_FLIP", "Dès que l'avant touche le sol, flip avant : stick vers l'avant et {jump}, sans sauter avant."},
    {"WALL_NO_DASH", "Sur le mur, tape {jump} puis flippe tout de suite en diagonale vers l'avant, côté mur."},
    {"FA_NO_JUMP", "Saute en gardant {jump} appuyé, avec {boost} et le stick tiré vers toi."},
    {"ST_NO_STALL", "Mets le stick d'un côté et {airroll} de l'autre, puis appuie sur {jump}."},
    {"ST_TOO_LOW", "Monte plus haut avant le stall : garde {jump} appuyé au premier saut."},
    {"ST_DOUBLE_JUMP", "Un seul saut : le double saut utilise ton flip, le stall n'est plus possible. Garde {jump} pour monter."},
    {"AS_NO_SPIN", "Fais {airroll} à fond d'un côté, et le stick à fond du même côté."},
    {"PS_NO_SLIDE", "Pendant le virage, appuie un court instant sur {slide} : l'arrière doit glisser."},
};

std::string replaceAll(std::string s, const std::string& from, const std::string& to) {
    for (size_t pos = s.find(from); pos != std::string::npos; pos = s.find(from, pos + to.size())) s.replace(pos, from.size(), to);
    return s;
}

} // namespace

std::string fillButtons(std::string_view text, const PlayerProfile& profile, KeyStyle style) {
    std::string s(text);
    if (s.find('{') == std::string::npos) return s;
    s = replaceAll(s, "{jump}", actionPrompt(profile, GameAction::Jump, style));
    s = replaceAll(s, "{boost}", actionPrompt(profile, GameAction::Boost, style));
    s = replaceAll(s, "{slide}", actionPrompt(profile, GameAction::Powerslide, style));
    s = replaceAll(s, "{throttle}", actionPrompt(profile, GameAction::Throttle, style));
    s = replaceAll(s, "{airroll}", airRollPrompt(profile, 0, style));
    return s;
}

std::string airRollPrompt(const PlayerProfile& profile, int side, KeyStyle style) {
    if (side < 0 && !actionKeyLabel(profile, GameAction::AirRollLeft, style).empty())
        return actionPrompt(profile, GameAction::AirRollLeft, style);
    if (side > 0 && !actionKeyLabel(profile, GameAction::AirRollRight, style).empty())
        return actionPrompt(profile, GameAction::AirRollRight, style);
    std::string key = actionKeyLabel(profile, GameAction::AirRoll, style);
    if (key.empty()) key = actionKeyLabel(profile, GameAction::Powerslide, style);  // default bindings share the button
    std::string base = key.empty() ? std::string("l'air roll") : "l'air roll (" + key + ")";
    if (side < 0) return base + " + stick à gauche";
    if (side > 0) return base + " + stick à droite";
    return base;
}

std::string tipText(std::string_view code, const MechGrade& g, const PlayerProfile& profile, float deadzone,
                    KeyStyle style) {
    for (const Plain& p : kPlain)
        if (code == p.code) return p.fr;
    for (const Plain& p : kButtons)
        if (code == p.code) return fillButtons(p.fr, profile, style);
    const std::string jump = actionPrompt(profile, GameAction::Jump, style);
    const std::string boost = actionPrompt(profile, GameAction::Boost, style);
    const std::string throttle = actionPrompt(profile, GameAction::Throttle, style);
    const std::string slide = actionPrompt(profile, GameAction::Powerslide, style);
    const std::string dz = num2(deadzone);
    const int side = sideOfVariant(g.variant);

    if (code == "WD_TOO_EARLY") return "Attends que les roues arrière effleurent le sol avant d'appuyer sur " + jump + ".";
    if (code == "WD_SIDE_NO_POWERSLIDE")
        return "Maintiens " + slide + " à l'atterrissage d'un wavedash latéral, sinon les pneus mangent ta vitesse.";
    if (code == "WD_HIGH_HOP") return "Saute moins haut : un simple tap sur " + jump + " suffit.";
    if (code == "WD_NO_STICK")
        return "Incline le stick au-delà de ta zone morte de flip (" + dz +
               ") en sautant : stick au centre = double saut, pas de dash.";
    if (code == "WALL_TAP_SLOW") return "Rapproche tes deux appuis sur " + jump + " : tape-les très vite pour le wall dash.";
    if (code == "WALL_FELL_OFF")
        return "Flippe en diagonale VERS le mur (stick avant + " + airRollPrompt(profile, 0, style) + " côté mur) pour rester collé.";
    if (code == "CHAIN_NO_POWERSLIDE") return "Garde " + slide + " appuyé pendant toute la chaîne de wavedash.";
    if (code == "CHAIN_BOOST_USED") return "Lâche " + boost + " pendant la chaîne : le but est d'aller supersonique sans boost.";
    if (code == "SF_ANGLE_STRAIGHT")
        return "Flippe plus en diagonale : stick vers " + clockPositions(g.metric("targetAngleDeg", 30.f)) + ", pas tout droit.";
    if (code == "SF_ANGLE_WIDE")
        return "Flippe moins sur le côté : stick plus vers l'avant, vers " + clockPositions(g.metric("targetAngleDeg", 30.f)) + ".";
    if (code == "SF_BOOST_RELEASED") return "Garde " + boost + " appuyé pendant tout le speedflip.";
    if (code == "SF_THROTTLE_RELEASED") return "Garde " + throttle + " appuyé pendant tout le speedflip.";
    if (code == "SF_BAD_LANDING") return "Finis la vrille avec " + airRollPrompt(profile, side, style) + " pour retomber sur les roues.";
    if (code == "KO_NO_BOOST_AT_START") return "Appuie sur " + boost + " avant le top départ, puis accélère.";
    if (code == "HF_NO_ROLL")
        return "Après l'annulation, fais " + airRollPrompt(profile, 0, style) + " pour remettre les roues vers le sol.";
    if (code == "FA_ACCIDENTAL_DODGE")
        return "Recentre le stick avant le 2e appui sur " + jump + " : il dépassait ta zone morte de flip (" + dz +
               ") et tu as flippé.";
    if (code == "FA_GAP_SLOW")
        return "Double saut plus rapide : relâche puis réappuie tout de suite sur " + jump + ".";
    if (code == "FA_BOOST_LATE") return "Appuie sur " + boost + " dès le premier saut, pas après.";
    if (code == "FA_STICK_NEAR_DEADZONE")
        return "Recentre mieux le stick au 2e appui sur " + jump + " : il était presque assez penché pour flipper.";
    if (code == "ST_YAW_ROLL_MISMATCH") {
        // airroll-left: roll < 0, so the stick goes right.
        const bool rollLeft = g.variant.find("left") != std::string::npos;
        return std::string("Mets le stick à fond ") + (rollLeft ? "à droite" : "à gauche") + " et " +
               airRollPrompt(profile, rollLeft ? -1 : 1, style) + " à fond : yaw et air roll doivent s'annuler.";
    }
    if (code == "RC_ON_ROOF") return "Commence à te retourner plus tôt avec " + airRollPrompt(profile, 0, style) + " : tu as atterri sur le toit.";
    if (code == "PS_EXIT_SLIP") return "Relâche " + slide + " un peu plus tôt : ta voiture glisse encore en sortie.";
    if (code == "PS_NO_THROTTLE") return "Garde " + throttle + " appuyé pendant le powerslide.";
    if (code == "SM_BOOST_AT_MAX") return "Lâche " + boost + " à vitesse max : l'accélérateur suffit pour rester supersonique.";
    if (code == "SM_NO_THROTTLE") return "Garde " + throttle + " appuyé : sans lui tu ralentis et perds le supersonique.";
    if (code == "DRILL_BELOW_LEVEL") {
        if (g.hasMetric("passScore"))
            return "Presque : vise au moins " + num0(g.metric("passScore")) + "/100 pour valider ce niveau de l'exercice.";
        return "Presque : soigne chaque détail pour atteindre le score que demande ton niveau d'exercice.";
    }
    return "Recommence en te concentrant sur le geste clé de cette méca.";
}

const char* praiseText(Mech m) {
    switch (m) {
    case Mech::SpeedFlip: return "Excellent speedflip : angle, annulation et réception propres.";
    case Mech::HalfFlip: return "Half-flip propre : tu repars dans l'autre sens sans perdre de temps.";
    case Mech::FlipCancel: return "Flip annulé net : la voiture reste stable.";
    case Mech::Wavedash: return "Wavedash propre, bon gain de vitesse.";
    case Mech::ChainWavedash: return "Chaîne de wavedash parfaite : supersonique sans boost !";
    case Mech::ZapDash: return "Zap dash réussi : tu as relancé ta vitesse dès la réception.";
    case Mech::WallDash: return "Wall dash réussi : tu prends de la vitesse sur le mur.";
    case Mech::CeilingDash: return "Ceiling dash réussi.";
    case Mech::SpeedZap: return "Speedflip + zap dash : enchaînement parfait.";
    case Mech::HalfFlipWavedash: return "Half-flip suivi d'un wavedash : relance parfaite.";
    case Mech::FastAerial: return "Fast aerial efficace : timing des sauts et boost au top.";
    case Mech::DoubleJumpAerial: return "Double jump aerial propre.";
    case Mech::Stall: return "Stall réussi : la voiture flotte.";
    case Mech::AirSpin: return "Belle rotation contrôlée.";
    case Mech::Recovery: return "Réception propre : roues au sol, vitesse conservée.";
    case Mech::Powerslide: return "Powerslide efficace : virage serré sans perte de vitesse.";
    case Mech::BoostEfficiency: return "Bonne gestion de vitesse et de boost.";
    case Mech::WallChain: return "Wall dashes enchaînés : tu files le long du mur sans boost.";
    default: return "Bien joué !";
    }
}

std::string tipCode(std::string_view code) {
    std::string s = "me.";
    for (char c : code) s += (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
    return s;
}

} // namespace rlt::mech
