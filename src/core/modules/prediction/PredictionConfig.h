#pragma once
// Settings of the "Prédiction" module (ball path with bounces, landing point, first-touch zone, intercept
// advisor and goal warning). Every field is declared once in PredictionModule::describeSettings (cvar
// prefix "rlt_pr_"). This header is included by every module through Settings.h: keep it dependency-light.

#include "core/common/Color.h"

namespace rlt {

struct PredictionConfig {
    // ---- Général
    bool enabled = true;
    float horizon = 3.f;          // seconds predicted ahead (1..6)
    bool onlyAirborne = false;    // hide everything while the ball rolls on the ground
    int unit = 0;                 // SpeedUnit: 0 auto (game setting), 1 km/h, 2 mph, 3 uu/s
    float textScale = 1.2f;       // 3D labels and panel text, multiplied by the HUD scale

    // ---- Trajectoire
    bool showPath = true;
    Color pathColor{80.f, 200.f, 255.f, 230.f};
    float lineWidth = 2.f;
    bool fade = true;             // path fades out towards the horizon
    bool showTimeMarkers = true;  // small marks every 0.5 s
    bool showBounces = true;
    Color bounceColor{255.f, 220.f, 60.f, 230.f};

    // ---- Atterrissage et zone de touche
    bool showLanding = true;
    Color landingColor{255.f, 255.f, 255.f, 230.f};
    bool showZone = true;
    float zoneMin = 120.f;        // ball centre height band (uu above the floor) for the first touch
    float zoneMax = 300.f;
    Color zoneColor{255.f, 170.f, 40.f, 240.f};  // amber: a hue no other marker uses

    // ---- Interception
    bool showIntercept = true;
    Color interceptColor{255.f, 90.f, 255.f, 240.f};
    bool interceptUseBoost = true;

    // ---- Alerte but
    bool showGoalWarning = true;
    Color ownGoalColor{255.f, 60.f, 60.f, 240.f};
    Color opponentGoalColor{60.f, 255.f, 90.f, 240.f};

    // ---- Panneau HUD
    bool showPanel = false;
    float panelX = 0.01f;         // screen fractions
    float panelY = 0.62f;

    // ---- Avancé
    int recomputeTicks = 4;       // recompute period in physics ticks
    float recomputeSpeedDelta = 50.f;  // uu/s of unexpected ball velocity change that forces a recompute
    int drawStepTicks = 4;        // path polyline resolution in ticks
    int arenaMode = 0;            // 0 auto, 1 standard arena, 2 floor only, 3 no collision
    float interceptReach = 150.f;     // horizontal distance car centre -> ball centre at contact
    float groundReachHeight = 170.f;  // highest ball centre touchable without jumping
    float turnPenalty = 0.6f;         // seconds lost for a half turn
};

} // namespace rlt
