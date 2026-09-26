// ControllerSurfacePageStripTests.cpp -- FRO142 (docs/control/midi-remote.md#pages,
// docs/control/midi-remote-ui.md#pages): headless tests for ControllerSurfacePageStrip, driven
// through its REAL mouseDown()/mouseUp() button path with synthesized juce::MouseEvents (the "test
// the real mouse path" convention, Tests/UI/MidiRemote/ControllerSurfaceSelectionTests.cpp's
// template). The right-click menu goes through a free-function test hook, same reason and same
// shape as ControllersListTests.cpp's own contextMenuHookForTest.

#include "UI/MidiRemote/ControllerSurface/ControllerSurfacePageStrip.h"

#include <functional>
#include <gtest/gtest.h>

namespace synth::ui::test_hooks {
std::function<void(juce::PopupMenu&)>& pageStripContextMenuHookForTest();
} // namespace synth::ui::test_hooks

using synth::ui::ControllerSurfacePageStrip;

namespace {

juce::MouseEvent mouseEventAt(juce::Component& comp, juce::Point<float> pos, juce::ModifierKeys mods) {
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), pos, mods, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                            &comp, &comp, juce::Time::getCurrentTime(), pos, juce::Time::getCurrentTime(), 1, false);
}

// juce::Button narrows mouseDown/mouseUp to protected -- call them through the juce::Component
// base (whose overrides are public), same as every other "test the real mouse path" suite that
// drives a component through its base-class mouse entry points.
void click(juce::Button& button, juce::ModifierKeys mods = juce::ModifierKeys()) {
    auto& comp = static_cast<juce::Component&>(button);
    const auto pos = comp.getLocalBounds().getCentre().toFloat();
    comp.mouseDown(mouseEventAt(comp, pos, mods));
    comp.mouseUp(mouseEventAt(comp, pos, mods));
}

// PopupMenu::Item::action isn't publicly iterable without an Iterator -- mirrors
// ControllersListTests.cpp's own "find by text, run its action" helper.
void chooseMenuItem(juce::PopupMenu& menu, const juce::String& text) {
    for (juce::PopupMenu::MenuItemIterator it(menu); it.next();) {
        if (it.getItem().text == text) {
            it.getItem().action();
            return;
        }
    }
    FAIL() << "no menu item named " << text.toStdString();
}

class ControllerSurfacePageStripTest : public ::testing::Test {
protected:
    void TearDown() override { synth::ui::test_hooks::pageStripContextMenuHookForTest() = nullptr; }

    ControllerSurfacePageStrip strip_;
};

} // namespace

TEST_F(ControllerSurfacePageStripTest, ShowsOneButtonAndAddEvenWithASinglePage) {
    strip_.setSize(200, ControllerSurfacePageStrip::kHeight);
    strip_.setPages(1, 1);
    EXPECT_NE(strip_.getPageButtonForTest(1), nullptr);
    EXPECT_EQ(strip_.getPageButtonForTest(2), nullptr);
    EXPECT_TRUE(strip_.getPageButtonForTest(1)->getToggleState());
    EXPECT_EQ(strip_.getAddButtonForTest().getButtonText(), "+");
}

TEST_F(ControllerSurfacePageStripTest, ShowsAButtonPerEffectivePageAndHighlightsTheActiveOne) {
    strip_.setSize(200, ControllerSurfacePageStrip::kHeight);
    strip_.setPages(3, 2);
    ASSERT_NE(strip_.getPageButtonForTest(1), nullptr);
    ASSERT_NE(strip_.getPageButtonForTest(2), nullptr);
    ASSERT_NE(strip_.getPageButtonForTest(3), nullptr);
    EXPECT_EQ(strip_.getPageButtonForTest(4), nullptr);
    EXPECT_FALSE(strip_.getPageButtonForTest(1)->getToggleState());
    EXPECT_TRUE(strip_.getPageButtonForTest(2)->getToggleState());
    EXPECT_FALSE(strip_.getPageButtonForTest(3)->getToggleState());
}

TEST_F(ControllerSurfacePageStripTest, ClickingAnInactivePageFiresOnPageSelected) {
    strip_.setSize(200, ControllerSurfacePageStrip::kHeight);
    strip_.setPages(3, 1);
    int selected = -1;
    strip_.onPageSelected = [&](int page) { selected = page; };

    click(*strip_.getPageButtonForTest(2));
    EXPECT_EQ(selected, 2);
}

TEST_F(ControllerSurfacePageStripTest, ClickingTheAlreadyActivePageFiresNothing) {
    strip_.setSize(200, ControllerSurfacePageStrip::kHeight);
    strip_.setPages(3, 1);
    bool fired = false;
    strip_.onPageSelected = [&](int) { fired = true; };

    click(*strip_.getPageButtonForTest(1));
    EXPECT_FALSE(fired);
}

TEST_F(ControllerSurfacePageStripTest, ClickingAddFiresOnAddPageRequested) {
    strip_.setSize(200, ControllerSurfacePageStrip::kHeight);
    strip_.setPages(1, 1);
    bool fired = false;
    strip_.onAddPageRequested = [&] { fired = true; };

    click(strip_.getAddButtonForTest());
    EXPECT_TRUE(fired);
}

TEST_F(ControllerSurfacePageStripTest, RightClickOnPageOneOffersNoDeleteMenu) {
    strip_.setSize(200, ControllerSurfacePageStrip::kHeight);
    strip_.setPages(2, 1);
    bool hookCalled = false;
    synth::ui::test_hooks::pageStripContextMenuHookForTest() = [&](juce::PopupMenu&) { hookCalled = true; };

    click(*strip_.getPageButtonForTest(1), juce::ModifierKeys(juce::ModifierKeys::rightButtonModifier));
    EXPECT_FALSE(hookCalled) << "page 1 always exists -- there is nothing to delete";
}

TEST_F(ControllerSurfacePageStripTest, RightClickOnAHigherPageOffersDeleteAndFiresOnChoice) {
    strip_.setSize(200, ControllerSurfacePageStrip::kHeight);
    strip_.setPages(2, 1);
    int deletedPage = -1;
    strip_.onDeletePageRequested = [&](int page) { deletedPage = page; };

    juce::PopupMenu captured;
    synth::ui::test_hooks::pageStripContextMenuHookForTest() = [&](juce::PopupMenu& menu) { captured = menu; };

    click(*strip_.getPageButtonForTest(2), juce::ModifierKeys(juce::ModifierKeys::rightButtonModifier));
    chooseMenuItem(captured, "Delete page");
    EXPECT_EQ(deletedPage, 2);
}

TEST_F(ControllerSurfacePageStripTest, RightClickNeverStartsAnOrdinaryClickCycle) {
    // A right-click must never fire onPageSelected even on an inactive page -- it's a context menu
    // gesture, not a selection one.
    strip_.setSize(200, ControllerSurfacePageStrip::kHeight);
    strip_.setPages(2, 1);
    bool selectFired = false;
    strip_.onPageSelected = [&](int) { selectFired = true; };
    synth::ui::test_hooks::pageStripContextMenuHookForTest() = [](juce::PopupMenu&) {};

    click(*strip_.getPageButtonForTest(2), juce::ModifierKeys(juce::ModifierKeys::rightButtonModifier));
    EXPECT_FALSE(selectFired);
}
