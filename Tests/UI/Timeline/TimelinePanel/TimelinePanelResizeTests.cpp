// TimelinePanelResizeTests.cpp
//
// The panel's layout at any dock height. The resize gesture itself (one top-edge handle on the
// bottom DOCK, every tab) and MainComponent's ownership of the value live in
// Tests/UI/Mixer/BottomDockResizeTests.cpp.

#include "UI/Timeline/TimelinePanelComponent/TimelinePanelComponent.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

// ============================================================================
// 5. Panel layout at any height — the panel never resizes itself; the dock reports the height.
// ============================================================================

TEST(TimelinePanelComponentTest, AddTrackButtonCarriesATooltip) {
    synth::ui::TimelinePanelComponent panel;
    EXPECT_EQ(panel.getAddTrackButton().getTooltip(), "Add Track: a track or marker; type to search");
}

// ============================================================================
// Toolbar buttons: icon chips with the name beside them, "Show button labels"
// ============================================================================

TEST(TimelinePanelToolbarButtonsTest, AddTrackAndSnapShowTheirNameBesideTheIconAndFitTheirBars) {
    synth::ui::TimelinePanelComponent panel;
    panel.setSize(1200, 300);
    auto& addTrack = panel.getAddTrackButton();
    auto& snap = panel.getSnapToggleButton();

    EXPECT_EQ(addTrack.getButtonText(), "Add Track");
    EXPECT_EQ(snap.getButtonText(), "Snap");
    for (auto* b : {&addTrack, &snap}) {
        EXPECT_TRUE(b->isInline()) << b->getName();
        EXPECT_EQ(b->getTitle(), b->getName());
        EXPECT_GE(b->getWidth(), b->getPreferredWidth()) << b->getName() << " has room for its caption";
        const auto chip = synth::theme::toolbarChipBounds(*b);
        EXPECT_LE(chip.getBottom(), (float)b->getHeight()) << "chip and caption share one row";
        EXPECT_LT(chip.getRight(), (float)b->getWidth());
    }
    EXPECT_EQ(panel.getTransportBarBounds().getHeight(), 34);
    EXPECT_LE(snap.getHeight(), 34);
    const int stripHeight = panel.getTrackHeaderViewport().getY() - panel.getTrackHeaderBounds().getY();
    EXPECT_EQ(stripHeight, 30) << "the Add Track strip is as tall as the ruler";
    EXPECT_LE(addTrack.getHeight(), stripHeight);
    EXPECT_TRUE(juce::Rectangle<int>(0, 0, panel.getTrackHeaderBounds().getWidth(), stripHeight)
                    .contains(addTrack.getBounds().translated(-panel.getTrackHeaderBounds().getX(),
                                                              -panel.getTrackHeaderBounds().getY())));
}

TEST(TimelinePanelToolbarButtonsTest, WithLabelsOffTheButtonsAreIconsOnlyAndTheBarsKeepTheirHeights) {
    synth::ui::TimelinePanelComponent panel;
    panel.setSize(1200, 300);
    const int labelledSnapWidth = panel.getSnapToggleButton().getWidth();
    panel.setToolbarLabelsShown(false);

    auto& addTrack = panel.getAddTrackButton();
    auto& snap = panel.getSnapToggleButton();
    EXPECT_TRUE(addTrack.getButtonText().isEmpty());
    EXPECT_TRUE(snap.getButtonText().isEmpty());
    EXPECT_EQ(addTrack.getTitle(), "Add Track") << "the screen-reader name stays the full name";
    EXPECT_EQ(snap.getTitle(), "Snap");
    EXPECT_TRUE(addTrack.getTooltip().containsIgnoreCase("Add Track")) << addTrack.getTooltip();
    EXPECT_TRUE(snap.getTooltip().containsIgnoreCase("Snap on/off")) << snap.getTooltip();
    EXPECT_TRUE(snap.getTooltip().contains("(j)")) << "the shortcut stays in the tooltip";
    EXPECT_LT(snap.getWidth(), labelledSnapWidth);
    EXPECT_EQ(panel.getTransportBarBounds().getHeight(), 34);
    EXPECT_LE(snap.getHeight(), 34);
    EXPECT_EQ(panel.getTrackHeaderViewport().getY() - panel.getTrackHeaderBounds().getY(), 30);

    panel.setToolbarLabelsShown(true);
    EXPECT_EQ(panel.getSnapToggleButton().getButtonText(), "Snap");
    EXPECT_EQ(panel.getSnapToggleButton().getWidth(), labelledSnapWidth);
}

TEST(TimelinePanelToolbarButtonsTest, TheSnapSwitchIsLitWhileSnappingIsOnInBothModes) {
    synth::ui::TimelinePanelComponent panel;
    panel.setSize(1200, 300);
    auto& snap = panel.getSnapToggleButton();
    for (const bool labels : {true, false}) {
        panel.setToolbarLabelsShown(labels);
        panel.getViewState().snapEnabled = true;
        snap.onClick(); // off
        EXPECT_FALSE(snap.getToggleState());
        EXPECT_EQ(snap.getLitAmount(), 0.0f);
        snap.onClick(); // on
        EXPECT_TRUE(snap.getToggleState());
        EXPECT_EQ(snap.getLitAmount(), 1.0f);
        EXPECT_EQ(snap.getTitle(), "Snap");
    }
}

// Nothing inside the panel assumes the default height: at 2x, every extra pixel goes to the lanes.
TEST(TimelinePanelResizeTest, InternalLayoutHoldsAtDoubleHeight) {
    synth::ui::TimelinePanelComponent panel;
    panel.setSize(1200, 220);
    const int lanesAtDefault = panel.getLanesBounds().getHeight();

    panel.setSize(1200, 440);
    const auto transport = panel.getTransportBarBounds();
    const auto trackHeader = panel.getTrackHeaderBounds();
    const auto lanes = panel.getLanesBounds();
    EXPECT_EQ(transport.getUnion(trackHeader).getUnion(lanes), panel.getLocalBounds());
    EXPECT_EQ(transport.getHeight(), 34) << "the transport strip keeps its metric height";
    EXPECT_EQ(lanes.getHeight(), lanesAtDefault + 220) << "the extra height all goes to the lanes";
    EXPECT_EQ(transport.getY(), 0) << "no handle overlaps the strip: it starts at the panel's own top edge";

    // The clip lanes still fill the lanes region below the 30 px ruler — the rect the grid is
    // painted into, so clips stay aligned with it at any height. (30, up from 24: the strip now
    // tiles a numbers row and a marker band — see Theme::Metrics::timelineRulerHeight.)
    EXPECT_EQ(panel.getClipLaneArea().getBounds(), lanes.withTrimmedTop(30));
    // The ROLL shares the same rect, except for how much of the ruler band it claims: it owns the
    // vertical run above the ruler when its chip toolbar is showing, so that top edge belongs to
    // PianoRollComponent's own layout and is asserted in its own tests. What must hold HERE is that
    // the roll tracks the lanes REGION at any panel height — full width, flush bottom, top inside.
    const auto roll = panel.getPianoRoll().getBounds();
    EXPECT_EQ(roll.getX(), lanes.getX());
    EXPECT_EQ(roll.getWidth(), lanes.getWidth());
    EXPECT_EQ(roll.getBottom(), lanes.getBottom());
    EXPECT_GE(roll.getY(), lanes.getY());
    EXPECT_LE(roll.getY(), lanes.getY() + 30);
    // The header list fills the taller viewport even with no tracks (no gap under the last row).
    ASSERT_NE(panel.getTrackHeaderViewport().getViewedComponent(), nullptr);
    EXPECT_GE(panel.getTrackHeaderViewport().getViewedComponent()->getHeight(),
              panel.getTrackHeaderViewport().getMaximumVisibleHeight());

    const juce::Image img = panel.createComponentSnapshot(panel.getLocalBounds());
    EXPECT_FALSE(img.isNull());
    EXPECT_EQ(img.getHeight(), 440);
}
