#pragma once

#include "../FadeVisibilityTestGuard.h"
#include "UI/Layout/ButtonStateEase.h"
#include "UI/Layout/ChevronTurn.h"
#include "UI/Layout/FadeAmount.h"
#include "UI/Layout/SwitchGlide.h"

// Steps every motion helper in flight by hand: FadeVisibility, FadeAmount, ChevronTurn, ButtonStateEase and
// SwitchGlide. No VBlank reaches an off-screen component, so a headless test forces the animated path
// (FadeAnimateGuard) and calls this with the progress it wants (1 finishes them).
inline void stepMotion(float t) {
    synth::ui::FadeVisibility::stepAllForTest(t);
    synth::ui::FadeAmount::stepAllForTest(t);
    synth::ui::ChevronTurn::stepAllForTest(t);
    synth::ui::ButtonStateEase::stepAllForTest(t);
    synth::ui::SwitchGlide::stepAllForTest(t);
}
