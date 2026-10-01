// ModuleLibraryAccessibilityTests.cpp -- what a screen reader hears from the module library: the
// wording builders (Source/UI/Library/ModuleLibraryAccessibilityText.h) and the list handler whose
// value follows the keyboard-focused row. The native peer does not exist headlessly, so the handler
// is built directly with createAccessibilityHandler().
#include "UI/Library/ModuleLibraryAccessibilityText.h"
#include "UI/Library/ModuleLibraryComponent/ModuleLibraryComponent.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

using synth::ui::describeLibraryForAccessibility;
using synth::ui::describeLibraryRowForAccessibility;
using synth::ui::describeLibrarySectionForAccessibility;

namespace {

int indexOfText(const ModuleLibraryComponent& library, const juce::String& text) {
    for (int i = 0; i < library.getEntryCount(); ++i)
        if (library.getEntryText(i) == text)
            return i;
    return -1;
}

} // namespace

TEST(ModuleLibraryAccessibilityTextTest, RowReadsNamePositionAndCategory) {
    EXPECT_EQ(describeLibraryRowForAccessibility("Oscillator", 3, 12, "Sources"), "Oscillator, 3 of 12, Sources");
    EXPECT_EQ(describeLibraryRowForAccessibility("Oscillator", 1, 1, {}), "Oscillator, 1 of 1");
}

TEST(ModuleLibraryAccessibilityTextTest, SectionReadsStateAndItemCount) {
    EXPECT_EQ(describeLibrarySectionForAccessibility("Sources", false, 4), "Sources, section, expanded, 4 items");
    EXPECT_EQ(describeLibrarySectionForAccessibility("Snippets", true, 1), "Snippets, section, collapsed, 1 item");
}

TEST(ModuleLibraryAccessibilityTextTest, WholeLibraryReadsTheRowCount) {
    EXPECT_EQ(describeLibraryForAccessibility(12), "12 items");
    EXPECT_EQ(describeLibraryForAccessibility(1), "1 item");
}

TEST(ModuleLibraryAccessibilityTest, IsANamedListWithATooltip) {
    ModuleLibraryComponent library;
    library.setSize(240, 2000);
    EXPECT_EQ(library.getTitle(), "Module library");
    EXPECT_TRUE(library.getTooltip().isNotEmpty());

    const auto handler = library.createAccessibilityHandler();
    ASSERT_NE(handler, nullptr);
    EXPECT_EQ(handler->getRole(), juce::AccessibilityRole::list);
    ASSERT_NE(handler->getValueInterface(), nullptr);
}

TEST(ModuleLibraryAccessibilityTest, ValueFollowsTheKeyboardFocusedRow) {
    ModuleLibraryComponent library;
    library.setSize(240, 2000);
    const auto handler = library.createAccessibilityHandler();
    auto* value = handler->getValueInterface();
    ASSERT_NE(value, nullptr);
    EXPECT_TRUE(value->isReadOnly());

    const int oscillator = indexOfText(library, "Oscillator");
    ASSERT_GE(oscillator, 0);
    library.setKeyboardFocusedIndexForTest(oscillator);

    const auto text = value->getCurrentValueAsString();
    const auto category = library.getSectionForModule("Oscillator");
    EXPECT_TRUE(text.startsWith("Oscillator, ")) << text;
    EXPECT_TRUE(text.endsWith(", " + category)) << text;
    EXPECT_TRUE(text.contains(" of ")) << text;

    library.setKeyboardFocusedIndexForTest(-1);
    EXPECT_EQ(value->getCurrentValueAsString(), library.getAccessibilityValueText());
    EXPECT_TRUE(value->getCurrentValueAsString().endsWith(" items"));
}

TEST(ModuleLibraryAccessibilityTest, PositionCountsTheRowsOfTheFocusedRowsSectionOnly) {
    ModuleLibraryComponent library;
    library.setSize(240, 2000);
    const auto category = library.getSectionForModule("Oscillator");
    int inSection = 0;
    int position = 0;
    for (const auto& name : library.getDraggableModuleNames()) {
        if (library.getSectionForModule(name) != category)
            continue;
        ++inSection;
        if (name == "Oscillator")
            position = inSection;
    }
    library.setKeyboardFocusedIndexForTest(indexOfText(library, "Oscillator"));
    EXPECT_EQ(library.getAccessibilityValueText(),
              describeLibraryRowForAccessibility("Oscillator", position, inSection, category));
}

TEST(ModuleLibraryAccessibilityTest, FocusedHeaderReadsItsFoldState) {
    ModuleLibraryComponent library;
    library.setSize(240, 2000);
    const auto category = library.getSectionForModule("Oscillator");
    library.setKeyboardFocusedIndexForTest(indexOfText(library, category));
    EXPECT_TRUE(library.getAccessibilityValueText().startsWith(category + ", section, expanded, "))
        << library.getAccessibilityValueText();

    library.setSectionCollapsed(category, true);
    library.finishCollapseAnimation();
    EXPECT_TRUE(library.getAccessibilityValueText().startsWith(category + ", section, collapsed, "))
        << library.getAccessibilityValueText();
}

TEST(ModuleLibraryAccessibilityTest, SearchFieldIsNamed) {
    ModuleLibraryComponent library;
    library.setSize(240, 600);
    bool found = false;
    for (auto* child : library.getChildren())
        if (auto* editor = dynamic_cast<juce::TextEditor*>(child)) {
            found = true;
            EXPECT_EQ(editor->getTitle(), "Search library");
        }
    EXPECT_TRUE(found);
}
