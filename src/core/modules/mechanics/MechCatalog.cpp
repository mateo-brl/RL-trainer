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

const FocusInfo kFocus[] = {
    {Focus::SpeedFlip, "speedflip", "Speedflip", "Flips",
     "Flip diagonal annulé qui garde le nez droit : le départ de kickoff le plus rapide.",
     "Clé : petit saut, flip à ~30° du nez, stick tiré en arrière tout de suite, boost tenu.", Mech::SpeedFlip},
    {Focus::HalfFlip, "halfflip", "Half-flip", "Flips",
     "Flip arrière annulé puis air roll : demi-tour express sans perdre de vitesse.",
     "Clé : annule quand la voiture est à la verticale (~0,2 s), puis air roll jusqu'aux roues.", Mech::HalfFlip},
    {Focus::FlipCancel, "flipcancel", "Flip cancel", "Flips",
     "Stopper la rotation d'un flip en tirant le stick dans le sens opposé.",
     "Clé : stick à fond à l'opposé du flip dans les 50 ms qui suivent l'appui.", Mech::FlipCancel},
    {Focus::Wavedash, "wavedash", "Wavedash", "Sol",
     "Flip déclenché juste avant que les roues touchent : gain de vitesse instantané.",
     "Clé : petit saut, nez cabré de 15 à 25°, flip quand les roues arrière effleurent le sol.", Mech::Wavedash},
    {Focus::ChainDash, "chaindash", "Chain wavedash", "Sol",
     "Wavedashes enchaînés sans boost jusqu'au supersonique (infinite wavedash).",
     "Clé : relance le saut dès que les roues touchent, powerslide maintenu, zéro boost.", Mech::ChainWavedash},
    {Focus::ZapDash, "zapdash", "Zap dash", "Sol",
     "Wavedash sans saut, déclenché par le rebond d'une réception nez vers le bas.",
     "Clé : atterris le nez légèrement baissé, flip avant dès que l'avant décolle.", Mech::ZapDash},
    {Focus::WallDash, "walldash", "Wall dash", "Mur",
     "Wavedashes en chaîne sur le mur pour accélérer sans boost.",
     "Clé : deux appuis sur saut très rapprochés (< 85 ms), flip en diagonale vers le mur.", Mech::WallDash},
    {Focus::FastAerial, "fastaerial", "Fast aerial", "Air",
     "Décollage le plus rapide possible pour aller chercher une balle haute.",
     "Clé : saut tenu 0,2 s avec boost et stick tiré, puis double saut immédiat stick au centre.", Mech::FastAerial},
    {Focus::Stall, "stall", "Stall", "Air",
     "Tornado stall : la voiture flotte en l'air en consommant le flip.",
     "Clé : stick à fond d'un côté, air roll à fond de l'autre, puis saut.", Mech::Stall},
    {Focus::Helicopter, "helicopter", "Hélico / tornado", "Air",
     "Rotation contrôlée en l'air (hélicoptère ou tornado spin).",
     "Clé : air roll et yaw du même côté (hélico) ou air roll et boost (tornado), constants.", Mech::AirSpin},
    {Focus::Recovery, "recovery", "Réception", "Air",
     "Retomber sur les roues après un aérien sans perdre de vitesse.",
     "Clé : roues vers le sol et nez dans le sens du mouvement avant l'impact, wavedash si tu as ton flip.",
     Mech::Recovery},
    {Focus::Powerslide, "powerslide", "Powerslide", "Sol",
     "Virage serré au powerslide sans casser ta vitesse.",
     "Clé : quelques frames de powerslide seulement, relâche dès que le nez vise la sortie.", Mech::Powerslide},
    {Focus::Boost, "boost", "Gestion du boost", "Boost",
     "Rester supersonique en dépensant le moins de boost possible.",
     "Clé : accélérateur toujours appuyé, petits taps de boost (feathering), jamais à vitesse max.",
     Mech::BoostEfficiency},
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
