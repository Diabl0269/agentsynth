// TimelinePanelResizeTests.cpp
//
// The panel's layout at any dock height. The resize gesture itself (one top-edge handle on the
// bottom DOCK, every tab) and MainComponent's ownership of the value live in
// Tests/UI/Mixer/BottomDockResizeTests.cpp since FRO231.

#include "UI/Timeline/TimelinePanelComponent/TimelinePanelComponent.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

// ============================================================================
// 5. Panel layout at any height — the panel never resizes itself; the dock reports the height.
// ============================================================================

TEST(TimelinePanelComponentTest, AddTrackButtonCarriesATooltip) {
    synth::ui::TimelinePanelComponent panel;
    EXPECT_EQ(panel.getAddTrackButton().getTooltip(), "Add a MIDI or Audio track");
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
