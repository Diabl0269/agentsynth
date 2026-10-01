// AutomationLanesFoldTests.cpp -- a track's automation lanes fold out under it: the fold arrow (mouse
// and keyboard), where the lane rows land in the shared row layout, what a click at each y hits,
// showAutomationLane(), and the vertical-zoom anchor with lane rows present.

#include "AutomationLanesTestFixture.h"
#include "ShortcutManager/ShortcutManager.h"

using namespace automation_lanes_test;
using synth::TrackKind;

TEST(AutomationLanesFoldTest, FoldArrowClickOpensTheLaneRowsWhereTheLayoutPutsThem) {
    LanesPanel f;
    const auto bass = f.doc.addTrack(TrackKind::Midi, "Bass");
    const auto lead = f.doc.addTrack(TrackKind::Midi, "Lead");
    const auto cutoff = f.addLane(bass, "cutoff");
    const auto res = f.addLane(bass, "resonance");
    auto* header = f.panel.getTrackHeaderAt(0);
    ASSERT_NE(header, nullptr);
    ASSERT_TRUE(header->getFoldArrow().isVisible()) << "a track with lanes shows the arrow";
    EXPECT_FALSE(header->getFoldArrow().isExpanded());
    EXPECT_EQ(f.panel.laneRowBoundsForTest(cutoff), juce::Rectangle<int>()) << "folded: no lane rows";
    const int leadTopFolded = f.panel.getTrackHeaderAt(1)->getY();

    clickButton(header->getFoldArrow());

    ASSERT_TRUE(f.panel.isTrackAutomationExpandedForTest(bass));
    EXPECT_TRUE(header->getFoldArrow().isExpanded());
    const auto layout = f.panel.getClipLaneArea().getRowLayout();
    const int laneHeight = 40;
    EXPECT_EQ(layout.trackExtraHeight(0), 2 * laneHeight);
    const int firstLaneTop = layout.trackTop(0) + layout.trackRowHeight(0);
    const int lanesTop = f.panel.getClipLaneArea().getY();
    EXPECT_EQ(f.panel.laneRowBoundsForTest(cutoff).getY(), lanesTop + firstLaneTop);
    EXPECT_EQ(f.panel.laneRowBoundsForTest(res).getY(), lanesTop + firstLaneTop + laneHeight);
    EXPECT_EQ(f.panel.getTrackHeaderAt(1)->getY(), leadTopFolded + 2 * laneHeight) << "Lead moves down";
    ASSERT_NE(f.panel.laneHeaderForTest(cutoff), nullptr);
    EXPECT_EQ(f.panel.laneHeaderForTest(cutoff)->getY(), firstLaneTop) << "the lane header sits at its row too";
    EXPECT_EQ(f.panel.laneEditorForTest(res)->getBounds().getY(), firstLaneTop + laneHeight);

    // What a click hits: the lane row is the editor's, the shifted Lead row is the clip lanes'.
    const int x = f.panel.getClipLaneArea().getX() + 200;
    const auto laneRow = f.panel.laneRowBoundsForTest(cutoff);
    EXPECT_EQ(f.componentAt({x, laneRow.getCentreY()}), f.panel.laneEditorForTest(cutoff));
    const int leadY = lanesTop + layout.trackTop(1) + layout.trackRowHeight(1) / 2;
    EXPECT_EQ(f.componentAt({x, leadY}), &f.panel.getClipLaneArea());
    // ... and a real double-click there creates a clip on Lead, not on Bass.
    auto& clips = f.panel.getClipLaneArea();
    const auto inClips = clips.getLocalPoint(&f.panel, juce::Point<int>(x, leadY)).toFloat();
    clips.mouseDoubleClick(makeClickEvent(clips, inClips, leftButton()));
    EXPECT_EQ(f.doc.getTrack(lead)->clips.size(), 1u);
    EXPECT_TRUE(f.doc.getTrack(bass)->clips.empty());

    clickButton(header->getFoldArrow());
    EXPECT_FALSE(f.panel.isTrackAutomationExpandedForTest(bass));
    EXPECT_EQ(f.panel.getTrackHeaderAt(1)->getY(), leadTopFolded) << "folding restores the rows";
    EXPECT_EQ(f.panel.laneEditorForTest(cutoff), nullptr);
    EXPECT_EQ(f.panel.laneHeaderForTest(cutoff), nullptr);
}

TEST(AutomationLanesFoldTest, ReturnAndSpaceOnTheFocusedArrowToggleAndRenameIt) {
    LanesPanel f;
    const auto bass = f.doc.addTrack(TrackKind::Midi, "Bass");
    f.addLane(bass, "cutoff");
    auto& arrow = f.panel.getTrackHeaderAt(0)->getFoldArrow();
    EXPECT_TRUE(arrow.getWantsKeyboardFocus()) << "a Tab stop";
    EXPECT_EQ(arrow.getTitle(), "Show Bass automation (1 lane)");
    EXPECT_EQ(arrow.getTooltip(), "Show Bass automation (1 lane)");

    EXPECT_TRUE(arrow.keyPressed(juce::KeyPress(juce::KeyPress::returnKey)));
    EXPECT_TRUE(f.panel.isTrackAutomationExpandedForTest(bass));
    EXPECT_EQ(arrow.getTitle(), "Hide Bass automation");

    EXPECT_TRUE(arrow.keyPressed(juce::KeyPress(juce::KeyPress::spaceKey)));
    EXPECT_FALSE(f.panel.isTrackAutomationExpandedForTest(bass));
    EXPECT_EQ(arrow.getTitle(), "Show Bass automation (1 lane)");

    // A rename reaches the name too.
    f.doc.setTrackName(bass, "Sub");
    EXPECT_EQ(arrow.getTitle(), "Show Sub automation (1 lane)");
}

TEST(AutomationLanesFoldTest, TheBoundShortcutOnAFocusedHeaderTogglesItsLanes) {
    LanesPanel f;
    ShortcutManager shortcuts;
    f.panel.setShortcutManager(&shortcuts);
    const auto bass = f.doc.addTrack(TrackKind::Midi, "Bass");
    f.addLane(bass, "cutoff");
    const juce::KeyPress key('a', juce::ModifierKeys::noModifiers, 0);
    EXPECT_EQ(shortcuts.getBinding("timelineToggleTrackAutomation"), key) << "bare A by default";

    auto* header = f.panel.getTrackHeaderAt(0);
    EXPECT_TRUE(header->keyPressed(key));
    EXPECT_TRUE(f.panel.isTrackAutomationExpandedForTest(bass));
    EXPECT_TRUE(header->keyPressed(key));
    EXPECT_FALSE(f.panel.isTrackAutomationExpandedForTest(bass));
    f.panel.setShortcutManager(nullptr);
}

// A track row's header column has no room for a count badge beside the name (it squeezed the name
// to "..." in a real project), so the folded arrow carries the count in its name and tooltip instead.
TEST(AutomationLanesFoldTest, FoldedArrowNamesItsLaneCountAndTheNameKeepsItsRoom) {
    LanesPanel f;
    const auto bass = f.doc.addTrack(TrackKind::Midi, "Bass");
    f.addLane(bass, "cutoff");
    f.addLane(bass, "resonance");
    EXPECT_EQ(synth::ui::laneCountBadgeText(2), "2 lanes");
    EXPECT_EQ(synth::ui::laneCountBadgeText(1), "1 lane");
    auto* header = f.panel.getTrackHeaderAt(0);
    const auto nameFolded = header->getNameLabel().getBounds();
    EXPECT_EQ(header->getFoldArrow().getTitle(), "Show Bass automation (2 lanes)");
    EXPECT_EQ(header->getFoldArrow().getTooltip(), "Show Bass automation (2 lanes)");
    f.panel.setTrackAutomationExpanded(bass, true);
    EXPECT_EQ(header->getNameLabel().getBounds(), nameFolded) << "no badge takes room from the name";
    EXPECT_EQ(header->getFoldArrow().getTitle(), "Hide Bass automation");
}

TEST(AutomationLanesFoldTest, ShowAutomationLaneOpensScrollsAndSelects) {
    LanesPanel f;
    synth::TrackId last;
    for (int i = 0; i < 12; ++i)
        last = f.doc.addTrack(TrackKind::Midi, "T" + juce::String(i));
    const auto lane = f.addLane(last, "cutoff");
    ASSERT_EQ(f.panel.getViewState().trackScrollY, 0.0);

    f.panel.showAutomationLane(lane);

    EXPECT_TRUE(f.panel.isTrackAutomationExpandedForTest(last));
    EXPECT_EQ(f.panel.getSelectedAutomationLane(), lane);
    const auto row = f.panel.laneRowBoundsForTest(lane);
    const auto lanes = f.panel.getClipLaneArea().getBounds();
    EXPECT_GT(f.panel.getViewState().trackScrollY, 0.0) << "the last track's lane was below the fold";
    EXPECT_TRUE(lanes.contains(row)) << "the lane row is scrolled fully into view: row " << row.toString() << " lanes "
                                     << lanes.toString();
    auto* editor = f.panel.laneEditorForTest(lane);
    ASSERT_NE(editor, nullptr);
    EXPECT_TRUE(editor->getWantsKeyboardFocus()) << "the editor is what showAutomationLane hands focus to";
    // Real keyboard focus needs a native window, which this suite never opens (TimelineTrackFocusTests).
}

// Regression test for FRO439: Cmd+Shift+wheel zoom anchored by scaling the content y by the
// clip-row ratio alone, which drifts once lane rows and the fixed section row are in the layout.
TEST(AutomationLanesFoldTest, VerticalZoomKeepsTheRowUnderThePointerPutWithLanesOpen) {
    LanesPanel f;
    synth::TrackId tracks[10]; // enough below the anchor that the zoomed scroll is never clamped
    for (auto& t : tracks)
        t = f.doc.addTrack(TrackKind::Midi, "T");
    f.addLane(tracks[0], "a");
    f.addLane(tracks[0], "b");
    const auto target = f.addLane(tracks[4], "cutoff");
    f.panel.setTrackAutomationExpanded(tracks[0], true);
    f.panel.setTrackAutomationExpanded(tracks[4], true);

    // The anchor: the middle of track 4's lane row, in clip-lane coordinates.
    auto& clips = f.panel.getClipLaneArea();
    const auto rowBefore = f.panel.laneRowBoundsForTest(target);
    const float anchorY = (float)(rowBefore.getCentreY() - clips.getY());
    const juce::Point<float> anchor((float)clips.getX() + 100.0f, (float)rowBefore.getCentreY());
    f.panel.mouseMagnify(
        makeTimelineMouseEvent(f.panel, anchor, juce::ModifierKeys(juce::ModifierKeys::shiftModifier), false, anchor),
        1.5f);

    const auto rowAfter = f.panel.laneRowBoundsForTest(target);
    ASSERT_GT(rowAfter.getHeight(), rowBefore.getHeight()) << "the lane rows zoomed too";
    const float laneCentreAfter = (float)(rowAfter.getCentreY() - clips.getY());
    EXPECT_NEAR(laneCentreAfter, anchorY, 2.0f) << "the lane under the pointer stays under it";
}
