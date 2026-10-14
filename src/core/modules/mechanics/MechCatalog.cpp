#include "core/modules/mechanics/MechCatalog.h"

#include <cstring>
#include <iterator>

namespace rlt::mech {

namespace {

const MechInfo kMechs[] = {
    {Mech::SpeedFlip, "speedflip", "Speedflip", Focus::SpeedFlip},
    {Mech::HalfFlip, "halfflip", "Half-flip", Focus::HalfFlip},
    {Mech::FlipCancel, "flipcancel", "Flip cancel", Focus::FlipCancel},
    {Mech::Wavedash, "wavedash", "Wavedash", Focus::Wavedash},
    {Mech::ChainWavedash, "chaindash", "Chain wavedash", Focus::ChainDash},
    {Mech::ZapDash, "zapdash", "Zap dash", Focus::ZapDash},
    {Mech::WallDash, "walldash", "Wall dash", Focus::WallDash},
    {Mech::CeilingDash, "ceilingdash", "Ceiling dash", Focus::WallDash},
    {Mech::SpeedZap, "speedzap", "Speed zap", Focus::ZapDash},
    {Mech::HalfFlipWavedash, "halfflip_wavedash", "Half-flip + wavedash", Focus::HalfFlip},
    {Mech::FastAerial, "fastaerial", "Fast aerial", Focus::FastAerial},
    {Mech::DoubleJumpAerial, "doublejump_aerial", "Double jump aerial", Focus::FastAerial},
    {Mech::Stall, "stall", "Stall", Focus::Stall},
    {Mech::AirSpin, "helicopter", "Hélico / tornado", Focus::Helicopter},
    {Mech::Recovery, "recovery", "Réception", Focus::Recovery},
    {Mech::Powerslide, "powerslide", "Powerslide", Focus::Powerslide},
    {Mech::BoostEfficiency, "boost", "Gestion du boost", Focus::Boost},
    {Mech::WallChain, "walldash_chain", "Wall dash en chaîne", Focus::WallDash},
};

// Default progression text: the level only narrows the margins.
constexpr const char* kProgressMargins =
    "Le niveau monte quand tu réussis environ 3 essais sur 4 : les marges se resserrent et le score demandé monte.";

const FocusInfo kFocus[] = {
    {Focus::SpeedFlip, "speedflip", "Speedflip", "Flips",
     "Flip en diagonale annulé qui garde la voiture droite : le départ de kickoff le plus rapide.",
     "Clé : flip juste après un petit saut, en diagonale vers l'avant, puis stick tiré vers toi tout de suite.",
     Mech::SpeedFlip, Tier::Advanced, "Boost, saute, flip en diagonale, tire le stick vers toi",
     {"Boost et tourne un peu", "Saute (petit saut)", "Flip en diagonale vers l'avant",
      "Tire tout de suite le stick vers toi pour annuler le flip", "Retombe sur les roues en gardant le boost"},
     "Angle du flip : environ 30° du nez (réglable). Annulation : moins de 50 ms après le flip. Saut puis flip : "
     "moins de 110 ms, premier saut tenu environ 90 ms. Au kickoff : premier saut 0,46 à 0,58 s après le GO, boost "
     "tenu tout du long.",
     "Niveaux 1 à 3 : le flip diagonal annulé seul, sans balle ni timing de kickoff, avec des marges larges. "
     "Niveaux 4 à 6 : le vrai kickoff depuis les diagonales, avec le moment du premier saut. Niveaux 7 à 10 : "
     "toutes les positions de kickoff, marges serrées."},
    {Focus::HalfFlip, "halfflip", "Half-flip", "Flips",
     "Flip arrière annulé puis air roll : demi-tour express sans perdre de vitesse.",
     "Clé : pousse le stick vers l'avant quand la voiture est à la verticale, puis air roll jusqu'aux roues.",
     Mech::HalfFlip, Tier::Intermediate, "Flip arrière, stick en avant, air roll",
     {"Recule", "Saute", "Flip arrière (stick vers toi)", "À la verticale, pousse le stick vers l'avant",
      "Air roll pour retomber sur les roues, puis repars"},
     "Annulation 120 à 250 ms après le flip (idéal 200 ms). Air roll une fois la voiture à l'envers. Demi-tour "
     "complet (180°) en moins de 0,9 s.",
     kProgressMargins},
    {Focus::FlipCancel, "flipcancel", "Flip cancel", "Flips",
     "Stopper la rotation d'un flip en tirant le stick dans l'autre sens. C'est la base du half-flip et du speedflip.",
     "Clé : stick à fond dans le sens opposé au flip, tout de suite après l'avoir lancé.", Mech::FlipCancel,
     Tier::Intermediate, "Flip avant, tire tout de suite le stick vers toi",
     {"Saute", "Flip avant", "Tire tout de suite le stick à fond vers toi", "Retombe sur les roues"},
     "Annulation en moins de 50 ms après le flip, stick opposé à plus de 95 %.", kProgressMargins},
    {Focus::Wavedash, "wavedash", "Wavedash", "Sol",
     "Flip lancé juste avant que les roues touchent le sol : gain de vitesse immédiat.",
     "Clé : petit saut, nez un peu levé, flip quand les roues arrière effleurent le sol.", Mech::Wavedash,
     Tier::Intermediate, "Nez un peu levé, flip juste avant le sol",
     {"Petit saut", "Lève un peu le nez", "Juste avant que les roues touchent : flip", "Garde l'accélérateur"},
     "Nez cabré de 15 à 25° au flip. Roues au sol moins de 67 ms après le flip. Saut de moins de 90 uu de haut.",
     kProgressMargins},
    {Focus::ChainDash, "chaindash", "Chain wavedash", "Sol",
     "Wavedashes enchaînés sans boost jusqu'à la vitesse supersonique (infinite wavedash).",
     "Clé : relance le saut dès que les roues touchent, powerslide maintenu, sans boost.", Mech::ChainWavedash,
     Tier::Advanced, "Petit saut, flip au sol, recommence sans pause",
     {"Petit saut", "Flip avant juste avant de toucher le sol", "Garde le powerslide appuyé",
      "Relance tout de suite le saut suivant"},
     "Moins de 200 ms au sol entre deux dashes (réglable). Supersonique (2200 uu/s) sans boost, au moins 3 dashes.",
     "Le nombre de dashes demandé et la vitesse à atteindre montent avec le niveau, le temps disponible baisse."},
    {Focus::ZapDash, "zapdash", "Zap dash", "Sol",
     "Wavedash sans saut, lancé au rebond d'une réception nez vers le bas.",
     "Clé : atterris le nez légèrement baissé, puis flip avant dès que l'avant rebondit.", Mech::ZapDash,
     Tier::Advanced, "Atterris nez baissé, flip avant au rebond",
     {"Baisse un peu le nez avant de toucher le sol", "Atterris sans sauter", "Dès le rebond : flip avant"},
     "Nez entre -3 et 25° au contact. Flip moins de 67 ms avant que les roues touchent.", kProgressMargins},
    {Focus::WallDash, "walldash", "Wall dash", "Mur", "Wavedashes en chaîne sur le mur pour accélérer sans boost.",
     "Clé : deux appuis sur saut très rapprochés, flip en diagonale vers le mur.", Mech::WallDash, Tier::Advanced,
     "Tape saut deux fois très vite, flip vers le mur",
     {"Roule sur le mur", "Tape saut sans le garder",
      "Tout de suite : flip en diagonale vers l'avant et vers le mur",
      "Recommence dès que les roues retouchent le mur"},
     "Moins de 85 ms entre les deux appuis, premier saut tenu moins de 85 ms, retour sur le mur en moins de 85 ms.",
     kProgressMargins},
    {Focus::FastAerial, "fastaerial", "Fast aerial", "Air",
     "Décoller le plus vite possible pour aller chercher une balle haute.",
     "Clé : premier saut gardé un court instant avec boost et stick tiré, puis double saut tout de suite.",
     Mech::FastAerial, Tier::Intermediate, "Saute avec boost, double saut tout de suite",
     {"Saute en gardant le bouton, avec le boost et le stick vers toi", "Lâche le saut",
      "Rappuie tout de suite (double saut), stick au centre", "Garde le boost jusqu'à la balle"},
     "Premier saut tenu environ 0,2 s (195 à 225 ms). Moins de 75 ms entre le relâchement et le double saut. Boost "
     "dès le premier saut.",
     kProgressMargins},
    {Focus::Stall, "stall", "Stall", "Air", "Tornado stall : la voiture flotte en l'air en utilisant le flip.",
     "Clé : stick à fond d'un côté, air roll à fond de l'autre, puis saut.", Mech::Stall, Tier::Advanced,
     "Stick d'un côté, air roll de l'autre, saut",
     {"Stick à fond d'un côté", "Air roll à fond de l'autre côté", "Appuie sur saut", "Lâche tout pour flotter"},
     "Yaw et air roll s'annulent (somme proche de 0), stick ni en haut ni en bas. Le flip doit être disponible (moins "
     "de 1,25 s après le saut). Flottement visé : 0,4 s ou plus.",
     kProgressMargins},
    {Focus::Helicopter, "helicopter", "Hélico / tornado", "Air",
     "Rotation contrôlée en l'air (hélicoptère ou tornado spin).",
     "Clé : air roll et stick du même côté (hélico), ou air roll et boost (tornado), sans bouger.", Mech::AirSpin,
     Tier::Intermediate, "Air roll et stick du même côté, à fond",
     {"En l'air, air roll à fond d'un côté", "Stick à fond du même côté", "Garde les deux sans bouger"},
     "Rotation d'au moins 1 s, axe stable à 80 % ou plus, au moins 3 rad/s.", kProgressMargins},
    {Focus::Recovery, "recovery", "Réception", "Air",
     "Retomber sur les roues après un saut ou un aérien sans perdre de vitesse.",
     "Clé : roues vers le sol et nez dans le sens du mouvement avant de toucher.", Mech::Recovery, Tier::Beginner,
     "Roues vers le sol, nez dans le sens du mouvement",
     {"Regarde où tu vas tomber", "Tourne la voiture pour mettre les roues vers le sol",
      "Pointe le nez dans le sens du mouvement", "Atterris en gardant l'accélérateur (wavedash si tu as ton flip)"},
     "Inclinaison au contact de moins de 10°, nez à moins de 15° de la direction, 95 % de la vitesse gardée.",
     "Le lancer devient plus penché et plus rapide avec le niveau, et il faut garder plus de vitesse."},
    {Focus::Powerslide, "powerslide", "Powerslide", "Sol", "Virage serré au powerslide sans casser ta vitesse.",
     "Clé : un court appui sur powerslide, relâche dès que le nez vise la sortie.", Mech::Powerslide,
     Tier::Beginner, "Tourne, court powerslide, relâche vite",
     {"Tourne vers la cible", "Appuie un court instant sur powerslide", "Relâche dès que le nez vise la cible",
      "Accélère"},
     "85 % de la vitesse gardée ou plus, glisse en sortie de moins de 8°.", kProgressMargins},
    {Focus::Boost, "boost", "Gestion du boost", "Boost", "Rester supersonique en dépensant le moins de boost possible.",
     "Clé : accélérateur toujours appuyé, petits coups de boost, jamais de boost à vitesse max.",
     Mech::BoostEfficiency, Tier::Beginner, "Accélérateur appuyé, petits coups de boost",
     {"Garde l'accélérateur appuyé", "Petits coups de boost quand tu ralentis", "Lâche le boost à vitesse max"},
     "Moins de 5 % du boost dépensé à vitesse max, moins de 5 % du temps sans accélérateur au supersonique.",
     "Avec le niveau, il faut rester supersonique plus longtemps en dépensant moins de boost."},
};

static_assert(std::size(kMechs) == static_cast<size_t>(kMechCount), "one MechInfo per Mech, in enum order");
static_assert(std::size(kFocus) == static_cast<size_t>(kFocusCount), "one FocusInfo per Focus, in enum order");

struct Token {
    const char* from;
    const char* to;
};

// Whole dashed groups first (word order and hyphens differ in French), then single tokens.
const Token kPhrases[] = {
    {"roll-left", "air roll à gauche"},    {"roll-right", "air roll à droite"},
    {"airroll-left", "air roll à gauche"}, {"airroll-right", "air roll à droite"},
    {"no-roll", "sans air roll"},          {"no-flip", "sans flip"},
    {"u-turn", "demi-tour"},               {"double-jump", "double saut"},
};

const Token kTokens[] = {
    {"left", "gauche"},       {"right", "droite"},     {"forward", "avant"},   {"back", "arrière"},
    {"side", "côté"},         {"diag", "diag."},       {"kickoff", "kickoff"}, {"wall", "mur"},
    {"floor", "sol"},         {"ceiling", "plafond"},  {"roll", "air roll"},   {"airroll", "air roll"},
    {"no", "sans"},           {"late", "trop tard"},   {"fast", "rapide"},     {"double", "double"},
    {"jump", "saut"},         {"helicopter", "hélico"}, {"tornado", "tornado"}, {"landing", "atterrissage"},
    {"wavedash", "wavedash"}, {"cut", "virage"},       {"window", "fenêtre"},  {"straight", "droit"},
    {"flip", "flip"},         {"nostick", "stick au centre"}, {"stall", "stall"}, {"drill", "exercice"},
    {"crash", "sur le toit"},
};

std::string translateToken(const std::string& t) {
    for (const Token& k : kTokens)
        if (t == k.from) return k.to;
    return t;
}

std::string translateDashed(const std::string& part) {
    for (const Token& k : kPhrases)
        if (part == k.from) return k.to;
    // A phrase at the start of a longer group ("roll-right-..."): translate it, then the rest.
    for (const Token& k : kPhrases) {
        const size_t n = std::strlen(k.from);
        if (part.size() > n + 1 && part.compare(0, n, k.from) == 0 && part[n] == '-')
            return std::string(k.to) + " " + translateDashed(part.substr(n + 1));
    }
    std::string out, tok;
    auto flush = [&]() {
        if (tok.empty()) return;
        if (!out.empty()) out += ' ';
        out += translateToken(tok);
        tok.clear();
    };
    for (char c : part) {
        if (c == '-') flush();
        else tok += c;
    }
    flush();
    return out;
}

} // namespace

const char* tierLabel(Tier t) {
    switch (t) {
    case Tier::Beginner: return "Débutant";
    case Tier::Intermediate: return "Intermédiaire";
    case Tier::Advanced: return "Avancé";
    }
    return "";
}

const char* tierAdvice() {
    return "Débutant : commence par Réception, Powerslide et Gestion du boost, puis Flip cancel avant Half-flip et "
           "Wavedash. Le Speedflip est une méca avancée.";
}

const MechInfo& mechInfo(Mech m) {
    const int i = index(m);
    return kMechs[(i >= 0 && i < kMechCount) ? i : 0];
}

const FocusInfo& focusInfo(Focus f) {
    const int i = index(f);
    return kFocus[(i >= 0 && i < kFocusCount) ? i : 0];
}

Focus familyOf(Mech m) { return mechInfo(m).family; }

Focus focusFromId(const std::string& id) {
    std::string low;
    for (char c : id) low += (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
    for (const FocusInfo& f : kFocus)
        if (low == f.id) return f.focus;
    // A few friendly aliases.
    if (low == "half-flip" || low == "half_flip") return Focus::HalfFlip;
    if (low == "chain" || low == "chainwavedash" || low == "infinite") return Focus::ChainDash;
    if (low == "zap") return Focus::ZapDash;
    if (low == "wall") return Focus::WallDash;
    if (low == "aerial" || low == "fast_aerial") return Focus::FastAerial;
    if (low == "heli" || low == "tornado") return Focus::Helicopter;
    if (low == "cancel" || low == "flip_cancel") return Focus::FlipCancel;
    return Focus::None;
}

std::string focusIdList() {
    std::string s;
    for (const FocusInfo& f : kFocus) {
        if (!s.empty()) s += ", ";
        s += f.id;
    }
    return s;
}

std::string variantLabel(const std::string& variant) {
    std::string out, part;
    for (char c : variant) {
        if (c == '+') {
            if (!out.empty()) out += " + ";
            out += translateDashed(part);
            part.clear();
        } else {
            part += c;
        }
    }
    if (!out.empty() || !part.empty()) {
        if (!out.empty()) out += " + ";
        out += translateDashed(part);
    }
    return out;
}

} // namespace rlt::mech
