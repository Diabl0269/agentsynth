// TimelinePanelAuthoringGestureTests.cpp
//
// Authoring gestures reaching the panel: a double-click on empty MIDI lane space creates a
// clip and opens the piano roll on it; panel-scoped keys (J snap toggle, L loop toggle, P loop
// the selection) and the ruler's piano-roll mapping override.

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
// 6. Authoring gestures reaching the panel. The lane-area half of the gesture (snapping, length,
//    one undo step) lives in Tests/UI/Timeline/TimelineClipLane/TimelineClipLaneAuthoringTests.cpp; the import half in
//    Tests/AssetManagerTests.cpp.
// ============================================================================

// The panel's onClipDoubleClicked -> openPianoRoll wiring has to cover the clip a double-click on
// EMPTY MIDI lane space just created, not only an existing one being reopened.
TEST(TimelinePanelComponentTest, DoubleClickOnEmptyMidiLaneCreatesAClipAndOpensThePianoRoll) {
    synth::TimelineDoc doc;
    synth::ui::TimelinePanelComponent panel;
    panel.setTimelineDoc(&doc);
    panel.setSize(1200, 320);
    panel.getViewState().pixelsPerBeat = 40.0;
    panel.getViewState().firstVisibleBeat = 0.0;
    panel.getViewState().snap = synth::ui::TimelineViewState::Snap::Bar;

    const auto trackId = doc.addTrack(synth::TrackKind::Midi, "Track 1");
    ASSERT_TRUE(trackId.isValid());
    ASSERT_FALSE(panel.isPianoRollOpen());

    auto& lane = panel.getClipLaneArea();
    lane.mouseDoubleClick(makeClickEvent(lane, {200.0f, (float)(lane.getRowHeight() / 2)}));

    ASSERT_NE(doc.getTrack(trackId), nullptr);
    ASSERT_EQ(doc.getTrack(trackId)->clips.size(), 1u);
    const auto& clip = doc.getTrack(trackId)->clips[0];
    EXPECT_DOUBLE_EQ(clip.startBeat, 4.0) << "x = 200 px -> beat 5.0, floored onto the bar grid";
    EXPECT_DOUBLE_EQ(clip.lengthBeats, 4.0);
    ASSERT_TRUE(panel.isPianoRollOpen()) << "the user lands in the note editor, ready to draw";
    EXPECT_EQ(panel.getPianoRoll().getClipId(), clip.id);
    EXPECT_FALSE(lane.isVisible()) << "the piano roll replaced the lanes, same as reopening a clip";
}

// ============================================================================
// Panel-scoped keys (J = snap toggle, L = loop toggle, P = loop the selection) and the ruler's
// piano-roll mapping override.
// ============================================================================

// J, Cubase's snap key. Q used to do this and now belongs to the roll's quantise, so a bare Q must
// no longer reach the snap switch at all.
TEST(TimelinePanelComponentTest, JKeyTogglesSnapEnabledAndQNoLongerDoes) {
    synth::ui::TimelinePanelComponent panel;
    panel.setSize(1000, 300);
    ASSERT_TRUE(panel.getViewState().snapEnabled);

    EXPECT_TRUE(panel.keyPressed(juce::KeyPress('j')));
    EXPECT_FALSE(panel.getViewState().snapEnabled);
    EXPECT_TRUE(panel.keyPressed(juce::KeyPress('j')));
    EXPECT_TRUE(panel.getViewState().snapEnabled);

    EXPECT_FALSE(panel.keyPressed(juce::KeyPress('q'))) << "Q is the roll's quantise now, not snap";
    EXPECT_TRUE(panel.getViewState().snapEnabled) << "and it left the switch alone";
}

// The transport bar's own Snap button flips the same shared switch and mirrors its state — including
// when the flip came from somewhere else (the J key here).
TEST(TimelinePanelComponentTest, SnapToggleButtonFlipsAndMirrorsSnapEnabled) {
    synth::ui::TimelinePanelComponent panel;
    panel.setSize(1000, 300);
    auto& button = panel.getSnapToggleButton();
    ASSERT_TRUE(panel.getViewState().snapEnabled);
    EXPECT_TRUE(button.getToggleState());

    button.onClick();
    EXPECT_FALSE(panel.getViewState().snapEnabled);
    EXPECT_FALSE(button.getToggleState());

    EXPECT_TRUE(panel.keyPressed(juce::KeyPress('j')));
    EXPECT_TRUE(button.getToggleState()) << "a J-key flip re-lights the button";
}

TEST(TimelinePanelComponentTest, PickingADivisionReEnablesSnap) {
    synth::ui::TimelinePanelComponent panel;
    panel.getViewState().snapEnabled = false;
    panel.getSnapCombo().setSelectedId(4, juce::sendNotificationSync); // "1/2"
    EXPECT_TRUE(panel.getViewState().snapEnabled) << "choosing a grid size means 'snap to THIS'";
    EXPECT_EQ(panel.getViewState().snap, synth::ui::TimelineViewState::Snap::Half);
}

TEST(TimelinePanelComponentTest, LKeyTogglesLoopingKeepingBounds) {
    synth::ui::TimelinePanelComponent panel;
    synth::TransportService transport;
    transport.prepare(48000.0, 512);
    ASSERT_TRUE(transport.setLoop(2.0, 6.0, false));
    transport.tick(512);
    panel.setTransport(&transport);

    EXPECT_TRUE(panel.keyPressed(juce::KeyPress('l')));
    transport.tick(512);
    auto snap = transport.getPositionSnapshot();
    EXPECT_TRUE(snap.looping);
    EXPECT_DOUBLE_EQ(snap.loopStartPpq, 2.0);
    EXPECT_DOUBLE_EQ(snap.loopEndPpq, 6.0);

    EXPECT_TRUE(panel.keyPressed(juce::KeyPress('l')));
    transport.tick(512);
    snap = transport.getPositionSnapshot();
    EXPECT_FALSE(snap.looping);
    EXPECT_DOUBLE_EQ(snap.loopStartPpq, 2.0) << "toggling keeps the bounds";
}

TEST(TimelinePanelComponentTest, PKeyLoopsTheSelectedClipsFromPanelScope) {
    synth::TimelineDoc doc;
    synth::ui::TimelinePanelComponent panel;
    synth::TransportService transport;
    transport.prepare(48000.0, 512);
    panel.setTransport(&transport);
    panel.setTimelineDoc(&doc);

    const auto trackId = doc.addTrack(synth::TrackKind::Midi, "Track 1");
    const auto clipA = doc.addClip(trackId, 4.0, 4.0, "A");
    const auto clipB = doc.addClip(trackId, 12.0, 2.0, "B");
    ASSERT_TRUE(clipA.isValid());
    ASSERT_TRUE(clipB.isValid());

    EXPECT_FALSE(panel.keyPressed(juce::KeyPress('p'))) << "no selection -> the key falls through";

    panel.getClipSelection().setSelection({clipA, clipB});
    EXPECT_TRUE(panel.keyPressed(juce::KeyPress('p')));
    transport.tick(512);
    const auto snap = transport.getPositionSnapshot();
    EXPECT_TRUE(snap.looping);
    EXPECT_DOUBLE_EQ(snap.loopStartPpq, 4.0);
    EXPECT_DOUBLE_EQ(snap.loopEndPpq, 14.0) << "the span covers the whole multi-clip selection";
}

// With the "timelineLoopSelectionArms" preference off, P places the locators but leaves the loop
// switch exactly as it was (Cubase's reading); L is then what arms it.
TEST(TimelinePanelComponentTest, PKeyRespectsTheLoopSelectionArmsPreference) {
    juce::PropertiesFile::Options opts;
    opts.applicationName = "Agent Synth Timeline LoopArms Test";
    opts.folderName = "Agent Synth Timeline LoopArms Test";
    opts.filenameSuffix = "settings";
    opts.osxLibrarySubFolder = "Application Support";
    opts.storageFormat = juce::PropertiesFile::storeAsXML;
    juce::ApplicationProperties props;
    props.setStorageParameters(opts);
    if (auto* s = props.getUserSettings())
        s->setValue("timelineLoopSelectionArms", "0");

    synth::TimelineDoc doc;
    synth::ui::TimelinePanelComponent panel;
    synth::TransportService transport;
    transport.prepare(48000.0, 512);
    panel.setTransport(&transport);
    panel.setTimelineDoc(&doc);
    panel.setApplicationProperties(&props);

    const auto trackId = doc.addTrack(synth::TrackKind::Midi, "Track 1");
    const auto clipId = doc.addClip(trackId, 4.0, 4.0, "A");
    ASSERT_TRUE(clipId.isValid());
    panel.getClipSelection().setSelection({clipId});

    EXPECT_TRUE(panel.keyPressed(juce::KeyPress('p')));
    transport.tick(512);
    auto snap = transport.getPositionSnapshot();
    EXPECT_FALSE(snap.looping) << "preference off: locators only, looping untouched";
    EXPECT_DOUBLE_EQ(snap.loopStartPpq, 4.0);
    EXPECT_DOUBLE_EQ(snap.loopEndPpq, 8.0);

    // Already-armed looping stays armed — the preference suppresses the ARM, it never disarms.
    ASSERT_TRUE(transport.setLoop(0.0, 2.0, true));
    transport.tick(512);
    EXPECT_TRUE(panel.keyPressed(juce::KeyPress('p')));
    transport.tick(512);
    snap = transport.getPositionSnapshot();
    EXPECT_TRUE(snap.looping);
    EXPECT_DOUBLE_EQ(snap.loopStartPpq, 4.0);

    if (auto* s = props.getUserSettings())
        s->getFile().deleteFile();
}

TEST(TimelinePanelComponentTest, PKeyWithThePianoRollOpenLoopsTheEditedClip) {
    synth::TimelineDoc doc;
    synth::ui::TimelinePanelComponent panel;
    synth::TransportService transport;
    transport.prepare(48000.0, 512);
    panel.setTransport(&transport);
    panel.setTimelineDoc(&doc);
    panel.setSize(1200, 320);

    const auto trackId = doc.addTrack(synth::TrackKind::Midi, "Track 1");
    const auto clipId = doc.addClip(trackId, 8.0, 4.0, "Clip");
    ASSERT_TRUE(clipId.isValid());
    panel.openPianoRoll(clipId);
    ASSERT_TRUE(panel.isPianoRollOpen());

    EXPECT_TRUE(panel.keyPressed(juce::KeyPress('p')));
    transport.tick(512);
    const auto snap = transport.getPositionSnapshot();
    EXPECT_TRUE(snap.looping);
    EXPECT_DOUBLE_EQ(snap.loopStartPpq, 8.0);
    EXPECT_DOUBLE_EQ(snap.loopEndPpq, 12.0);
}

// While the roll is open the ruler mirrors the ROLL's mapping (offset by the keys gutter), so its
// bar numbers show the edited clip's real timeline position; closing restores the shared mapping.
TEST(TimelinePanelComponentTest, PianoRollOpenInstallsTheRulerMappingOverride) {
    synth::TimelineDoc doc;
    synth::ui::TimelinePanelComponent panel;
    panel.setTimelineDoc(&doc);
    panel.setSize(1200, 320);

    const auto trackId = doc.addTrack(synth::TrackKind::Midi, "Track 1");
    const auto clipId = doc.addClip(trackId, 20.0, 4.0, "Clip"); // bar 6 in 4/4
    ASSERT_TRUE(clipId.isValid());
    ASSERT_FALSE(panel.getRuler().hasMappingOverrideForTest());

    panel.openPianoRoll(clipId);
    ASSERT_TRUE(panel.isPianoRollOpen());
    EXPECT_TRUE(panel.getRuler().hasMappingOverrideForTest());
    // The roll framed the clip with its start at the keys gutter's right edge — beat 20, i.e. the
    // ruler above now starts labelling from bar 6, not bar 1.
    EXPECT_DOUBLE_EQ(panel.getPianoRoll().getFirstVisibleBeat(), 20.0);

    panel.closePianoRoll();
    EXPECT_FALSE(panel.getRuler().hasMappingOverrideForTest());
}

// The override's OFFSET (not just its presence) has to track the scale-assist panel's width: the
// roll's grid starts at leftGutterWidth(), which grows by kScalePanelWidth while that panel is
// open, and PianoRollComponent::setScalePanelVisible's onHorizontalViewChanged fire (relayed by
// TimelinePanelComponent's wiring) is what keeps the ruler's ticks/scrub hit-testing from sitting
// 170px left of the grid whenever the panel is toggled open while the roll is already showing.
TEST(TimelinePanelComponentTest, PianoRollOpenTracksTheScalePanelWidthInTheRulerOverride) {
    synth::TimelineDoc doc;
    synth::ui::TimelinePanelComponent panel;
    panel.setTimelineDoc(&doc);
    panel.setSize(1200, 320);

    const auto trackId = doc.addTrack(synth::TrackKind::Midi, "Track 1");
    const auto clipId = doc.addClip(trackId, 20.0, 4.0, "Clip");
    ASSERT_TRUE(clipId.isValid());

    panel.openPianoRoll(clipId);
    ASSERT_TRUE(panel.isPianoRollOpen());
    ASSERT_TRUE(panel.getRuler().hasMappingOverrideForTest());
    EXPECT_EQ(panel.getRuler().getMappingOverrideOffsetForTest(), synth::ui::PianoRollComponent::kKeysColumnWidth)
        << "panel closed: the offset is the keys gutter alone";

    auto& roll = panel.getPianoRoll();
    ASSERT_FALSE(roll.getScaleAssistPanel().isVisible()) << "closed by default";
    const juce::Point<float> scaleButtonCentre((float)roll.getScaleButtonBounds().getCentreX(),
                                               (float)roll.getScaleButtonBounds().getCentreY());

    // The roll's mouseDown ignores anything but a left-button press, so the synthetic click has to
    // carry the button modifier (unlike the ruler, which takes any press).
    const juce::ModifierKeys leftButton(juce::ModifierKeys::leftButtonModifier);
    roll.mouseDown(makeClickEvent(roll, scaleButtonCentre, leftButton)); // toggle the scale panel ON
    ASSERT_TRUE(roll.getScaleAssistPanel().isVisible());
    EXPECT_EQ(panel.getRuler().getMappingOverrideOffsetForTest(),
              synth::ui::PianoRollComponent::kKeysColumnWidth + synth::ui::PianoRollComponent::kScalePanelWidth)
        << "panel open: the offset grows by kScalePanelWidth";

    roll.mouseDown(makeClickEvent(roll, scaleButtonCentre, leftButton)); // toggle it back OFF
    ASSERT_FALSE(roll.getScaleAssistPanel().isVisible());
    EXPECT_EQ(panel.getRuler().getMappingOverrideOffsetForTest(), synth::ui::PianoRollComponent::kKeysColumnWidth)
        << "closing the panel must restore the narrower offset";
}
