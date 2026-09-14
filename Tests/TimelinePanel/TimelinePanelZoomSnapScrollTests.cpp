// TimelinePanelZoomSnapScrollTests.cpp
//
// Wheel scroll/zoom, the snap combo + setSnapValue/cycleSnapValue API (including the fine
// 1/32-1/128 divisions), zoomTimelineHorizontal/zoomTimelineVertical, wheel-inversion policy
// and zoom-scroll direction (synth::ui::wheelGestureIsUpward).

#include "../../Source/AI/AIProvider.h"
#include "../../Source/AI/AIStateMapper/AIStateMapper.h"
#include "../../Source/AppUndoManager.h"
#include "../../Source/ProjectBundle.h"
#include "../../Source/Timeline/TimelineDoc.h"
#include "../../Source/Transport/TransportService.h"
#include "../../Source/UI/EdgeAutoScroll.h"
#include "../../Source/UI/Theme/AppLookAndFeel.h"
#include "../../Source/UI/Theme/BuiltInThemes.h"
#include "../../Source/UI/TimelinePanelComponent/TimelinePanelComponent.h"
#include "../../Source/UI/TrackColour.h"
#include "../../Source/UserSettings.h"
#include "MainComponent/MainComponent.h"
#include "TimelinePanelTestFixture.h"
#include <algorithm>
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

TEST(TimelinePanelInteractionTest, WheelScrollsAndCmdWheelZooms) {
    synth::TimelineDoc doc;
    synth::ui::TimelinePanelComponent panel;
    panel.setTimelineDoc(&doc);
    panel.setSize(1200, 220);
    // Enough tracks that the rows overflow the visible lanes height and vertical scroll has range.
    for (int i = 0; i < 24; ++i)
        doc.addTrack(synth::TrackKind::Midi, "T" + juce::String(i));

    auto& state = panel.getViewState();
    // Start comfortably away from the firstVisibleBeat >= 0 clamp so the scroll below is visible
    // regardless of which wheel direction maps to which scroll direction.
    state.firstVisibleBeat = 500.0;
    const double ppbBefore = state.pixelsPerBeat;
    const double firstVisibleBefore = state.firstVisibleBeat;

    // Plain vertical wheel: vertical track scroll, horizontal mapping untouched.
    juce::MouseWheelDetails wheel{}; // value-init: deltaX has no default and the router reads it
    wheel.deltaY = -0.5f;            // wheel down -> scroll down into the track list
    panel.mouseWheelMove(makeClickEvent(panel, {400.0f, 100.0f}), wheel);
    EXPECT_DOUBLE_EQ(state.pixelsPerBeat, ppbBefore);
    EXPECT_DOUBLE_EQ(state.firstVisibleBeat, firstVisibleBefore);
    EXPECT_GT(state.trackScrollY, 0.0);
    EXPECT_EQ(panel.getTrackHeaderViewport().getViewPositionY(), (int)std::llround(state.trackScrollY))
        << "the header column follows the shared vertical scroll";

    // Shift+wheel: horizontal scroll, vertical untouched.
    const double trackScrollBefore = state.trackScrollY;
    juce::MouseWheelDetails hWheel{};
    hWheel.deltaY = 0.5f;
    panel.mouseWheelMove(makeClickEvent(panel, {400.0f, 100.0f}, juce::ModifierKeys(juce::ModifierKeys::shiftModifier)),
                         hWheel);
    EXPECT_DOUBLE_EQ(state.pixelsPerBeat, ppbBefore);
    EXPECT_NE(state.firstVisibleBeat, firstVisibleBefore);
    EXPECT_DOUBLE_EQ(state.trackScrollY, trackScrollBefore);

    // Cmd+Shift+wheel: vertical (row height) zoom within its clamps.
    const double scaleBefore = state.rowHeightScale;
    juce::MouseWheelDetails vZoomWheel{};
    vZoomWheel.deltaY = 0.5f;
    panel.mouseWheelMove(
        makeClickEvent(panel, {400.0f, 100.0f},
                       juce::ModifierKeys(juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier)),
        vZoomWheel);
    EXPECT_GT(state.rowHeightScale, scaleBefore);
    EXPECT_LE(state.rowHeightScale, synth::ui::TimelineViewState::kMaxRowHeightScale);

    const juce::Point<float> cursor(300.0f, 12.0f);
    // The ruler shares TimelineViewState's x==0 origin, so reproject into its coordinate space to
    // compute the same anchor beat the panel's mouseWheelMove uses internally.
    const double anchorXInRuler = (double)cursor.x - (double)panel.getRuler().getX();
    const double anchorBeatBefore = state.xToBeat(anchorXInRuler);
    const double ppbBeforeZoom = state.pixelsPerBeat;

    juce::MouseWheelDetails zoomWheel{};
    zoomWheel.deltaY = 0.5f;
    panel.mouseWheelMove(makeClickEvent(panel, cursor, juce::ModifierKeys(juce::ModifierKeys::commandModifier)),
                         zoomWheel);

    EXPECT_NE(state.pixelsPerBeat, ppbBeforeZoom);
    EXPECT_GE(state.pixelsPerBeat, synth::ui::TimelineViewState::kMinPixelsPerBeat);
    EXPECT_LE(state.pixelsPerBeat, synth::ui::TimelineViewState::kMaxPixelsPerBeat);
    EXPECT_NEAR(state.xToBeat(anchorXInRuler), anchorBeatBefore, 1e-6);
}

// The snap combo's choice persists to ApplicationProperties under "timelineSnap" and is restored
// by a freshly-constructed panel reading the same (isolated, test-only) properties file.
TEST(TimelinePanelSnapComboTest, SnapChoicePersists) {
    juce::PropertiesFile::Options opts;
    opts.applicationName = "Agent Synth Timeline Snap Test";
    opts.folderName = "Agent Synth Timeline Snap Test";
    opts.filenameSuffix = "settings";
    opts.osxLibrarySubFolder = "Application Support";
    opts.storageFormat = juce::PropertiesFile::storeAsXML;

    // Hermetic regardless of a previous run: start from no file at all.
    {
        juce::ApplicationProperties initial;
        initial.setStorageParameters(opts);
        if (auto* s = initial.getUserSettings())
            s->getFile().deleteFile();
    }

    juce::ApplicationProperties props;
    props.setStorageParameters(opts);

    synth::ui::TimelinePanelComponent panel;
    panel.setApplicationProperties(&props);
    ASSERT_EQ(panel.getViewState().snap, synth::ui::TimelineViewState::Snap::Quarter); // documented default

    panel.getSnapCombo().setSelectedId(7, juce::sendNotificationSync); // "1/16" -> Sixteenth
    EXPECT_EQ(panel.getViewState().snap, synth::ui::TimelineViewState::Snap::Sixteenth);

    juce::ApplicationProperties props2;
    props2.setStorageParameters(opts);
    synth::ui::TimelinePanelComponent panel2;
    panel2.setApplicationProperties(&props2);
    EXPECT_EQ(panel2.getViewState().snap, synth::ui::TimelineViewState::Snap::Sixteenth);
    EXPECT_EQ(panel2.getSnapCombo().getSelectedId(), 7);

    if (auto* s = props.getUserSettings())
        s->getFile().deleteFile();
}

// ---- setSnapValue / cycleSnapValue: the view-state verbs behind the snap shortcuts ----

// setSnapValue is the ONE writer the combo, the shortcut layer and cycleSnapValue share: it moves
// the view state, mirrors the combo, re-arms the master switch (same meaning a combo pick has) and
// persists through the existing persistSnapChoice() path.
TEST(TimelinePanelSnapApiTest, SetSnapValueSyncsTheComboAndPersistsThroughTheExistingPath) {
    using Snap = synth::ui::TimelineViewState::Snap;

    juce::PropertiesFile::Options opts;
    opts.applicationName = "Agent Synth Timeline Snap Api Test";
    opts.folderName = "Agent Synth Timeline Snap Api Test";
    opts.filenameSuffix = "settings";
    opts.osxLibrarySubFolder = "Application Support";
    opts.storageFormat = juce::PropertiesFile::storeAsXML;

    // Hermetic regardless of a previous run — same idiom as SnapChoicePersists above.
    {
        juce::ApplicationProperties initial;
        initial.setStorageParameters(opts);
        if (auto* s = initial.getUserSettings())
            s->getFile().deleteFile();
    }

    juce::ApplicationProperties props;
    props.setStorageParameters(opts);

    synth::ui::TimelinePanelComponent panel;
    panel.setApplicationProperties(&props);
    ASSERT_EQ(panel.getViewState().snap, Snap::Quarter); // documented default, freshly-deleted file

    EXPECT_TRUE(panel.setSnapValue(Snap::Eighth));
    EXPECT_EQ(panel.getViewState().snap, Snap::Eighth);
    EXPECT_EQ(panel.getSnapCombo().getSelectedId(), (int)Snap::Eighth + 1) << "the combo mirrors the view state";
    EXPECT_TRUE(panel.getViewState().snapEnabled);
    ASSERT_NE(props.getUserSettings(), nullptr);
    EXPECT_EQ(props.getUserSettings()->getIntValue("timelineSnap", -1), (int)Snap::Eighth);

    // Turn magnetism off (the Q button — the shared switch), then re-pick the SAME division: no
    // change is reported, but it still re-arms, exactly like re-picking from the combo.
    panel.getSnapToggleButton().onClick();
    ASSERT_FALSE(panel.getViewState().snapEnabled);
    EXPECT_FALSE(panel.setSnapValue(Snap::Eighth));
    EXPECT_TRUE(panel.getViewState().snapEnabled);

    // Reset the persisted keys so the next run (and every other test) starts from nothing.
    if (auto* s = props.getUserSettings())
        s->getFile().deleteFile();
}

// The cycle walks Bar -> 1 -> 1/2 -> 1/4 -> 1/8 -> 1/16 -> 1/32 -> 1/64 -> 1/128 and CLAMPS at both
// ends: it never wraps and never lands on Off (that is the Q key's job). The finest stop moved from
// Sixteenth to HundredTwentyEighth when the three finer divisions were added — see
// TimelineViewState::Snap's class comment for why they were appended after Sixteenth rather than
// inserted in note-value order.
TEST(TimelinePanelSnapApiTest, CycleWalksTheMusicalDivisionsAndClampsAtBothEnds) {
    using Snap = synth::ui::TimelineViewState::Snap;
    synth::ui::TimelinePanelComponent panel; // no ApplicationProperties: nothing reads or writes settings
    panel.getViewState().setSnap(Snap::Quarter);
    panel.getViewState().snapEnabled = true;

    // Coarser, down to the Bar end, then one more press that must change nothing.
    EXPECT_TRUE(panel.cycleSnapValue(-1));
    EXPECT_EQ(panel.getViewState().snap, Snap::Half);
    EXPECT_TRUE(panel.cycleSnapValue(-1));
    EXPECT_EQ(panel.getViewState().snap, Snap::Whole);
    EXPECT_TRUE(panel.cycleSnapValue(-1));
    EXPECT_EQ(panel.getViewState().snap, Snap::Bar);
    EXPECT_FALSE(panel.cycleSnapValue(-1)) << "clamped at Bar — no wrap, and never Off";
    EXPECT_EQ(panel.getViewState().snap, Snap::Bar);

    // Finer, all the way up the list, now through the three new fine divisions.
    for (auto expected : {Snap::Whole, Snap::Half, Snap::Quarter, Snap::Eighth, Snap::Sixteenth, Snap::ThirtySecond,
                          Snap::SixtyFourth, Snap::HundredTwentyEighth}) {
        EXPECT_TRUE(panel.cycleSnapValue(1));
        EXPECT_EQ(panel.getViewState().snap, expected);
    }
    EXPECT_FALSE(panel.cycleSnapValue(1)) << "clamped at 1/128 — a held key parks here";
    EXPECT_EQ(panel.getViewState().snap, Snap::HundredTwentyEighth);
    EXPECT_EQ(panel.getSnapCombo().getSelectedId(), (int)Snap::HundredTwentyEighth + 1);

    EXPECT_FALSE(panel.cycleSnapValue(0)) << "no direction, no move";
    EXPECT_EQ(panel.getViewState().snap, Snap::HundredTwentyEighth);
}

// From Off, BOTH directions re-enter at the last division the user actually chose — with the view
// state's default as the fallback when nothing was ever chosen.
TEST(TimelinePanelSnapApiTest, CyclingFromOffReEntersAtTheLastMusicalDivision) {
    using Snap = synth::ui::TimelineViewState::Snap;
    synth::ui::TimelinePanelComponent panel;
    panel.getViewState().snapEnabled = true;

    ASSERT_TRUE(panel.setSnapValue(Snap::Eighth)); // the "last musical" value from here on
    ASSERT_TRUE(panel.setSnapValue(Snap::Off));
    EXPECT_TRUE(panel.cycleSnapValue(1));
    EXPECT_EQ(panel.getViewState().snap, Snap::Eighth) << "finer-from-Off resumes where the user was";

    ASSERT_TRUE(panel.setSnapValue(Snap::Off));
    EXPECT_TRUE(panel.cycleSnapValue(-1));
    EXPECT_EQ(panel.getViewState().snap, Snap::Eighth) << "coarser-from-Off follows the same one rule";

    // A panel that never had a musical division picked falls back to the view state's default.
    synth::ui::TimelinePanelComponent fresh;
    fresh.getViewState().snap = Snap::Off; // straight assignment: never went through setSnap()
    EXPECT_TRUE(fresh.cycleSnapValue(1));
    EXPECT_EQ(fresh.getViewState().snap, Snap::Quarter);
}

// ---- The three finest divisions (1/32, 1/64, 1/128) added alongside Sixteenth ----

// A note value is a fraction of a whole note (4 beats — see TimelineViewState::divisionBeatsRaw's
// header comment), so 1/32 is an eighth of a beat, 1/64 a sixteenth, 1/128 a thirty-second — each
// half the one before it, same as Eighth->Sixteenth already was.
TEST(TimelinePanelSnapApiTest, DivisionBeatsForTheThreeNewFineSnaps) {
    using Snap = synth::ui::TimelineViewState::Snap;
    synth::ui::TimelinePanelComponent panel;
    auto& state = panel.getViewState();

    state.snap = Snap::ThirtySecond;
    EXPECT_DOUBLE_EQ(state.divisionBeats(4.0), 0.125);
    EXPECT_DOUBLE_EQ(state.divisionBeatsRaw(4.0), 0.125);

    state.snap = Snap::SixtyFourth;
    EXPECT_DOUBLE_EQ(state.divisionBeats(4.0), 0.0625);
    EXPECT_DOUBLE_EQ(state.divisionBeatsRaw(4.0), 0.0625);

    state.snap = Snap::HundredTwentyEighth;
    EXPECT_DOUBLE_EQ(state.divisionBeats(4.0), 0.03125);
    EXPECT_DOUBLE_EQ(state.divisionBeatsRaw(4.0), 0.03125);

    // beatsPerBar is irrelevant below Snap::Bar — same contract every other musical division has.
    EXPECT_DOUBLE_EQ(state.divisionBeats(3.0), 0.03125);
}

// The combo's ids follow the (int)snap + 1 convention all the way through the new entries, and
// picking one from the combo reaches the view state through the same setSnapValue path as every
// other division.
TEST(TimelinePanelSnapComboTest, ComboShowsAndSetsTheThreeNewFineDivisions) {
    using Snap = synth::ui::TimelineViewState::Snap;
    synth::ui::TimelinePanelComponent panel;
    auto& combo = panel.getSnapCombo();

    EXPECT_EQ(combo.getItemText(combo.indexOfItemId((int)Snap::ThirtySecond + 1)), "1/32");
    EXPECT_EQ(combo.getItemText(combo.indexOfItemId((int)Snap::SixtyFourth + 1)), "1/64");
    EXPECT_EQ(combo.getItemText(combo.indexOfItemId((int)Snap::HundredTwentyEighth + 1)), "1/128");

    combo.setSelectedId((int)Snap::SixtyFourth + 1, juce::sendNotificationSync);
    EXPECT_EQ(panel.getViewState().snap, Snap::SixtyFourth);

    EXPECT_TRUE(panel.setSnapValue(Snap::HundredTwentyEighth));
    EXPECT_EQ(combo.getSelectedId(), (int)Snap::HundredTwentyEighth + 1) << "the combo mirrors the view state";
}

// Same persistence path as SnapChoicePersists above, exercised at one of the new fine divisions —
// the jlimit clamp in setApplicationProperties() had to move to HundredTwentyEighth alongside the
// combo/cycle bounds, and this is what proves a restored fine value survives it rather than being
// silently clamped back down to Sixteenth.
TEST(TimelinePanelSnapComboTest, AFineSnapPersistsAndRestoresThroughTheExistingPath) {
    using Snap = synth::ui::TimelineViewState::Snap;

    juce::PropertiesFile::Options opts;
    opts.applicationName = "Agent Synth Timeline Fine Snap Test";
    opts.folderName = "Agent Synth Timeline Fine Snap Test";
    opts.filenameSuffix = "settings";
    opts.osxLibrarySubFolder = "Application Support";
    opts.storageFormat = juce::PropertiesFile::storeAsXML;

    // Hermetic regardless of a previous run — same idiom as SnapChoicePersists above.
    {
        juce::ApplicationProperties initial;
        initial.setStorageParameters(opts);
        if (auto* s = initial.getUserSettings())
            s->getFile().deleteFile();
    }

    juce::ApplicationProperties props;
    props.setStorageParameters(opts);

    synth::ui::TimelinePanelComponent panel;
    panel.setApplicationProperties(&props);
    ASSERT_TRUE(panel.setSnapValue(Snap::HundredTwentyEighth));

    synth::ui::TimelinePanelComponent panel2;
    panel2.setApplicationProperties(&props);
    EXPECT_EQ(panel2.getViewState().snap, Snap::HundredTwentyEighth);
    EXPECT_EQ(panel2.getSnapCombo().getSelectedId(), (int)Snap::HundredTwentyEighth + 1);

    // Reset the persisted keys, per this file's pattern, so no other test inherits them.
    if (auto* s = props.getUserSettings())
        s->getFile().deleteFile();
}

// ---- zoomTimelineHorizontal / zoomTimelineVertical: the same paths the wheel/pinch use ----

TEST(TimelinePanelZoomApiTest, HorizontalZoomKeepsTheVisibleCentreBeatAndClamps) {
    using View = synth::ui::TimelineViewState;
    synth::ui::TimelinePanelComponent panel;
    panel.setSize(1200, 220);

    auto& state = panel.getViewState();
    state.pixelsPerBeat = 24.0;
    state.firstVisibleBeat = 100.0;

    // The panel anchors a keyboard zoom on the middle of the ruler strip, whose local x == 0 is the
    // view state's own origin.
    const double centreX = (double)panel.getRuler().getWidth() * 0.5;
    const double centreBeat = state.xToBeat(centreX);

    panel.zoomTimelineHorizontal(2.0);
    EXPECT_DOUBLE_EQ(state.pixelsPerBeat, 48.0);
    EXPECT_NEAR(state.xToBeat(centreX), centreBeat, 1e-6) << "the centre beat stays under the centre pixel";

    for (int i = 0; i < 20; ++i)
        panel.zoomTimelineHorizontal(2.0);
    EXPECT_DOUBLE_EQ(state.pixelsPerBeat, View::kMaxPixelsPerBeat);

    for (int i = 0; i < 40; ++i)
        panel.zoomTimelineHorizontal(0.5);
    EXPECT_DOUBLE_EQ(state.pixelsPerBeat, View::kMinPixelsPerBeat);

    // Garbage factors are ignored rather than propagated into the mapping.
    const double settled = state.pixelsPerBeat;
    panel.zoomTimelineHorizontal(0.0);
    panel.zoomTimelineHorizontal(-2.0);
    EXPECT_DOUBLE_EQ(state.pixelsPerBeat, settled);
}

TEST(TimelinePanelZoomApiTest, VerticalZoomScalesTheRowHeightWithinItsClampsAndRelaysTheHeaders) {
    using View = synth::ui::TimelineViewState;
    synth::TimelineDoc doc;
    synth::ui::TimelinePanelComponent panel;
    panel.setTimelineDoc(&doc);
    panel.setSize(1200, 220);
    for (int i = 0; i < 24; ++i)
        doc.addTrack(synth::TrackKind::Midi, "T" + juce::String(i));

    auto& state = panel.getViewState();
    state.rowHeightScale = 1.0;
    state.trackScrollY = 0.0;
    ASSERT_NE(panel.getTrackHeaderAt(0), nullptr);
    const int rowHeightBefore = panel.getTrackHeaderAt(0)->getHeight();

    panel.zoomTimelineVertical(1.5);
    EXPECT_GT(state.rowHeightScale, 1.0);
    EXPECT_GT(panel.getTrackHeaderAt(0)->getHeight(), rowHeightBefore)
        << "the header column is relaid out by the same path the wheel zoom uses";

    for (int i = 0; i < 10; ++i)
        panel.zoomTimelineVertical(2.0);
    EXPECT_DOUBLE_EQ(state.rowHeightScale, View::kMaxRowHeightScale);

    for (int i = 0; i < 20; ++i)
        panel.zoomTimelineVertical(0.5);
    EXPECT_DOUBLE_EQ(state.rowHeightScale, View::kMinRowHeightScale);

    const double settled = state.rowHeightScale;
    panel.zoomTimelineVertical(0.0);
    panel.zoomTimelineVertical(-2.0);
    EXPECT_DOUBLE_EQ(state.rowHeightScale, settled);
}

// ---- Wheel policy (synth::ui::ScrollPolicy) ----

// Both ZOOM branches are chosen by their modifiers, so they must read the dominant axis: macOS
// folds Shift+wheel into deltaX, which used to leave Cmd+Shift+wheel reading deltaY == 0 and doing
// nothing at all.
TEST(TimelinePanelInteractionTest, ModifierZoomSurvivesTheShiftAxisSwap) {
    synth::TimelineDoc doc;
    synth::ui::TimelinePanelComponent panel;
    panel.setTimelineDoc(&doc);
    panel.setSize(1200, 220);
    for (int i = 0; i < 24; ++i)
        doc.addTrack(synth::TrackKind::Midi, "T" + juce::String(i));

    auto& state = panel.getViewState();
    state.pixelsPerBeat = 24.0;
    state.firstVisibleBeat = 500.0;
    state.rowHeightScale = 1.0;

    // Cmd + a deltaX-ONLY wheel: horizontal zoom still happens.
    juce::MouseWheelDetails xOnly{};
    xOnly.deltaX = 0.5f;
    panel.mouseWheelMove(
        makeClickEvent(panel, {300.0f, 12.0f}, juce::ModifierKeys(juce::ModifierKeys::commandModifier)), xOnly);
    EXPECT_GT(state.pixelsPerBeat, 24.0);

    // Cmd+Shift + a deltaX-only wheel: the row-height zoom that the axis swap used to kill.
    panel.mouseWheelMove(
        makeClickEvent(panel, {300.0f, 100.0f},
                       juce::ModifierKeys(juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier)),
        xOnly);
    EXPECT_GT(state.rowHeightScale, 1.0);
}

// Plain scroll follows juce::Viewport's sign convention (origin -= delta) by default, and
// setScrollInverted(true) flips both axes by exactly the same amount.
TEST(TimelinePanelInteractionTest, ScrollInversionFlipsBothAxesAroundTheViewportConvention) {
    synth::TimelineDoc doc;
    synth::ui::TimelinePanelComponent panel;
    panel.setTimelineDoc(&doc);
    panel.setSize(1200, 220);
    for (int i = 0; i < 24; ++i)
        doc.addTrack(synth::TrackKind::Midi, "T" + juce::String(i));

    auto& state = panel.getViewState();
    state.pixelsPerBeat = 24.0;
    ASSERT_FALSE(panel.isScrollInverted()) << "natural is the default";

    juce::MouseWheelDetails wheel{};
    wheel.deltaY = 0.5f;
    const auto shift = juce::ModifierKeys(juce::ModifierKeys::shiftModifier);

    // Horizontal (Shift+wheel). Start well clear of the firstVisibleBeat >= 0 clamp so both
    // directions have room.
    state.firstVisibleBeat = 500.0;
    panel.mouseWheelMove(makeClickEvent(panel, {400.0f, 100.0f}, shift), wheel);
    const double naturalBeat = state.firstVisibleBeat;
    EXPECT_LT(naturalBeat, 500.0);

    state.firstVisibleBeat = 500.0;
    panel.setScrollInverted(true);
    panel.mouseWheelMove(makeClickEvent(panel, {400.0f, 100.0f}, shift), wheel);
    EXPECT_GT(state.firstVisibleBeat, 500.0);
    EXPECT_NEAR(state.firstVisibleBeat - 500.0, 500.0 - naturalBeat, 1e-9) << "same distance, opposite sign";

    // Vertical (plain wheel). Park mid-range so neither direction is swallowed by a clamp.
    panel.setScrollInverted(false);
    state.trackScrollY = 200.0;
    panel.mouseWheelMove(makeClickEvent(panel, {400.0f, 100.0f}), wheel);
    const double naturalY = state.trackScrollY;
    EXPECT_LT(naturalY, 200.0);

    state.trackScrollY = 200.0;
    panel.setScrollInverted(true);
    panel.mouseWheelMove(makeClickEvent(panel, {400.0f, 100.0f}), wheel);
    EXPECT_GT(state.trackScrollY, 200.0);
    EXPECT_NEAR(state.trackScrollY - 200.0, 200.0 - naturalY, 1e-9);
}

// ---- Zoom-scroll direction (synth::ui::wheelGestureIsUpward) ----
//
// UP ZOOMS IN by default, and that must hold under BOTH natural-scrolling conventions: JUCE's
// isReversed flag flips which raw delta sign is "up" (see ScrollPolicy.h's wheelGestureIsUpward
// comment), so a raw-delta-sign zoom — what mouseWheelMove did before this test was added — would
// silently reverse itself depending on the OS's natural-scrolling setting. None of the OLDER
// zoom-wheel tests above (WheelScrollsAndCmdWheelZooms, ModifierZoomSurvivesTheShiftAxisSwap) had
// to change for this: they only ever drive a positive deltaY with isReversed left at its default
// (false), which is "up" under both the old raw-sign reading and the new gesture-based one, and
// none of them assert a direction anyway — WheelScrollsAndCmdWheelZooms asserts the anchor
// invariant, ModifierZoomSurvivesTheShiftAxisSwap asserts the axis-swap guard fires at all.
TEST(TimelinePanelInteractionTest, ZoomUpZoomsInOnBothNaturalScrollConventions) {
    synth::TimelineDoc doc;
    synth::ui::TimelinePanelComponent panel;
    panel.setTimelineDoc(&doc);
    panel.setSize(1200, 220);
    for (int i = 0; i < 24; ++i)
        doc.addTrack(synth::TrackKind::Midi, "T" + juce::String(i));

    auto& state = panel.getViewState();
    ASSERT_FALSE(panel.isZoomScrollInverted()) << "up-zooms-in is the default";
    const auto cmd = juce::ModifierKeys(juce::ModifierKeys::commandModifier);
    const auto cmdShift = juce::ModifierKeys(juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier);
    const juce::Point<float> cursor(300.0f, 12.0f);

    // isReversed == false: physically "up" is a POSITIVE delta.
    state.pixelsPerBeat = 24.0;
    state.rowHeightScale = 1.0;
    juce::MouseWheelDetails naturalUp{};
    naturalUp.deltaY = 0.5f; // isReversed defaults false
    panel.mouseWheelMove(makeClickEvent(panel, cursor, cmd), naturalUp);
    EXPECT_GT(state.pixelsPerBeat, 24.0) << "up zooms IN horizontally";
    panel.mouseWheelMove(makeClickEvent(panel, cursor, cmdShift), naturalUp);
    EXPECT_GT(state.rowHeightScale, 1.0) << "up zooms IN vertically";

    // isReversed == true: the SAME physical gesture now arrives as a NEGATIVE delta, so the raw
    // sign flipped — but the outcome must not: it is still an upward gesture, so it still zooms IN.
    state.pixelsPerBeat = 24.0;
    state.rowHeightScale = 1.0;
    juce::MouseWheelDetails reversedUp{};
    reversedUp.deltaY = -0.5f;
    reversedUp.isReversed = true;
    panel.mouseWheelMove(makeClickEvent(panel, cursor, cmd), reversedUp);
    EXPECT_GT(state.pixelsPerBeat, 24.0) << "the same physical 'up' zooms IN regardless of isReversed";
    panel.mouseWheelMove(makeClickEvent(panel, cursor, cmdShift), reversedUp);
    EXPECT_GT(state.rowHeightScale, 1.0);
}

// setZoomScrollInverted(true) flips the sense of BOTH zoom axes at once — one preference behind
// both Cmd branches, not two — while leaving the magnitude/sensitivity curve (and the unrelated
// setScrollInverted preference) alone.
TEST(TimelinePanelInteractionTest, SetZoomScrollInvertedFlipsBothZoomAxes) {
    synth::TimelineDoc doc;
    synth::ui::TimelinePanelComponent panel;
    panel.setTimelineDoc(&doc);
    panel.setSize(1200, 220);
    for (int i = 0; i < 24; ++i)
        doc.addTrack(synth::TrackKind::Midi, "T" + juce::String(i));

    auto& state = panel.getViewState();
    const auto cmd = juce::ModifierKeys(juce::ModifierKeys::commandModifier);
    const auto cmdShift = juce::ModifierKeys(juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier);
    const juce::Point<float> cursor(300.0f, 12.0f);
    juce::MouseWheelDetails up{};
    up.deltaY = 0.5f;

    panel.setZoomScrollInverted(true);
    EXPECT_TRUE(panel.isZoomScrollInverted());

    state.pixelsPerBeat = 24.0;
    state.rowHeightScale = 1.0;
    panel.mouseWheelMove(makeClickEvent(panel, cursor, cmd), up);
    EXPECT_LT(state.pixelsPerBeat, 24.0) << "inverted: up now zooms OUT horizontally";
    panel.mouseWheelMove(makeClickEvent(panel, cursor, cmdShift), up);
    EXPECT_LT(state.rowHeightScale, 1.0) << "inverted: up now zooms OUT vertically";

    // A separate preference — flipping zoom must not have touched plain-scroll direction.
    EXPECT_FALSE(panel.isScrollInverted());
}

// The panel forwards BOTH scroll-direction preferences to the piano roll: it runs its OWN
// plain-scroll and Cmd-wheel-zoom branches (PianoRollComponent::mouseWheelMove), so a preference
// set from the panel chrome (or Preferences) has to reach it directly — TimelineViewState, shared
// between the two surfaces, carries no scroll/zoom preferences to piggyback on.
TEST(TimelinePanelInteractionTest, ScrollAndZoomInversionForwardToThePianoRoll) {
    synth::ui::TimelinePanelComponent panel;
    ASSERT_FALSE(panel.getPianoRoll().isScrollInverted());
    ASSERT_FALSE(panel.getPianoRoll().isZoomScrollInverted());

    panel.setScrollInverted(true);
    EXPECT_TRUE(panel.getPianoRoll().isScrollInverted());

    panel.setZoomScrollInverted(true);
    EXPECT_TRUE(panel.getPianoRoll().isZoomScrollInverted());

    panel.setScrollInverted(false);
    panel.setZoomScrollInverted(false);
    EXPECT_FALSE(panel.getPianoRoll().isScrollInverted());
    EXPECT_FALSE(panel.getPianoRoll().isZoomScrollInverted());
}
