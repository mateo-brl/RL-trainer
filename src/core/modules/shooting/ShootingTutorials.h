#pragma once
// Tutorials of the shooting drills (tab "Tirs & flicks"): what the drill is, each step with what to do, why and how to
// know it is right, the lines the in-game panel shows, what niveau 1 asks, the "parcours complet" and the numbers
// ("Détails techniques" only). French, tutoiement, plain words, no dash characters. Button names go through the
// placeholders {jump} and {boost} (shootFillButtons) so the player reads his own bindings.

#include <string>
#include <vector>

#include "core/common/Context.h"
#include "core/common/Controls.h"

namespace rlt::shoot {

// The drills of the tab, each with its "Revenir au niveau 1" and its parcours complet.
enum class ShootDrill : int { Flick = 0, Roof = 1, Rolling = 2, Bouncing = 3, Pass = 4, Power = 5, Count = 6 };
constexpr int kShootDrillCount = static_cast<int>(ShootDrill::Count);

const char* shootDrillId(ShootDrill d);     // "flick", "roof", "rolling", "bouncing", "pass", "power"
bool parseShootDrill(const std::string& text, ShootDrill& out);
const char* shootDrillTitle(ShootDrill d);  // "Flick, balle sur le toit", "Flick en roulant", "Cibles, balle roulante"...

struct ShootTutorialStep {
    const char* title;  // the step in a few words
    const char* todo;   // "" or how to do it ("À faire")
    const char* why;    // Pourquoi
    const char* ok;     // C'est bon quand
};

struct ShootTutorial {
    const char* intro = "";
    std::vector<ShootTutorialStep> steps;
    std::vector<const char*> panel;   // in-game panel lines, numbered by the HUD ("2/4 Saute")
    const char* level1 = "";          // what niveau 1 asks (no numbers)
    std::vector<const char*> tech;    // "Détails techniques": the only place with numbers
    const char* fullTitle = "";
    const char* fullText = "";
    std::vector<const char*> fullTech;
};

const ShootTutorial& shootTutorial(ShootDrill d);
// In-game panel line `index` with its number ("2/4 Saute"); clamped.
std::string shootPanelLine(ShootDrill d, int index);
int shootPanelCount(ShootDrill d);

const char* shootLexicon();
const char* shootProgressRule();
// {jump} / {boost} -> the player's own buttons ("saut (A)").
std::string shootFillButtons(const std::string& text, const PlayerProfile& profile, KeyStyle style);

} // namespace rlt::shoot
