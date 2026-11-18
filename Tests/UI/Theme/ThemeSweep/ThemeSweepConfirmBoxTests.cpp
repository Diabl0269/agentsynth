// ThemeSweepConfirmBoxTests.cpp -- the confirm dialog's tick box ("Remember my choice") in every sweep theme, off and
// on, measured from rendered pixels: the box reads on the alert surface (3:1 for its outline off and its fill on), its
// label reads (4.5:1) and the box's left edge is the message text's left edge. Bug kind theme-sweep (a control that
// vanished in one theme) and layout-overlap (a control that drifted off its column).
#include "ThemeSweepSupport.h"
#include "ThemeSweepSurfaces.h"
#include "UI/Chrome/ConfirmDontAskDialog.h"
#include "UI/Theme/AppLookAndFeel/AlertIconBounds.h"
#include "UI/Theme/AppLookAndFeel/TickToggleMetrics.h"
#include "UI/Timeline/DeleteTrackConfirm.h"
#include <gtest/gtest.h>

using namespace synth::test::theme_sweep;
using synth::ui::ConfirmDontAskSpec;

namespace {

ConfirmDontAskSpec confirmBoxDeleteTracksSpec() {
    const auto text = synth::ui::deleteTrackConfirmText(2);
    return {text.title, text.message,        "Delete", "deleteTrackDontAskAgain", "Turn it back on in Settings",
            {},         "Remember my choice"};
}

// The leftmost column, among the message's rows (above the box), that holds ink: a pixel unlike the surface.
int confirmBoxLeftmostInk(const juce::Image& image, juce::Rectangle<int> rows, int fromX, int toX,
                          juce::Colour surface) {
    for (int x = fromX; x < toX; ++x)
        for (int y = rows.getY(); y < rows.getBottom(); ++y)
            if (contrastRatio(image.getPixelAt(x, y), surface) > 1.5)
                return x;
    return -1;
}

} // namespace

TEST(ThemeSweepConfirmBox, TheBoxReadsAndLinesUpWithTheMessageInEveryThemeOffAndOn) {
    for (const auto& theme : sweepThemes()) {
        for (const bool ticked : {false, true}) {
            SweepEnvironment env(theme);
            auto built = synth::ui::buildConfirmChoiceWindow(confirmBoxDeleteTracksSpec());
            ASSERT_NE(built.window, nullptr);
            auto& alert = *built.window;
            built.box->setToggleState(ticked, juce::dontSendNotification);
            const auto image = renderToImage(alert);
            writeSweepPng(juce::String("confirm-box-") + (ticked ? "on" : "off"), theme.id, image);

            const juce::String where = theme.id + (ticked ? " (ticked)" : " (unticked)");
            const auto surface = alert.findColour(juce::AlertWindow::backgroundColourId).withAlpha(1.0f);
            const int left = synth::ui::kAlertTextLeft;
            const auto box = alert.getLocalArea(built.box, built.box->getLocalBounds());
            const int centreY = box.getCentreY();

            // The box's painted left edge: the column at kAlertTextLeft is the box, the one before it is the surface.
            const auto edge = image.getPixelAt(left, centreY);
            EXPECT_GE(contrastRatio(edge, surface), 3.0) << where << ": the box's edge does not read on the surface";
            EXPECT_LT(contrastRatio(image.getPixelAt(left - 1, centreY), surface), 1.2)
                << where << ": the box starts left of the message text";
            if (ticked)
                EXPECT_GE(contrastRatio(image.getPixelAt(left + 2, centreY), surface), 3.0)
                    << where << ": the ticked box's fill does not read on the surface";

            // The message's own ink starts at the same column (a glyph's side bearing allowed).
            const juce::Rectangle<int> messageRows(0, synth::ui::kAlertTextTop, 1,
                                                   box.getY() - synth::ui::kAlertTextTop);
            const int ink = confirmBoxLeftmostInk(image, messageRows, left - 6, left + 8, surface);
            EXPECT_GE(ink, left - 1) << where << ": the message starts left of the box";
            EXPECT_LE(ink, left + 4) << where << ": the message starts right of the box";

            // The label reads, and the box is as wide as the text column (label left-aligned under the message).
            const auto label = juce::String("Remember my choice");
            EXPECT_EQ(built.box->getButtonText(), label);
            EXPECT_GE(contrastRatio(built.box->findColour(juce::ToggleButton::textColourId), surface), kMinTextContrast)
                << where << ": the box's label does not read";
            EXPECT_EQ(box.getX() + synth::ui::kTickToggleBoxInset, left) << where;
            EXPECT_LE(box.getRight(), alert.getWidth() - 12) << where << ": the label runs out of the window";
        }
    }
}
