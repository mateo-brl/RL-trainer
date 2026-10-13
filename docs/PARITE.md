# Parité avec First Touch Trainer

RL Trainer reprend **toutes** les fonctions de [First Touch Trainer](https://bakkesplugins.com/plugin/476)
(AkumaRL, v1.0.5), plus celles que l'original annonçait sans les livrer. Le code de RL Trainer est entièrement
original : l'original n'a pas de licence, il a seulement servi à comprendre le comportement attendu.

Chaque ligne indique où trouver la fonction dans RL Trainer (onglet, réglage ou commande) et ce qui a été amélioré.

## Écart de vitesse (onglet Affichage, section « Écart de vitesse »)

| # | Fonction de l'original | Dans RL Trainer | Amélioration |
|---|---|---|---|
| P1 | Activer l'affichage | `rlt_ov_speed_enabled` (désactivé par défaut, comme l'original) | |
| P2 | Calculs XY, XYZ, « XY & Z » (ligne Z en petit), zone morte de ±1 uu/s | `rlt_ov_speed_mode` : XY, XYZ, XY + Z | Nouveau mode « Vitesse d'approche », vitesse au dernier contact (`rlt_ov_speed_last_touch`) |
| P3 | Unités KPH / MPH selon le réglage du jeu | `rlt_ov_speed_unit` : Auto (réglage du jeu), KPH, MPH, uu/s | Unité forçable |
| P4 | Zones bon / moyen / mauvais (150 / 300 uu/s) et couleurs | `rlt_ov_good_threshold`, `rlt_ov_alright_threshold`, `rlt_ov_good_color`, `rlt_ov_alright_color`, `rlt_ov_bad_color` | Seuils modifiables |
| P5 | Taille du texte 1..10, ombre, position X/Y, bouton de réinitialisation | `rlt_ov_text_scale`, `rlt_ov_shadow`, `rlt_ov_speed_x`, `rlt_ov_speed_y`, bouton « Réinitialiser » par section | Position en fraction d'écran : elle ne bouge plus quand tu changes de résolution |
| P6 | Masqué en ligne, affiché seulement en jeu | Le moteur coupe tout en ligne | |

## Chrono de session (onglet Affichage, section « Chrono de session »)

| # | Fonction de l'original | Dans RL Trainer | Amélioration |
|---|---|---|---|
| P7 | Chrono de session, position X/Y, couleur | `rlt_ov_timer_enabled`, `rlt_ov_timer_x`, `rlt_ov_timer_y`, `rlt_ov_timer_color`, commande `rlt overlay reset_timer` | Format h:mm:ss (l'original repartait à zéro chaque heure), pause respectée, remise à zéro au changement de map. Temps d'entraînement total et par jour dans l'onglet |

## Cercle de contact (onglet Affichage, section « Cercle de contact »)

| # | Fonction de l'original | Dans RL Trainer | Amélioration |
|---|---|---|---|
| P8 | Comportements Statique, Piloté par la vitesse, Garder en l'air | `rlt_ov_zone_enabled`, `rlt_ov_zone_behavior` | Mode Auto (passe en « piloté » sur les maps d'air dribble). Le cercle est posé sur la surface de la balle et ses mouvements sont lissés |
| P9 | Rayon 30..60 (47), épaisseur 1..5 (1) | `rlt_ov_zone_radius`, `rlt_ov_zone_thickness` | Inclinaison max, vitesses min/max et lissage en réglages avancés |
| P10 | Couleur, option « même couleur que l'écart » | `rlt_ov_zone_color`, `rlt_ov_zone_match` (modifier la couleur coupe l'option, comme l'original) | |
| P11 | Seulement balle en l'air (> 400 uu) et voiture sous la balle | `rlt_ov_only_air`, `rlt_ov_air_height`, `rlt_ov_car_below` | Chaque condition est une option. La partie cachée derrière la balle est masquée (`rlt_ov_zone_occlusion`) |
| P12 | Fonctionne dans les replays | Oui (option générale « Afficher dans les replays ») | |
| P13 | Freeplay, entraînement personnalisé | Oui, plus les maps workshop et les matchs hors ligne | |

## Sphère centrale (onglet Affichage, section « Sphère centrale »)

| # | Fonction de l'original | Dans RL Trainer | Amélioration |
|---|---|---|---|
| P14 | Sphère au centre de la balle, rayon 1..10 (5), couleur, même couleur que l'écart | `rlt_ov_sphere_enabled`, `rlt_ov_sphere_radius`, `rlt_ov_sphere_color`, `rlt_ov_sphere_match` | |

## Réglages et console

| # | Fonction de l'original | Dans RL Trainer | Amélioration |
|---|---|---|---|
| P15 | Panneau de réglages avec infobulles et boutons de réinitialisation | F2 > Plugins > RL Trainer, ou fenêtre `rlt_menu` | Valeurs en direct en haut de l'onglet, sections identiques à l'original |
| P16 | Réglages conservés | Enregistrés dans le `config.cfg` de BakkesMod | |
| P17 | Commande console | `rlt overlay speed` (écart actuel dans le chat), `rlt overlay reset_timer`, `rlt overlay preset_original` (remet tous les réglages de l'original) | |

## Fonctions annoncées par l'original mais jamais livrées

| # | Promesse de l'original | Dans RL Trainer |
|---|---|---|
| P18 | Sauvegarde de la configuration | Oui (P16), plus la progression dans `bakkesmod/data/rltrainer/progress.txt` |
| P19 | Cercle qui tourne contre la vitesse de la balle (air dribble qui monte) et dans le sens de l'élan | Comportements « Piloté par la vitesse » et « Garder en l'air », plus le mode Auto (P8) |
| P20 | Entraînement à viser le filet (lucarnes, poteaux bas, centre) | Onglet Tirs & flicks, mode Cibles : `rlt shooting targets <zone>` (zones : tl, tr, bl, br, center, random) |
| P21 | Log de l'écart de vitesse dans le chat à chaque touche et à chaque flip reset | Onglet Première touche : `rlt_ft_chat`, `rlt_ft_chat_flip`, `rlt_ft_chat_score` |
