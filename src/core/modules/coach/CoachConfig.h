#pragma once
// Settings of the "Coach & progression" module: feedback card after each graded attempt, records, sessions,
// daily streak, session goals and the adaptive training plan.
// Every field is declared once in CoachModule::describeSettings (cvar prefix "rlt_coach_").

#include <string>

#include "core/common/Color.h"

namespace rlt {

struct CoachConfig {
    bool enabled = true;

    // Feedback card (HUD, right zone, anchored on its right edge).
    bool cardEnabled = true;
    float cardSeconds = 4.f;     // display time, the last part fades out
    float cardX = 0.99f;         // screen fraction of the card's right edge
    float cardY = 0.30f;         // screen fraction of the card's top edge
    float cardScale = 1.f;       // multiplied by the global HUD scale
    int cardMetrics = 2;         // metrics shown on the card (0..3)
    bool cardTip = true;         // show the most important tip
    bool feedbackFading = true;  // fewer cards once a skill is mastered (guidance hypothesis)
    float fadingRate = 0.8f;     // success rate over the last 10 attempts above which the card fades
    int fadingEvery = 3;         // while fading, show every Nth success

    // Records.
    bool recordToast = true;
    bool recordChat = true;
    bool recordAnnounceAll = false;  // also toast/chat records of exercises that already announce their own

    // Sessions, streak and goals.
    bool summaryChat = true;     // chat summary when leaving the map
    int summaryMinAttempts = 3;  // no summary for shorter sessions
    int streakMinAttempts = 10;  // graded attempts per day that keep the daily streak alive
    bool goalToasts = true;      // toasts for goals reached and streak days
    int breakMinutes = 25;       // suggest a short break every N minutes of session (0 = never)

    // Training plan.
    int planSize = 3;
    float spacingWeight = 0.08f;  // need added per day without practice (capped at 10 days)
    float mistakeWeight = 0.4f;   // need added by the frequency of the main mistake
    float trendThreshold = 0.5f;  // score points per attempt that make a trend "en progrès" / "en baisse"
};

} // namespace rlt
