// ShortcutsSettingsTabTests.cpp
// Unit tests for ShortcutsSettingsTab: row count, description text, binding display
// strings, paint smoke, and programmatic rebind via startListeningForTest.
//
// NOTE: ShortcutsSettingsTab.cpp must be added to BOTH the app target (AgentSynth)
// and the test target (Tests) in the root CMakeLists.txt and
// Tests/CMakeLists.txt respectively before these tests will link.

#include "../Source/ShortcutManager.h"
#include "../Source/UI/ShortcutsSettingsTab.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

class ShortcutsSettingsTabTest : public ::testing::Test {
protected:
    void SetUp() override {
        // ShortcutManager::resetToDefaults() is called in its constructor.
        tab = std::make_unique<ShortcutsSettingsTab>(manager);
        tab->setSize(600, 500);
    }

    ShortcutManager manager;
    std::unique_ptr<ShortcutsSettingsTab> tab;
};

// ---------------------------------------------------------------------------
// Row count
// ---------------------------------------------------------------------------

TEST_F(ShortcutsSettingsTabTest, RowCountMatchesActionIds) {
    // The tab must produce exactly one row per action registered in ShortcutManager.
    int expectedCount = static_cast<int>(manager.getActionIds().size());
    EXPECT_EQ(tab->getShortcutCount(), expectedCount);
    EXPECT_GT(tab->getShortcutCount(), 0); // sanity: at least one action exists
}

// ---------------------------------------------------------------------------
// Descriptions
// ---------------------------------------------------------------------------

TEST_F(ShortcutsSettingsTabTest, RowDescriptionsMatchActionDescriptions) {
    const auto& ids = manager.getActionIds();
    for (int i = 0; i < static_cast<int>(ids.size()); ++i) {
        juce::String expected = ShortcutManager::getActionDescription(ids[i]);
        EXPECT_EQ(tab->getRowDescription(i), expected)
            << "Row " << i << " description mismatch for action: " << ids[i].toStdString();
    }
}

TEST_F(ShortcutsSettingsTabTest, OutOfRangeDescriptionReturnsEmpty) {
    EXPECT_EQ(tab->getRowDescription(-1), juce::String{});
    EXPECT_EQ(tab->getRowDescription(tab->getShortcutCount()), juce::String{});
}

// ---------------------------------------------------------------------------
// Binding display strings
// ---------------------------------------------------------------------------

TEST_F(ShortcutsSettingsTabTest, RowBindingTextsMatchCurrentBindings) {
    const auto& ids = manager.getActionIds();
    for (int i = 0; i < static_cast<int>(ids.size()); ++i) {
        juce::String expected = ShortcutManager::keyPressToDisplayString(manager.getBinding(ids[i]));
        EXPECT_EQ(tab->getRowBindingText(i), expected)
            << "Row " << i << " binding text mismatch for action: " << ids[i].toStdString();
    }
}

TEST_F(ShortcutsSettingsTabTest, OutOfRangeBindingTextReturnsEmpty) {
    EXPECT_EQ(tab->getRowBindingText(-1), juce::String{});
    EXPECT_EQ(tab->getRowBindingText(tab->getShortcutCount()), juce::String{});
}

// ---------------------------------------------------------------------------
// Paint smoke test
// ---------------------------------------------------------------------------

TEST_F(ShortcutsSettingsTabTest, PaintDoesNotCrash) {
    juce::Image img(juce::Image::ARGB, 600, 500, true);
    juce::Graphics g(img);
    EXPECT_NO_THROW(tab->paint(g));
}

// ---------------------------------------------------------------------------
// Programmatic rebind
// ---------------------------------------------------------------------------

TEST_F(ShortcutsSettingsTabTest, StartListeningEntersListeningMode) {
    // After startListeningForTest, the button text should change to "Press a key..."
    ASSERT_GT(tab->getShortcutCount(), 0);
    tab->startListeningForTest(0);
    EXPECT_EQ(tab->getRowBindingText(0), "Press a key...");
}

TEST_F(ShortcutsSettingsTabTest, KeyPressWhileListeningUpdatesBinding) {
    // Enter listening mode on row 0, then simulate a key press.
    // The binding should be updated and the button text should reflect the new key.
    ASSERT_GT(tab->getShortcutCount(), 0);
    tab->startListeningForTest(0);

    // Press Cmd+K (unlikely to conflict with row 0's default ',')
    juce::KeyPress newKey('k', juce::ModifierKeys::commandModifier, 0);
    tab->keyPressed(newKey);

    juce::String expected = ShortcutManager::keyPressToDisplayString(newKey);
    EXPECT_EQ(tab->getRowBindingText(0), expected);

    // The ShortcutManager should also reflect the change
    const auto& ids = manager.getActionIds();
    EXPECT_EQ(manager.getBinding(ids[0]).getKeyCode(), 'k');
}

TEST_F(ShortcutsSettingsTabTest, EscapeCancelsListening) {
    ASSERT_GT(tab->getShortcutCount(), 0);
    // Record the original binding text
    juce::String original = tab->getRowBindingText(0);

    tab->startListeningForTest(0);
    EXPECT_EQ(tab->getRowBindingText(0), "Press a key..."); // in listening mode

    // Press Escape — should restore the original text
    tab->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
    EXPECT_EQ(tab->getRowBindingText(0), original);
}

TEST_F(ShortcutsSettingsTabTest, ResizingDoesNotCrash) {
    EXPECT_NO_THROW(tab->setSize(400, 300));
    EXPECT_NO_THROW(tab->resized());
    EXPECT_NO_THROW(tab->setSize(800, 600));
    EXPECT_NO_THROW(tab->resized());
}
