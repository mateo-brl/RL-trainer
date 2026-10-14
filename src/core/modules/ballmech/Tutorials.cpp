#include "core/modules/ballmech/Tutorials.h"

namespace rlt::bm {

namespace {

Tutorial ceiling() {
    Tutorial t;
    t.principle =
        "Quand tes roues touchent le plafond, le jeu te rend ton flip. Si tu quittes le plafond sans sauter, tu gardes "
        "ce flip pendant toute ta chute. Tu peux alors flipper dans la balle très haut, loin du sol.";
    t.goodToKnow =
        "Tu tombes à l'envers, donc le stick est inversé : pour pointer le nez vers le sol, tire le stick vers toi. "
        "C'est la première chose à savoir après avoir quitté le plafond.";
    t.before =
        "Tir aérien, air roll shot, et la conduite au mur (Parcours, anneaux sol et murs à partir du niveau 4). Tu peux "
        "quand même l'essayer tout de suite : au niveau 1, la balle est relancée juste devant toi quand tu quittes "
        "le plafond.";
    t.steps = {
        {"Va au mur de côté.", "Roule vers le mur de côté, un peu en biais vers le but adverse, et boost avec {boost}.",
         "Il faut de la vitesse pour monter tout en haut du mur.", "Tes roues roulent sur le mur et l'étape passe au vert.",
         ""},
        {"Monte jusqu'au plafond.", "Garde l'accélérateur, monte presque tout droit, et lâche le boost au milieu du mur.",
         "Si tu arrives trop vite au plafond, tu files trop loin et tu passes au-dessus de la balle.",
         "Ta voiture passe l'arrondi en haut du mur et touche le plafond, roues en haut.", ""},
        {"Laisse-toi tomber, sans sauter.",
         "Lâche l'accélérateur et ne touche pas à {jump}. La voiture ne tient pas au plafond : elle tombe toute seule, "
         "c'est normal.",
         "Si tu sautes pour te décrocher, tu perds le flip que le plafond vient de te rendre.",
         "L'étape « Lâche-toi » passe au vert : ton flip est gardé.", ""},
        {"Place-toi derrière la balle.",
         "Pendant la chute, tourne le nez vers la balle, puis boost par petites touches pour arriver juste derrière "
         "elle, à sa hauteur.",
         "Le flip pousse droit devant ton nez. Trop haut ou trop bas, il passe à côté de la balle.",
         "La balle est juste devant ton nez, à une longueur de voiture.",
         "Attention : tu tombes à l'envers, donc le stick est inversé. Pour pointer le nez vers le sol, tire le stick "
         "vers toi. Si ça te perturbe, fais d'abord un demi-tour avec {airroll} pour te remettre à l'endroit."},
        {"Flippe dans la balle.", "Appuie sur {jump} avec le stick vers l'avant, juste avant de toucher la balle.",
         "Le flip donne toute sa force au moment du contact.", "La balle part fort, devant toi.", ""},
    };
    t.blocks = {
        {"Départ au plafond (pour travailler la frappe seule)",
         "Tu commences collé au plafond, près du mur. Au début, ne fais rien : ne boost pas, ne saute pas. Tu tombes "
         "et la balle monte vers toi. Avec « Guide du départ au plafond » (réglages, Affichage), un cercle montre où "
         "vous allez vous croiser. Quand la balle est juste devant ton "
         "nez, à ta hauteur, flippe."},
    };
    t.errors = {
        {"Saut en quittant le plafond", "tu as appuyé sur saut. Laisse-toi juste tomber."},
        {"Tombé du mur", "pas assez de vitesse. Boost jusqu'au milieu du mur et monte plus droit."},
        {"Trop vite au plafond", "lâche le boost plus tôt dans le mur."},
        {"Flip dans le vide", "tu as flippé trop tôt. Attends que la balle soit à une longueur de voiture."},
        {"Touché sans flip", "tu es rentré dans la balle avant de flipper. Flippe un peu plus tôt."},
    };
    t.level1 =
        "Parcours complet : quand tu quittes le plafond, la balle est relancée juste devant toi. Pas besoin de "
        "boost pendant la chute : attends qu'elle soit devant ton nez et flippe. Prends ton temps pour monter. "
        "Départ au plafond : le jeu est ralenti.";
    t.tech = {
        "Parcours complet, niveaux 1 à 3 : voiture à 12 m du mur (1200 uu), lancée à 22 km/h (600 uu/s), boost plein. "
        "Balle suspendue à 14,5 m de haut (1450 uu), 15 m plus loin vers le but adverse. 0,3 s après que tu as "
        "quitté le plafond, elle est relancée d'en dessous : elle est au plus haut 0,8 s plus tard, à la hauteur de "
        "ta voiture et au moins 2,6 m devant ton nez (260 uu, plus si tu vas vite), sur ta trajectoire de chute.",
        "Niveaux 4 à 6 : la balle est lâchée quand tu touches le plafond. Niveaux 7 à 10 : voiture à l'arrêt au "
        "départ, balle lâchée dès que tu montes sur le mur.",
        "Mur validé : au moins 3 roues sur le mur. Plafond validé : moins de 90 uu du plafond, voiture à l'envers.",
        "Chute sans saut : aucun appui sur saut pendant 0,15 s après avoir quitté le plafond, flip toujours là 0,3 s "
        "après.",
        "Frappe : flip puis contact en moins de 0,35 s. Balle à 43 km/h (1200 uu/s) ou plus au niveau 1, 79 km/h "
        "(2200 uu/s) au niveau 10, cadrée à partir du niveau 6.",
        "Temps : 6 s pour le mur (4 s à partir du niveau 4), 4 s pour le plafond, 3 s pour lâcher (1,8 s à partir du "
        "niveau 4), 2,5 s pour frapper.",
        "Au-dessus de 65 km/h (1800 uu/s) en arrivant au plafond, tu passes au-dessus de la balle.",
        "Départ au plafond, niveau 1 : la balle est au plus haut 1 s après le départ, 2,6 m devant ton nez (260 uu), "
        "jeu ralenti à 75 % (85 % au niveau 2).",
    };
    return t;
}

Tutorial flipReset() {
    Tutorial t;
    t.principle =
        "Si les 4 roues touchent la balle en l'air, le jeu te rend ton flip. Tu peux alors flipper une deuxième fois "
        "dans la balle.";
    t.goodToKnow =
        "L'air roll fait tourner la voiture sur elle-même autour de son nez. C'est souvent le même bouton que le "
        "powerslide. Chez toi : {airroll}.";
    t.before = "Air roll shot et air dribble.";
    t.steps = {
        {"Décolle vers la balle.", "Saute avec {jump}, lève le nez et boost avec {boost} vers la balle qui t'attend en l'air.",
         "Il faut arriver sous la balle, un peu plus bas qu'elle.", "Tu voles et la balle est au-dessus de toi, devant.", ""},
        {"Passe sous la balle.", "Vise un point juste sous la balle, pas la balle elle-même.",
         "Les roues sont sous la voiture. Pour les poser sur la balle, il faut arriver par en dessous.",
         "La balle est au-dessus de toi, tout près.", ""},
        {"Tourne les roues vers la balle.",
         "Lève le nez (stick vers toi) et tourne avec {airroll} jusqu'à présenter le dessous de la voiture à la balle. "
         "Lâche le boost juste avant le contact.",
         "Il faut les 4 roues en même temps, et arriver doucement, sinon la balle rebondit avant que les roues la "
         "touchent.",
         "La jauge « Roues vers la balle » passe au vert.", ""},
        {"Touche la balle avec les 4 roues.", "Laisse la voiture venir sur la balle, doucement.",
         "C'est ce contact qui recharge ton flip.", "Un cercle vert apparaît autour de ta voiture.", ""},
        {"Flippe dans la balle (à partir du niveau 5).", "Tourne le nez vers la balle et flippe dedans avec {jump} avant de retomber.",
         "C'est tout l'intérêt du reset : une deuxième frappe en l'air.",
         "La balle part fort, vers le but à partir du niveau 8.", ""},
    };
    t.blocks = {
        {"Départ sous la balle (exercice court)",
         "Tu pars en l'air, sous la balle, déjà lancé vers elle. Ta voiture est tournée sur le côté : fais l'air roll "
         "du côté écrit sur le panneau pour mettre les roues face à la balle, puis laisse-la venir."},
    };
    t.errors = {
        {"Roues mal alignées", "tourne plus tôt, avant d'être collé à la balle."},
        {"Contact trop rapide", "lâche le boost avant le contact."},
        {"Reset gâché", "tu es retombé sans utiliser ton flip."},
    };
    t.level1 =
        "Départ sous la balle : tu pars en l'air, sous la balle. Ta voiture est tournée d'un quart de tour : un petit "
        "air roll du côté écrit sur le panneau suffit pour mettre les roues face à la balle. Le jeu est ralenti. C'est "
        "réussi dès que le reset est obtenu. Parcours complet : la balle t'attend en l'air, immobile, devant toi.";
    t.tech = {
        "Roues alignées : dessous de la voiture à moins de 37° de la direction de la balle.",
        "Reset : événement du jeu ou 3 roues sur la balle.",
        "Approche conseillée : moins de 14 km/h (400 uu/s). Au-delà de 32 km/h (900 uu/s), la balle rebondit.",
        "Départ sous la balle, niveau 1 : contact 1,6 s après le départ, approche à 11 km/h (300 uu/s), voiture "
        "tournée d'un quart de tour, jeu ralenti à 75 % (85 % au niveau 2). Le contact se fait vers 10 m de haut.",
        "Parcours complet, niveaux 1 à 3 : balle suspendue à 12,5 m de haut et 20 m devant toi, lâchée quand tu "
        "arrives à 7 m (700 uu). Niveaux 4 à 6 : balle lancée en l'air au départ. Niveaux 7 à 10 : elle part du mur.",
        "Temps du parcours : 5 s pour décoller (3 s à partir du niveau 4), 3 s pour passer sous la balle, 2 s pour "
        "tourner les roues, 1,5 s pour le reset.",
    };
    return t;
}

Tutorial airDribble() {
    Tutorial t;
    t.principle = "Tu emmènes la balle en l'air et tu la pousses doucement avec le nez, plusieurs fois, sans la perdre.";
    t.goodToKnow = "";
    t.before = "Flick, et Dribble sur le toit (Parcours).";
    t.steps = {
        {"Balle stable sur le toit.",
         "Au niveau 1, ne touche à rien : la balle est posée sur ton toit. Plus tard, roule doucement en gardant la "
         "balle au centre. En parcours complet, ramasse d'abord la balle.",
         "Un départ stable donne un pop propre.", "La balle ne bouge plus sur le toit.", ""},
        {"Le pop.", "Saute avec {jump} en levant un peu le nez. La balle décolle devant toi.",
         "La balle doit monter plus vite que toi pour que tu puisses la suivre par en dessous.",
         "La balle monte nettement au-dessus de ta voiture.", ""},
        {"Première touche en l'air.", "Nez vers la balle, boost par petites touches avec {boost}, et touche-la avec le nez.",
         "Le nez pousse la balle vers le haut et vers l'avant sans l'envoyer loin.", "L'étape passe au vert.", ""},
        {"Garde-la.", "Reste sous la balle, le nez toujours pointé vers elle. Appuie et relâche le boost.",
         "Trop de boost et tu la frappes trop fort, pas assez et elle t'échappe.",
         "La durée atteint l'objectif de ton niveau.", ""},
        {"Finis vers le but (niveau 7 et plus).", "Pousse la balle ou flippe dedans vers le but.",
         "Un air dribble sert à marquer.", "La balle part vers le but.", ""},
    };
    t.blocks = {
        {"Depuis le mur (niveau 6 et plus)",
         "La balle monte le long du mur, juste au-dessus de ton nez. Reste sur le mur sous elle, saute du mur quand elle "
         "commence à ralentir, puis continue comme en l'air."},
    };
    t.errors = {
        {"Trop court", "tu as perdu la balle trop tôt, boost moins fort."},
        {"Balle trop loin", "tu la tapes trop fort, garde-la plus près du nez."},
        {"Plus de boost", "dose le boost dès le pop."},
        {"Pop trop faible", "saute plus franchement, la balle doit monter au-dessus de ta voiture."},
    };
    t.level1 = "Voiture à l'arrêt, balle posée sur le toit. C'est réussi avec le pop et une seule touche en l'air.";
    t.tech = {
        "Touche en l'air : voiture sans contact avec le sol, balle à 3 m de haut (300 uu) ou plus.",
        "Un contact continu compte une touche toutes les 0,15 s.",
        "Pop validé : balle 2,5 m au-dessus du toit (250 uu) en moins de 0,6 s.",
        "Niveaux 2 et 3 : au moins 3 touches dont 2 en l'air. Durée demandée : 0,8 s aux niveaux 4 et 5, jusqu'à 3 s au "
        "niveau 10.",
        "Départ : voiture à l'arrêt au niveau 1, puis 13 km/h (350 uu/s) aux niveaux 2 à 4, puis 18 à 29 km/h (500 à "
        "800 uu/s).",
        "Mur : balle collée au mur, qui monte à 34 km/h (950 uu/s), sommet vers 12,5 m.",
    };
    return t;
}

Tutorial doubleTap() {
    Tutorial t;
    t.principle =
        "La balle rebondit sur le mur du fond adverse, au-dessus du but, et tu la reprends en l'air avant de retomber.";
    t.goodToKnow = "";
    t.before = "Tir aérien et air dribble.";
    t.steps = {
        {"Suis la balle.", "Dès le départ, boost avec {boost} vers le mur du fond, derrière la balle.",
         "Le rebond revient vite. Si tu pars tard, la balle est déjà basse quand tu arrives.",
         "Tu es près du mur quand la balle le touche.", ""},
        {"Décolle avant le rebond.", "Saute avec {jump} et boost vers l'endroit où la balle va toucher le mur, un peu en dessous.",
         "Après le rebond, la balle revient vers toi. Il faut déjà être en l'air.",
         "Tu voles au moment où la balle touche le mur.", ""},
        {"Lis le rebond.", "Regarde la balle revenir et descendre. Place ton nez sous elle sans foncer dedans.",
         "Si tu la touches avant le rebond, ce n'est plus un double tap.",
         "La balle a touché le mur et arrive devant ton nez.", ""},
        {"Reprends-la.", "Touche-la avec le nez vers le but. Flippe si tu as encore ton flip.",
         "C'est la deuxième touche qui marque.",
         "La balle part vers le but. Aux niveaux 1 et 2, il suffit de la toucher.", ""},
    };
    t.blocks = {
        {"À partir du niveau 7, et en parcours complet",
         "Le premier tir est pour toi. Frappe la balle haut sur le mur du fond, au-dessus de la barre, puis enchaîne "
         "les étapes."},
    };
    t.errors = {
        {"Trop loin au rebond", "pars plus tôt, boost tout de suite."},
        {"Retombé", "tu as décollé trop tôt ou sans assez de boost."},
        {"Pas de deuxième touche", "tu attendais trop haut, la balle est passée sous toi."},
    };
    t.level1 =
        "La balle est envoyée sur le mur pour toi, depuis tout près. Tu pars lancé juste derrière. Toucher le rebond "
        "suffit.";
    t.tech = {
        "Niveau 1 : balle lancée à 47 km/h (1300 uu/s), rebond environ 1,5 s après le départ à 10 m de haut. Elle reste "
        "au-dessus de 3 m pendant environ 1,4 s après le rebond.",
        "Distance conseillée au rebond : moins de 12 m (1200 uu).",
        "Premier tir (niveau 7 et plus) : impact sur le mur au-dessus de 7,5 m (750 uu).",
        "Vitesse demandée : 29 km/h (800 uu/s) au niveau 1, jusqu'à 65 km/h (1800 uu/s) au niveau 10.",
    };
    return t;
}

Tutorial flick() {
    Tutorial t;
    t.principle =
        "Comme en match : la balle est sur ton toit et tu roules déjà vers le but. Tu sautes, puis tu flippes : le toit "
        "et le nez projettent la balle d'un coup, comme un tir.";
    t.goodToKnow =
        "Coupe la ball cam pendant le dribble : avec elle, la caméra vise la balle sur ton toit et le but sort de "
        "l'écran. Remets-la après le flick. Choisis le flick demandé sous la liste des mécas (Tableau de bord). « Tous » "
        "accepte n'importe lequel.";
    t.before = "Frappe puissante, puis Dribble sur le toit (Parcours).";
    t.steps = {
        {"Balle stable sur le toit, vitesse gardée.",
         "Au GO, accélère un peu, juste assez pour rouler à la même vitesse que la balle. Ball cam coupée, tu vois le "
         "but devant toi.",
         "Si la balle glisse, le flick part de travers ou rate.", "La balle ne bouge plus sur le toit.", ""},
        {"Saute.", "Un appui court sur {jump}.", "La voiture monte avec la balle toujours sur le toit.",
         "Tu es en l'air, la balle encore sur toi.", ""},
        {"Tourne un peu (flick 45 ou 90 seulement).",
         "Juste après le saut, pousse le stick sur le côté. Un huitième de tour pour le 45, un quart de tour pour le 90.",
         "La rotation donne l'angle et de la puissance.", "Le capot regarde un peu de côté.", ""},
        {"Flippe vers le but.",
         "Vite après le saut, appuie sur {jump} avec le stick à fond dans la direction voulue : vers l'avant pour le "
         "flick avant, en diagonale pour le 45.",
         "Le flip fait taper le toit et le nez dans la balle.", "La balle part fort vers le but.",
         "Attention : flick avant d'arriver devant le but, sinon l'essai s'arrête. Remets la ball cam après le flick."},
    };
    t.errors = {
        {"Balle perdue", "garde ta vitesse : sans accélérer, la balle glisse vers l'avant ; en accélérant fort, vers "
                         "l'arrière."},
        {"Saut pas stable", "attends que la balle soit immobile sur le toit avant de sauter."},
        {"Flip trop tôt ou trop tard", "le flip vient très vite après le saut."},
        {"Flick faible", "stick à fond au moment du flip."},
        {"Trop près du but", "flick plus tôt, dès que la balle est stable."},
        {"Tir pas vers le but", "oriente la voiture vers le but avant de sauter."},
    };
    t.level1 = "Tu roules déjà vers le but, pas trop vite, et la balle est aidée à rester sur le toit au tout début. Le "
               "tir doit partir vers le but, pas forcément cadré.";
    t.tech = {
        "Délai saut-flip conseillé : 0,10 à 0,40 s (45 : 0,12 à 0,45 s ; 90 : 0,12 à 0,50 s).",
        "Rotation : 25 à 65° pour le 45, 65 à 115° pour le 90.",
        "Vitesse de la balle demandée, comme un tir en match : 54 km/h (1500 uu/s) au niveau 1, jusqu'à 86 km/h "
        "(2400 uu/s) au niveau 10. Niveaux 1 et 2 : tir vers le but (moins de 25° d'écart avec le centre du but) ; "
        "cadré à partir du niveau 3.",
        "Départ en dribble : à 45 à 50 m du but (4500 à 5000 uu), 32 km/h (900 uu/s) au niveau 1 ; 50 à 55 m (5000 à "
        "5500 uu), 47 km/h (1300 uu/s) au niveau 10. L'accélérateur seul tient cette vitesse. Le flick doit partir avant "
        "12 m du but (1200 uu).",
        "Aide au départ jusqu'au niveau 6 : pendant 0,6 s, la balle est remise au centre du toit tant qu'elle n'a pas "
        "glissé de plus de 40 uu. Balle tombée dans la première seconde : l'essai est rejoué, deux fois de suite au "
        "plus.",
    };
    return t;
}

Tutorial musty() {
    Tutorial t;
    t.principle =
        "Tu sautes avec la balle sur le toit, tu baisses le nez, puis tu fais un flip arrière. Le nez remonte et frappe "
        "la balle par en dessous, comme un uppercut.";
    t.goodToKnow = "";
    t.before = "Flick.";
    t.steps = {
        {"Balle stable sur le toit.",
         "Au GO, garde ta vitesse : accélère juste assez. Coupe ta ball cam pour voir le but, remets-la après le flick.",
         "Une balle qui glisse part de travers.", "La balle ne bouge plus.", ""},
        {"Saute.", "Un appui sur {jump}.", "Il faut un peu de hauteur pour avoir la place de basculer.",
         "Tu es en l'air avec la balle sur toi.", ""},
        {"Baisse le nez.", "Juste après le saut, pousse le stick vers l'avant et garde-le. Le nez descend vers le sol.",
         "Plus le nez est bas, plus le flip arrière remonte fort dans la balle.",
         "La jauge « Nez vers le bas » passe au vert : tu vois le sol devant ton capot.", ""},
        {"Flip arrière.", "Tire le stick vers toi et appuie sur {jump}.",
         "La voiture bascule vers l'arrière et le nez cogne la balle par en dessous.", "La balle part haut et devant toi.",
         ""},
    };
    t.errors = {
        {"Nez pas assez bas", "garde le stick vers l'avant plus longtemps avant le flip."},
        {"Flip arrière simple", "tu as flippé avant de baisser le nez."},
        {"Trop près du but", "flick plus tôt, dès que la balle est stable."},
    };
    t.level1 = "Tu roules déjà vers le but avec la balle sur le toit, pas trop vite. Réussi si le geste est bon et que la "
               "balle part vers le but, assez vite.";
    t.tech = {
        "Nez à au moins 35° vers le bas au moment du flip, idéal entre 50 et 120°.",
        "Flip arrière entre 0,2 et 0,7 s après le saut.",
        "Vitesse de la balle demandée : 54 km/h (1500 uu/s) au niveau 1, jusqu'à 86 km/h (2400 uu/s) au niveau 10 ; "
        "vers le but aux niveaux 1 et 2, cadré ensuite.",
        "Départ en dribble comme le flick : 32 km/h (900 uu/s) au niveau 1 jusqu'à 47 km/h (1300 uu/s) au niveau 10, "
        "flick avant 12 m du but (1200 uu).",
    };
    return t;
}

Tutorial breezi() {
    Tutorial t;
    t.principle =
        "Un musty flick précédé d'une tornade. La voiture tourne sur elle-même avec l'air roll pendant que la balle reste "
        "dessus, puis tu fais un flip arrière.";
    t.goodToKnow = "";
    t.before = "Musty flick, et l'hélico (Mécas mouvement).";
    t.steps = {
        {"Balle stable sur le toit.",
         "Au GO, garde ta vitesse, sans traînée de supersonique. Coupe ta ball cam pour voir le but, remets-la après.",
         "La tornade part mieux d'une balle stable.", "La balle ne bouge plus.", ""},
        {"Saut maintenu.", "Saute et garde {jump} enfoncé.", "Tu montes plus haut, ce qui te laisse le temps de tourner.",
         "Tu montes plus haut qu'un saut court.", ""},
        {"La tornade.",
         "Maintiens {airroll} d'un côté et pousse le stick de l'autre côté. La voiture tourne comme une toupie et le nez "
         "descend.",
         "La rotation garde la balle collée et prépare l'uppercut.",
         "La voiture a fait un peu plus d'un demi-tour sur elle-même, la balle toujours près de toi.", ""},
        {"Flip arrière quand le nez remonte.", "Au moment où le nez revient vers la balle, stick vers toi et {jump}.",
         "Le nez frappe la balle par en dessous.", "La balle part haut et loin.", ""},
    };
    t.errors = {
        {"Pas de tornade", "garde l'air roll plus longtemps, stick à l'opposé."},
        {"Départ supersonique", "roule moins vite."},
        {"Flip trop tôt", "attends que le nez remonte."},
    };
    t.level1 =
        "Niveaux 1 et 2 : la tornade seule. C'est réussi si tu fais un peu plus d'un demi-tour en l'air avec la balle "
        "près de toi, même sans le flip final.";
    t.tech = {
        "Tornade comptée à 230° de rotation, air roll tenu au moins 30 % du temps en l'air, balle à moins de 2,8 m "
        "(280 uu) au moins 70 % du temps.",
        "Flip arrière entre 0,5 et 1,2 s après le saut. Départ sous 79 km/h (2200 uu/s).",
        "Vitesse de la balle demandée : 54 km/h (1500 uu/s) au niveau 3, jusqu'à 86 km/h (2400 uu/s) au niveau 10, "
        "cadré.",
        "Départ en dribble : 32 km/h (900 uu/s) au niveau 1 jusqu'à 47 km/h (1300 uu/s) au niveau 10, flick avant 12 m "
        "du but (1200 uu).",
    };
    return t;
}

Tutorial pinch() {
    Tutorial t;
    t.principle =
        "Tu frappes la balle au moment exact où elle touche le mur. Coincée entre ta voiture et le mur, elle part beaucoup "
        "plus vite qu'une frappe normale.";
    t.goodToKnow = "";
    t.before = "Frappe puissante.";
    t.steps = {
        {"Prends de la vitesse.", "Boost avec {boost} en ligne droite vers l'endroit où la balle va toucher le mur.",
         "La vitesse de la balle dépend surtout de la tienne.", "Ta voiture laisse une traînée, ou presque.", ""},
        {"Vise le point de contact.", "Regarde où la balle va toucher le mur et vise ce point, pas la balle.",
         "Tu dois y arriver en même temps qu'elle.", "Ton nez pointe vers ce point.", ""},
        {"Frappe au contact.", "Touche la balle quand elle touche le mur, un peu à côté du centre, côté terrain.",
         "Le mur sert de deuxième doigt et pince la balle.", "La vitesse de sortie dépasse l'objectif de ton niveau.", ""},
    };
    t.blocks = {
        {"Parcours complet",
         "La balle roule vers le mur de côté et ta voiture est à l'arrêt : lis où elle va toucher le mur, attends, puis "
         "pars au bon moment pour arriver en même temps qu'elle."},
    };
    t.errors = {
        {"Mauvais timing", "tu es arrivé trop tôt ou trop tard, la balle n'était pas contre le mur."},
        {"Pas pincée", "tu as tapé en plein centre, vise un peu à côté."},
        {"Pas cadrée (niveau 6 et plus)", "approche plus en biais pour renvoyer vers le but."},
    };
    t.level1 = "La balle roule doucement vers le mur et tu pars déjà lancé. L'objectif de vitesse est plus bas qu'en jeu libre.";
    t.tech = {
        "Contact compté si la balle est à moins de 1,2 m du mur (120 uu) et si tu roules vers le mur à 11 km/h "
        "(300 uu/s) ou plus.",
        "Objectif : 108 km/h (3000 uu/s) au niveau 1, puis environ 6 km/h de plus par niveau, 162 km/h (4500 uu/s) au "
        "niveau 10.",
        "En jeu libre, un pinch compte à partir de 144 km/h (4000 uu/s), réglable.",
    };
    return t;
}

Tutorial powerShot() {
    Tutorial t;
    t.principle = "Arriver vite, bien aligné, et flipper juste avant de toucher la balle.";
    t.goodToKnow = "";
    t.before = "";
    t.steps = {
        {"Aligne-toi.", "Place ta voiture pour que la balle soit entre toi et le but.",
         "La balle part dans la direction où tu roules.", "La balle et le but sont droit devant toi.", ""},
        {"Prends de la vitesse.", "Boost avec {boost} en ligne droite.", "Plus tu vas vite, plus la balle part vite.",
         "Ta vitesse monte jusqu'à la balle.", ""},
        {"Flippe juste avant le contact.",
         "Quand la balle est à environ une longueur de voiture, flip avant : {jump}, puis {jump} avec le stick vers "
         "l'avant.",
         "Le flip ajoute de la vitesse pile au moment du contact.", "La balle part plus vite que ta voiture.", ""},
        {"Frappe avec l'avant.", "Touche la balle avec le pare-choc avant, pas avec le côté.",
         "Un contact de côté envoie la balle de travers.", "La balle part droit vers le but.", ""},
    };
    t.blocks = {
        {"Balle de côté (niveau 6 et plus)",
         "Glisse avec {slide} pour te tourner vite face à la balle, et relâche la glissade avant de frapper. Si la "
         "glissade est nouvelle pour toi, fais d'abord l'exercice Powerslide (Mécas mouvement)."},
        {"Parcours complet",
         "La balle est posée sur le côté de ta ligne : décale-toi d'abord pour la mettre entre toi et le but, puis "
         "prends de la vitesse et flippe juste avant de la toucher."},
    };
    t.errors = {
        {"Flip trop tôt", "attends d'être presque sur la balle."},
        {"Touchée de côté", "aligne-toi plus tôt."},
        {"Glissade tenue", "relâche la glissade avant l'impact."},
    };
    t.level1 = "Balle posée, immobile, droit devant toi, le but derrière elle. Le flip est conseillé, pas obligé.";
    t.tech = {
        "Flip conseillé entre 0,05 et 0,25 s avant le contact.",
        "Vitesse de balle demandée : 72 km/h (2000 uu/s) au niveau 1, jusqu'à 115 km/h (3200 uu/s) au niveau 10, tir "
        "cadré.",
        "Niveau 1 : contact vers 2 s après le départ, voiture vers 82 km/h (2270 uu/s).",
        "Parcours complet : la frappe se fait en flip, voiture alignée à moins de 20° avant les 8 derniers mètres "
        "(800 uu), 58 km/h (1600 uu/s) ou plus à 4 m de la balle.",
    };
    return t;
}

Tutorial airRoll() {
    Tutorial t;
    t.principle =
        "En l'air, l'air roll fait tourner la voiture autour de son nez. Quand la voiture est penchée, le stick haut et "
        "bas la fait tourner vers la gauche ou la droite : tu t'en sers pour viser le centre de la balle.";
    t.goodToKnow = "Chez toi, l'air roll : {airroll}.";
    t.before = "Tir aérien, puis le fast aerial (Mécas mouvement).";
    t.steps = {
        {"Décolle vers la balle.", "Saute avec {jump}, lève le nez vers la balle et boost avec {boost}.",
         "Le boost pousse là où pointe ton nez.", "Tu voles vers la balle. Au départ en l'air, tu voles déjà.", ""},
        {"Air roll dans un seul sens.", "Maintiens {airroll}, toujours du même côté, pendant que tu montes.",
         "Un seul sens garde la rotation régulière et prévisible.", "L'étape « Air roll » passe au vert en parcours complet.",
         ""},
        {"Vise le centre.", "Garde la balle au milieu de l'écran, juste devant ton nez.",
         "Un contact au centre envoie la balle droit et fort.", "La balle grossit au centre de ton écran.", ""},
        {"Frappe.", "Garde le boost et l'air roll jusqu'au contact.", "Lâcher trop tôt fait dévier la voiture.",
         "La balle part vers le but.", ""},
    };
    t.errors = {
        {"Pas d'air roll", "tu as lâché l'air roll trop tôt, tiens-le jusqu'au contact."},
        {"Air roll dans les deux sens", "choisis un côté et garde-le."},
        {"Pas au centre", "vise le milieu de la balle, pas le dessus."},
    };
    t.level1 =
        "Tu pars en l'air, face à la balle qui flotte haut et redescend lentement. Il suffit de la frapper en tenant "
        "l'air roll, le tir n'a pas besoin d'être cadré.";
    t.tech = {
        "Air roll compté s'il est tenu au moins 40 % du temps dans la demi-seconde avant le contact, dans le même sens "
        "à 70 % ou plus.",
        "Centre : moins de 90 uu entre la balle et l'axe du nez.",
        "Vitesse : 36 km/h (1000 uu/s) au niveau 1, puis 43 à 79 km/h (1200 à 2200 uu/s), tir cadré à partir du "
        "niveau 2.",
        "Niveau 1 : balle à 11,5 m de haut, frappe possible entre 1,5 et 2,1 s après le départ.",
        "Voiture penchée d'un quart de tour jusqu'au niveau 5, de trois huitièmes ensuite. Balle décalée de 2,5 m "
        "(250 uu) sur le côté à partir du niveau 4.",
    };
    return t;
}

Tutorial aerial() {
    Tutorial t;
    t.principle = "Tu décolles, tu vises le centre de la balle avec le nez et tu la frappes vers le but.";
    t.goodToKnow = "";
    t.before = "Frappe puissante.";
    t.steps = {
        {"Place-toi dans l'axe.", "Avant de sauter, mets-toi en ligne avec la balle et le but.",
         "En l'air, c'est dur de changer de direction. L'angle se prépare au sol.",
         "La balle est droit devant toi et le but derrière elle.", ""},
        {"Décolle.",
         "Saute avec {jump}, lève le nez vers la balle (stick vers toi) et boost avec {boost}. Tu peux sauter une "
         "deuxième fois pour monter plus vite.",
         "Le boost pousse là où pointe ton nez.", "Tu montes vers la balle sans passer au-dessus.", ""},
        {"Vise le centre.", "Garde la balle au milieu de l'écran, juste devant ton nez. Corrige par petites touches de stick.",
         "Un contact au centre envoie la balle droit et fort.", "La balle grossit au centre de ton écran.", ""},
        {"Frappe.", "Garde le boost jusqu'au contact.", "Sans boost tu arrives mou et la balle part lentement.",
         "La balle part vers le but.", ""},
    };
    t.blocks = {
        {"Niveau 7 et plus, et parcours complet",
         "La balle arrive de côté, comme un centre. Place-toi dans l'axe de l'endroit où tu vas la toucher et du but "
         "avant de sauter."},
    };
    t.errors = {
        {"Pas cadré", "tu n'étais pas dans l'axe au décollage."},
        {"Touchée de côté", "vise le centre, pas le bord de la balle."},
        {"Trop faible", "garde le boost jusqu'au contact."},
    };
    t.level1 = "La balle est lancée droit en l'air devant toi, à mi-hauteur, et redescend doucement. Le but est derrière elle.";
    t.tech = {
        "Niveau 1 : balle lancée vers le haut à 17 m devant toi, sommet vers 10 m de haut au bout de 1,6 s. Frappe "
        "possible dès 2 s environ.",
        "Vitesse demandée : 43 km/h (1200 uu/s) au niveau 1, jusqu'à 90 km/h (2500 uu/s) au niveau 10.",
        "Axe : moins de 25° d'écart au décollage. Visée : nez à moins de 15° de la balle.",
    };
    return t;
}

} // namespace

const Tutorial& tutorial(Mech m) {
    static const Tutorial kAll[kMechCount] = {flipReset(), ceiling(),   flick(),  musty(),   breezi(),   doubleTap(),
                                              airDribble(), airRoll(), aerial(), pinch(), powerShot()};
    const int i = static_cast<int>(m);
    return kAll[i >= 0 && i < kMechCount ? i : 0];
}

const char* lexicon() {
    return "Flip : un saut, puis un deuxième appui sur saut avec le stick penché. La voiture fait une roulade et prend "
           "de la vitesse. Annuler un flip : tirer le stick dans l'autre sens juste après le flip pour arrêter la "
           "roulade. Air roll : faire tourner la voiture sur elle-même autour de son nez, en l'air (souvent le même "
           "bouton que le powerslide). Powerslide : la glissade au sol. Supersonique : la vitesse maximale, quand une "
           "traînée apparaît derrière ta voiture. Kickoff : l'engagement au centre.";
}

const char* progressRule() {
    return "Passe à l'exercice suivant quand tu réussis environ 3 essais sur 4 au niveau 4. Tu peux toujours revenir en "
           "arrière.";
}

const TutorialBlock& pickupBlock() {
    static const TutorialBlock b{
        "Parcours complet : ramasse la balle",
        "Flick, musty et breezi, comme en match : ta voiture roule déjà vers le but, derrière la balle qui roule devant "
        "toi. Rattrape-la un peu plus vite qu'elle, nez bien droit : elle monte sur le capot puis sur le toit. Air "
        "dribble : ta voiture est à l'arrêt et la balle est par terre devant toi (plus tard, elle roule vers toi) ; "
        "roule doucement vers elle. Garde ensuite la balle au centre du toit : si elle glisse vers l'arrière, lâche un "
        "peu l'accélérateur ; si elle glisse vers l'avant, accélère un peu. Quand elle ne bouge plus, enchaîne la méca. "
        "Un ramassage raté ne fait pas baisser le niveau de la méca : il a son propre niveau."};
    return b;
}

const std::vector<const char*>& pickupTech() {
    static const std::vector<const char*> t = {
        "Ramassage en course (flick, musty, breezi) : départ à 55 m du but (5500 uu). Niveau 1 du ramassage : balle 5 m "
        "devant toi (500 uu) qui roule à 16 km/h (450 uu/s), ta voiture à 25 km/h (700 uu/s). Niveau 10 : 7 m (700 uu), "
        "balle à 19 km/h (520 uu/s), voiture à 33 km/h (920 uu/s). Le flick doit partir avant 12 m du but (1200 uu).",
        "Ramassage de l'air dribble : balle posée à 6 m devant toi (600 uu) aux niveaux 1 à 3 du ramassage, puis qui "
        "arrive vers toi depuis 15 m à 13 à 16 km/h (350 à 450 uu/s).",
        "Balle sur le toit 0,5 s (8 s pour y arriver), puis stable 0,3 s : moins de 5 km/h (150 uu/s) par rapport à la "
        "voiture et à moins de 40 uu du centre du toit.",
    };
    return t;
}

} // namespace rlt::bm
