#include "core/modules/mechanics/MechCatalog.h"

#include <cstring>
#include <iterator>

namespace rlt::mech {

namespace {

const MechInfo kMechs[] = {
    {Mech::SpeedFlip, "speedflip", "Speedflip", Focus::SpeedFlip},
    {Mech::HalfFlip, "halfflip", "Half-flip", Focus::HalfFlip},
    {Mech::FlipCancel, "flipcancel", "Flip cancel", Focus::FlipCancel},
    {Mech::Wavedash, "wavedash", "Wavedash", Focus::Wavedash},
    {Mech::ChainWavedash, "chaindash", "Chain wavedash", Focus::ChainDash},
    {Mech::ZapDash, "zapdash", "Zap dash", Focus::ZapDash},
    {Mech::WallDash, "walldash", "Wall dash", Focus::WallDash},
    {Mech::CeilingDash, "ceilingdash", "Ceiling dash", Focus::WallDash},
    {Mech::SpeedZap, "speedzap", "Speed zap", Focus::ZapDash},
    {Mech::HalfFlipWavedash, "halfflip_wavedash", "Half-flip + wavedash", Focus::HalfFlip},
    {Mech::FastAerial, "fastaerial", "Fast aerial", Focus::FastAerial},
    {Mech::DoubleJumpAerial, "doublejump_aerial", "Double jump aerial", Focus::FastAerial},
    {Mech::Stall, "stall", "Stall", Focus::Stall},
    {Mech::AirSpin, "helicopter", "Hélico / tornado", Focus::Helicopter},
    {Mech::Recovery, "recovery", "Réception", Focus::Recovery},
    {Mech::Powerslide, "powerslide", "Powerslide", Focus::Powerslide},
    {Mech::BoostEfficiency, "boost", "Gestion du boost", Focus::Boost},
    {Mech::WallChain, "walldash_chain", "Wall dash en chaîne", Focus::WallDash},
};

// Default progression text: the level only narrows the margins.
constexpr const char* kProgressMargins =
    "Le niveau monte quand tu réussis environ 3 essais sur 4 : les marges se resserrent et le score demandé monte.";

const FocusInfo kFocus[] = {
    {Focus::SpeedFlip, "speedflip", "Speedflip", "Flips",
     "Flip en diagonale que tu arrêtes tout de suite : la voiture reste à plat et file vers la balle. Le départ des bons kickoffs.",
     "Clé : petit saut, flip en diagonale vers l'avant juste après, puis stick tiré vers toi tout de suite.",
     Mech::SpeedFlip, Tier::Advanced, "Petit saut, flip en diagonale, stick vers toi",
     {"Boost et accélérateur", "Tourne un peu vers la balle", "Petit saut", "Flip en diagonale",
      "Stick vers toi tout de suite", "Roues au sol, fonce"},
     "Flip visé à 30° du nez (accepté de 5 à 58° au niveau 1, à 8° près au niveau 10). Flip au plus tard 0,3 s après "
     "le saut au niveau 1, 0,12 s au niveau 10. Annulation : stick tiré en moins de 0,17 s au niveau 1, 0,05 s au "
     "niveau 10. Aux niveaux 4 et 5, passer à moins de 150 puis 80 uu de la balle suffit. Décompte 3 s puis 2 s entre "
     "les essais, boost 33 limité. Temps de référence jusqu'à la balle depuis la diagonale : 2,40 s (débutant), 2,07 s "
     "(standard), 1,95 s (pro).",
     "Niveaux 1 à 3 : le flip diagonal annulé seul, sans balle ni timing de kickoff, avec des marges larges. "
     "Niveaux 4 à 6 : le vrai kickoff depuis les diagonales (aux niveaux 4 et 5, passer tout près de la balle suffit). "
     "Niveaux 7 à 10 : toutes les positions de kickoff, marges serrées et temps jusqu'à la balle."},
    {Focus::HalfFlip, "halfflip", "Half-flip", "Flips",
     "Demi-tour express : flip arrière, arrêté à mi-chemin, puis air roll pour te remettre à l'endroit.",
     "Clé : pousse le stick vers l'avant quand la voiture est à la verticale, puis air roll jusqu'aux roues.",
     Mech::HalfFlip, Tier::Intermediate, "Flip arrière, stick en avant, air roll",
     {"Recule vers la balle", "Saute, puis flip arrière", "Nez en l'air : stick vers l'avant", "Air roll, roues au sol",
      "Fonce sur la balle"},
     "Annulation idéale entre 0,12 et 0,25 s après le flip (acceptée de 0,03 à 0,4 s au niveau 1). Flip au plus tard "
     "0,17 s après le saut. Demi-tour d'au moins 120° au niveau 1, 155° au niveau 10. Recul de 11 à 40 km/h (300 à "
     "1100 uu/s) selon le niveau. Au niveau 10, la balle doit être touchée moins de 1,9 s après le GO.",
     kProgressMargins},
    {Focus::FlipCancel, "flipcancel", "Flip cancel", "Flips",
     "Tu lances un flip avant et tu l'arrêtes tout de suite en tirant le stick vers toi. C'est la base du speedflip et "
     "du half-flip.",
     "Clé : stick à fond dans le sens opposé au flip, tout de suite après l'avoir lancé.", Mech::FlipCancel,
     Tier::Intermediate, "Flip avant, stick vers toi tout de suite",
     {"Roule tout droit", "Petit saut", "Flip avant", "Stick vers toi tout de suite"},
     "Annulation en moins de 0,17 s au niveau 1, 0,05 s au niveau 10 ; stick tiré à 50 % puis 90 %. Décompte 2 s. "
     "Vitesse de départ 29 à 50 km/h (800 à 1400 uu/s).",
     kProgressMargins},
    {Focus::Wavedash, "wavedash", "Wavedash", "Sol",
     "Flip lancé juste avant que les roues touchent le sol : la voiture garde la poussée sans faire de roulade.",
     "Clé : nez un peu levé, flip avant quand les roues arrière effleurent le sol.", Mech::Wavedash,
     Tier::Intermediate, "Nez un peu levé, flip juste avant le sol",
     {"Lève un peu le nez", "Au ras du sol : flip avant", "Garde l'accélérateur"},
     "Nez entre -3 et 25° au contact (accepté de -15 à 40° au niveau 1). Flip quand les roues arrière sont à moins de "
     "50 uu du sol ; roues au sol au plus tard 0,17 s après le flip au niveau 1, 0,08 s au niveau 10. Chute de 3,5 m "
     "(contact 1 s après le GO) au niveau 1, 2 m (0,6 s) au niveau 10. Gain d'environ 18 km/h (500 uu/s).",
     kProgressMargins},
    {Focus::ChainDash, "chaindash", "Chain wavedash", "Sol",
     "Des wavedashes enchaînés sans pause : chacun ajoute de la vitesse, jusqu'au supersonique sans boost.",
     "Clé : ressaute dès que les roues touchent, powerslide gardé, sans boost.", Mech::ChainWavedash,
     Tier::Advanced, "Saute, dash, ressaute, sans boost",
     {"Petit saut, nez un peu levé", "Flip avant au ras du sol", "Powerslide gardé, ressaute", "Pas de boost"},
     "Pause au sol de 0,2 s maximum au niveau 1, 0,08 s au niveau 10. Niveau 1 : 2 dashes et 54 km/h (1500 uu/s) ; "
     "niveau 10 : 4 dashes, supersonique (79 km/h, 2200 uu/s), zéro boost. Départ à 36 km/h (1000 uu/s) au niveau 1. "
     "L'essai s'arrête avant le mur du fond.",
     "Le nombre de dashes demandé et la vitesse à atteindre montent avec le niveau, la vitesse de départ et le temps "
     "disponible baissent."},
    {Focus::ZapDash, "zapdash", "Zap dash", "Sol",
     "Un wavedash sans saut juste avant : tu retombes le nez un peu vers le bas et tu flippes au petit rebond.",
     "Clé : atterris le nez légèrement baissé, puis flip avant dès le rebond, sans sauter.", Mech::ZapDash,
     Tier::Advanced, "Nez bas, rebond, flip avant",
     {"Nez un peu vers le bas", "Au rebond : flip avant", "Garde l'accélérateur"},
     "Nez entre -20 et -3° au contact. Le rebond doit arriver moins de 0,17 s après la réception, le flip pendant ce "
     "rebond. Chute de 3 m (contact 0,8 s après le GO) au niveau 1, 1,4 m (0,34 s) au niveau 10. Conseillé après "
     "Wavedash niveau 5. La détection du rebond n'est pas encore vérifiée en jeu.",
     kProgressMargins},
    {Focus::WallDash, "walldash", "Wall dash", "Mur",
     "Des wavedashes sur le mur : tu te décolles à peine et tu flippes aussitôt, tu accélères sans boost.",
     "Clé : deux appuis sur saut très rapprochés, flip en diagonale vers l'avant, côté mur.", Mech::WallDash,
     Tier::Advanced, "Tape saut, flip côté mur, recommence",
     {"Monte sur le mur", "Longe le mur sans monter ni descendre", "Tape saut", "Flip vers l'avant, côté mur",
      "Recommence"},
     "Deux appuis sur saut en moins de 0,25 s au niveau 1, 0,09 s au niveau 10 ; premier appui tenu moins de 0,09 s ; "
     "roues sur le mur au plus 0,17 s après le flip. Un wall dash au niveau 1, trois à la suite au niveau 10. "
     "Hauteur de départ 7 m au niveau 1, 5 m au niveau 10.",
     kProgressMargins},
    {Focus::FastAerial, "fastaerial", "Fast aerial", "Air",
     "Décoller le plus vite possible pour aller chercher une balle haute.",
     "Clé : premier saut gardé un court instant avec boost et stick tiré, puis double saut tout de suite.",
     Mech::FastAerial, Tier::Intermediate, "Saut tenu, double saut, boost",
     {"Saut tenu, boost, stick vers toi", "Lâche, rappuie, stick au centre", "Vise la balle"},
     "Premier saut tenu 0,2 s, deuxième appui moins de 0,075 s après avoir lâché, boost dès le premier saut. Niveau 1 : "
     "balle immobile à 14 m devant toi et 8 m de haut, à toucher en moins de 3,5 s. Niveau 5 : 11 m devant, 11 m de "
     "haut, en moins de 2,5 s. Niveaux 6 à 10 : balle lâchée de 15 m de haut, à toucher avant qu'elle passe sous 5 m "
     "(7 m au niveau 10).",
     "Niveaux 1 à 5 : la balle t'attend en l'air, de plus en plus haut, et il faut l'atteindre de plus en plus vite. "
     "Niveaux 6 à 10 : la balle tombe dès le GO."},
    {Focus::Stall, "stall", "Stall", "Air",
     "La voiture s'arrête de tomber et flotte un instant : tu utilises ton flip avec des commandes qui s'annulent.",
     "Clé : stick à fond d'un côté, air roll à fond de l'autre, puis saut.", Mech::Stall, Tier::Intermediate,
     "Stick d'un côté, air roll de l'autre, saut",
     {"Stick d'un côté, air roll de l'autre", "Saut", "Laisse flotter"},
     "Stick et air roll à 40 % ou plus chacun, en sens opposés, haut et bas du stick sous 10 %. Le flip reste "
     "disponible 1,25 s après le saut. Flottement de 0,07 s au niveau 1, 0,33 s au niveau 10. Départ à 10 m de haut au "
     "niveau 1, 5 m au niveau 10.",
     kProgressMargins},
    {Focus::Helicopter, "helicopter", "Hélico / tornado", "Air",
     "Faire tourner la voiture sur elle-même en l'air, comme un hélicoptère, de façon contrôlée.",
     "Clé : air roll et stick du même côté, à fond, sans bouger.", Mech::AirSpin, Tier::Beginner,
     "Air roll et stick du même côté",
     {"Air roll d'un côté", "Stick du même côté", "Garde, puis roues au sol"},
     "Rotation d'au moins 0,5 s au niveau 1, 1,25 s au niveau 10 ; stabilité de l'axe 0,5 puis 0,85 ; vitesse de "
     "rotation 1,5 puis 3,5 rad/s. Aux niveaux 8 à 10, il faut aussi retomber sur les roues.",
     kProgressMargins},
    {Focus::Recovery, "recovery", "Réception", "Air",
     "Retomber sur les roues après avoir été projeté, dans le sens où tu avances, pour repartir tout de suite.",
     "Clé : roues vers le sol et nez dans le sens du mouvement avant de toucher.", Mech::Recovery, Tier::Beginner,
     "Roues en bas, nez devant",
     {"Roues vers le sol", "Nez dans le sens du mouvement", "Lâche tout, accélère"},
     "Inclinaison au contact de 45° maximum au niveau 1, 15° au niveau 10 ; vitesse gardée 40 % puis 85 % ; nez "
     "aligné à 30° près au niveau 10. Décompte 1,5 s pour lire la position de la voiture.",
     "Le lancer devient plus penché, plus rapide et plus tournant avec le niveau, et il faut garder plus de vitesse."},
    {Focus::Powerslide, "powerslide", "Powerslide", "Sol",
     "Un virage serré en glissant, pour te tourner vite vers la balle sans casser ta vitesse.",
     "Clé : un court appui sur powerslide pendant le virage, relâche dès que le nez vise la balle.", Mech::Powerslide,
     Tier::Beginner, "Tourne, glisse, relâche face à la balle",
     {"Tourne vers la balle", "Powerslide court", "Relâche, nez sur la balle", "Touche la balle"},
     "Glissade d'au moins 0,03 s et 20° de virage. Visée à 25° près au niveau 1, 10° au niveau 10, mesurée quand la "
     "voiture ne glisse plus. Vitesse gardée 50 % puis 85 %. Dérapage en sortie 40° puis 12° maximum. Balle à "
     "toucher moins de 3 s (niveau 1) puis 2 s (niveau 10) après le virage.",
     kProgressMargins},
    {Focus::Boost, "boost", "Gestion du boost", "Boost",
     "Aller le plus vite possible (supersonique) en gaspillant le moins de boost.",
     "Clé : accélérateur toujours appuyé, boost jusqu'au max, puis lâche-le.", Mech::BoostEfficiency, Tier::Beginner,
     "Boost jusqu'au max, puis lâche",
     {"Accélérateur toujours", "Boost jusqu'au max", "Lâche le boost au max"},
     "Supersonique à partir de 79 km/h (2200 uu/s), gardé au-dessus de 76 km/h (2100 uu/s). Départ à 58 km/h "
     "(1600 uu/s) au niveau 1, 52 km/h au niveau 10 ; il faut environ 20 à 25 de boost pour y arriver ; marge de 15 "
     "puis 4 ; boost gaspillé à la vitesse max 5 puis 1,5 au plus. Supersonique en 3 s au niveau 1, 2 s au niveau 10.",
     "Avec le niveau, tu pars moins vite, il faut devenir supersonique plus vite et gaspiller moins de boost."},
};

// Tutorials (spec "tutorialFr"). Placeholders: {jump}, {boost}, {slide}, {airroll}, {throttle}.
const Tutorial kTutorials[] = {
    {Focus::SpeedFlip,
     "Un flip en diagonale que tu arrêtes tout de suite. La voiture reste à plat et file vers la balle plus vite "
     "qu'avec un flip normal. C'est le départ des bons kickoffs.",
     {{"Pendant le décompte, garde l'accélérateur et {boost} appuyés.", "La voiture part à fond dès le GO.",
       "La voiture bondit au GO et ta jauge de boost descend."},
      {"Au tout début, tourne un tout petit peu vers la balle.",
       "Sur les diagonales, la voiture ne vise pas pile la balle au départ, et en l'air tu ne peux plus corriger.",
       "Ton capot pointe sur la balle avant de sauter."},
      {"Saute avec un appui court sur {jump}.", "Un petit saut garde la voiture près du sol, tu retombes vite.",
       "Les roues décollent à peine."},
      {"Tout de suite après, flip vers l'avant en penchant un peu le stick sur le côté (deuxième appui sur {jump}).",
       "Le flip te pousse vers l'avant, le côté t'aide à garder le nez droit juste après.",
       "La voiture part en vrille de biais, pas en roulade avant."},
      {"Aussitôt, tire le stick à fond vers toi et garde-le.",
       "Ça arrête la roulade, la voiture reste à plat au lieu de se retourner.", "Le nez ne plonge pas vers le sol."},
      {"Si la voiture penche, remets les roues vers le sol avec {airroll}, puis garde {boost} et l'accélérateur jusqu'à "
       "la balle.",
       "Retomber sur les roues et dans l'axe garde toute ta vitesse.",
       "Tu retombes à plat, le nez vers la balle, et tu la touches."}},
     "Un vrai kickoff sans aide : position tirée au hasard, boost limité, chaque étape est vérifiée dans l'ordre, du "
     "boost au GO jusqu'à la touche.",
     "Niveaux 1 à 3 : pas de balle. Tu fais seulement petit saut, flip en diagonale, stick vers toi, et tu retombes sur "
     "les roues. Aux niveaux 4 et 5, passer tout près de la balle suffit. Ensuite il faut la toucher, puis viennent "
     "toutes les positions de kickoff.",
     {"Boost et accélérateur", "Tourne un peu vers la balle", "Petit saut", "Flip en diagonale",
      "Stick vers toi tout de suite", "Roues au sol, fonce", nullptr}},
    {Focus::HalfFlip,
     "Un demi-tour express. Tu recules, tu fais un flip arrière, tu l'arrêtes à mi-chemin et tu te remets à l'endroit "
     "avec l'air roll. Tu repars dans l'autre sens sans perdre de temps.",
     {{"Recule vers la balle qui est derrière toi.", "Le flip arrière te lance dans le sens où tu recules, donc vers la balle.",
       "La voiture recule bien droit."},
      {"Saute, puis flip arrière : stick tiré vers toi et nouvel appui sur {jump}.",
       "Le flip arrière te pousse vers la balle et fait basculer la voiture.", "Le nez monte vers le ciel."},
      {"Quand la voiture est debout, nez vers le haut, pousse le stick à fond vers l'avant et garde-le.",
       "Ça arrête la bascule au moment où la voiture regarde déjà vers la balle.",
       "La voiture ne finit pas sa roulade et reste toit vers le sol."},
      {"Fais {airroll} à gauche ou à droite pour remettre les roues vers le sol.",
       "Tu tournes sur le côté pour te remettre à l'endroit, nez vers la balle.",
       "Tu retombes sur les roues, capot vers la balle."},
      {"Accélère et boost vers la balle.", "C'est le but du demi-tour.", "Tu la touches."},
      {nullptr, nullptr, nullptr}},
     "La voiture est arrêtée, capot vers ton but, la balle derrière toi : c'est toi qui recules avant le half-flip.",
     "La voiture recule doucement, la balle est loin, tu as plus de temps, et un atterrissage un peu de travers est "
     "accepté.",
     {"Recule vers la balle", "Saute, puis flip arrière", "Nez en l'air : stick vers l'avant", "Air roll, roues au sol",
      "Fonce sur la balle", nullptr, nullptr}},
    {Focus::FlipCancel,
     "Tu lances un flip avant et tu l'arrêtes tout de suite en tirant le stick vers toi. Tu gardes la poussée du flip "
     "sans faire la roulade. C'est la base du speedflip et du half-flip.",
     {{"Roule tout droit, accélérateur appuyé.", "Le flip pousse mieux quand tu avances déjà.", "La voiture va droit."},
      {"Saute avec un appui court sur {jump}.", "Un petit saut suffit, tu restes près du sol.", "Les roues décollent à peine."},
      {"Flip avant : stick vers l'avant et nouvel appui sur {jump}.", "C'est le flip que tu vas arrêter.",
       "Le nez commence à plonger."},
      {"Tout de suite, tire le stick à fond vers toi et garde-le jusqu'au sol.", "Ça bloque la roulade.",
       "La voiture reste presque à plat et retombe sur les roues."},
      {nullptr, nullptr, nullptr},
      {nullptr, nullptr, nullptr}},
     "La voiture est arrêtée : tu prends de la vitesse toi-même avant de sauter.",
     "Tu as bien plus de temps pour tirer le stick, et un stick tiré à moitié est accepté.",
     {"Roule tout droit", "Petit saut", "Flip avant", "Stick vers toi tout de suite", nullptr, nullptr, nullptr}},
    {Focus::Wavedash,
     "Un flip lancé juste avant que les roues touchent le sol. Les roues touchent pendant le flip : la voiture ne fait "
     "pas de roulade mais garde la poussée. Tu gagnes de la vitesse sans boost.",
     {{"Pendant la chute, tire un peu le stick vers toi pour lever légèrement le nez.",
       "Les roues arrière touchent en premier, la voiture ne s'écrase pas sur le nez.",
       "Tu vois le capot monter un peu, pas plus."},
      {"Juste avant que les roues touchent, flip avant : stick vers l'avant et {jump}.",
       "Trop tôt, tu fais un vrai flip en l'air ; trop tard, les roues sont déjà au sol et tu fais juste un saut.",
       "La voiture glisse d'un coup vers l'avant et accélère, sans roulade."},
      {"Garde l'accélérateur.", "Tu gardes la vitesse gagnée.", "Ta vitesse monte."},
      {nullptr, nullptr, nullptr},
      {nullptr, nullptr, nullptr},
      {nullptr, nullptr, nullptr}},
     "Depuis le sol, fais d'abord un petit saut (appui court sur {jump}), puis les étapes 1 et 2 pendant la redescente.",
     "La voiture est lâchée plus haut, tu as deux fois plus de temps, et « Maintenant ! » s'affiche au bon moment.",
     {"Lève un peu le nez", "Au ras du sol : flip avant", "Garde l'accélérateur", nullptr, nullptr, nullptr, nullptr}},
    {Focus::ChainDash,
     "Des wavedashes enchaînés sans pause. Chacun ajoute de la vitesse, jusqu'au supersonique sans boost.",
     {{"Fais un wavedash propre (voir l'exercice Wavedash).", "Chaque maillon de la chaîne est un wavedash.",
       "La voiture accélère d'un coup."},
      {"Garde {slide} appuyé pendant toute la chaîne.", "À chaque réception, les pneus ne freinent pas la voiture.",
       "La voiture glisse un peu au contact au lieu d'accrocher."},
      {"Dès que les roues retouchent, ressaute tout de suite.", "Chaque instant au sol te fait perdre de la vitesse.",
       "Les dashes s'enchaînent sans pause."},
      {"Ne touche pas au boost.", "L'exercice mesure la vitesse gagnée sans boost.", "Ta jauge ne bouge pas."},
      {nullptr, nullptr, nullptr},
      {nullptr, nullptr, nullptr}},
     "La voiture est arrêtée : tu prends de la vitesse sans boost, puis tu enchaînes les wavedashes jusqu'au bout du "
     "terrain.",
     "Deux wavedashes à la suite suffisent, une petite pause entre les deux est acceptée, un peu de boost est toléré.",
     {"Petit saut, nez un peu levé", "Flip avant au ras du sol", "Powerslide gardé, ressaute", "Pas de boost", nullptr,
      nullptr, nullptr}},
    {Focus::ZapDash,
     "Un wavedash sans saut juste avant. Tu retombes le nez un peu vers le bas, la voiture rebondit un tout petit peu, "
     "et tu lances le flip pendant ce rebond.",
     {{"Pendant la chute, garde le nez un peu vers le bas (stick un poil vers l'avant).",
       "L'avant touche en premier et la voiture rebondit légèrement.", "Tu vois les roues avant toucher d'abord."},
      {"Au rebond, flip avant (stick vers l'avant et {jump}), sans avoir sauté avant.",
       "La voiture est encore en l'air un court instant, ton appui devient un flip qui touche le sol tout de suite.",
       "La voiture accélère d'un coup sans décoller."},
      {"Garde l'accélérateur.", "Tu gardes la vitesse gagnée.", "Ta vitesse monte."},
      {nullptr, nullptr, nullptr},
      {nullptr, nullptr, nullptr},
      {nullptr, nullptr, nullptr}},
     "Un petit saut depuis le sol, le nez un peu vers le bas à la retombée, puis le flip au rebond.",
     "La chute est plus haute, tu as plus de temps ; un petit gain de vitesse suffit. Commence cet exercice après "
     "Wavedash niveau 5.",
     {"Nez un peu vers le bas", "Au rebond : flip avant", "Garde l'accélérateur", nullptr, nullptr, nullptr, nullptr}},
    {Focus::WallDash,
     "Des wavedashes sur le mur. Tu te décolles à peine du mur et tu flippes aussitôt : les roues le retouchent tout de "
     "suite et tu accélères sans boost.",
     {{"Roule vers le mur et monte dessus.", "Il faut rouler sur le mur, roues contre la paroi.", "La voiture roule sur le mur."},
      {"Longe le mur, assez bas, sans monter ni descendre.", "À cette hauteur, tu as la place d'enchaîner sans tomber.",
       "Tu avances le long du mur sans descendre vite."},
      {"Tape {jump} très vite, sans le garder.", "Un saut long t'éloigne du mur.", "La voiture se décolle à peine."},
      {"Tout de suite, flip en diagonale vers l'avant, côté mur.",
       "Le flip ramène les roues contre le mur et te pousse vers l'avant.",
       "Les roues retouchent le mur presque aussitôt et tu vas plus vite."},
      {"Recommence dès que les roues retouchent le mur.", "Chaque wall dash ajoute de la vitesse.",
       "Tu enchaînes sans tomber du mur."},
      {nullptr, nullptr, nullptr}},
     "La voiture part du sol : tu montes toi-même sur le mur, tu le longes, puis tu fais tes wall dashes.",
     "La voiture est posée plus haut sur le mur, un seul wall dash suffit, et tes deux appuis peuvent être un peu plus "
     "espacés.",
     {"Monte sur le mur", "Longe le mur sans monter ni descendre", "Tape saut", "Flip vers l'avant, côté mur", "Recommence",
      nullptr, nullptr}},
    {Focus::FastAerial,
     "Décoller le plus vite possible pour aller chercher une balle haute.",
     {{"Saute en gardant {jump} appuyé, appuie sur {boost} en même temps et tire le stick vers toi.",
       "Garder le saut pousse la voiture plus haut, tirer le stick lève le nez vers le ciel.",
       "La voiture décolle en se cabrant."},
      {"Lâche le saut puis rappuie tout de suite, stick au centre.",
       "Le deuxième saut te redonne de la hauteur ; si le stick est penché, tu fais un flip et tu perds tout.",
       "La voiture monte encore d'un coup, sans tourner."},
      {"Garde {boost} et vise la balle avec le stick.", "Le boost pousse là où pointe ton nez.", "Tu touches la balle."},
      {nullptr, nullptr, nullptr},
      {nullptr, nullptr, nullptr},
      {nullptr, nullptr, nullptr}},
     "La balle part du centre en cloche vers toi. Attends un peu, place-toi sous sa trajectoire, puis décolle.",
     "La balle reste immobile en l'air jusqu'à ce que tu la touches. Tu as le temps.",
     {"Saut tenu, boost, stick vers toi", "Lâche, rappuie, stick au centre", "Vise la balle", nullptr, nullptr, nullptr,
      nullptr}},
    {Focus::Stall,
     "La voiture s'arrête de tomber et flotte un instant. Tu utilises ton flip, mais avec des commandes qui s'annulent : "
     "pas de roulade.",
     {{"Mets le stick à fond d'un côté.", "C'est la première moitié de la commande qui s'annule.",
       "La voiture commence à tourner de ce côté."},
      {"Mets {airroll} à fond de l'autre côté (par exemple stick à droite et air roll à gauche).",
       "Les deux s'annulent, le flip n'a plus de direction.", "La voiture tourne à peine."},
      {"Appuie sur {jump} sans pousser le stick en haut ni en bas.", "Au lieu d'un flip, la voiture s'arrête net en l'air.",
       "La voiture flotte au lieu de tomber."},
      {"Lâche tout et laisse flotter.", "Bouger le stick relance la chute.", "La barre de flottement se remplit."},
      {nullptr, nullptr, nullptr},
      {nullptr, nullptr, nullptr}},
     "Saute depuis le sol (un seul saut, garde {jump} pour monter plus haut), puis fais les étapes 1 à 3 en haut du "
     "saut, avant que ton flip disparaisse.",
     "La voiture est lâchée de plus haut, tu as plus de temps, et un stall court suffit.",
     {"Stick d'un côté, air roll de l'autre", "Saut", "Laisse flotter", nullptr, nullptr, nullptr, nullptr}},
    {Focus::Helicopter,
     "Faire tourner la voiture sur elle-même en l'air, comme un hélicoptère, de façon contrôlée.",
     {{"Fais {airroll} à fond d'un côté.", "C'est la moitié de la rotation.", "La voiture tourne autour de son nez."},
      {"Mets le stick à fond du même côté (gauche avec gauche, droite avec droite).",
       "La voiture tourne alors autour d'un axe stable.", "Elle tourne régulièrement, sans tanguer."},
      {"Garde les deux sans bouger.", "La rotation doit durer.", "La barre de durée se remplit."},
      {"Lâche avant le sol et remets les roues vers le bas.", "Retomber sur le toit te bloque.", "Tu retombes sur les roues."},
      {nullptr, nullptr, nullptr},
      {nullptr, nullptr, nullptr}},
     "Décolle toi-même (saut puis double saut, ou boost), tourne, puis retombe sur les roues.",
     "Une demi-seconde de rotation suffit, une rotation un peu irrégulière est acceptée.",
     {"Air roll d'un côté", "Stick du même côté", "Garde, puis roues au sol", nullptr, nullptr, nullptr, nullptr}},
    {Focus::Recovery,
     "Retomber sur les roues après avoir été projeté, dans le sens où tu avances, pour repartir tout de suite.",
     {{"Regarde où part la voiture.", "C'est dans cette direction que ton nez doit pointer à l'atterrissage.",
       "Tu sais vers où tu tombes."},
      {"Remets les roues vers le sol avec {airroll} et le stick.", "Atterrir sur le toit ou sur le côté te bloque.",
       "Tu vois le sol sous les roues."},
      {"Tourne le nez dans le sens du mouvement.", "Si le nez va dans le même sens que la voiture, tu ne perds pas de vitesse.",
       "La voiture ne dérape pas en touchant le sol."},
      {"Lâche les commandes juste avant de toucher, puis accélère. Bonus : s'il te reste ton flip, fais un wavedash à "
       "l'atterrissage.",
       "Une voiture qui tourne encore au contact rebondit.", "Tu repars aussitôt."},
      {nullptr, nullptr, nullptr},
      {nullptr, nullptr, nullptr}},
     "Monte sur le mur, saute du mur, puis rattrape-toi pour retomber sur les roues.",
     "Lancers doux, voiture peu penchée et sans rotation ; un atterrissage un peu penché est accepté.",
     {"Roues vers le sol", "Nez dans le sens du mouvement", "Lâche tout, accélère", nullptr, nullptr, nullptr, nullptr}},
    {Focus::Powerslide,
     "Un virage serré en glissant, pour te tourner vite vers la balle sans casser ta vitesse.",
     {{"Commence à tourner vers la balle.", "La glissade amplifie un virage déjà commencé.",
       "Ta voiture tourne du côté de la balle."},
      {"Appuie sur {slide} un court instant pendant le virage.", "La voiture pivote beaucoup plus vite.", "L'arrière glisse."},
      {"Relâche dès que ton nez vise la balle.", "Garder le bouton trop longtemps fait déraper et ralentir.",
       "La voiture repart droit vers la balle."},
      {"Fonce et touche la balle.", "C'est le but du virage.", "Tu la touches."},
      {nullptr, nullptr, nullptr},
      {nullptr, nullptr, nullptr}},
     "Pars à l'arrêt, prends de la vitesse, puis fais le virage.",
     "Tu vas moins vite, la balle est plus loin sur le côté, et tu as plus de marge sur la visée et la vitesse perdue.",
     {"Tourne vers la balle", "Powerslide court", "Relâche, nez sur la balle", "Touche la balle", nullptr, nullptr,
      nullptr}},
    {Focus::Boost,
     "Aller le plus vite possible (supersonique : la traînée derrière ta voiture) en gaspillant le moins de boost.",
     {{"Garde l'accélérateur appuyé tout le temps.", "L'accélérateur seul garde ta vitesse, même au maximum.",
       "Ta vitesse ne baisse pas quand tu lâches le boost."},
      {"Appuie sur {boost} jusqu'à la vitesse maximale.", "Seul le boost t'emmène au supersonique.",
       "La traînée supersonique apparaît."},
      {"Lâche le boost dès que tu es au maximum.", "Au maximum, le boost ne t'apporte plus rien, il se vide pour rien.",
       "Ta jauge s'arrête de descendre et tu restes supersonique."},
      {"Si tu ralentis (virage, saut), donne seulement de petits coups de boost.",
       "Un petit coup suffit pour revenir au maximum.", "La traînée revient sans que ta jauge fonde."},
      {nullptr, nullptr, nullptr},
      {nullptr, nullptr, nullptr}},
     "Pars à l'arrêt, accélérateur seul d'abord, boost ensuite, puis lâche-le au maximum.",
     "Tu pars presque à la vitesse maximale et plus de boost est toléré.",
     {"Accélérateur toujours", "Boost jusqu'au max", "Lâche le boost au max", nullptr, nullptr, nullptr, nullptr}},
};
static_assert(std::size(kTutorials) == static_cast<size_t>(kFocusCount), "one Tutorial per Focus, in enum order");

const std::vector<PathEntry> kPath = {
    {Focus::Powerslide, "Powerslide", "Conduite (Débutant)", "", ""},
    {Focus::Boost, "Gestion du boost", "Conduite (Débutant)", "", ""},
    {Focus::Recovery, "Réception", "Conduite (Débutant)", "", ""},
    {Focus::Helicopter, "Hélico", "Flips (Intermédiaire)", "", ""},
    {Focus::FlipCancel, "Flip cancel", "Flips (Intermédiaire)", "La base du speedflip et du half-flip.", ""},
    {Focus::SpeedFlip, "Speedflip étape 1, sans balle (niveaux 1 à 3)", "Flips (Intermédiaire)", "", ""},
    {Focus::HalfFlip, "Half-flip", "Flips (Intermédiaire)", "", ""},
    {Focus::FastAerial, "Fast aerial", "En l'air", "", ""},
    {Focus::Wavedash, "Wavedash", "Timing (Intermédiaire)", "", ""},
    {Focus::Stall, "Stall", "Timing (Intermédiaire)", "", ""},
    {Focus::SpeedFlip, "Speedflip au kickoff (étapes 2 et 3)", "Timing (Intermédiaire)", "Commence au niveau 4.",
     "kickoff"},
    {Focus::ChainDash, "Chain wavedash", "Avancé", "", ""},
    {Focus::ZapDash, "Zap dash", "Avancé", "Après Wavedash niveau 5.", ""},
    {Focus::WallDash, "Wall dash", "Avancé", "", ""},
};

static_assert(std::size(kMechs) == static_cast<size_t>(kMechCount), "one MechInfo per Mech, in enum order");
static_assert(std::size(kFocus) == static_cast<size_t>(kFocusCount), "one FocusInfo per Focus, in enum order");

struct Token {
    const char* from;
    const char* to;
};

// Whole dashed groups first (word order and hyphens differ in French), then single tokens.
const Token kPhrases[] = {
    {"roll-left", "air roll à gauche"},    {"roll-right", "air roll à droite"},
    {"airroll-left", "air roll à gauche"}, {"airroll-right", "air roll à droite"},
    {"no-roll", "sans air roll"},          {"no-flip", "sans flip"},
    {"u-turn", "demi-tour"},               {"double-jump", "double saut"},
};

const Token kTokens[] = {
    {"left", "gauche"},       {"right", "droite"},     {"forward", "avant"},   {"back", "arrière"},
    {"side", "côté"},         {"diag", "diag."},       {"kickoff", "kickoff"}, {"wall", "mur"},
    {"floor", "sol"},         {"ceiling", "plafond"},  {"roll", "air roll"},   {"airroll", "air roll"},
    {"no", "sans"},           {"late", "trop tard"},   {"fast", "rapide"},     {"double", "double"},
    {"jump", "saut"},         {"helicopter", "hélico"}, {"tornado", "tornado"}, {"landing", "atterrissage"},
    {"wavedash", "wavedash"}, {"cut", "virage"},       {"window", "fenêtre"},  {"straight", "droit"},
    {"flip", "flip"},         {"nostick", "stick au centre"}, {"stall", "stall"}, {"drill", "exercice"},
    {"crash", "sur le toit"},
};

std::string translateToken(const std::string& t) {
    for (const Token& k : kTokens)
        if (t == k.from) return k.to;
    return t;
}

std::string translateDashed(const std::string& part) {
    for (const Token& k : kPhrases)
        if (part == k.from) return k.to;
    // A phrase at the start of a longer group ("roll-right-..."): translate it, then the rest.
    for (const Token& k : kPhrases) {
        const size_t n = std::strlen(k.from);
        if (part.size() > n + 1 && part.compare(0, n, k.from) == 0 && part[n] == '-')
            return std::string(k.to) + " " + translateDashed(part.substr(n + 1));
    }
    std::string out, tok;
    auto flush = [&]() {
        if (tok.empty()) return;
        if (!out.empty()) out += ' ';
        out += translateToken(tok);
        tok.clear();
    };
    for (char c : part) {
        if (c == '-') flush();
        else tok += c;
    }
    flush();
    return out;
}

} // namespace

const char* tierLabel(Tier t) {
    switch (t) {
    case Tier::Beginner: return "Débutant";
    case Tier::Intermediate: return "Intermédiaire";
    case Tier::Advanced: return "Avancé";
    }
    return "";
}

const char* tierAdvice() {
    return "Débutant : commence par Powerslide, Gestion du boost et Réception, puis Hélico et Flip cancel avant le "
           "Speedflip (étape 1 sans balle) et le Half-flip. Suis le parcours conseillé ci-dessous.";
}

const Tutorial& tutorial(Focus f) {
    const int i = index(f);
    return kTutorials[(i >= 0 && i < kFocusCount) ? i : 0];
}

const std::vector<PathEntry>& mechanicsPath() { return kPath; }

int pathIndex(Focus f) {
    for (size_t i = 0; i < kPath.size(); ++i)
        if (kPath[i].focus == f) return static_cast<int>(i);
    return static_cast<int>(kPath.size());
}

const char* pathRule() {
    return "Passe à l'exercice suivant quand tu réussis environ 3 essais sur 4 au niveau 4. Tu peux toujours revenir en "
           "arrière.";
}

const char* lexicon() {
    return "Flip : un saut, puis un deuxième appui sur saut avec le stick penché. La voiture fait une roulade et prend "
           "de la vitesse. Annuler un flip : tirer le stick dans l'autre sens juste après le flip pour arrêter la "
           "roulade. Air roll : faire tourner la voiture sur elle-même autour de son nez, en l'air (souvent le même "
           "bouton que le powerslide). Powerslide : la glissade au sol. Supersonique : la vitesse maximale, quand une "
           "traînée apparaît derrière ta voiture. Kickoff : l'engagement au centre.";
}

const MechInfo& mechInfo(Mech m) {
    const int i = index(m);
    return kMechs[(i >= 0 && i < kMechCount) ? i : 0];
}

const FocusInfo& focusInfo(Focus f) {
    const int i = index(f);
    return kFocus[(i >= 0 && i < kFocusCount) ? i : 0];
}

Focus familyOf(Mech m) { return mechInfo(m).family; }

Focus focusFromId(const std::string& id) {
    std::string low;
    for (char c : id) low += (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
    for (const FocusInfo& f : kFocus)
        if (low == f.id) return f.focus;
    // A few friendly aliases.
    if (low == "half-flip" || low == "half_flip") return Focus::HalfFlip;
    if (low == "chain" || low == "chainwavedash" || low == "infinite") return Focus::ChainDash;
    if (low == "zap") return Focus::ZapDash;
    if (low == "wall") return Focus::WallDash;
    if (low == "aerial" || low == "fast_aerial") return Focus::FastAerial;
    if (low == "heli" || low == "tornado") return Focus::Helicopter;
    if (low == "cancel" || low == "flip_cancel") return Focus::FlipCancel;
    return Focus::None;
}

std::string focusIdList() {
    std::string s;
    for (const FocusInfo& f : kFocus) {
        if (!s.empty()) s += ", ";
        s += f.id;
    }
    return s;
}

std::string variantLabel(const std::string& variant) {
    std::string out, part;
    for (char c : variant) {
        if (c == '+') {
            if (!out.empty()) out += " + ";
            out += translateDashed(part);
            part.clear();
        } else {
            part += c;
        }
    }
    if (!out.empty() || !part.empty()) {
        if (!out.empty()) out += " + ";
        out += translateDashed(part);
    }
    return out;
}

} // namespace rlt::mech
