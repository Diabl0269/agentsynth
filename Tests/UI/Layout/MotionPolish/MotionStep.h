#pragma once

#include "../FadeVisibilityTestGuard.h"
#include "UI/Layout/ChevronTurn.h"
#include "UI/Layout/FadeAmount.h"

// Steps every motion helper in flight by hand: FadeVisibility, FadeAmount and ChevronTurn. No VBlank reaches an
// off-screen component, so a headless test forces the animated path (FadeAnimateGuard) and calls this with the
// progress it wants (1 finishes them).
inline void stepMotion(float t) {
    synth::ui::FadeVisibility::stepAllForTest(t);
    synth::ui::FadeAmount::stepAllForTest(t);
    synth::ui::ChevronTurn::stepAllForTest(t);
}
