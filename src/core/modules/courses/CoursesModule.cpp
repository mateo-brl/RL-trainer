#include "core/modules/courses/CoursesModule.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "core/common/Text.h"

namespace rlt {

namespace {

struct KindInfo {
    const char* key;
    const char* title;
    const char* skill;
    const char* help;
    int tier;  // 0 Débutant, 1 Intermédiaire, 2 Avancé
};

constexpr KindInfo kKinds[kCoursesKindCount] = {
    {"air", "Anneaux aériens", "course.rings.air",
     "Vole à travers des anneaux générés : contrôle aérien, trajectoires propres et gestion du boost.", 2},
    {"ground", "Anneaux sol & murs", "course.rings.ground",
     "Des arches au sol et sur les murs : vitesse, trajectoires et transitions sol-mur.", 0},
    {"airdribble", "Air dribble", "course.rings.airdribble",
     "Fais passer la BALLE dans les anneaux en air dribble. Elle ne doit jamais toucher le sol.", 2},
    {"dribble", "Dribble sur le toit", "course.control.dribble",
     "Garde la balle sur le toit le plus longtemps possible en roulant.", 0},
    {"slalom", "Slalom", "course.control.slalom", "Passe chaque porte avec la balle sur le toit, sans la faire tomber.",
     1},
    {"catch", "Rattrapage", "course.control.catch",
     "La balle tombe du ciel ou sort d'un mur : amortis-la et garde-la sur le toit.", 1},
};

const KindInfo& info(CoursesKind k) { return kKinds[std::clamp(static_cast<int>(k), 0, kCoursesKindCount - 1)]; }

courses::CourseType typeForKind(CoursesKind k) {
    switch (k) {
    case CoursesKind::RingsGround: return courses::CourseType::GroundWall;
    case CoursesKind::RingsAirDribble: return courses::CourseType::AirDribble;
    case CoursesKind::Slalom: return courses::CourseType::DribbleGates;
    default: return courses::CourseType::Aerial;
    }
}

CoursesKind kindForType(courses::CourseType t) {
    switch (t) {
    case courses::CourseType::GroundWall: return CoursesKind::RingsGround;
    case courses::CourseType::AirDribble: return CoursesKind::RingsAirDribble;
    case courses::CourseType::DribbleGates: return CoursesKind::Slalom;
    default: return CoursesKind::RingsAir;
    }
}

std::string lower(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

bool parseKind(const std::string& raw, CoursesKind& out) {
    const std::string s = lower(raw);
    if (s == "air" || s == "aerial" || s == "aerien" || s == "aeriens" || s == "a") out = CoursesKind::RingsAir;
    else if (s == "ground" || s == "sol" || s == "murs" || s == "wall" || s == "walls" || s == "g") out = CoursesKind::RingsGround;
    else if (s == "airdribble" || s == "ad" || s == "air_dribble" || s == "d") out = CoursesKind::RingsAirDribble;
    else if (s == "dribble" || s == "toit" || s == "roof" || s == "keepup") out = CoursesKind::Dribble;
    else if (s == "slalom" || s == "s" || s == "gates" || s == "portes") out = CoursesKind::Slalom;
    else if (s == "catch" || s == "rattrapage" || s == "drop") out = CoursesKind::Catch;
    else return false;
    return true;
}

bool workshopCourseMap(MapCategory c) {
    return c == MapCategory::Rings || c == MapCategory::Dribble || c == MapCategory::AirDribble;
}

void say(Actions& out, const std::string& text) { out.chat.push_back(ChatMessage{text, "RL Trainer"}); }

} // namespace

// ---- Static metadata ---------------------------------------------------------------------------------------
const char* CoursesModule::kindTitle(CoursesKind k) { return info(k).title; }
const char* CoursesModule::kindKey(CoursesKind k) { return info(k).key; }
const char* CoursesModule::kindSkill(CoursesKind k) { return info(k).skill; }
const char* CoursesModule::kindHelp(CoursesKind k) { return info(k).help; }
int CoursesModule::kindTier(CoursesKind k) { return info(k).tier; }
const char* CoursesModule::kindTierLabel(CoursesKind k) {
    switch (info(k).tier) {
    case 0: return "Débutant";
    case 1: return "Intermédiaire";
    default: return "Avancé";
    }
}
std::vector<CoursesKind> CoursesModule::kindsByTier() {
    std::vector<CoursesKind> out;
    for (int t = 0; t < 3; ++t)
        for (int i = 0; i < kCoursesKindCount; ++i)
            if (info(static_cast<CoursesKind>(i)).tier == t) out.push_back(static_cast<CoursesKind>(i));
    return out;
}
int CoursesModule::levelNumber(int difficultyPct) {
    return 1 + static_cast<int>(std::lround(std::clamp(difficultyPct, 0, 100) * 9 / 100.0));
}
bool CoursesModule::kindUsesCourse(CoursesKind k) {
    return k == CoursesKind::RingsAir || k == CoursesKind::RingsGround || k == CoursesKind::RingsAirDribble ||
           k == CoursesKind::Slalom;
}
bool CoursesModule::kindIsRings(CoursesKind k) {
    return k == CoursesKind::RingsAir || k == CoursesKind::RingsGround || k == CoursesKind::RingsAirDribble;
}

// ---- Construction / settings -------------------------------------------------------------------------------
CoursesModule::CoursesModule() : rng_(0xC0C0A5E5ULL, 0x2545F4914F6CDD1DULL) {
    for (auto& s : stairs_) s = Staircase(0.75f, 0.04f, 0.f);  // a new player starts at niveau 1
}

void CoursesModule::describeSettings(SettingsSchema& schema) {
    auto g = schema.section(&Settings::courses, "courses", "Parcours");
    g.toggle("rlt_co_enabled", &CoursesConfig::enabled, true, "Activer les parcours",
             "Parcours virtuels en freeplay : anneaux (vol, sol et murs, air dribble) et contrôle de balle.");
    g.choice("rlt_co_difficulty_mode", &CoursesConfig::difficultyMode, 0,
             {"Auto (s'adapte à toi)", "Facile", "Moyen", "Difficile", "Extrême", "Manuelle"}, "Difficulté",
             "Auto : monte quand tu réussis, baisse quand tu échoues (vise 75 % de réussite). Facile à Extrême : "
             "niveaux fixes façon maps d'anneaux classiques.");
    g.integer("rlt_co_difficulty", &CoursesConfig::manualDifficulty, 50, 0, 100, "Difficulté manuelle",
              "Utilisée quand la difficulté est sur Manuelle : 0 = très facile, 100 = extrême.");
    g.integer("rlt_co_ring_count", &CoursesConfig::ringCount, 0, 0, 30, "Nombre d'anneaux",
              "0 = selon la difficulté. Sinon nombre d'anneaux (ou de portes) des nouveaux parcours. Le code du "
              "parcours le retient.");
    g.number("rlt_co_countdown", &CoursesConfig::countdown, 2.f, 0.f, 5.f, "Compte à rebours (s)",
             "Temps pendant lequel la voiture est bloquée avant le départ.", 1);
    g.choice("rlt_co_chain", &CoursesConfig::chainMode, 0,
             {"Intelligent", "Toujours le même parcours", "Nouveau parcours après une réussite", "S'arrêter à la fin"},
             "Après un essai",
             "Intelligent : en difficulté Auto, nouveau parcours après une réussite et même parcours après un échec ; "
             "sinon le même parcours pour battre ton record.");

    auto r = schema.section(&Settings::courses, "courses", "Anneaux");
    r.choice("rlt_co_miss_policy", &CoursesConfig::missPolicy, 0,
             {"Pénalité de temps", "Revenir le prendre", "Échec immédiat"}, "Anneau raté",
             "Pénalité : l'anneau est compté raté, tu continues avec du temps en plus. Revenir : tu dois repasser "
             "dedans. Échec : l'essai s'arrête.");
    r.number("rlt_co_miss_penalty", &CoursesConfig::missPenalty, 2.f, 0.f, 10.f, "Pénalité par anneau raté (s)",
             "Secondes ajoutées à ton temps pour chaque anneau raté (mode Pénalité).", 1);
    r.integer("rlt_co_boost_start", &CoursesConfig::boostStart, 100, 0, 100, "Boost au départ",
              "Boost donné au départ de chaque essai.");
    r.toggle("rlt_co_unlimited_boost", &CoursesConfig::unlimitedBoost, false, "Boost illimité",
             "Le réservoir reste plein pendant tout le parcours.");
    r.toggle("rlt_co_boost_refills", &CoursesConfig::boostRefills, true, "Anneaux de recharge",
             "Certains anneaux (marqués BOOST) remettent ton boost à 100. Utile quand le boost est limité.");
    r.choice("rlt_co_ad_start", &CoursesConfig::airDribbleStart, 0, {"Balle sur le toit", "Balle lancée devant"},
             "Départ air dribble", "Position de la balle au départ des parcours d'air dribble.");
    r.toggle("rlt_co_park_ball", &CoursesConfig::parkBall, true, "Ranger la balle",
             "Pendant les anneaux en voiture, la balle est rangée en hauteur dans un coin. Coupe la caméra balle.");

    auto c = schema.section(&Settings::courses, "courses", "Contrôle");
    c.number("rlt_co_roof_tolerance", &CoursesConfig::roofTolerance, 0.3f, 0.1f, 1.5f, "Tolérance hors du toit (s)",
             "Dribble et slalom : temps pendant lequel la balle peut quitter le toit (petit rebond) avant d'être "
             "perdue.", 2);
    c.number("rlt_co_catch_hold", &CoursesConfig::catchHold, 1.f, 0.3f, 3.f, "Temps sur le toit pour valider (s)",
             "Rattrapage : la balle doit rester sur le toit ce temps-là, sans interruption.", 1);
    c.choice("rlt_co_catch_type", &CoursesConfig::catchType, 0, {"Mélange", "Chute du ciel", "Sortie de mur"},
             "Type de rattrapage", "D'où arrive la balle à rattraper.");
    c.toggle("rlt_co_catch_marker", &CoursesConfig::catchMarker, true, "Point de chute",
             "Affiche un cercle au sol là où la balle va arriver.");

    auto d = schema.section(&Settings::courses, "courses", "Affichage");
    d.color("rlt_co_next_color", &CoursesConfig::nextColor, Color{60, 255, 120, 255}, "Prochain anneau",
            "Couleur de l'anneau à passer maintenant (plus épais, avec son numéro).");
    d.color("rlt_co_ring_color", &CoursesConfig::ringColor, Color{255, 255, 255, 170}, "Anneaux suivants",
            "Couleur des anneaux d'après (de plus en plus transparents).");
    d.color("rlt_co_passed_color", &CoursesConfig::passedColor, Color{60, 200, 60, 110}, "Anneaux passés",
            "Couleur des anneaux déjà passés.");
    d.color("rlt_co_guide_color", &CoursesConfig::guideColor, Color{255, 220, 60, 200}, "Ligne guide",
            "Couleur de la ligne vers le prochain anneau et du point de chute.");
    d.number("rlt_co_line_width", &CoursesConfig::lineWidth, 3.f, 1.f, 8.f, "Épaisseur des anneaux",
             "Épaisseur des traits des anneaux (le prochain est 1,6 fois plus épais).", 1);
    d.integer("rlt_co_lookahead", &CoursesConfig::lookahead, 4, 0, 8, "Anneaux affichés à l'avance",
              "Nombre d'anneaux visibles après le prochain.");
    d.toggle("rlt_co_show_passed", &CoursesConfig::showPassed, true, "Afficher les anneaux passés",
             "Garde les derniers anneaux passés visibles (vert), les ratés en rouge.");
    d.toggle("rlt_co_guide_line", &CoursesConfig::guideLine, true, "Ligne guide",
             "Ligne pointillée de ta voiture (ou de la balle) vers le prochain anneau.");
    d.toggle("rlt_co_ring_numbers", &CoursesConfig::ringNumbers, true, "Numéros des anneaux",
             "Affiche le numéro des anneaux à venir.");
    d.toggle("rlt_co_hud", &CoursesConfig::hud, true, "Panneau d'infos",
             "Panneau en haut à gauche : chrono, anneau, écart avec ton record, boost.");

    auto a = schema.section(&Settings::courses, "courses", "Avancé");
    a.advanced();
    a.toggle("rlt_co_graze_counts", &CoursesConfig::grazeCounts, true, "Frôler le bord compte",
             "Un passage qui touche le bord de l'anneau est validé. Désactivé : il compte comme raté. Le slalom "
             "demande toujours un passage propre entre les poteaux.");
    a.number("rlt_co_timeout_factor", &CoursesConfig::timeoutFactor, 2.5f, 1.5f, 5.f, "Limite de temps",
             "Temps maximum d'un essai = ce facteur x le temps cible + 10 s.", 1);
    a.number("rlt_co_result_hold", &CoursesConfig::resultHold, 1.5f, 0.5f, 5.f, "Pause après un essai (s)",
             "Temps d'affichage du résultat avant l'essai suivant.", 1);
    a.number("rlt_co_hud_x", &CoursesConfig::hudX, 0.01f, 0.f, 1.f, "Panneau : position X",
             "Position horizontale du panneau (fraction de l'écran).", 3);
    a.number("rlt_co_hud_y", &CoursesConfig::hudY, 0.12f, 0.f, 1.f, "Panneau : position Y",
             "Position verticale du panneau (fraction de l'écran).", 3);
    a.number("rlt_co_hud_scale", &CoursesConfig::hudScale, 1.f, 0.5f, 2.f, "Panneau : taille",
             "Multiplie la taille du panneau, en plus de la taille générale du HUD.", 2);
}

void CoursesModule::onSettingsChanged(const Settings& settings) { cfg_ = settings.courses; }

courses::MissPolicy CoursesModule::missPolicy() const {
    switch (cfg_.missPolicy) {
    case 1: return courses::MissPolicy::ComeBack;
    case 2: return courses::MissPolicy::Strict;
    default: return courses::MissPolicy::Penalty;
    }
}

int CoursesModule::difficultyFor(CoursesKind k) const {
    switch (cfg_.difficultyMode) {
    case 1: return 15;
    case 2: return 40;
    case 3: return 65;
    case 4: return 90;
    case 5: return std::clamp(cfg_.manualDifficulty, 0, 100);
    default: {
        const float lvl = stairs_[static_cast<size_t>(k)].level();
        return std::clamp(static_cast<int>(std::lround(lvl * 100.f)), 0, 100);
    }
    }
}

float CoursesModule::timeLimit() const {
    switch (kind_) {
    case CoursesKind::Dribble: return 600.f;
    case CoursesKind::Catch: return catchPlan_.timeLimit + cfg_.catchHold + 0.5f;
    default: return static_cast<float>(course_.parTime) * cfg_.timeoutFactor + 10.f;
    }
}

void CoursesModule::flash(const std::string& text, Color c, float seconds) {
    flashText_ = text;
    flashColor_ = c;
    flashTicks_ = static_cast<int>(seconds * 120.f);
}

// ---- Lifecycle ---------------------------------------------------------------------------------------------
uint64_t CoursesModule::newSeed(const TickContext& ctx) {
    seedSalt_ = mix64(seedSalt_ + 0x9E3779B97F4A7C15ULL + static_cast<uint64_t>(ctx.snap.frame));
    uint64_t s = mix64(seedSalt_ ^ (static_cast<uint64_t>(rng_.next()) << 32) ^ rng_.next());
    s &= 0x00FFFFFFFFFFFFFFULL;
    if (s == 0) s = 1;
    // Explicit ring count chosen in the settings travels inside the seed (and so inside the course code).
    if (cfg_.ringCount > 0) s = courses::seedWithRingCount(s, cfg_.ringCount);
    return s;
}

bool CoursesModule::buildCourse(CoursesKind kind, uint64_t seed, int pct, bool fromCode) {
    courses::Course c = courses::generateCourse(seed, typeForKind(kind), pct);
    if (c.rings.empty()) return false;
    course_ = std::move(c);
    seed_ = seed;
    difficulty_ = course_.difficultyPct;
    fromCode_ = fromCode;
    code_ = courses::encodeCourseCode(seed, course_.type, difficulty_);
    // Ball parking spot (car-only courses): the top corner farthest from every ring and from the start.
    const Vec3 corners[4] = {{3300.f, 4400.f, 1940.f}, {-3300.f, 4400.f, 1940.f}, {3300.f, -4400.f, 1940.f},
                             {-3300.f, -4400.f, 1940.f}};
    float bestScore = -1.f;
    for (const Vec3& corner : corners) {
        float m = distance(corner, courses::toF(course_.start.carPos));
        for (const auto& r : course_.rings) m = std::min(m, distance(corner, courses::toF(r.center)));
        if (m > bestScore) {
            bestScore = m;
            parkPos_ = corner;
        }
    }
    return true;
}

void CoursesModule::prepareControl(const TickContext& ctx) {
    const float d = static_cast<float>(difficulty_) / 100.f;
    if (kind_ == CoursesKind::Dribble) {
        dribbleTarget_ = lerpf(8.f, 40.f, d);
        dribbleMinSpeed_ = lerpf(250.f, 1000.f, d);
        const float lane = static_cast<float>(rng_.rangeInt(-1500, 1500));
        dribbleCarPos_ = {lane, -3600.f, 17.f};
    } else if (kind_ == CoursesKind::Catch) {
        catchPlan_ = courses::planCatch(seed_, static_cast<double>(d), cfg_.catchType);
    }
    (void)ctx;
}

bool CoursesModule::refuseIdleShortcut(const TickContext& ctx, Actions& out) {
    // "restart" / "next" while idle are shortcuts (key binds, a stale tab button): they never stop another
    // module's drill. Choosing an exercise ("rings", "control", "play") still takes over on purpose.
    if (!ctx.otherActivityRunning) return false;
    say(out, "Un autre exercice est en cours : choisis un parcours dans la page Parcours pour le remplacer.");
    return true;
}

bool CoursesModule::acquireActivity(CoursesKind kind, const TickContext& ctx, Actions& out) {
    if (!cfg_.enabled) {
        say(out, "Le module Parcours est désactivé (page Parcours).");
        return false;
    }
    if (workshopCourseMap(ctx.game.category)) {
        say(out, "Tu es sur une map workshop de parcours : le module Parcours reste en pause, profite de la map !");
        return false;
    }
    const char* activity = kindIsRings(kind) ? "Anneaux" : "Contrôle";
    if (!ctx.worldControlAllowed || !requestActivity(activity)) {
        say(out, "Exercice indisponible ici : lance le freeplay (ou autorise les exercices sur cette map dans "
                 "Réglages > Avancé).");
        return false;
    }
    return true;
}

void CoursesModule::startExercise(CoursesKind kind, const std::string& codeText, const TickContext& ctx, Actions& out) {
    if (!cfg_.enabled || workshopCourseMap(ctx.game.category)) {
        acquireActivity(kind, ctx, out);  // says why
        return;
    }
    std::optional<courses::DecodedCode> decoded;
    if (!codeText.empty()) {
        decoded = courses::parseCourseCode(codeText);
        if (!decoded) {
            say(out, "Code de parcours invalide. Format attendu : RLT1-A050-XXXXXXXXXXXXX-X.");
            return;
        }
        kind = kindForType(decoded->type);
    }
    if (!acquireActivity(kind, ctx, out)) return;
    resetTransient();
    kind_ = kind;
    if (kindUsesCourse(kind)) {
        bool ok = false;
        if (decoded) {
            ok = buildCourse(kind, decoded->seed, decoded->difficultyPct, true);
        } else {
            for (int i = 0; i < 4 && !ok; ++i) ok = buildCourse(kind, newSeed(ctx), difficultyFor(kind), false);
        }
        if (!ok) {
            say(out, "Impossible de générer ce parcours. Essaie un autre code.");
            stopExercise(true);
            return;
        }
        if (PbEntry* pb = findPb(code_)) pb->lastUse = ++useSeq_;
        say(out, std::string(kindTitle(kind)) + " - niveau " + std::to_string(levelNumber(difficulty_)) + "/10 - code " +
                     code_);
    } else {
        fromCode_ = false;
        difficulty_ = difficultyFor(kind);
        seed_ = newSeed(ctx);
        prepareControl(ctx);
        say(out, std::string(kindTitle(kind)) + " - niveau " + std::to_string(levelNumber(difficulty_)) + "/10");
    }
    restartAttempt();
}

void CoursesModule::replayLast(const TickContext& ctx, Actions& out) {
    const CoursesKind kind = kind_;
    const bool haveCourse = kindUsesCourse(kind) && !course_.rings.empty() && !code_.empty();
    if (kindUsesCourse(kind) && !haveCourse) {
        // Nothing built yet (fresh start, or only the last code restored from the save): play that code.
        startExercise(kind, code_, ctx, out);
        return;
    }
    if (!acquireActivity(kind, ctx, out)) return;
    resetTransient();
    // Same course or drill, same origin: a generated course stays generated (the staircase keeps adapting and the
    // smart chain still moves on after a success), a course played from a code stays fixed.
    if (haveCourse) {
        if (PbEntry* pb = findPb(code_)) pb->lastUse = ++useSeq_;
        say(out, std::string(kindTitle(kind)) + " - même parcours - code " + code_);
    } else {
        prepareControl(ctx);  // same seed and difficulty: the catch replays the same ball
        say(out, std::string(kindTitle(kind)) + " - niveau " + std::to_string(levelNumber(difficulty_)) + "/10");
    }
    restartAttempt();
}

void CoursesModule::restartAttempt() {
    phase_ = CoursesPhase::Setup;
    phaseTicks_ = 0;
    countdownTotal_ = 0;
    lastCountdownMsg_ = 0;
    // The slalom always needs a clean pass between the posts: the straight lane line grazes every gate of the
    // easy courses (the weave there is small), so counting grazes would let a straight drive finish.
    const bool grazeCounts = cfg_.grazeCounts && kind_ != CoursesKind::Slalom;
    run_.reset(kindUsesCourse(kind_) ? &course_ : nullptr, missPolicy(), grazeCounts, cfg_.missPenalty);
    havePrev_ = false;
    prevT_ = 0.0;
    runTime_ = 0.f;
    lastBoost_ = -1.f;
    boostUsed_ = 0.f;
    hadBoost_ = false;
    boostEmptied_ = false;
    speedSum_ = 0.0;
    speedSamples_ = 0;
    teleports_ = 0;
    hasDelta_ = false;
    lastDelta_ = 0.f;
    keeper_.reset(cfg_.roofTolerance);
    targetReached_ = false;
    wasOnRoof_ = false;
    departRelVel_ = {};
    departRelPos_ = {};
    catchOnRoof_ = 0.f;
    touches_ = 0;
    haveFirstTouch_ = false;
    firstTouchFlat_ = 0.f;
    firstTouchJump_ = 0.f;
    firstTouchOffset_ = 0.f;
    ballOnRoofNow_ = false;
    flashTicks_ = 0;
}

void CoursesModule::nextAttempt(const TickContext& ctx, Actions& out) {
    if (kindUsesCourse(kind_)) {
        bool ok = false;
        for (int i = 0; i < 4 && !ok; ++i) ok = buildCourse(kind_, newSeed(ctx), difficultyFor(kind_), false);
        if (ok) say(out, "Nouveau parcours (niveau " + std::to_string(levelNumber(difficulty_)) + "/10) : " + code_);
    } else {
        fromCode_ = false;
        difficulty_ = difficultyFor(kind_);
        seed_ = newSeed(ctx);
        prepareControl(ctx);
    }
    restartAttempt();
}

void CoursesModule::resetTransient() {
    flashTicks_ = 0;
    havePrev_ = false;
    phaseTicks_ = 0;
    lastFrame_ = -1;
    resultHint_.clear();
    resultMedal_.clear();
}

void CoursesModule::stopExercise(bool release) {
    phase_ = CoursesPhase::Idle;
    resetTransient();
    if (release) releaseActivity();
}

void CoursesModule::onActivityStopped() {
    phase_ = CoursesPhase::Idle;
    resetTransient();
}

// ---- Events / commands -------------------------------------------------------------------------------------
void CoursesModule::onTouch(const TouchEvent& touch, const TickContext& ctx, Actions& out) {
    (void)ctx;
    (void)out;
    if (phase_ != CoursesPhase::Running) return;
    ++touches_;
    if (!haveFirstTouch_ && touch.pre.ball.valid && touch.pre.car.valid) {
        const CarState& car = touch.pre.car;
        const Vec3 rel = touch.pre.ball.vel - car.vel;
        haveFirstTouch_ = true;
        firstTouchFlat_ = std::sqrt(rel.x * rel.x + rel.y * rel.y);
        // Car moving up into the falling ball (a jump, or driving up a wall): closing speed the player added.
        firstTouchJump_ = std::max(0.f, car.vel.z);
        const Vec3 local = car.basis.toLocal(touch.pre.ball.pos - hitboxCenter(car));
        firstTouchOffset_ = std::sqrt(local.x * local.x + local.y * local.y);
    }
}

void CoursesModule::onGameEvent(const GameEvent& ev, const TickContext& ctx, Actions& out) {
    switch (ev.kind) {
    case GameEventKind::ContextChanged: {
        const bool changed = haveContext_ && (ctx.game.mapName != lastMapName_ || ctx.game.mode != lastMode_);
        lastMapName_ = ctx.game.mapName;
        lastMode_ = ctx.game.mode;
        haveContext_ = true;
        if (phase_ != CoursesPhase::Idle && (changed || workshopCourseMap(ctx.game.category) || !ctx.worldControlAllowed))
            stopExercise(true);
        break;
    }
    case GameEventKind::MapUnloaded:
        if (phase_ != CoursesPhase::Idle) stopExercise(true);
        break;
    case GameEventKind::ResetShot:
        if (phase_ != CoursesPhase::Idle) {
            restartAttempt();
            flash("On recommence !", colors::Cyan, 1.f);
        }
        break;
    case GameEventKind::CarSpawned:
        if (phase_ == CoursesPhase::Running) restartAttempt();
        break;
    case GameEventKind::Goal:
        if (phase_ == CoursesPhase::Running && kind_ != CoursesKind::RingsAir && kind_ != CoursesKind::RingsGround) {
            if (kind_ == CoursesKind::Dribble) endDribble(ctx, out);
            else if (kind_ == CoursesKind::Catch) endCatch(ctx, out, false, "la balle est entrée dans un but");
            else failRun(ctx, out, "la balle est entrée dans un but", "courses.ball_goal",
                         gateCourse() ? "Garde la balle dans le parcours : vise le centre des portes."
                                      : "Garde la balle dans le parcours : vise le centre des anneaux.");
        }
        break;
    default: break;
    }
}

void CoursesModule::onCommand(const Command& cmd, const TickContext& ctx, Actions& out) {
    auto arg = [&](size_t i) { return i < cmd.args.size() ? cmd.args[i] : std::string(); };
    const std::string name = lower(cmd.name);
    if (name == "rings" || name == "anneaux") {
        CoursesKind k = CoursesKind::RingsAir;
        std::string code;
        if (!cmd.args.empty()) {
            CoursesKind parsed = k;
            if (parseKind(arg(0), parsed) && kindIsRings(parsed)) {
                k = parsed;
                code = arg(1);
            } else if (courses::parseCourseCode(arg(0))) {
                code = arg(0);
            } else {
                say(out, "Type inconnu. Utilise : rings air, rings ground ou rings airdribble (+ code optionnel).");
                return;
            }
        }
        startExercise(k, code, ctx, out);
    } else if (name == "control" || name == "controle") {
        CoursesKind k = CoursesKind::Dribble;
        if (!cmd.args.empty()) {
            CoursesKind parsed = k;
            if (!parseKind(arg(0), parsed) || kindIsRings(parsed)) {
                say(out, "Type inconnu. Utilise : control dribble, control slalom ou control catch.");
                return;
            }
            k = parsed;
        }
        startExercise(k, k == CoursesKind::Slalom ? arg(1) : std::string(), ctx, out);
    } else if (name == "play" || (name == "code" && !cmd.args.empty())) {
        if (arg(0).empty()) {
            say(out, "Donne un code : play RLT1-A050-XXXXXXXXXXXXX-X.");
            return;
        }
        startExercise(CoursesKind::RingsAir, arg(0), ctx, out);
    } else if (name == "code") {
        if (code_.empty()) say(out, "Aucun parcours joué pour l'instant.");
        else say(out, "Code du parcours : " + code_ + " (bouton Copier dans la page Parcours).");
    } else if (name == "restart") {
        if (phase_ == CoursesPhase::Idle) {
            if (refuseIdleShortcut(ctx, out)) return;
            replayLast(ctx, out);  // the last course or control drill, keeping its origin (generated or code)
        } else {
            restartAttempt();
        }
    } else if (name == "next") {
        if (phase_ == CoursesPhase::Idle) {
            if (refuseIdleShortcut(ctx, out)) return;
            startExercise(kind_, std::string(), ctx, out);
        } else {
            nextAttempt(ctx, out);
        }
    } else if (name == "stop") {
        if (phase_ != CoursesPhase::Idle) {
            stopExercise(true);
            say(out, "Exercice arrêté.");
        }
    } else if (name == "clearpb") {
        const std::string target = arg(0).empty() ? code_ : courses::normalizeCourseCode(arg(0));
        const auto before = pbs_.size();
        pbs_.erase(std::remove_if(pbs_.begin(), pbs_.end(), [&](const PbEntry& e) { return e.code == target; }), pbs_.end());
        if (pbs_.size() != before) {
            say(out, "Record effacé pour " + target + ".");
            out.requestSave = true;
        }
    } else {
        say(out, "Commandes : rings <air|ground|airdribble> [code], control <dribble|slalom|catch>, play <code>, "
                 "restart, next, stop, code.");
    }
}

// ---- Personal bests ----------------------------------------------------------------------------------------
CoursesModule::PbEntry* CoursesModule::findPb(const std::string& code) {
    for (auto& e : pbs_)
        if (e.code == code) return &e;
    return nullptr;
}

const CoursesModule::PbEntry* CoursesModule::findPb(const std::string& code) const {
    for (const auto& e : pbs_)
        if (e.code == code) return &e;
    return nullptr;
}

void CoursesModule::trimPbs() {
    while (pbs_.size() > static_cast<size_t>(kMaxPbs)) {
        auto oldest = std::min_element(pbs_.begin(), pbs_.end(),
                                       [](const PbEntry& a, const PbEntry& b) { return a.lastUse < b.lastUse; });
        pbs_.erase(oldest);
    }
}

CoursesModule::PbEntry& CoursesModule::touchPb(const std::string& code, const std::string& date) {
    PbEntry* e = findPb(code);
    if (!e) {
        if (pbs_.size() >= static_cast<size_t>(kMaxPbs)) {
            auto oldest = std::min_element(pbs_.begin(), pbs_.end(),
                                           [](const PbEntry& a, const PbEntry& b) { return a.lastUse < b.lastUse; });
            pbs_.erase(oldest);
        }
        pbs_.push_back(PbEntry{});
        e = &pbs_.back();
        e->code = code;
    }
    e->lastUse = ++useSeq_;
    if (!date.empty()) e->date = date;
    return *e;
}

// ---- UI accessors ------------------------------------------------------------------------------------------
CoursesStatus CoursesModule::status() const {
    CoursesStatus s;
    s.active = phase_ != CoursesPhase::Idle;
    s.kind = kind_;
    s.phase = phase_;
    s.title = kindTitle(kind_);
    switch (phase_) {
    case CoursesPhase::Setup:
    case CoursesPhase::Countdown: s.phaseLabel = "Compte à rebours"; break;
    case CoursesPhase::Running: s.phaseLabel = "En cours"; break;
    case CoursesPhase::Result: s.phaseLabel = "Résultat"; break;
    default: s.phaseLabel = "Aucun exercice"; break;
    }
    s.difficulty = difficulty_;
    s.fromCode = fromCode_;
    if (kindUsesCourse(kind_)) {
        s.code = code_;
        s.next = run_.nextRing();
        s.total = static_cast<int>(course_.rings.size());
        s.misses = run_.misses();
        s.parTime = static_cast<float>(course_.parTime);
        s.estBoost = static_cast<float>(course_.estBoost);
        if (const PbEntry* pb = findPb(code_); pb && pb->time > 0.f) {
            s.hasPb = true;
            s.pbTime = pb->time;
        }
        s.time = phase_ == CoursesPhase::Result ? last_.time : runTime_ + static_cast<float>(run_.penaltyTime());
    } else {
        s.time = phase_ == CoursesPhase::Result ? last_.time : runTime_;
    }
    return s;
}

std::vector<CoursesPbView> CoursesModule::personalBests() const {
    std::vector<const PbEntry*> sorted;
    for (const auto& e : pbs_) sorted.push_back(&e);
    std::sort(sorted.begin(), sorted.end(), [](const PbEntry* a, const PbEntry* b) { return a->lastUse > b->lastUse; });
    std::vector<CoursesPbView> out;
    for (const PbEntry* e : sorted) {
        CoursesPbView v;
        v.code = e->code;
        if (auto dec = courses::decodeCourseCode(e->code)) {
            v.typeLabel = kindTitle(kindForType(dec->type));
            v.difficulty = dec->difficultyPct;
        }
        v.time = e->time;
        v.attempts = e->attempts;
        v.finishes = e->finishes;
        v.date = e->date;
        v.rings = static_cast<int>(e->splits.size());
        out.push_back(std::move(v));
    }
    return out;
}

std::vector<CoursesLevelView> CoursesModule::levels() const {
    std::vector<CoursesLevelView> out;
    for (int i = 0; i < kCoursesKindCount; ++i) {
        const auto k = static_cast<CoursesKind>(i);
        const Staircase& st = stairs_[static_cast<size_t>(i)];
        CoursesLevelView v;
        v.kind = k;
        v.title = kindTitle(k);
        v.level = std::clamp(static_cast<int>(std::lround(st.level() * 100.f)), 0, 100);
        v.levelNumber = levelNumber(v.level);
        v.attempts = attempts_[static_cast<size_t>(i)];
        v.successes = successes_[static_cast<size_t>(i)];
        v.recentRate = st.recentRate();
        out.push_back(std::move(v));
    }
    return out;
}

// ---- Persistence -------------------------------------------------------------------------------------------
namespace {

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    size_t start = 0;
    while (true) {
        const size_t p = s.find(sep, start);
        if (p == std::string::npos) {
            out.push_back(s.substr(start));
            break;
        }
        out.push_back(s.substr(start, p - start));
        start = p + 1;
        if (out.size() > 256) break;
    }
    return out;
}

bool parseFloatStrict(const std::string& s, float& out) {
    if (s.empty() || s.size() > 32) return false;
    char* end = nullptr;
    const double v = std::strtod(s.c_str(), &end);
    if (end != s.c_str() + s.size() || !std::isfinite(v)) return false;
    out = static_cast<float>(v);
    return true;
}

bool parseUnsigned(const std::string& s, unsigned long long& out) {
    if (s.empty() || s.size() > 20) return false;
    unsigned long long v = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return false;
        v = v * 10ULL + static_cast<unsigned long long>(c - '0');
    }
    out = v;
    return true;
}

std::string fmtFloat(float v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.3f", static_cast<double>(v));
    return buf;
}

} // namespace

void CoursesModule::save(Store& store) const {
    store.erasePrefix("courses.");
    store.setInt("courses.version", 1);
    store.setInt("courses.seq", static_cast<long long>(useSeq_));
    std::vector<std::string> list;
    for (const auto& e : pbs_) {
        // code|time|attempts|finishes|date|lastUse|s0;s1;...
        std::string line = e.code + "|" + fmtFloat(e.time) + "|" + std::to_string(e.attempts) + "|" +
                           std::to_string(e.finishes) + "|" + e.date + "|" + std::to_string(e.lastUse) + "|";
        for (size_t i = 0; i < e.splits.size() && i < static_cast<size_t>(kMaxSplits); ++i) {
            if (i) line += ";";
            line += fmtFloat(e.splits[i]);
        }
        list.push_back(std::move(line));
    }
    store.setList("courses.pb", list);
    for (int i = 0; i < kCoursesKindCount; ++i) {
        const std::string key = std::string("courses.stair.") + kindKey(static_cast<CoursesKind>(i));
        stairs_[static_cast<size_t>(i)].save(store, key);
        store.setInt(std::string("courses.stats.") + kindKey(static_cast<CoursesKind>(i)) + ".attempts",
                     attempts_[static_cast<size_t>(i)]);
        store.setInt(std::string("courses.stats.") + kindKey(static_cast<CoursesKind>(i)) + ".successes",
                     successes_[static_cast<size_t>(i)]);
    }
    store.setFloat("courses.dribble.best", dribbleBest_);
    store.setFloat("courses.catch.best", catchBest_);
    if (!code_.empty()) store.setString("courses.last_code", code_);
}

void CoursesModule::load(const Store& store) {
    pbs_.clear();
    const long long seq = store.getInt("courses.seq", 0);
    useSeq_ = seq > 0 ? static_cast<uint64_t>(seq) : 0;
    for (const std::string& line : store.getList("courses.pb")) {
        const auto f = split(line, '|');
        if (f.size() < 7) continue;
        PbEntry e;
        e.code = courses::normalizeCourseCode(f[0]);
        if (!courses::decodeCourseCode(e.code)) continue;
        if (!parseFloatStrict(f[1], e.time) || e.time < 0.f || e.time > 36000.f) continue;
        unsigned long long n = 0;
        if (parseUnsigned(f[2], n)) e.attempts = static_cast<int>(std::min<unsigned long long>(n, 1000000ULL));
        if (parseUnsigned(f[3], n)) e.finishes = static_cast<int>(std::min<unsigned long long>(n, 1000000ULL));
        e.date = f[4].substr(0, 16);
        if (parseUnsigned(f[5], n)) e.lastUse = n;
        if (!f[6].empty()) {
            for (const std::string& sp : split(f[6], ';')) {
                float v = 0.f;
                if (!parseFloatStrict(sp, v) || v < 0.f) {
                    e.splits.clear();
                    break;
                }
                if (e.splits.size() < static_cast<size_t>(kMaxSplits)) e.splits.push_back(v);
            }
        }
        if (findPb(e.code)) continue;
        useSeq_ = std::max(useSeq_, e.lastUse);
        pbs_.push_back(std::move(e));
    }
    trimPbs();
    for (int i = 0; i < kCoursesKindCount; ++i) {
        const std::string key = std::string("courses.stair.") + kindKey(static_cast<CoursesKind>(i));
        // Only a played level is kept: the previous version saved untried exercises at its old start value.
        if (store.getInt(key + ".attempts", 0) > 0) stairs_[static_cast<size_t>(i)].load(store, key);
        attempts_[static_cast<size_t>(i)] = static_cast<int>(std::clamp<long long>(
            store.getInt(std::string("courses.stats.") + kindKey(static_cast<CoursesKind>(i)) + ".attempts", 0), 0, 100000000));
        successes_[static_cast<size_t>(i)] = static_cast<int>(std::clamp<long long>(
            store.getInt(std::string("courses.stats.") + kindKey(static_cast<CoursesKind>(i)) + ".successes", 0), 0, 100000000));
    }
    dribbleBest_ = std::clamp(store.getFloat("courses.dribble.best", 0.f), 0.f, 36000.f);
    catchBest_ = std::clamp(store.getFloat("courses.catch.best", 0.f), 0.f, 600.f);
    const std::string last = courses::normalizeCourseCode(store.getString("courses.last_code"));
    if (phase_ == CoursesPhase::Idle && courses::decodeCourseCode(last)) code_ = last;
}


} // namespace rlt
