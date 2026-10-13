#pragma once
// Settings of the "Première touche" module (analysis of every touch, score, coaching, first-touch drills).
// Every field is declared once in FirstTouchModule::describeSettings (cvar prefix "rlt_ft_").

#include <string>

#include "core/common/Color.h"

namespace rlt {

struct FirstTouchConfig {
    // ---- Analyse des touches
    bool enabled = true;
    float controlWindow = 1.0f;        // s followed after a first touch
    float controlRadius = 350.f;       // uu, ball centre to car hitbox centre
    float controlRelSpeed = 450.f;     // uu/s, max |v_ball - v_car| to count as controlled
    float controlFraction = 0.6f;      // share of the window that must be controlled ("most of the window")
    float successScore = 60.f;         // minimum score of a successful touch
    float goodZone = 150.f;            // |speed diff| <= goodZone: green zone ([BON])
    float okZone = 300.f;              // |speed diff| <= okZone: yellow zone ([MOYEN])
    int unit = 0;                      // 0 Auto (game setting), 1 km/h, 2 mph, 3 uu/s
    bool sendResults = true;           // send every graded touch to the coach (AttemptResult)

    // ---- Note (advanced)
    float firstTouchGap = 1.0f;        // s without contact before a contact counts as a new first touch
    float speedGood = 150.f;           // relative speed giving the full speed sub-score
    float speedBad = 600.f;            // relative speed giving 0
    float contactGoodDeg = 30.f;       // contact error giving the full contact sub-score
    float contactBadDeg = 70.f;
    float weightSpeed = 35.f;          // weights in percent (normalised)
    float weightContact = 25.f;
    float weightControl = 40.f;
    float tooFastThreshold = 300.f;    // along-track speed difference for the "too fast / too slow" tips
    float angleThreshold = 300.f;      // cross-track difference for the "angle" tip
    float lateralThreshold = 45.f;     // uu of lateral offset for the "off-centre" tip
    float popThreshold = 300.f;        // uu/s of ball pop for the "ball popped" tip

    // ---- HUD
    bool hudEnabled = true;
    float hudX = 0.99f;                // screen fractions (anchored right when x > 0.5)
    float hudY = 0.70f;
    float hudSize = 1.f;
    bool showHeatmap = true;
    bool showTips = true;
    bool showTrend = true;
    bool showContactMarker = true;
    float markerSeconds = 1.5f;
    Color goodColor{40, 220, 90, 255};
    Color okColor{255, 205, 40, 255};
    Color badColor{255, 70, 60, 255};

    // ---- Chat (parity P21)
    bool chatLog = false;
    bool chatFlipReset = true;
    bool chatWithScore = true;

    // ---- Exercices
    float drillTimeout = 6.f;          // s without touch -> failed rep
    float drillPause = 1.5f;           // s between two reps
    bool drillResetCar = true;         // put the car back on its start pose (speed 0) every rep
    bool drillFullBoost = true;
    float drillTarget = 0.75f;         // target success rate of the adaptive level
    float drillStep = 0.04f;           // level step after a success
    float drillHudX = 0.01f;
    float drillHudY = 0.12f;
    int drillSeed = 0;                 // 0 = new random series at every start
};

} // namespace rlt
