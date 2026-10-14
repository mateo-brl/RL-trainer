#include "core/modules/shooting/ShootingTutorials.h"

#include <algorithm>
#include <cctype>

namespace rlt::shoot {

namespace {

ShootTutorial flick() {
    ShootTutorial t;
    t.intro = "Comme en match : la balle est sur ton toit et ta voiture roule déjà vers le but, depuis le milieu du "
              "terrain. Garde la balle stable, choisis ton moment, puis saute et flip pour envoyer un vrai tir au but.";
    t.steps = {
        {"Coupe la ball cam pendant le dribble.", "Appuie sur ta touche de ball cam avant le GO.",
         "Avec la ball cam, la caméra vise la balle posée sur ton toit et le but sort de l'écran. Sans elle, tu vois "
         "le but devant toi.",
         "Tu vois le but devant toi pendant que tu roules."},
        {"Au GO, garde ta vitesse.", "Accélère un peu, juste assez pour rouler à la même vitesse que la balle.",
         "Sans accélérer, ta voiture ralentit et la balle glisse vers l'avant ; en accélérant fort, elle glisse vers "
         "l'arrière.",
         "La balle reste au centre du toit et le panneau affiche « Balle stable »."},
        {"Avant d'arriver devant le but, saute puis flip vers lui.",
         "Appuie une fois sur {jump}, puis tout de suite stick vers l'avant et {jump}.",
         "Le nez de ta voiture bascule et frappe la balle vers le but. Le flick est un tir de loin : trop près du but, "
         "l'essai s'arrête.",
         "La balle part devant toi, plus vite que ta voiture."},
        {"Remets la ball cam après le flick.", "Appuie sur ta touche de ball cam dès que la balle est partie.",
         "Tu suis la balle pour la suite de l'action.", "Le point d'arrivée s'affiche en vert dans le but."},
    };
    t.panel = {"Garde ta vitesse, balle au centre du toit", "Balle stable : saute", "Flip vers le but tout de suite",
               "Remets la ball cam"};
    t.level1 = "Dribble lent, la balle est aidée à rester sur ton toit au tout début, et n'importe quel flick compte "
               "s'il est cadré et assez rapide.";
    t.tech = {
        "Départ en dribble : niveau 1, à 45 à 50 m du but (4500 à 5000 uu), voiture et balle à 32 km/h (900 uu/s). "
        "Niveau 10 : 50 à 55 m (5000 à 5500 uu), 47 km/h (1300 uu/s). L'accélérateur seul tient jusqu'à 50 km/h "
        "environ (1400 uu/s) : pas besoin de boost.",
        "Le flick doit partir avant 12 m du but (1200 uu) : plus près, l'essai s'arrête. À vitesse constante, il te "
        "reste au moins 3,6 s de dribble au niveau 1 et 2,9 s au niveau 10.",
        "Vitesse de la balle demandée, comme un tir en match : 54 km/h (1500 uu/s) au niveau 1, 86 km/h (2400 uu/s) "
        "au niveau 10, et cadré. Niveaux 1 et 2 : n'importe quel flick compte ; le flick demandé compte à partir du "
        "niveau 3.",
        "Aide au départ jusqu'au niveau 6 : pendant 0,6 s, la balle est remise au centre du toit tant qu'elle n'a pas "
        "glissé de plus de 40 uu. Une balle perdue dans la première seconde : l'essai est rejoué, sans compter.",
        "Départ « Voiture à l'arrêt » (réglage Départ), pour apprendre le geste : but à 20 m (2000 uu) au niveau 1, "
        "45 m (4500 uu) au niveau 10 ; vitesse de balle demandée 36 km/h (1000 uu/s) au niveau 1.",
        "La première balle attend que tu appuies sur accélérer, puis compte 3 s ; ensuite 1 s d'attente avant chaque "
        "essai, voiture et balle immobiles.",
    };
    t.fullTitle = "Parcours complet : rattrape, ramasse, porte, flick";
    t.fullText = "Comme en match : ta voiture roule déjà vers le but depuis ta moitié de terrain, derrière la balle qui "
                 "roule devant toi. Rattrape-la un peu plus vite qu'elle pour la ramasser sur ton toit, garde-la au "
                 "centre, porte-la, puis flick vers le but avant d'arriver devant lui. Un ramassage raté ne fait pas "
                 "baisser ton niveau de flick : il a son propre niveau.";
    t.fullTech = {
        "Départ à 55 m du but (5500 uu). Ramassage niveau 1 : balle 5 m devant toi (500 uu) qui roule à 16 km/h "
        "(450 uu/s), ta voiture à 25 km/h (700 uu/s). Niveau 10 : 7 m (700 uu), balle à 19 km/h (520 uu/s), voiture à "
        "33 km/h (920 uu/s), balle un peu décalée de ton axe. Une fois la balle sur le toit, prends ta vitesse de "
        "dribble.",
        "Ramasse : balle sur le toit 0,5 s, en 8 s. Garde-la au centre : moins de 5 km/h (150 uu/s) par rapport à la "
        "voiture et à moins de 40 uu du centre, 0,3 s. Porte-la : 1 s sur le toit en roulant à 11 km/h (300 uu/s) ou "
        "plus, 0,3 s hors du toit tolérée. Flick : la balle touchée par le flip, avant 12 m du but (1200 uu). Cadre : "
        "cadré et au moins 54 km/h (1500 uu/s) au niveau 1 (86 km/h au niveau 10).",
    };
    return t;
}

ShootTutorial roof() {
    ShootTutorial t;
    t.intro = "Comme en match : la balle est sur ton toit et ta voiture roule déjà vers le but. Garde la balle, puis "
              "flick dans la zone en couleur.";
    t.steps = {
        {"Coupe la ball cam pendant le dribble.", "Appuie sur ta touche de ball cam avant le GO.",
         "Avec la ball cam, la caméra vise la balle sur ton toit et le but sort de l'écran.",
         "Tu vois le but et sa zone en couleur pendant que tu roules."},
        {"Au GO, accélère tout doucement, juste assez pour garder ta vitesse.", "",
         "Sans accélérer, la voiture ralentit et la balle glisse vers l'avant ; en accélérant fort, elle glisse vers "
         "l'arrière.",
         "La balle reste au centre du toit."},
        {"Quand elle est stable, saute puis flip vers la cible.",
         "Saute avec {jump}, puis flip vers la zone, avant d'arriver devant le but.", "Le flick part mieux d'une balle "
         "stable.",
         "Le point d'arrivée s'affiche en vert dans la zone."},
        {"Remets la ball cam après le flick.", "", "Tu suis la balle pour la suite de l'action.",
         "La caméra suit de nouveau la balle."},
    };
    t.panel = {"Départ en dribble : garde ta vitesse", "Balle stable : choisis ton moment", "Saute et flip vers la cible",
               "Remets la ball cam"};
    t.level1 = "Dribble lent, grandes zones en bas et au centre, et la balle est aidée à rester sur le toit au tout "
               "début.";
    t.tech = {
        "Même départ en dribble que le flick : 45 à 55 m du but (4500 à 5500 uu), 32 km/h (900 uu/s) au niveau 1 "
        "jusqu'à 47 km/h (1300 uu/s) au niveau 10. Le flick doit partir avant 12 m du but (1200 uu).",
        "Sans accélérer, ta voiture ralentit d'environ 19 km/h par seconde (525 uu/s²) et la balle glisse vers l'avant.",
        "Aide au départ jusqu'au niveau 6 : pendant 0,6 s, la balle est remise au centre du toit tant qu'elle n'a pas "
        "glissé de plus de 40 uu. Une balle perdue dans la première seconde : l'essai est rejoué, sans compter.",
    };
    t.fullTitle = "Parcours complet : rattrape la balle, dribble, flick vers la cible";
    t.fullText = "Ta voiture roule déjà vers le but derrière la balle qui roule devant toi. Rattrape-la et ramasse-la "
                 "sur ton toit, porte-la vers le but en roulant, puis flick dans la zone en couleur avant d'arriver "
                 "devant le but.";
    t.fullTech = {
        "Même ramassage en course que le flick (départ à 55 m du but, 5500 uu). Dribble vers le but : 1,5 s sur le toit "
        "en roulant à 18 à 50 km/h (500 à 1400 uu/s), le nez à moins de 30° de la zone, 0,3 s hors du toit tolérée.",
        "Flick : la balle touchée par le flip, avant 12 m du but (1200 uu). Dans la zone : la balle passe la ligne de "
        "but dans la zone en couleur.",
    };
    return t;
}

ShootTutorial rolling() {
    ShootTutorial t;
    t.intro = "La balle roule vers le côté du but : arrive derrière elle et tire dans la zone en couleur.";
    t.steps = {
        {"Pendant le compte à rebours, regarde la zone en couleur dans le but.", "",
         "Tu choisis où viser avant de bouger.", "Tu sais si c'est à gauche, à droite ou au centre."},
        {"Rattrape la balle en te décalant un peu du côté opposé à la zone.", "",
         "Pour envoyer la balle à gauche, tu dois la toucher sur son côté droit.",
         "Ton nez, la balle et la zone sont presque alignés."},
        {"Tire avec le nez en accélérant.", "", "Un tir appuyé part droit, une touche molle dévie.",
         "Le point d'arrivée s'affiche en vert dans la zone."},
    };
    t.panel = {"Regarde la zone", "Décale-toi du côté opposé", "Tire en accélérant"};
    t.level1 = "Grandes zones, seulement en bas et au centre.";
    t.tech = {
        "Balle à 30 m du but (3000 uu), 18 km/h (500 uu/s) ; ta voiture part 7 m derrière (700 uu) à 16 km/h "
        "(450 uu/s). Zones 1,25 fois plus grandes au niveau 1 ; lucarnes à partir du niveau 4 quand la zone est "
        "aléatoire.",
        "Une balle que tu ne touches pas passe au moins 4 m à côté du but (400 uu) : il faut la rediriger.",
    };
    t.fullTitle = "Parcours complet : conduite et tir";
    t.fullText = "La balle est posée au centre du terrain, ta voiture arrêtée derrière. Va la chercher sans la frapper, "
                 "pousse-la vers le but, puis tire dans la zone.";
    t.fullTech = {
        "Balle au centre du terrain, voiture 25 m derrière (2500 uu) à l'arrêt. Va à la balle sans la frapper : "
        "première touche à moins de 22 km/h d'écart (600 uu/s), en 6 s.",
        "Pousse-la vers le but : elle roule vers le but entre 20 et 35 m de lui (2000 à 3500 uu), toi derrière. Tire : "
        "une touche qui l'envoie à plus de 43 km/h (1200 uu/s) avec un impact prévu. Dans la zone : l'impact prévu "
        "est dans la zone.",
    };
    return t;
}

ShootTutorial bouncing() {
    ShootTutorial t;
    t.intro = "La balle rebondit devant le but : tire-la juste après un rebond.";
    t.steps = {
        {"Regarde la balle tomber et repère ses rebonds.", "",
         "Une balle est facile à frapper juste après un rebond.", "Tu sais où elle retombe la deuxième fois."},
        {"Arrive au deuxième rebond, pas avant.", "", "La balle y est plus basse et plus lente.",
         "La balle touche le sol juste devant ton nez."},
        {"Tire juste après le rebond, quand elle remonte.", "", "Elle part tendue vers le but.",
         "Le point d'arrivée s'affiche en vert dans la zone."},
    };
    t.panel = {"Regarde les rebonds", "Arrive au deuxième rebond", "Tire quand elle remonte"};
    t.level1 = "Balle lâchée de pas très haut, zones en bas et au centre.";
    t.tech = {
        "Balle à 16 m du but (1600 uu), lâchée de 4 m (400 uu) ; voiture 12 m derrière (1200 uu), à l'arrêt : elle "
        "peut être au deuxième rebond environ 0,7 s avant la balle. Niveau 10 : 9 m de haut, 23 m du but, voiture "
        "20 m derrière.",
        "Chaque rebond fait perdre 40 % de vitesse verticale. Avec « Cercles au sol » (réglages, Affichage), les deux "
        "prochains rebonds sont dessinés au sol.",
    };
    t.fullTitle = "Parcours complet : rebond devant le but";
    t.fullText = "La balle tombe de haut devant le but, ta voiture est loin derrière. Lis les rebonds, arrive au "
                 "deuxième, tire quand elle remonte.";
    t.fullTech = {
        "Balle lâchée de 9 m (900 uu) à 25 m du but (2500 uu), voiture 30 m derrière (3000 uu) à l'arrêt.",
        "Lis les rebonds : nez vers le deuxième rebond en 1 s. Arrive au rebond : à moins de 300 uu du deuxième "
        "rebond quand la balle touche le sol. Tire en montée : touche moins de 0,3 s après un rebond, balle envoyée à "
        "plus de 43 km/h (1200 uu/s). Dans la zone.",
    };
    return t;
}

ShootTutorial pass() {
    ShootTutorial t;
    t.intro = "Un centre roule devant le but : arrive au bon moment pour le reprendre directement.";
    t.steps = {
        {"Au GO, repère où ta route croise celle du centre.", "",
         "C'est là que tu dois arriver, juste avant la balle.", "Tu as choisi ton point avant d'accélérer."},
        {"Accélère vers ce point sans boost.", "",
         "Sans boost, tu y arrives juste un peu avant la balle ; avec, tu arrives beaucoup trop tôt.",
         "Tu es au point et la balle est à deux longueurs de voiture."},
        {"Tire du premier coup vers la zone.", "", "Si tu contrôles d'abord, le défenseur a le temps de revenir.",
         "Le point d'arrivée s'affiche en vert dans la zone."},
    };
    t.panel = {"Regarde le point de rencontre", "Accélère sans boost", "Tire du premier coup"};
    t.level1 = "Centre lent, tu pars à l'arrêt.";
    t.tech = {
        "Balle à 15 m sur le côté (1500 uu), 20 km/h (550 uu/s) ; elle croise le milieu du but en 2,7 s ; ta voiture, "
        "partie à l'arrêt 33 m devant le but (3300 uu), peut y être en 2,0 s.",
        "Niveau 10 : balle à 26 m sur le côté, 47 km/h (1300 uu/s), voiture à 39 m du but. Ta voiture peut toujours "
        "arriver 0,3 à 0,8 s avant la balle. Avec « Cercles au sol » (réglages, Affichage), le point de rencontre est "
        "dessiné au sol.",
    };
    t.fullTitle = "Parcours complet : appel de balle";
    t.fullText = "Tu pars du côté du second poteau ; le centre part du coin opposé vers le point de penalty. Démarre au "
                 "bon moment, coupe sa trajectoire, et tire du premier coup.";
    t.fullTech = {
        "Voiture à l'arrêt à 15 m sur le côté (1500 uu) et 40 m du but (4000 uu). Centre depuis le coin (33 m sur le "
        "côté, 10 m du but) à 32 km/h (900 uu/s), vers un point à 12 m devant le but (1200 uu).",
        "Démarre au bon moment : plus de 18 km/h (500 uu/s) avant que la balle soit à mi-chemin. Coupe la trajectoire : "
        "à moins de 300 uu du point de rencontre, 0 à 0,5 s avant la balle. Tire du premier coup : ta première touche "
        "envoie la balle à plus de 43 km/h (1200 uu/s) vers le but. Dans la zone.",
    };
    return t;
}

ShootTutorial power() {
    ShootTutorial t;
    t.intro = "La balle roule vers toi : frappe-la le plus fort possible.";
    t.steps = {
        {"Boost à fond tout de suite, tout droit vers la balle.", "Garde {boost} appuyé.",
         "La vitesse de ta voiture fait la puissance du tir.", "Ta voiture laisse une traînée de vitesse maximale."},
        {"Aligne la balle et le but.", "", "Un tir de travers perd de la puissance.",
         "La balle est pile entre ton nez et le but."},
        {"Juste avant le contact, fais un flip avant dans la balle.", "Stick vers l'avant et {jump}, deux fois.",
         "Le flip ajoute de la vitesse au moment du choc.", "La balle part plus vite que ta voiture."},
    };
    t.panel = {"Boost à fond", "Aligne balle et but", "Flip avant au dernier moment"};
    t.level1 = "L'objectif de vitesse est bas au début, il monte quand tu réussis.";
    t.tech = {
        "Balle à 30 m qui roule vers toi à 25 km/h (694 uu/s) ; contact vers 1,7 s avec une voiture à 76 km/h "
        "(2100 uu/s).",
        "Objectif : 70 km/h au niveau 1, 90 km/h au milieu, jusqu'à 110 km/h au niveau 10 ; 3 réussites font monter "
        "l'objectif d'environ 5 km/h. Avec « Cercles au sol » (réglages, Affichage), un cercle d'élan est dessiné 12 m "
        "avant le point de rencontre (1200 uu) : sois à fond quand tu le passes.",
        "Réglage « Objectif adaptatif » coupé : l'objectif fixe des réglages (90 km/h par défaut).",
    };
    t.fullTitle = "Parcours complet : frappe de dégagement";
    t.fullText = "La balle arrive de la moitié adverse. Prends de l'élan, aligne-toi sur le but, flip dans la balle et "
                 "frappe au-dessus de l'objectif de ton niveau.";
    t.fullTech = {
        "Prends de l'élan : plus de 58 km/h (1600 uu/s) quand la balle est à 8 m (800 uu). Aligne : nez à moins de 15° "
        "de la ligne balle-but. Flip dans la balle : flip avant commencé moins de 0,25 s avant le contact. Puissance : "
        "au-dessus de l'objectif de ton niveau.",
    };
    return t;
}

const ShootTutorial& tutorialAt(int i) {
    static const ShootTutorial kAll[kShootDrillCount] = {flick(), roof(), rolling(), bouncing(), pass(), power()};
    return kAll[std::clamp(i, 0, kShootDrillCount - 1)];
}

std::string replaceAll(std::string s, const std::string& from, const std::string& to) {
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
    return s;
}

std::string withKey(const char* noun, const std::string& key) {
    return key.empty() ? std::string(noun) : std::string(noun) + " (" + key + ")";
}

} // namespace

const char* shootDrillId(ShootDrill d) {
    switch (d) {
    case ShootDrill::Flick: return "flick";
    case ShootDrill::Roof: return "roof";
    case ShootDrill::Rolling: return "rolling";
    case ShootDrill::Bouncing: return "bouncing";
    case ShootDrill::Pass: return "pass";
    case ShootDrill::Power: return "power";
    default: return "flick";
    }
}

bool parseShootDrill(const std::string& text, ShootDrill& out) {
    std::string s;
    for (char c : text) s.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    for (int i = 0; i < kShootDrillCount; ++i) {
        if (s == shootDrillId(static_cast<ShootDrill>(i))) {
            out = static_cast<ShootDrill>(i);
            return true;
        }
    }
    return false;
}

const char* shootDrillTitle(ShootDrill d) {
    switch (d) {
    case ShootDrill::Flick: return "Flick, balle sur le toit";
    case ShootDrill::Roof: return "Flick en dribble vers la cible";
    case ShootDrill::Rolling: return "Cibles, balle roulante";
    case ShootDrill::Bouncing: return "Cibles, balle qui rebondit";
    case ShootDrill::Pass: return "Centre en travers";
    case ShootDrill::Power: return "Puissance";
    default: return "";
    }
}

const ShootTutorial& shootTutorial(ShootDrill d) { return tutorialAt(static_cast<int>(d)); }

int shootPanelCount(ShootDrill d) { return static_cast<int>(shootTutorial(d).panel.size()); }

std::string shootPanelLine(ShootDrill d, int index) {
    const auto& p = shootTutorial(d).panel;
    if (p.empty()) return {};
    const int n = static_cast<int>(p.size());
    const int i = std::clamp(index, 0, n - 1);
    return std::to_string(i + 1) + "/" + std::to_string(n) + " " + p[static_cast<size_t>(i)];
}

const char* shootBallCamHint() { return "Coupe la ball cam pendant le dribble pour voir le but, remets-la après le flick"; }

const char* shootLexicon() {
    return "Flip : un saut, puis un deuxième appui sur saut avec le stick penché. La voiture fait une roulade et prend "
           "de la vitesse. Annuler un flip : tirer le stick dans l'autre sens juste après le flip pour arrêter la "
           "roulade. Air roll : faire tourner la voiture sur elle-même autour de son nez, en l'air (souvent le même "
           "bouton que le powerslide). Powerslide : la glissade au sol. Supersonique : la vitesse maximale, quand une "
           "traînée apparaît derrière ta voiture. Kickoff : l'engagement au centre.";
}

const char* shootProgressRule() {
    return "Passe à l'exercice suivant quand tu réussis environ 3 essais sur 4 au niveau 4. Tu peux toujours revenir en "
           "arrière.";
}

std::string shootFillButtons(const std::string& text, const PlayerProfile& profile, KeyStyle style) {
    if (text.find('{') == std::string::npos) return text;
    std::string s = text;
    s = replaceAll(s, "{jump}", withKey("saut", actionKeyLabel(profile, GameAction::Jump, style)));
    s = replaceAll(s, "{boost}", withKey("boost", actionKeyLabel(profile, GameAction::Boost, style)));
    return s;
}

} // namespace rlt::shoot
