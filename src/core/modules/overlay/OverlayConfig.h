#pragma once
// Settings of the "Affichage" module: speed differential HUD, session timer, touch zone circle drawn on the ball
// and centre sphere (feature parity with First Touch Trainer v1.0.5, improved). Every field is declared once in
// OverlayModule::describeSettings (cvar prefix "rlt_ov_"). The defaults below are exactly the schema defaults, which
// are the original plugin's defaults (positions converted to screen fractions, rounded to 4 decimals like the cvars).

#include "core/common/Color.h"

namespace rlt {

struct OverlayConfig {
    // ---- Activation (everything off by default, like the original) ----------------------------------------
    bool speedEnabled = false;      // speed differential text
    bool timerEnabled = false;      // session timer
    bool zoneEnabled = false;       // touch zone circle on the ball
    bool sphereEnabled = false;     // small sphere at the ball centre

    // ---- Speed differential ("Écart de vitesse") ----------------------------------------------------------
    int speedMode = 0;              // ovl::SpeedMode: 0 XY, 1 XYZ, 2 XY + Z, 3 closing speed
    int speedUnit = 0;              // SpeedUnit: 0 auto (game setting), 1 KPH, 2 MPH, 3 uu/s
    float textScale = 3.f;          // canvas DrawString scale at 1080p (also used by the session timer)
    bool shadow = true;             // text drop shadow (speed and timer)
    float speedX = 0.4587f;         // screen fractions of the text's top-left corner (original: W / 2.18)
    float speedY = 0.9524f;         // (original: H / 1.05)
    bool lastTouchLine = false;     // extra small line with the differential measured at the last touch

    // ---- Session timer ("Chrono de session") --------------------------------------------------------------
    float timerX = 0.4695f;         // original: W / 2.13
    float timerY = 0.0093f;         // original: H / 108
    Color timerColor{255.f, 255.f, 255.f, 255.f};

    // ---- Touch zone circle ("Cercle de contact") ----------------------------------------------------------
    int zoneBehavior = 0;           // ovl::ZoneBehavior: 0 static, 1 velocity driven, 2 keep it up, 3 auto
    float zoneRadius = 47.f;        // radius of the circle drawn on the ball surface (uu)
    int zoneThickness = 1;          // line width in pixels
    Color zoneColor{255.f, 0.f, 0.f, 255.f};
    bool zoneMatchColor = true;     // use the current speed-zone colour instead of zoneColor
    bool onlyBallInAir = true;      // circle and sphere only when the ball is above airHeight
    float airHeight = 400.f;        // uu, ball centre height
    bool onlyCarBelowBall = true;   // circle only when the car is lower than the ball centre
    bool zoneOcclusion = true;      // hide the part of the circle behind the ball (camera point of view)
    float zoneHiddenAlpha = 0.f;    // advanced: opacity (0..1) of the hidden part when occlusion is on, 0 = invisible
    float zoneMaxTiltDeg = 35.f;    // advanced: tilt of the zone at zoneMaxSpeed
    float zoneMinSpeed = 800.f;     // advanced: horizontal ball speed where the tilt starts (also the Auto switch)
    float zoneMaxSpeed = 2100.f;    // advanced: horizontal ball speed of the full tilt
    float zoneSmoothing = 0.1f;     // advanced: time constant of the direction smoothing (s), 0 = none
    int zoneSegments = 32;          // advanced: line segments of the circle

    // ---- Centre sphere ("Sphère centrale") ----------------------------------------------------------------
    float sphereRadius = 5.f;
    Color sphereColor{255.f, 0.f, 0.f, 255.f};
    bool sphereMatchColor = true;

    // ---- Speed zones ("Couleurs des zones") ---------------------------------------------------------------
    float goodThreshold = 150.f;    // |diff| <= this (uu/s): good
    float alrightThreshold = 300.f; // |diff| <= this (uu/s): alright, above: bad
    Color goodColor{0.f, 255.f, 0.f, 255.f};
    Color alrightColor{255.f, 255.f, 0.f, 255.f};
    Color badColor{255.f, 0.f, 0.f, 255.f};
};

} // namespace rlt
