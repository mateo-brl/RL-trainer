#pragma once
// Tutorials of the Parcours exercises (tab "Parcours"): what the exercise is, each step with what to do, why and how
// to know it is right, the in-game panel lines, what niveau 1 asks, the "parcours complet" and the numbers ("Détails
// techniques" only). French, tutoiement, plain words, no dash characters. Button names go through the placeholders
// {jump}, {boost}, {airroll} (fillButtons) so the player reads his own bindings.

#include <string>
#include <vector>

#include "core/common/Context.h"
#include "core/common/Controls.h"
#include "core/modules/courses/CoursesModule.h"

namespace rlt::courses {

struct TutorialStep {
    const char* todo;  // "Étape 1 : saute, puis lève le nez."
    const char* why;   // Pourquoi
    const char* ok;    // C'est bon quand
};

struct Tutorial {
    const char* intro = "";                // one or two sentences: what the exercise is
    const char* before = "";               // exercises to do first ("" = none)
    std::vector<TutorialStep> steps;
    std::vector<const char*> panel;        // "PANNEAU EN JEU" lines, as the HUD shows them
    const char* level1 = "";               // what niveau 1 asks, in plain words
    const char* full = "";                 // the "parcours complet", in plain words
    std::vector<const char*> fullSteps;    // its steps, as the HUD numbers them
    std::vector<const char*> tech;         // "Détails techniques": the only place with numbers
};

const Tutorial& tutorial(CoursesKind k);

// Words every tab explains (fix 8).
const char* lexicon();
// When to move on (global beginner path).
const char* progressRule();
// Order of the exercises of this tab on the beginner path.
const char* pathAdvice();
// The shared "Ramassage" start of the full sequences of the air dribble, the roof dribble and the slalom.
const char* pickupText();
const std::vector<const char*>& pickupTech();

// Replaces {jump}, {boost} and {airroll} with the player's own buttons.
std::string fillButtons(const std::string& text, const PlayerProfile& profile, KeyStyle style);

} // namespace rlt::courses
