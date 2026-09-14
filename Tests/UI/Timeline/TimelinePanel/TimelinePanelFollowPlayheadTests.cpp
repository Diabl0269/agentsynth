// TimelinePanelFollowPlayheadTests.cpp
//
// Follow-playhead: the toggle (state + button mirror + persistence) and the page-flip it
// drives inside updateFromTransport(). Panel level, ungated. IsolatedPropsGuard/
// FollowPlayheadFixture are local to this file.

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

// ============================================================================
// 8. Follow-playhead: the toggle (state + button mirror + persistence) and the page-flip it
//    drives inside updateFromTransport(). Panel level, ungated (see the file header) — backfilled
//    for the already-landed TL implementation (see EdgeAutoScroll.h / TimelineClipLaneArea's
//    beat-anchored drag, covered in TimelineClipLane/TimelineClipLaneMouseTests.cpp).
// ============================================================================

namespace {
// Same isolated-properties-file idiom as TimelinePanelSnapComboTest::SnapChoicePersists above:
// hermetic regardless of a previous run, and cleaned up on the way out.
struct IsolatedPropsGuard {
    juce::PropertiesFile::Options opts;
    juce::ApplicationProperties props;

    explicit IsolatedPropsGuard(const char* name) {
        opts.applicationName = name;
        opts.folderName = name;
        opts.filenameSuffix = "settings";
        opts.osxLibrarySubFolder = "Application Support";
        opts.storageFormat = juce::PropertiesFile::storeAsXML;

        {
            juce::ApplicationProperties initial;
            initial.setStorageParameters(opts);
            if (auto* s = initial.getUserSettings())
                s->getFile().deleteFile();
        }
        props.setStorageParameters(opts);
    }

    ~IsolatedPropsGuard() {
        if (auto* s = props.getUserSettings())
            s->getFile().deleteFile();
    }
};
} // namespace

TEST(TimelineFollowPlayheadTest, ToggleFlipsStateAndButtonMirror) {
    synth::ui::TimelinePanelComponent panel;
    panel.setSize(1200, 320);
    ASSERT_FALSE(panel.isFollowPlayheadEnabled()) << "documented default: off";
    EXPECT_FALSE(panel.getFollowPlayheadButtonForTest().getToggleState());

    panel.setFollowPlayheadEnabled(true);
    EXPECT_TRUE(panel.isFollowPlayheadEnabled());
    EXPECT_TRUE(panel.getFollowPlayheadButtonForTest().getToggleState()) << "the button only mirrors the state";

    panel.setFollowPlayheadEnabled(false);
    EXPECT_FALSE(panel.isFollowPlayheadEnabled());
    EXPECT_FALSE(panel.getFollowPlayheadButtonForTest().getToggleState());
}

TEST(TimelineFollowPlayheadTest, ButtonClickRoundTripsThroughSetFollowPlayheadEnabled) {
    synth::ui::TimelinePanelComponent panel;
    panel.setSize(1200, 320);
    ASSERT_FALSE(panel.isFollowPlayheadEnabled());

    panel.getFollowPlayheadButtonForTest().onClick();
    EXPECT_TRUE(panel.isFollowPlayheadEnabled());
    EXPECT_TRUE(panel.getFollowPlayheadButtonForTest().getToggleState());

    panel.getFollowPlayheadButtonForTest().onClick();
    EXPECT_FALSE(panel.isFollowPlayheadEnabled());
    EXPECT_FALSE(panel.getFollowPlayheadButtonForTest().getToggleState());
}

// The choice persists under "timelineFollowPlayhead" and is restored by a fresh
// setApplicationProperties call against the same (isolated) properties file.
TEST(TimelineFollowPlayheadTest, ChoicePersistsAndIsRestored) {
    IsolatedPropsGuard guard("Agent Synth Timeline Follow Test");

    synth::ui::TimelinePanelComponent panel;
    panel.setSize(1200, 320);
    panel.setApplicationProperties(&guard.props);
    ASSERT_FALSE(panel.isFollowPlayheadEnabled()) << "documented default, freshly-deleted file";

    panel.setFollowPlayheadEnabled(true);
    ASSERT_NE(guard.props.getUserSettings(), nullptr);
    EXPECT_TRUE(guard.props.getUserSettings()->getBoolValue("timelineFollowPlayhead", false));

    synth::ui::TimelinePanelComponent panel2;
    panel2.setSize(1200, 320);
    panel2.setApplicationProperties(&guard.props);
    EXPECT_TRUE(panel2.isFollowPlayheadEnabled());
    EXPECT_TRUE(panel2.getFollowPlayheadButtonForTest().getToggleState());
}

// The follow-playhead button re-skins on a theme switch — the SAME sequence (the theme mutates in
// place, then the app broadcasts via sendLookAndFeelChange(), with no doc/view change in between)
// TimelineTrackHeaderTest::ThemeSwitchReappliesChipAndMSRColoursWithNoDocChange locks for the
// track header's chip/M/S/R colours. applyToolStripTheme() is the seam both the constructor and
// lookAndFeelChanged() run through, and backgroundOnColourId is the exact colour it writes for
// this button (see TimelinePanelComponent::applyToolStripTheme).
TEST(TimelineFollowPlayheadTest, ThemeSwitchReskinsTheFollowPlayheadButton) {
    synth::ui::TimelinePanelComponent panel;
    panel.setSize(1200, 320);

    synth::theme::AppLookAndFeel lf;
    const auto themeA = synth::theme::makeObsidian();
    const auto themeB = synth::theme::makeNeon();

    lf.applyTheme(themeA);
    panel.setLookAndFeel(&lf); // installing triggers lookAndFeelChanged() once already
    EXPECT_EQ(panel.getFollowPlayheadButtonForTest().findColour(juce::DrawableButton::backgroundOnColourId),
              themeA.colors.toolActive);

    // Theme mutates in place, then the app broadcasts the switch — no doc/view change at all, only
    // lookAndFeelChanged() to notice the button needs re-tinting.
    lf.applyTheme(themeB);
    panel.sendLookAndFeelChange();

    EXPECT_EQ(panel.getFollowPlayheadButtonForTest().findColour(juce::DrawableButton::backgroundOnColourId),
              themeB.colors.toolActive);

    panel.setLookAndFeel(nullptr);
}

// Regression test for the reported bug: the follow-playhead button never appearing in the running
// app. Reproduces AgentSynthPluginEditor's EXACT construction order (see PluginEditor.cpp's ctor):
// a parent has setLookAndFeel() called on it BEFORE the timeline panel is added as its child. At
// panel-CONSTRUCTION time there is therefore no themed LookAndFeel anywhere on its (nonexistent)
// ancestor chain, so applyToolStripTheme()'s constructor-time call is a no-op for every icon,
// including this button's — and juce::Component::sendLookAndFeelChange() only walks components
// ALREADY in a child list at the moment it fires, so installing the LnF on `parent` first never
// reaches `panel` either. Only parentHierarchyChanged(), fired when `panel` is attached moments
// later, can pick the theme up — this is what proves that hook actually does.
TEST(TimelineFollowPlayheadTest, AttachingUnderAnAncestorThatAlreadyHasAThemedLookAndFeelStillAppliesTheIcon) {
    synth::ui::TimelinePanelComponent panel;
    panel.setSize(1200, 320);

    juce::Component parent;
    synth::theme::AppLookAndFeel lf;
    const auto theme = synth::theme::makeObsidian();
    lf.applyTheme(theme);
    parent.setLookAndFeel(&lf); // installed BEFORE panel is a child — the plugin editor's order

    parent.addAndMakeVisible(panel);

    EXPECT_EQ(panel.getFollowPlayheadButtonForTest().findColour(juce::DrawableButton::backgroundOnColourId),
              theme.colors.toolActive)
        << "parentHierarchyChanged() must re-run applyToolStripTheme() once attached under an "
           "ancestor that already has a themed LookAndFeel — otherwise the icon/background this "
           "button needs never gets applied at all";

    parent.setLookAndFeel(nullptr);
}

// The three explicit checks the bug report asked for: real (non-empty, in-panel, non-overlapping)
// bounds at a realistic launch size, and visibility all the way up to the panel. Arithmetic-only
// coverage — TimelinePanelComponentTest.PanelRegionsTile-style — for resized()'s transport-bar
// carve order, which the strip-width audit in this task's report shows has headroom today but is
// exactly the kind of change that could silently zero this button out again.
TEST(TimelineFollowPlayheadTest, ButtonHasRealNonOverlappingBoundsAndIsVisibleAtRealisticSize) {
    synth::ui::TimelinePanelComponent panel;
    synth::theme::AppLookAndFeel lf;
    lf.applyTheme(synth::theme::makeObsidian());
    panel.setLookAndFeel(&lf);
    panel.setVisible(true); // a parentless Component is never visible by default
    panel.setSize(1200, 320);

    auto& button = panel.getFollowPlayheadButtonForTest();
    const auto bounds = button.getBounds();
    EXPECT_FALSE(bounds.isEmpty()) << "follow-playhead button got a zero-area bounds";
    EXPECT_TRUE(panel.getLocalBounds().contains(bounds)) << "follow-playhead button bounds fall outside the panel";

    const auto snapComboBounds = panel.getSnapCombo().getBounds();
    const auto snapToggleBounds = panel.getSnapToggleButton().getBounds();
    EXPECT_FALSE(bounds.intersects(snapComboBounds)) << "follow-playhead button overlaps the snap combo";
    EXPECT_FALSE(bounds.intersects(snapToggleBounds)) << "follow-playhead button overlaps the snap toggle (Q)";

    // Visible all the way up to the panel — nothing on this path was ever hidden via
    // addChildComponent (which starts invisible) instead of addAndMakeVisible. isShowing() is
    // deliberately NOT asserted here: it additionally requires a real OS peer
    // (Component::addToDesktop()), which this test suite avoids for the same headless-CI
    // flakiness reason FocusArbitrationTests.cpp's real-focus tests do.
    EXPECT_TRUE(button.isVisible());
    EXPECT_TRUE(panel.isVisible());

    panel.setLookAndFeel(nullptr);
}

// F mirrors the transport strip's follow button, resolved through "timelineFollowPlayheadToggle"
// exactly like Q/L/P (see PanelLetterKeysResolveThroughTheShortcutManager for the shared idiom):
// hardcoded fallback with no manager, strict resolution with one installed.
TEST(TimelineFollowPlayheadTest, FKeyTogglesFollowThroughTheShortcutManager) {
    synth::ui::TimelinePanelComponent panel;
    panel.setSize(1200, 320);

    // No manager: the hardcoded 'f' fallback.
    ASSERT_FALSE(panel.isFollowPlayheadEnabled());
    EXPECT_TRUE(panel.keyPressed(juce::KeyPress('f')));
    EXPECT_TRUE(panel.isFollowPlayheadEnabled());
    EXPECT_TRUE(panel.keyPressed(juce::KeyPress('f')));
    EXPECT_FALSE(panel.isFollowPlayheadEnabled());

    // With a manager installed, resolution is strict: rebinding moves the key, and the old one
    // falls through untouched.
    ShortcutManager shortcuts;
    panel.setShortcutManager(&shortcuts);
    EXPECT_TRUE(panel.keyPressed(juce::KeyPress('f'))) << "default binding is a bare F";
    EXPECT_TRUE(panel.isFollowPlayheadEnabled());

    shortcuts.setBinding("timelineFollowPlayheadToggle", juce::KeyPress('g', juce::ModifierKeys::noModifiers, 0));
    EXPECT_FALSE(panel.keyPressed(juce::KeyPress('f'))) << "the old key falls through";
    EXPECT_TRUE(panel.isFollowPlayheadEnabled()) << "and did not toggle";
    EXPECT_TRUE(panel.keyPressed(juce::KeyPress('g')));
    EXPECT_FALSE(panel.isFollowPlayheadEnabled());
}

namespace {
// A minimal fixture for updateFromTransport()'s page-flip: a doc-less panel (the page-flip needs
// no TimelineDoc at all) with the view state pinned so the expected math below is exact.
struct FollowPlayheadFixture {
    synth::ui::TimelinePanelComponent panel;

    FollowPlayheadFixture() {
        panel.setSize(1200, 320);
        auto& state = panel.getViewState();
        state.pixelsPerBeat = 40.0;
        state.firstVisibleBeat = 0.0;
        panel.setFollowPlayheadEnabled(true);
    }

    // The exact width the page-flip's visibleBeats term reads from — clipLaneArea_ fills
    // gridLanesBounds_ exactly (see TimelinePanelComponent::resized()'s comment), so this IS that
    // width without duplicating layout maths.
    double visibleBeats() { return (double)panel.getClipLaneArea().getWidth() / panel.getViewState().pixelsPerBeat; }

    synth::TransportService::PositionSnapshot playingSnapshotAt(double ppq) const {
        synth::TransportService::PositionSnapshot snap;
        snap.playing = true;
        snap.ppq = ppq;
        return snap;
    }
};
} // namespace

TEST(TimelineFollowPlayheadTest, PageFlipsWhenThePlayheadCrossesTheRightEdge) {
    FollowPlayheadFixture f;
    const double visible = f.visibleBeats();
    // Just past the last visible beat.
    const double playheadBeat = visible + 1.0;

    f.panel.updateFromTransport(f.playingSnapshotAt(playheadBeat), 0.0);

    // The landed implementation's exact formula: max(0, playheadBeat - 0.1 * visibleBeats).
    const double expected = std::max(0.0, playheadBeat - 0.1 * visible);
    EXPECT_DOUBLE_EQ(f.panel.getViewState().firstVisibleBeat, expected);
}

TEST(TimelineFollowPlayheadTest, NoScrollWhilePlayheadStaysInsideTheVisibleRange) {
    FollowPlayheadFixture f;
    const double visible = f.visibleBeats();
    const double playheadBeat = visible * 0.5; // comfortably inside [0, visible]

    f.panel.updateFromTransport(f.playingSnapshotAt(playheadBeat), 0.0);

    EXPECT_DOUBLE_EQ(f.panel.getViewState().firstVisibleBeat, 0.0);
}

TEST(TimelineFollowPlayheadTest, NoScrollWhenStopped) {
    FollowPlayheadFixture f;
    const double visible = f.visibleBeats();
    auto snapshot = f.playingSnapshotAt(visible + 1.0);
    snapshot.playing = false;

    f.panel.updateFromTransport(snapshot, 0.0);

    EXPECT_DOUBLE_EQ(f.panel.getViewState().firstVisibleBeat, 0.0);
}

TEST(TimelineFollowPlayheadTest, NoScrollWhenFollowIsOff) {
    FollowPlayheadFixture f;
    f.panel.setFollowPlayheadEnabled(false);
    const double visible = f.visibleBeats();

    f.panel.updateFromTransport(f.playingSnapshotAt(visible + 1.0), 0.0);

    EXPECT_DOUBLE_EQ(f.panel.getViewState().firstVisibleBeat, 0.0);
}

TEST(TimelineFollowPlayheadTest, NoScrollWhileThePianoRollIsOpen) {
    FollowPlayheadFixture f;
    synth::TimelineDoc doc;
    f.panel.setTimelineDoc(&doc);
    const auto track = doc.addTrack(synth::TrackKind::Midi, "Midi");
    const auto clip = doc.addClip(track, 0.0, 4.0, "Clip");
    ASSERT_TRUE(clip.isValid());

    f.panel.openPianoRoll(clip);
    ASSERT_TRUE(f.panel.isPianoRollOpen());

    const double visible = f.visibleBeats();
    f.panel.updateFromTransport(f.playingSnapshotAt(visible + 1.0), 0.0);

    EXPECT_DOUBLE_EQ(f.panel.getViewState().firstVisibleBeat, 0.0);
}

namespace {
// A local left-button mouse event for the clip-lane area — the same shape
// TimelineClipLane/TimelineClipLaneTestFixture.h's makeClipMouseEvent builds. Kept local (rather than reusing
// TimelinePanelTestFixture.h's makeTimelineMouseEvent) since it always forces the left-button
// modifier rather than taking one from the caller.
juce::MouseEvent leftButtonEventOnLane(juce::Component& comp, juce::Point<float> pos, juce::Point<float> anchor,
                                       bool wasDragged) {
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), pos,
                            juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                            &comp, &comp, juce::Time::getCurrentTime(), anchor, juce::Time::getCurrentTime(), 1,
                            wasDragged);
}
} // namespace

TEST(TimelineFollowPlayheadTest, NoScrollWhileAClipDragIsInProgress) {
    FollowPlayheadFixture f;
    synth::TimelineDoc doc;
    AppUndoManager undo;
    f.panel.setTimelineDoc(&doc);
    f.panel.setUndoManager(&undo);
    const auto track = doc.addTrack(synth::TrackKind::Midi, "Midi");
    const auto clip = doc.addClip(track, 0.0, 4.0, "Clip");
    ASSERT_TRUE(clip.isValid());

    auto& lane = f.panel.getClipLaneArea();
    const auto rect = lane.getClipRect(clip);
    const juce::Point<float> anchor((float)rect.getCentreX(), (float)rect.getCentreY());
    const juce::Point<float> dragged(anchor.x + 10.0f, anchor.y);

    lane.mouseDown(leftButtonEventOnLane(lane, anchor, anchor, false));
    lane.mouseDrag(leftButtonEventOnLane(lane, dragged, anchor, true));
    ASSERT_TRUE(lane.isDragInProgress());

    const double visible = f.visibleBeats();
    f.panel.updateFromTransport(f.playingSnapshotAt(visible + 1.0), 0.0);

    EXPECT_DOUBLE_EQ(f.panel.getViewState().firstVisibleBeat, 0.0);

    lane.mouseUp(leftButtonEventOnLane(lane, dragged, anchor, true));
}
