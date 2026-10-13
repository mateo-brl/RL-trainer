#pragma once
// What the coach knows about skill ids reported by the other modules: family (prefix before the first '.'),
// French names, and which exercise trains a skill (a command of another module, posted by the UI).
// Unknown ids are handled generically: grouped by prefix, named from their last segment, not launchable.

#include <string>
#include <vector>

#include "core/modules/coach/CoachTypes.h"

namespace rlt {
namespace coach {

// "mech.speedflip" -> "mech", "kickoff" -> "kickoff", "" -> "autre".
std::string skillFamily(const std::string& skill);
// "mech" -> "Mécas mouvement", unknown prefixes are capitalised.
std::string familyLabel(const std::string& family);
// Display order of the families in the UI (known families first, then alphabetical).
int familyOrder(const std::string& family);
// Stable French name of a skill the coach knows (the catalog below, or a known parametrised family such as
// "kickoff.<kind>" or "shoot.flick.<type>"). Returns false for other ids (workshop maps, unknown mechanics,
// unknown families): their name comes from the results (AttemptResult::title is a per-attempt headline such
// as "Kickoff (diagonale gauche)" or "Flick : raté", so it never names a known skill).
bool catalogSkillTitle(const std::string& skill, std::string& out);
// Catalog name when known, else a generic name from the id: "mech.halfflip" -> "Half-flip", "x.foo_bar" -> "Foo bar".
std::string defaultSkillTitle(const std::string& skill);
// Exercise for a skill (see the module spec table). Only ids the target module accepts are launched: mechanics
// detected in freeplay without a drill of their own map to the closest drill (ceilingdash -> walldash, ...).
// Invalid LaunchSpec when the skill has no exercise.
LaunchSpec launchForSkill(const std::string& skill);
// Modules that already announce their own personal records (toast and/or chat): the coach does not repeat
// those announcements unless the player asks for it (CoachConfig::recordAnnounceAll).
bool moduleAnnouncesRecords(const std::string& module);
// Balanced plan for a new player: first touch (roll in), random kickoff, speedflip.
std::vector<PlanItem> starterPlan();

} // namespace coach
} // namespace rlt
