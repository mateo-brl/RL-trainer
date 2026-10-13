# Architecture de RL Trainer

Ce document est le contrat technique du projet. Toute contribution (humaine ou agent) le respecte.

## 1. Vue d'ensemble

```
src/
  core/                      C++20 pur, AUCUNE dépendance au SDK BakkesMod, testé sur Linux et Windows
    common/                  types partagés (Snapshot, Settings, DrawList, Actions, Module, Store...)
    engine/                  Engine : possède les modules, dispatch, arbitrage des exercices
    modules/<id>/            une fonctionnalité = un module (logique + réglages + persistance)
  plugin/                    couche BakkesMod (Windows/MSVC), aussi fine que possible
    RLTrainer.*              point d'entrée : hooks, drawable, cvars, timers, fenêtres ImGui
    GameBridge.*             SDK -> Snapshot / ViewContext, Actions -> SDK
    Renderer.*               DrawList -> canvas (projection, clipping, occlusion, panneaux)
    services/                accès SDK spécifiques (ProfileService, MapsService)
    ui/                      ImGui : Ui.* (cadre + widgets générés depuis le schéma), tabs/*Tab.cpp
tests/<id>/                  tests unitaires d'un module (exécutable rlt_test_<id>)
tests/common/                tests du cœur et du moteur
tools/sdk_syntax_check.sh    compile src/plugin/** contre les vrais headers du SDK (clang, Linux)
scripts/                     install.bat / install.ps1 (installation), verify_dll.py (CI)
```

Règle d'or : **toute la logique de jeu vit dans `src/core`**. Le plugin ne fait que traduire.
Ainsi tout est testable sans Rocket League.

## 2. Flux de données

| Quand | Thread | Glue (plugin) | Cœur |
|---|---|---|---|
| Chaque tick physique (120 Hz), voiture locale | jeu | hook pré `Car_TA.SetVehicleInput` -> `GameBridge::buildSnapshot` | `Engine::tick(snapshot, actions)` -> `Module::onTick` |
| Contact voiture/balle | jeu | hook post `Car_TA.OnHitBall` -> `Engine::reportTouch` | résolu au tick suivant en `TouchEvent{pre, post}` -> `Module::onTouch` |
| Événements (reset, but, flip reset, map chargée...) | jeu | hooks -> `Engine::gameEvent` | `Module::onGameEvent` |
| Chaque image rendue | jeu | `RegisterDrawable` -> `GameBridge::buildView` | `Engine::draw` -> `Module::draw(view, ctx, DrawList)` -> `Renderer` |
| Toutes les 0,25 s | jeu | `SetTimeout` -> contexte, profil, commandes, sauvegardes | `Engine::setContext`, `setProfile`, `pump` |
| Interface | rendu | `RenderSettings` / `Render` -> `renderUi` | lecture sous `Engine::lock()`, écriture via cvars et commandes |

`TouchEvent.pre` est l'état au début du tick du contact (avant l'impulsion), `post` l'état au tick
suivant (après). Ne jamais lire de vitesses « après impact » dans le hook lui-même.

## 3. Écrire un module

Un module = `src/core/modules/<id>/` + `tests/<id>/` + `src/plugin/ui/tabs/<Id>Tab.cpp`.

- Classe `XModule : public Module` avec `id()`, `title()` (français), `static void describeSettings(SettingsSchema&)`.
- Réglages : struct `XConfig` dans `XConfig.h` (agrégée dans `Settings`). Chaque champ est déclaré **une fois**
  dans `describeSettings` (cvar, défaut, bornes, libellé et infobulle en français). Les cvars sont
  `rlt_<préfixe>_<nom>` : `ov` overlay, `ft` firsttouch, `pr` prediction, `me` mechanics (mouvement),
  `bm` ballmech (mécas balle), `ko` kickoff, `co` courses (anneaux, contrôle), `sh` shooting (flicks, tirs),
  `map` maps, `coach` coach, `pf` profile. Ne jamais renommer une cvar publiée.
- Données entrantes : uniquement les arguments des callbacks (`TickContext`, `TouchEvent`, `GameEvent`,
  `ViewContext`, `DrawContext`, `Command`, `KismetVar`). Jamais le SDK.
- Données sortantes : `Actions` (placement balle/voiture, inputs, vitesse du jeu, chat, toasts, résultats,
  événements, commandes console autorisées) et `DrawList` (2D, 3D, panneaux).
- Un module ne dépend **d'aucun autre module** (bibliothèques séparées). Pour communiquer : émettre un
  `GameEvent` dans `Actions::events` ou un `AttemptResult` dans `Actions::results` (le coach les reçoit tous).
- Persistance : `save(Store&)` / `load(const Store&)`, clés préfixées `<id>.`. Rester raisonnable en taille
  (historiques bornés, par exemple 200 entrées max). `Store::setDouble` pour les grands compteurs.
- Remettre ses propres réglages par défaut depuis une commande : `Actions::resetSettings` (`"<id>"` ou
  `"<id>/<section>"`), exécuté par la glue via les cvars.
- `ViewContext::frameDt` donne le temps réel entre deux images (lissages dans les replays, sans tick).
- `GameContext::paused` est tenu à jour par le moteur (événements Paused/Unpaused).

### Exercices et contrôle du monde

Déplacer la balle ou la voiture, forcer des inputs ou changer la vitesse du jeu exige :
1. `requestActivity("nom de l'exercice")` qui renvoie `true` (le moteur arrête l'exercice précédent) ;
2. un contexte qui l'autorise (freeplay ; workshop et entraînement personnalisé seulement si l'option
   avancée est cochée). Sinon le moteur filtre ces actions.

`onActivityStopped()` doit tout remettre à zéro (vitesse du jeu à 1 si modifiée). `releaseActivity()` à la fin
normale d'un exercice. `freezeCar` et `overrideInputs` ne s'appliquent que depuis `onTick`.

### Résultats et coaching

Chaque tentative notée produit un `AttemptResult` (score 0..100, `success`, métriques, conseils `Tip`
triés par poids, valeur de record). Les conseils sont en français, une phrase courte à l'impératif, avec un
`code` stable (`<module>.<erreur>`) pour compter les erreurs récurrentes.

## 4. Affichage (HUD)

- Positions configurables en **fractions d'écran** (0..1), multipliées par `view.screenW/H`.
- Taille : `DrawContext::hudScale` (réglage utilisateur x hauteur d'écran / 1080).
- Utiliser `DrawList::panel` pour les panneaux (le renderer mesure le texte et dessine le fond).
- Zones par défaut pour éviter les chevauchements :

| Zone (x, y en fraction d'écran) | Module |
|---|---|
| bas centre (0.46, 0.95) | overlay : écart de vitesse (parité avec l'original) |
| haut centre (0.47, 0.01) | overlay : chrono de session |
| haut gauche (0.01, 0.12) | exercice en cours (kickoff, parcours, drills) : chrono, consignes |
| gauche milieu (0.01, 0.40) | mechanics : dernière méca détectée |
| droite (0.99, 0.30), ancré à droite | coach : carte du dernier essai + conseils |
| droite bas (0.99, 0.70), ancré à droite | firsttouch : stats de session des touches |

- Texte HUD : passe par `hudText()` (accents repliés en ASCII par défaut). Pas de tiret cadratin.
- 3D : `DrawList::line3D/circle3D/...` ; `Occluder` pour masquer ce qui est derrière la balle.

## 5. Interface (ImGui 1.75 de BakkesMod)

- Thread de rendu : lire l'état d'un module sous `auto lk = ctx.engine.lock();` puis copier, relâcher.
- Modifier un réglage : `ctx.setCvar(nom, valeur)`. Déclencher une action : `ctx.post(module, commande, args)`.
- Pas d'API `BeginTable` (absente en 1.75) : utiliser `Columns`. Pas de `imgui_stdlib`.
- `ui::moduleSettings(ctx, "<id>")` dessine tous les réglages du module depuis le schéma.

## 6. Vérifications

```
cmake -S . -B build -G Ninja && cmake --build build && ctest --test-dir build   # cœur + tests
tools/sdk_syntax_check.sh                                                       # glue vs SDK
```
La CI (`.github/workflows/build.yml`) fait les deux sur Linux, puis compile la vraie DLL avec MSVC,
relance les tests sous MSVC et vérifie la DLL (`scripts/verify_dll.py`).
