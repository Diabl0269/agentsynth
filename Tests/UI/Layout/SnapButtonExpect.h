#pragma once

// The checks every surface's Snap switch passes: the shared button with the Snap icon, named, with a tooltip that
// says more than its name, and a Tab stop.
#include "UI/Chrome/ToolbarButton/ToolbarButton.h"
#include <gtest/gtest.h>

namespace synth::ui::test {

inline void expectSharedSnapButton(SnapToggleButton& button, const char* surface) {
    EXPECT_EQ(button.getIcon(), synth::theme::Icon::ToolbarSnap) << surface;
    EXPECT_FALSE(button.getTitle().isEmpty()) << surface;
    EXPECT_FALSE(button.getTooltip().isEmpty()) << surface;
    EXPECT_NE(button.getTooltip(), button.getTitle()) << surface << ": a tooltip says more than the name";
    EXPECT_TRUE(button.getWantsKeyboardFocus()) << surface;
}

} // namespace synth::ui::test
