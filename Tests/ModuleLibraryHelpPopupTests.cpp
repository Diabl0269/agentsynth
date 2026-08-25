// ModuleLibraryHelpPopupTests.cpp
//
// Headless coverage for the module library sidebar's "?" help button (ModuleLibraryComponent) and
// its popover content (synth::ui::ModuleLibraryHelpPopup) — no juce::CallOutBox is ever launched.
// A real CallOutBox is a top-level window and crashes a display-less test runner exactly the way
// docs/timeline_panel_core.md's marker-context-menu seam note describes for juce::PopupMenu, so
// every test here drives either the pure content helpers, the mouse-hover/geometry seam, or
// createHelpPopupForTest() / a showHelpPopover() override — never the real launch path. Mirrors
// MidiDestinationPickerTests.cpp's "talk to the component directly" approach.

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
    auto content = comp.createHelpPopupForTest();
    ASSERT_NE(content, nullptr);

    auto* popup = dynamic_cast<ModuleLibraryHelpPopup*>(content.get());
    EXPECT_NE(popup, nullptr) << "the seam must hand back the same component type the real button opens";
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

    auto content = comp.createHelpPopupForTest();
    auto* popup = dynamic_cast<ModuleLibraryHelpPopup*>(content.get());
    ASSERT_NE(popup, nullptr);

    const auto lines = popup->getSectionLinesForTest(ModuleLibraryHelpPopup::KeyShortcuts);
    const juce::String joined = lines.joinIntoString(" | ");
    EXPECT_TRUE(joined.contains("Cmd + U"));
    EXPECT_FALSE(joined.contains("Cmd + Z"));
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
    const auto lines = ModuleLibraryHelpPopup::shortcutLines(nullptr);
    const juce::String joined = lines.joinIntoString(" | ");
    EXPECT_TRUE(joined.contains("Undo"));
    EXPECT_TRUE(joined.contains("Cmd + Z"));
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
    ASSERT_GE(undoIndex, 0);
    EXPECT_TRUE(before[undoIndex].contains("Cmd + Z"));

    manager.setBinding("undo", juce::KeyPress('u', juce::ModifierKeys::commandModifier, 0));
    const auto after = ModuleLibraryHelpPopup::shortcutLines(&manager);

    EXPECT_TRUE(after[undoIndex].contains("Cmd + U"));
    EXPECT_FALSE(after[undoIndex].contains("Cmd + Z"));
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
