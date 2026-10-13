#include "core/modules/coach/CoachModule.h"

#include <algorithm>
#include <cmath>

#include "core/common/Text.h"
#include "core/modules/coach/CoachCatalog.h"
#include "core/modules/coach/CoachGoals.h"
#include "core/modules/coach/CoachUtil.h"

namespace rlt {

namespace {

constexpr Color kGold{255, 200, 60, 255};
constexpr Color kTipColor{215, 230, 255, 255};
constexpr Color kGoalColor{120, 220, 255, 255};

Color verdictColor(Verdict v) {
    switch (v) {
    case Verdict::Perfect: return Color(40, 230, 90);
    case Verdict::Good: return Color(160, 235, 90);
    case Verdict::Ok: return Color(255, 215, 0);
    case Verdict::Fail: return Color(255, 75, 60);
    default: return colors::White;
    }
}

// Index of the most important tip (highest weight, first on ties), -1 when none has text.
int topTipIndex(const std::vector<Tip>& tips) {
    int best = -1;
    for (size_t i = 0; i < tips.size(); ++i) {
        if (tips[i].text.empty()) continue;
        if (best < 0 || tips[i].weight > tips[static_cast<size_t>(best)].weight) best = static_cast<int>(i);
    }
    return best;
}

// Exercises that train a whole family: a focus goal on them counts every skill of the family.
std::string focusScope(const std::string& skill) {
    const std::string fam = coach::skillFamily(skill);
    if (fam == "first_touch" || fam == "kickoff") return fam;
    if (skill.rfind("shoot.flick", 0) == 0) return "shoot.flick";
    if (skill.rfind("shoot.target", 0) == 0) return "shoot.target";
    return skill;
}

} // namespace

// ---- Settings ------------------------------------------------------------------------------------------
void CoachModule::describeSettings(SettingsSchema& schema) {
    auto g = schema.section(&Settings::coach, "coach", "Coach & progression");
    g.toggle("rlt_coach_enabled", &CoachConfig::enabled, true, "Activer le coach",
             "Enregistre chaque essai noté (stats, records, séries) et affiche ta carte de résultat.");

    auto c = schema.section(&Settings::coach, "coach", "Carte après chaque essai");
    c.toggle("rlt_coach_card", &CoachConfig::cardEnabled, true, "Afficher la carte",
             "Après chaque essai noté : verdict, note, mesures clés et le conseil le plus utile.");
    c.number("rlt_coach_card_seconds", &CoachConfig::cardSeconds, 4.f, 1.f, 15.f, "Durée d'affichage (s)",
             "Temps pendant lequel la carte reste à l'écran. Elle s'efface en douceur à la fin.", 1);
    c.integer("rlt_coach_card_metrics", &CoachConfig::cardMetrics, 2, 0, 3, "Mesures affichées",
              "Nombre de mesures sur la carte, au format « mesuré (idéal) ».");
    c.toggle("rlt_coach_card_tip", &CoachConfig::cardTip, true, "Afficher le conseil",
             "Ajoute le conseil le plus important de l'essai. En orange s'il revient souvent.");
    c.number("rlt_coach_card_x", &CoachConfig::cardX, 0.99f, 0.f, 1.f, "Position horizontale",
             "Bord droit de la carte, en fraction de la largeur de l'écran (0,99 = tout à droite).", 2);
    c.number("rlt_coach_card_y", &CoachConfig::cardY, 0.30f, 0.f, 1.f, "Position verticale",
             "Haut de la carte, en fraction de la hauteur de l'écran.", 2);
    c.number("rlt_coach_card_scale", &CoachConfig::cardScale, 1.f, 0.5f, 2.5f, "Taille de la carte",
             "Multiplie la taille de la carte (en plus de la taille générale de l'interface).", 2);

    auto f = schema.section(&Settings::coach, "coach", "Feedback dégressif");
    f.toggle(
        "rlt_coach_fading", &CoachConfig::feedbackFading, true, "Feedback dégressif",
        "Quand tu réussis plus de 80 % des 10 derniers essais d'une compétence, la carte n'apparaît plus que "
        "pour les ratés et une réussite sur trois. Moins d'aide = plus d'autonomie (et un meilleur apprentissage).");
    f.advanced();
    f.number("rlt_coach_fading_rate", &CoachConfig::fadingRate, 0.8f, 0.5f, 1.f, "Seuil de réussite",
             "Taux de réussite (sur les 10 derniers essais) au-dessus duquel la carte devient discrète.", 2);
    f.integer(
        "rlt_coach_fading_every", &CoachConfig::fadingEvery, 3, 1, 10, "Une réussite affichée sur",
        "Pendant le feedback dégressif, montre la carte toutes les N réussites (les ratés sont toujours montrés).");

    auto r = schema.section(&Settings::coach, "coach", "Records");
    r.toggle("rlt_coach_pb_toast", &CoachConfig::recordToast, true, "Notification de record",
             "Une notification quand tu bats un record personnel.");
    r.toggle("rlt_coach_pb_chat", &CoachConfig::recordChat, true, "Record dans le chat",
             "Écrit aussi le nouveau record dans le chat.");
    r.advanced();
    r.toggle("rlt_coach_pb_all", &CoachConfig::recordAnnounceAll, false, "Annoncer aussi les records des exercices",
             "Mécas mouvement, Kickoffs, Parcours, Tirs & flicks et Maps annoncent déjà leurs records : le coach "
             "ne les répète pas (ils restent sur sa carte et dans ses stats). Coche pour les annoncer quand même.");

    auto s = schema.section(&Settings::coach, "coach", "Session et série");
    s.toggle("rlt_coach_summary_chat", &CoachConfig::summaryChat, true, "Résumé de session dans le chat",
             "En quittant la map : essais, moyenne, records, point fort et point à travailler.");
    s.integer("rlt_coach_streak_min", &CoachConfig::streakMinAttempts, 10, 1, 200, "Essais par jour pour la série",
              "Nombre d'essais notés dans la journée pour prolonger ta série de jours d'entraînement.");
    s.toggle("rlt_coach_goal_toast", &CoachConfig::goalToasts, true, "Notifications d'objectifs",
             "Une notification quand un objectif de session est atteint ou que ta série du jour est validée.");
    s.integer("rlt_coach_break_minutes", &CoachConfig::breakMinutes, 25, 0, 90, "Rappel de pause (min)",
              "Propose une petite pause après ce temps d'entraînement dans la session (0 = jamais). "
              "Des pauses régulières aident à mémoriser ce que tu viens de travailler.");
    s.advanced();
    s.integer("rlt_coach_summary_min", &CoachConfig::summaryMinAttempts, 3, 1, 100, "Essais mini pour le résumé",
              "Pas de résumé dans le chat pour une session plus courte.");

    auto p = schema.section(&Settings::coach, "coach", "Plan conseillé");
    p.integer("rlt_coach_plan_size", &CoachConfig::planSize, 3, 1, 5, "Exercices conseillés",
              "Nombre d'exercices proposés dans le plan conseillé.");
    p.advanced();
    p.number("rlt_coach_plan_spacing", &CoachConfig::spacingWeight, 0.08f, 0.f, 0.3f, "Poids de l'espacement",
             "Priorité ajoutée par jour sans pratique (pratique espacée, plafonnée à 10 jours).", 2);
    p.number("rlt_coach_plan_mistakes", &CoachConfig::mistakeWeight, 0.4f, 0.f, 1.5f, "Poids des erreurs récurrentes",
             "Priorité ajoutée quand la même erreur revient souvent.", 2);
    p.number("rlt_coach_trend_threshold", &CoachConfig::trendThreshold, 0.5f, 0.1f, 3.f, "Seuil de tendance",
             "Pente (points par essai, sur les 20 derniers) à partir de laquelle une compétence est « en progrès » "
             "ou « en baisse ».",
             2);
}

void CoachModule::onSettingsChanged(const Settings& settings) {
    cfg_ = settings.coach;
    refreshDerived();
}

// ---- Callbacks -----------------------------------------------------------------------------------------
void CoachModule::onTick(const TickContext& ctx, Actions& out) {
    if (!cfg_.enabled || !ctx.hasSnapshot) return;
    const int64_t f = ctx.snap.frame;
    if (session_.active() && lastTickFrame_ >= 0 && f > lastTickFrame_ && !ctx.snap.paused && !ctx.game.paused) {
        // Frame gaps (pause, loading, teleports of the clock) count at most a quarter second.
        session_.addTime(std::min(static_cast<float>(f - lastTickFrame_) * kTickDt, 0.25f));
        // Spaced practice: suggest a short break every `breakMinutes` of session.
        if (cfg_.breakMinutes > 0 &&
            session_.seconds() >= static_cast<float>((breaksShown_ + 1) * cfg_.breakMinutes) * 60.f) {
            ++breaksShown_;
            toast(out, "Petite pause ?",
                  std::to_string(breaksShown_ * cfg_.breakMinutes) +
                      " minutes d'entraînement : souffle 5 minutes, ça aide ton cerveau à mémoriser.",
                  ToastKind::Info);
        }
    }
    lastTickFrame_ = f;
    if (std::isfinite(ctx.snap.time)) lastTime_ = ctx.snap.time;
}

void CoachModule::onGameEvent(const GameEvent& ev, const TickContext& ctx, Actions& out) {
    switch (ev.kind) {
    case GameEventKind::ContextChanged: {
        setToday(ctx.game.localDate);
        const bool mapChanged = contextSeen_ && (ctx.game.mode != lastMode_ || ctx.game.mapName != lastMap_);
        contextSeen_ = true;
        lastMode_ = ctx.game.mode;
        lastMap_ = ctx.game.mapName;
        if (mapChanged) {
            card_ = FeedbackCard{};
            lastTickFrame_ = -1;
            lastTime_ = 0.f;
            closeSession(out);  // usually already closed by MapUnloaded
        }
        refreshDerived();  // the date may have changed (spacing, streak)
        break;
    }
    case GameEventKind::MapUnloaded:
        card_ = FeedbackCard{};
        lastTickFrame_ = -1;
        lastTime_ = 0.f;
        closeSession(out);
        break;
    default: break;
    }
}

void CoachModule::onResult(const AttemptResult& raw, const TickContext& ctx, Actions& out) {
    if (!cfg_.enabled) return;
    setToday(ctx.game.localDate);
    float now = lastTime_;
    if (ctx.hasSnapshot && std::isfinite(ctx.snap.time)) now = ctx.snap.time;
    lastTime_ = now;

    std::string skill = coach::sanitizeId(raw.skill, 64);
    if (skill.empty()) skill = coach::sanitizeId(raw.module, 32);
    if (skill.empty()) skill = "autre";
    AttemptResult r = raw;
    r.score = std::isfinite(raw.score) ? std::clamp(raw.score, 0.f, 100.f) : 0.f;
    if (r.verdict == Verdict::None) r.verdict = verdictFromScore(r.score);

    if (!session_.active()) startSession();

    SkillTracker& t = tracker(skill);
    int prevCount = 0;
    const float prevRate = t.recentSuccessRate(kFadingWindow, &prevCount);
    const SkillTracker::RecordUpdate rec = t.add(r, today_, session_.index());
    ++totalAttempts_;
    const int dayBefore = days_.attemptsOn(today_);
    days_.add(today_, r.success, r.score);
    session_.add(skill, t.title(), r.success, r.score, rec.newRecord);

    // Personal record.
    if (rec.newRecord) {
        std::string value, before;
        t.formatRecordPair(rec.value, rec.previous, value, before);
        // Exercises that celebrate their own records are not repeated (the card still shows the record line).
        const bool announce = cfg_.recordAnnounceAll || !coach::moduleAnnouncesRecords(coach::sanitizeId(r.module));
        if (announce && cfg_.recordToast)
            toast(out, "Nouveau record !", t.title() + " : " + value + " (avant " + before + ")", ToastKind::Ok);
        if (announce && cfg_.recordChat)
            say(out, "Nouveau record : " + t.title() + " " + value + " (avant " + before + ")");
        out.requestSave = true;
    } else if (rec.firstRecord) {
        out.requestSave = true;
    }

    // Daily streak validated today.
    const int need = std::max(1, cfg_.streakMinAttempts);
    if (coach::validDate(today_) && dayBefore < need && days_.attemptsOn(today_) >= need) {
        const int streak = days_.streak(today_, need);
        bestStreak_ = std::max(bestStreak_, streak);
        if (cfg_.goalToasts)
            toast(out, streak > 1 ? "Série : " + std::to_string(streak) + " jours !" : std::string("Série lancée !"),
                  "Tes " + std::to_string(need) + " essais du jour sont faits. Reviens demain pour la prolonger.",
                  ToastKind::Ok);
        out.requestSave = true;
    }

    // Session goals.
    std::string goalLine;
    bool completedNow = false;
    for (auto& g : goals_) {
        const bool wasDone = g.done;
        if (!coach::applyResultToGoal(g, skill, r.score, r.success, rec.newRecord)) continue;
        if (g.done && !wasDone) {
            completedNow = true;
            goalLine = "Objectif atteint : " + g.label;
            if (cfg_.goalToasts) toast(out, "Objectif atteint !", g.label, ToastKind::Ok);
        } else if (goalLine.empty()) {
            goalLine = "Objectif " + std::to_string(g.progress) + "/" + std::to_string(g.target) + " : " + g.label;
        }
    }
    if (completedNow && goals_.size() > 1 &&
        std::all_of(goals_.begin(), goals_.end(), [](const Goal& g) { return g.done; }) && cfg_.goalToasts)
        toast(out, "Session réussie !", "Tous tes objectifs de session sont atteints. Bravo !", ToastKind::Ok);

    // Feedback card with fading.
    coach::FadingRule rule;
    rule.enabled = cfg_.feedbackFading;
    rule.rate = cfg_.fadingRate;
    rule.window = kFadingWindow;
    rule.every = cfg_.fadingEvery;
    const bool show = coach::shouldShowCard(rule, prevCount, prevRate, r.success, rec.newRecord, t.hiddenSuccesses());
    if (cfg_.cardEnabled && show) buildCard(r, t, rec, now, goalLine);
    else card_ = FeedbackCard{};  // never leave the card of an older attempt on screen

    refreshDerived();
    if (++unsavedResults_ >= kSaveEveryResults) {
        unsavedResults_ = 0;
        out.requestSave = true;
    }
}

void CoachModule::onCommand(const Command& cmd, const TickContext& ctx, Actions& out) {
    if (setToday(ctx.game.localDate)) refreshDerived();
    const std::string& n = cmd.name;
    if (n == "new_session") {
        closeSession(out);
        startSession();
        // No degree-like sign here: chat text is folded to ASCII by default ("n°" would read "n deg").
        say(out, "Session " + std::to_string(session_.index()) + " lancée. Bon entraînement !");
    } else if (n == "reset_all") {
        resetProgress();
        out.requestSave = true;
        say(out, "Progression du coach réinitialisée : stats, records, séries et historique effacés.");
    } else if (n == "goal_add") {
        if (cmd.args.empty()) {
            std::string ids;
            for (const auto& p : coach::goalPresets()) ids += std::string(ids.empty() ? "" : ", ") + p.id;
            say(out, "Objectifs disponibles : focus, " + ids + ", ou custom <type> <compétence> <nombre>.");
            return;
        }
        Goal g;
        std::string error;
        const std::string& id = cmd.args[0];
        if (id == "focus") {
            g = focusGoal();
        } else if (id == "custom") {
            const std::vector<std::string> rest(cmd.args.begin() + 1, cmd.args.end());
            const std::string scope =
                rest.size() >= 2 && rest[1] != "any" && rest[1] != "*" ? coach::sanitizeId(rest[1]) : "";
            if (!coach::parseCustomGoal(rest, g, error, scope.empty() ? std::string() : titleFor(scope))) {
                say(out, error);
                return;
            }
        } else if (!coach::makePresetGoal(id, g)) {
            say(out, "Objectif inconnu : " + id + ". Tape \"rlt coach goal_add\" pour la liste.");
            return;
        }
        addGoal(g, out);
    } else if (n == "goal_remove") {
        int idx = 0;
        if (cmd.args.empty() || !parseIntValue(cmd.args[0], idx) || idx < 1 || idx > static_cast<int>(goals_.size())) {
            say(out, "Numéro d'objectif invalide (1 à " + std::to_string(goals_.size()) + ").");
            return;
        }
        goals_.erase(goals_.begin() + (idx - 1));
        out.requestSave = true;
    } else if (n == "goal_clear") {
        goals_.clear();
        out.requestSave = true;
    } else if (n == "plan") {
        for (const auto& line : planLines()) say(out, line);
    } else if (n == "summary") {
        const std::string s = currentSessionSummary();
        say(out, s.empty() ? std::string("Pas encore d'essai noté dans cette session.") : s);
    } else if (n == "preview") {
        // Sample card at the configured position, to place it from the settings.
        card_ = FeedbackCard{};
        card_.active = true;
        card_.startTime = ctx.hasSnapshot ? ctx.snap.time : lastTime_;
        card_.title = "Aperçu : Speedflip";
        card_.verdict = Verdict::Good;
        card_.score = 82.f;
        card_.success = true;
        card_.metricLines = {"Temps au ballon : 2.05 s (idéal 1.95 s)", "Angle du flip : 38° (idéal 30°)"};
        card_.tip = "Annule le flip un peu plus tôt pour garder ta vitesse.";
        card_.tipLines = coach::wrapText(card_.tip, 44);
        card_.goalLine = "Objectif 7/10 : 10 speedflips réussis";
    } else {
        say(out,
            "Commande coach inconnue : " + n +
                ". Commandes : new_session, reset_all, goal_add, goal_remove, goal_clear, plan, summary, preview.");
    }
}

// ---- Drawing -------------------------------------------------------------------------------------------
float CoachModule::cardAlpha(float now) const {
    if (!card_.active || !std::isfinite(now)) return 0.f;
    const float duration = std::max(0.5f, cfg_.cardSeconds);
    float elapsed = now - card_.startTime;
    if (elapsed < -0.5f) return 0.f;  // the clock went back (new map): stale card
    if (elapsed < 0.f) elapsed = 0.f;
    if (elapsed >= duration) return 0.f;
    const float fade = std::min(0.6f, duration * 0.3f);
    const float left = duration - elapsed;
    return left >= fade ? 1.f : left / fade;
}

void CoachModule::draw(const ViewContext& view, const DrawContext& ctx, DrawList& out) {
    if (!cfg_.enabled || !cfg_.cardEnabled || !card_.active || !ctx.hasSnapshot) return;
    const float alpha = cardAlpha(ctx.snap.time);
    if (alpha <= 0.f) return;
    const float sw = view.screenW > 0.f ? view.screenW : 1920.f;
    const float sh = view.screenH > 0.f ? view.screenH : 1080.f;
    const float scale = std::max(0.3f, cfg_.cardScale * (ctx.hudScale > 0.f ? ctx.hudScale : 1.f));
    auto fadeColor = [alpha](const Color& c) { return c.withAlpha(c.a * alpha); };

    DrawPanel p;
    p.pos = Vec2(std::clamp(cfg_.cardX, 0.f, 1.f) * sw, std::clamp(cfg_.cardY, 0.f, 1.f) * sh);
    p.anchorX = Align::Right;
    p.title = card_.title;
    p.titleColor = fadeColor(kGold);
    p.background = Color(0, 0, 0, 160.f * alpha);
    p.scale = scale;
    p.minWidth = 230.f * scale;

    const Color vc = verdictColor(card_.verdict);
    PanelLine verdict;
    verdict.text = std::string(verdictLabel(card_.verdict)) + " : " + coach::formatScore(card_.score) + "/100";
    verdict.color = fadeColor(vc);
    verdict.bar = std::clamp(card_.score / 100.f, 0.f, 1.f);
    verdict.barColor = fadeColor(vc);
    verdict.scale = 1.15f;
    p.lines.push_back(std::move(verdict));

    if (card_.newRecord && !card_.recordLine.empty()) {
        PanelLine l;
        l.text = card_.recordLine;
        l.color = fadeColor(kGold);
        p.lines.push_back(std::move(l));
    }
    for (const auto& m : card_.metricLines) {
        PanelLine l;
        l.text = m;
        l.color = fadeColor(Color(235, 235, 235));
        l.scale = 0.9f;
        p.lines.push_back(std::move(l));
    }
    if (!card_.tip.empty()) {
        for (const auto& w : card_.tipLines) {
            PanelLine l;
            l.text = w;
            l.color = fadeColor(card_.tipRecurring ? colors::Orange : kTipColor);
            l.scale = 0.9f;
            p.lines.push_back(std::move(l));
        }
        if (card_.tipRecurring && !card_.recurringNote.empty()) {
            PanelLine l;
            l.text = card_.recurringNote;
            l.color = fadeColor(colors::Grey);
            l.scale = 0.8f;
            p.lines.push_back(std::move(l));
        }
    }
    if (!card_.goalLine.empty()) {
        PanelLine l;
        l.text = card_.goalLine;
        l.color = fadeColor(kGoalColor);
        l.scale = 0.85f;
        p.lines.push_back(std::move(l));
    }
    out.panel(std::move(p));
}

void CoachModule::buildCard(const AttemptResult& r, const SkillTracker& t, const SkillTracker::RecordUpdate& rec,
                            float now, const std::string& goalLine) {
    card_ = FeedbackCard{};
    card_.active = true;
    card_.startTime = now;
    // Header: the attempt headline from the producer ("Kickoff (diagonale gauche)", "Flick : raté"), the stable
    // skill name when it gave none.
    card_.title = r.title.empty() ? t.title() : coach::sanitizeField(r.title, 60);
    card_.verdict = r.verdict;
    card_.score = r.score;
    card_.success = r.success;
    const size_t maxMetrics = static_cast<size_t>(std::clamp(cfg_.cardMetrics, 0, 3));
    for (const auto& m : r.metrics) {
        if (card_.metricLines.size() >= maxMetrics) break;
        if (!std::isfinite(m.value)) continue;
        const std::string label = m.label.empty() ? m.key : m.label;
        const bool zeroIdeal = m.hasIdeal && std::fabs(m.ideal) < 1e-6f;
        std::string line = label + " : " + coach::formatValue(m.value, m.unit, zeroIdeal);
        if (m.hasIdeal && std::isfinite(m.ideal)) line += " (idéal " + coach::formatValue(m.ideal, m.unit) + ")";
        card_.metricLines.push_back(coach::sanitizeField(line, 90));
    }
    if (cfg_.cardTip) {
        const int ti = topTipIndex(r.tips);
        if (ti >= 0) {
            const Tip& tip = r.tips[static_cast<size_t>(ti)];
            card_.tip = coach::sanitizeField(tip.text, 160);
            card_.tipLines = coach::wrapText(card_.tip, 44);
            const std::string code = coach::sanitizeId(tip.code, 48);
            const int seen = code.empty() ? 0 : t.codeCount(code, 5);
            if (seen >= 3) {
                card_.tipRecurring = true;
                card_.recurringNote = "Erreur fréquente : " + std::to_string(seen) + " des 5 derniers essais";
            }
        }
    }
    if (rec.newRecord) {
        card_.newRecord = true;
        std::string value, before;
        t.formatRecordPair(rec.value, rec.previous, value, before);
        card_.recordLine = "Nouveau record ! " + value + " (avant " + before + ")";
    }
    card_.goalLine = goalLine;
}

// ---- Sessions, goals, plan -----------------------------------------------------------------------------
bool CoachModule::setToday(const std::string& date) {
    if (!coach::validDate(date) || date == today_) return false;
    today_ = date;
    return true;
}

void CoachModule::startSession() {
    ++sessionCount_;
    session_.start(sessionCount_, today_);
    lastTickFrame_ = -1;
    breaksShown_ = 0;
    for (auto& g : goals_) {
        g.progress = 0;
        g.done = false;
    }
}

void CoachModule::closeSession(Actions& out) {
    if (!session_.active()) return;
    if (session_.attempts() > 0) {
        const SessionRecord rec = session_.toRecord(3);
        history_.push_back(rec);
        if (history_.size() > kMaxSessionHistory)
            history_.erase(history_.begin(),
                           history_.begin() + static_cast<long>(history_.size() - kMaxSessionHistory));
        ++revision_;
        if (cfg_.summaryChat && rec.attempts >= std::max(1, cfg_.summaryMinAttempts))
            say(out, sessionSummaryText(rec, days_.streak(today_, cfg_.streakMinAttempts), goalsDone(), goalsTotal()));
        out.requestSave = true;
    } else if (session_.index() == sessionCount_ && sessionCount_ > 0) {
        --sessionCount_;  // empty session: give its number back
    }
    session_.stop();
}

std::string CoachModule::currentSessionSummary() const {
    if (!session_.active() || session_.attempts() == 0) return std::string();
    return sessionSummaryText(session_.toRecord(3), streak_, goalsDone(), goalsTotal());
}

coach::PlanWeights CoachModule::weights() const {
    coach::PlanWeights w;
    w.spacing = std::max(0.f, cfg_.spacingWeight);
    w.mistake = std::max(0.f, cfg_.mistakeWeight);
    return w;
}

void CoachModule::refreshDerived() {
    views_.clear();
    views_.reserve(skills_.size());
    std::vector<coach::PlanCandidate> candidates;
    candidates.reserve(skills_.size());
    for (const auto& kv : skills_) {
        const SkillTracker& t = kv.second;
        views_.push_back(makeView(kv.first, t));
        const SkillView& v = views_.back();
        if (v.attempts == 0 || !v.launchable) continue;
        coach::PlanCandidate c;
        c.skill = v.id;
        c.title = v.title;
        c.attempts = v.attempts;
        c.recentAverage = v.averageRecent;
        c.recentSuccess = v.recentSuccess;
        c.daysSince = v.daysSince;
        c.mistake = v.mainMistake;
        c.metricReason = t.metricReason();
        c.trend = v.trend;
        candidates.push_back(std::move(c));
    }
    std::stable_sort(views_.begin(), views_.end(), [](const SkillView& a, const SkillView& b) {
        const int fa = coach::familyOrder(a.family), fb = coach::familyOrder(b.family);
        if (fa != fb) return fa < fb;
        if (a.family != b.family) return a.family < b.family;
        return a.title < b.title;
    });
    plan_ = coach::buildPlan(candidates, std::clamp(cfg_.planSize, 1, 5), weights());
    streak_ = days_.streak(today_, cfg_.streakMinAttempts);
    ++revision_;
}

std::vector<std::string> CoachModule::planLines() const {
    std::vector<std::string> lines;
    lines.push_back("Plan conseillé :");
    int i = 1;
    for (const auto& it : plan_) {
        lines.push_back(std::to_string(i++) + ". " + it.title + " : " + it.reason + " -> " + it.launch.consoleLine());
    }
    return lines;
}

int CoachModule::goalsDone() const {
    return static_cast<int>(std::count_if(goals_.begin(), goals_.end(), [](const Goal& g) { return g.done; }));
}

std::string CoachModule::titleFor(const std::string& scope) const {
    auto it = skills_.find(scope);
    if (it != skills_.end()) return it->second.title();
    return coach::scopeLabel(scope);
}

Goal CoachModule::focusGoal() const {
    PlanItem item;
    if (!plan_.empty()) item = plan_.front();
    else item = coach::starterPlan().front();
    Goal g;
    g.kind = GoalKind::Successes;
    g.scope = focusScope(item.skill);
    g.target = 10;
    g.preset = "focus";
    g.label = coach::customGoalLabel(g.kind, g.scope, g.target, 0.f, titleFor(g.scope));
    return g;
}

void CoachModule::addGoal(const Goal& g, Actions& out) {
    for (const auto& e : goals_) {
        if (e.kind == g.kind && e.scope == g.scope && e.target == g.target && e.minScore == g.minScore) {
            say(out, "Cet objectif est déjà actif : " + g.label + ".");
            return;
        }
    }
    if (goals_.size() >= coach::kMaxGoals) {
        say(out, "Tu as déjà " + std::to_string(coach::kMaxGoals) + " objectifs : retires-en un d'abord.");
        return;
    }
    Goal added = g;
    added.progress = 0;
    added.done = false;
    goals_.push_back(added);
    out.requestSave = true;
    if (cfg_.goalToasts) toast(out, "Objectif ajouté", added.label, ToastKind::Info);
}

// ---- Helpers -------------------------------------------------------------------------------------------
SkillTracker& CoachModule::tracker(const std::string& skill) {
    auto it = skills_.find(skill);
    if (it != skills_.end()) return it->second;
    if (skills_.size() >= kMaxSkills) {
        // Forget the skill practised the longest time ago (fewest attempts on ties).
        auto victim = skills_.begin();
        for (auto s = skills_.begin(); s != skills_.end(); ++s) {
            const auto& a = s->second;
            const auto& b = victim->second;
            if (a.lastDate() < b.lastDate() || (a.lastDate() == b.lastDate() && a.attempts() < b.attempts()))
                victim = s;
        }
        skills_.erase(victim);
    }
    return skills_.emplace(skill, SkillTracker(skill)).first->second;
}

void CoachModule::resetProgress() {
    skills_.clear();
    days_.clear();
    history_.clear();
    session_.stop();
    card_ = FeedbackCard{};
    sessionCount_ = 0;
    bestStreak_ = 0;
    totalAttempts_ = 0;
    unsavedResults_ = 0;
    for (auto& g : goals_) {
        g.progress = 0;
        g.done = false;
    }
    refreshDerived();
}

void CoachModule::say(Actions& out, const std::string& text) const {
    ChatMessage m;
    m.text = text;
    out.chat.push_back(std::move(m));
}

void CoachModule::toast(Actions& out, const std::string& title, const std::string& text, ToastKind kind) const {
    ToastMessage t;
    t.title = title;
    t.text = text;
    t.kind = kind;
    t.seconds = 4.f;
    out.toasts.push_back(std::move(t));
}

// ---- Accessors -----------------------------------------------------------------------------------------
CoachOverview CoachModule::overview() const {
    CoachOverview o;
    o.today = today_;
    if (const DayStats* d = days_.day(today_)) {
        o.todayAttempts = d->attempts;
        o.todaySuccesses = d->successes;
        o.todayAverage = d->attempts > 0 ? static_cast<float>(d->scoreSum / d->attempts) : 0.f;
    }
    o.streakMin = std::max(1, cfg_.streakMinAttempts);
    o.streak = streak_;
    o.bestStreak = std::max(bestStreak_, o.streak);
    o.sessionActive = session_.active();
    o.sessionIndex = session_.active() ? session_.index() : sessionCount_;
    o.sessionAttempts = session_.attempts();
    o.sessionSuccesses = session_.successes();
    o.sessionAverage = session_.average();
    o.sessionRecords = session_.records();
    o.sessionSeconds = session_.seconds();
    o.totalAttempts = totalAttempts_;
    o.skillCount = static_cast<int>(skills_.size());
    return o;
}

SkillView CoachModule::makeView(const std::string& id, const SkillTracker& t) const {
    SkillView v;
    v.id = id;
    v.title = t.title();
    v.family = coach::skillFamily(id);
    v.familyLabel = coach::familyLabel(v.family);
    v.attempts = t.attempts();
    v.successes = t.successes();
    v.successRate = t.successRate();
    v.recentSuccess = t.recentSuccessRate(10);
    v.averageAll = t.averageAll();
    v.averageRecent = t.averageRecent(SkillTracker::kRecentWindow);
    v.bestScore = t.bestScore();
    v.trendSlope = t.trendSlope();
    v.trend = t.trend(cfg_.trendThreshold);
    v.hasRecord = t.hasRecord();
    v.record = t.record();
    v.lowerIsBetter = t.lowerIsBetter();
    v.recordText = t.recordText();
    v.lastDate = t.lastDate();
    int d = 0;
    if (coach::validDate(today_) && coach::daysBetween(t.lastDate(), today_, d)) v.daysSince = std::max(0, d);
    v.lastSession = t.lastSession();
    v.mainMistake = t.mainMistake();
    v.scores = t.scores();
    v.launch = coach::launchForSkill(id);
    v.launchable = v.launch.valid();
    return v;
}

bool CoachModule::skill(const std::string& id, SkillView& v) const {
    for (const auto& view : views_) {
        if (view.id != id) continue;
        v = view;
        return true;
    }
    return false;
}

std::vector<MistakeView> CoachModule::topMistakes(size_t n) const {
    std::vector<MistakeView> out;
    for (const auto& v : views_) {
        if (!v.mainMistake.valid() || v.mainMistake.count < 2) continue;
        out.push_back({v.id, v.title, v.mainMistake});
    }
    std::stable_sort(out.begin(), out.end(), [](const MistakeView& a, const MistakeView& b) {
        const float ra = static_cast<float>(a.mistake.count) / static_cast<float>(std::max(1, a.mistake.window));
        const float rb = static_cast<float>(b.mistake.count) / static_cast<float>(std::max(1, b.mistake.window));
        if (ra != rb) return ra > rb;
        return a.mistake.count > b.mistake.count;
    });
    if (out.size() > n) out.resize(n);
    return out;
}

std::vector<DayPoint> CoachModule::recentDays(int n) const { return days_.lastDays(today_, n); }

// ---- Persistence ---------------------------------------------------------------------------------------
void CoachModule::save(Store& store) const {
    store.erasePrefix("coach.");
    store.setInt("coach.version", 1);
    store.setInt("coach.session_count", sessionCount_);
    store.setInt("coach.best_streak", bestStreak_);
    store.setInt("coach.total", totalAttempts_);
    std::vector<std::string> ids;
    ids.reserve(skills_.size());
    for (const auto& kv : skills_) {
        kv.second.save(store, "coach.s." + std::to_string(ids.size()) + ".");
        ids.push_back(kv.first);
    }
    store.setList("coach.skills", ids);
    days_.save(store, "coach.days");
    std::vector<std::string> sessions;
    for (const auto& s : history_) sessions.push_back(encodeSession(s));
    store.setList("coach.sessions", sessions);
    std::vector<std::string> goals;
    for (const auto& g : goals_) goals.push_back(coach::encodeGoal(g));
    store.setList("coach.goals", goals);
}

void CoachModule::load(const Store& store) {
    skills_.clear();
    history_.clear();
    goals_.clear();
    sessionCount_ = static_cast<int>(std::clamp<long long>(store.getInt("coach.session_count", 0), 0, 100000000));
    bestStreak_ = static_cast<int>(std::clamp<long long>(store.getInt("coach.best_streak", 0), 0, 100000));
    totalAttempts_ = static_cast<int>(std::clamp<long long>(store.getInt("coach.total", 0), 0, 1000000000));

    const size_t count = store.getList("coach.skills").size();
    long long sum = 0;
    for (size_t i = 0; i < count && skills_.size() < kMaxSkills; ++i) {
        SkillTracker t;
        if (!t.load(store, "coach.s." + std::to_string(i) + ".")) continue;
        if (skills_.count(t.id())) continue;
        sum += t.attempts();
        const std::string id = t.id();
        skills_.emplace(id, std::move(t));
    }
    totalAttempts_ = static_cast<int>(std::max<long long>(totalAttempts_, std::min<long long>(sum, 1000000000)));
    days_.load(store, "coach.days");
    for (const auto& line : store.getList("coach.sessions")) {
        SessionRecord s;
        if (decodeSession(line, s)) history_.push_back(s);
    }
    if (history_.size() > kMaxSessionHistory)
        history_.erase(history_.begin(), history_.begin() + static_cast<long>(history_.size() - kMaxSessionHistory));
    for (const auto& h : history_) sessionCount_ = std::max(sessionCount_, h.index);
    for (const auto& line : store.getList("coach.goals")) {
        if (goals_.size() >= coach::kMaxGoals) break;
        Goal g;
        if (coach::decodeGoal(line, g)) goals_.push_back(g);
    }
    refreshDerived();
}

} // namespace rlt
