// RenderProgressDialog -- the small modal panel behind "Render to audio track": its Cancel button and Escape stop the
// render, and it is named for screen readers with a keyboard-focusable, tooltipped Cancel button.
#include "UI/Chrome/RenderProgressDialog.h"

#include <gtest/gtest.h>

using synth::ui::RenderProgressDialog;

TEST(RenderProgressDialogTest, CancelButtonAndEscapeBothAskTheHostToStop) {
    int cancels = 0;
    RenderProgressDialog dialog([] { return 0.5; });
    dialog.onCancel = [&cancels] { ++cancels; };

    dialog.getCancelButtonForTest().onClick();
    EXPECT_EQ(cancels, 1);
    EXPECT_TRUE(dialog.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
    EXPECT_EQ(cancels, 2);
    EXPECT_FALSE(dialog.keyPressed(juce::KeyPress('x')));
}

TEST(RenderProgressDialogTest, IsNamedForScreenReadersAndCancelIsAKeyboardStop) {
    RenderProgressDialog dialog([] { return 0.0; });
    EXPECT_EQ(dialog.getTitle(), "Rendering to audio");
    EXPECT_TRUE(dialog.getDescription().isNotEmpty());

    auto& cancel = dialog.getCancelButtonForTest();
    EXPECT_EQ(cancel.getTitle(), "Cancel render");
    EXPECT_TRUE(cancel.getTooltip().isNotEmpty());
    EXPECT_TRUE(cancel.getWantsKeyboardFocus());
    EXPECT_FALSE(dialog.getWantsKeyboardFocus()) << "the only control is the initially focused Cancel button";
}
