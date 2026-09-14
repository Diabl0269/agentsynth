// TimelinePanelTrackHeaderTests.cpp
//
// The track header column at panel level (ungated): follows the TimelineDoc with no timer,
// whole-row drag-to-reorder, drop-indicator paint order, add-button/viewport layout, and
// scrolling when tracks overflow. The app-level timeline wiring (TimelineAppWiringTest) that
// shares this banner in the original file lives in TimelinePanelAppWiringTests.cpp.

#include "AI/AIProvider.h"
#include "AI/AIStateMapper/AIStateMapper.h"
#include "AppUndoManager.h"
#include "MainComponent/MainComponent.h"
#include "ProjectBundle.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "TimelinePanelTestFixture.h"
#include "Transport/TransportService.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Theme/BuiltInThemes.h"
#include "UI/Timeline/EdgeAutoScroll.h"
#include "UI/Timeline/TimelinePanelComponent/TimelinePanelComponent.h"
#include "UI/Timeline/TrackColour.h"
#include "UserSettings.h"
#include <algorithm>
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

// ============================================================================
// 4. Track headers + the app-level timeline wiring.
// ============================================================================

using synth::TrackKind;

// ---- Panel level (ungated): the header column is a pure view of the document ----

TEST(TimelinePanelTrackHeaderTest, HeadersFollowTheDocumentWithNoTimer) {
    synth::TimelineDoc doc; // declared first: the panel removes its listener in its destructor
    synth::ui::TimelinePanelComponent panel;
    panel.setSize(1200, 220);
    panel.setTimelineDoc(&doc);
    EXPECT_EQ(panel.getTrackHeaderCount(), 0);

    const auto first = doc.addTrack(TrackKind::Midi, "Track 1");
    EXPECT_EQ(panel.getTrackHeaderCount(), 1);
    doc.addTrack(TrackKind::Midi, "Track 2");
    ASSERT_EQ(panel.getTrackHeaderCount(), 2);

    // A value change REFRESHES the existing rows; it must not rebuild them (that would drop an
    // in-progress name edit and churn the whole column on every mute click).
    auto* firstHeader = panel.getTrackHeaderAt(0);
    ASSERT_NE(firstHeader, nullptr);
    doc.setTrackName(first, "Bassline");
    EXPECT_EQ(panel.getTrackHeaderAt(0), firstHeader);
    EXPECT_EQ(firstHeader->getNameLabel().getText(), "Bassline");

    doc.removeTrack(first);
    EXPECT_EQ(panel.getTrackHeaderCount(), 1);
    EXPECT_EQ(panel.getTrackHeaderAt(0)->getNameLabel().getText(), "Track 2");

    panel.setTimelineDoc(nullptr);
    EXPECT_EQ(panel.getTrackHeaderCount(), 0);
}

// T166: whole-row drag-to-reorder. No TrackHeaderHost is installed here (trackHeaderHost_ ==
// nullptr) — deliberately, to exercise TimelinePanelComponent::endTrackDrag's own no-host
// fallback (the same convention TimelineTrackHeaderComponent::performEdit already follows), which
// also makes this the worst case for the ordering hazard below: with no host indirection, the
// mutation that destroys the dragged row runs SYNCHRONOUSLY inside the row's own mouseUp.
TEST(TimelinePanelTrackHeaderTest, WholeRowDragReordersTracksAndSurvivesTheHeaderRebuildMidGesture) {
    synth::TimelineDoc doc;
    synth::ui::TimelinePanelComponent panel;
    panel.setSize(1200, 400); // tall enough that all 3 rows are laid out with no scroll offset
    panel.setTimelineDoc(&doc);

    const auto a = doc.addTrack(TrackKind::Midi, "A");
    const auto b = doc.addTrack(TrackKind::Midi, "B");
    const auto c = doc.addTrack(TrackKind::Midi, "C");
    ASSERT_EQ(panel.getTrackHeaderCount(), 3);

    auto* rowA = panel.getTrackHeaderAt(0);
    ASSERT_NE(rowA, nullptr);
    const int rowHeight = rowA->getHeight();
    ASSERT_GT(rowHeight, 0);

    // Drag row A (index 0) down past rows B and C. Positions are LOCAL TO rowA (both mouseDown and
    // mouseDrag pass `comp = *rowA`, matching TimelineTrackHeaderComponent's own contract), so the
    // header resolves them to screen space via its real ancestor chain (trackHeaderList_ ->
    // trackHeaderViewport_ -> panel) exactly like production code does.
    rowA->mouseDown(makeClickEvent(*rowA, {40.0f, 10.0f}));
    rowA->mouseDrag(makeDragEvent(*rowA, {40.0f, (float)(rowHeight * 3)}, {40.0f, 10.0f}));

    // This mouseUp call is where the hazard lives: it drives the doc mutation that makes
    // TimelinePanelComponent::syncTrackHeaders() rebuild the ENTIRE header column — destroying
    // `rowA` from inside its own still-running mouseUp. Nothing after this line may dereference
    // rowA; the assertions below only ever go through panel/doc.
    rowA->mouseUp(makeDragEvent(*rowA, {40.0f, (float)(rowHeight * 3)}, {40.0f, 10.0f}));

    ASSERT_EQ(panel.getTrackHeaderCount(), 3) << "no track was added or lost, only reordered";
    const auto& tracks = doc.getTracks();
    EXPECT_EQ(tracks[0].id, b);
    EXPECT_EQ(tracks[1].id, c);
    EXPECT_EQ(tracks[2].id, a) << "dragged 3 row-heights down from index 0 in a 3-track list lands last";

    // The rebuilt column is a live, working view of the new order — not just left-over headers.
    EXPECT_EQ(panel.getTrackHeaderAt(0)->getTrackId(), b);
    EXPECT_EQ(panel.getTrackHeaderAt(2)->getTrackId(), a);
}

// T166: pixel-level regression test for the drop indicator's paint ORDER. An earlier version drew
// it in TrackHeaderList::paint() rather than paintOverChildren() — the header rows are children,
// painted AFTER their parent, and each does an OPAQUE fill of its own bounds
// (TimelineTrackHeaderComponent::paint()'s g.fillAll(colours.surface)), so the line was painted
// UNDER every interior row and invisible in practice. A themed LookAndFeel is installed
// specifically so colours.surface is a real opaque fill rather than headless test's default
// (fully transparent, alpha 0 — which would make the original bug invisible to this same test).
TEST(TimelinePanelTrackHeaderTest, DropIndicatorPaintsOverTheRowsNotUnderThem) {
    synth::TimelineDoc doc;
    synth::ui::TimelinePanelComponent panel;
    panel.setSize(1200, 400);
    panel.setTimelineDoc(&doc);

    synth::theme::AppLookAndFeel lf;
    const auto theme = synth::theme::makeObsidian();
    lf.applyTheme(theme);
    panel.setLookAndFeel(&lf);

    doc.addTrack(TrackKind::Midi, "A");
    doc.addTrack(TrackKind::Midi, "B");
    doc.addTrack(TrackKind::Midi, "C");
    ASSERT_EQ(panel.getTrackHeaderCount(), 3);

    auto* rowA = panel.getTrackHeaderAt(0);
    ASSERT_NE(rowA, nullptr);
    const int rowHeight = rowA->getHeight();

    rowA->mouseDown(makeClickEvent(*rowA, {40.0f, 10.0f}));
    // Same boundary math as WholeRowDragReordersTracksAndSurvivesTheHeaderRebuildMidGesture above:
    // dragging to local Y == 2*rowHeight resolves to dragInsertionIndex_ == 2, the boundary between
    // row 1 (B) and row 2 (C).
    rowA->mouseDrag(makeDragEvent(*rowA, {40.0f, (float)(rowHeight * 2)}, {40.0f, 10.0f}));

    auto& list = panel.getTrackHeaderListForTest();
    const juce::Image snapshot = list.createComponentSnapshot(list.getLocalBounds());
    ASSERT_FALSE(snapshot.isNull());

    bool foundIndicator = false;
    const int boundaryY = rowHeight * 2;
    for (int x = 0; x < snapshot.getWidth() && !foundIndicator; ++x)
        for (int y = boundaryY - 1; y <= boundaryY; ++y)
            if (juce::isPositiveAndBelow(y, snapshot.getHeight()) && snapshot.getPixelAt(x, y) == theme.colors.accent)
                foundIndicator = true;
    EXPECT_TRUE(foundIndicator) << "the drop-indicator line must paint OVER the rows, not under them";

    rowA->mouseUp(makeDragEvent(*rowA, {40.0f, (float)(rowHeight * 2)}, {40.0f, 10.0f}));
    panel.setLookAndFeel(nullptr);
}

TEST(TimelinePanelTrackHeaderTest, AddButtonAndHeaderListStayInsideTheHeaderColumn) {
    synth::TimelineDoc doc;
    synth::ui::TimelinePanelComponent panel;
    panel.setSize(1200, 220);
    panel.setTimelineDoc(&doc);

    const auto column = panel.getTrackHeaderBounds();
    EXPECT_TRUE(column.contains(panel.getAddTrackButton().getBounds()));
    EXPECT_TRUE(column.contains(panel.getTrackHeaderViewport().getBounds()));
    // The button sits above the scrolling list, and the two do not overlap.
    EXPECT_LE(panel.getAddTrackButton().getBounds().getBottom(), panel.getTrackHeaderViewport().getY());
}

TEST(TimelinePanelTrackHeaderTest, HeaderListScrollsWhenTracksOverflow) {
    synth::TimelineDoc doc;
    synth::ui::TimelinePanelComponent panel;
    panel.setSize(1200, 220);
    panel.setTimelineDoc(&doc);

    for (int i = 0; i < 12; ++i)
        doc.addTrack(TrackKind::Midi, "Track " + juce::String(i + 1));

    ASSERT_EQ(panel.getTrackHeaderCount(), 12);
    auto& viewport = panel.getTrackHeaderViewport();
    ASSERT_NE(viewport.getViewedComponent(), nullptr);
    EXPECT_GT(viewport.getViewedComponent()->getHeight(), viewport.getMaximumVisibleHeight())
        << "12 rows must overflow a 220 px panel — that is what the Viewport is for";
    EXPECT_TRUE(viewport.isVerticalScrollBarShown());
}
