#include "core/modules/firsttouch/FirstTouchTutorials.h"

#include <algorithm>

#include "core/modules/firsttouch/FirstTouchDrills.h"

namespace rlt::ft {

namespace {

DrillTutorial rollIn() {
    DrillTutorial t;
    t.intro = "La balle roule vers toi : arrête-la contre ta voiture sans qu'elle reparte loin.";
    t.steps = {
        {"Reste sur place, face à la balle, sans accélérer.", "",
         "Si tu avances, ta vitesse s'ajoute à la sienne et elle rebondit loin.",
         "La balle arrive droit sur ton nez et ta voiture est presque arrêtée."},
        {"Juste avant le contact, recule doucement.", "",
         "En reculant un peu moins vite qu'elle, tu amortis le choc, comme quand tu attrapes un ballon en ramenant les "
         "mains.",
         "La balle s'arrête presque collée à ton nez."},
        {"Repars doucement vers l'avant, la balle devant toi.", "", "C'est le début d'un dribble ou d'une passe.",
         "La balle reste devant ton capot."},
    };
    t.panel = {"Reste face à la balle", "Recule doucement au contact", "Repars avec elle"};
    t.level1 = "Balle lente qui vient droit sur toi, seule la douceur de la touche compte. Garder la balle compte à "
               "partir du niveau 4.";
    t.tech = {
        "Balle à 16 m (1600 uu), 13 à 16 km/h (350 à 450 uu/s), droit sur ton nez au niveau 1. Niveau 10 : 19 à 25 m, "
        "29 à 50 km/h (800 à 1400 uu/s), jusqu'à 7 m sur le côté.",
        "Avant chaque balle : 1 s d'attente, voiture et balle immobiles (0,5 s aux niveaux 4 à 6, rien ensuite). Le "
        "réglage « Trajectoire de la balle avant l'essai » (coupé par défaut) dessine son chemin pendant l'attente. La "
        "première balle attend que tu appuies sur accélérer, puis compte 3 s.",
        "Touche parfaite sous 5 km/h d'écart de vitesse (150 uu/s), aucun point au-delà de 22 km/h (600 uu/s). "
        "Réussite au niveau 1 : 50/100 sur la touche seule. 9 s pour toucher.",
        "Si ta position de départ a moins de 35 m libres devant toi (3500 uu) ou est à moins de 12 m d'un mur de côté "
        "(1200 uu), l'exercice part de ta moitié de terrain, face au but adverse.",
    };
    t.fullTitle = "Parcours complet : renvoi adverse";
    t.fullText = "Un adversaire dégage la balle vers toi depuis loin et un peu sur le côté. Aligne-toi sur sa route, "
                 "ralentis avant qu'elle arrive, puis amortis-la. À partir du niveau 4, garde-la près de toi ensuite.";
    t.fullTech = {
        "Voiture arrêtée dans ta moitié, balle à 35 m devant (3500 uu) et 4 à 6 m sur le côté (400 à 600 uu), qui roule "
        "vers toi à 18 à 25 km/h (500 à 700 uu/s).",
        "Aligne-toi : en 2 s, nez face à la balle (moins de 20° d'écart) et à moins de 120 uu de sa route. Ralentis : "
        "moins de 11 km/h (300 uu/s) quand elle est à 6 m (600 uu). Touche douce : moins de 16 km/h d'écart (450 uu/s, "
        "11 km/h ou 300 uu/s à partir du niveau 4), contact à moins de 45° du point idéal. Garde-la : à moins de 350 uu "
        "pendant 60 % de la seconde qui suit.",
    };
    return t;
}

DrillTutorial rollAway() {
    DrillTutorial t;
    t.intro = "La balle part devant toi : rattrape-la et touche-la à sa vitesse pour qu'elle reste devant toi.";
    t.steps = {
        {"Accélère tout droit derrière la balle.", "", "Elle s'éloigne, il faut combler l'écart.",
         "L'écart diminue vite."},
        {"À deux longueurs de voiture, relâche l'accélérateur.", "",
         "Si tu arrives trop vite, tu la frappes et elle part loin.",
         "La barre de vitesse est verte : vous roulez presque à la même vitesse."},
        {"Touche le bas de la balle avec le nez, tout doucement.", "",
         "Une touche par le bas la garde près de toi, une touche au milieu la pousse.",
         "La balle reste à moins d'une longueur de voiture."},
    };
    t.panel = {"Rattrape-la", "Lâche l'accélérateur près d'elle", "Touche douce, même vitesse"};
    t.level1 = "Balle lente, tout droit devant toi, avec beaucoup de place.";
    t.tech = {
        "Balle à 4 m devant toi (400 uu), 16 à 20 km/h (450 à 550 uu/s), tout droit au niveau 1. Niveau 10 : 29 à "
        "54 km/h (800 à 1500 uu/s), jusqu'à 35° sur le côté.",
        "Au moins 45 m libres devant toi (4500 uu), sinon l'exercice part de ta moitié de terrain, face au but adverse. "
        "1 s d'attente avant la balle aux niveaux 1 à 3 (0,5 s aux niveaux 4 à 6).",
        "Barre de vitesse affichée à moins de 8 m de la balle (800 uu) : verte sous 5 km/h d'écart (150 uu/s). 9 s "
        "pour toucher.",
    };
    t.fullTitle = "Parcours complet : récupère un dégagement";
    t.fullText = "La balle est posée juste devant toi et part d'un coup, loin devant. Rattrape-la, cale ta vitesse sur "
                 "la sienne, puis touche-la tout doucement.";
    t.fullTech = {
        "Balle à 2 m devant toi (200 uu), lancée à 32 à 43 km/h (900 à 1200 uu/s), jusqu'à 20° sur le côté.",
        "Rattrape-la : en 4 s, à 2 à 5 m derrière elle (200 à 500 uu). Cale ta vitesse : moins de 5 km/h d'écart "
        "(150 uu/s) pendant 0,5 s à cette distance. Touche douce : moins de 11 km/h d'écart (300 uu/s), contact à "
        "moins de 35° du point idéal. Garde-la à partir du niveau 4.",
    };
    return t;
}

DrillTutorial pass() {
    DrillTutorial t;
    t.intro = "Une passe arrive par le côté. Au moment de la toucher, tu dois rouler dans le même sens qu'elle.";
    t.steps = {
        {"Regarde d'où vient la balle et le point de rencontre (le cercle vert au sol).", "",
         "Tu dois savoir où aller avant de bouger.", "Tu vois le point de rencontre."},
        {"Roule vers ce point et tourne doucement pour partir dans le sens de la balle.", "",
         "Si tu la touches de travers, elle part sur le côté.",
         "Ta voiture et la balle vont dans la même direction."},
        {"Laisse la balle venir contre ton nez en roulant avec elle.", "",
         "À la même vitesse et dans le même sens, la touche est douce.", "La balle reste devant toi après le contact."},
    };
    t.panel = {"Regarde le point de rencontre", "Tourne dans le sens de la balle", "Roule avec elle au contact"};
    t.level1 = "La passe arrive en biais vers l'avant, lentement : tu tournes à peine.";
    t.tech = {
        "Point de rencontre à 10 m devant toi (1000 uu), passe à 14 à 18 km/h (400 à 500 uu/s), en biais de 35° vers "
        "l'avant au niveau 1, de plus en plus de travers jusqu'au niveau 6, puis la passe revient vers toi (20° au "
        "niveau 10).",
        "La balle met 2,6 s pour arriver au point de rencontre ; ta voiture peut y être vers 1,3 s. 1 s d'attente "
        "avant la balle aux niveaux 1 à 3, avec le point de rencontre dessiné. 9 s pour toucher.",
    };
    t.fullTitle = "Parcours complet : passe d'un coéquipier";
    t.fullText = "Un coéquipier te passe la balle depuis l'autre aile. Lis la passe, va au point de rencontre avant la "
                 "balle, puis accompagne-la dans son sens.";
    t.fullTech = {
        "Voiture arrêtée sur une aile, point de rencontre à 18 m devant toi (1800 uu). La passe part de l'autre aile à "
        "25 à 32 km/h (700 à 900 uu/s), en biais de 20° vers l'avant, et met environ 4 s à arriver.",
        "Lis la passe : en 1 s, nez à moins de 30° du point de rencontre. Va au point : à moins de 400 uu de lui au "
        "moins 0,4 s avant la balle. Accompagne : au contact, moins de 35° entre ta route et celle de la balle et moins "
        "de 14 km/h d'écart (400 uu/s). Garde-la à partir du niveau 4.",
    };
    return t;
}

DrillTutorial lob() {
    DrillTutorial t;
    t.intro = "Une balle en cloche retombe devant toi. Le bon geste : la laisser rebondir une fois, puis la cueillir "
              "quand elle redescend.";
    t.steps = {
        {"Regarde le cercle au sol, c'est là que la balle tombe.", "",
         "Savoir où elle tombe te laisse le temps de te placer.", "Tu roules vers le cercle."},
        {"Arrête-toi juste derrière le cercle, côté voiture.", "",
         "Tu dois être placé quand elle tombe, pas encore en train de rouler.",
         "Ta voiture est presque arrêtée, le cercle juste devant ton nez."},
        {"Laisse-la rebondir une fois devant toi.", "",
         "Si elle tombe directement sur ton capot, elle tape trop fort et repart loin ; le rebond au sol lui enlève de "
         "la vitesse.",
         "Elle remonte moins haut qu'avant."},
        {"Avance un peu et touche-la quand elle redescend après le rebond.", "",
         "À ce moment elle tombe doucement et ta touche l'amortit.", "Elle reste devant toi."},
    };
    t.panel = {"Va au cercle", "Arrête-toi juste derrière", "Laisse-la rebondir", "Touche-la quand elle redescend"};
    t.level1 = "Lob bas et lent qui tombe à 9 m devant toi.";
    t.tech = {
        "La balle monte à 4,5 à 5,5 m (450 à 550 uu) et tombe à 8 à 10 m devant toi (800 à 1000 uu) environ 2 s après "
        "le départ. Le cercle montre où elle tombe. 1 s d'attente avant la balle aux niveaux 1 à 3.",
        "Tombée directe sur un capot immobile : 23 à 30 km/h d'écart (630 à 850 uu/s), note 42. Après un rebond : 12 "
        "à 13 km/h (340 à 370 uu/s), note 71. Réussite au niveau 1 : 50/100. 9 s pour toucher.",
    };
    t.fullTitle = "Parcours complet : lob adverse";
    t.fullText = "Un adversaire lobe la balle de loin vers toi. Repère où elle tombe, place-toi, laisse-la rebondir "
                 "une fois et cueille-la quand elle redescend.";
    t.fullTech = {
        "Voiture arrêtée à 25 m du point de chute (2500 uu), face à lui. Lob de loin qui monte à 5,5 à 6 m (550 à 620 "
        "uu) : plus haut, la balle redescendrait trop vite après le rebond pour une touche douce.",
        "Repère : nez à moins de 25° du cercle en 1,5 s. Place-toi : à moins de 300 uu de l'endroit où elle redescend, "
        "moins de 14 km/h (400 uu/s), avant le premier rebond. Touche : après exactement un rebond, balle qui "
        "descend, moins de 16 km/h d'écart (450 uu/s). Garde-la à partir du niveau 4.",
    };
    return t;
}

DrillTutorial wall() {
    DrillTutorial t;
    t.intro = "La balle descend du mur sur le côté. Sois sous elle quand elle touche le sol, puis contrôle-la.";
    t.steps = {
        {"Regarde la balle sur le mur et le cercle au sol.", "",
         "En quittant le mur, la balle tombe toujours un peu vers le milieu du terrain.",
         "Tu sais où est le cercle avant de bouger."},
        {"Roule vers le cercle sans foncer.", "", "Tu dois arriver avant la balle, pas en même temps qu'elle.",
         "Tu es près du cercle alors que la balle est encore en l'air."},
        {"Freine et laisse-la faire son premier rebond devant toi.", "",
         "Si tu arrives vite, tu la frappes au lieu de la contrôler.",
         "Ta voiture est presque arrêtée au moment où elle touche le sol."},
        {"Touche-la doucement quand elle redescend après le rebond.", "",
         "Comme pour le lob, c'est le moment où elle est la plus lente.", "Elle reste devant toi."},
    };
    t.panel = {"Regarde le cercle", "Va dessous sans foncer", "Freine", "Touche douce après le rebond"};
    t.level1 = "Ta voiture est garée parallèle au mur, tout près, et la balle descend de bas, doucement.";
    t.tech = {
        "Voiture garée parallèle au mur, à 11 m de lui (1100 uu) au niveau 1, 18 m au niveau 10, tournée vers le côté "
        "où il y a le plus de place. Le mur change de côté à chaque essai.",
        "Balle à 4 à 5 m de haut (400 à 500 uu), 7 à 9 m devant ta voiture, repoussée du mur à 7 à 9 km/h (200 à 250 "
        "uu/s). Premier contact possible vers 1,5 s, après un rebond. 1 s d'attente avec le cercle dessiné. 9 s pour "
        "toucher.",
        "Si « Replacer la voiture » est coupé : la balle va sur le mur de côté vers lequel tu regardes, seulement quand "
        "il est à moins de 20 m (2000 uu).",
    };
    t.fullTitle = "Parcours complet : balle qui sort du mur";
    t.fullText = "Tu pars du milieu de ta moitié. La balle est en haut d'un mur de côté et s'en décolle. Regarde-la, va "
                 "sans foncer là où elle redescend, freine, laisse-la rebondir puis amortis-la.";
    t.fullTech = {
        "Voiture arrêtée au milieu de ta moitié. Balle tenue 1 s en haut d'un mur de côté (9 à 11 m de haut, 900 à "
        "1100 uu), 25 m plus loin (2500 uu), puis repoussée à 7 à 11 km/h (200 à 300 uu/s).",
        "Elle tombe de haut : après son premier rebond elle redescend encore trop vite pour une touche douce. Le "
        "cercle montre l'endroit où elle redescend assez lentement, après ses rebonds.",
        "Regarde le mur : nez à moins de 30° du cercle en 1,2 s (0,8 s à partir du niveau 4). Va au cercle : à moins "
        "de 500 uu de lui au moins 0,3 s avant la balle. Freine : moins de 18 km/h (500 uu/s) pendant 0,2 s. Touche "
        "douce : moins de 16 km/h d'écart (450 uu/s). Garde-la à partir du niveau 4.",
    };
    return t;
}

DrillTutorial high() {
    DrillTutorial t;
    t.intro = "La balle est en l'air devant toi. Le secret : la toucher quand elle et toi êtes presque immobiles en "
              "l'air, en haut de ton saut. Sinon, tu la frappes trop fort.";
    t.steps = {
        {"Place-toi sous la balle.",
         "Roule doucement jusqu'à avoir la balle juste au-dessus de l'avant de ta voiture, puis arrête-toi.",
         "Tu dois être placé avant de sauter, pas encore en train de rouler.",
         "Le panneau affiche « Sous la balle »."},
        {"Saute en gardant {jump} appuyé.", "",
         "Au niveau 1, ton saut lance la balle vers le haut. Vous montez ensemble et vous vous retrouvez en haut en "
         "même temps.",
         "Ta voiture monte avec la balle juste au-dessus."},
        {"Touche-la avec l'avant du toit, en haut de ton saut.", "",
         "En haut du saut, ta voiture ne monte plus et la balle redescend doucement : le choc est léger.",
         "La balle retombe devant toi au lieu de partir loin."},
        {"Retombe sur tes roues et garde la balle devant toi.", "", "C'est le début de ton contrôle.",
         "La balle reste près de ton capot."},
    };
    t.panel = {"Mets-toi sous la balle", "Saute (bouton gardé)", "Touche-la en haut de ton saut"};
    t.level1 = "La balle t'attend en l'air et ne part que quand tu sautes sous elle. À partir du niveau 4, elle part "
               "dès que tu es dessous : à toi de choisir le moment du saut.";
    t.tech = {
        "Niveaux 1 à 3 : balle suspendue à 10 m devant toi (1000 uu), centre à 3,5 m de haut (345 uu). Quand tu sautes "
        "avec la balle au-dessus de l'avant de ton toit (0 à 120 uu devant l'avant de ta voiture), elle remonte à "
        "10 km/h (280 uu/s). Contact vers 0,7 s après le saut, environ 11 km/h d'écart (305 uu/s), note 80 ; avec 0,3 s "
        "de retard, encore 55.",
        "Sans saut, la balle est lâchée au bout de 1,5 s et tombe sur ton toit trop fort : note sous 50.",
        "Niveaux 4 à 6 : la balle part vers le haut dès qu'elle est au-dessus de l'avant de ton toit, à 16 km/h au "
        "niveau 4 (450 uu/s), 13 km/h au niveau 6 (365 uu/s). Saute dans la demi-seconde qui suit (0,4 s au niveau 6).",
        "Niveaux 7 à 10 : balle lancée devant toi, 3,5 à 8 m de haut, qui monte encore. 1 s d'attente avant la balle "
        "aux niveaux 1 à 3.",
    };
    t.fullTitle = "Parcours complet : lob haut adverse";
    t.fullText = "Un lob haut retombe devant toi. Place-toi là où elle remonte après son rebond, laisse-la rebondir, "
                 "puis saute pour la prendre en haut et retombe avec elle.";
    t.fullTech = {
        "Lob lancé de 25 m devant toi (2500 uu), qui monte à 7 à 8 m (700 à 800 uu) et tombe vers 6 m devant ta "
        "voiture. Après le rebond, elle remonte à environ 3,3 m : un saut simple, bouton gardé, la prend en haut.",
        "Repère : nez à moins de 25° du point de chute en 1 s. Place-toi : à moins de 200 uu du point où elle remonte "
        "au plus haut, moins de 11 km/h (300 uu/s), avant le rebond. Saute : contact voiture en l'air, balle presque "
        "immobile en hauteur (moins de 250 uu/s), moins de 14 km/h d'écart (400 uu/s). Retombe : balle à moins de "
        "400 uu quand tes roues touchent le sol.",
    };
    return t;
}

DrillTutorial mix() {
    DrillTutorial t;
    t.intro = "Chaque balle est différente : roulante, passe, lob, mur ou balle haute.";
    t.steps = {
        {"Pendant la pause, lis le nom de la prochaine balle.", "", "Tu sais quel geste préparer.",
         "Tu sais déjà si tu vas freiner, rattraper ou te placer dessous."},
        {"Fais le geste de cet exercice (ses étapes sont dans sa fiche).", "",
         "Chaque balle a sa bonne façon d'être touchée.", "Tu arrives placé avant la balle."},
        {"Touche-la en douceur et garde-la devant toi.", "", "C'est ce qui compte en match.",
         "La balle reste près de toi."},
    };
    t.panel = {"Lis le nom", "Freine, rattrape ou place-toi dessous", "Touche douce"};
    t.level1 = "Seuls les exercices que tu sais déjà faire sortent : les deux exercices Débutant, puis chaque exercice "
               "dont tu as atteint le niveau 4.";
    t.tech = {
        "Jamais deux fois le même de suite ; les exercices où tu réussis le moins sortent plus souvent ; le nom "
        "s'affiche pendant 1 s avant chaque balle.",
        "Un exercice entre dans le mix quand c'est un exercice Débutant, quand tu as atteint son niveau 4 ou quand tu "
        "l'as réussi 5 fois. Chaque balle utilise le niveau de son exercice.",
    };
    t.fullTitle = "Parcours complet : séquence de match";
    t.fullText = "Cinq balles d'affilée, sans que ta voiture soit replacée : chaque balle arrive par rapport à là où tu "
                 "es et où tu vas, comme en match. Réussis-en au moins quatre.";
    t.fullTech = {
        "5 balles, la suivante 2 s après la fin de la précédente, toujours d'une direction avec au moins 30 m de place "
        "(3000 uu). Chaque balle est jugée comme dans son exercice, à son niveau. Réussite : 4 balles sur 5.",
    };
    return t;
}

const DrillTutorial& tutorialAt(int i) {
    static const DrillTutorial kAll[7] = {rollIn(), rollAway(), pass(), lob(), wall(), high(), mix()};
    return kAll[std::clamp(i, 0, 6)];
}

std::string withKey(const char* noun, const std::string& key) {
    return key.empty() ? std::string(noun) : std::string(noun) + " (" + key + ")";
}

std::string replaceAll(std::string s, const std::string& from, const std::string& to) {
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
    return s;
}

} // namespace

const DrillTutorial& drillTutorial(DrillType t) { return tutorialAt(static_cast<int>(t)); }

int drillPanelCount(DrillType t) { return static_cast<int>(drillTutorial(t).panel.size()); }

std::string drillPanelLine(DrillType t, int index) {
    const auto& p = drillTutorial(t).panel;
    if (p.empty()) return {};
    const int n = static_cast<int>(p.size());
    const int i = std::clamp(index, 0, n - 1);
    return std::to_string(i + 1) + "/" + std::to_string(n) + " " + p[static_cast<size_t>(i)];
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

std::string fillButtons(const std::string& text, const PlayerProfile& profile, KeyStyle style) {
    if (text.find('{') == std::string::npos) return text;
    std::string s = text;
    s = replaceAll(s, "{jump}", withKey("saut", actionKeyLabel(profile, GameAction::Jump, style)));
    s = replaceAll(s, "{boost}", withKey("boost", actionKeyLabel(profile, GameAction::Boost, style)));
    return s;
}

} // namespace rlt::ft
