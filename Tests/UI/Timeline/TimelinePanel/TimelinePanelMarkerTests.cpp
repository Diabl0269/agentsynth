// TimelinePanelMarkerTests.cpp
//
// Timeline ruler markers (flags): hit-testing, drag-to-move, rename, colour picker, context
// menu, loop-toggle interaction; plus the panel's column divider and grid-line drawing.
// RecordingMarkerRuler/MarkerRulerFixture are local to this file.

#include "AI/AIProvider.h"
#include "AI/AIStateMapper/AIStateMapper.h"
#include "AppUndoManager.h"
#include "MainComponent/MainComponent.h"
#include "ProjectBundle.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "TimelinePanelTestFixture.h"
#include "Transport/TransportService.h"
#include "UI/Theme/AppLookAndFeel.h"
#include "UI/Theme/BuiltInThemes.h"
#include "UI/Timeline/EdgeAutoScroll.h"
#include "UI/Timeline/TimelinePanelComponent/TimelinePanelComponent.h"
#include "UI/Timeline/TrackColour.h"
#include "UserSettings.h"
#include <algorithm>
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

// ---------------------------------------------------------------------------
// 3b. Ruler markers — flags, hit-testing, drag-to-move, the context menu's
//     verbs and the undo shape.
// ---------------------------------------------------------------------------

namespace {

// A ruler wired to a doc + transport, at 40 px/beat with Beat snap — the same mapping every ruler
// test above uses, so a pixel figure means the same thing throughout this file.
// Records the marker context-menu REQUEST instead of opening a real one. A live juce::PopupMenu
// creates a top-level window, and JUCE positions it against a display it looks up from the mouse
// point — on a display-less CI runner (the Linux Debug coverage job) that lookup returns null and
// MenuWindow::calculateWindowPos dereferences it, so a test that reaches the real path SIGSEGVs
// there while passing on macOS/Windows. Same seam idiom as
// PianoRollComponent::promptExtendClipToFitNotes.
class RecordingMarkerRuler : public synth::ui::TimelineRulerComponent {
public:
    using synth::ui::TimelineRulerComponent::TimelineRulerComponent;

    int menuRequests = 0;
    synth::MarkerId lastMenuMarker;

protected:
    void openMarkerContextMenu(synth::MarkerId id) override {
        ++menuRequests;
        lastMenuMarker = id;
    }
};

struct MarkerRulerFixture {
    synth::ui::TimelineViewState state;
    synth::TransportService transport;
    synth::TimelineDoc doc;
    AppUndoManager undo;
    std::unique_ptr<RecordingMarkerRuler> ruler;

    MarkerRulerFixture(bool withUndoManager = true) {
        state.pixelsPerBeat = 40.0;
        state.firstVisibleBeat = 0.0;
        state.snap = synth::ui::TimelineViewState::Snap::Quarter;
        transport.prepare(48000.0, 512);

        ruler = std::make_unique<RecordingMarkerRuler>(state);
        ruler->setTransport(&transport);
        ruler->setTimelineDoc(&doc);
        if (withUndoManager)
            ruler->setUndoManager(&undo);
        ruler->setSize(800, 24);
    }
};

// The centre of a marker's flag, which is what a click has to land on.
juce::Point<float> flagCentre(const synth::ui::TimelineRulerComponent& ruler, synth::MarkerId id) {
    for (const auto& flag : ruler.buildMarkerFlags())
        if (flag.id == id)
            return flag.bounds.getCentre();
    return {-1.0f, -1.0f};
}

} // namespace

TEST(TimelineRulerMarkerTest, FlagsSitInTheirOwnBandBelowTheNumbersRow) {
    MarkerRulerFixture f;
    EXPECT_TRUE(f.ruler->buildMarkerFlags().empty()) << "no markers, no flags";

    const auto intro = f.doc.addMarker(2.0, "Intro", 0xffE0A33D);
    const auto chorus = f.doc.addMarker(10.0, "Chorus", 0xff44AA88);

    const auto flags = f.ruler->buildMarkerFlags();
    ASSERT_EQ(flags.size(), 2u);
    // Doc (beat, id) order, and anchored at the marker's own beat: 2 * 40 = 80, 10 * 40 = 400.
    EXPECT_EQ(flags[0].id, intro);
    EXPECT_FLOAT_EQ(flags[0].bounds.getX(), 80.0f);
    EXPECT_EQ(flags[1].id, chorus);
    EXPECT_FLOAT_EQ(flags[1].bounds.getX(), 400.0f);
    EXPECT_EQ(flags[0].colour, juce::Colour(0xffE0A33D));
    EXPECT_EQ(flags[0].text, "Intro");

    // THE regression this pins: a flag must start where the NUMBERS ROW ENDS, so a marker at beat 4
    // cannot be drawn over the ruler's own "4". At 24 px tall that is y = 14..23.
    const float labelRow = synth::ui::rulerLabelRowHeight(24);
    EXPECT_FLOAT_EQ(labelRow, 24.0f - synth::ui::kMarkerBandHeight);
    for (const auto& flag : flags) {
        EXPECT_FLOAT_EQ(flag.bounds.getY(), labelRow) << "the band starts exactly where the numbers row ends";
        EXPECT_GE(flag.bounds.getY(), 8.0f) << "a flag must not sit under the loop brace either";
        EXPECT_LE(flag.bounds.getBottom(), 24.0f);
    }

    // A strip too short to leave the numbers a usable row keeps its full height for them rather
    // than ending up with a negative row or a flag taller than the strip.
    EXPECT_FLOAT_EQ(synth::ui::rulerLabelRowHeight(8), 8.0f);
    EXPECT_FLOAT_EQ(synth::ui::rulerLabelRowHeight(0), 0.0f);
    // ...and the boundary: the band appears as soon as kMinNumbersRowHeight survives it.
    const int firstBanded = (int)(synth::ui::kMarkerBandHeight + synth::ui::kMinNumbersRowHeight);
    EXPECT_FLOAT_EQ(synth::ui::rulerLabelRowHeight(firstBanded), (float)firstBanded - synth::ui::kMarkerBandHeight);
    EXPECT_FLOAT_EQ(synth::ui::rulerLabelRowHeight(firstBanded - 1), (float)(firstBanded - 1));

    // Width comes from the label's character COUNT, never from measured text.
    EXPECT_FLOAT_EQ(flags[0].bounds.getWidth(), synth::ui::markerFlagWidthFor(5));
    // An unlabelled marker still gets a grabbable tab.
    const auto blank = f.doc.addMarker(20.0, "", 0xff112233);
    const auto withBlank = f.ruler->buildMarkerFlags();
    ASSERT_EQ(withBlank.size(), 3u);
    EXPECT_EQ(withBlank[2].id, blank);
    EXPECT_FLOAT_EQ(withBlank[2].bounds.getWidth(), synth::ui::kMarkerMinFlagWidth);
}

TEST(TimelineRulerMarkerTest, FlagsAreCulledPerFlagNotPerRange) {
    MarkerRulerFixture f;
    f.doc.addMarker(2.0, "Visible", 0xff000000);
    f.doc.addMarker(500.0, "Way off to the right", 0xff000000);
    // 800 px / 40 = 20 visible beats, so only the first is on screen.
    EXPECT_EQ(f.ruler->buildMarkerFlags().size(), 1u);

    // Scrolled so the marker's anchor is just LEFT of x = 0 but its tab still overlaps the strip:
    // culling by the anchor alone would wrongly drop it.
    f.state.firstVisibleBeat = 2.05; // anchor at -2 px, tab runs ~30 px to the right
    const auto scrolled = f.ruler->buildMarkerFlags();
    ASSERT_EQ(scrolled.size(), 1u) << "a partly-visible flag stays in the list";
    EXPECT_LT(scrolled[0].bounds.getX(), 0.0f);
    EXPECT_GT(scrolled[0].bounds.getRight(), 0.0f);
}

TEST(TimelineRulerMarkerTest, HitTestResolvesTheFlagUnderThePointerAndNothingElse) {
    MarkerRulerFixture f;
    const auto intro = f.doc.addMarker(2.0, "Intro", 0xff000000);

    const auto centre = flagCentre(*f.ruler, intro);
    EXPECT_EQ(f.ruler->markerAt(centre), intro);
    // Just above the flag is the ruler, not the marker.
    EXPECT_FALSE(f.ruler->markerAt({centre.x, 2.0f}).isValid());
    // Left of the anchor is not the marker either — the tab runs RIGHT of the beat.
    EXPECT_FALSE(f.ruler->markerAt({70.0f, centre.y}).isValid());
    EXPECT_FALSE(f.ruler->markerAt({600.0f, centre.y}).isValid());

    // Two overlapping flags: the later (topmost drawn) one wins.
    const auto overlapping = f.doc.addMarker(2.05, "Overlap", 0xff000000);
    EXPECT_EQ(f.ruler->markerAt(flagCentre(*f.ruler, overlapping)), overlapping);
}

TEST(TimelineRulerMarkerTest, DragMovesTheMarkerSnappedAsOneUndoStep) {
    MarkerRulerFixture f;
    const auto intro = f.doc.addMarker(2.0, "Intro", 0xff000000);
    const auto revisionBefore = f.doc.getRevision();

    const auto press = flagCentre(*f.ruler, intro);
    f.ruler->mouseDown(makeClickEvent(*f.ruler, press));
    EXPECT_EQ(f.ruler->getDraggingMarkerForTest(), intro);
    EXPECT_DOUBLE_EQ(f.doc.getMarker(intro)->beat, 2.0) << "the press itself must not move anything";

    // +160 px == +4 beats, snapped to the Quarter grid (a whole beat here).
    f.ruler->mouseDrag(makeDragEvent(*f.ruler, {press.x + 160.0f, press.y}, press));
    EXPECT_DOUBLE_EQ(f.ruler->getMarkerDragBeatForTest(), 6.0);
    EXPECT_DOUBLE_EQ(f.doc.getMarker(intro)->beat, 2.0) << "a drag in flight is a preview only";
    // The PREVIEW is what the flag (and therefore the hit test) follows mid-drag.
    EXPECT_FLOAT_EQ(f.ruler->buildMarkerFlags()[0].bounds.getX(), 240.0f);

    f.ruler->mouseUp(makeDragEvent(*f.ruler, {press.x + 160.0f, press.y}, press));
    EXPECT_FALSE(f.ruler->getDraggingMarkerForTest().isValid());
    EXPECT_DOUBLE_EQ(f.doc.getMarker(intro)->beat, 6.0);
    EXPECT_EQ(f.doc.getRevision(), revisionBefore + 1) << "one mutation for the whole drag";

    // ONE undo step, and it puts the marker back.
    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    EXPECT_DOUBLE_EQ(f.doc.getMarker(intro)->beat, 2.0);
}

TEST(TimelineRulerMarkerTest, DragNeverGoesNegativeAndAPressThatNeverMovedCommitsNothing) {
    MarkerRulerFixture f;
    const auto marker = f.doc.addMarker(2.0, "M", 0xff000000);
    const auto revisionBefore = f.doc.getRevision();

    const auto press = flagCentre(*f.ruler, marker);
    f.ruler->mouseDown(makeClickEvent(*f.ruler, press));
    f.ruler->mouseUp(makeClickEvent(*f.ruler, press)); // mouseWasDragged == false
    EXPECT_DOUBLE_EQ(f.doc.getMarker(marker)->beat, 2.0);
    EXPECT_EQ(f.doc.getRevision(), revisionBefore) << "a stray click must not re-snap the marker";
    EXPECT_FALSE(f.undo.canUndo());

    // Dragged far left: clamped at beat 0, never negative (the model would refuse it).
    f.ruler->mouseDown(makeClickEvent(*f.ruler, press));
    f.ruler->mouseDrag(makeDragEvent(*f.ruler, {-500.0f, press.y}, press));
    EXPECT_DOUBLE_EQ(f.ruler->getMarkerDragBeatForTest(), 0.0);
    f.ruler->mouseUp(makeDragEvent(*f.ruler, {-500.0f, press.y}, press));
    EXPECT_DOUBLE_EQ(f.doc.getMarker(marker)->beat, 0.0);
}

// A marker press must not also scrub or set a loop — and the reverse: the zone gestures still work
// everywhere a flag is not.
TEST(TimelineRulerMarkerTest, AMarkerPressConsumesTheGestureButLeavesTheRestOfTheStripAlone) {
    MarkerRulerFixture f;
    const auto marker = f.doc.addMarker(2.0, "M", 0xff000000);

    const auto onFlag = flagCentre(*f.ruler, marker);
    f.ruler->mouseDown(makeClickEvent(*f.ruler, onFlag));
    f.ruler->mouseDrag(makeDragEvent(*f.ruler, {onFlag.x + 80.0f, onFlag.y}, onFlag));
    f.ruler->mouseUp(makeDragEvent(*f.ruler, {onFlag.x + 80.0f, onFlag.y}, onFlag));
    f.transport.tick(512);
    EXPECT_EQ(f.ruler->getSeekPostCountForTest(), 0) << "a marker drag never scrubs";
    EXPECT_FALSE(f.transport.getPositionSnapshot().looping) << "and never sets a loop";

    // The playhead zone, clear of every flag, still scrubs exactly as before.
    const juce::Point<float> emptyPlayheadZone(600.0f, kPlayheadZoneY);
    ASSERT_FALSE(f.ruler->markerAt(emptyPlayheadZone).isValid());
    f.ruler->mouseDown(makeClickEvent(*f.ruler, emptyPlayheadZone));
    f.transport.tick(512);
    EXPECT_EQ(f.ruler->getSeekPostCountForTest(), 1);
    EXPECT_DOUBLE_EQ(f.transport.getPositionSnapshot().ppq, 15.0);
}

// Cmd+click stays the "switch looping off" gesture even over a flag — a marker must not punch dead
// holes in it.
TEST(TimelineRulerMarkerTest, CommandClickOverAFlagStillTogglesLooping) {
    MarkerRulerFixture f;
    const auto marker = f.doc.addMarker(2.0, "M", 0xff000000);
    ASSERT_TRUE(f.transport.setLoop(0.0, 8.0, true));
    f.transport.tick(512);

    const auto onFlag = flagCentre(*f.ruler, marker);
    f.ruler->mouseDown(makeClickEvent(*f.ruler, onFlag, juce::ModifierKeys::commandModifier));
    f.transport.tick(512);
    EXPECT_FALSE(f.transport.getPositionSnapshot().looping);
    EXPECT_FALSE(f.ruler->getDraggingMarkerForTest().isValid()) << "no marker drag was started";
    EXPECT_DOUBLE_EQ(f.doc.getMarker(marker)->beat, 2.0);
}

TEST(TimelineRulerMarkerTest, HoverTracksTheFlagUnderThePointerAndClearsOnExit) {
    MarkerRulerFixture f;
    const auto marker = f.doc.addMarker(2.0, "Intro", 0xff000000);
    const auto onFlag = flagCentre(*f.ruler, marker);

    f.ruler->mouseEnter(makeClickEvent(*f.ruler, onFlag));
    EXPECT_EQ(f.ruler->getHoveredMarkerForTest(), marker);

    f.ruler->mouseMove(makeClickEvent(*f.ruler, {600.0f, onFlag.y}));
    EXPECT_FALSE(f.ruler->getHoveredMarkerForTest().isValid());

    f.ruler->mouseMove(makeClickEvent(*f.ruler, onFlag));
    EXPECT_EQ(f.ruler->getHoveredMarkerForTest(), marker);
    f.ruler->mouseExit(makeClickEvent(*f.ruler, onFlag));
    EXPECT_FALSE(f.ruler->getHoveredMarkerForTest().isValid());
}

TEST(TimelineRulerMarkerTest, ContextMenuDeleteIsOneUndoStepAndTheOtherTwoChoicesMutateNothing) {
    using Choice = synth::ui::TimelineRulerComponent::MarkerContextChoice;
    MarkerRulerFixture f;
    const auto marker = f.doc.addMarker(2.0, "Intro", 0xff112233);
    const auto revisionAfterAdd = f.doc.getRevision();

    // Rename and ChangeColour open UI; neither is a mutation.
    f.ruler->applyMarkerContextChoice(marker, Choice::Rename);
    f.ruler->applyMarkerContextChoice(marker, Choice::ChangeColour);
    EXPECT_EQ(f.doc.getRevision(), revisionAfterAdd);
    EXPECT_NE(f.doc.getMarker(marker), nullptr);

    f.ruler->applyMarkerContextChoice(marker, Choice::Delete);
    EXPECT_EQ(f.doc.getMarker(marker), nullptr);
    EXPECT_TRUE(f.doc.getMarkers().empty());
    EXPECT_FALSE(f.ruler->getHoveredMarkerForTest().isValid());

    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    ASSERT_EQ(f.doc.getMarkers().size(), 1u);
    EXPECT_EQ(f.doc.getMarkers().front().text, "Intro");

    // A choice naming a marker that is already gone is inert, not a crash.
    f.ruler->applyMarkerContextChoice(synth::MarkerId{999}, Choice::Delete);
    EXPECT_EQ(f.doc.getMarkers().size(), 1u);
}

TEST(TimelineRulerMarkerTest, RenameCommitsThroughTheDocAndRefusesAnOverLongLabel) {
    MarkerRulerFixture f;
    const auto marker = f.doc.addMarker(2.0, "Intro", 0xff000000);

    f.ruler->renameMarker(marker, "Verse 1");
    EXPECT_EQ(f.doc.getMarker(marker)->text, "Verse 1");
    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    EXPECT_EQ(f.doc.getMarker(marker)->text, "Intro");

    // Over the cap: refused by the doc, which leaves the existing label in place.
    f.ruler->renameMarker(marker, juce::String::repeatedString("x", synth::TimelineDoc::kMaxMarkerTextLength + 1));
    EXPECT_EQ(f.doc.getMarker(marker)->text, "Intro");
}

TEST(TimelineRulerMarkerTest, InlineRenameEditorCommitsOnReturnAndCancelsOnEscape) {
    MarkerRulerFixture f;
    const auto marker = f.doc.addMarker(2.0, "Intro", 0xff000000);

    f.ruler->beginRenameMarker(marker);
    auto* editor = f.ruler->getMarkerRenameEditorForTest();
    ASSERT_NE(editor, nullptr);
    EXPECT_EQ(editor->getText(), "Intro") << "the editor opens on the current label";
    // Inside the strip, and wide enough to type into.
    EXPECT_TRUE(f.ruler->getLocalBounds().contains(editor->getBounds()));
    EXPECT_GE(editor->getWidth(), 96);

    editor->setText("Bridge", juce::dontSendNotification);
    editor->onReturnKey();
    EXPECT_EQ(f.ruler->getMarkerRenameEditorForTest(), nullptr) << "committing closes the editor";
    EXPECT_EQ(f.doc.getMarker(marker)->text, "Bridge");

    // Escape is the cancel.
    f.ruler->beginRenameMarker(marker);
    editor = f.ruler->getMarkerRenameEditorForTest();
    ASSERT_NE(editor, nullptr);
    editor->setText("Discarded", juce::dontSendNotification);
    editor->onEscapeKey();
    EXPECT_EQ(f.ruler->getMarkerRenameEditorForTest(), nullptr);
    EXPECT_EQ(f.doc.getMarker(marker)->text, "Bridge");

    // A marker scrolled off screen has nowhere to put an editor.
    f.state.firstVisibleBeat = 500.0;
    f.ruler->beginRenameMarker(marker);
    EXPECT_EQ(f.ruler->getMarkerRenameEditorForTest(), nullptr);
}

// The simpler rename path: double-click the FLAG. Scoped to flag hits — the ruler had no
// double-click handler at all before, so everywhere else the two clicks keep their old meaning.
TEST(TimelineRulerMarkerTest, DoubleClickOnAFlagOpensTheRenameEditor) {
    MarkerRulerFixture f;
    const auto marker = f.doc.addMarker(2.0, "Intro", 0xff000000);
    const auto onFlag = flagCentre(*f.ruler, marker);

    // JUCE's real order for a double-click: down, up, down, doubleClick, up. Drive all of it, so
    // the interaction between the latched marker drag and the rename is exercised for real.
    f.ruler->mouseDown(makeClickEvent(*f.ruler, onFlag));
    f.ruler->mouseUp(makeClickEvent(*f.ruler, onFlag));
    f.ruler->mouseDown(makeClickEvent(*f.ruler, onFlag));
    f.ruler->mouseDoubleClick(makeClickEvent(*f.ruler, onFlag));

    auto* editor = f.ruler->getMarkerRenameEditorForTest();
    ASSERT_NE(editor, nullptr) << "double-clicking a flag opens the inline rename editor";
    EXPECT_EQ(editor->getText(), "Intro");

    // The trailing mouseUp must neither commit a move nor seek the cursor out from under the editor.
    f.ruler->mouseUp(makeClickEvent(*f.ruler, onFlag));
    f.transport.tick(512);
    EXPECT_NE(f.ruler->getMarkerRenameEditorForTest(), nullptr) << "the editor survives the trailing mouseUp";
    EXPECT_DOUBLE_EQ(f.doc.getMarker(marker)->beat, 2.0) << "no move was committed";
    EXPECT_EQ(f.ruler->getSeekPostCountForTest(), 0) << "and the playhead never moved";
    EXPECT_FALSE(f.undo.canUndo());

    editor = f.ruler->getMarkerRenameEditorForTest();
    editor->setText("Verse", juce::dontSendNotification);
    editor->onReturnKey();
    EXPECT_EQ(f.doc.getMarker(marker)->text, "Verse");
}

// THE regression behind "the right-click menu flashes and dismisses itself". The menu is opened
// from mouseDown; the release that follows must not touch the transport or repaint, or it kills the
// modal window it just opened. The harness cannot observe a juce::PopupMenu's lifetime, so this pins
// the gesture-state invariant instead: a right-click latches NOTHING and posts NOTHING.
TEST(TimelineRulerMarkerTest, RightClickOnAFlagLatchesNothingAndPostsNothing) {
    MarkerRulerFixture f;
    const auto marker = f.doc.addMarker(2.0, "Intro", 0xff000000);
    ASSERT_TRUE(f.transport.setLoop(1.0, 9.0, true));
    f.transport.tick(512);
    const double posBefore = f.transport.getPositionSnapshot().ppq;
    const auto revisionBefore = f.doc.getRevision();

    const auto onFlag = flagCentre(*f.ruler, marker);
    const auto rightClick = makeClickEvent(*f.ruler, onFlag, juce::ModifierKeys::popupMenuClickModifier);

    f.ruler->mouseDown(rightClick);
    EXPECT_EQ(f.ruler->menuRequests, 1) << "the press opens the menu";
    EXPECT_EQ(f.ruler->lastMenuMarker, marker) << "...for the marker that was clicked";
    EXPECT_FALSE(f.ruler->getDraggingMarkerForTest().isValid()) << "a right-click must not latch a marker drag";

    // The whole release path: this is what used to run postSeekIfChanged() under a STALE latched
    // gestureZone_ and dismiss the menu.
    f.ruler->mouseDrag(
        makeDragEvent(*f.ruler, {onFlag.x + 40.0f, onFlag.y}, onFlag, juce::ModifierKeys::popupMenuClickModifier));
    f.ruler->mouseUp(rightClick);
    f.transport.tick(512);

    // The release must not open a SECOND menu either, and — the actual regression — must not have
    // run the scrub path that used to dismiss the first one.
    EXPECT_EQ(f.ruler->menuRequests, 1);
    EXPECT_EQ(f.ruler->getSeekPostCountForTest(), 0) << "a right-click never scrubs the playhead";
    EXPECT_DOUBLE_EQ(f.transport.getPositionSnapshot().ppq, posBefore);
    EXPECT_DOUBLE_EQ(f.transport.getPositionSnapshot().loopStartPpq, 1.0) << "and never moves a locator";
    EXPECT_DOUBLE_EQ(f.transport.getPositionSnapshot().loopEndPpq, 9.0);
    EXPECT_TRUE(f.transport.getPositionSnapshot().looping);
    EXPECT_EQ(f.doc.getRevision(), revisionBefore) << "and mutates nothing";
    EXPECT_FALSE(f.ruler->getDraggingMarkerForTest().isValid());
}

// The same gate, off a flag: a right-click anywhere on the strip is inert. It used to seek, because
// mouseDown latched the playhead zone for ANY button and seeked on press.
TEST(TimelineRulerMarkerTest, RightClickOffAFlagDoesNotSeekOrLoop) {
    MarkerRulerFixture f;
    ASSERT_TRUE(f.transport.setLoop(1.0, 9.0, true));
    f.transport.tick(512);
    const double posBefore = f.transport.getPositionSnapshot().ppq;

    for (const float y : {kLoopZoneY, kPlayheadZoneY}) {
        const auto pos = juce::Point<float>{600.0f, y};
        const auto rightClick = makeClickEvent(*f.ruler, pos, juce::ModifierKeys::popupMenuClickModifier);
        f.ruler->mouseDown(rightClick);
        f.ruler->mouseUp(rightClick);
    }
    f.transport.tick(512);

    EXPECT_EQ(f.ruler->menuRequests, 0) << "off a flag there is no marker menu to open";
    EXPECT_EQ(f.ruler->getSeekPostCountForTest(), 0);
    EXPECT_DOUBLE_EQ(f.transport.getPositionSnapshot().ppq, posBefore);
    EXPECT_DOUBLE_EQ(f.transport.getPositionSnapshot().loopStartPpq, 1.0);
    EXPECT_DOUBLE_EQ(f.transport.getPositionSnapshot().loopEndPpq, 9.0);

    // ...while the LEFT button still does everything it always did.
    const auto leftClick = makeClickEvent(*f.ruler, {600.0f, kPlayheadZoneY});
    f.ruler->mouseDown(leftClick);
    f.transport.tick(512);
    EXPECT_EQ(f.ruler->getSeekPostCountForTest(), 1) << "the left-button scrub is untouched";
    EXPECT_DOUBLE_EQ(f.transport.getPositionSnapshot().ppq, 15.0);
}

// Marker visuals: the flag has to be big enough to read, and its label has to contrast against
// whatever colour the user picked.
TEST(TimelineRulerMarkerTest, FlagsAreLargeEnoughToReadAndLabelsContrastWithTheFill) {
    // A dark fill takes white text, a light fill takes black — both branches of the pure helper.
    EXPECT_EQ(synth::ui::markerLabelColourFor(juce::Colour(0xff101010)), juce::Colours::white);
    EXPECT_EQ(synth::ui::markerLabelColourFor(juce::Colour(0xffF0F0F0)), juce::Colours::black);
    // The amber a new marker gets is a light mid-tone: black text, which is the case the old
    // fixed-token version got wrong.
    EXPECT_EQ(synth::ui::markerLabelColourFor(juce::Colour(0xffE0A33D)), juce::Colours::black);

    // The flag is tall enough for its own 11 pt label, and the stem is not a hairline.
    EXPECT_GE(synth::ui::kMarkerFlagHeight, synth::ui::kMarkerFlagFontHeight + 1.0f);
    EXPECT_GE(synth::ui::kMarkerStemWidth, 2.0f);

    // At the themed 30 px strip the numbers row still clears the bar-number font, and the band gets
    // the rest — the two tile exactly.
    const float labelRow = synth::ui::rulerLabelRowHeight(30);
    EXPECT_FLOAT_EQ(labelRow, 30.0f - synth::ui::kMarkerBandHeight);
    EXPECT_GE(labelRow, 13.0f) << "the bar numbers must still have room to render";

    MarkerRulerFixture f;
    f.ruler->setSize(800, 30);
    const auto marker = f.doc.addMarker(2.0, "Chorus", 0xffE0A33D);
    const auto flags = f.ruler->buildMarkerFlags();
    ASSERT_EQ(flags.size(), 1u);
    EXPECT_FLOAT_EQ(flags[0].bounds.getY(), labelRow);
    EXPECT_FLOAT_EQ(flags[0].bounds.getHeight(), synth::ui::kMarkerFlagHeight);
    // "Chorus" is 6 characters — the tab is sized from the count, so it is wide enough to hold them.
    EXPECT_FLOAT_EQ(flags[0].bounds.getWidth(), synth::ui::markerFlagWidthFor(6));
    EXPECT_GT(flags[0].bounds.getWidth(), 6.0f * synth::ui::kMarkerCharWidthPx);
    EXPECT_EQ(f.ruler->markerAt(flags[0].bounds.getCentre()), marker) << "and the bigger tab is all clickable";
}

TEST(TimelineRulerMarkerTest, DoubleClickOffAFlagOpensNothing) {
    MarkerRulerFixture f;
    f.doc.addMarker(2.0, "Intro", 0xff000000);

    // In the numbers row directly above the flag, and far to the right of every flag.
    f.ruler->mouseDoubleClick(makeClickEvent(*f.ruler, {84.0f, 3.0f}));
    EXPECT_EQ(f.ruler->getMarkerRenameEditorForTest(), nullptr);
    f.ruler->mouseDoubleClick(makeClickEvent(*f.ruler, {600.0f, kPlayheadZoneY}));
    EXPECT_EQ(f.ruler->getMarkerRenameEditorForTest(), nullptr);

    // A right-button double-click is the context menu's business, not the rename's.
    f.ruler->mouseDoubleClick(makeClickEvent(*f.ruler, flagCentre(*f.ruler, f.doc.getMarkers().front().id),
                                             juce::ModifierKeys::popupMenuClickModifier));
    EXPECT_EQ(f.ruler->getMarkerRenameEditorForTest(), nullptr);
}

TEST(TimelineRulerMarkerTest, ColourPickerPreviewsLiveAndCommitsOneUndoStep) {
    MarkerRulerFixture f;
    const auto marker = f.doc.addMarker(2.0, "Intro", 0xff112233);

    // Driven through the popup's OWN test seams, never a real juce::CallOutBox — the same way
    // TimelineTrackHeaderTests drives the track-colour swatch.
    auto picker = f.ruler->createMarkerColourPickerForTest(marker);
    ASSERT_NE(picker, nullptr);

    picker->setCurrentColourForTest(juce::Colour(0xff445566));
    EXPECT_EQ(f.doc.getMarker(marker)->colourArgb, 0xff445566u) << "a preview writes the doc directly";
    EXPECT_FALSE(f.undo.canUndo()) << "...and records nothing";

    picker->setCurrentColourForTest(juce::Colour(0xff778899));
    picker->commitForTest();
    EXPECT_EQ(f.doc.getMarker(marker)->colourArgb, 0xff778899u);
    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    EXPECT_EQ(f.doc.getMarker(marker)->colourArgb, 0xff112233u) << "undo restores the ORIGINAL, not the preview";

    // Closing on the colour it started with records no step at all.
    auto noChange = f.ruler->createMarkerColourPickerForTest(marker);
    ASSERT_NE(noChange, nullptr);
    noChange->setCurrentColourForTest(juce::Colour(0xffabcdef));
    noChange->setCurrentColourForTest(juce::Colour(0xff112233));
    noChange->commitForTest();
    EXPECT_EQ(f.doc.getMarker(marker)->colourArgb, 0xff112233u);
    EXPECT_FALSE(f.undo.canUndo());

    EXPECT_EQ(f.ruler->createMarkerColourPickerForTest(synth::MarkerId{999}), nullptr);
}

// Without an undo manager every marker edit still applies — it just does not land on a stack.
TEST(TimelineRulerMarkerTest, MarkerEditsWorkWithNoUndoManagerInstalled) {
    MarkerRulerFixture f(/*withUndoManager=*/false);
    const auto marker = f.doc.addMarker(2.0, "Intro", 0xff000000);

    f.ruler->renameMarker(marker, "Renamed");
    EXPECT_EQ(f.doc.getMarker(marker)->text, "Renamed");
    f.ruler->applyMarkerContextChoice(marker, synth::ui::TimelineRulerComponent::MarkerContextChoice::Delete);
    EXPECT_TRUE(f.doc.getMarkers().empty());
    EXPECT_FALSE(f.undo.canUndo());
}

// Markers live on the DOC, so they stay editable in a build with no transport wired in — and
// clearing the doc drops every gesture that named it.
TEST(TimelineRulerMarkerTest, MarkersWorkWithoutATransportAndSwappingTheDocResetsGestureState) {
    synth::ui::TimelineViewState state;
    state.pixelsPerBeat = 40.0;
    state.snap = synth::ui::TimelineViewState::Snap::Quarter;
    synth::TimelineDoc doc;
    synth::ui::TimelineRulerComponent ruler(state);
    ruler.setTimelineDoc(&doc);
    ruler.setSize(800, 24);
    ASSERT_EQ(ruler.getTransport(), nullptr);

    const auto marker = doc.addMarker(2.0, "Intro", 0xff000000);
    const auto press = flagCentre(ruler, marker);
    ruler.mouseDown(makeClickEvent(ruler, press));
    EXPECT_EQ(ruler.getDraggingMarkerForTest(), marker) << "no transport, but the marker still drags";
    ruler.mouseDrag(makeDragEvent(ruler, {press.x + 160.0f, press.y}, press));
    ruler.mouseUp(makeDragEvent(ruler, {press.x + 160.0f, press.y}, press));
    EXPECT_DOUBLE_EQ(doc.getMarker(marker)->beat, 6.0);

    // Re-pointing at another doc drops the drag/hover ids and any open rename.
    ruler.mouseEnter(makeClickEvent(ruler, flagCentre(ruler, marker)));
    ruler.beginRenameMarker(marker);
    ASSERT_NE(ruler.getMarkerRenameEditorForTest(), nullptr);
    synth::TimelineDoc other;
    ruler.setTimelineDoc(&other);
    EXPECT_EQ(ruler.getMarkerRenameEditorForTest(), nullptr);
    EXPECT_FALSE(ruler.getHoveredMarkerForTest().isValid());
    EXPECT_TRUE(ruler.buildMarkerFlags().empty());

    // No doc at all: every gesture is inert rather than a crash.
    ruler.setTimelineDoc(nullptr);
    ruler.mouseDown(makeClickEvent(ruler, {80.0f, kPlayheadZoneY}));
    EXPECT_FALSE(ruler.getDraggingMarkerForTest().isValid());
    EXPECT_TRUE(ruler.buildMarkerFlags().empty());
}

// The track-header column and whatever sits to its right (the lanes, the open piano roll, and the
// roll's own scale sidebar) used to butt together with no separation. The divider is drawn by the
// panel — the component that owns the seam — so a future right-side sidebar inherits it.
TEST(TimelinePanelDividerTest, AColumnDividerSeparatesTheHeaderColumnFromTheLanes) {
    synth::ui::TimelinePanelComponent panel;
    panel.setSize(1200, 260);

    const auto header = panel.getTrackHeaderBounds();
    const auto lanes = panel.getLanesBounds();
    ASSERT_FALSE(header.isEmpty());
    ASSERT_FALSE(lanes.isEmpty());

    // The seam the divider is painted on: the header column's right edge, which is also the lanes'
    // left edge (the three regions tile, so there is exactly one boundary here).
    EXPECT_EQ(header.getRight(), lanes.getX()) << "precondition: the two columns are flush";

    // It spans from the header column's top (below the transport strip) to the panel's bottom — the
    // transport bar is one continuous row and must not be cut in half.
    EXPECT_GT(header.getY(), 0) << "the header column starts below the transport strip";
    EXPECT_EQ(header.getBottom(), panel.getHeight());

    // Painted, not a child component: the panel draws it, so nothing has to be laid out for it and
    // no sidebar can forget to.
    juce::Image image(juce::Image::ARGB, panel.getWidth(), panel.getHeight(), true);
    juce::Graphics g(image);
    EXPECT_NO_THROW(panel.paintEntireComponent(g, true));

    // The divider column is not left transparent — something was drawn on the seam.
    const int x = header.getRight() - 1;
    bool anyPixelPainted = false;
    for (int y = header.getY(); y < panel.getHeight(); ++y)
        if (image.getPixelAt(x, y).getAlpha() > 0) {
            anyPixelPainted = true;
            break;
        }
    EXPECT_TRUE(anyPixelPainted) << "the seam at x=" << x << " must carry a divider";
}

// A marker's stem is painted down through the clips as well as in the ruler, so a marker is locatable
// against the arrangement. Static paint, repainted only when the doc changes — no timer.
TEST(TimelinePanelDividerTest, MarkerStemsPaintThroughTheLanesAndFollowTheDoc) {
    synth::TimelineDoc doc;
    synth::ui::TimelinePanelComponent panel;
    panel.setTimelineDoc(&doc);
    panel.setSize(1200, 260);
    panel.getViewState().pixelsPerBeat = 40.0;
    panel.getViewState().firstVisibleBeat = 0.0;
    doc.addTrack(synth::TrackKind::Midi, "Track 1");

    juce::Image image(juce::Image::ARGB, panel.getWidth(), panel.getHeight(), true);
    juce::Graphics g(image);
    EXPECT_NO_THROW(panel.paintEntireComponent(g, true)) << "no markers: nothing to draw, no crash";

    doc.addMarker(4.0, "Chorus", 0xffE0A33D);
    EXPECT_NO_THROW(panel.paintEntireComponent(g, true));

    // Off-screen markers are culled rather than clamped to an edge.
    doc.addMarker(100000.0, "Far away", 0xffE0A33D);
    EXPECT_NO_THROW(panel.paintEntireComponent(g, true));

    panel.setTimelineDoc(nullptr);
    EXPECT_NO_THROW(panel.paintEntireComponent(g, true)) << "no doc: the stem pass is skipped";
}

// Toggling snap OFF must not change which grid lines are DRAWN — it turns MAGNETISM off. The lanes
// grid reads divisionBeatsRaw() for exactly that reason; divisionBeats() collapses to 0 with the
// switch off and used to make the subdivision lines vanish under the user.
TEST(TimelinePanelGridDrawingTest, SnapOffKeepsTheSubdivisionLinesDrawn) {
    synth::ui::TimelinePanelComponent panel;
    auto& view = panel.getViewState();
    view.pixelsPerBeat = 120.0; // wide enough for a 1/8 subdivision to clear the readability guard
    view.setSnap(synth::ui::TimelineViewState::Snap::Eighth);
    panel.setSize(1200, 220);

    // The two helpers the paint site chooses between, at the same division.
    ASSERT_TRUE(view.snapEnabled);
    const double drawn = view.divisionBeatsRaw(4.0);
    EXPECT_DOUBLE_EQ(view.divisionBeats(4.0), drawn) << "with snap on the two agree";

    view.snapEnabled = false;
    EXPECT_DOUBLE_EQ(view.divisionBeatsRaw(4.0), drawn) << "the CHOSEN division is unchanged by the switch";
    EXPECT_DOUBLE_EQ(view.divisionBeats(4.0), 0.0) << "...while magnetism collapses to no grid";

    // Paint both ways: the point is that it does not throw and the panel keeps drawing. The pure
    // assertion above is what actually pins which helper the paint site must use.
    juce::Image image(juce::Image::ARGB, panel.getWidth(), panel.getHeight(), true);
    juce::Graphics g(image);
    EXPECT_NO_THROW(panel.paintEntireComponent(g, true));
    view.snapEnabled = true;
    EXPECT_NO_THROW(panel.paintEntireComponent(g, true));
}

// The panel is what repaints the strip when a marker changes — there is no timer under it.
TEST(TimelineRulerMarkerTest, PanelPaintsMarkersWithNoTimerAndTheAddMarkerMenuEntryWorks) {
    synth::TimelineDoc doc;
    AppUndoManager undo;
    synth::TransportService transport;
    transport.prepare(48000.0, 512);

    synth::ui::TimelinePanelComponent panel;
    panel.setTimelineDoc(&doc);
    panel.setUndoManager(&undo);
    panel.setTransport(&transport);
    panel.setSize(1200, 220);

    // The ruler got the doc through the panel's own forward.
    EXPECT_EQ(panel.getRuler().getTimelineDoc(), &doc);

    transport.locateBeat(6.0);
    transport.tick(512);
    ASSERT_DOUBLE_EQ(transport.getPositionSnapshot().ppq, 6.0);

    const auto added = panel.addMarkerAtPlayhead();
    ASSERT_TRUE(added.isValid());
    ASSERT_EQ(doc.getMarkers().size(), 1u);
    EXPECT_DOUBLE_EQ(doc.getMarkers().front().beat, 6.0) << "at the playhead, unsnapped";
    EXPECT_EQ(doc.getMarkers().front().text, "Marker 1");
    EXPECT_FALSE(panel.getRuler().buildMarkerFlags().empty());

    // One undo step, and the whole "+ Track" menu route lands on the same method.
    ASSERT_TRUE(undo.canUndo());
    undo.undo();
    EXPECT_TRUE(doc.getMarkers().empty());

    panel.applyAddTrackMenuChoice(synth::ui::TimelinePanelComponent::kAddMarkerMenuId);
    ASSERT_EQ(doc.getMarkers().size(), 1u);
    // ...and a second one counts up from what is already there.
    panel.applyAddTrackMenuChoice(synth::ui::TimelinePanelComponent::kAddMarkerMenuId);
    ASSERT_EQ(doc.getMarkers().size(), 2u);
    EXPECT_EQ(doc.getMarkers()[1].text, "Marker 2");

    // Paint smoke: the flags are drawn by the ruler's own paint(), no timer involved.
    juce::Image image(juce::Image::ARGB, panel.getWidth(), panel.getHeight(), true);
    juce::Graphics g(image);
    EXPECT_NO_THROW(panel.paintEntireComponent(g, true));

    panel.setTimelineDoc(nullptr);
    panel.setUndoManager(nullptr);
    panel.setTransport(nullptr);
}

// Adding a marker with no doc (or a doc at kMaxMarkers) is a refusal, not a crash.
TEST(TimelineRulerMarkerTest, AddMarkerAtPlayheadRefusesCleanly) {
    synth::ui::TimelinePanelComponent panel;
    panel.setSize(1200, 220);
    EXPECT_FALSE(panel.addMarkerAtPlayhead().isValid()) << "no doc: nothing to add to";

    synth::TimelineDoc doc;
    panel.setTimelineDoc(&doc);
    // With no transport the playhead is beat 0.
    const auto first = panel.addMarkerAtPlayhead();
    ASSERT_TRUE(first.isValid());
    EXPECT_DOUBLE_EQ(doc.getMarkers().front().beat, 0.0);

    while ((int)doc.getMarkers().size() < synth::TimelineDoc::kMaxMarkers)
        ASSERT_TRUE(doc.addMarker(1.0, "filler", 0xff000000).isValid());
    EXPECT_FALSE(panel.addMarkerAtPlayhead().isValid()) << "at the cap: refused";

    panel.setTimelineDoc(nullptr);
}

// Wheel bindings (Cubase-style): plain vertical wheel scrolls the TRACK rows, Shift+wheel (or a
// trackpad's own deltaX) scrolls horizontally, Cmd+wheel zooms horizontally around the cursor
// (keeping the beat under it fixed), Cmd+Shift+wheel zooms the row height.
