// ModuleLibraryHelpPopupTests.cpp
//
// Headless coverage for the module library sidebar's "?" help button (ModuleLibraryComponent) and
// its popover content (synth::ui::ModuleLibraryHelpPopup) — a real juce::CallOutBox is NEVER
// constructed anywhere in this file. That is a top-level window and crashes a display-less test
// runner exactly the way docs/timeline_panel_core.md's marker-context-menu seam note describes for
// juce::PopupMenu, so every test here drives either the pure content helpers, the mouse-
// hover/geometry seam, or one of the two protected-virtual leaves ModuleLibraryComponent exposes
// for exactly this reason:
//   - showHelpPopover()      — the whole pin-aware dispatch; RecordingModuleLibraryComponent stubs
//                              this ENTIRELY, for tests that only care whether the help button was
//                              clicked at all (vs. falling through to toggleAllSections()).
//   - launchHelpCallOutBox() — ONLY the real juce::CallOutBox construction;
//                              RecordingCallOutBoxModuleLibraryComponent stubs just this, so
//                              showHelpPopover()'s and setHelpPopoverPinned()'s real pin/re-host
//                              logic still runs for real. Pinning itself (setHelpPopoverPinned(true))
//                              never reaches launchHelpCallOutBox() at all, so it is exercised
//                              directly on a plain ModuleLibraryComponent wherever that is all a
//                              test needs — see synth::ui::ModuleLibraryHelpPopup's class comment
//                              for why the CallOutBox/floating split is implemented this way.
// Mirrors MidiDestinationPickerTests.cpp's "talk to the component directly" approach.

#include "../Source/ShortcutManager.h"
#include "../Source/UI/ModuleLibraryComponent.h"
#include "../Source/UI/ModuleLibraryHelpPopup.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

using synth::ui::ModuleLibraryHelpPopup;

namespace {

juce::MouseEvent makeMouseEventAt(juce::Component& comp, juce::Point<float> pos) {
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), pos, juce::ModifierKeys(), 1.0f, 0.0f,
                            0.0f, 0.0f, 0.0f, &comp, &comp, juce::Time::getCurrentTime(), pos,
                            juce::Time::getCurrentTime(), 1, false);
}

// Overrides the CallOutBox-launching seam so mouseDown()'s real routing logic can be exercised
// without ever creating a real top-level window (see this file's header comment).
class RecordingModuleLibraryComponent : public ModuleLibraryComponent {
public:
    int helpPopoverRequests = 0;

protected:
    void showHelpPopover() override { ++helpPopoverRequests; }
};

// Stubs ONLY the real juce::CallOutBox construction, leaving showHelpPopover()'s pin-aware
// dispatch and setHelpPopoverPinned()'s un-pin path fully real — see this file's header comment.
class RecordingCallOutBoxModuleLibraryComponent : public ModuleLibraryComponent {
public:
    int callOutBoxLaunches = 0;

protected:
    void launchHelpCallOutBox() override { ++callOutBoxLaunches; }
};

} // namespace

// ============================================================================
// Header-row hit-test / hover for the "?" help button
// ============================================================================

TEST(ModuleLibraryHelpButton, BoundsSitInsideTheTopStrip) {
    const auto bounds = ModuleLibraryComponent::getHelpButtonBounds();
    EXPECT_TRUE(ModuleLibraryComponent::isInTopStrip(bounds.getY()));
    EXPECT_TRUE(ModuleLibraryComponent::isInTopStrip(bounds.getBottom() - 1));
    EXPECT_GT(bounds.getWidth(), 0);
    EXPECT_GT(bounds.getHeight(), 0);
}

TEST(ModuleLibraryHelpButton, HoveringTheButtonSetsHelpButtonHover) {
    ModuleLibraryComponent comp;
    comp.setSize(200, 600);

    const auto bounds = ModuleLibraryComponent::getHelpButtonBounds();
    comp.mouseMove(makeMouseEventAt(comp, bounds.toFloat().getCentre()));
    EXPECT_TRUE(comp.isHelpButtonHoveredForTest());
}

TEST(ModuleLibraryHelpButton, HoveringElsewhereInTheStripDoesNotHoverTheButton) {
    ModuleLibraryComponent comp;
    comp.setSize(200, 600);

    // Far right of the strip, well clear of the button's bounds on the left.
    const int stripY = ModuleLibraryComponent::kSearchHeight + 4;
    comp.mouseMove(makeMouseEventAt(comp, {150.0f, (float)stripY}));
    EXPECT_FALSE(comp.isHelpButtonHoveredForTest());
}

TEST(ModuleLibraryHelpButton, HoveringAboveTheStripDoesNotHoverTheButton) {
    ModuleLibraryComponent comp;
    comp.setSize(200, 600);

    // Same x as the button, but inside the search field above the strip.
    const auto bounds = ModuleLibraryComponent::getHelpButtonBounds();
    comp.mouseMove(makeMouseEventAt(comp, {(float)bounds.getCentreX(), 4.0f}));
    EXPECT_FALSE(comp.isHelpButtonHoveredForTest());
}

TEST(ModuleLibraryHelpButton, MouseExitClearsButtonHover) {
    ModuleLibraryComponent comp;
    comp.setSize(200, 600);

    const auto bounds = ModuleLibraryComponent::getHelpButtonBounds();
    comp.mouseMove(makeMouseEventAt(comp, bounds.toFloat().getCentre()));
    ASSERT_TRUE(comp.isHelpButtonHoveredForTest());

    comp.mouseExit(makeMouseEventAt(comp, {-1.0f, -1.0f}));
    EXPECT_FALSE(comp.isHelpButtonHoveredForTest());
}

TEST(ModuleLibraryHelpButton, PaintWithButtonHoveredNoCrash) {
    ModuleLibraryComponent comp;
    comp.setSize(200, 600);

    const auto bounds = ModuleLibraryComponent::getHelpButtonBounds();
    comp.mouseMove(makeMouseEventAt(comp, bounds.toFloat().getCentre()));
    ASSERT_TRUE(comp.isHelpButtonHoveredForTest());

    juce::Image img(juce::Image::ARGB, 200, 600, true);
    juce::Graphics g(img);
    EXPECT_NO_THROW(comp.paint(g));
}

TEST(ModuleLibraryHelpButton, ClickingTheButtonOpensTheHelpPopoverInsteadOfCollapsingSections) {
    RecordingModuleLibraryComponent comp;
    comp.setSize(200, 600);
    ASSERT_FALSE(comp.areAllSectionsCollapsed());

    const auto bounds = ModuleLibraryComponent::getHelpButtonBounds();
    comp.mouseDown(makeMouseEventAt(comp, bounds.toFloat().getCentre()));

    EXPECT_EQ(comp.helpPopoverRequests, 1);
    EXPECT_FALSE(comp.areAllSectionsCollapsed())
        << "clicking the help button must not fall through to the collapse-all behaviour";
}

TEST(ModuleLibraryHelpButton, ClickingElsewhereInTheStripStillCollapsesEverything) {
    RecordingModuleLibraryComponent comp;
    comp.setSize(200, 600);

    const int stripY = ModuleLibraryComponent::kSearchHeight + 4;
    comp.mouseDown(makeMouseEventAt(comp, {150.0f, (float)stripY}));

    EXPECT_EQ(comp.helpPopoverRequests, 0);
    EXPECT_TRUE(comp.areAllSectionsCollapsed());
}

// ============================================================================
// Popover content builds headlessly, via the seam — no juce::CallOutBox involved
// ============================================================================

TEST(ModuleLibraryHelpPopupSeam, CreateHelpPopupForTestBuildsTheRealPopupContent) {
    ModuleLibraryComponent comp;
    auto* popup = comp.createHelpPopupForTest();
    EXPECT_NE(popup, nullptr) << "the seam must hand back the same object the real button would show";
}

TEST(ModuleLibraryHelpPopupSeam, CreateHelpPopupForTestReturnsTheSamePersistentInstanceEveryCall) {
    // The popup is built ONCE and re-hosted, never rebuilt per open — a pin transition would have
    // nothing to transplant otherwise. Calling the seam twice must hand back the identical object.
    ModuleLibraryComponent comp;
    auto* first = comp.createHelpPopupForTest();
    auto* second = comp.createHelpPopupForTest();
    EXPECT_EQ(first, second);
}

TEST(ModuleLibraryHelpPopupSeam, ThreeSectionsInOrder) {
    ModuleLibraryHelpPopup popup;
    EXPECT_EQ(popup.getSectionTitleForTest(0), "Using modules");
    EXPECT_EQ(popup.getSectionTitleForTest(1), "Your first patch");
    EXPECT_EQ(popup.getSectionTitleForTest(2), "Key shortcuts");
}

TEST(ModuleLibraryHelpPopupSeam, SectionsStartExpanded) {
    ModuleLibraryHelpPopup popup;
    for (int i = 0; i < ModuleLibraryHelpPopup::kSectionCount; ++i)
        EXPECT_TRUE(popup.isSectionExpandedForTest(i)) << i;
}

TEST(ModuleLibraryHelpPopupSeam, TogglingASectionCollapsesOnlyThatSection) {
    ModuleLibraryHelpPopup popup;
    popup.toggleSectionForTest(1);
    EXPECT_TRUE(popup.isSectionExpandedForTest(0));
    EXPECT_FALSE(popup.isSectionExpandedForTest(1));
    EXPECT_TRUE(popup.isSectionExpandedForTest(2));

    popup.toggleSectionForTest(1);
    EXPECT_TRUE(popup.isSectionExpandedForTest(1));
}

TEST(ModuleLibraryHelpPopupSeam, PaintSmokeNoCrash) {
    ModuleLibraryHelpPopup popup;
    juce::Image img(juce::Image::ARGB, juce::jmax(1, popup.getWidth()), juce::jmax(1, popup.getHeight()), true);
    juce::Graphics g(img);
    EXPECT_NO_THROW(popup.paint(g));
}

TEST(ModuleLibraryHelpPopupSeam, ComponentPassesItsShortcutManagerIntoThePopup) {
    ShortcutManager manager;
    manager.setBinding("undo", juce::KeyPress('u', juce::ModifierKeys::commandModifier, 0));

    ModuleLibraryComponent comp;
    comp.setShortcutManager(&manager);

    auto* popup = comp.createHelpPopupForTest();
    ASSERT_NE(popup, nullptr);

    // The command modifier renders "Cmd" on macOS and "Ctrl" elsewhere — resolve the expected
    // text through the same formatter the popup uses instead of hard-coding one platform's name.
    const auto cmdU =
        ShortcutManager::keyPressToDisplayString(juce::KeyPress('u', juce::ModifierKeys::commandModifier, 0));
    const auto cmdZ =
        ShortcutManager::keyPressToDisplayString(juce::KeyPress('z', juce::ModifierKeys::commandModifier, 0));
    const auto lines = popup->getSectionLinesForTest(ModuleLibraryHelpPopup::KeyShortcuts);
    const juce::String joined = lines.joinIntoString(" | ");
    EXPECT_TRUE(joined.contains(cmdU));
    EXPECT_FALSE(joined.contains(cmdZ));
}

TEST(ModuleLibraryHelpPopupSeam, RefreshingAfterARebindUpdatesTheShortcutSectionOfAPersistentPopup) {
    // setShortcutManager()/createHelpPopupForTest() only capture the manager's bindings ONCE, at
    // first construction — because the popup now persists across opens (so pinning has something
    // to re-host), the owner must re-pull the live bindings on every subsequent open
    // (refreshHelpPopoverForTest() drives the exact call showHelpPopover() makes for real) or a
    // rebind made after the popup already exists would stale forever for this app session.
    ShortcutManager manager;
    ModuleLibraryComponent comp;
    comp.setShortcutManager(&manager);
    auto* popup = comp.createHelpPopupForTest();

    const auto cmdZ =
        ShortcutManager::keyPressToDisplayString(juce::KeyPress('z', juce::ModifierKeys::commandModifier, 0));
    const auto cmdU =
        ShortcutManager::keyPressToDisplayString(juce::KeyPress('u', juce::ModifierKeys::commandModifier, 0));
    ASSERT_TRUE(popup->getSectionLinesForTest(ModuleLibraryHelpPopup::KeyShortcuts).joinIntoString("|").contains(cmdZ));

    manager.setBinding("undo", juce::KeyPress('u', juce::ModifierKeys::commandModifier, 0));
    comp.refreshHelpPopoverForTest();

    const auto after = popup->getSectionLinesForTest(ModuleLibraryHelpPopup::KeyShortcuts).joinIntoString("|");
    EXPECT_TRUE(after.contains(cmdU));
    EXPECT_FALSE(after.contains(cmdZ));
}

// ============================================================================
// Pin / float (round 2) — a real juce::CallOutBox is never constructed in this section; pinning
// alone never reaches one, and every test that could reach the un-pin/reopen path uses
// RecordingCallOutBoxModuleLibraryComponent (see this file's header comment).
// ============================================================================

TEST(ModuleLibraryHelpPin, DefaultsToUnpinned) {
    ModuleLibraryComponent comp;
    EXPECT_FALSE(comp.isHelpPopoverPinnedForTest());
    EXPECT_FALSE(comp.createHelpPopupForTest()->isPinned());
}

TEST(ModuleLibraryHelpPin, UnpinnedClickStillRoutesThroughTheRealCallOutBoxLaunchSeam) {
    // "Unpinned dismiss behaviour unchanged": clicking "?" while unpinned must still take the
    // callout path (never the floating one) — proven by counting calls to the one leaf that
    // constructs the real juce::CallOutBox, with showHelpPopover()'s actual dispatch logic intact.
    RecordingCallOutBoxModuleLibraryComponent comp;
    comp.setSize(200, 600);

    const auto bounds = ModuleLibraryComponent::getHelpButtonBounds();
    comp.mouseDown(makeMouseEventAt(comp, bounds.toFloat().getCentre()));

    EXPECT_EQ(comp.callOutBoxLaunches, 1);
    EXPECT_FALSE(comp.isHelpPopoverPinnedForTest());
}

TEST(ModuleLibraryHelpPin, PinningReHostsTheSamePopupWithTheSameSectionContent) {
    ModuleLibraryComponent comp;
    auto* popup = comp.createHelpPopupForTest();
    const auto firstPatchBefore = popup->getSectionLinesForTest(ModuleLibraryHelpPopup::FirstPatch);
    const auto shortcutsBefore = popup->getSectionLinesForTest(ModuleLibraryHelpPopup::KeyShortcuts);

    comp.setHelpPopoverPinnedForTest(true);

    EXPECT_TRUE(comp.isHelpPopoverPinnedForTest());
    EXPECT_TRUE(popup->isPinned());
    // Same object — re-hosted, not rebuilt.
    EXPECT_EQ(comp.createHelpPopupForTest(), popup);
    EXPECT_EQ(popup->getSectionLinesForTest(ModuleLibraryHelpPopup::FirstPatch), firstPatchBefore);
    EXPECT_EQ(popup->getSectionLinesForTest(ModuleLibraryHelpPopup::KeyShortcuts), shortcutsBefore);
    // Re-hosted as a plain, non-modal child (addAndMakeVisible on an ancestor) — never a desktop
    // window: isOnDesktop() is exactly the flag juce::ComponentDragger itself branches on.
    ASSERT_NE(popup->getParentComponent(), nullptr);
    EXPECT_FALSE(popup->isOnDesktop());
    EXPECT_TRUE(popup->isVisible());
}

TEST(ModuleLibraryHelpPin, PinTogglesBackAndForthThroughTheHeaderButton) {
    RecordingCallOutBoxModuleLibraryComponent comp;
    auto* popup = comp.createHelpPopupForTest();
    ASSERT_FALSE(popup->isPinned());

    popup->triggerPinToggleForTest();
    EXPECT_TRUE(popup->isPinned());
    EXPECT_TRUE(comp.isHelpPopoverPinnedForTest());
    EXPECT_EQ(comp.callOutBoxLaunches, 0) << "pinning alone must never construct a callout";

    popup->triggerPinToggleForTest(); // un-pin — routes back through the (stubbed) callout launch
    EXPECT_FALSE(popup->isPinned());
    EXPECT_FALSE(comp.isHelpPopoverPinnedForTest());
    EXPECT_EQ(comp.callOutBoxLaunches, 1);
}

TEST(ModuleLibraryHelpPin, PinnedPopoverSurvivesASimulatedOutsideClick) {
    ModuleLibraryComponent comp;
    comp.setSize(200, 600);
    auto* popup = comp.createHelpPopupForTest();
    comp.setHelpPopoverPinnedForTest(true);
    ASSERT_TRUE(popup->isVisible());

    // The exact gesture that would have dismissed a juce::CallOutBox: a click far from the popup,
    // on the "canvas" side of things (here, elsewhere on the library component itself, standing in
    // for the app's ancestor chain — see floatingHelpHostFor()). A plain non-modal child has no
    // dismiss-on-outside-click wiring at all, so this must be a complete no-op for it.
    comp.mouseDown(makeMouseEventAt(comp, {5.0f, 500.0f}));
    comp.mouseUp(makeMouseEventAt(comp, {5.0f, 500.0f}));

    EXPECT_TRUE(popup->isVisible());
    EXPECT_NE(popup->getParentComponent(), nullptr);
    EXPECT_TRUE(comp.isHelpPopoverPinnedForTest());
}

TEST(ModuleLibraryHelpPin, CloseButtonHidesAndDetachesAPinnedPopover) {
    ModuleLibraryComponent comp;
    auto* popup = comp.createHelpPopupForTest();
    comp.setHelpPopoverPinnedForTest(true);
    ASSERT_NE(popup->getParentComponent(), nullptr);

    popup->triggerCloseForTest();

    EXPECT_FALSE(comp.isHelpPopoverPinnedForTest());
    EXPECT_FALSE(popup->isPinned());
    EXPECT_FALSE(popup->isVisible());
    EXPECT_EQ(popup->getParentComponent(), nullptr);
}

TEST(ModuleLibraryHelpPin, CloseButtonWithNoActiveHostIsSafe) {
    // The popup exists (created lazily) but was never shown through either host — closing it must
    // not crash, and must leave pin state clean for the next open.
    ModuleLibraryComponent comp;
    auto* popup = comp.createHelpPopupForTest();
    EXPECT_NO_THROW(popup->triggerCloseForTest());
    EXPECT_FALSE(comp.isHelpPopoverPinnedForTest());
}

TEST(ModuleLibraryHelpPin, ClosingViaTheOwnerSeamMatchesTheButton) {
    ModuleLibraryComponent comp;
    auto* popup = comp.createHelpPopupForTest();
    comp.setHelpPopoverPinnedForTest(true);

    comp.closeHelpPopoverForTest();

    EXPECT_FALSE(comp.isHelpPopoverPinnedForTest());
    EXPECT_FALSE(popup->isVisible());
}

// ============================================================================
// Content drift guards — "Your first patch" must mention every module the recipe depends on
// ============================================================================

TEST(ModuleLibraryHelpContent, UsingModulesLinesAreNonEmpty) {
    const auto lines = ModuleLibraryHelpPopup::usingModulesLines();
    EXPECT_GE(lines.size(), 3);
    for (const auto& line : lines)
        EXPECT_FALSE(line.isEmpty());
}

TEST(ModuleLibraryHelpContent, FirstPatchStepsMentionEveryRequiredModule) {
    const auto steps = ModuleLibraryHelpPopup::firstPatchSteps();
    const juce::String joined = steps.joinIntoString(" ");

    EXPECT_TRUE(joined.contains("Poly MIDI")) << "the guide must name the MIDI source";
    EXPECT_TRUE(joined.contains("Oscillator"));
    EXPECT_TRUE(joined.contains("VCA"));
    EXPECT_TRUE(joined.contains("ADSR"));
    EXPECT_TRUE(joined.contains("Audio Output"));
    EXPECT_TRUE(joined.contains("MIDI Keyboard") || joined.contains("piano roll"))
        << "the guide must say what feeds the MIDI source";
}

TEST(ModuleLibraryHelpContent, FirstPatchStepsAreNumberedInOrder) {
    const auto steps = ModuleLibraryHelpPopup::firstPatchSteps();
    ASSERT_GE(steps.size(), 5);
    for (int i = 0; i < steps.size(); ++i)
        EXPECT_TRUE(steps[i].startsWith(juce::String(i + 1) + ".")) << steps[i].toStdString();
}

// ============================================================================
// Key shortcuts — resolved LIVE from ShortcutManager, never staled by a rebind
// ============================================================================

TEST(ModuleLibraryHelpContent, ShortcutLinesCountIsWithinTheCuratedRange) {
    const auto lines = ModuleLibraryHelpPopup::shortcutLines(nullptr);
    EXPECT_GE(lines.size(), 8);
    EXPECT_LE(lines.size(), 12);
}

TEST(ModuleLibraryHelpContent, ShortcutLinesWithNoManagerUseTheDocumentedDefaults) {
    const auto cmdZ =
        ShortcutManager::keyPressToDisplayString(juce::KeyPress('z', juce::ModifierKeys::commandModifier, 0));
    const auto lines = ModuleLibraryHelpPopup::shortcutLines(nullptr);
    const juce::String joined = lines.joinIntoString(" | ");
    EXPECT_TRUE(joined.contains("Undo"));
    EXPECT_TRUE(joined.contains(cmdZ));
    EXPECT_TRUE(joined.contains("Redo"));
    EXPECT_TRUE(joined.contains("Toggle Playback"));
    EXPECT_TRUE(joined.contains("Space"));
    EXPECT_TRUE(joined.contains("Toggle Snap"));
}

TEST(ModuleLibraryHelpContent, RebindingAShortcutChangesItsLineImmediately) {
    ShortcutManager manager;
    const auto before = ModuleLibraryHelpPopup::shortcutLines(&manager);

    // "undo" is a curated entry — found by its description rather than a hard-coded index, so this
    // test survives the curated list being reordered.
    int undoIndex = -1;
    for (int i = 0; i < before.size(); ++i)
        if (before[i].startsWith(ShortcutManager::getActionDescription("undo")))
            undoIndex = i;
    const auto cmdZ =
        ShortcutManager::keyPressToDisplayString(juce::KeyPress('z', juce::ModifierKeys::commandModifier, 0));
    const auto cmdU =
        ShortcutManager::keyPressToDisplayString(juce::KeyPress('u', juce::ModifierKeys::commandModifier, 0));
    ASSERT_GE(undoIndex, 0);
    EXPECT_TRUE(before[undoIndex].contains(cmdZ));

    manager.setBinding("undo", juce::KeyPress('u', juce::ModifierKeys::commandModifier, 0));
    const auto after = ModuleLibraryHelpPopup::shortcutLines(&manager);

    EXPECT_TRUE(after[undoIndex].contains(cmdU));
    EXPECT_FALSE(after[undoIndex].contains(cmdZ));
}

TEST(ModuleLibraryHelpContent, ClearingAShortcutDropsItsKeyButKeepsTheLabel) {
    ShortcutManager manager;
    manager.setBinding("timelineSnapToggle", juce::KeyPress());
    const auto lines = ModuleLibraryHelpPopup::shortcutLines(&manager);

    bool found = false;
    for (const auto& line : lines) {
        if (line.startsWith(ShortcutManager::getActionDescription("timelineSnapToggle"))) {
            found = true;
            EXPECT_EQ(line, ShortcutManager::getActionDescription("timelineSnapToggle"))
                << "a cleared binding must show the label with no trailing key text";
        }
    }
    EXPECT_TRUE(found);
}
