#pragma once
// Settings of the "Maps" module (workshop maps: catalogue, Kismet profiles, run timing, per-map stats).
// Every field is declared in MapsModule::describeSettings (cvar prefix "rlt_map_").

#include <string>

#include "core/common/Color.h"

namespace rlt {

struct MapsConfig {
    bool enabled = true;

    // ---- Run timing
    bool readKismet = true;       // use the map's Kismet variables (profiles, generic heuristics)
    bool autoLevel = true;        // on unknown maps, use a level-like Kismet counter as splits
    bool fallbackTiming = true;   // own timing when the map gives no start signal
    float startSpeed = 100.f;     // uu/s: own timing starts when the car moves faster after a reset
    int fallbackFinish = 0;       // own timing: 0 = a goal ends the run, 1 = a goal or a reset ends it
    bool announcePb = true;       // chat + toast on a new personal best

    // ---- HUD
    bool hudEnabled = true;
    float hudX = 0.01f;           // screen fractions (top-left zone of docs/ARCHITECTURE.md)
    float hudY = 0.12f;
    float hudScale = 1.f;
    bool hudSplits = true;        // split delta vs personal best
    bool hudProfile = true;       // small line telling which timing profile is used
    float splitHold = 3.f;        // seconds a split delta stays on screen
    Color aheadColor{80, 230, 100, 255};
    Color behindColor{255, 90, 80, 255};

    // ---- Folders (scanned by the plugin glue, MapsService)
    std::string extraFolders;     // ';'-separated absolute folders
    bool scanSteam = true;
    bool scanMods = true;
    bool scanWml = true;
};

} // namespace rlt
