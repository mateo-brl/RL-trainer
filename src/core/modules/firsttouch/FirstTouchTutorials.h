#pragma once
// Tutorials of the first-touch drills (tab "Exercices"): what the drill is, each step with what to do, why and how to
// know it is right, the lines the in-game panel shows, what niveau 1 asks, the "parcours complet" and the numbers
// ("Détails techniques" only). French, tutoiement, plain words, no dash characters. Button names go through the
// placeholders {jump} and {boost} (fillButtons) so the player reads his own bindings.

#include <string>
#include <vector>

#include "core/common/Context.h"
#include "core/common/Controls.h"

namespace rlt::ft {

enum class DrillType : int;

struct TutorialStep {
    const char* title;  // "Reste sur place, face à la balle, sans accélérer." (the tab's step list)
    const char* todo;   // "" or how to do it ("À faire")
    const char* why;    // Pourquoi
    const char* ok;     // C'est bon quand
};

struct DrillTutorial {
    const char* intro = "";                // the drill in one or two sentences
    std::vector<TutorialStep> steps;
    std::vector<const char*> panel;        // in-game panel lines, numbered by the HUD ("1/3 Reste face à la balle")
    const char* level1 = "";               // what niveau 1 asks (no numbers)
    std::vector<const char*> tech;         // "Détails techniques": the only place with numbers
    const char* fullTitle = "";            // "Parcours complet : renvoi adverse"
    const char* fullText = "";             // the full sequence in words
    std::vector<const char*> fullTech;     // its numbers
};

const DrillTutorial& drillTutorial(DrillType t);
// In-game panel line `index` of a drill with its number ("2/3 Recule doucement au contact"); clamped.
std::string drillPanelLine(DrillType t, int index);
int drillPanelCount(DrillType t);

// Words every tab uses (fix 8).
const char* lexicon();
// When to move on (global beginner path).
const char* progressRule();
// {jump} / {boost} -> the player's own buttons ("le saut (A)").
std::string fillButtons(const std::string& text, const PlayerProfile& profile, KeyStyle style);

} // namespace rlt::ft
