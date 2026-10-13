#pragma once
// Catalogue of the movement mechanics: internal mechanic ids (one per kind of graded attempt), trainable
// families (one per UI row / focus / drill / settings toggle) and their French names and explanations.

#include <string>

namespace rlt::mech {

// Every kind of graded attempt. Combos and variants have their own id so stats stay separate.
enum class Mech : int {
    SpeedFlip = 0,
    HalfFlip,
    FlipCancel,
    Wavedash,
    ChainWavedash,
    ZapDash,
    WallDash,
    CeilingDash,
    SpeedZap,          // speedflip followed by a zap dash on landing
    HalfFlipWavedash,  // half-flip followed by a wavedash on landing
    FastAerial,
    DoubleJumpAerial,
    Stall,
    AirSpin,           // helicopter / tornado spin
    Recovery,
    Powerslide,
    BoostEfficiency,
    WallChain,         // consecutive wall dashes
    Count
};
constexpr int kMechCount = static_cast<int>(Mech::Count);

// Trainable family: the unit of focus mode, drills, settings toggles and the UI grid.
enum class Focus : int {
    None = -1,
    SpeedFlip = 0,
    HalfFlip,
    FlipCancel,
    Wavedash,
    ChainDash,
    ZapDash,
    WallDash,
    FastAerial,
    Stall,
    Helicopter,
    Recovery,
    Powerslide,
    Boost,
    Count
};
constexpr int kFocusCount = static_cast<int>(Focus::Count);

struct MechInfo {
    Mech mech;
    const char* key;    // stable id used in skills ("mech.<key>") and persistence keys
    const char* title;  // French display name
    Focus family;
};

struct FocusInfo {
    Focus focus;
    const char* id;       // console / command id ("speedflip", "chaindash"...)
    const char* title;    // French display name
    const char* group;    // settings group: "Sol", "Flips", "Air", "Mur", "Boost"
    const char* what;     // one line: what it is
    const char* key;      // one line: the key to succeed
    Mech primary;         // mechanic whose stats represent the family
};

const MechInfo& mechInfo(Mech m);
const FocusInfo& focusInfo(Focus f);
Focus familyOf(Mech m);
// Parses a command id ("speedflip", "halfflip", ...). Returns Focus::None when unknown.
Focus focusFromId(const std::string& id);
// Comma separated list of every focus id (for help messages).
std::string focusIdList();

// French label of a detector variant ("left-kickoff" -> "gauche kickoff", "side-right" -> "côté droite").
std::string variantLabel(const std::string& variant);

inline int index(Mech m) { return static_cast<int>(m); }
inline int index(Focus f) { return static_cast<int>(f); }

} // namespace rlt::mech
