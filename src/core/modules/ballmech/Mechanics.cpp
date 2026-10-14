#include "core/modules/ballmech/Mechanics.h"

#include <cmath>

namespace rlt::bm {

namespace {
const std::array<MechInfo, kMechCount> kMechs = {{
    {Mech::FlipReset, "flipreset", "ball.flipreset", "Flip reset",
     "Pose les 4 roues sur la balle en l'air : le jeu te rend ton flip, sans limite de temps.",
     "Présente tout le dessous de la voiture face au centre de la balle, arrive doucement (lâche le boost juste "
     "avant), puis flippe dans la balle avec {jump}.",
     "Pose les 4 roues sur la balle, puis flippe dedans avec {jump}.", Family::FlipReset, 7.f, true,
     {"alignment_deg", "rel_speed", "ball_speed_after", "time_to_use"}, Difficulty::Advanced,
     {"Monte vers la balle en l'air, le dessous de la voiture tourné vers elle.",
      "Lâche le boost juste avant le contact pour arriver doucement.",
      "Pose les 4 roues sur la balle : ton flip revient (cercle vert autour de la voiture).",
      "Flippe dans la balle avec {jump} avant de retomber."}},
    {Mech::Ceiling, "ceiling", "ball.ceiling", "Ceiling shot",
     "Monte au plafond puis laisse-toi tomber sans sauter : le plafond te rend ton flip et tu le gardes pour frapper.",
     "Ne saute pas en quittant le plafond. Tu tombes à l'envers : pour pointer le nez vers le sol, tire le stick vers "
     "toi. Flippe quand la balle est juste devant ton nez.",
     "Monte au plafond, laisse-toi tomber sans sauter, puis flippe dans la balle.", Family::Ceiling, 6.f, true,
     {"ball_speed_after", "drop_to_shot", "separation_at_touch", ""}, Difficulty::Intermediate,
     {"Va au mur de côté et monte jusqu'au plafond avec {boost}.",
      "Lâche-toi du plafond sans toucher à {jump} : tu gardes ton flip.",
      "Pendant la chute tu es à l'envers : tire le stick vers toi pour pointer le nez vers la balle.",
      "Flippe dans la balle avec {jump} quand elle est juste devant ton nez."}},
    {Mech::Flick, "flick", "ball.flick", "Flick",
     "Balle posée sur le toit, saut puis flip pour la projeter (avant, 45°, 90°, arrière, inversé).",
     "Stabilise la balle au centre du toit, saute avec {jump}, tourne d'environ 45° puis flippe stick à fond.",
     "Garde la balle sur le toit, saute avec {jump} puis flippe dans la balle.", Family::Flick, 8.f, false,
     {"ball_dv", "delay", "yaw_deg", "dodge_to_touch"}, Difficulty::Intermediate,
     {"Garde la balle au centre du toit en roulant vers le but.", "Saute avec {jump} quand la balle ne bouge plus.",
      "Tourne un peu la voiture (stick sur le côté).",
      "Flippe vers la balle, stick à fond : elle part devant toi."}},
    {Mech::Musty, "musty", "ball.musty", "Musty flick",
     "Saut, nez vers le bas, puis flip arrière : le nez remonte dans la balle comme un uppercut.",
     "Pousse le stick vers l'avant juste après le saut pour baisser le nez, puis fais un flip arrière.",
     "Saute, bascule le nez vers le bas (stick avant) puis fais un flip arrière.", Family::Flick, 8.f, false,
     {"ball_dv", "pitch_deg", "delay", ""}, Difficulty::Advanced,
     {"Balle sur le toit, saute avec {jump}.", "Pousse le stick vers l'avant : le nez descend.",
      "Fais un flip arrière (stick vers toi et {jump}).", "Le nez remonte et frappe la balle par-dessous."}},
    {Mech::Breezi, "breezi", "ball.breezi", "Breezi flick",
     "Une tornade (air roll et stick opposé) enchaînée sur un musty flick.",
     "Garde {jump} enfoncé, fais tourner la voiture avec {airroll}, relâche quand le nez remonte puis flip "
     "arrière. Pas de supersonique.",
     "Saut maintenu, tornade avec {airroll}, puis flip arrière quand le nez remonte.", Family::Flick, 9.f, false,
     {"ball_dv", "roll_deg", "delay", ""}, Difficulty::Advanced,
     {"Balle sur le toit, pars sans être supersonique.", "Saute en gardant {jump} enfoncé.",
      "Fais tourner la voiture avec {airroll}, stick à l'opposé (tornade).",
      "Quand le nez remonte vers la balle, fais un flip arrière."}},
    {Mech::DoubleTap, "doubletap", "ball.doubletap", "Double tap",
     "Envoie la balle sur le mur du fond, suis-la et reprends le rebond avant de retomber.",
     "Vise haut sur le mur, reste à une longueur de voiture derrière la balle et frappe le rebond par l'arrière.",
     "Suis la balle vers le mur du fond et reprends le rebond avant de retomber.", Family::DoubleTap, 7.f, true,
     {"backboard_height", "car_dist_at_bounce", "ball_speed_after", ""}, Difficulty::Advanced,
     {"Envoie la balle haut sur le mur du fond adverse.", "Suis-la en l'air, une longueur de voiture derrière.",
      "Attends le rebond sur le mur.", "Reprends la balle par l'arrière avant de retomber, vers le but."}},
    {Mech::AirDribble, "airdribble", "ball.airdribble", "Air dribble",
     "Emmène la balle en l'air (depuis le sol ou le mur) et touche-la plusieurs fois avec le nez ou le toit.",
     "Touches douces, boost dosé avec {boost} (feathering), nez toujours pointé vers la balle.",
     "Emmène la balle en l'air et touche-la plusieurs fois, boost dosé.", Family::AirDribble, 10.f, true,
     {"duration", "touches", "avg_dist", "air_time"}, Difficulty::Advanced,
     {"Balle sur le toit (ou contre le mur), saute avec {jump} en levant un peu le nez.",
      "Boost doucement sous la balle avec {boost} : elle reste sur le nez.",
      "Touche-la par petites touches, le nez toujours vers elle.", "Finis en poussant la balle vers le but."}},
    {Mech::AirRoll, "airroll", "ball.airroll", "Air roll shot",
     "Tir aérien où tu orientes la voiture avec l'air roll (directionnel de préférence) avant le contact.",
     "Utilise {airroll} pour aligner le nez sur la balle, garde un seul sens de rotation et frappe au centre.",
     "Oriente la voiture avec {airroll} et frappe la balle au centre, vers le but.", Family::Aerial, 6.f, true,
     {"ball_speed_after", "off_axis", "air_roll_consistency", "impact_err_deg"}, Difficulty::Intermediate,
     {"Décolle vers la balle.", "Tourne la voiture avec {airroll} pour aligner le nez sur la balle.",
      "Garde un seul sens de rotation.", "Frappe la balle au centre, vers le but."}},
    {Mech::Aerial, "aerial", "ball.aerial", "Tir aérien",
     "Touche aérienne précise : frappe le centre de la balle et envoie-la dans le but.",
     "Place-toi dans l'axe balle-but avant de décoller et garde le boost jusqu'au contact.",
     "Décolle, vise le centre de la balle et cadre ton tir.", Family::Aerial, 6.f, true,
     {"ball_speed_after", "off_axis", "angle_err_deg", "impact_err_deg"}, Difficulty::Beginner,
     {"Place-toi dans l'axe balle-but avant de décoller.", "Saute avec {jump}, lève le nez puis boost avec {boost}.",
      "Vise le centre de la balle avec le nez.", "Garde le boost jusqu'au contact."}},
    {Mech::Pinch, "pinch", "ball.pinch", "Pinch",
     "Coince la balle contre le sol, le mur ou le plafond : elle part à plus de 144 km/h.",
     "Frappe la balle au moment où elle touche la surface, un peu à côté du centre (le mur sert de deuxième doigt).",
     "Frappe la balle quand elle touche le mur, un peu à côté du centre.", Family::Pinch, 7.f, false,
     {"exit_speed", "surface_gap", "push_speed", ""}, Difficulty::Intermediate,
     {"Roule vers la balle qui part vers le mur.", "Arrive au moment où elle touche le mur.",
      "Frappe-la un peu à côté du centre : le mur la coince et elle part très vite.", ""}},
    {Mech::PowerShot, "powershot", "ball.powershot", "Frappe puissante",
     "Grosse frappe au sol : flip ou glissade (powerslide) au bon moment.",
     "Arrive vite, flippe environ 0,1 s avant le contact et frappe avec l'avant ; relâche {slide} avant l'impact.",
     "Arrive vite, flippe juste avant le contact et frappe avec l'avant.", Family::PowerShot, 6.f, false,
     {"exit_speed", "car_speed", "dodge_to_contact", ""}, Difficulty::Beginner,
     {"Prends de la vitesse en ligne droite vers la balle.", "Aligne voiture, balle et but.",
      "Flippe vers l'avant juste avant de toucher la balle.", "Frappe avec l'avant de la voiture, pas le côté."}},
}};

// Basics first: ground power, simple aerials, then roof and wall play, then the advanced aerial mechanics.
// Pinch before the air roll and the ceiling shot: it is played on the ground.
const std::array<Mech, kMechCount> kLearningOrder = {
    Mech::PowerShot,  Mech::Aerial, Mech::Flick,     Mech::Pinch,     Mech::AirRoll, Mech::Ceiling,
    Mech::AirDribble, Mech::Musty,  Mech::DoubleTap, Mech::FlipReset, Mech::Breezi};

std::string baseVariant(const std::string& v) { return v.substr(0, v.find("_delayed")); }
} // namespace

const char* difficultyLabel(Difficulty d) {
    switch (d) {
    case Difficulty::Beginner: return "Débutant";
    case Difficulty::Intermediate: return "Intermédiaire";
    default: return "Avancé";
    }
}

const std::array<Mech, kMechCount>& learningOrder() { return kLearningOrder; }

const MechInfo& mechInfo(Mech m) {
    const int i = static_cast<int>(m);
    return kMechs[static_cast<size_t>(i >= 0 && i < kMechCount ? i : 0)];
}

bool mechFromId(const std::string& id, Mech& out) {
    for (const auto& m : kMechs) {
        if (id == m.id) {
            out = m.mech;
            return true;
        }
    }
    // A few friendly aliases for the console.
    if (id == "reset" || id == "flip_reset") { out = Mech::FlipReset; return true; }
    if (id == "ceiling_shot" || id == "plafond") { out = Mech::Ceiling; return true; }
    if (id == "double_tap" || id == "dt") { out = Mech::DoubleTap; return true; }
    if (id == "air_dribble" || id == "ad") { out = Mech::AirDribble; return true; }
    if (id == "power" || id == "power_shot") { out = Mech::PowerShot; return true; }
    return false;
}

Mech mechOf(const MechGrade& g) {
    switch (g.family) {
    case Family::FlipReset: return Mech::FlipReset;
    case Family::Ceiling: return Mech::Ceiling;
    case Family::DoubleTap: return Mech::DoubleTap;
    case Family::Pinch: return Mech::Pinch;
    case Family::AirDribble: return Mech::AirDribble;
    case Family::PowerShot: return Mech::PowerShot;
    case Family::Aerial: return g.variant == "air_roll_shot" ? Mech::AirRoll : Mech::Aerial;
    case Family::Flick: {
        const std::string b = baseVariant(g.variant);
        if (b == "musty") return Mech::Musty;
        if (b == "breezi" || b == "tornado") return Mech::Breezi;
        return Mech::Flick;
    }
    default: return Mech::Aerial;
    }
}

std::string gradeTitle(const MechGrade& g) {
    const std::string& v = g.variant;
    switch (g.family) {
    case Family::FlipReset:
        if (v == "used") return "Flip reset : double touche";
        if (v == "used_jump") return "Flip reset : saut dans la balle";
        if (v == "flip_missed") return "Flip reset : flip dans le vide";
        if (v == "no_reset") return "Flip reset manqué";
        return "Flip reset non utilisé";
    case Family::Ceiling: return v == "jump_off" ? "Ceiling shot (saut du plafond)" : "Ceiling shot";
    case Family::DoubleTap: return v == "read" ? "Double tap (lecture du rebond)" : "Double tap";
    case Family::AirDribble: {
        std::string t = v.rfind("wall_to_air", 0) == 0 ? "Air dribble depuis le mur" : "Air dribble depuis le sol";
        if (v.find("_carry") != std::string::npos) t += " (porté)";
        else if (v.find("_bump") != std::string::npos) t += " (en touches)";
        return t;
    }
    case Family::Aerial:
        if (v == "redirect") return "Redirection aérienne";
        if (v == "air_roll_shot") return "Air roll shot";
        return "Tir aérien";
    case Family::Pinch: {
        std::string t;
        if (v.rfind("ground_pinch", 0) == 0) t = "Pinch au sol";
        else if (v.rfind("ceiling_pinch", 0) == 0) t = "Pinch au plafond";
        else if (v.rfind("backwall_pinch", 0) == 0) t = "Pinch sur le mur du fond";
        else if (v.rfind("corner_pinch", 0) == 0) t = "Pinch dans le coin";
        else t = "Pinch mural";
        if (v.find("_kuxir") != std::string::npos) t += " (Kuxir)";
        return t;
    }
    case Family::PowerShot:
        if (v == "powerslide_shot") return "Frappe en glissade";
        if (v == "flip_shot") return "Frappe en flip";
        return "Frappe puissante";
    case Family::Flick: {
        const std::string b = baseVariant(v);
        std::string t;
        if (b == "front") t = "Flick avant";
        else if (b == "45") t = "Flick en diagonale";  // HUD title: no degrees (the 45 and 90 flicks)
        else if (b == "90") t = "Flick sur le côté";
        else if (b == "backflip") t = "Flick arrière";
        else if (b == "side") t = "Flick latéral";
        else if (b == "reverse") t = "Flick inversé (Mawkzy)";
        else if (b == "musty") t = "Musty flick";
        else if (b == "breezi") t = "Breezi flick";
        else if (b == "tornado") t = "Tornade (breezi)";
        else t = "Flick";
        if (v.find("_delayed") != std::string::npos) t += " retardé";
        return t;
    }
    default: return "Méca balle";
    }
}

bool recordValueOf(Mech m, const MechGrade& g, float score, float& value) {
    bool measured = true;
    switch (m) {
    case Mech::Flick:
    case Mech::Musty:
    case Mech::Breezi:
        value = g.ballSpeed;
        measured = g.hasShot && value > 0.f;
        break;
    case Mech::AirDribble:
        value = g.value("duration");
        measured = g.has("duration") && value > 0.f;
        break;
    case Mech::Pinch:
    case Mech::PowerShot:
        value = g.has("exit_speed") ? g.value("exit_speed") : g.ballSpeed;
        measured = (g.has("exit_speed") || g.hasShot) && value > 0.f;
        break;
    default: value = score; break;
    }
    if (!measured || !std::isfinite(value)) {
        value = 0.f;
        return false;
    }
    return true;
}

} // namespace rlt::bm
