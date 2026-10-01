// PianoRollKeyboardReachTests.cpp -- a keyboard-only user can open the piano roll, reach every control
// in it and come back.
//
// Clip keyboard mode opens the roll with Return and the roll's Escape hands the lanes back; the header
// chips are painted shapes, so each has a transparent button over it that is the Tab stop, the
// screen-reader name and the Return/Space target. Real OS focus needs a native peer, so the tests assert
// the model the focus calls mirror (open roll, visible panes, keyboard clip) and walk the tab order
// with juce::KeyboardFocusTraverser.
#include "PianoRollTestHelpers.h"
#include "Transport/TransportService.h"
#include "UI/PianoRoll/VelocityLane/PianoRollVelocityLane.h"
#include "UI/Timeline/TimelineTrackHeaderComponent.h"
#include <algorithm>

namespace {

using HeaderButtonId = PianoRollComponent::HeaderButtonId;

constexpr HeaderButtonId kAllChips[] = {
    HeaderButtonId::Back,  HeaderButtonId::Quantise,    HeaderButtonId::QuantiseLength, HeaderButtonId::QuantisePitches,
    HeaderButtonId::Scale, HeaderButtonId::ScaleFilter, HeaderButtonId::Velocity,       HeaderButtonId::Humanize};

juce::KeyPress plain(int code) { return juce::KeyPress(code, juce::ModifierKeys::noModifiers, 0); }

// juce::Button redeclares keyPressed as protected; the public Component entry is what the key
// dispatcher calls.
bool pressReturn(juce::Button* chip) {
    return static_cast<juce::Component&>(*chip).keyPressed(plain(juce::KeyPress::returnKey));
}

struct RollWithClip : PianoRollFixture {
    ClipId clip;
    NoteId note;

    RollWithClip() {
        const auto track = doc.addTrack(TrackKind::Midi, "Track 1");
        clip = doc.addClip(track, 0.0, 8.0, "Lead");
        note = doc.addNote(clip, makeNote(1.1, 60, 1.0));
        roll.setVelocityLaneVisible(true, false);
        open(clip);
    }
};

// Return from the track header's Right: clip mode on the one clip, ready to open.
struct PanelWithClip {
    TimelineDoc doc;
    AppUndoManager undo;
    AuditionRecordingHost host;
    synth::TransportService transport;
    synth::ui::TimelinePanelComponent panel;
    ClipId clip;

    PanelWithClip() {
        panel.setSize(1200, 400);
        panel.setTrackHeaderHost(&host);
        panel.setTimelineDoc(&doc);
        panel.setUndoManager(&undo);
        panel.setTransport(&transport);
        const auto track = doc.addTrack(TrackKind::Midi, "Bass");
        clip = doc.addClip(track, 0.0, 4.0, "Bassline");
    }
};

} // namespace

TEST(PianoRollKeyboardReachTest, EveryHeaderChipIsANamedTabStopWithATooltip) {
    RollWithClip f;
    for (const auto id : kAllChips) {
        auto* chip = f.roll.getHeaderChipButtonForTest(id);
        ASSERT_NE(chip, nullptr);
        EXPECT_TRUE(chip->getWantsKeyboardFocus());
        EXPECT_TRUE(chip->getTitle().isNotEmpty());
        EXPECT_TRUE(chip->getTooltip().isNotEmpty()) << chip->getTitle();
        EXPECT_EQ(chip->getBounds(), f.roll.getHeaderChipBounds(id)) << "the button sits exactly over its chip";
        bool clicks = true, childClicks = true;
        chip->getInterceptsMouseClicks(clicks, childClicks);
        EXPECT_FALSE(clicks) << "the roll keeps handling the pointer";
    }
}

TEST(PianoRollKeyboardReachTest, ReturnOnAChipRunsTheChipsAction) {
    RollWithClip f;
    setSnap(f, TimelineViewState::Snap::Quarter);

    EXPECT_DOUBLE_EQ(f.doc.getNote(f.note)->startBeat, 1.1);
    ASSERT_TRUE(pressReturn(f.roll.getHeaderChipButtonForTest(HeaderButtonId::Quantise)));
    EXPECT_DOUBLE_EQ(f.doc.getNote(f.note)->startBeat, 1.0) << "the Quantise chip quantised from the keyboard";

    auto* velocity = f.roll.getHeaderChipButtonForTest(HeaderButtonId::Velocity);
    EXPECT_TRUE(f.roll.isVelocityLaneVisible());
    EXPECT_TRUE(velocity->getToggleState()) << "a toggle chip reports its state";
    pressReturn(velocity);
    EXPECT_FALSE(f.roll.isVelocityLaneVisible());
    EXPECT_FALSE(velocity->getToggleState());

    auto* scale = f.roll.getHeaderChipButtonForTest(HeaderButtonId::Scale);
    EXPECT_FALSE(scale->getToggleState());
    pressReturn(scale);
    EXPECT_TRUE(f.roll.isScalePanelTargetVisibleForTest());
    EXPECT_TRUE(scale->getToggleState());
}

TEST(PianoRollKeyboardReachTest, AToggleChipThatActedOnNothingKeepsItsTrueState) {
    PianoRollFixture f; // no clip open: the row filter cannot turn on
    auto* filter = f.roll.getHeaderChipButtonForTest(HeaderButtonId::ScaleFilter);
    ASSERT_NE(filter, nullptr);
    pressReturn(filter);
    EXPECT_FALSE(f.roll.isScaleFilterOn());
    EXPECT_FALSE(filter->getToggleState());
}

TEST(PianoRollKeyboardReachTest, ReturnOnTheBackChipClosesTheRoll) {
    RollWithClip f;
    bool closeRequested = false;
    f.roll.onCloseRequested = [&] { closeRequested = true; };
    pressReturn(f.roll.getHeaderChipButtonForTest(HeaderButtonId::Back));
    EXPECT_TRUE(closeRequested);
    EXPECT_FALSE(f.roll.isOpen());
}

TEST(PianoRollKeyboardReachTest, TabWalksTheHeaderChipsThenTheVelocityControls) {
    RollWithClip f;
    f.roll.toggleScalePanel();
    f.roll.resized();

    juce::KeyboardFocusTraverser traverser;
    const auto order = traverser.getAllComponents(&f.roll);
    const auto indexOf = [&](juce::Component* c) {
        const auto it = std::find(order.begin(), order.end(), c);
        return it == order.end() ? -1 : (int)(it - order.begin());
    };

    int previous = -1;
    for (const auto id : {HeaderButtonId::Back, HeaderButtonId::Quantise, HeaderButtonId::QuantiseLength,
                          HeaderButtonId::QuantisePitches, HeaderButtonId::Scale, HeaderButtonId::ScaleFilter}) {
        const int at = indexOf(f.roll.getHeaderChipButtonForTest(id));
        ASSERT_GE(at, 0) << "every chip is reachable by Tab";
        EXPECT_GT(at, previous) << "chips are walked left to right";
        previous = at;
    }
    EXPECT_GE(indexOf(f.roll.getHeaderChipButtonForTest(HeaderButtonId::Velocity)), 0);
    EXPECT_GE(indexOf(f.roll.getHeaderChipButtonForTest(HeaderButtonId::Humanize)), 0);
    EXPECT_GE(indexOf(&f.roll.getVelocityValueBox()), 0) << "the Set box";
    EXPECT_GE(indexOf(&f.roll.getVelocityLane()), 0) << "the velocity strip";
    EXPECT_GE(indexOf(&f.roll.getScaleAssistPanel().getScaleCombo()), 0) << "the scale panel's controls";
}

TEST(PianoRollKeyboardReachTest, ReturnFromClipModeOpensTheRollAndEscapeHandsTheClipBack) {
    PanelWithClip f;
    auto& lane = f.panel.getClipLaneArea();
    auto& roll = f.panel.getPianoRoll();
    ASSERT_TRUE(f.panel.getTrackHeaderAt(0)->keyPressed(plain(juce::KeyPress::rightKey)));
    ASSERT_EQ(lane.getKeyboardClip(), f.clip);

    ASSERT_TRUE(lane.keyPressed(plain(juce::KeyPress::returnKey)));
    // openPianoRoll hands keyboard focus to the roll (a no-op without a native peer): the roll is the
    // visible, focusable pane and the lanes are gone.
    EXPECT_TRUE(roll.isOpen());
    EXPECT_TRUE(roll.isVisible());
    EXPECT_TRUE(roll.getWantsKeyboardFocus());
    EXPECT_FALSE(lane.isVisible());

    ASSERT_TRUE(roll.keyPressed(plain(juce::KeyPress::escapeKey)));
    EXPECT_FALSE(roll.isOpen());
    EXPECT_TRUE(lane.isVisible()) << "closePianoRoll hands focus back to the lanes";
    EXPECT_EQ(lane.getKeyboardClip(), f.clip) << "still the clip the roll came from";
}
