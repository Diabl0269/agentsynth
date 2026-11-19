// ConfirmDontAskDialogLayoutTests.cpp -- where the confirm dialog puts its tick box for the specs its callers use (a
// short question, a long one, with and without the second button): the box's painted left edge is the message text's
// left edge, it sits between the message and the buttons, and it stays inside the window.
#include "../Layout/NativeWindowTestGuard.h"
#include "UI/Chrome/ConfirmDontAskDialog.h"
#include "UI/Theme/AppLookAndFeel/AlertIconBounds.h"
#include "UI/Theme/AppLookAndFeel/TickToggleMetrics.h"
#include "UI/Timeline/DeleteTrackConfirm.h"
#include <gtest/gtest.h>

using synth::ui::ConfirmDontAskSpec;

namespace {

std::vector<ConfirmDontAskSpec> confirmCallerSpecs() {
    std::vector<ConfirmDontAskSpec> specs;
    const auto many = synth::ui::deleteTrackConfirmText(3);
    specs.push_back({many.title, many.message, "Delete", "deleteTrackDontAskAgain", "tip", {}, "Remember my choice"});
    const auto macro = synth::ui::deleteTrackMacroConfirmText(4, 2);
    specs.push_back({macro.title, macro.message, "Remove macro", "deleteTrackMacroDontAskAgain", "tip", "Keep macro",
                     "Remember my choice"});
    specs.push_back({"Hide pad?", "Short.", "Hide", "hideDrumDontAskAgain", "tip", {}, "Don't ask again"});
    return specs;
}

} // namespace

class ConfirmDontAskDialogLayout : public NeedsDisplayTest {};

TEST_F(ConfirmDontAskDialogLayout, TheBoxLeftEdgeIsTheMessageTextLeftEdgeForEveryCallersSpec) {
    for (const auto& spec : confirmCallerSpecs()) {
        auto built = synth::ui::buildConfirmChoiceWindow(spec);
        ASSERT_NE(built.window, nullptr) << spec.title;
        ASSERT_NE(built.box, nullptr) << spec.title;
        auto& alert = *built.window;
        const auto box = built.box->getBounds();
        EXPECT_EQ(box.getX() + synth::ui::kTickToggleBoxInset, synth::ui::kAlertTextLeft) << spec.title;
        EXPECT_EQ(built.box->getButtonText(), spec.checkboxLabel) << spec.title;
        EXPECT_EQ(built.box->getComponentID(), spec.dontAskId) << spec.title;
        EXPECT_TRUE(alert.getLocalBounds().contains(box)) << spec.title << ": the box sticks out of the window";
        EXPECT_GE(alert.getWidth() - box.getRight(), 12) << spec.title << ": no right margin";
        EXPECT_GT(box.getWidth(), 150) << spec.title << ": too narrow for the label";
        for (int b = 0; b < alert.getNumButtons(); ++b) {
            EXPECT_GE(alert.getButton(b)->getY(), box.getBottom()) << spec.title << ": a button overlaps the box";
            EXPECT_GE(alert.getButton(b)->getX(), 0) << spec.title;
            EXPECT_LE(alert.getButton(b)->getRight(), alert.getWidth()) << spec.title;
        }
        EXPECT_EQ(alert.getNumButtons(), spec.alternateLabel.isNotEmpty() ? 3 : 2) << spec.title;
    }
}
