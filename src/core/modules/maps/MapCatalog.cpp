#include "core/modules/maps/MapCatalog.h"

#include <algorithm>
#include <cctype>

namespace rlt {

// ---- Categories ----------------------------------------------------------------------------------------
const char* mapCategoryLabel(MapCategory c) {
    switch (c) {
    case MapCategory::Standard: return "Standard";
    case MapCategory::Rings: return "Anneaux";
    case MapCategory::Dribble: return "Dribble";
    case MapCategory::AirDribble: return "Air dribble";
    case MapCategory::Shooting: return "Tir";
    case MapCategory::Obstacle: return "Obstacles";
    case MapCategory::Other: return "Autre";
    }
    return "Autre";
}

const char* mapCategoryKey(MapCategory c) {
    switch (c) {
    case MapCategory::Standard: return "standard";
    case MapCategory::Rings: return "rings";
    case MapCategory::Dribble: return "dribble";
    case MapCategory::AirDribble: return "airdribble";
    case MapCategory::Shooting: return "shooting";
    case MapCategory::Obstacle: return "obstacle";
    case MapCategory::Other: return "other";
    }
    return "other";
}

const char* mapCategoryPurpose(MapCategory c) {
    switch (c) {
    case MapCategory::Rings: return "Navigation aérienne, gestion du boost et recoveries.";
    case MapCategory::Dribble: return "Contrôle de balle au sol : balle sur le toit, petites touches.";
    case MapCategory::AirDribble: return "Air dribble : garder la balle collée en l'air avec le nez.";
    case MapCategory::Shooting: return "Précision et puissance des tirs, pinchs et premières touches.";
    case MapCategory::Obstacle: return "Contrôle de la voiture : sauts, murs, trajectoires propres.";
    case MapCategory::Standard: return "Terrain standard.";
    case MapCategory::Other: return "Map d'entraînement variée.";
    }
    return "";
}

bool parseMapCategory(const std::string& text, MapCategory& out) {
    const std::string t = toLowerAscii(text);
    if (t.empty()) return false;
    if (t.size() == 1 && t[0] >= '0' && t[0] < static_cast<char>('0' + kMapCategoryCount)) {
        out = static_cast<MapCategory>(t[0] - '0');
        return true;
    }
    for (int i = 0; i < kMapCategoryCount; ++i) {
        const auto c = static_cast<MapCategory>(i);
        if (t == mapCategoryKey(c) || t == toLowerAscii(mapCategoryLabel(c)) ||
            normalizeMapName(t) == normalizeMapName(mapCategoryLabel(c))) {
            out = c;
            return true;
        }
    }
    // A few friendly aliases.
    if (t == "anneaux" || t == "ring") out = MapCategory::Rings;
    else if (t == "tir" || t == "shoot" || t == "aim") out = MapCategory::Shooting;
    else if (t == "obstacles" || t == "parcours") out = MapCategory::Obstacle;
    else if (t == "autre") out = MapCategory::Other;
    else return false;
    return true;
}

// ---- Catalogue data ------------------------------------------------------------------------------------
namespace {

CatalogMap entry(std::string title, std::string author, MapCategory cat, std::vector<std::string> ids,
                 std::vector<std::string> aliases, int levels, std::string trains, std::string profile = {},
                 bool recommended = false) {
    CatalogMap m;
    m.title = std::move(title);
    m.author = std::move(author);
    m.category = cat;
    m.ids = std::move(ids);
    m.aliases = std::move(aliases);
    m.levels = levels;
    m.trains = std::move(trains);
    m.profile = std::move(profile);
    m.recommended = recommended;
    return m;
}

std::vector<CatalogMap> buildCatalog() {
    using C = MapCategory;
    std::vector<CatalogMap> v;
    v.reserve(48);
    // ---- Rings / aerial courses
    v.push_back(entry("Lethamyr's Giant Rings", "Lethamyr", C::Rings, {"2025564083"},
                      {"giantrings", "giantringsmap", "lethgiantrings", "lethamyrgiantrings", "lethamyrgiantringsmap",
                       "lethamyrsgiantringsmap"}, 20,
                      "20 niveaux d'anneaux géants : navigation aérienne et gestion du boost.", "leth_rings", true));
    v.push_back(entry("Lethamyr's Tiny Rings", "Lethamyr", C::Rings, {"2541758604"},
                      {"tinyrings", "tinyringsmap", "lethtinyrings", "lethamyrtinyrings"}, 20,
                      "Le parcours Giant Rings avec des anneaux plus petits : précision aérienne.", "leth_rings"));
    v.push_back(entry("Lethamyr's Lava Rings", "Lethamyr", C::Rings, {"2104185189"},
                      {"lavarings", "lavaringsmap", "lethlavarings"}, 20,
                      "Variante lave des anneaux de Lethamyr : contrôle aérien sous pression.", "leth_rings"));
    v.push_back(entry("Lethamyr's Neon Heights Rings", "Lethamyr", C::Rings, {"2468375059"},
                      {"neonrings", "neonheights", "neonheightsrings", "lethneonrings"}, 20,
                      "20 niveaux d'anneaux dans une ville : trajectoires et boost.", "leth_rings", true));
    v.push_back(entry("Lethamyr's Medieval Rings", "Lethamyr", C::Rings, {"2815095306"},
                      {"medievalrings", "lethmedievalrings"}, 20,
                      "20 niveaux d'anneaux de plus en plus petits : vise ton record.", "leth_rings"));
    v.push_back(entry("Speed Jump: Rings 1", "dmc", C::Rings, {"1768760178"}, {"speedjumprings1", "sjr1"}, 162,
                      "162 anneaux, parcours difficile : air roll et vitesse.", "sjr1"));
    v.push_back(entry("Speed Jump: Rings 2", "dmc", C::Rings, {"2734626907", "1454827766"},
                      {"speedjumprings2", "sjr2", "speedjumprings2timerupdate"}, 0,
                      "Parcours d'anneaux intermédiaire, le plus joué pour progresser en aérien.", "sjr2", true));
    v.push_back(entry("Speed Jump: Rings 3", "dmc", C::Rings, {"2779629941", "1671658424"},
                      {"speedjumprings3", "sjr3", "speedjumprings3timerupdate"}, 15,
                      "Parcours d'anneaux intermédiaire à difficile, 14 segments.", "sjr3"));
    v.push_back(entry("Panic's Air Race: Beach", "Panic", C::Rings, {"2204381361"},
                      {"panicsairrace", "airracebeach", "panicsairracebeach"}, 0,
                      "Course aérienne avec checkpoints : vitesse en l'air.", "panic_air_race"));
    v.push_back(entry("Rings Of Death", "Rocket League Mapmaking", C::Rings, {"3583433749"}, {"ringsofdeath"}, 10,
                      "10 niveaux d'anneaux exigeants."));
    v.push_back(entry("Orbital Rings", "Rocket League Mapmaking", C::Rings, {"3616409796"}, {"orbitalrings"}, 10,
                      "10 niveaux d'anneaux en orbite."));
    v.push_back(entry("Fractals Tree Rings", "Rocket League Mapmaking", C::Rings, {"3477750050"},
                      {"fractalstreerings", "fractalsrings"}, 10, "10 niveaux d'anneaux."));
    v.push_back(entry("Sharingan Rings", "ace", C::Rings, {"3250504064"}, {"sharinganrings"}, 13,
                      "13 niveaux d'anneaux."));
    v.push_back(entry("Future Rings", "JoshB", C::Rings, {"2457429625"}, {"futurerings"}, 0, "Parcours d'anneaux."));
    v.push_back(entry("Lucid Rings", "", C::Rings, {"2116244468"}, {"lucidrings"}, 0, "Parcours d'anneaux."));
    // ---- Obstacle courses
    v.push_back(entry("Obstacle Course 1", "French Fries", C::Obstacle, {"814207936"},
                      {"obstaclecourse1", "obstaclecourse"}, 0,
                      "Défis d'obstacles en vol : contrôle de la voiture.", {}, true));
    v.push_back(entry("Obstacle Course #2", "French Fries", C::Obstacle, {"828803580"},
                      {"obstaclecourse2", "obstaclecourse2v11"}, 0, "Obstacles en vol, plus durs."));
    v.push_back(entry("Speed Jump: Trials 1", "dmc", C::Obstacle, {"1578331091"}, {"speedjumptrials1"}, 0,
                      "Parcours de sauts et de conduite (facile)."));
    v.push_back(entry("Speed Jump: Trials 2", "dmc", C::Obstacle, {"1581379736"}, {"speedjumptrials2"}, 0,
                      "Parcours de sauts et de conduite."));
    v.push_back(entry("Speed Jump: Trials 3", "dmc", C::Obstacle, {"1759763738"}, {"speedjumptrials3"}, 0,
                      "Parcours de sauts et de conduite (difficile)."));
    v.push_back(entry("Speed Jump: Trials 4", "dmc", C::Obstacle, {"1763596534"}, {"speedjumptrials4"}, 0,
                      "Parcours de sauts et de conduite (difficile)."));
    v.push_back(entry("Speed Jump 2", "dmc", C::Obstacle, {"1429288182"}, {"speedjump2"}, 0,
                      "Parcours d'obstacles."));
    v.push_back(entry("Speed Jump: Boost", "dmc", C::Obstacle, {"1421195368"}, {"speedjumpboost"}, 0,
                      "Parcours d'obstacles extrême au boost."));
    v.push_back(entry("Aerial Training", "thanrek", C::Obstacle, {"814164094"}, {"aerialtraining"}, 12,
                      "12 niveaux de contrôle de la voiture en l'air."));
    v.push_back(entry("The Wall", "French Fries", C::Obstacle, {"814263405"}, {"thewall"}, 0,
                      "Course d'obstacles poursuivie par un mur."));
    v.push_back(entry("Lethamyr's Egyptian Tomb", "Lethamyr", C::Obstacle, {},
                      {"egyptiantomb", "lethsegyptiantomb", "lethamyrsegyptiantomb"}, 0,
                      "Parcours d'exploration avec des ankhs à ramasser.", "egyptian_tomb"));
    // ---- Ground dribble
    v.push_back(entry("Dribbling Challenge #2", "French Fries", C::Dribble, {"964271505"},
                      {"dribblingchallenge2", "dribblechallenge2"}, 30,
                      "30 niveaux de dribble au sol : le classique du contrôle de balle.", {}, true));
    v.push_back(entry("Dribble 2 Overhaul", "0xDigby", C::Dribble, {"1694288506"},
                      {"dribble2overhaul", "dribblingchallenge2overhaul"}, 30,
                      "Dribbling Challenge #2 avec resets rapides et mode chrono.", "dribble2_overhaul", true));
    v.push_back(entry("Dribbling Challenge 1.2", "French Fries", C::Dribble, {"814218628"},
                      {"dribblingchallenge12", "dribblingchallenge1", "dribblingchallenge"}, 0,
                      "Parcours de dribble."));
    v.push_back(entry("Noob Dribble", "dmc", C::Dribble, {"1843822064"}, {"noobdribble"}, 0,
                      "Parcours de dribble pour débuter : idéal en premier.", {}, true));
    v.push_back(entry("Lethamyr's Dribble Challenge", "Lethamyr", C::Dribble, {"1903946125"},
                      {"lethdribblechallenge", "lethamyrsdribblechallenge"}, 0, "Parcours de dribble avancé."));
    v.push_back(entry("Lethamyr's REMASTERED Dribble Challenge", "Lethamyr", C::Dribble, {"2793609394"},
                      {"remastereddribblechallenge", "lethamyrsremastereddribblechallenge"}, 0,
                      "Le parcours de Lethamyr avec sélection de niveau."));
    v.push_back(entry("Eversax's Dribble Challenge", "Eversax", C::Dribble, {"2489923276"},
                      {"eversaxdribblechallenge", "eversaxsdribblechallenge"}, 0, "Parcours de dribble."));
    v.push_back(entry("Limited Boost Dribble Challenge", "adixox12", C::Dribble, {"3457231221"},
                      {"limitedboostdribblechallenge", "lbdc"}, 10,
                      "10 niveaux durs avec boost limité, chrono intégré."));
    v.push_back(entry("Impossible Dribble", "adixox12", C::Dribble, {"3482406520"}, {"impossibledribble"}, 0,
                      "Dribble très difficile, chrono intégré."));
    v.push_back(entry("Impossible Dribble 2", "adixox12", C::Dribble, {"3490861613"}, {"impossibledribble2"}, 0,
                      "Dribble très difficile, chrono intégré."));
    v.push_back(entry("OpenDribble", "", C::Dribble, {"1546981021"}, {"opendribble"}, 0, "Zone libre de dribble."));
    v.push_back(entry("Intermediate (or not) Dribble Map", "", C::Dribble, {"2611039001"},
                      {"intermediatedribblemap", "intermediateornotdribblemap"}, 0, "Parcours de dribble."));
    v.push_back(entry("The Floor is Lava dribbling", "", C::Dribble, {"1893307036"},
                      {"thefloorislava", "floorislavadribbling"}, 0, "Dribble sans toucher le sol."));
    // ---- Air dribble
    v.push_back(entry("Air Dribble Challenge", "gidek", C::AirDribble, {"1576137650"}, {"airdribblechallenge"}, 0,
                      "Garde la balle en l'air le plus longtemps possible.", {}, true));
    v.push_back(entry("Air Dribble Hoops", "tjbrother", C::AirDribble, {"2100268170"}, {"airdribblehoops"}, 21,
                      "21 niveaux d'air dribble à travers des cerceaux (boost illimité conseillé).", "ad_hoops", true));
    v.push_back(entry("Air Dribble Gauntlet", "ace", C::AirDribble, {"3241139478"}, {"airdribblegauntlet"}, 10,
                      "10 niveaux d'air dribble en solo."));
    v.push_back(entry("MUGZRD'S AIR DRIBBLE GAUNTLET!", "ChrisClearsTV", C::AirDribble, {"1992438961"},
                      {"mugzrdsairdribblegauntlet"}, 0, "Gauntlet d'air dribble."));
    v.push_back(entry("Lethamyr's Air Dribble Gauntlet 1", "Lethamyr", C::AirDribble, {"3263947955"},
                      {"lethairdribblegauntlet1"}, 0, "Gauntlet d'air dribble (temps et boost illimités conseillés)."));
    v.push_back(entry("Lethamyr's Air Dribble Gauntlet 2", "Lethamyr", C::AirDribble, {"3263959018"},
                      {"lethairdribblegauntlet2"}, 9, "9 niveaux d'air dribble."));
    v.push_back(entry("Air Dribble Melee Single Player", "GrandChimp", C::AirDribble, {"2503669574"},
                      {"airdribblemelee", "airdribblemeleesingleplayer"}, 0, "Air dribbles jusqu'au but."));
    v.push_back(entry("Air Dribble Pillars", "", C::AirDribble, {"2091448664"}, {"airdribblepillars"}, 0,
                      "Air dribble entre des piliers."));
    v.push_back(entry("The Air Dribbling Test", "", C::AirDribble, {"1471221134"}, {"theairdribblingtest"}, 0,
                      "Test d'air dribble."));
    v.push_back(entry("Air Dribble Warm Up", "", C::AirDribble, {"3583920889"}, {"airdribblewarmup"}, 0,
                      "Échauffement d'air dribble."));
    // ---- Shooting / first touch
    v.push_back(entry("Aim Training", "CoCo", C::Shooting, {"1906378036"}, {"aimtraining", "aimtrainingcoco"}, 0,
                      "Précision de tir : modes Facile, Normal et Survie, avec score et vies.", {}, true));
    v.push_back(entry("Pinch-Map", "Rocket League Mapmaking", C::Shooting, {"2826761038"}, {"pinchmap"}, 22,
                      "22 niveaux de pinchs : précision du contact.", {}, true));
    v.push_back(entry("Minigolf", "FroYo", C::Shooting, {"2142821184"}, {"minigolf", "minigolf1"}, 0,
                      "Dosage de la puissance des touches, compteur de coups."));
    return v;
}

} // namespace

const std::vector<CatalogMap>& MapCatalog::all() {
    static const std::vector<CatalogMap> catalog = buildCatalog();
    return catalog;
}

const CatalogMap* MapCatalog::findById(const std::string& workshopId) {
    if (workshopId.empty()) return nullptr;
    for (const auto& m : all())
        for (const auto& id : m.ids)
            if (id == workshopId) return &m;
    return nullptr;
}

const CatalogMap* MapCatalog::findByStem(const std::string& stemOrMapName) {
    const std::string key = stemKey(stemOrMapName);
    if (key.size() < 3) return nullptr;
    for (const auto& m : all()) {
        if (stemKey(m.title) == key) return &m;
        for (const auto& a : m.aliases)
            if (stemKey(a) == key) return &m;
    }
    return nullptr;
}

const CatalogMap* MapCatalog::find(const std::string& idOrStem) {
    if (const CatalogMap* m = findById(idOrStem)) return m;
    return findByStem(idOrStem);
}

std::vector<const CatalogMap*> MapCatalog::recommended(MapCategory c) {
    std::vector<const CatalogMap*> out;
    for (const auto& m : all())
        if (m.recommended && m.category == c) out.push_back(&m);
    return out;
}

std::vector<const CatalogMap*> MapCatalog::byCategory(MapCategory c) {
    std::vector<const CatalogMap*> out;
    for (const auto& m : all())
        if (m.category == c) out.push_back(&m);
    return out;
}

// ---- Name helpers --------------------------------------------------------------------------------------
std::string toLowerAscii(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

std::string normalizeMapName(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c >= 'A' && c <= 'Z') out.push_back(static_cast<char>(c - 'A' + 'a'));
        else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) out.push_back(c);
    }
    return out;
}

namespace {
bool endsWith(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// Lower-case stem without extension, Workshop Map Loader "_antifreeze" suffix and Unreal "_p" suffix.
std::string strippedLowerStem(const std::string& in) {
    std::string s = toLowerAscii(in);
    // Directory part, if a path was given.
    const size_t slash = s.find_last_of("/\\");
    if (slash != std::string::npos) s = s.substr(slash + 1);
    if (endsWith(s, ".udk") || endsWith(s, ".upk")) s.resize(s.size() - 4);
    if (endsWith(s, "_antifreeze")) s.resize(s.size() - 11);
    if (endsWith(s, "_p")) s.resize(s.size() - 2);
    return s;
}
} // namespace

std::string stemKey(const std::string& stemOrFileName) { return normalizeMapName(strippedLowerStem(stemOrFileName)); }

bool isWorkshopId(const std::string& s) {
    if (s.size() < 4 || s.size() > 20) return false;
    return std::all_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; });
}

MapCategory guessMapCategory(const std::string& name) {
    const std::string n = normalizeMapName(name);
    if (n.empty()) return MapCategory::Other;
    auto has = [&](const char* k) { return n.find(k) != std::string::npos; };
    if (has("airdribbl") || has("hoops") || has("aerialdribbl")) return MapCategory::AirDribble;
    if (has("dribbl")) return MapCategory::Dribble;
    // "ring" alone: only as a whole word of the original name (avoids "spring", "string").
    bool ringWord = false;
    {
        const std::string lower = toLowerAscii(name);
        std::string word;
        for (size_t i = 0; i <= lower.size(); ++i) {
            const char c = i < lower.size() ? lower[i] : ' ';
            if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
                word.push_back(c);
            } else {
                if (word == "ring" || word == "rings") ringWord = true;
                word.clear();
            }
        }
    }
    if (has("rings") || ringWord) return MapCategory::Rings;
    if (has("obstacle") || has("speedjump") || has("parkour") || has("trial") || has("course") || has("tower") ||
        has("gauntlet"))
        return MapCategory::Obstacle;
    if (has("aim") || has("shoot") || has("shot") || has("pinch") || has("flick") || has("golf") || has("striker") ||
        has("redirect"))
        return MapCategory::Shooting;
    return MapCategory::Other;
}

std::string mapStatsKey(const std::string& workshopId, const std::string& mapName) {
    if (isWorkshopId(workshopId)) return workshopId;
    const std::string& source = workshopId.empty() ? mapName : workshopId;
    std::string s = toLowerAscii(source);
    const size_t slash = s.find_last_of("/\\");
    if (slash != std::string::npos) s = s.substr(slash + 1);
    if (endsWith(s, ".udk") || endsWith(s, ".upk")) s.resize(s.size() - 4);
    if (endsWith(s, "_antifreeze")) s.resize(s.size() - 11);
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
        const char w = ok ? c : '_';
        if (w == '_' && !out.empty() && out.back() == '_') continue;  // collapse separators
        out.push_back(w);
    }
    while (!out.empty() && (out.front() == '_' || out.front() == '-')) out.erase(out.begin());
    while (!out.empty() && out.back() == '_') out.pop_back();
    if (out.size() > 64) out.resize(64);
    if (out.empty() && !source.empty()) out = "map";
    return out;
}

std::string prettifyMapName(const std::string& stem) {
    std::string s = stem;
    const size_t slash = s.find_last_of("/\\");
    if (slash != std::string::npos) s = s.substr(slash + 1);
    const std::string lower = toLowerAscii(s);
    size_t cut = s.size();
    if (endsWith(lower, ".udk") || endsWith(lower, ".upk")) cut -= 4;
    s.resize(cut);
    if (endsWith(toLowerAscii(s), "_antifreeze")) s.resize(s.size() - 11);
    if (endsWith(toLowerAscii(s), "_p")) s.resize(s.size() - 2);
    std::string out;
    out.reserve(s.size() + 8);
    for (size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (c == '_' || c == '-' || c == '.') {
            if (!out.empty() && out.back() != ' ') out.push_back(' ');
            continue;
        }
        // camelCase boundary: "GiantRings" -> "Giant Rings".
        const bool upper = c >= 'A' && c <= 'Z';
        if (upper && i > 0) {
            const char p = s[i - 1];
            if (p >= 'a' && p <= 'z' && !out.empty() && out.back() != ' ') out.push_back(' ');
        }
        out.push_back(c);
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out.empty() ? stem : out;
}

std::vector<MapHint> mapCategoryHints(MapCategory c) {
    switch (c) {
    case MapCategory::Rings:
        return {
            {"Mécas mouvement", "Tes mécas aériennes (fast aerial, air roll, recoveries) sont analysées en direct."},
            {"Prédiction", "Pas de balle sur ces maps : coupe la prédiction pour alléger l'écran."},
            {"Progression", "Chaque run terminé arrive dans Progression avec ton record et tes splits."}};
    case MapCategory::Dribble:
        return {
            {"Mécas balle", "Active la détection de dribble pour mesurer ton temps de balle sur le toit."},
            {"Première touche", "Garde l'analyse des touches : vise des touches douces pour ne pas perdre la balle."},
            {"Affichage", "Le cercle de contact t'aide à placer la balle au centre du toit."}};
    case MapCategory::AirDribble:
        return {
            {"Affichage", "Active le cercle de contact en mode Auto : il suit la balle pour monter tes air dribbles."},
            {"Prédiction", "Affiche la trajectoire de la balle pour te placer dessous plus tôt."},
            {"Mécas balle", "Tes air dribbles et flip resets sont détectés et notés."}};
    case MapCategory::Shooting:
        return {{"Tirs & flicks", "La vitesse et la précision de chaque tir sont mesurées."},
                {"Première touche", "Regarde l'écart de vitesse au contact pour doser ta puissance."},
                {"Prédiction", "La trajectoire t'aide à lire les rebonds avant de frapper."}};
    case MapCategory::Obstacle:
        return {{"Mécas mouvement", "Wavedash, half flip et speedflip sont détectés pendant le parcours."},
                {"Affichage", "Le chrono de session suit ton temps d'entraînement total."}};
    case MapCategory::Standard:
    case MapCategory::Other:
        break;
    }
    return {{"Progression", "Tes runs terminés et tes records sont enregistrés pour cette map."}};
}

} // namespace rlt
