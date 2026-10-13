#include "core/modules/maps/MapsModule.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "core/common/Controls.h"
#include "core/common/Text.h"

namespace rlt {

namespace {
constexpr float kMinRunSeconds = 1.f;    // shorter runs are noise (car nudged after a reset)
constexpr size_t kMaxPaths = 500;
constexpr size_t kMaxPathText = 512;
constexpr float kLastRunShown = 12.f;    // s: "last run" line stays on the HUD this long

const char* kSectionTiming = "Chrono et splits";
const char* kSectionHud = "Affichage en jeu";
const char* kSectionFolders = "Dossiers de maps";

std::string bounded(const std::string& s, size_t n) { return s.size() > n ? s.substr(0, n) : s; }

bool validVarName(const std::string& s) {
    if (s.empty() || s.size() > 64) return false;
    return std::all_of(s.begin(), s.end(), [](char c) { return static_cast<unsigned char>(c) >= 32 && c != '='; });
}
} // namespace

const char* profileSourceLabel(ProfileSource s) {
    switch (s) {
    case ProfileSource::WorkshopId: return "profil connu (id workshop)";
    case ProfileSource::Signature: return "profil reconnu (variables de la map)";
    case ProfileSource::Pins: return "tes variables épinglées";
    case ProfileSource::Auto: return "compteur détecté automatiquement";
    case ProfileSource::None: break;
    }
    return "aucun profil";
}

std::string formatDeltaSeconds(float delta) {
    if (!std::isfinite(delta)) delta = 0.f;
    std::string n = formatNumber(std::fabs(delta), 2);
    const bool zero = n == "0.00";
    return (zero ? "" : (delta < 0.f ? "-" : "+")) + n + " s";
}

// ---- Settings ------------------------------------------------------------------------------------------
void MapsModule::describeSettings(SettingsSchema& schema) {
    using C = MapsConfig;
    {
        auto w = schema.section(&Settings::maps, "maps", kSectionTiming);
        w.toggle("rlt_map_enabled", &C::enabled, true, "Activer",
                 "Reconnaît les maps workshop, chronomètre tes runs (splits et records) et garde tes stats par map.");
        w.toggle("rlt_map_kismet", &C::readKismet, true, "Lire les variables de la map",
                 "Utilise les variables Kismet de la map (départ, niveau, chrono) pour des splits exacts. "
                 "Lecture seule : RL Trainer ne modifie jamais la map.");
        w.toggle("rlt_map_fallback", &C::fallbackTiming, true, "Chrono automatique",
                 "Sans profil connu, le chrono part quand tu bouges après un reset et s'arrête sur un but.");
        w.toggle("rlt_map_announce_pb", &C::announcePb, true, "Annoncer les records",
                 "Notification et message dans le chat quand tu bats ton record sur une map.");
        w.advanced();
        w.toggle("rlt_map_auto_level", &C::autoLevel, true, "Deviner le compteur de niveaux",
                 "Sur une map inconnue, une variable qui monte de 1 avec un nom comme Level ou Checkpoint sert de "
                 "splits.");
        w.number("rlt_map_start_speed", &C::startSpeed, 100.f, 20.f, 1000.f, "Vitesse de départ (uu/s)",
                 "Chrono automatique : le run démarre quand ta voiture dépasse cette vitesse après un reset.", 0);
        w.choice("rlt_map_fallback_finish", &C::fallbackFinish, 0, {"But uniquement", "But ou reset"},
                 "Fin d'un run (chrono auto)",
                 "But uniquement : un reset abandonne le run. But ou reset : un reset termine aussi le run "
                 "(maps où le reset marque la fin d'un essai).");
    }
    {
        auto w = schema.section(&Settings::maps, "maps", kSectionHud);
        w.toggle("rlt_map_hud", &C::hudEnabled, true, "Panneau sur les maps perso",
                 "Affiche le nom de la map, le chrono, le niveau, l'écart au record et ton record.");
        w.toggle("rlt_map_hud_splits", &C::hudSplits, true, "Écart au record",
                 "Montre l'écart à chaque split (vert en avance, rouge en retard) et le retard en direct.");
        w.toggle("rlt_map_hud_profile", &C::hudProfile, true, "Ligne du profil",
                 "Indique d'où vient le chrono (profil de la map ou chrono automatique).");
        w.number("rlt_map_hud_x", &C::hudX, 0.01f, 0.f, 1.f, "Position horizontale",
                 "Bord gauche du panneau, en fraction de la largeur de l'écran.", 3);
        w.number("rlt_map_hud_y", &C::hudY, 0.12f, 0.f, 1.f, "Position verticale",
                 "Haut du panneau, en fraction de la hauteur de l'écran.", 3);
        w.number("rlt_map_hud_scale", &C::hudScale, 1.f, 0.5f, 2.5f, "Taille du panneau",
                 "Multiplie la taille du texte du panneau.", 2);
        w.advanced();
        w.number("rlt_map_split_hold", &C::splitHold, 3.f, 0.5f, 10.f, "Durée d'un split à l'écran (s)",
                 "Temps pendant lequel l'écart d'un split reste affiché.", 1);
        w.color("rlt_map_color_ahead", &C::aheadColor, Color{80, 230, 100, 255}, "Couleur en avance",
                "Couleur des écarts négatifs (plus rapide que ton record).");
        w.color("rlt_map_color_behind", &C::behindColor, Color{255, 90, 80, 255}, "Couleur en retard",
                "Couleur des écarts positifs (plus lent que ton record).");
    }
    {
        auto w = schema.section(&Settings::maps, "maps", kSectionFolders);
        w.text("rlt_map_extra_folders", &C::extraFolders, "", "Dossiers en plus",
               "Dossiers où chercher des maps (.udk / .upk), séparés par des points-virgules. "
               "Exemple : D:\\Maps;E:\\RL\\workshop. Clique sur Rafraîchir après un changement.");
        w.advanced();
        w.toggle("rlt_map_scan_steam", &C::scanSteam, true, "Workshop Steam",
                 "Cherche les maps abonnées sur le workshop Steam (version Steam du jeu).");
        w.toggle("rlt_map_scan_mods", &C::scanMods, true, "Dossier mods",
                 "Cherche dans TAGame/CookedPCConsole/mods (Epic, Workshop Map Loader).");
        w.toggle("rlt_map_scan_wml", &C::scanWml, true, "Dossier de Workshop Map Loader",
                 "Lit le dossier configuré dans le plugin Workshop Map Loader, s'il est installé.");
    }
}

void MapsModule::onSettingsChanged(const Settings& settings) {
    const bool wasEnabled = cfg_.enabled;
    const bool hadKismet = cfg_.readKismet;
    cfg_ = settings.maps;
    accents_ = settings.general.hudAccents;
    RunOptions o;
    o.ownTiming = cfg_.fallbackTiming;
    o.startSpeed = cfg_.startSpeed;
    o.resetFinishes = cfg_.fallbackFinish == 1;
    run_.setOptions(o);

    Actions ignored;
    if (wasEnabled && !cfg_.enabled && custom_) {
        leaveMap(now_, ignored);
    } else if (!wasEnabled && cfg_.enabled && hasLastGame_) {
        enterContext(lastGame_, now_, ignored);
    } else if (custom_ && hadKismet != cfg_.readKismet) {
        profileVerified_ = false;
        resolveProfile(nullptr);
    }
}

// ---- Identity ------------------------------------------------------------------------------------------
std::string MapsModule::resolvedTitle() const {
    if (catalog_) return catalog_->title;
    if (!ctxTitle_.empty()) return ctxTitle_;
    if (const MapStats* st = book_.find(key_))
        if (!st->title.empty()) return st->title;
    return prettifyMapName(mapName_);
}

MapCategory MapsModule::resolvedCategory(std::string* source) const {
    return categoryFrom(book_.find(key_), ctxCategory_, ctxTitle_, mapName_, source);
}

MapCategory MapsModule::categoryFrom(const MapStats* st, MapCategory listed, const std::string& title,
                                     const std::string& mapName, std::string* source) const {
    auto set = [&](const char* s) {
        if (source) *source = s;
    };
    if (st && st->categoryOverride) {
        set("ton choix");
        return st->category;
    }
    if (catalog_) {
        set("catalogue");
        return catalog_->category;
    }
    if (run_.hasProfile() && run_.profile().category != MapCategory::Other &&
        (profileSource_ == ProfileSource::WorkshopId || profileSource_ == ProfileSource::Signature)) {
        set("variables de la map");
        return run_.profile().category;
    }
    if (listed != MapCategory::Standard && listed != MapCategory::Other) {
        set("liste des maps");
        return listed;
    }
    const MapCategory g = guessMapCategory(title + " " + mapName);
    if (g != MapCategory::Other) {
        set("nom de la map");
        return g;
    }
    set("inconnue");
    return MapCategory::Other;
}

// Only the last level the run timer really uses (the catalogue's count is folded into the profile by
// withCatalogFinal): showing "k/N" while no run can ever finish at N would be a lie.
int MapsModule::resolvedFinalLevel() const { return run_.finalLevel(); }

void MapsModule::refreshIdentity(const GameContext& game) {
    ctxTitle_ = bounded(game.mapTitle, 256);
    ctxCategory_ = game.category;
    if (!game.workshopId.empty()) workshopId_ = game.workshopId;
    catalog_ = MapCatalog::findById(workshopId_);
    if (!catalog_) catalog_ = MapCatalog::findByStem(mapName_);
    if (!catalog_ && !ctxTitle_.empty()) catalog_ = MapCatalog::findByStem(ctxTitle_);
    if (MapStats* st = book_.find(key_)) {
        st->title = resolvedTitle();
        if (!st->categoryOverride) st->category = resolvedCategory(nullptr);
        auto p = paths_.find(key_);
        if (p != paths_.end()) st->path = p->second;
    }
}

// ---- Map lifecycle -------------------------------------------------------------------------------------
void MapsModule::enterContext(const GameContext& game, float now, Actions& out) {
    if (!game.localDate.empty()) date_ = game.localDate;
    const bool custom = game.isCustomMap && game.mode != GameMode::Online && game.mode != GameMode::Replay &&
                        game.mode != GameMode::Menu;
    if (!custom) {
        if (custom_) leaveMap(now, out);
        switch (game.mode) {
        case GameMode::Freeplay:
        case GameMode::CustomTraining:
        case GameMode::OfflineMatch:
        case GameMode::Online:
        case GameMode::Replay: sessionKey_.clear(); break;
        default: break;  // menu / loading screens keep the session (map reload)
        }
        return;
    }
    const std::string key = mapStatsKey(game.workshopId, game.mapName);
    if (key.empty()) return;
    if (custom_ && key == key_) {
        refreshIdentity(game);
        return;
    }
    if (custom_ && game.mapName == mapName_ && isWorkshopId(key) && !isWorkshopId(key_)) {
        // Same map, its workshop id became known: move the stats from the stem key to the id key.
        MapStats moved;
        if (const MapStats* old = book_.find(key_)) moved = *old;
        book_.erase(key_);
        MapStats& dst = book_.get(key);
        dst.secondsPlayed += moved.secondsPlayed;
        dst.sessions += moved.sessions;  // the current session was counted under the old key only
        dst.attempts += moved.attempts;
        dst.finishes += moved.finishes;
        dst.touches += moved.touches;
        dst.goals += moved.goals;
        dst.maxLevel = std::max(dst.maxLevel, moved.maxLevel);
        if (moved.hasBest && (!dst.hasBest || moved.best < dst.best)) {
            dst.hasBest = true;
            dst.best = moved.best;
            dst.bestSplits = moved.bestSplits;
        }
        for (float r : moved.lastRuns) MapStatsBook::addRun(dst, r);
        if (!dst.hasPins() && moved.hasPins()) {
            dst.pinStart = moved.pinStart;
            dst.pinLevel = moved.pinLevel;
            dst.pinTimer = moved.pinTimer;
            dst.pinFinal = moved.pinFinal;
        }
        if (book_.isFavorite(key_)) {
            book_.setFavorite(key_, false);
            book_.setFavorite(key, true);
        }
        key_ = key;
        sessionKey_ = key;
        book_.markPlayed(key_, date_);
        refreshIdentity(game);
        syncPersonalBest();
        out.requestSave = true;
        return;
    }
    if (custom_) leaveMap(now, out);

    custom_ = true;
    key_ = key;
    workshopId_ = game.workshopId;
    mapName_ = bounded(game.mapName, 256);
    MapStats& st = book_.get(key_);
    book_.markPlayed(key_, date_);
    if (sessionKey_ != key_) {
        ++st.sessions;
        sessionKey_ = key_;
        sessionSeconds_ = 0.0;
    }
    refreshIdentity(game);

    watcher_.reset();
    kismetSeen_ = false;
    resolvedVarCount_ = 0;
    profileVerified_ = false;
    profileSource_ = ProfileSource::None;
    hasLastTick_ = false;
    movedSinceEnter_ = false;
    lastWasPb_ = false;
    hasLastPbDelta_ = false;
    run_.clear();
    resolveProfile(nullptr);  // pins or known id right away, checked against the variables later
    syncPersonalBest();
    run_.arm(now, true);
    out.requestSave = true;
}

void MapsModule::leaveMap(float now, Actions& out) {
    run_.onUnload(now);
    drain(now, out);
    run_.clear();
    watcher_.reset();
    profileSource_ = ProfileSource::None;
    profileVerified_ = false;
    kismetSeen_ = false;
    resolvedVarCount_ = 0;
    custom_ = false;
    key_.clear();
    workshopId_.clear();
    mapName_.clear();
    ctxTitle_.clear();
    ctxCategory_ = MapCategory::Standard;
    catalog_ = nullptr;
    hasLastTick_ = false;
    out.requestSave = true;
}

void MapsModule::syncPersonalBest() {
    const MapStats* st = book_.find(key_);
    if (st && st->hasBest) run_.setPersonalBest(st->bestSplits, st->best, true);
    else run_.setPersonalBest({}, 0.f, false);
}

// ---- Profiles ------------------------------------------------------------------------------------------
void MapsModule::applyProfile(const KismetProfile* p, ProfileSource src) {
    const bool same = (p == nullptr) == !run_.hasProfile() && src == profileSource_ &&
                      (!p || (p->key == run_.profile().key && p->startBool == run_.profile().startBool &&
                              p->levelInt == run_.profile().levelInt && p->counterInt == run_.profile().counterInt &&
                              p->timerString == run_.profile().timerString &&
                              p->finalLevel == run_.profile().finalLevel &&
                              p->finishOnWrap == run_.profile().finishOnWrap &&
                              p->finishOnGoal == run_.profile().finishOnGoal &&
                              p->finishOnFall == run_.profile().finishOnFall));
    if (same) return;
    run_.setProfile(p);
    profileSource_ = p ? src : ProfileSource::None;
    // Profile changed on the map: an own-timed run armed now is only record-eligible while the player has
    // not moved yet (otherwise it would start somewhere in the middle of the course).
    if (!run_.running()) run_.arm(now_, !movedSinceEnter_);
}

KismetProfile MapsModule::withCatalogFinal(const KismetProfile& p) const {
    KismetProfile c = p;
    if (!c.levelInt.empty() && c.finalLevel <= 0 && catalog_ && catalog_->levels > 0) {
        c.finalLevel = catalog_->levels;
        c.finishOnWrap = true;  // back to level 1 after the last level
        c.finishOnGoal = true;  // goal on the last level
    }
    return c;
}

void MapsModule::resolveProfile(const std::vector<KismetVar>* vars) {
    if (!custom_) return;
    if (!cfg_.readKismet) {
        applyProfile(nullptr, ProfileSource::None);
        profileVerified_ = true;
        return;
    }
    auto apply = [&](const KismetProfile* p, ProfileSource src) {
        if (!p) {
            applyProfile(nullptr, ProfileSource::None);
            return;
        }
        const KismetProfile c = withCatalogFinal(*p);
        applyProfile(&c, src);
    };
    const MapStats* st = book_.find(key_);
    if (st && st->hasPins()) {
        KismetProfile pin;
        pin.key = "pins";
        pin.label = "Tes variables";
        pin.category = resolvedCategory(nullptr);
        pin.startBool = st->pinStart;
        pin.levelInt = st->pinLevel;
        pin.timerString = st->pinTimer;
        const bool noLevel = st->pinLevel.empty();
        // A final level means nothing without a level variable: then the start variable going back to false
        // (or a goal) is the only possible end of the run.
        pin.finalLevel = noLevel ? 0 : st->pinFinal;
        pin.finishOnWrap = pin.finalLevel > 0;
        pin.finishOnGoal = pin.finalLevel > 0 || noLevel;
        pin.finishOnFall = noLevel;
        apply(&pin, ProfileSource::Pins);
        profileVerified_ = true;
        return;
    }
    const KismetProfile* byId = kismetProfileById(workshopId_);
    if (!byId && catalog_ && !catalog_->profile.empty()) byId = findKismetProfile(catalog_->profile);
    if (!vars) {
        apply(byId, ProfileSource::WorkshopId);
        profileVerified_ = false;
        return;
    }
    profileVerified_ = true;
    if (byId) {
        KismetProfile timingOnly = *byId;
        timingOnly.extraSignature.clear();
        if (kismetSignatureMatches(timingOnly, *vars)) {
            apply(byId, ProfileSource::WorkshopId);
            return;
        }
    }
    if (const KismetProfile* sig = kismetProfileBySignature(*vars)) {
        apply(sig, ProfileSource::Signature);
        return;
    }
    if (cfg_.autoLevel) {
        if (const KismetCandidate* c = watcher_.bestLevelCandidate(false)) {
            KismetProfile p;
            p.key = "auto";
            p.label = "Compteur " + c->name;
            p.levelInt = c->name;
            apply(&p, ProfileSource::Auto);
            return;
        }
    }
    applyProfile(nullptr, ProfileSource::None);
}

// ---- Callbacks -----------------------------------------------------------------------------------------
void MapsModule::onTick(const TickContext& ctx, Actions& out) {
    if (!cfg_.enabled || !custom_) return;
    const Snapshot& s = ctx.snap;
    now_ = s.time;
    if (hasLastTick_ && !s.paused) {
        const float dt = s.time - lastTickTime_;
        if (dt > 0.f && dt < 0.25f) {
            if (MapStats* st = book_.find(key_)) st->secondsPlayed += dt;
            sessionSeconds_ += dt;
        }
    }
    lastTickTime_ = s.time;
    hasLastTick_ = true;
    if (s.car.valid && !s.paused && speed(s.car) > cfg_.startSpeed) movedSinceEnter_ = true;
    run_.onTick(s);
    if (!run_.events().empty()) drain(s.time, out);
}

void MapsModule::onTouch(const TouchEvent& touch, const TickContext& ctx, Actions& out) {
    (void)ctx;
    (void)out;
    if (!cfg_.enabled || !custom_ || !touch.newContact) return;
    if (MapStats* st = book_.find(key_)) ++st->touches;
}

void MapsModule::onGameEvent(const GameEvent& ev, const TickContext& ctx, Actions& out) {
    const float now = ctx.hasSnapshot ? ctx.snap.time : now_;
    switch (ev.kind) {
    case GameEventKind::ContextChanged:
        lastGame_ = ctx.game;
        hasLastGame_ = true;
        if (cfg_.enabled) enterContext(ctx.game, now, out);
        break;
    case GameEventKind::MapUnloaded:
        if (custom_) leaveMap(now, out);
        break;
    case GameEventKind::ResetShot:
        if (!cfg_.enabled || !custom_) break;
        run_.onReset(now);
        drain(now, out);
        break;
    case GameEventKind::Goal:
        if (!cfg_.enabled || !custom_) break;
        if (MapStats* st = book_.find(key_)) ++st->goals;
        run_.onGoal(now);
        drain(now, out);
        break;
    default: break;
    }
}

void MapsModule::onKismet(const std::vector<KismetVar>& vars, const TickContext& ctx, Actions& out) {
    if (!cfg_.enabled || !custom_ || !cfg_.readKismet) return;
    const float now = ctx.hasSnapshot ? ctx.snap.time : now_;
    now_ = now;
    watcher_.update(vars, now);
    const bool first = !kismetSeen_;
    kismetSeen_ = true;
    const bool countChanged = watcher_.variableCount() != resolvedVarCount_;
    const bool soft = profileSource_ == ProfileSource::None || profileSource_ == ProfileSource::Signature ||
                      profileSource_ == ProfileSource::Auto;
    const bool autoCandidate = profileSource_ == ProfileSource::None && cfg_.autoLevel &&
                               watcher_.bestLevelCandidate(false) != nullptr;
    if (first || !profileVerified_ || (countChanged && soft) || autoCandidate) {
        resolveProfile(&vars);
        resolvedVarCount_ = watcher_.variableCount();
    }
    run_.onKismet(vars, now);
    if (!run_.events().empty()) drain(now, out);
}

// ---- Run events ----------------------------------------------------------------------------------------
void MapsModule::drain(float now, Actions& out) {
    std::vector<RunEvent> events = run_.events();
    run_.clearEvents();
    for (const RunEvent& e : events) {
        MapStats* st = book_.find(key_);
        switch (e.kind) {
        case RunEvent::Kind::Started:
            lastWasPb_ = false;
            hasLastPbDelta_ = false;
            break;
        case RunEvent::Kind::Split:
            if (st) st->maxLevel = std::max(st->maxLevel, e.level);
            break;
        case RunEvent::Kind::Finished:
            if (e.time < kMinRunSeconds) break;
            if (st) {
                ++st->attempts;
                ++st->finishes;
            }
            onRunFinished(now, out);
            break;
        case RunEvent::Kind::Aborted:
            if (st && e.time >= kMinRunSeconds) ++st->attempts;
            break;
        case RunEvent::Kind::Cancelled: break;
        }
    }
}

void MapsModule::onRunFinished(float now, Actions& out) {
    MapStats* st = book_.find(key_);
    if (!st) return;
    const RunSummary& r = run_.last();
    if (!r.valid || !r.finished) return;
    const bool hadPb = st->hasBest;
    const float prevPb = st->best;
    const std::vector<float> prevSplits = st->bestSplits;
    const bool isPb = r.recordEligible && !r.endedByReset && (!hadPb || r.time < prevPb - 1e-3f);
    MapStatsBook::addRun(*st, r.time);
    if (r.hasLevel) st->maxLevel = std::max(st->maxLevel, r.level);

    const std::string title = resolvedTitle();
    out.results.push_back(makeMapRunResult(key_, title, resolvedCategory(nullptr), r, hadPb, prevPb, prevSplits, now));

    lastWasPb_ = isPb;
    hasLastPbDelta_ = hadPb;
    lastPbDelta_ = r.time - prevPb;
    if (isPb) {
        st->hasBest = true;
        st->best = r.time;
        st->bestSplits.clear();
        // Splits of a run whose profile changed midway are shifted (the first checkpoints were missed):
        // a record without splits is better than deltas against the wrong segments.
        for (const auto& s : r.splits) {
            if (r.partialSplits || st->bestSplits.size() >= MapStatsBook::kMaxSplits) break;
            st->bestSplits.push_back(s.time);
        }
        syncPersonalBest();
        if (cfg_.announcePb) {
            if (hadPb) {
                const std::string text = title + " : " + formatTime(r.time, false) + " (" +
                                         formatDeltaSeconds(r.time - prevPb) + ")";
                toast(out, "Nouveau record !", text, ToastKind::Ok);
                ChatMessage m;
                m.text = "Nouveau record sur " + text;
                out.chat.push_back(m);
            } else {
                toast(out, "Premier chrono", title + " : " + formatTime(r.time, false), ToastKind::Info);
            }
        }
    }
    out.requestSave = true;
}

AttemptResult makeMapRunResult(const std::string& key, const std::string& title, MapCategory category,
                               const RunSummary& run, bool hadPb, float prevPb, const std::vector<float>& prevPbSplits,
                               float gameTime) {
    AttemptResult r;
    r.module = "maps";
    r.skill = "map." + key;
    r.title = title.empty() ? key : title;
    r.success = run.finished;
    r.time = gameTime;
    r.valueForRecord = run.time;
    r.lowerIsBetter = true;
    // A run ended by a reset ("goal or reset" mode) counts as an attempt, never as a record: the shortest
    // attempt the player gave up on would otherwise become one.
    const bool recordable = run.finished && run.recordEligible && !run.endedByReset;
    r.hasRecordValue = recordable;

    const bool isPb = recordable && (!hadPb || run.time < prevPb - 1e-3f);
    float score = 0.f;
    if (!run.finished) score = 0.f;
    else if (isPb) score = hadPb ? 100.f : 80.f;
    else if (hadPb && prevPb > 0.f) score = clampf(100.f - (run.time / prevPb - 1.f) * 250.f, 0.f, 99.f);
    else score = 70.f;
    if (!run.recordEligible || run.endedByReset) score = std::min(score, 60.f);
    r.score = score;
    r.verdict = verdictFromScore(score);

    auto metric = [&](const char* k, const char* label, float v, const char* unit, bool hasIdeal, float ideal) {
        Metric m;
        m.key = k;
        m.label = label;
        m.value = v;
        m.unit = unit;
        m.hasIdeal = hasIdeal;
        m.ideal = ideal;
        r.metrics.push_back(m);
    };
    metric("time", "Temps", run.time, "s", hadPb, prevPb);
    if (hadPb) metric("pb_delta", "Écart au record", run.time - prevPb, "s", true, 0.f);
    if (run.hasLevel) metric("level", "Niveau atteint", static_cast<float>(run.level), "", false, 0.f);
    if (!run.splits.empty()) metric("splits", "Splits", static_cast<float>(run.splits.size()), "", false, 0.f);
    if (run.hasDeaths) metric("deaths", "Morts", static_cast<float>(run.deaths), "", true, 0.f);
    if (run.resets > 0) metric("resets", "Resets", static_cast<float>(run.resets), "", true, 0.f);

    // Tips: biggest time loss on one segment vs the record, deaths / resets, then a category reminder.
    char buf[200];
    if (hadPb && !run.partialSplits && !run.splits.empty() && !prevPbSplits.empty()) {
        float worst = 0.f;
        int worstIdx = 0;
        const size_t n = std::min(run.splits.size(), prevPbSplits.size());
        float prevT = 0.f, prevPbT = 0.f;
        for (size_t i = 0; i < n; ++i) {
            const float loss = (run.splits[i].time - prevT) - (prevPbSplits[i] - prevPbT);
            if (loss > worst) {
                worst = loss;
                worstIdx = static_cast<int>(i) + 1;
            }
            prevT = run.splits[i].time;
            prevPbT = prevPbSplits[i];
        }
        if (run.splits.size() == prevPbSplits.size()) {
            const float loss = (run.time - prevT) - (prevPb - prevPbT);
            if (loss > worst) {
                worst = loss;
                worstIdx = static_cast<int>(n) + 1;
            }
        }
        if (worst > 0.5f) {
            std::snprintf(buf, sizeof(buf), "Le segment %d t'a coûté %s sur ton record : rejoue-le en priorité.",
                          worstIdx, formatDeltaSeconds(worst).c_str());
            r.tips.push_back({"maps.slow_segment", buf, worst});
        }
    }
    if (run.hasDeaths && run.deaths >= 3) {
        std::snprintf(buf, sizeof(buf),
                      "%d morts sur ce run : sécurise les passages durs avant de chercher la vitesse.", run.deaths);
        r.tips.push_back({"maps.deaths", buf, 0.5f * static_cast<float>(run.deaths)});
    } else if (!run.hasDeaths && run.resets >= 3) {
        std::snprintf(buf, sizeof(buf), "%d resets sur ce run : vise la régularité avant la vitesse.", run.resets);
        r.tips.push_back({"maps.resets", buf, 0.4f * static_cast<float>(run.resets)});
    }
    if (hadPb && prevPb > 0.f && run.time > prevPb * 1.1f) {
        switch (category) {
        case MapCategory::Rings:
            r.tips.push_back(
                {"maps.rings.boost",
                 "Garde ta vitesse : de petites pressions de boost entre les anneaux plutôt que des freinages.", 1.f});
            break;
        case MapCategory::Obstacle:
            r.tips.push_back(
                {"maps.obstacle.line", "Prends des trajectoires plus directes et prépare chaque saut en avance.", 1.f});
            break;
        case MapCategory::Dribble:
            r.tips.push_back({"maps.dribble.control",
                              "Garde la balle au centre du toit avec de petites corrections de direction.", 1.f});
            break;
        case MapCategory::AirDribble:
            r.tips.push_back(
                {"maps.airdribble.touch", "Pousse la balle avec le nez par petites touches, sans la dépasser.", 1.f});
            break;
        case MapCategory::Shooting:
            r.tips.push_back({"maps.shooting.setup", "Prends le temps de bien te placer avant chaque tir.", 1.f});
            break;
        default:
            r.tips.push_back(
                {"maps.consistency", "Reprends les trajectoires de ton record pour gagner en régularité.", 1.f});
            break;
        }
    }
    if (run.finished && run.endedByReset) {
        r.tips.push_back({"maps.reset_end",
                          "Run terminé par un reset : il compte dans tes stats, pas pour le record. Finis sur un but "
                          "pour viser le record.",
                          0.1f});
    } else if (run.finished && !run.recordEligible) {
        r.tips.push_back({"maps.not_eligible",
                          run.cleanStart ? "Run hors record : un niveau a été sauté ou rejoué."
                                         : "Run hors record : le chrono n'a pas vu ton départ. Fais un reset pour un "
                                           "run complet.",
                          0.1f});
    }
    std::stable_sort(r.tips.begin(), r.tips.end(), [](const Tip& a, const Tip& b) { return a.weight > b.weight; });
    if (r.tips.size() > 3) r.tips.resize(3);
    return r;
}

// ---- Commands ------------------------------------------------------------------------------------------
void MapsModule::toast(Actions& out, const std::string& title, const std::string& text, ToastKind kind) const {
    ToastMessage t;
    t.title = title;
    t.text = text;
    t.kind = kind;
    out.toasts.push_back(t);
}

std::string MapsModule::targetKey(const Command& cmd, size_t argIndex) const {
    if (cmd.args.size() > argIndex && !cmd.args[argIndex].empty()) {
        const std::string& a = cmd.args[argIndex];
        return isWorkshopId(a) ? a : mapStatsKey(a, a);
    }
    return key_;
}

void MapsModule::onCommand(const Command& cmd, const TickContext& ctx, Actions& out) {
    const float now = ctx.hasSnapshot ? ctx.snap.time : now_;
    const std::string& n = cmd.name;
    auto arg = [&](size_t i) -> std::string { return i < cmd.args.size() ? cmd.args[i] : std::string(); };
    auto needMap = [&](const std::string& key) {
        if (!key.empty()) return true;
        toast(out, "Maps", "Aucune map perso chargée : précise l'id de la map.", ToastKind::Warning);
        return false;
    };

    if (n == "fav" || n == "unfav" || n == "togglefav") {
        const std::string key = targetKey(cmd, 0);
        if (!needMap(key)) return;
        const bool on = n == "fav" || (n == "togglefav" && !book_.isFavorite(key));
        if (book_.setFavorite(key, on)) out.requestSave = true;
        return;
    }
    if (n == "played") {
        const std::string key = targetKey(cmd, 0);
        if (key.empty() || cmd.args.empty()) return;
        MapStats& st = book_.get(key);
        const std::string title = arg(1);
        if (!title.empty() && (st.title.empty() || key != key_)) st.title = bounded(title, 256);
        MapCategory c;
        if (!st.categoryOverride && parseMapCategory(arg(2), c)) st.category = c;
        const std::string path = arg(3);
        if (!path.empty()) {
            st.path = bounded(path, kMaxPathText);
            if (paths_.size() < kMaxPaths || paths_.count(key)) paths_[key] = st.path;
        }
        book_.markPlayed(key, date_);
        out.requestSave = true;
        return;
    }
    if (n == "register") {
        for (size_t i = 0; i + 1 < cmd.args.size(); i += 2) {
            const std::string key = targetKey(cmd, i);
            if (key.empty() || cmd.args[i + 1].empty()) continue;
            if (paths_.size() >= kMaxPaths && !paths_.count(key)) break;
            paths_[key] = bounded(cmd.args[i + 1], kMaxPathText);
        }
        return;
    }
    if (n == "load") {
        const std::string key = targetKey(cmd, 0);
        if (cmd.args.empty() || key.empty()) {
            toast(out, "Maps", "Usage : rlt maps load <id ou nom de la map>.", ToastKind::Warning);
            return;
        }
        std::string path;
        if (auto it = paths_.find(key); it != paths_.end()) path = it->second;
        else if (const MapStats* st = book_.find(key)) path = st->path;
        if (path.empty()) {
            const std::string want = stemKey(cmd.args[0]);
            for (const auto& kv : paths_)
                if (stemKey(kv.second) == want || stemKey(kv.first) == want) path = kv.second;
        }
        if (path.empty() || path.find('"') != std::string::npos) {
            toast(out, "Map introuvable",
                  "Ouvre l'onglet Maps (F2) pour scanner tes maps, puis relance la commande.", ToastKind::Warning);
            return;
        }
        out.consoleCommands.push_back("load_workshop \"" + path + "\"");
        return;
    }
    if (n == "reload") {
        out.consoleCommands.push_back("unreal:servertravel ?");
        return;
    }
    if (n == "menu") {
        out.consoleCommands.push_back("unreal:start MENU_Main_p?close");
        return;
    }
    if (n == "freeplay") {
        out.consoleCommands.push_back("load_freeplay");
        return;
    }
    if (n == "pin") {
        const std::string role = toLowerAscii(arg(0));
        const std::string value = arg(1);
        const std::string key = targetKey(cmd, 2);
        if (!needMap(key)) return;
        MapStats& st = book_.get(key);
        if (role == "final") {
            int f = 0;
            if (!parseIntValue(value, f) || f < 0 || f > 10000) {
                toast(out, "Maps", "Niveau final invalide (0 à 10000).", ToastKind::Warning);
                return;
            }
            st.pinFinal = f;
        } else if (role == "start" || role == "level" || role == "timer") {
            if (!validVarName(value)) {
                toast(out, "Maps", "Nom de variable invalide.", ToastKind::Warning);
                return;
            }
            (role == "start" ? st.pinStart : role == "level" ? st.pinLevel : st.pinTimer) = value;
        } else {
            toast(out, "Maps", "Usage : rlt maps pin <start|level|timer|final> <variable ou nombre>.",
                  ToastKind::Warning);
            return;
        }
        if (key == key_) {
            profileVerified_ = false;
            resolveProfile(nullptr);
        }
        toast(out, "Variable épinglée", role + " = " + value, ToastKind::Ok);
        out.requestSave = true;
        return;
    }
    if (n == "unpin") {
        const std::string role = toLowerAscii(arg(0));
        const bool all = role.empty() || role == "all";
        const std::string key = targetKey(cmd, all && !role.empty() ? 1 : (all ? 0 : 1));
        if (!needMap(key)) return;
        MapStats* st = book_.find(key);
        if (!st) return;
        if (all || role == "start") st->pinStart.clear();
        if (all || role == "level") st->pinLevel.clear();
        if (all || role == "timer") st->pinTimer.clear();
        if (all || role == "final") st->pinFinal = 0;
        if (key == key_) {
            profileVerified_ = false;
            resolveProfile(nullptr);
        }
        out.requestSave = true;
        return;
    }
    if (n == "category") {
        const std::string key = targetKey(cmd, 1);
        if (!needMap(key)) return;
        MapStats& st = book_.get(key);
        MapCategory c;
        if (toLowerAscii(arg(0)) == "auto") {
            st.categoryOverride = false;
            if (key == key_) st.category = resolvedCategory(nullptr);
        } else if (parseMapCategory(arg(0), c)) {
            st.category = c;
            st.categoryOverride = true;
        } else {
            toast(out, "Maps", "Catégorie inconnue (rings, dribble, airdribble, shooting, obstacle, other, auto).",
                  ToastKind::Warning);
            return;
        }
        out.requestSave = true;
        return;
    }
    if (n == "reset_pb") {
        const std::string key = targetKey(cmd, 0);
        if (!needMap(key)) return;
        if (MapStats* st = book_.find(key)) {
            st->hasBest = false;
            st->best = 0.f;
            st->bestSplits.clear();
            if (key == key_) syncPersonalBest();
            toast(out, "Record effacé", st->title.empty() ? key : st->title, ToastKind::Info);
            out.requestSave = true;
        }
        return;
    }
    if (n == "forget") {
        const std::string key = targetKey(cmd, 0);
        if (!needMap(key)) return;
        book_.erase(key);
        book_.setFavorite(key, false);
        paths_.erase(key);
        if (key == key_ && custom_) {
            book_.get(key_);
            book_.markPlayed(key_, date_);
            refreshIdentity(lastGame_);
            syncPersonalBest();
        }
        out.requestSave = true;
        return;
    }
    if (n == "restart") {
        if (!custom_) return;
        run_.abort(now);
        drain(now, out);
        return;
    }
    if (n == "help") {
        ChatMessage m;
        m.text =
            "rlt maps : fav/unfav <id>, played <id>, load <id>, reload, menu, freeplay, pin <start|level|timer|final> "
            "<valeur>, unpin, category <cat|auto>, reset_pb, forget, restart";
        out.chat.push_back(m);
        return;
    }
}

// ---- Drawing -------------------------------------------------------------------------------------------
DrawPanel MapsModule::buildHud(const ViewContext& view, const DrawContext& ctx, float now) const {
    const Color white = colors::White;
    const Color grey = colors::Grey;
    auto txt = [&](const std::string& s) { return hudText(s, accents_); };
    DrawPanel p;
    p.pos = {cfg_.hudX * view.screenW, cfg_.hudY * view.screenH};
    p.scale = cfg_.hudScale * ctx.hudScale;
    p.minWidth = 210.f * p.scale;
    p.title = txt(resolvedTitle());
    auto line = [&](const std::string& s, Color c, float bar = -1.f, float scale = 1.f) {
        PanelLine l;
        l.text = txt(s);
        l.color = c;
        l.bar = bar;
        l.scale = scale;
        p.lines.push_back(std::move(l));
    };

    // Timer / state
    if (run_.running()) {
        line("Chrono " + formatTime(run_.elapsed(now), false), white, -1.f, 1.15f);
    } else if (run_.phase() == RunPhase::Armed) {
        line("Prêt : le chrono part quand tu bouges", grey);
    } else if (run_.hasProfile() && run_.profile().hasKismetStart()) {
        line("En attente du départ de la map", grey);
    } else if (run_.ownStartActive()) {
        line(actionPrompt(ctx.profile, GameAction::ResetShot, keyStyleFromIndex(ctx.settings.profile.buttonStyle)) +
                 " pour relancer un run",
             grey);
    }

    // Level k/N
    if (run_.hasLevel()) {
        const int fin = resolvedFinalLevel();
        const bool rings = run_.hasProfile() && !run_.profile().counterInt.empty();
        std::string s = std::string(rings ? "Anneaux " : "Niveau ") + std::to_string(run_.level());
        float bar = -1.f;
        if (fin > 0) {
            s += "/" + std::to_string(fin);
            bar = clamp01(static_cast<float>(run_.level()) / static_cast<float>(fin));
        }
        line(s, white, bar);
    }

    // Split delta vs personal best
    if (cfg_.hudSplits && run_.running()) {
        const auto& sp = run_.splits();
        const bool fresh = !sp.empty() && run_.lastSplitAt() >= 0.f && now - run_.lastSplitAt() <= cfg_.splitHold;
        float live = 0.f;
        if (fresh && sp.back().hasDelta) {
            line("Split " + std::to_string(sp.back().index) + " : " + formatDeltaSeconds(sp.back().delta),
                 sp.back().delta <= 0.f ? cfg_.aheadColor : cfg_.behindColor);
        } else if (run_.liveDelta(now, live)) {
            line("Retard : " + formatDeltaSeconds(live), cfg_.behindColor);
        } else if (fresh) {
            line("Split " + std::to_string(sp.back().index) + " : " + formatTime(sp.back().time, false), white);
        }
    }

    // Last run (shortly after it ended)
    const RunSummary& last = run_.last();
    if (!run_.running() && last.valid && last.time >= kMinRunSeconds && now - last.endedAt <= kLastRunShown &&
        now >= last.endedAt) {
        if (last.finished && last.endedByReset) {
            line("Run fini sur reset : " + formatTime(last.time, false), grey);
        } else if (last.finished) {
            std::string s = "Dernier run : " + formatTime(last.time, false);
            Color c = white;
            if (lastWasPb_) {
                s += " (record !)";
                c = cfg_.aheadColor;
            } else if (hasLastPbDelta_) {
                s += " (" + formatDeltaSeconds(lastPbDelta_) + ")";
                c = lastPbDelta_ <= 0.f ? cfg_.aheadColor : cfg_.behindColor;
            }
            line(s, c);
        } else {
            line("Run arrêté à " + formatTime(last.time, false), grey);
        }
    }

    // Personal best
    const MapStats* st = book_.find(key_);
    if (st && st->hasBest) line("Record : " + formatTime(st->best, false), Color{255, 215, 90, 255});
    else line("Record : aucun pour l'instant", grey);

    if (run_.hasMapTimer() && !run_.mapTimer().empty()) line("Chrono de la map : " + run_.mapTimer(), grey);
    if (run_.running() && run_.hasDeaths()) line("Morts : " + std::to_string(run_.deathsInRun()), grey);

    if (cfg_.hudProfile) {
        std::string s;
        if (run_.hasProfile()) s = "Profil : " + run_.profile().label;
        else if (cfg_.fallbackTiming) s = "Chrono auto (map sans profil)";
        else s = "Chrono désactivé pour cette map";
        line(s, grey, -1.f, 0.8f);
    }
    return p;
}

void MapsModule::draw(const ViewContext& view, const DrawContext& ctx, DrawList& out) {
    if (!cfg_.enabled || !cfg_.hudEnabled || !custom_ || !ctx.game.isCustomMap) return;
    if (ctx.game.mode == GameMode::Replay || ctx.game.mode == GameMode::Online) return;
    const float now = ctx.hasSnapshot ? ctx.snap.time : now_;
    out.panel(buildHud(view, ctx, now));
}

// ---- Persistence ---------------------------------------------------------------------------------------
void MapsModule::save(Store& store) const { book_.save(store); }

void MapsModule::load(const Store& store) {
    book_.load(store);
    if (custom_) {
        book_.get(key_);
        syncPersonalBest();
        profileVerified_ = false;
        resolveProfile(nullptr);
    }
}

// ---- UI accessors --------------------------------------------------------------------------------------
MapsLiveStatus MapsModule::status() const {
    MapsLiveStatus s;
    s.enabled = cfg_.enabled;
    s.onCustomMap = custom_;
    if (!custom_) return s;
    s.key = key_;
    s.workshopId = workshopId_;
    s.mapName = mapName_;
    s.title = resolvedTitle();
    s.category = resolvedCategory(&s.categorySource);
    if (catalog_) {
        s.inCatalog = true;
        s.author = catalog_->author;
        s.trains = catalog_->trains;
        s.catalogLevels = catalog_->levels;
    }
    s.profileSource = profileSource_;
    if (run_.hasProfile()) {
        s.profileKey = run_.profile().key;
        s.profileLabel = run_.profile().label;
    }
    s.kismetSeen = kismetSeen_;
    s.kismetVars = static_cast<int>(watcher_.variableCount());
    s.phase = run_.phase();
    s.runSource = run_.source();
    s.ownStart = run_.ownStartActive();
    s.runTime = run_.elapsed(now_);
    s.hasLevel = run_.hasLevel();
    s.level = run_.level();
    s.finalLevel = resolvedFinalLevel();
    s.hasMapTimer = run_.hasMapTimer();
    s.mapTimer = run_.mapTimer();
    s.hasDeaths = run_.hasDeaths();
    s.deaths = run_.deathsInRun();
    s.splits = run_.splits();
    s.hasLiveDelta = run_.liveDelta(now_, s.liveDelta);
    s.last = run_.last();
    if (s.last.time < kMinRunSeconds) s.last.valid = false;  // a nudge after a reset is not a run
    s.lastWasPb = lastWasPb_;
    s.sessionSeconds = sessionSeconds_;
    s.hints = mapCategoryHints(s.category);
    if (const MapStats* st = book_.find(key_)) {
        s.categoryOverride = st->categoryOverride;
        s.hasPb = st->hasBest;
        s.pb = st->best;
        s.pbSplits = st->bestSplits;
        s.pinStart = st->pinStart;
        s.pinLevel = st->pinLevel;
        s.pinTimer = st->pinTimer;
        s.pinFinal = st->pinFinal;
    }
    return s;
}

namespace {
MapStatsView toView(const MapStats& s, bool favorite) {
    MapStatsView v;
    v.key = s.key;
    v.title = s.title;
    v.path = s.path;
    v.lastDate = s.lastDate;
    v.category = s.category;
    v.categoryOverride = s.categoryOverride;
    v.secondsPlayed = s.secondsPlayed;
    v.sessions = s.sessions;
    v.attempts = s.attempts;
    v.finishes = s.finishes;
    v.touches = s.touches;
    v.goals = s.goals;
    v.maxLevel = s.maxLevel;
    v.hasBest = s.hasBest;
    v.best = s.best;
    v.lastRuns = s.lastRuns;
    v.lastSeq = s.lastSeq;
    v.favorite = favorite;
    return v;
}
} // namespace

std::vector<MapStatsView> MapsModule::statsList() const {
    std::vector<MapStatsView> out;
    for (const MapStats* s : book_.byRecent()) out.push_back(toView(*s, book_.isFavorite(s->key)));
    return out;
}

bool MapsModule::statsFor(const std::string& key, MapStatsView& out) const {
    const MapStats* s = book_.find(key);
    if (!s) return false;
    out = toView(*s, book_.isFavorite(key));
    return true;
}

std::map<std::string, MapCategory> MapsModule::categoryOverrides() const {
    std::map<std::string, MapCategory> out;
    for (const MapStats* s : book_.byRecent())
        if (s->categoryOverride) out[s->key] = s->category;
    return out;
}

// Runs every 0.25 s on the game thread (glue context refresh, under the engine lock), possibly before any map
// was seen: lookups only, never creates a stats entry nor touches the module state. `ctx` comes fresh from the
// glue each time; reading it (not the engine's previous, already refined context) keeps a removed choice from
// sticking.
void MapsModule::refineContext(GameContext& ctx) const {
    if (!ctx.isCustomMap) return;
    // Same file as the current map but the glue lost its workshop id: keep the id key (no stats split).
    if (custom_ && ctx.workshopId.empty() && isWorkshopId(workshopId_) && bounded(ctx.mapName, 256) == mapName_)
        ctx.workshopId = workshopId_;
    const std::string key = mapStatsKey(ctx.workshopId, ctx.mapName);
    if (key.empty()) return;
    const MapStats* st = book_.find(key);
    if (custom_ && key == key_) {
        if (ctx.mapTitle.empty() || catalog_) ctx.mapTitle = resolvedTitle();
        ctx.category = categoryFrom(st, ctx.category, ctx.mapTitle, ctx.mapName, nullptr);
        return;
    }
    if (!st) return;
    if (ctx.mapTitle.empty() && !st->title.empty()) ctx.mapTitle = st->title;
    if (st->categoryOverride) ctx.category = st->category;
}

} // namespace rlt
