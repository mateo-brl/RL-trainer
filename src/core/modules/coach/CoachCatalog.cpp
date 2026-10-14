#include "core/modules/coach/CoachCatalog.h"

#include <cstring>

namespace rlt {
namespace coach {

namespace {

struct Name {
    const char* key;
    const char* title;
};

const Name kFamilies[] = {
    {"first_touch", "Premières touches"},
    {"mech", "Mécas mouvement"},
    {"ball", "Mécas balle"},
    {"kickoff", "Kickoffs"},
    {"course", "Parcours"},
    {"shoot", "Tirs & flicks"},
    {"map", "Maps workshop"},
};

const Name kSkills[] = {
    // Movement mechanics (names as in the mechanics and ball mechanics tabs).
    {"mech.speedflip", "Speedflip"},
    {"mech.halfflip", "Half-flip"},
    {"mech.wavedash", "Wavedash"},
    {"mech.chaindash", "Chain wavedash"},
    {"mech.zapdash", "Zap dash"},
    {"mech.walldash", "Wall dash"},
    {"mech.fastaerial", "Fast aerial"},
    {"mech.stall", "Stall"},
    {"mech.helicopter", "Hélico / tornado"},
    {"mech.recovery", "Réception"},
    {"mech.powerslide", "Powerslide"},
    {"mech.boost", "Gestion du boost"},
    {"mech.flipcancel", "Flip cancel"},
    {"mech.ceilingdash", "Ceiling dash"},
    {"mech.speedzap", "Speed zap"},
    {"mech.halfflip_wavedash", "Half-flip + wavedash"},
    {"mech.doublejump_aerial", "Double jump aerial"},
    {"mech.walldash_chain", "Wall dash en chaîne"},
    // Ball mechanics.
    {"ball.flipreset", "Flip reset"},
    {"ball.ceiling", "Ceiling shot"},
    {"ball.flick", "Flick"},
    {"ball.musty", "Musty flick"},
    {"ball.breezi", "Breezi flick"},
    {"ball.doubletap", "Double tap"},
    {"ball.airdribble", "Air dribble"},
    {"ball.airroll", "Air roll shot"},
    {"ball.aerial", "Tir aérien"},
    {"ball.pinch", "Pinch"},
    {"ball.powershot", "Frappe puissante"},
    // Shooting.
    {"shoot.power", "Puissance de frappe"},
};

// Drill ids accepted by "rlt mechanics drill <id>", and the drill that trains the mechanics detected in
// freeplay without a drill of their own.
const char* const kMechDrills[] = {"speedflip", "halfflip", "flipcancel", "wavedash", "chaindash",
                                   "zapdash",   "walldash", "fastaerial", "stall",    "helicopter",
                                   "recovery",  "powerslide", "boost"};
const Name kMechAliases[] = {
    {"ceilingdash", "walldash"}, {"walldash_chain", "walldash"},      {"speedzap", "zapdash"},
    {"halfflip_wavedash", "halfflip"}, {"doublejump_aerial", "fastaerial"},
};
// Drill ids accepted by "rlt ballmech drill <id>".
const char* const kBallDrills[] = {"flipreset", "ceiling", "flick",  "musty", "breezi",    "doubletap",
                                   "airdribble", "airroll", "aerial", "pinch", "powershot"};

// Second-level names for parametrised families ("first_touch.<type>", "kickoff.<kind>", ...).
const Name kFirstTouch[] = {
    {"ground", "au sol"}, {"roll_in", "balle qui roule"}, {"bounce", "rebond"}, {"catch", "amorti"},
    {"drop", "amorti"},   {"wall", "sortie de mur"},      {"air", "aérienne"},  {"dribble", "dribble"},
    {"mix", "mélange"},
};
const Name kKickoff[] = {
    {"diagonal", "diagonale"}, {"offcenter", "décalé"}, {"off_center", "décalé"}, {"decale", "décalé"},
    {"center", "centre"},      {"centre", "centre"},    {"straight", "centre"},   {"back", "centre"},
    {"random", "aléatoire"},   {"weakness", "points faibles"},
};
const Name kRings[] = {{"air", "aérien"}, {"ground", "au sol"}, {"airdribble", "air dribble"}};
const Name kControl[] = {{"dribble", "dribble"}, {"slalom", "slalom"}, {"catch", "réception"}};
const Name kFlicks[] = {
    {"front", "avant"},     {"45", "45"},         {"90", "90"},           {"backflip", "back flick"},
    {"delayed", "retardé"}, {"musty", "musty"},   {"breezi", "breezi"},   {"reverse", "reverse 45"},
    {"diagonal", "diagonal"}, {"side", "latéral"}, {"any", "tous types"}, {"other", "autre"},
};
const Name kZones[] = {
    {"tl", "lucarne gauche"},    {"tr", "lucarne droite"}, {"bl", "poteau bas gauche"},
    {"br", "poteau bas droit"},  {"center", "centre"},     {"random", "aléatoire"},
};

template <size_t N>
const char* lookup(const Name (&table)[N], const std::string& key) {
    for (const auto& n : table)
        if (key == n.key) return n.title;
    return nullptr;
}

bool startsWith(const std::string& s, const char* prefix) { return s.compare(0, std::strlen(prefix), prefix) == 0; }

// "air_dribble" -> "air dribble".
std::string pretty(const std::string& raw) {
    std::string s = raw;
    for (char& c : s)
        if (c == '_' || c == '-') c = ' ';
    return s;
}

std::string capitalize(const std::string& s) {
    if (s.empty()) return s;
    std::string r = s;
    if (r[0] >= 'a' && r[0] <= 'z') r[0] = static_cast<char>(r[0] - 'a' + 'A');
    return r;
}

template <size_t N>
std::string named(const Name (&table)[N], const std::string& key) {
    if (const char* t = lookup(table, key)) return t;
    return pretty(key);
}

std::string lastSegment(const std::string& skill) {
    const size_t dot = skill.rfind('.');
    return dot == std::string::npos ? skill : skill.substr(dot + 1);
}

// "mech.speedflip.v2" -> "speedflip" (empty when there is no second segment).
std::string secondSegment(const std::string& skill) {
    const size_t dot = skill.find('.');
    if (dot == std::string::npos) return std::string();
    const size_t end = skill.find('.', dot + 1);
    return skill.substr(dot + 1, end == std::string::npos ? std::string::npos : end - dot - 1);
}

LaunchSpec make(const char* module, const char* command, std::string arg, std::string label) {
    LaunchSpec l;
    l.module = module;
    l.command = command;
    l.args.push_back(std::move(arg));
    l.label = std::move(label);
    return l;
}

} // namespace

std::string skillFamily(const std::string& skill) {
    if (skill.empty()) return "autre";
    const size_t dot = skill.find('.');
    return dot == std::string::npos ? skill : (dot == 0 ? std::string("autre") : skill.substr(0, dot));
}

std::string familyLabel(const std::string& family) {
    if (const char* t = lookup(kFamilies, family)) return t;
    if (family == "autre" || family.empty()) return "Autres";
    return capitalize(pretty(family));
}

int familyOrder(const std::string& family) {
    int i = 0;
    for (const auto& f : kFamilies) {
        if (family == f.key) return i;
        ++i;
    }
    return 100;
}

bool catalogSkillTitle(const std::string& skill, std::string& out) {
    if (const char* t = lookup(kSkills, skill)) {
        out = t;
        return true;
    }
    const std::string last = lastSegment(skill);
    if (startsWith(skill, "first_touch.")) out = "Première touche (" + named(kFirstTouch, last) + ")";
    else if (startsWith(skill, "kickoff.")) out = "Kickoff (" + named(kKickoff, last) + ")";
    else if (skill == "kickoff") out = "Kickoff";
    else if (startsWith(skill, "course.rings.")) out = "Anneaux (" + named(kRings, last) + ")";
    else if (startsWith(skill, "course.control.")) out = "Contrôle (" + named(kControl, last) + ")";
    else if (startsWith(skill, "shoot.flick.")) out = "Flick (" + named(kFlicks, last) + ")";
    else if (startsWith(skill, "shoot.target.")) out = "Tir cadré (" + named(kZones, last) + ")";
    else return false;
    return true;
}

std::string defaultSkillTitle(const std::string& skill) {
    std::string known;
    if (catalogSkillTitle(skill, known)) return known;
    if (startsWith(skill, "map.")) return "Map " + pretty(skill.substr(4));
    if (skill.empty()) return "Essai";
    return capitalize(pretty(lastSegment(skill)));
}

LaunchSpec launchForSkill(const std::string& skill) {
    const std::string fam = skillFamily(skill);
    const std::string last = lastSegment(skill);
    if (fam == "first_touch") {
        // One drill per kind of touch, never the Mix (Avancé). Sol, dribble and unknown kinds: the rolling ball.
        if (last == "bounce") return make("firsttouch", "drill", "lob", "Exercice première touche (rebond)");
        if (last == "air") return make("firsttouch", "drill", "high", "Exercice première touche (balle haute)");
        if (last == "wall") return make("firsttouch", "drill", "wall", "Exercice première touche (mur)");
        return make("firsttouch", "drill", "roll_in", "Exercice première touche (balle qui roule)");
    }
    if (fam == "kickoff") return make("kickoff", "start", "weakness", "Kickoffs ciblés sur tes points faibles");
    const std::string name = secondSegment(skill);
    if (fam == "mech" && !name.empty()) {
        std::string drill;
        for (const char* d : kMechDrills)
            if (name == d) drill = d;
        if (drill.empty())
            if (const char* alias = lookup(kMechAliases, name)) drill = alias;
        if (drill.empty()) return LaunchSpec{};
        return make("mechanics", "drill", drill, "Exercice " + defaultSkillTitle("mech." + drill));
    }
    if (fam == "ball" && !name.empty()) {
        for (const char* d : kBallDrills)
            if (name == d) return make("ballmech", "drill", name, "Exercice " + defaultSkillTitle("ball." + name));
        return LaunchSpec{};
    }
    if (startsWith(skill, "course.rings")) {
        const std::string t = lookup(kRings, last) ? last : std::string("ground");
        return make("courses", "rings", t, "Parcours d'anneaux (" + named(kRings, t) + ")");
    }
    if (startsWith(skill, "course.control")) {
        const std::string t = lookup(kControl, last) ? last : std::string("dribble");
        return make("courses", "control", t, "Parcours de contrôle (" + named(kControl, t) + ")");
    }
    if (startsWith(skill, "shoot.flick")) return make("shooting", "flick", "any", "Exercice de flicks");
    if (startsWith(skill, "shoot.target")) return make("shooting", "targets", "random", "Tirs sur cibles");
    if (skill == "shoot.power") {
        LaunchSpec l = make("shooting", "power", std::string(), "Exercice de frappes puissantes");
        l.args.clear();
        return l;
    }
    return LaunchSpec{};
}

bool moduleAnnouncesRecords(const std::string& module) {
    // Checked in their sources: each one toasts (and some also chat) its own new records.
    static const char* const kAnnouncing[] = {"mechanics", "kickoff", "courses", "shooting", "maps"};
    for (const char* m : kAnnouncing)
        if (module == m) return true;
    return false;
}

std::vector<PlanItem> starterPlan() {
    std::vector<PlanItem> plan;
    PlanItem a;
    a.skill = "first_touch.roll_in";
    a.title = "Première touche";
    a.family = "first_touch";
    a.launch = make("firsttouch", "drill", "roll_in", "Exercice première touche (balle qui roule)");
    a.reason = "Pour bien démarrer : contrôle une balle qui roule vers toi, la base de tout.";
    a.starter = true;
    plan.push_back(a);

    PlanItem b;
    b.skill = "kickoff.random";
    b.title = "Kickoffs";
    b.family = "kickoff";
    b.launch = make("kickoff", "start", "random", "Kickoffs aléatoires");
    b.reason = "Chaque but se rejoue au kickoff : prends l'avantage dès l'engagement.";
    b.starter = true;
    plan.push_back(b);

    PlanItem c;
    c.skill = "mech.recovery";
    c.title = "Réception";
    c.family = "mech";
    c.launch = make("mechanics", "drill", "recovery", "Exercice Réception");
    c.reason = "Pour bien démarrer : retombe sur tes roues sans perdre ta vitesse.";
    c.starter = true;
    plan.push_back(c);
    return plan;
}

} // namespace coach
} // namespace rlt
