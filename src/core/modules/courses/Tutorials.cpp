#include "core/modules/courses/Tutorials.h"

namespace rlt::courses {

namespace {

Tutorial ringsAir() {
    Tutorial t;
    t.intro = "Tu traverses des anneaux en volant. Les niveaux 1 à 3 sont une initiation : de grands anneaux tout bas, "
              "puis un peu plus hauts. Le vrai parcours aérien commence au niveau 4.";
    t.before = "Tir aérien (Mécas balle) et Fast aerial (Mécas mouvement) aident pour les niveaux 3 et plus. Le niveau 1 "
               "se joue sans savoir voler.";
    t.steps = {
        {"Étape 1 : saute avec {jump}, puis lève le nez.", "Le boost pousse dans la direction de ton nez.",
         "Ta voiture pointe vers l'anneau."},
        {"Étape 2 : boost avec {boost} en gardant le nez vers le centre de l'anneau.",
         "C'est comme ça qu'on monte et qu'on avance en même temps.", "L'anneau grossit au centre de ton écran."},
        {"Étape 3 : entre deux anneaux, lâche le boost un instant.", "Tu économises du boost et tu corriges ta direction.",
         "Il te reste du boost au dernier anneau."},
        {"Étape 4 : atterris sur tes roues.", "Tu repars aussitôt.", "La voiture ne rebondit pas sur le toit."},
    };
    t.panel = {"1/3 Saute et lève le nez", "2/3 Boost vers le centre", "3/3 Lâche le boost entre deux anneaux"};
    t.level1 = "4 grands anneaux tout bas : tu les passes presque au sol. Ta voiture part à l'arrêt, le premier anneau est "
               "loin devant, prends ton temps.";
    t.full = "Un seul anneau, assez haut, devant ta voiture à l'arrêt. Tu travailles le décollage : saute, lève le nez, "
             "boost jusqu'à l'anneau, puis retombe sur tes roues. Chaque étape passe au vert sur le panneau.";
    t.fullSteps = {"Saute", "Lève le nez", "Boost vers l'anneau", "Passe l'anneau", "Atterris sur les roues"};
    t.tech = {
        "Initiation 1 (niveau 1) : 4 anneaux de 12 m de diamètre (rayon 600 uu), centre entre 2,8 et 3,8 m de haut, "
        "espacés de 15 à 18 m, virages de 15° au plus. Voiture à l'arrêt, boost plein, premier anneau à 25 m au moins.",
        "Initiation 2 (niveau 2) : 5 anneaux de 9 m (rayon 450 uu), centre entre 4 et 6 m : un simple saut suffit. "
        "Initiation 3 (niveau 3) : 6 anneaux de 8 m (rayon 400 uu), centre entre 5 et 9 m : le premier vrai vol.",
        "Niveaux 4 à 10 : le parcours normal, de 8 anneaux (jusqu'à 12 m de haut) à 16 anneaux plus petits et plus "
        "hauts. Départ lancé à 29 km/h (800 uu/s), premier anneau à 25 m au moins.",
        "Temps limite : 2,5 fois le temps cible plus 10 s. Médaille d'or sous le temps cible.",
        "Parcours complet : anneau à 25 m, centre à 7 m de haut, 11 m de diamètre au niveau 1 (9 m de haut, 8 m de "
        "diamètre au niveau 10). Saute dans les 4 s, nez levé de 25° dans les 0,8 s après le saut (30° en 0,35 s au "
        "niveau 10), boost et plus de 4 m de haut dans les 2 s, anneau dans les 4 s, atterrissage dans les 5 s.",
    };
    return t;
}

Tutorial ringsGround() {
    Tutorial t;
    t.intro = "Tu passes sous des arches en roulant, d'abord au sol, plus tard sur les murs.";
    t.before = "";
    t.steps = {
        {"Étape 1 : regarde le prochain anneau (en vert) et celui d'après.", "Tu prépares ton virage avant de passer.",
         "Tu passes au centre en tournant déjà vers le suivant."},
        {"Étape 2 : garde ta vitesse dans les virages : relâche un peu l'accélérateur au lieu de freiner.",
         "Freiner fait perdre beaucoup de temps et de boost.", "Ta vitesse reste stable sur tout le parcours."},
        {"Étape 3 (à partir du niveau 4) : pour une arche sur le mur, arrive vers le mur en biais.",
         "De face, tu montes tout droit et tu retombes.", "Ta voiture roule sur le mur, roues collées."},
    };
    t.panel = {"1/3 Regarde l'anneau suivant", "2/3 Garde ta vitesse", "3/3 Mur : arrive en biais"};
    t.level1 = "Seulement des arches au sol, larges. Les arches sur les murs arrivent au niveau 4 : elles sont marquées "
               "MUR, avec une ligne au sol qui montre où tourner vers le mur. C'est la conduite au mur qui prépare le "
               "ceiling shot (Mécas balle).";
    t.full = "Une seule arche, sur le mur de côté. Ta voiture part à l'arrêt, de biais par rapport au mur. Roule vers le "
             "mur en biais, monte dessus, passe l'arche, puis redescends en roulant.";
    t.fullSteps = {"Roule vers le mur en biais", "Monte sur le mur", "Passe l'arche", "Redescends en roulant"};
    t.tech = {
        "Niveaux 1 à 3 : 8 à 10 arches de 9 m de large (rayon 450 uu), toutes au sol, espacées de 11 à 15 m. Départ lancé "
        "à 32 km/h (900 uu/s). Temps cible calculé pour 43 km/h (1200 uu/s).",
        "À partir du niveau 4 : arches de 7 à 8 m, et des arches sur les murs, jusqu'à 8 m de haut au début (14 m au "
        "niveau 10). Environ une course sur quatre a une arche sur le mur au niveau 5.",
        "Parcours complet : voiture à 19 m du mur, à 35° du mur. Arche centrée à 6 m de haut. Arrive au mur entre 10° et "
        "60° de biais au niveau 1 (entre 20° et 45° au niveau 10), roues sur le mur dans les 3 s, arche dans les 4 s, "
        "retour au sol sur les roues à plus de 11 km/h (300 uu/s) dans les 4 s.",
    };
    return t;
}

Tutorial ringsAirDribble() {
    Tutorial t;
    t.intro = "Tu fais passer la balle dans des anneaux en la poussant en l'air avec ta voiture. Elle ne doit jamais "
              "toucher le sol.";
    t.before = "Dribble sur le toit (niveau 3 ou plus) et Air dribble (Mécas balle, niveau 2 ou plus).";
    t.steps = {
        {"Étape 1 : pars avec la balle bien posée sur ton toit.", "Tout commence avec une balle stable.",
         "Elle ne glisse plus."},
        {"Étape 2 : saute avec {jump} pour soulever la balle.", "La balle doit décoller juste avant toi.",
         "Elle est juste au-dessus de ton nez."},
        {"Étape 3 : boost doucement avec {boost} sous la balle, nez un peu vers le haut.",
         "Tu la pousses par en dessous, à petits coups.",
         "La balle reste à moins d'une longueur de voiture de ton nez."},
        {"Étape 4 : vise l'anneau avec la balle, pas avec ta voiture.", "C'est la balle qui doit passer dans l'anneau.",
         "La balle traverse l'anneau."},
    };
    t.panel = {"1/4 Balle stable sur le toit", "2/4 Saute pour la soulever", "3/4 Petits coups de boost dessous",
               "4/4 Vise l'anneau avec la balle"};
    t.level1 = "Un seul grand anneau, juste devant et pas haut. Tu pars à l'arrêt, la balle posée sur ton toit.";
    t.full = "Tout depuis le sol : ramasse la balle, roule avec elle sur le toit, saute pour la soulever, suis-la au "
             "boost et pousse-la dans l'anneau. L'anneau apparaît devant toi au moment où tu sautes.";
    t.fullSteps = {"Ramasse la balle sur ton toit", "Garde-la au centre du toit", "Roule avec la balle sur le toit",
                   "Saute pour soulever la balle", "Suis la balle au boost", "Pousse la balle dans l'anneau"};
    t.tech = {
        "Niveau 1 : 1 anneau de 9 m de diamètre (rayon 450 uu), centre 4,5 m au-dessus de la balle et 15 m devant, "
        "départ à l'arrêt, balle sur le toit. Une balle gardée sur le toit passe sous l'anneau : il faut la soulever.",
        "Niveau 2 : 2 anneaux. Niveaux 3 à 10 : 5 à 8 anneaux, le premier 13 à 17 m devant et 3,5 à 4,5 m au-dessus de "
        "la balle. Avec le départ « balle lancée devant » (réglages), la voiture part lancée à 22 km/h (600 uu/s).",
        "Le départ choisi est gardé dans le code du parcours.",
        "Parcours complet : rouler entre 11 et 40 km/h (300 à 1100 uu/s) 0,8 s avec la balle sur le toit, puis sauter : "
        "la balle doit monter à plus de 7 km/h (200 uu/s) dans les 0,5 s. Suivre au boost à moins de 3,5 m pendant "
        "0,4 s. Anneau de 9 m, 15 m devant toi et 4,5 m au-dessus de la balle au moment du saut (niveau 1).",
    };
    return t;
}

Tutorial dribble() {
    Tutorial t;
    t.intro = "Garde la balle posée sur ton toit en roulant.";
    t.before = "";
    t.steps = {
        {"Étape 1 : accélère tout doucement.", "Une forte accélération fait glisser la balle vers l'arrière.",
         "La jauge avant-arrière reste verte."},
        {"Étape 2 : si la balle glisse vers l'arrière, relâche l'accélérateur ; si elle glisse vers l'avant, accélère un "
         "peu.",
         "Tu remets ton toit sous la balle.", "La jauge revient au centre."},
        {"Étape 3 : tourne très doucement.", "Un virage serré la fait tomber sur le côté.",
         "La balle ne part pas sur le côté."},
    };
    t.panel = {"1/3 Accélère doucement", "2/3 Balle en arrière : relâche. Balle en avant : accélère",
               "3/3 Tourne doucement"};
    t.level1 = "Tiens 5 s, même presque à l'arrêt. La balle peut quitter le toit un court instant.";
    t.full = "Tu ramasses d'abord la balle posée par terre (plus tard, elle roule vers toi), tu la stabilises, puis tu "
             "roules en la gardant sur le toit.";
    t.fullSteps = {"Mets ton nez sous la balle (conseil)", "Ramasse la balle sur ton toit", "Garde-la au centre du toit",
                   "Roule en gardant la balle sur le toit"};
    t.tech = {
        "Niveau 1 : 5 s, vitesse libre, la balle peut quitter le toit 0,5 s. Ensuite 8 s à 9 km/h de moyenne (250 uu/s) "
        "au niveau 2, jusqu'à 40 s à 36 km/h (1000 uu/s) au niveau 10.",
        "Jauge avant-arrière : verte quand la balle est à moins de 25 uu du centre du toit.",
        "À fond depuis l'arrêt, la balle glisse du toit en 0,4 s ; sans accélérer, en 0,6 s.",
        "Conseil « tourne » quand il reste moins de 15 m de ligne droite devant toi.",
    };
    return t;
}

Tutorial slalom() {
    Tutorial t;
    t.intro = "Avec la balle sur le toit, passe dans chaque porte, une fois à gauche, une fois à droite.";
    t.before = "Dribble sur le toit, niveau 3 ou plus.";
    t.steps = {
        {"Étape 1 : stabilise la balle sur ton toit avant la première porte, comme dans le dribble.",
         "Un départ brusque la fait tomber.", "La balle reste au centre du toit."},
        {"Étape 2 : avant chaque porte, décale-toi tôt et doucement de son côté.",
         "Un virage tardif et serré fait tomber la balle.", "Tu passes au centre de la porte en roulant presque droit."},
        {"Étape 3 : redresse juste après la porte et prépare la suivante.", "La porte suivante est de l'autre côté.",
         "La balle est toujours au centre du toit."},
    };
    t.panel = {"1/3 Balle stable", "2/3 Décale-toi tôt vers la porte", "3/3 Redresse après la porte"};
    t.level1 = "Départ à l'arrêt, portes larges, la première est loin devant.";
    t.full = "Tu ramasses d'abord la balle posée par terre (plus tard, elle roule vers toi) avant la première porte, tu "
             "la stabilises, puis tu fais le slalom.";
    t.fullSteps = {"Ramasse la balle sur ton toit", "Garde-la au centre du toit", "Décale-toi vers la porte 1 (conseil)",
                   "Passe la porte 1", "et ainsi de suite pour chaque porte"};
    t.tech = {
        "Niveau 1 : 5 portes de 10 m de large, espacées de 16 m, la première à 15 m. Pour passer proprement, décale la "
        "balle de 1,5 m de ta ligne de départ de chaque côté (3 m au niveau 10, portes de 5 m).",
        "Passage propre obligatoire, sans frôler les poteaux. Temps cible calculé pour 26 km/h (720 uu/s) au niveau 1, "
        "limité par le virage que la balle supporte sur le toit.",
        "La balle peut quitter le toit 0,3 s (réglage « Tolérance hors du toit »).",
    };
    return t;
}

Tutorial catchDrill() {
    Tutorial t;
    t.intro = "La balle tombe du ciel ou sort d'un mur : récupère-la sur ton toit et garde-la.";
    t.before = "Dribble sur le toit, niveau 2 ou plus.";
    t.steps = {
        {"Étape 1 : regarde le cercle au sol.", "C'est là que la balle arrive à la hauteur de ton toit.",
         "Tu roules vers lui."},
        {"Étape 2 : place-toi pile dans le cercle et arrête-toi avant qu'elle arrive.",
         "Si tu roules encore, elle tape le bord du toit et repart.",
         "Le centre de ton toit est au centre du cercle."},
        {"Étape 3 : ne saute pas, laisse-la tomber sur ton toit.",
         "En sautant, tu montes dans la balle et elle rebondit plus haut.", "Le premier rebond est plus petit que la chute."},
        {"Étape 4 : elle va rebondir un peu vers l'avant : avance doucement pour rester dessous.",
         "Chaque rebond la pousse vers l'avant.", "Les rebonds sont de plus en plus petits."},
        {"Étape 5 : quand elle ne rebondit plus, garde-la sur ton toit.", "Il faut la tenir une seconde.",
         "La barre de maintien se remplit."},
    };
    t.panel = {"1/5 Va dans le cercle", "2/5 Arrête-toi dessous", "3/5 Ne saute pas", "4/5 Suis les rebonds vers l'avant",
               "5/5 Garde-la"};
    t.level1 = "La balle monte un peu puis retombe de bas, juste devant toi : un ou deux petits rebonds. Tu as le temps "
               "de te placer.";
    t.full = "Une chandelle : la balle part du sol, loin devant toi, monte haut et redescend vers toi. Lis où elle tombe, "
             "place-toi dessous, amortis sans sauter, suis les rebonds et garde-la.";
    t.fullSteps = {"Roule vers le cercle", "Arrête-toi sous la balle", "Ne saute pas, laisse-la tomber", "Suis les rebonds",
                   "Garde-la sur le toit"};
    t.tech = {
        "Niveau 1, chute du ciel : la balle monte un peu puis redescend d'un sommet de 3,5 à 4,5 m, et arrive à hauteur "
        "de toit 1,5 s ou plus après le départ, 2 à 3 m devant toi. Sortie de mur : 4,5 à 5 m de haut, poussée "
        "doucement vers le terrain, ta voiture à 2 m de là où elle tombe.",
        "Une balle qui tombe sur un toit repart vers le haut à environ 65 % de sa vitesse, et un peu vers l'avant. Elle "
        "reste posée quand un rebond ne monte plus de 40 uu.",
        "Temps accordé : la chute, puis les rebonds, puis 1 s sur le toit, plus 1 s de marge. Les petits rebonds (moins "
        "de 70 uu) comptent comme sur le toit, et une coupure de 0,15 s ne remet pas le compteur à zéro.",
        "Parcours complet : balle à 12 m devant toi, lancée vers le haut à 36 km/h (1000 uu/s), sommet vers 8,6 m, "
        "retour à hauteur de toit après 3 s. Rouler vers le point de chute dans les 1,5 s, s'arrêter dessous à moins de "
        "1,2 m et sous 11 km/h (300 uu/s) au moins 0,2 s avant qu'elle arrive (niveau 1).",
    };
    return t;
}

} // namespace

const Tutorial& tutorial(CoursesKind k) {
    static const Tutorial kAll[kCoursesKindCount] = {ringsAir(), ringsGround(), ringsAirDribble(), dribble(), slalom(),
                                                     catchDrill()};
    const int i = static_cast<int>(k);
    return kAll[i >= 0 && i < kCoursesKindCount ? i : 0];
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

const char* pathAdvice() {
    return "Ordre conseillé : Anneaux sol et murs (au sol, niveaux 1 à 3), Anneaux aériens en initiation, Dribble sur le "
           "toit, Rattrapage, Slalom, Anneaux sol et murs à partir du niveau 4 (les arches sur les murs, utiles pour le "
           "ceiling shot), puis Air dribble.";
}

const char* pickupText() {
    return "Ta voiture est à l'arrêt et la balle est par terre devant toi (plus tard, elle roule vers toi). Roule "
           "doucement vers elle, nez bien droit : elle monte sur le capot puis sur le toit. Garde la balle au centre du "
           "toit : si elle glisse vers l'arrière, lâche un peu l'accélérateur ; si elle glisse vers l'avant, accélère un "
           "peu. Un ramassage raté ne fait pas baisser le niveau de l'exercice : il a son propre niveau.";
}

const std::vector<const char*>& pickupTech() {
    static const std::vector<const char*> t = {
        "Ramassage : balle posée à 6 m devant toi (600 uu) aux niveaux 1 à 3 du ramassage, puis qui arrive vers toi "
        "depuis 15 m à 13 à 16 km/h (350 à 450 uu/s).",
        "Balle sur le toit 0,5 s (8 s pour y arriver), puis stable 0,3 s : moins de 5 km/h (150 uu/s) par rapport à la "
        "voiture et à moins de 40 uu du centre du toit.",
    };
    return t;
}

std::string fillButtons(const std::string& text, const PlayerProfile& profile, KeyStyle style) {
    struct Slot {
        const char* tag;
        GameAction action;
    };
    static const Slot kSlots[] = {{"{jump}", GameAction::Jump}, {"{boost}", GameAction::Boost}, {"{airroll}", GameAction::AirRoll}};
    std::string out = text;
    for (const Slot& s : kSlots) {
        const std::string tag = s.tag;
        size_t pos = out.find(tag);
        if (pos == std::string::npos) continue;
        std::string key = actionPrompt(profile, s.action, style);
        if (key.empty()) key = s.action == GameAction::Jump ? "saut" : (s.action == GameAction::Boost ? "boost" : "air roll");
        while (pos != std::string::npos) {
            out.replace(pos, tag.size(), key);
            pos = out.find(tag, pos + key.size());
        }
    }
    return out;
}

} // namespace rlt::courses
