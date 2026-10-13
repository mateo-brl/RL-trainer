#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "core/common/Text.h"
#include "core/modules/maps/MapCatalog.h"
#include "core/modules/maps/MapsModule.h"
#include "imgui.h"
#include "plugin/services/MapsService.h"
#include "plugin/ui/Ui.h"

namespace rlt {

namespace {

constexpr Color kGold{255, 200, 60, 255};
constexpr size_t kMaxRegistered = 500;
constexpr size_t kMaxKismetRows = 200;

ImVec4 toImVec(const Color& c) { return ImVec4(c.r / 255.f, c.g / 255.f, c.b / 255.f, c.a / 255.f); }

Color categoryColor(MapCategory c) {
    switch (c) {
    case MapCategory::Rings: return {90, 200, 255, 255};
    case MapCategory::Dribble: return {120, 220, 120, 255};
    case MapCategory::AirDribble: return {200, 150, 255, 255};
    case MapCategory::Shooting: return {255, 150, 80, 255};
    case MapCategory::Obstacle: return {255, 215, 90, 255};
    case MapCategory::Standard:
    case MapCategory::Other: break;
    }
    return colors::Grey;
}

void tooltip(const char* text) {
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

std::string lowerCopy(const std::string& s) { return toLowerAscii(s); }

// Key used by the maps module for an installed map (same rule as mapStatsKey on the loaded map).
std::string keyOf(const InstalledMap& m) { return isWorkshopId(m.id) ? m.id : mapStatsKey("", m.stem); }

std::string statsSummary(const MapStatsView* s) {
    if (!s) return "jamais jouée";
    std::string out;
    if (s->hasBest) out = "record " + formatTime(s->best, false);
    if (s->secondsPlayed >= 1.0)
        out += (out.empty() ? "" : " | ") + formatClock(static_cast<float>(s->secondsPlayed)) + " jouées";
    if (s->sessions > 0)
        out += (out.empty() ? "" : " | ") + std::to_string(s->sessions) + " session" + (s->sessions > 1 ? "s" : "");
    return out.empty() ? "pas encore de stats" : out;
}

// Installed maps cached in the UI state, rebuilt only when a new scan result is swapped in.
const MapsUiState& library(MapsService& svc) {
    MapsUiState& ui = svc.uiState();
    const int gen = svc.scanGeneration();
    if (gen == ui.libraryGeneration) return ui;
    ui.libraryGeneration = gen;
    ui.library = svc.installedMaps();
    ui.libraryKeys.clear();
    ui.librarySearch.clear();
    ui.libraryKeySet.clear();
    ui.libraryKeys.reserve(ui.library.size());
    ui.librarySearch.reserve(ui.library.size());
    for (const auto& m : ui.library) {
        ui.libraryKeys.push_back(keyOf(m));
        ui.librarySearch.push_back(lowerCopy(m.title + " " + m.stem + " " + m.id));
        ui.libraryKeySet.insert(ui.libraryKeys.back());
    }
    return ui;
}

void launchMap(UiContext& ctx, MapsService* svc, const InstalledMap& m) {
    if (!svc) return;
    ctx.runOnGame([svc, m]() { svc->launch(m); });
    ctx.post("maps", "played", {m.id, m.title, mapCategoryKey(m.category), m.path});
}

void navigationButtons(UiContext& ctx, MapsService* svc, bool onCustomMap) {
    if (!svc) return;
    if (onCustomMap) {
        if (ImGui::Button("Recharger la map")) ctx.runOnGame([svc]() { svc->reloadMap(); });
        tooltip("Recharge la map (comme servertravel) : ses variables, niveaux et chronos repartent de zéro.");
        ImGui::SameLine();
    }
    if (ImGui::Button("Menu principal")) ctx.runOnGame([svc]() { svc->exitToMenu(); });
    tooltip("Quitte la map et retourne au menu principal.");
    ImGui::SameLine();
    if (ImGui::Button("Retour au freeplay")) ctx.runOnGame([svc]() { svc->backToFreeplay(); });
    tooltip("Charge le freeplay standard (les exercices RL Trainer y sont disponibles).");
}

// ---- Current map -----------------------------------------------------------------------------------------
// Deltas come from the run itself (computed against the record held when the split happened; none for a run
// whose timing profile changed midway, its split numbers do not match the record's).
void splitsTable(const std::vector<RunSplit>& splits, const MapsConfig& cfg) {
    if (splits.empty()) return;
    const size_t first = splits.size() > 40 ? splits.size() - 40 : 0;  // the last 40 rows are enough
    const float rows = static_cast<float>(std::min<size_t>(splits.size() - first, 10) + 1);
    ImGui::BeginChild("##maps_splits", ImVec2(0, rows * ImGui::GetTextLineHeightWithSpacing() + 12.f), true);
    ImGui::Columns(4, "##maps_split_cols", true);
    ImGui::TextDisabled("Split");
    ImGui::NextColumn();
    ImGui::TextDisabled("Niveau");
    ImGui::NextColumn();
    ImGui::TextDisabled("Temps");
    ImGui::NextColumn();
    ImGui::TextDisabled("Écart au record");
    ImGui::NextColumn();
    ImGui::Separator();
    for (size_t i = first; i < splits.size(); ++i) {
        const RunSplit& s = splits[i];
        ImGui::Text("%d", s.index);
        ImGui::NextColumn();
        ImGui::Text("%d", s.level);
        ImGui::NextColumn();
        ImGui::TextUnformatted(formatTime(s.time, false).c_str());
        ImGui::NextColumn();
        if (s.hasDelta)
            ui::textColored(s.delta <= 0.f ? cfg.aheadColor : cfg.behindColor, formatDeltaSeconds(s.delta));
        else
            ImGui::TextDisabled("-");
        ImGui::NextColumn();
    }
    ImGui::Columns(1);
    ImGui::EndChild();
}

void kismetDebug(UiContext& ctx, const MapsLiveStatus& st, const std::vector<KismetCandidate>& vars, MapsService* svc) {
    if (!ImGui::CollapsingHeader("Variables Kismet (avancé)##maps_kismet")) return;
    ImGui::TextWrapped("%s", "Variables nommées de la map, lues en lecture seule (RL Trainer ne les modifie jamais). "
                             "Épingle celle qui compte les niveaux, celle qui lance le run ou le chrono de la map pour "
                             "avoir des splits exacts sur une map inconnue.");
    if (svc) {
        const KismetCacheInfo info = svc->kismetInfo();
        if (info.cached)
            ImGui::Text("Cache : %d variables%s", info.variables, info.recursive ? " (sous-séquences incluses)" : "");
        else if (info.attempts > 0)
            ImGui::TextDisabled("Aucune variable nommée trouvée (%d essais).", info.attempts);
        else
            ImGui::TextDisabled("%s", "Lecture des variables une seconde après le chargement de la map...");
    }
    ImGui::Text("Épinglées : départ = %s, niveau = %s, chrono = %s, niveau final = %d",
                st.pinStart.empty() ? "-" : st.pinStart.c_str(), st.pinLevel.empty() ? "-" : st.pinLevel.c_str(),
                st.pinTimer.empty() ? "-" : st.pinTimer.c_str(), st.pinFinal);
    MapsUiState* ui = svc ? &svc->uiState() : nullptr;
    if (ui) {
        ImGui::SetNextItemWidth(120.f);
        ImGui::InputInt("Niveau final##maps_final", &ui->finalLevelInput);
        ui->finalLevelInput = std::clamp(ui->finalLevelInput, 0, 10000);
        ImGui::SameLine();
        if (ImGui::SmallButton("Appliquer##maps_final_apply"))
            ctx.post("maps", "pin", {"final", std::to_string(ui->finalLevelInput)});
        tooltip("Dernier niveau de la map (avec une variable de niveau épinglée) : quand tu le termines, le run "
                "compte comme fini (record possible).");
        ImGui::SameLine();
    }
    if (ImGui::SmallButton("Tout désépingler##maps_unpin")) ctx.post("maps", "unpin", {"all"});
    if (vars.empty()) {
        ImGui::TextDisabled("%s", "Aucune variable reçue pour l'instant.");
        return;
    }
    const float rows = static_cast<float>(std::min<size_t>(vars.size(), 14) + 1);
    ImGui::BeginChild("##maps_kvars", ImVec2(0, rows * ImGui::GetTextLineHeightWithSpacing() + 12.f), true);
    ImGui::Columns(5, "##maps_kvar_cols", true);
    ImGui::TextDisabled("Nom");
    ImGui::NextColumn();
    ImGui::TextDisabled("Type");
    ImGui::NextColumn();
    ImGui::TextDisabled("Valeur");
    ImGui::NextColumn();
    ImGui::TextDisabled("Indices");
    ImGui::NextColumn();
    ImGui::TextDisabled("Épingler");
    ImGui::NextColumn();
    ImGui::Separator();
    size_t shown = 0;
    for (const auto& v : vars) {
        if (++shown > kMaxKismetRows) break;
        ImGui::PushID(v.name.c_str());
        const bool interesting = v.plusOneSteps > 0 || v.risingEdges > 0 || (v.timerLike && v.timerTicks > 1);
        if (interesting) ui::textColored(colors::Green, v.name);
        else ImGui::TextUnformatted(v.name.c_str());
        ImGui::NextColumn();
        ImGui::TextUnformatted(kismetTypeLabel(v.type));
        ImGui::NextColumn();
        ImGui::TextUnformatted(v.value.c_str());
        ImGui::NextColumn();
        switch (v.type) {
        case KismetVar::Type::Int: ImGui::Text("+1 x%d, autres %d", v.plusOneSteps, v.otherChanges); break;
        case KismetVar::Type::Bool: ImGui::Text("faux>vrai x%d", v.risingEdges); break;
        case KismetVar::Type::String:
            if (v.timerLike) ImGui::Text("chrono ? (%d)", v.timerTicks);
            else ImGui::TextDisabled("-");
            break;
        default: ImGui::TextDisabled("-"); break;
        }
        ImGui::NextColumn();
        if (v.type == KismetVar::Type::Int) {
            if (ImGui::SmallButton("Niveau")) ctx.post("maps", "pin", {"level", v.name});
            tooltip("Utilise cette variable comme compteur de niveaux (un split à chaque +1).");
        } else if (v.type == KismetVar::Type::Bool) {
            if (ImGui::SmallButton("Départ")) ctx.post("maps", "pin", {"start", v.name});
            tooltip("Le run démarre quand cette variable passe de faux à vrai. Sans variable de niveau épinglée, il "
                    "se termine quand elle repasse à faux (ou sur un but).");
        } else if (v.type == KismetVar::Type::String) {
            if (ImGui::SmallButton("Chrono")) ctx.post("maps", "pin", {"timer", v.name});
            tooltip("Chrono affiché par la map : le run démarre quand il revient à 0:00.");
        }
        ImGui::NextColumn();
        ImGui::PopID();
    }
    ImGui::Columns(1);
    ImGui::EndChild();
}

void currentMapSection(UiContext& ctx, const MapsLiveStatus& st, const MapsConfig& cfg, const MapStatsView* stats,
                       const std::vector<KismetCandidate>& kvars, bool favorite, MapsService* svc) {
    if (!st.onCustomMap) {
        ImGui::TextWrapped("%s", "Aucune map perso chargée. Choisis une map dans l'onglet « Mes maps » et clique sur "
                                 "Jouer : le chrono, les splits et tes stats démarrent tout seuls.");
        ImGui::Spacing();
        navigationButtons(ctx, svc, false);
        return;
    }

    ui::textColored(kGold, st.title);
    ImGui::SameLine();
    if (ImGui::SmallButton(favorite ? "Retirer des favoris" : "Ajouter aux favoris"))
        ctx.post("maps", favorite ? "unfav" : "fav", {st.key});

    ImGui::Text("Catégorie :");
    ImGui::SameLine();
    ui::textColored(categoryColor(st.category), mapCategoryLabel(st.category));
    ImGui::SameLine();
    ImGui::TextDisabled("(%s)", st.categorySource.c_str());
    ImGui::SameLine();
    ImGui::SetNextItemWidth(140.f);
    if (ImGui::BeginCombo("##maps_cat", st.categoryOverride ? "Mon choix" : "Auto")) {
        if (ImGui::Selectable("Auto (détection)", !st.categoryOverride)) ctx.post("maps", "category", {"auto"});
        for (int i = 1; i < kMapCategoryCount; ++i) {
            const auto c = static_cast<MapCategory>(i);
            if (ImGui::Selectable(mapCategoryLabel(c), st.categoryOverride && st.category == c))
                ctx.post("maps", "category", {mapCategoryKey(c)});
        }
        ImGui::EndCombo();
    }
    tooltip(
        "Change la catégorie si la détection se trompe : les autres onglets s'adaptent (cercle de contact, conseils).");
    if (!st.author.empty()) ImGui::Text("Auteur : %s", st.author.c_str());
    if (!st.trains.empty()) ImGui::TextWrapped("%s", st.trains.c_str());
    ImGui::TextDisabled("Fichier : %s | Id : %s", st.mapName.c_str(),
                        st.workshopId.empty() ? "inconnu" : st.workshopId.c_str());

    ui::separatorText("Chrono et splits");
    if (st.profileSource != ProfileSource::None) {
        ImGui::Text("Profil : %s", st.profileLabel.c_str());
        ui::helpMarker(profileSourceLabel(st.profileSource));
    } else if (cfg.fallbackTiming) {
        ImGui::TextDisabled("%s", "Pas de profil pour cette map : chrono automatique (départ quand tu bouges après un "
                                  "reset, arrivée sur un but).");
    } else {
        ImGui::TextDisabled("%s", "Pas de profil pour cette map et chrono automatique désactivé.");
    }
    switch (st.phase) {
    case RunPhase::Running: ui::textColored(colors::White, "Run en cours : " + formatTime(st.runTime, false)); break;
    case RunPhase::Armed: ImGui::TextDisabled("%s", "Prêt : le chrono part quand tu bouges."); break;
    case RunPhase::Idle:
        if (!st.ownStart) ImGui::TextDisabled("%s", "En attente du départ donné par la map.");
        else ImGui::TextDisabled("%s", "Fais un reset pour lancer un nouveau run.");
        break;
    }
    if (st.hasLevel) {
        if (st.finalLevel > 0) {
            const float frac = std::clamp(static_cast<float>(st.level) / static_cast<float>(st.finalLevel), 0.f, 1.f);
            const std::string overlay = "Niveau " + std::to_string(st.level) + " / " + std::to_string(st.finalLevel);
            ImGui::ProgressBar(frac, ImVec2(260.f, 0.f), overlay.c_str());
        } else {
            ImGui::Text("Niveau %d", st.level);
        }
    }
    if (st.hasMapTimer) ImGui::Text("Chrono de la map : %s", st.mapTimer.c_str());
    if (st.hasDeaths && st.phase == RunPhase::Running) ImGui::Text("Morts sur ce run : %d", st.deaths);
    if (st.hasLiveDelta) ui::textColored(cfg.behindColor, "Retard sur le record : " + formatDeltaSeconds(st.liveDelta));
    if (st.hasPb) ui::textColored(kGold, "Record : " + formatTime(st.pb, false));
    else ImGui::TextDisabled("%s", "Pas encore de record sur cette map.");
    if (st.last.valid) {
        std::string s = std::string(st.last.finished ? "Dernier run : " : "Dernier run (abandonné) : ") +
                        formatTime(st.last.time, false);
        if (st.lastWasPb) s += "  nouveau record !";
        else if (st.last.endedByReset) s += "  (fini sur reset, hors record)";
        else if (!st.last.recordEligible)
            s += st.last.cleanStart ? "  (hors record)" : "  (départ pas vu, hors record)";
        ui::textColored(st.lastWasPb ? cfg.aheadColor : colors::Grey, s);
    }
    ImGui::Text("Session : %s", formatClock(static_cast<float>(st.sessionSeconds)).c_str());
    if (stats) {
        ImGui::SameLine();
        ImGui::TextDisabled("| total %s | %d runs finis sur %d essais | %d touches | %d buts",
                            formatClock(static_cast<float>(stats->secondsPlayed)).c_str(), stats->finishes,
                            stats->attempts, stats->touches, stats->goals);
    }

    if (ImGui::Button("Relancer le chrono")) ctx.post("maps", "restart");
    tooltip("Abandonne le run en cours et attend ton prochain départ.");
    ImGui::SameLine();
    if (ImGui::Button("Effacer le record")) ImGui::OpenPopup("Effacer le record ?##maps");
    if (ImGui::BeginPopupModal("Effacer le record ?##maps", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Effacer ton record sur %s ?", st.title.c_str());
        if (ImGui::Button("Oui, effacer")) {
            ctx.post("maps", "reset_pb");
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Annuler")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    const std::vector<RunSplit>& rows = st.phase == RunPhase::Running ? st.splits : st.last.splits;
    if (!rows.empty()) {
        ImGui::TextDisabled("%s", st.phase == RunPhase::Running ? "Splits du run en cours" : "Splits du dernier run");
        splitsTable(rows, cfg);
    }
    if (stats && stats->lastRuns.size() >= 2) {
        float lo = stats->lastRuns.front(), hi = lo;
        for (float v : stats->lastRuns) {
            lo = std::min(lo, v);
            hi = std::max(hi, v);
        }
        ImGui::PlotLines("##maps_runs", stats->lastRuns.data(), static_cast<int>(stats->lastRuns.size()), 0,
                         "Tes derniers runs finis (plus bas = plus rapide)", lo * 0.95f, hi * 1.05f, ImVec2(0.f, 60.f));
    }

    ui::separatorText("Ce qui t'aide sur ce type de map");
    for (const auto& h : st.hints) ImGui::BulletText("%s : %s", h.tab.c_str(), h.text.c_str());

    ImGui::Spacing();
    navigationButtons(ctx, svc, true);
    if (ctx.showAdvanced) {
        ImGui::Spacing();
        kismetDebug(ctx, st, kvars, svc);
    }
}

// ---- Installed maps --------------------------------------------------------------------------------------
struct Row {
    const InstalledMap* map = nullptr;
    const std::string* key = nullptr;
    const MapStatsView* stats = nullptr;
    bool favorite = false;
};

void librarySection(UiContext& ctx, MapsService* svc, const std::vector<MapStatsView>& stats,
                    const std::vector<std::string>& favorites, const MapScanOptions& options,
                    const std::string& currentKey) {
    if (!svc) {
        ImGui::TextDisabled("%s", "Service des maps indisponible.");
        return;
    }
    MapsUiState& ui = svc->uiState();
    const MapsUiState& lib = library(*svc);
    const std::vector<InstalledMap>& maps = lib.library;
    const bool scanning = svc->scanning();

    if (scanning) ImGui::TextDisabled("%s", "Recherche des maps installées...");
    else ImGui::Text("%d maps installées trouvées", static_cast<int>(maps.size()));
    ImGui::SameLine();
    if (ImGui::Button("Rafraîchir")) svc->rescan(options);
    tooltip("Relance la recherche (workshop Steam, dossier mods, Workshop Map Loader et tes dossiers en plus).");

    if (ImGui::CollapsingHeader("Dossiers scannés##maps_roots")) {
        const auto roots = svc->scanRoots();
        if (roots.empty()) ImGui::TextDisabled("%s", "Aucun dossier scanné pour l'instant.");
        for (const auto& r : roots) {
            if (r.exists) ImGui::BulletText("%s : %s (%d maps)", r.label.c_str(), r.path.c_str(), r.found);
            else ImGui::BulletText("%s : %s (introuvable)", r.label.c_str(), r.path.c_str());
        }
        if (const SettingDef* def = ctx.engine.schema().find("rlt_map_extra_folders")) {
            ui::settingWidget(ctx, *def, ctx.settings());
            ImGui::TextDisabled("%s", "Valide avec Entrée : la recherche repart toute seule.");
        }
    }

    ImGui::SetNextItemWidth(220.f);
    ImGui::InputText("Rechercher##maps_search", ui.search, sizeof(ui.search));
    ImGui::SameLine();
    ImGui::Checkbox("Favoris seulement##maps_favonly", &ui.favoritesOnly);
    auto filterButton = [&](const char* label, int value) {
        const bool selected = ui.categoryFilter == value;
        if (selected) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.26f, 0.59f, 0.98f, 0.8f));
        if (ImGui::SmallButton(label)) ui.categoryFilter = value;
        if (selected) ImGui::PopStyleColor();
    };
    filterButton("Toutes##maps_f", -1);
    for (int i = 1; i < kMapCategoryCount; ++i) {
        ImGui::SameLine();
        const std::string label =
            std::string(mapCategoryLabel(static_cast<MapCategory>(i))) + "##maps_f" + std::to_string(i);
        filterButton(label.c_str(), i);
    }

    std::map<std::string, const MapStatsView*> byKey;
    for (const auto& s : stats) byKey[s.key] = &s;
    const std::set<std::string> favSet(favorites.begin(), favorites.end());
    const std::string needle = lowerCopy(ui.search);
    std::vector<Row> rows;
    rows.reserve(maps.size());
    for (size_t i = 0; i < maps.size(); ++i) {
        const InstalledMap& m = maps[i];
        Row r;
        r.map = &m;
        r.key = &lib.libraryKeys[i];
        auto it = byKey.find(*r.key);
        r.stats = it == byKey.end() ? nullptr : it->second;
        r.favorite = favSet.count(*r.key) != 0;
        if (ui.favoritesOnly && !r.favorite) continue;
        if (ui.categoryFilter >= 0 && static_cast<int>(m.category) != ui.categoryFilter) continue;
        if (!needle.empty() && lib.librarySearch[i].find(needle) == std::string::npos) continue;
        rows.push_back(r);
    }
    // Favorites first, then last played; the scan order (by title) is kept otherwise.
    std::stable_sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) {
        if (a.favorite != b.favorite) return a.favorite;
        const long long sa = a.stats ? a.stats->lastSeq : 0, sb = b.stats ? b.stats->lastSeq : 0;
        return sa > sb;
    });

    if (maps.empty() && !scanning) {
        ImGui::TextWrapped("%s",
                           "Aucune map trouvée. Abonne-toi à des maps sur le workshop Steam, ou installe le plugin "
                           "Workshop Map Loader (Epic), ou ajoute ton dossier de maps dans les réglages. "
                           "L'onglet Aide explique tout.");
    }
    const float height =
        std::min(14.f, static_cast<float>(rows.size()) + 1.f) * ImGui::GetTextLineHeightWithSpacing() + 10.f;
    ImGui::BeginChild("##maps_list", ImVec2(0, std::max(height, 60.f)), true);
    ImGuiListClipper clipper(static_cast<int>(rows.size()), ImGui::GetTextLineHeightWithSpacing());
    while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
            const Row& r = rows[static_cast<size_t>(i)];
            const InstalledMap& m = *r.map;
            const std::string& key = *r.key;
            ImGui::PushID(i);
            ImGui::PushStyleColor(ImGuiCol_Text, r.favorite ? toImVec(kGold) : ImVec4(0.6f, 0.6f, 0.6f, 1.f));
            if (ImGui::SmallButton(r.favorite ? "*" : "+")) ctx.post("maps", r.favorite ? "unfav" : "fav", {key});
            ImGui::PopStyleColor();
            tooltip(r.favorite ? "Retirer des favoris" : "Ajouter aux favoris");
            ImGui::SameLine();
            if (ImGui::SmallButton("Jouer")) launchMap(ctx, svc, m);
            ImGui::SameLine();
            ui::textColored(categoryColor(m.category), std::string("[") + mapCategoryLabel(m.category) + "]");
            ImGui::SameLine();
            if (key == currentKey) ui::textColored(kGold, m.title);
            else ImGui::TextUnformatted(m.title.c_str());
            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::Text("Fichier : %s", m.path.c_str());
                ImGui::Text("Source : %s | Id : %s", m.source.c_str(), m.id.c_str());
                if (r.stats)
                    ImGui::Text("%d sessions | %d runs finis sur %d essais | %d touches", r.stats->sessions,
                                r.stats->finishes, r.stats->attempts, r.stats->touches);
                ImGui::EndTooltip();
            }
            if (m.nonAscii) {
                ImGui::SameLine();
                ui::textColored(colors::Orange, "(!)");
                tooltip(
                    "Chemin avec des accents : le chargement peut échouer. Déplace la map dans un dossier sans "
                    "accents.");
            }
            ImGui::SameLine();
            ImGui::TextDisabled("%s", statsSummary(r.stats).c_str());
            ImGui::PopID();
        }
    }
    ImGui::EndChild();

    // Maps with stats that are not installed anymore (or launched by another loader).
    auto orphan = [&](const MapStatsView& s) { return lib.libraryKeySet.count(s.key) == 0; };
    if (std::any_of(stats.begin(), stats.end(), orphan) &&
        ImGui::CollapsingHeader("Autres maps jouées##maps_orphans")) {
        for (const auto& so : stats) {
            if (!orphan(so)) continue;
            const MapStatsView* s = &so;
            ImGui::PushID(s->key.c_str());
            if (ImGui::SmallButton("Oublier")) ctx.post("maps", "forget", {s->key});
            tooltip("Efface les stats de cette map.");
            ImGui::SameLine();
            ui::textColored(categoryColor(s->category), std::string("[") + mapCategoryLabel(s->category) + "]");
            ImGui::SameLine();
            ImGui::TextUnformatted((s->title.empty() ? s->key : s->title).c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("%s", statsSummary(s).c_str());
            ImGui::PopID();
        }
    }
}

// ---- Help ------------------------------------------------------------------------------------------------
void helpSection(UiContext& ctx, MapsService* svc) {
    ui::separatorText("Avoir des maps workshop");
    ImGui::BulletText("%s",
                      "Steam : abonne-toi aux maps sur le Workshop de Rocket League (Steam > Communauté > Workshop).");
    ImGui::BulletText("%s", "Epic : installe le plugin Workshop Map Loader (BakkesMod > Plugins). Ses maps vont dans "
                            "TAGame/CookedPCConsole/mods, scanné automatiquement.");
    ImGui::BulletText("%s", "Fichiers .udk ou .upk téléchargés à la main : mets-les dans un dossier et ajoute-le dans "
                            "Réglages > Dossiers de maps, puis clique sur Rafraîchir.");
    ImGui::BulletText(
        "%s", "Eau noire, murs pas collants ? Installe le pack Workshop Textures (proposé par Workshop Map Loader).");
    if (svc && !svc->usingSteam())
        ImGui::TextDisabled("%s", "Version Epic détectée : le workshop Steam n'est pas scanné.");

    ui::separatorText("Maps conseillées");
    static const std::vector<InstalledMap> kNone;
    const std::vector<InstalledMap>& maps = svc ? library(*svc).library : kNone;
    for (auto c : {MapCategory::Rings, MapCategory::Dribble, MapCategory::AirDribble, MapCategory::Shooting,
                   MapCategory::Obstacle}) {
        const std::string header = std::string(mapCategoryLabel(c)) + " : " + mapCategoryPurpose(c) + "##maps_help_" +
                                   mapCategoryKey(c);
        if (!ImGui::CollapsingHeader(header.c_str())) continue;
        for (const CatalogMap* m : MapCatalog::recommended(c)) {
            ImGui::PushID(m->title.c_str());
            const InstalledMap* installed = nullptr;
            for (const auto& im : maps)
                if ((!m->ids.empty() && std::find(m->ids.begin(), m->ids.end(), im.id) != m->ids.end()) ||
                    MapCatalog::findByStem(im.stem) == m) {
                    installed = &im;
                    break;
                }
            if (installed) {
                if (ImGui::SmallButton("Jouer")) launchMap(ctx, svc, *installed);
            } else if (!m->ids.empty()) {
                if (ImGui::SmallButton("Copier l'id")) ImGui::SetClipboardText(m->ids.front().c_str());
                tooltip("Copie l'id Steam : colle-le dans la recherche du workshop pour trouver la map.");
            }
            ImGui::SameLine();
            ui::textColored(kGold, m->title);
            ImGui::SameLine();
            ImGui::TextDisabled("par %s%s", m->author.empty() ? "?" : m->author.c_str(),
                                installed ? "" : " (pas installée)");
            ImGui::Indent();
            ImGui::TextWrapped("%s", m->trains.c_str());
            ImGui::Unindent();
            ImGui::PopID();
        }
    }

    ui::separatorText("Comment marche le chrono");
    ImGui::BulletText(
        "%s",
        "Maps connues (anneaux de Lethamyr, Speed Jump Rings, Dribble 2 Overhaul, Air Dribble Hoops...) : "
        "départ, niveaux et arrivée lus dans la map, splits exacts.");
    ImGui::BulletText("%s", "Autres maps : le chrono part quand tu bouges après un reset et s'arrête sur un but. "
                            "Un compteur comme Level ou Checkpoint est utilisé pour les splits s'il est détecté.");
    ImGui::BulletText("%s", "Réglages avancés : la liste des variables Kismet te permet d'épingler toi-même le départ, "
                            "le niveau, le chrono et le niveau final d'une map.");
    ImGui::BulletText(
        "%s", "Chaque run fini arrive dans Progression avec ton record ; l'écart au record s'affiche à chaque split.");
    ImGui::BulletText("%s", "RL Trainer ne fait que lire la map : il ne déplace jamais la balle ni la voiture ici.");

    ui::separatorText("Commandes console (F6)");
    ImGui::BulletText("%s", "rlt maps load <id> : lance une map déjà vue dans cet onglet");
    ImGui::BulletText("%s", "rlt maps reload | menu | freeplay : recharge la map, menu principal, freeplay");
    ImGui::BulletText("%s", "rlt maps fav <id> | unfav <id> : favoris");
    ImGui::BulletText("%s", "rlt maps restart : relance le chrono");
    ImGui::BulletText("%s", "rlt maps pin <start|level|timer|final> <valeur> | unpin : variables de la map");
    ImGui::BulletText("%s", "rlt maps category <rings|dribble|airdribble|shooting|obstacle|other|auto>");
    ImGui::BulletText("%s", "rlt maps reset_pb | forget [id] : efface le record ou les stats");
    ImGui::TextDisabled("%s", "Astuce : bind F5 \"rlt maps reload\" pour recharger la map d'une touche.");
}

} // namespace

void renderMapsTab(UiContext& ctx) {
    MapsService* svc = ctx.mapsService;
    MapsLiveStatus st;
    std::vector<MapStatsView> stats;
    std::vector<KismetCandidate> kvars;
    std::vector<std::string> favorites;
    std::map<std::string, MapCategory> overrides;
    MapsConfig cfg;
    bool found = false;
    {
        auto lk = ctx.engine.lock();
        if (auto* mod = ctx.engine.get<MapsModule>()) {
            found = true;
            st = mod->status();
            stats = mod->statsList();
            favorites = mod->favorites();
            overrides = mod->categoryOverrides();
            cfg = mod->config();
            if (ctx.showAdvanced && st.onCustomMap) kvars = mod->kismetCandidates();
        }
    }
    if (!found) {
        ImGui::TextDisabled("%s", "Module Maps indisponible.");
        return;
    }

    MapScanOptions options;
    options.extraFolders = cfg.extraFolders;
    options.steam = cfg.scanSteam;
    options.mods = cfg.scanMods;
    options.wml = cfg.scanWml;
    if (svc) {
        svc->setCategoryOverrides(overrides);
        svc->ensureScanned(options);
        // Tell the module where each installed map lives, so "rlt maps load <id>" works from the console.
        MapsUiState& ui = svc->uiState();
        const int gen = svc->scanGeneration();
        if (gen != ui.registeredGeneration && !svc->scanning()) {
            std::vector<std::string> args;
            for (const auto& m : svc->installedMaps()) {
                if (args.size() / 2 >= kMaxRegistered) break;
                args.push_back(m.id);
                args.push_back(m.path);
            }
            if (!args.empty()) ctx.post("maps", "register", std::move(args));
            ui.registeredGeneration = gen;
        }
    }

    ImGui::TextWrapped("%s", "Tes maps workshop (anneaux, dribble, air dribble, tir...) : RL Trainer reconnaît la map, "
                             "chronomètre tes runs avec des splits, garde tes records et tes stats par map.");
    if (!cfg.enabled) ui::textColored(colors::Orange, "Le module Maps est désactivé (onglet Réglages ci-dessous).");

    const MapStatsView* currentStats = nullptr;
    bool favorite = false;
    for (const auto& s : stats)
        if (s.key == st.key) {
            currentStats = &s;
            favorite = s.favorite;
        }

    if (ImGui::BeginTabBar("##maps_tabs")) {
        if (ImGui::BeginTabItem("Map en cours")) {
            currentMapSection(ctx, st, cfg, currentStats, kvars, favorite, svc);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Mes maps")) {
            librarySection(ctx, svc, stats, favorites, options, st.onCustomMap ? st.key : std::string());
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Aide")) {
            helpSection(ctx, svc);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Réglages")) {
            ui::moduleSettings(ctx, "maps");
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

} // namespace rlt
