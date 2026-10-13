# RL Trainer

Plugin [BakkesMod](https://bakkesmod.com) pour Rocket League. Il note chacune de tes touches de balle et tes
mécaniques, t'explique en français ce qui a coincé et règle la difficulté des exercices sur tes résultats.

## Installation

Il te faut BakkesMod, installé et lancé au moins une fois.

1. Ferme Rocket League.
2. Télécharge **[RLTrainer-Setup.exe](https://github.com/mateo-brl/RL-trainer/releases/latest/download/RLTrainer-Setup.exe)** et double-clique dessus.
3. Lance Rocket League avec BakkesMod, puis appuie sur **F2 > Plugins > RL Trainer**.

L'installeur n'est pas signé. Si Windows affiche « Windows a protégé votre ordinateur », clique sur
« Informations complémentaires » puis « Exécuter quand même ».

Les mises à jour se font depuis l'onglet **Accueil** du plugin, en un clic. Pour désinstaller :
Paramètres Windows > Applications > « RL Trainer (plugin BakkesMod) ». Ta progression est gardée.

## Utilisation

- **F2 > Plugins > RL Trainer** ouvre tous les onglets et réglages.
- `rlt_menu` dans la console (F6) ouvre la même chose dans une fenêtre à part.
- Le plugin ne fait rien en ligne. Il marche en freeplay, en entraînement personnalisé, sur les maps workshop
  et dans les replays.

## Fonctions

- **Affichage** : écart de vitesse voiture/balle en couleur, chrono de session, cercle de contact sur la balle,
  sphère au centre de la balle.
- **Première touche** : note sur 100 de chaque touche (vitesse, point de contact, contrôle après la touche),
  carte de tes points de contact, conseils, exercices (balle roulante, passe, lob, chute du mur, réception aérienne).
- **Mécas mouvement** : détection et note du speedflip, half-flip, wavedash, chain wavedash, zap dash, wall dash,
  fast aerial, stall, hélicoptère, réceptions, powerslide et gestion du boost. Mode focus au ralenti.
- **Mécas balle** : flip reset, ceiling shot, flicks (45, 90, musty, breezi...), double tap, air dribble,
  air roll shot, pinch, frappe puissante.
- **Kickoff** : les 5 positions, décompte comme en match, temps à la frame, fantôme (niveaux ou ton record),
  note et conseils.
- **Parcours** : anneaux en vol, au sol et en air dribble, dribble sur le toit, slalom, rattrapages, directement
  en freeplay. Codes de parcours à partager.
- **Tirs & flicks** : balle posée sur le toit pour flicker, cibles dans le but (lucarnes, poteaux, centre),
  record de puissance.
- **Prédiction** : trajectoire de la balle avec rebonds, point d'atterrissage, meilleur point pour l'intercepter.
- **Maps** : tes maps workshop installées, lancement en un clic, chronos sur les maps connues, stats par map.
- **Progression** : records, historique, erreurs qui reviennent, objectifs de session, jours d'affilée,
  exercice conseillé.
- **Ma config** : lit ta config Rocket League (manette ou clavier, touches, caméra, voiture et hitbox) pour
  afficher tes vraies touches dans les messages et choisir des raccourcis sans conflit.

RL Trainer reprend toutes les fonctions de [First Touch Trainer](https://bakkesplugins.com/plugin/476) d'AkumaRL,
avec un code entièrement original. Le détail, fonction par fonction : [docs/PARITE.md](docs/PARITE.md).

## Développement

Le cœur (`src/core`) est en C++20 sans dépendance et testé sur Linux et Windows. La couche BakkesMod est dans
`src/plugin`. Architecture : [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

```bash
cmake -S . -B build -G Ninja && cmake --build build && ctest --test-dir build
```

Pour la DLL sous Windows : `cmake -S . -B build -A x64` puis `cmake --build build --config Release`
(SDK BakkesMod dans `%APPDATA%\bakkesmod\bakkesmod\bakkesmodsdk`, ou `-DBAKKESMOD_SDK_DIR=<chemin>`).
