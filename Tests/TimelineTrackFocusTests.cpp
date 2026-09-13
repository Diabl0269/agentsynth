// TimelineTrackFocusTests.cpp
//
// T161: keyboard focus + configurable M/S/R shortcuts for timeline track header rows.
//   - TimelineTrackHeaderComponent: setWantsKeyboardFocus(true), keyPressed() resolves bare m/s/r
//     (rebindable via ShortcutManager) and Up/Down (reported via onFocusMoveRequested, never
//     rebindable — matching ModuleLibraryComponent's T160 precedent for row navigation), mouseDown()
//     reports a click-to-select via onSelectRequested.
//   - TimelinePanelComponent: focusedTrackIndex_ (ephemeral UI state — never on TimelineDoc),
//     moveFocusedTrack()'s clamp-at-the-ends rule, ensureTrackVisible()'s auto-scroll via the real
//     trackScrollY/Viewport plumbing, and focus preserved BY TRACK ID across a syncTrackHeaders()
//     rebuild.
//
// What this file deliberately does NOT exercise, and why: a real, OS-tracked keyboard-focus grab
// needs a native peer (juce::Component::addToDesktop()), which this suite avoids for the same
// headless-CI flakiness/hang risk FocusArbitrationTest::SurfaceResolverRealFocus and
// FocusRegionTests.cpp document. Two consequences:
//   - grabKeyboardFocus()/hasKeyboardFocus() calls made by the code under test are harmless no-ops
//     here — real focus movement (and therefore the per-row accent outline TimelineTrackHeaderComponent
//     ::paintOverChildren paints) is not observable headlessly, same accepted gap as T159/T160's own
//     outline paint.
//   - TimelinePanelComponent::keyPressed's "bare Down on the panel root seeds row 0" branch is gated
//     on juce::Component::getCurrentlyFocusedComponent() == this, which never becomes true without a
//     real peer either, so that branch has no test here.
// Everything else — the MODEL (focusedTrackIndex_), the doc mutation M/S/R performs, and the scroll
// state auto-scroll writes — is plain C++ state this file drives directly, exactly the way
// ModuleLibraryKeyboardNavTests.cpp calls keyPressed() straight on the component under test.

#include "../Source/ShortcutManager.h"
#include "../Source/Timeline/TimelineDoc.h"
#include "../Source/UI/TimelinePanelComponent.h"
#include "../Source/UI/TimelineTrackHeaderComponent.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

using synth::TimelineDoc;
using synth::TrackId;
using synth::TrackKind;
using synth::ui::TimelineTrackHeaderComponent;
using synth::ui::TrackHeaderHost;

namespace {

juce::KeyPress upKey() { return juce::KeyPress(juce::KeyPress::upKey, juce::ModifierKeys::noModifiers, 0); }
juce::KeyPress downKey() { return juce::KeyPress(juce::KeyPress::downKey, juce::ModifierKeys::noModifiers, 0); }
juce::KeyPress mKey() { return juce::KeyPress('m', juce::ModifierKeys::noModifiers, 0); }
juce::KeyPress sKey() { return juce::KeyPress('s', juce::ModifierKeys::noModifiers, 0); }
juce::KeyPress rKey() { return juce::KeyPress('r', juce::ModifierKeys::noModifiers, 0); }

// Minimal TrackHeaderHost stub — counts performTrackEdit calls (the one-undo-step path the M/S/R
// keys have to go through, same as a real button click) rather than letting keyPressed() fall back
// to mutating the doc directly, which TimelineTrackHeaderComponent::performEdit only does with NO
// host installed and would prove nothing about that path.
struct StubHost : TrackHeaderHost {
    std::vector<BindingOption> getAvailableTrackInNodes(TrackId) override { return {}; }
    juce::String getNodeDisplayName(const juce::String&) override { return {}; }
    void bindTrackTo(TrackId, const juce::String&) override {}
    void createAndBindTrackInNode(TrackId) override {}
    void selectNodeInGraph(const juce::String&) override {}
    void deleteTrack(TrackId) override {}
    void performTrackEdit(const std::function<void()>& mutation) override {
        ++editCalls;
        if (mutation)
            mutation();
    }
    void addMidiTrack() override {}
    void addAudioTrack() override {}
    void addInstrumentTrack(const juce::String&, bool) override {}
    std::vector<PluginLaneOption> getAvailablePluginLaneOptions() const override { return {}; }
    synth::LaneId addPluginAutomationLane(const PluginLaneOption&) override { return {}; }

    int editCalls = 0;
};

struct TrackFocusFixture {
    TimelineDoc doc;
    StubHost host;
    synth::ui::TimelinePanelComponent panel;

    TrackFocusFixture() {
        panel.setSize(1200, 320);
        panel.setTrackHeaderHost(&host);
        panel.setTimelineDoc(&doc);
    }

    TrackId addTrack(const juce::String& name = "Track") { return doc.addTrack(TrackKind::Midi, name); }
};

// Hand-built MouseEvent — no OS mouse source exists headlessly, same pattern TimelinePanelTests.cpp's
// makeTimelineMouseEvent uses (MouseInputSource is copyable, Desktop always exposes one).
juce::MouseEvent makeClick(juce::Component& comp, juce::Point<float> position = {5.0f, 5.0f},
                           juce::ModifierKeys mods = juce::ModifierKeys()) {
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), position, mods, 0.0f, 0.0f, 0.0f, 0.0f,
                            0.0f, &comp, &comp, juce::Time::getCurrentTime(), position, juce::Time::getCurrentTime(), 1,
                            false);
}

} // namespace

// ============================================================================
// Row-level: setWantsKeyboardFocus, M/S/R
// ============================================================================

TEST(TimelineTrackFocusTest, RowWantsKeyboardFocus) {
    TrackFocusFixture f;
    auto id = f.addTrack();
    ASSERT_TRUE(id.isValid());
    EXPECT_TRUE(f.panel.getTrackHeaderAt(0)->getWantsKeyboardFocus())
        << "juce::Component::grabKeyboardFocus() is a no-op without this, exactly the reason "
           "TimelineClipLaneArea/PianoRollComponent already set it";
}

TEST(TimelineTrackFocusTest, MuteKeyTogglesTrackMutedThroughTheHostEditPath) {
    TrackFocusFixture f;
    auto id = f.addTrack();
    auto* header = f.panel.getTrackHeaderAt(0);
    ASSERT_NE(header, nullptr);
    ASSERT_FALSE(f.doc.getTrack(id)->muted);

    EXPECT_TRUE(header->keyPressed(mKey()));
    EXPECT_TRUE(f.doc.getTrack(id)->muted);
    EXPECT_EQ(f.host.editCalls, 1) << "must go through performTrackEdit, the same one-undo-step path "
                                      "the mute BUTTON's onClick uses";

    EXPECT_TRUE(header->keyPressed(mKey()));
    EXPECT_FALSE(f.doc.getTrack(id)->muted) << "a second press toggles back off";
    EXPECT_EQ(f.host.editCalls, 2);
}

TEST(TimelineTrackFocusTest, SoloKeyTogglesTrackSoloedThroughTheHostEditPath) {
    TrackFocusFixture f;
    auto id = f.addTrack();
    auto* header = f.panel.getTrackHeaderAt(0);
    ASSERT_NE(header, nullptr);

    EXPECT_TRUE(header->keyPressed(sKey()));
    EXPECT_TRUE(f.doc.getTrack(id)->soloed);
    EXPECT_EQ(f.host.editCalls, 1);
}

TEST(TimelineTrackFocusTest, ArmKeyTogglesTrackArmedThroughTheHostEditPath) {
    TrackFocusFixture f;
    auto id = f.addTrack();
    auto* header = f.panel.getTrackHeaderAt(0);
    ASSERT_NE(header, nullptr);

    EXPECT_TRUE(header->keyPressed(rKey()));
    EXPECT_TRUE(f.doc.getTrack(id)->armed);
    EXPECT_EQ(f.host.editCalls, 1);
}

TEST(TimelineTrackFocusTest, MSRKeysFallBackToBareLettersWithNoShortcutManagerInstalled) {
    TrackFocusFixture f;
    f.addTrack();
    auto* header = f.panel.getTrackHeaderAt(0);
    // No setShortcutManager call anywhere in this fixture — matchesAction() must fall back to the
    // hardcoded bare m/s/r, the same "no manager installed" contract every other surface action in
    // this app follows.
    EXPECT_TRUE(header->keyPressed(mKey()));
    EXPECT_TRUE(header->keyPressed(sKey()));
    EXPECT_TRUE(header->keyPressed(rKey()));
}

TEST(TimelineTrackFocusTest, MSRKeysAreRebindableThroughAnInstalledShortcutManager) {
    TrackFocusFixture f;
    ShortcutManager manager;
    manager.setBinding("timelineMuteFocusedTrack", juce::KeyPress('u', juce::ModifierKeys::noModifiers, 0));
    f.panel.setShortcutManager(&manager);

    auto id = f.addTrack();
    auto* header = f.panel.getTrackHeaderAt(0);
    ASSERT_NE(header, nullptr);

    EXPECT_FALSE(header->keyPressed(mKey())) << "the bare 'm' default was rebound away — an unset "
                                                "binding has NO key, it never falls back";
    EXPECT_FALSE(f.doc.getTrack(id)->muted);

    EXPECT_TRUE(header->keyPressed(juce::KeyPress('u', juce::ModifierKeys::noModifiers, 0)));
    EXPECT_TRUE(f.doc.getTrack(id)->muted);
}

TEST(TimelineTrackFocusTest, ShortcutManagerInstalledAfterHeadersExistStillReachesThem) {
    // setShortcutManager() must propagate to headers built BEFORE it was called, not just future
    // ones — TimelinePanelComponent's own setShortcutManager()/setTimelineDoc() ordering isn't
    // guaranteed by any caller.
    TrackFocusFixture f;
    f.addTrack();
    ShortcutManager manager;
    manager.setBinding("timelineSoloFocusedTrack", juce::KeyPress('u', juce::ModifierKeys::noModifiers, 0));
    f.panel.setShortcutManager(&manager);

    auto* header = f.panel.getTrackHeaderAt(0);
    ASSERT_NE(header, nullptr);
    EXPECT_FALSE(header->keyPressed(sKey())) << "bare 's' was rebound away on a header built earlier";
    EXPECT_TRUE(header->keyPressed(juce::KeyPress('u', juce::ModifierKeys::noModifiers, 0)));
}

TEST(TimelineTrackFocusTest, UnrelatedKeysAreNotClaimedByTheRow) {
    TrackFocusFixture f;
    f.addTrack();
    auto* header = f.panel.getTrackHeaderAt(0);
    ASSERT_NE(header, nullptr);
    EXPECT_FALSE(header->keyPressed(juce::KeyPress('j', juce::ModifierKeys::noModifiers, 0)))
        << "J/L/P/F and everything else must bubble to TimelinePanelComponent::keyPressed unclaimed";
    EXPECT_FALSE(header->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
}

// ============================================================================
// Row-level: Up/Down reports a direction, click reports a selection
// ============================================================================

TEST(TimelineTrackFocusTest, UpDownAlwaysClaimTheKeyAndReportADirection) {
    TrackFocusFixture f;
    f.addTrack();
    auto* header = f.panel.getTrackHeaderAt(0);
    ASSERT_NE(header, nullptr);

    int lastDirection = 0;
    int callCount = 0;
    header->onFocusMoveRequested = [&](int direction) {
        lastDirection = direction;
        ++callCount;
    };

    EXPECT_TRUE(header->keyPressed(downKey()));
    EXPECT_EQ(lastDirection, 1);
    EXPECT_TRUE(header->keyPressed(upKey()));
    EXPECT_EQ(lastDirection, -1);
    EXPECT_EQ(callCount, 2);
}

TEST(TimelineTrackFocusTest, UpDownStillClaimsTheKeyWhenNothingIsWired) {
    TrackFocusFixture f;
    f.addTrack();
    auto* header = f.panel.getTrackHeaderAt(0);
    ASSERT_NE(header, nullptr);
    EXPECT_TRUE(header->keyPressed(downKey())) << "a row built directly (no panel) still owns the key";
}

TEST(TimelineTrackFocusTest, PlainClickFiresOnSelectRequested) {
    TrackFocusFixture f;
    f.addTrack();
    auto* header = f.panel.getTrackHeaderAt(0);
    ASSERT_NE(header, nullptr);

    bool selected = false;
    header->onSelectRequested = [&] { selected = true; };
    header->mouseDown(makeClick(*header));
    EXPECT_TRUE(selected);
}

// ============================================================================
// Panel-level: focusedTrackIndex_ — click-to-select, Up/Down, clamping
// ============================================================================

TEST(TimelineTrackFocusTest, NothingFocusedByDefault) {
    TrackFocusFixture f;
    f.addTrack();
    EXPECT_EQ(f.panel.getFocusedTrackIndexForTest(), -1);
}

TEST(TimelineTrackFocusTest, ClickingARowFocusesItByIndex) {
    TrackFocusFixture f;
    f.addTrack("A");
    f.addTrack("B");
    auto* second = f.panel.getTrackHeaderAt(1);
    ASSERT_NE(second, nullptr);

    second->mouseDown(makeClick(*second));
    EXPECT_EQ(f.panel.getFocusedTrackIndexForTest(), 1);
}

TEST(TimelineTrackFocusTest, DownFromNothingFocusedLandsOnRowZero) {
    TrackFocusFixture f;
    f.addTrack("A");
    f.addTrack("B");
    f.addTrack("C");
    ASSERT_EQ(f.panel.getFocusedTrackIndexForTest(), -1);

    f.panel.getTrackHeaderAt(0)->keyPressed(downKey());
    EXPECT_EQ(f.panel.getFocusedTrackIndexForTest(), 0);
}

TEST(TimelineTrackFocusTest, DownWalksForwardAndUpWalksBackward) {
    TrackFocusFixture f;
    f.addTrack("A");
    f.addTrack("B");
    f.addTrack("C");

    f.panel.getTrackHeaderAt(0)->keyPressed(downKey()); // -> 0
    f.panel.getTrackHeaderAt(0)->keyPressed(downKey()); // -> 1
    EXPECT_EQ(f.panel.getFocusedTrackIndexForTest(), 1);
    f.panel.getTrackHeaderAt(0)->keyPressed(downKey()); // -> 2
    EXPECT_EQ(f.panel.getFocusedTrackIndexForTest(), 2);
    f.panel.getTrackHeaderAt(0)->keyPressed(upKey()); // -> 1
    EXPECT_EQ(f.panel.getFocusedTrackIndexForTest(), 1);
}

TEST(TimelineTrackFocusTest, DownClampsAtTheLastRowRatherThanWrapping) {
    TrackFocusFixture f;
    f.addTrack("A");
    f.addTrack("B");

    for (int i = 0; i < 5; ++i)
        f.panel.getTrackHeaderAt(0)->keyPressed(downKey());
    EXPECT_EQ(f.panel.getFocusedTrackIndexForTest(), 1) << "clamped at the last row, never wraps to 0";
}

TEST(TimelineTrackFocusTest, UpClampsAtTheFirstRowRatherThanWrapping) {
    TrackFocusFixture f;
    f.addTrack("A");
    f.addTrack("B");
    f.panel.getTrackHeaderAt(0)->keyPressed(downKey());
    f.panel.getTrackHeaderAt(0)->keyPressed(downKey());

    for (int i = 0; i < 5; ++i)
        f.panel.getTrackHeaderAt(0)->keyPressed(upKey());
    EXPECT_EQ(f.panel.getFocusedTrackIndexForTest(), 0);
}

TEST(TimelineTrackFocusTest, MoveFocusedTrackIsANoOpWithNoTracks) {
    TrackFocusFixture f;
    // No track headers exist at all — Up/Down can never even reach a row's keyPressed() in this
    // state in the real app, but nothing here should crash if it somehow were driven directly.
    EXPECT_EQ(f.panel.getTrackHeaderCount(), 0);
    EXPECT_EQ(f.panel.getFocusedTrackIndexForTest(), -1);
}

// ============================================================================
// Panel-level: auto-scroll via the real Viewport/trackScrollY plumbing
// ============================================================================

TEST(TimelineTrackFocusTest, MovingFocusBelowTheVisibleWindowScrollsDown) {
    TrackFocusFixture f;
    // A short panel (320px total, well under the transport bar + ruler + many rows) so the header
    // viewport's visible window is a handful of rows — enough to force scrolling well before 20
    // tracks are added.
    for (int i = 0; i < 20; ++i)
        f.addTrack("T" + juce::String(i));
    f.panel.resized();

    ASSERT_EQ(f.panel.getViewState().trackScrollY, 0.0);

    // Walk down past whatever fits on screen.
    for (int i = 0; i < 15; ++i)
        f.panel.getTrackHeaderAt(0)->keyPressed(downKey());

    EXPECT_EQ(f.panel.getFocusedTrackIndexForTest(), 14);
    EXPECT_GT(f.panel.getViewState().trackScrollY, 0.0)
        << "the focused row moved below the visible window, so ensureTrackVisible() must have "
           "scrolled trackScrollY (and, via scrollTrackRows -> syncTrackScroll, the header Viewport)";
}

TEST(TimelineTrackFocusTest, MovingFocusBackUpScrollsTheViewBackToTheTop) {
    TrackFocusFixture f;
    for (int i = 0; i < 20; ++i)
        f.addTrack("T" + juce::String(i));
    f.panel.resized();

    for (int i = 0; i < 15; ++i)
        f.panel.getTrackHeaderAt(0)->keyPressed(downKey());
    ASSERT_GT(f.panel.getViewState().trackScrollY, 0.0);

    for (int i = 0; i < 15; ++i)
        f.panel.getTrackHeaderAt(0)->keyPressed(upKey());

    EXPECT_EQ(f.panel.getFocusedTrackIndexForTest(), 0);
    EXPECT_EQ(f.panel.getViewState().trackScrollY, 0.0);
}

// ============================================================================
// Panel-level: focus survives a syncTrackHeaders() rebuild, by TrackId
// ============================================================================

TEST(TimelineTrackFocusTest, DeletingATrackAboveTheFocusedOnePreservesFocusOnTheSameTrack) {
    TrackFocusFixture f;
    auto a = f.addTrack("A");
    auto b = f.addTrack("B");
    f.addTrack("C");

    auto* headerB = f.panel.getTrackHeaderAt(1);
    ASSERT_NE(headerB, nullptr);
    headerB->mouseDown(makeClick(*headerB));
    ASSERT_EQ(f.panel.getFocusedTrackIndexForTest(), 1);

    f.doc.removeTrack(a); // the set of tracks changed -> syncTrackHeaders() rebuilds every row
    ASSERT_EQ(f.panel.getTrackHeaderCount(), 2);
    EXPECT_EQ(f.panel.getFocusedTrackIndexForTest(), 0) << "B is now row 0, and focus must follow it "
                                                           "there rather than staying pinned at the "
                                                           "old numeric index 1 (which is now C)";
    EXPECT_EQ(f.panel.getTrackHeaderAt(f.panel.getFocusedTrackIndexForTest())->getTrackId(), b);
}

TEST(TimelineTrackFocusTest, DeletingTheFocusedTrackItselfClearsFocus) {
    TrackFocusFixture f;
    auto a = f.addTrack("A");
    f.addTrack("B");

    auto* headerA = f.panel.getTrackHeaderAt(0);
    headerA->mouseDown(makeClick(*headerA));
    ASSERT_EQ(f.panel.getFocusedTrackIndexForTest(), 0);

    f.doc.removeTrack(a);
    EXPECT_EQ(f.panel.getFocusedTrackIndexForTest(), -1);
}

TEST(TimelineTrackFocusTest, ARenameDoesNotDisturbFocusedTrackIndex) {
    // A rename/mute/rebind is a refresh-in-place, not a rebuild (the track SET is unchanged) — see
    // syncTrackHeaders()'s own sameTracks branch, which returns before touching focusedTrackIndex_
    // at all.
    TrackFocusFixture f;
    f.addTrack("A");
    auto b = f.addTrack("B");

    auto* headerB = f.panel.getTrackHeaderAt(1);
    headerB->mouseDown(makeClick(*headerB));
    ASSERT_EQ(f.panel.getFocusedTrackIndexForTest(), 1);

    f.doc.setTrackName(b, "Renamed");
    EXPECT_EQ(f.panel.getFocusedTrackIndexForTest(), 1);
}
