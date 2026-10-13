# RL Trainer

Plugin [BakkesMod](https://bakkesmod.com) pour Rocket League : un entraîneur intelligent qui analyse chacune
de tes touches, reconnaît tes mécaniques, te donne des conseils en français et adapte les exercices à ton niveau.

Inspiré de [First Touch Trainer](https://bakkesplugins.com/plugin/476) d'AkumaRL : RL Trainer reprend
100 % de ses fonctions (voir [docs/PARITE.md](docs/PARITE.md)) et va beaucoup plus loin. Le code est entièrement original.

> Le plugin se désactive complètement en ligne. Il ne fonctionne qu'en freeplay, en entraînement personnalisé,
> sur les maps workshop et dans les replays.

## Ce que fait RL Trainer

| Onglet | Contenu |
|---|---|
| **Affichage** | Écart de vitesse voiture/balle en couleur (comme l'original, en mieux), chrono de session, cercle de contact sur la balle (statique, piloté par la vitesse, garder en l'air, auto), sphère centrale. |
| **Première touche** | Analyse de chaque touche : écart de vitesse juste avant l'impact, point de contact, contrôle après la touche, note sur 100, carte de tes points de contact, conseils. Exercices auto-adaptatifs (balle roulante, passe, lob, chute du mur, réception aérienne). |
| **Mécas mouvement** | Détection et notation automatiques : speedflip, half-flip, wavedash, chain wavedash, zap dash, wall dash, fast aerial, stall, hélicoptère, réceptions, powerslide, gestion du boost. Mode focus avec ralenti. |
| **Mécas balle** | Flip reset, ceiling shot, flicks (45, 90, musty, breezi...), double tap, air dribble, air roll shots, pinches, power shots. |
| **Kickoff** | Les 5 spawns, compte à rebours comme en match, chrono à la frame, adversaire fantôme (niveau pro ou ton record), note et conseils. |
| **Parcours** | Maps virtuelles en freeplay : anneaux en vol, au sol, en air dribble, dribble sur le toit, slalom, rattrapages. Codes de parcours partageables. |
| **Tirs & flicks** | Mode flick (balle posée sur le toit), cibles dans le but (lucarnes, poteaux, centre), record de puissance. |
| **Prédiction** | Trajectoire de la balle avec rebonds, point d'atterrissage, point d'interception conseillé et vitesse à adopter. |
| **Maps** | Tes maps workshop installées, lancement en un clic, chronos et temps intermédiaires sur les maps connues, stats par map. |
| **Progression** | Records, historique, erreurs récurrentes, objectifs de session, séries de jours, plan d'entraînement conseillé. |
| **Ma config** | Détection automatique de ta config Rocket League : manette ou clavier, tes touches, ta caméra, ta voiture et sa hitbox, conseils, raccourcis du plugin sans conflit avec tes binds. |

## Installation

### Option 1 : télécharger la DLL compilée

1. Ouvre l'onglet **Actions** du dépôt GitHub, choisis la dernière exécution réussie du workflow `build`
   et télécharge l'artefact `rltrainer-...` (il faut être connecté à GitHub).
2. Dézippe-le, puis double-clique sur `install.bat`. Le script copie `rltrainer.dll` dans le dossier
   `plugins` de BakkesMod et active le chargement automatique au lancement du jeu.
3. Lance Rocket League avec BakkesMod. Si le jeu était déjà ouvert, appuie sur **F6** et tape `plugin load rltrainer`.

### Option 2 : compiler toi-même (Windows)

1. Installe **Visual Studio 2022 ou 2026** avec la charge de travail « Développement Desktop en C++ ».
2. Clone le dépôt, puis dans un terminal « Developer PowerShell » :
   ```powershell
   cmake -S . -B build -A x64
   cmake --build build --config Release
   ```
   Le SDK BakkesMod est pris automatiquement dans `%APPDATA%\bakkesmod\bakkesmod\bakkesmodsdk`
   (sinon ajoute `-DBAKKESMOD_SDK_DIR=<chemin du SDK>`). Ajoute `-DRLT_INSTALL_TO_BAKKESMOD=ON`
   pour copier la DLL dans BakkesMod à chaque compilation.
3. La DLL est dans `build\Release\rltrainer.dll`. Installe-la avec `scripts\install.bat` (copie-la à côté du script)
   ou copie-la dans `%APPDATA%\bakkesmod\bakkesmod\plugins`.

## Utilisation

- **F2 > Plugins > RL Trainer** : tous les onglets et réglages.
- **Fenêtre dédiée** : tape `rlt_menu` dans la console F6 (ou `togglemenu rltrainer`). L'onglet « Ma config »
  peut t'assigner une touche libre pour l'ouvrir.
- **Console** : `rlt <module> <commande> [arguments]`, par exemple `rlt kickoff start random`,
  `rlt firsttouch drill mix`, `rlt courses rings air`, `rlt engine stop`.
- Tes réglages sont enregistrés dans le `config.cfg` de BakkesMod, ta progression dans
  `bakkesmod/data/rltrainer/progress.txt`.

## Développement

Toute la logique (analyse, mécas, exercices, coach) est en C++20 pur dans `src/core`, testée sur Linux et Windows.
La couche BakkesMod (`src/plugin`) ne fait que traduire. Détails dans [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

```bash
cmake -S . -B build -G Ninja && cmake --build build && ctest --test-dir build   # cœur + tests unitaires
tools/sdk_syntax_check.sh                                                       # couche BakkesMod vs headers du SDK
```

La CI GitHub Actions lance les tests sur Linux et Windows, vérifie la couche BakkesMod contre les headers du SDK,
compile la vraie DLL avec MSVC et contrôle qu'elle est conforme (x64, exports du plugin, runtime statique).
