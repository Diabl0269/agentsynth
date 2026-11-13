#pragma once

#include "UI/Layout/ExitEnterList/ExitEnterListPlan.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

namespace synth::ui {

/** The Mod Matrix's row list as it stood before a change: every row's key and place, and a picture of the viewport for
 *  a removal to shrink away from (docs/layout/animation.md "Delete and undo animation"). */
struct ModMatrixRowSnapshot {
    std::vector<ExitEnterListRow> rows;
    juce::Image picture;
    float pictureScale = 1.0f;
};

} // namespace synth::ui
