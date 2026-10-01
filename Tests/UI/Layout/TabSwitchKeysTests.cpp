// TabSwitchKeysTests.cpp -- the listener that offers tab-switching keys to a tab handler ahead of a text
// field's own key handling.
#include "UI/Layout/TabSwitchKeys.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

namespace {
using synth::ui::TabSwitchKeys;

// What ComponentPeer::handleKeyPress does for a key press with `focused` holding focus, for the one
// listener under test.
bool deliver(TabSwitchKeys& keys, juce::Component& focused, const juce::KeyPress& key) {
    for (auto* target = &focused; target != nullptr; target = target->getParentComponent()) {
        if (keys.isAttachedForTest(target) && keys.keyPressed(key, target))
            return true;
        if (target->keyPressed(key))
            return true;
    }
    return false;
}

juce::KeyPress cmdOne() { return juce::KeyPress('1', juce::ModifierKeys(juce::ModifierKeys::commandModifier), '1'); }
} // namespace

// Without the listener a text field types the digit of Cmd+1 and consumes the key, so the surface never
// sees it; with it the surface gets the key first and the field's text is untouched.
TEST(TabSwitchKeysTest, ATextFieldOffersTheKeyToTheHandlerBeforeTypingIt) {
    juce::Component surface;
    juce::TextEditor editor;
    surface.addAndMakeVisible(editor);

    int handled = 0;
    TabSwitchKeys keys([&handled](const juce::KeyPress& key) {
        const bool tabKey = TabSwitchKeys::positionalTabIndex(key) >= 0;
        handled += tabKey ? 1 : 0;
        return tabKey;
    });

    EXPECT_TRUE(deliver(keys, editor, cmdOne())) << "unattached, the editor itself consumes it";
    EXPECT_EQ(handled, 0);
    EXPECT_EQ(editor.getText(), "1") << "the bare field types the digit";
    editor.clear();

    keys.attachToTextEditorsIn(surface);
    EXPECT_TRUE(keys.isAttachedForTest(&editor));
    EXPECT_TRUE(deliver(keys, editor, cmdOne()));
    EXPECT_EQ(handled, 1);
    EXPECT_EQ(editor.getText(), "") << "the handler took the key before the field typed it";
}

TEST(TabSwitchKeysTest, KeysTheHandlerDeclinesReachTheFieldUntouched) {
    juce::TextEditor editor;
    TabSwitchKeys keys([](const juce::KeyPress&) { return false; });
    keys.attachTo(editor);
    EXPECT_TRUE(deliver(keys, editor, juce::KeyPress('a', juce::ModifierKeys(), 'a')));
    EXPECT_EQ(editor.getText(), "a");
}

TEST(TabSwitchKeysTest, AttachingTwiceIsHarmlessAndEitherSideMayDieFirst) {
    juce::TextEditor first;
    TabSwitchKeys keys([](const juce::KeyPress&) { return false; });
    keys.attachTo(first);
    keys.attachTo(first);
    EXPECT_TRUE(keys.isAttachedForTest(&first));

    {
        auto second = std::make_unique<juce::TextEditor>();
        keys.attachTo(*second);
        EXPECT_TRUE(keys.isAttachedForTest(second.get()));
        // The field dies while the listener lives on: the listener must cope.
    }

    {
        // The listener dies first: it detaches itself from the field that lives on.
        TabSwitchKeys shortLived([](const juce::KeyPress&) { return true; });
        shortLived.attachTo(first);
    }
    EXPECT_TRUE(deliver(keys, first, juce::KeyPress('a', juce::ModifierKeys(), 'a')));
    EXPECT_EQ(first.getText(), "a") << "only the surviving listener remains, and it declines";
}
