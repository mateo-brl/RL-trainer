#pragma once
// Tutorials of the ball mechanics drills (tab "Guide des mécas"): the principle, each step with what to do, why and
// how to know it is right, the common mistakes, what niveau 1 asks, and the numbers ("Détails techniques" only).
// French, tutoiement, plain words, no dash characters. Button names go through the placeholders {jump}, {boost},
// {airroll}, {slide} (bm::fillButtons) so the player reads his own bindings.

#include <vector>

#include "core/modules/ballmech/Mechanics.h"

namespace rlt::bm {

struct TutorialStep {
    const char* title;   // "Va au mur de côté."
    const char* todo;    // À faire
    const char* why;     // Pourquoi
    const char* ok;      // C'est bon quand
    const char* note;    // "" or a warning ("Attention : ...")
};

struct TutorialBlock {
    const char* title;   // "Départ au plafond (pour travailler la frappe seule)"
    const char* text;
};

struct TutorialError {
    const char* name;    // what the player sees ("Saut en quittant le plafond")
    const char* fix;
};

struct Tutorial {
    const char* principle = "";
    const char* goodToKnow = "";            // "" = none
    const char* before = "";                // drills to do first ("" = none)
    std::vector<TutorialStep> steps;
    std::vector<TutorialBlock> blocks;      // other starts and variants
    std::vector<TutorialError> errors;
    const char* level1 = "";
    std::vector<const char*> tech;          // "Détails techniques": the only place with numbers
};

const Tutorial& tutorial(Mech m);

// Words every tab uses (fix 8).
const char* lexicon();
// When to move on (global beginner path).
const char* progressRule();
// The shared "Ramassage" start of the full sequences of the flick, musty, breezi and air dribble.
const TutorialBlock& pickupBlock();
const std::vector<const char*>& pickupTech();

} // namespace rlt::bm
