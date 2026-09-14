// Pan-law tests for the L/R stereo split on voice modules (issue #219): the shared balance law
// every stereo-capable module's Pan parameter goes through.

#include "Modules/ModuleBase.h"
#include <gtest/gtest.h>

// ---------------------------------------------------------------------------
// The shared pan law
// ---------------------------------------------------------------------------

TEST(StereoPanLaw, CentreLeavesBothLegsAtUnity) {
    // Not equal-power on purpose: an equal-power centre (1/sqrt2) would have quietened every
    // existing mono patch by 3 dB the moment these modules grew a second output jack.
    float gainL = 0.0f;
    float gainR = 0.0f;
    ModuleBase::panGains(0.0f, gainL, gainR);
    EXPECT_FLOAT_EQ(gainL, 1.0f);
    EXPECT_FLOAT_EQ(gainR, 1.0f);
}

TEST(StereoPanLaw, HardPanSilencesTheFarLegAndLeavesTheNearLegAtUnity) {
    float gainL = 0.0f;
    float gainR = 0.0f;

    ModuleBase::panGains(-1.0f, gainL, gainR);
    EXPECT_FLOAT_EQ(gainL, 1.0f);
    EXPECT_FLOAT_EQ(gainR, 0.0f);

    ModuleBase::panGains(1.0f, gainL, gainR);
    EXPECT_FLOAT_EQ(gainL, 0.0f);
    EXPECT_FLOAT_EQ(gainR, 1.0f);
}

TEST(StereoPanLaw, OutOfRangePanIsClamped) {
    float gainL = 0.0f;
    float gainR = 0.0f;
    ModuleBase::panGains(-4.0f, gainL, gainR);
    EXPECT_FLOAT_EQ(gainL, 1.0f);
    EXPECT_FLOAT_EQ(gainR, 0.0f);
}
