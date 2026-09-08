// ModuleLibraryKeyboardNavTests.cpp
// T160: keyboard row navigation within the module library sidebar.
//   • Up/Down walk visible navigable rows (draggable + action + header/subheader), clamped at ends
//   • Left/Right fold/expand a focused (Sub)Header, no-op on any other focused row kind
//   • Enter-to-insert dispatches onModuleActivated/onSnippetActivated (new — no prior click path),
//     and still dispatches onScanPluginsRequested/onPluginActivated via the pre-existing activateRow
//   • keyboardFocusedIndex is clamped to the visible set exactly like hoveredIndex
//   • the searchEditor KeyListener path: Up/Down/Return (once focused) are intercepted; Left/Right/
//     Tab are deliberately left alone

#include "../Source/UI/ModuleLibraryComponent.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace {

constexpr int kPanelWidth = 200;
constexpr int kTallPanelHeight = 2000; // tall enough that every section's rows are on screen at once

juce::KeyPress upKey() { return juce::KeyPress(juce::KeyPress::upKey, juce::ModifierKeys::noModifiers, 0); }
juce::KeyPress downKey() { return juce::KeyPress(juce::KeyPress::downKey, juce::ModifierKeys::noModifiers, 0); }
juce::KeyPress leftKey() { return juce::KeyPress(juce::KeyPress::leftKey, juce::ModifierKeys::noModifiers, 0); }
juce::KeyPress rightKey() { return juce::KeyPress(juce::KeyPress::rightKey, juce::ModifierKeys::noModifiers, 0); }
juce::KeyPress enterKey() { return juce::KeyPress(juce::KeyPress::returnKey, juce::ModifierKeys::noModifiers, 0); }
juce::KeyPress tabKey() { return juce::KeyPress(juce::KeyPress::tabKey, juce::ModifierKeys::noModifiers, 0); }

int findEntryIndexForText(const ModuleLibraryComponent& comp, const juce::String& text) {
    for (int i = 0; i < comp.getEntryCount(); ++i)
        if (comp.getEntryText(i) == text)
            return i;
    return -1;
}

} // namespace

// ============================================================================
// Up/Down traversal
// ============================================================================

TEST(ModuleLibraryKeyboardNav, DownFromNothingFocusedLandsOnTheFirstVisibleRow) {
    ModuleLibraryComponent comp;
    comp.setSize(kPanelWidth, kTallPanelHeight);
    ASSERT_EQ(comp.getKeyboardFocusedIndex(), -1);

    EXPECT_TRUE(comp.keyPressed(downKey()));
    // The first row is always the Snippets header — Up/Down includes headers (see
    // isKeyboardNavigableEntry), so this is deterministic regardless of catalogue changes.
    EXPECT_EQ(comp.getKeyboardFocusedIndex(), 0);
    EXPECT_EQ(comp.getEntry(0).kind, ModuleLibraryComponent::RowKind::Header);
}

TEST(ModuleLibraryKeyboardNav, UpFromNothingFocusedLandsOnTheLastVisibleRow) {
    ModuleLibraryComponent comp;
    comp.setSize(kPanelWidth, kTallPanelHeight);

    EXPECT_TRUE(comp.keyPressed(upKey()));
    EXPECT_GE(comp.getKeyboardFocusedIndex(), 0);
    // The very last row today is the "Scan for plugins..." Action row (Plugins is the last
    // section and starts out empty) — assert via getVisibleRowCount()'s own row list instead of a
    // hardcoded index, so a future catalogue change can't silently make this assert nothing real.
    EXPECT_EQ(comp.getEntry(comp.getKeyboardFocusedIndex()).text, ModuleLibraryComponent::kScanPluginsRowText);
}

TEST(ModuleLibraryKeyboardNav, DownWalksForwardThroughEntries) {
    ModuleLibraryComponent comp;
    comp.setSize(kPanelWidth, kTallPanelHeight);

    comp.keyPressed(downKey());
    const int first = comp.getKeyboardFocusedIndex();
    comp.keyPressed(downKey());
    const int second = comp.getKeyboardFocusedIndex();
    EXPECT_GT(second, first);
}

TEST(ModuleLibraryKeyboardNav, DownClampsAtTheLastRowRatherThanWrapping) {
    ModuleLibraryComponent comp;
    comp.setSize(kPanelWidth, kTallPanelHeight);

    for (int i = 0; i < comp.getEntryCount() + 5; ++i)
        comp.keyPressed(downKey());

    const int last = comp.getKeyboardFocusedIndex();
    EXPECT_TRUE(comp.keyPressed(downKey())); // still "handled" — just doesn't move further
    EXPECT_EQ(comp.getKeyboardFocusedIndex(), last);
}

TEST(ModuleLibraryKeyboardNav, UpClampsAtTheFirstRowRatherThanWrapping) {
    ModuleLibraryComponent comp;
    comp.setSize(kPanelWidth, kTallPanelHeight);

    for (int i = 0; i < comp.getEntryCount() + 5; ++i)
        comp.keyPressed(upKey());

    EXPECT_EQ(comp.getKeyboardFocusedIndex(), 0);
}

TEST(ModuleLibraryKeyboardNav, NavigationSkipsCollapsedRows) {
    ModuleLibraryComponent comp;
    comp.setSize(kPanelWidth, kTallPanelHeight);
    comp.setSectionCollapsed("Sources", true);

    const int oscillator = findEntryIndexForText(comp, "Oscillator");
    ASSERT_GE(oscillator, 0);
    comp.finishCollapseAnimation();

    // Walk from the top: Oscillator's row is now hidden (Sources is folded), so keyboard nav must
    // never land on it.
    for (int i = 0; i < comp.getEntryCount(); ++i) {
        comp.keyPressed(downKey());
        EXPECT_NE(comp.getKeyboardFocusedIndex(), oscillator);
    }
}

// ============================================================================
// Left/Right — fold/expand a focused (Sub)Header, no-op on anything else
// ============================================================================

TEST(ModuleLibraryKeyboardNav, RightExpandsAFocusedCollapsedHeader) {
    ModuleLibraryComponent comp;
    comp.setSize(kPanelWidth, kTallPanelHeight);
    comp.setSectionCollapsed("Sources", true);
    comp.finishCollapseAnimation();

    const int sourcesHeader = findEntryIndexForText(comp, "Sources");
    ASSERT_GE(sourcesHeader, 0);
    comp.setKeyboardFocusedIndexForTest(sourcesHeader);
    ASSERT_TRUE(comp.isSectionCollapsed("Sources"));

    EXPECT_TRUE(comp.keyPressed(rightKey()));
    EXPECT_FALSE(comp.isSectionCollapsed("Sources"));
}

TEST(ModuleLibraryKeyboardNav, LeftCollapsesAFocusedOpenHeader) {
    ModuleLibraryComponent comp;
    comp.setSize(kPanelWidth, kTallPanelHeight);

    const int sourcesHeader = findEntryIndexForText(comp, "Sources");
    ASSERT_GE(sourcesHeader, 0);
    comp.setKeyboardFocusedIndexForTest(sourcesHeader);
    ASSERT_FALSE(comp.isSectionCollapsed("Sources"));

    EXPECT_TRUE(comp.keyPressed(leftKey()));
    EXPECT_TRUE(comp.isSectionCollapsed("Sources"));
}

TEST(ModuleLibraryKeyboardNav, LeftRightIsANoOpOnAFocusedChildRow) {
    ModuleLibraryComponent comp;
    comp.setSize(kPanelWidth, kTallPanelHeight);

    const int oscillator = findEntryIndexForText(comp, "Oscillator");
    ASSERT_GE(oscillator, 0);
    comp.setKeyboardFocusedIndexForTest(oscillator);

    // Locked decision: no-op, not a bubble — returns false, and nothing about the module row or
    // its section's collapse state changes.
    EXPECT_FALSE(comp.keyPressed(leftKey()));
    EXPECT_FALSE(comp.keyPressed(rightKey()));
    EXPECT_FALSE(comp.isSectionCollapsed("Sources"));
    EXPECT_EQ(comp.getKeyboardFocusedIndex(), oscillator);
}

TEST(ModuleLibraryKeyboardNav, LeftRightIsANoOpWithNothingFocused) {
    ModuleLibraryComponent comp;
    comp.setSize(kPanelWidth, kTallPanelHeight);
    ASSERT_EQ(comp.getKeyboardFocusedIndex(), -1);

    EXPECT_FALSE(comp.keyPressed(leftKey()));
    EXPECT_FALSE(comp.keyPressed(rightKey()));
}

// ============================================================================
// Enter-to-insert
// ============================================================================

TEST(ModuleLibraryKeyboardNav, EnterOnAModuleRowFiresOnModuleActivated) {
    ModuleLibraryComponent comp;
    comp.setSize(kPanelWidth, kTallPanelHeight);

    juce::String activated;
    comp.onModuleActivated = [&activated](const juce::String& name) { activated = name; };

    const int oscillator = findEntryIndexForText(comp, "Oscillator");
    ASSERT_GE(oscillator, 0);
    comp.setKeyboardFocusedIndexForTest(oscillator);

    EXPECT_TRUE(comp.keyPressed(enterKey()));
    EXPECT_EQ(activated, "Oscillator");
}

TEST(ModuleLibraryKeyboardNav, EnterOnADisabledModuleRowDoesNotFire) {
    ModuleLibraryComponent comp;
    comp.setSize(kPanelWidth, kTallPanelHeight);
    comp.isModuleAvailable = [](const juce::String&) { return false; };

    bool fired = false;
    comp.onModuleActivated = [&fired](const juce::String&) { fired = true; };

    const int oscillator = findEntryIndexForText(comp, "Oscillator");
    comp.setKeyboardFocusedIndexForTest(oscillator);

    EXPECT_TRUE(comp.keyPressed(enterKey())); // Enter is still "handled" — it just inserts nothing
    EXPECT_FALSE(fired);
}

TEST(ModuleLibraryKeyboardNav, EnterOnASnippetRowFiresOnSnippetActivated) {
    ModuleLibraryComponent comp;
    comp.setSize(kPanelWidth, kTallPanelHeight);
    synth::SnippetInfo snippet;
    snippet.name = "My Snippet";
    snippet.moduleCount = 2;
    comp.setSnippets({snippet});

    juce::String activated;
    comp.onSnippetActivated = [&activated](const juce::String& name) { activated = name; };

    const int row = findEntryIndexForText(comp, "My Snippet");
    ASSERT_GE(row, 0);
    comp.setKeyboardFocusedIndexForTest(row);

    EXPECT_TRUE(comp.keyPressed(enterKey()));
    EXPECT_EQ(activated, "My Snippet");
}

TEST(ModuleLibraryKeyboardNav, EnterOnTheScanActionRowStillFiresOnScanPluginsRequested) {
    ModuleLibraryComponent comp;
    comp.setSize(kPanelWidth, kTallPanelHeight);

    bool scanRequested = false;
    comp.onScanPluginsRequested = [&scanRequested] { scanRequested = true; };

    const int row = findEntryIndexForText(comp, ModuleLibraryComponent::kScanPluginsRowText);
    ASSERT_GE(row, 0);
    comp.setKeyboardFocusedIndexForTest(row);

    EXPECT_TRUE(comp.keyPressed(enterKey()));
    EXPECT_TRUE(scanRequested);
}

TEST(ModuleLibraryKeyboardNav, EnterWithNothingFocusedIsANoOp) {
    ModuleLibraryComponent comp;
    comp.setSize(kPanelWidth, kTallPanelHeight);
    bool fired = false;
    comp.onModuleActivated = [&fired](const juce::String&) { fired = true; };

    EXPECT_FALSE(comp.keyPressed(enterKey()));
    EXPECT_FALSE(fired);
}

// ============================================================================
// Clamping — keyboardFocusedIndex tracks hoveredIndex's own invalidation sites
// ============================================================================

TEST(ModuleLibraryKeyboardNav, FocusIsClampedWhenItsSectionCollapses) {
    ModuleLibraryComponent comp;
    comp.setSize(kPanelWidth, kTallPanelHeight);

    const int oscillator = findEntryIndexForText(comp, "Oscillator");
    comp.setKeyboardFocusedIndexForTest(oscillator);
    ASSERT_EQ(comp.getKeyboardFocusedIndex(), oscillator);

    comp.setSectionCollapsed("Sources", true);
    comp.finishCollapseAnimation();
    EXPECT_EQ(comp.getKeyboardFocusedIndex(), -1);
}

TEST(ModuleLibraryKeyboardNav, FocusIsClampedWhenASnippetItPointsAtIsRemoved) {
    ModuleLibraryComponent comp;
    comp.setSize(kPanelWidth, kTallPanelHeight);
    synth::SnippetInfo snippet;
    snippet.name = "Gone Soon";
    comp.setSnippets({snippet});

    const int row = findEntryIndexForText(comp, "Gone Soon");
    comp.setKeyboardFocusedIndexForTest(row);
    ASSERT_EQ(comp.getKeyboardFocusedIndex(), row);

    comp.setSnippets({});
    EXPECT_EQ(comp.getKeyboardFocusedIndex(), -1);
}

// ============================================================================
// The searchEditor KeyListener path (Cmd+F destination) — see
// simulateSearchFieldKeyPressForTest's own comment for why this can't be driven through a real
// focus grab headlessly.
// ============================================================================

TEST(ModuleLibraryKeyboardNav, SearchFieldDownFocusesTheFirstRowJustLikeTheListItself) {
    ModuleLibraryComponent comp;
    comp.setSize(kPanelWidth, kTallPanelHeight);

    EXPECT_TRUE(comp.simulateSearchFieldKeyPressForTest(downKey()));
    EXPECT_EQ(comp.getKeyboardFocusedIndex(), 0);
}

TEST(ModuleLibraryKeyboardNav, SearchFieldEnterActivatesOnceARowIsFocused) {
    ModuleLibraryComponent comp;
    comp.setSize(kPanelWidth, kTallPanelHeight);

    juce::String activated;
    comp.onModuleActivated = [&activated](const juce::String& name) { activated = name; };

    const int oscillator = findEntryIndexForText(comp, "Oscillator");
    comp.setKeyboardFocusedIndexForTest(oscillator);

    EXPECT_TRUE(comp.simulateSearchFieldKeyPressForTest(enterKey()));
    EXPECT_EQ(activated, "Oscillator");
}

TEST(ModuleLibraryKeyboardNav, SearchFieldEnterWithNothingFocusedIsLeftForTheEditorItself) {
    ModuleLibraryComponent comp;
    comp.setSize(kPanelWidth, kTallPanelHeight);
    ASSERT_EQ(comp.getKeyboardFocusedIndex(), -1);

    // Not yet navigating — Return must fall through so TextEditor keeps its normal (consumed,
    // no-op) behaviour rather than this component eating a keystroke with nothing to insert.
    EXPECT_FALSE(comp.simulateSearchFieldKeyPressForTest(enterKey()));
}

TEST(ModuleLibraryKeyboardNav, SearchFieldLeftRightAreNeverIntercepted) {
    ModuleLibraryComponent comp;
    comp.setSize(kPanelWidth, kTallPanelHeight);

    const int sourcesHeader = findEntryIndexForText(comp, "Sources");
    comp.setKeyboardFocusedIndexForTest(sourcesHeader);

    // Even with a header keyboard-focused, Left/Right from the search field must stay with the
    // text caret — folding a section from inside an active text edit would be a surprising side
    // effect of typing.
    EXPECT_FALSE(comp.simulateSearchFieldKeyPressForTest(leftKey()));
    EXPECT_FALSE(comp.simulateSearchFieldKeyPressForTest(rightKey()));
    EXPECT_FALSE(comp.isSectionCollapsed("Sources"));
}

TEST(ModuleLibraryKeyboardNav, SearchFieldTabIsNeverIntercepted) {
    ModuleLibraryComponent comp;
    comp.setSize(kPanelWidth, kTallPanelHeight);

    // Must stay false so Tab keeps bubbling to MainComponent's focusNextRegion cycle — see the
    // class comment on the KeyListener override for why no special handling is needed to achieve
    // this (TextEditor itself already never consumes Tab).
    EXPECT_FALSE(comp.simulateSearchFieldKeyPressForTest(tabKey()));
}
