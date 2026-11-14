// LibrarySidebarKeysTests.cpp -- how the keyboard enters and walks the library column: the region root takes focus and
// holds it (the whole column is ringed), Down steps to the Modules / Sounds switch and on to the showing view's search
// field, Left and Right change the view on the switch, Up from the view's top returns to the switch and Escape returns
// to the root. Keys go through the window's peer (RealKeyDelivery.h), so the app's own dispatch answers.
#include "../../TestWait.h"
#include "../Layout/KeyboardSweepShell.h"
#include "../Layout/RealKeyDelivery.h"
#include "UI/Layout/FocusStepWithin.h"
#include "UI/Library/LibrarySidebarViews.h"
#include <gtest/gtest.h>

using namespace synth::test::real_keys;
using synth::ui::SidebarView;

namespace {

juce::KeyPress key(int code) { return juce::KeyPress(code, juce::ModifierKeys(), 0); }

class LibrarySidebarKeys : public synth::test::keyboard_sweep::KeyboardDesktopShell {
protected:
    synth::ui::LibrarySidebarViews& sidebar() { return mc_->getLibrarySidebarForTest(); }

    void show(SidebarView view) {
        sidebar().setView(view, false);
        sidebar().completeViewMotionForTest();
        mc_->resized();
    }

    // The way Tab (or Shift+Tab) arrives from another region.
    bool arriveByRegionCycle() {
        EXPECT_TRUE(mc_->getFocusRegionsForTest().focusRegionById("library"));
        return synth::test::waitUntil([this] { return sidebar().hasKeyboardFocus(false); }, 1000, 20);
    }

    juce::Component* focused() { return juce::Component::getCurrentlyFocusedComponent(); }

    bool press(int code) {
        auto* c = focused();
        return c != nullptr && deliver(*c, key(code)).handled;
    }

    juce::Component& modulesSearch() {
        for (auto* c : synth::ui::focusableDescendants(sidebar()))
            if (c->getTitle() == "Search library")
                return *c;
        ADD_FAILURE() << "no Modules search field";
        return sidebar();
    }

    juce::Component& searchOf(SidebarView view) {
        return view == SidebarView::Sounds ? static_cast<juce::Component&>(sidebar().getSounds().getSearchField())
                                           : modulesSearch();
    }

    void walksRootSwitchSearch(SidebarView view) {
        show(view);
        ASSERT_TRUE(arriveByRegionCycle());
        EXPECT_EQ(focused(), &sidebar()) << "the column's root keeps focus when a region cycle lands on it";

        ASSERT_TRUE(press(juce::KeyPress::downKey));
        EXPECT_EQ(focused(), &sidebar().getSwitch()) << "Down from the root enters the switch";
        ASSERT_TRUE(press(juce::KeyPress::downKey));
        EXPECT_EQ(focused(), &searchOf(view)) << "Down from the switch enters the view's search field";
        ASSERT_TRUE(press(juce::KeyPress::upKey));
        EXPECT_EQ(focused(), &sidebar().getSwitch()) << "Up from the search field returns to the switch";
        ASSERT_TRUE(press(juce::KeyPress::upKey));
        EXPECT_EQ(focused(), &sidebar()) << "Up from the switch returns to the root";

        ASSERT_TRUE(press(juce::KeyPress::downKey));
        ASSERT_TRUE(press(juce::KeyPress::downKey));
        ASSERT_EQ(focused(), &searchOf(view));
        ASSERT_TRUE(press(juce::KeyPress::escapeKey));
        if (view == SidebarView::Sounds) { // an empty Sounds search hands focus to the list first
            EXPECT_EQ(focused(), &sidebar().getSounds());
            ASSERT_TRUE(press(juce::KeyPress::escapeKey));
        }
        EXPECT_EQ(focused(), &sidebar()) << "Escape from the search field returns to the root";
        ASSERT_TRUE(press(juce::KeyPress::downKey));
        ASSERT_EQ(focused(), &sidebar().getSwitch());
        ASSERT_TRUE(press(juce::KeyPress::escapeKey));
        EXPECT_EQ(focused(), &sidebar()) << "Escape from the switch returns to the root";
    }
};

} // namespace

TEST_F(LibrarySidebarKeys, TheRegionCycleLeavesFocusOnTheRootAndDownStepsSwitchThenSearchInSounds) {
    walksRootSwitchSearch(SidebarView::Sounds);
}

TEST_F(LibrarySidebarKeys, TheRegionCycleLeavesFocusOnTheRootAndDownStepsSwitchThenSearchInModules) {
    walksRootSwitchSearch(SidebarView::Modules);
}

TEST_F(LibrarySidebarKeys, LeftAndRightOnTheSwitchChangeTheView) {
    show(SidebarView::Modules);
    ASSERT_TRUE(arriveByRegionCycle());
    ASSERT_TRUE(press(juce::KeyPress::downKey));
    ASSERT_EQ(focused(), &sidebar().getSwitch());

    EXPECT_TRUE(press(juce::KeyPress::rightKey));
    EXPECT_EQ(sidebar().getView(), SidebarView::Sounds);
    EXPECT_EQ(focused(), &sidebar().getSwitch()) << "the switch keeps focus while the view changes";
    sidebar().completeViewMotionForTest();
    EXPECT_TRUE(press(juce::KeyPress::leftKey));
    EXPECT_EQ(sidebar().getView(), SidebarView::Modules);
    EXPECT_EQ(focused(), &sidebar().getSwitch());
}

TEST_F(LibrarySidebarKeys, LeftAndRightOnTheRootChangeTheViewToo) {
    show(SidebarView::Modules);
    ASSERT_TRUE(arriveByRegionCycle());
    EXPECT_TRUE(press(juce::KeyPress::rightKey));
    EXPECT_EQ(sidebar().getView(), SidebarView::Sounds);
    EXPECT_EQ(focused(), &sidebar());
}

TEST_F(LibrarySidebarKeys, UpFromTheFirstModulesRowReturnsToTheSwitch) {
    show(SidebarView::Modules);
    ASSERT_TRUE(arriveByRegionCycle());
    ASSERT_TRUE(press(juce::KeyPress::downKey));
    ASSERT_TRUE(press(juce::KeyPress::downKey));
    ASSERT_EQ(focused(), &modulesSearch());
    ASSERT_TRUE(press(juce::KeyPress::downKey)); // the cursor goes to the first row, focus stays in the field
    ASSERT_TRUE(press(juce::KeyPress::upKey));
    EXPECT_EQ(focused(), &sidebar().getSwitch()) << "Up with the cursor on the first row leaves for the switch";
}

TEST_F(LibrarySidebarKeys, TheSearchShortcutStillLandsOnTheSearchFieldDirectly) {
    for (const auto view : {SidebarView::Sounds, SidebarView::Modules}) {
        show(view);
        ASSERT_TRUE(arriveByRegionCycle());
        sidebar().focusShowingViewSearch();
        ASSERT_TRUE(synth::test::waitUntil([&] { return searchOf(view).hasKeyboardFocus(false); }, 1000, 20));
        EXPECT_EQ(focused(), &searchOf(view));
    }
}

// The ring is the whole column's: with focus on the root the column's edge is drawn, and it is not when the keys are
// in another region.
TEST_F(LibrarySidebarKeys, TheRootWithFocusRingsTheWholeColumn) {
    show(SidebarView::Sounds);
    const auto edge = [this] {
        juce::Image image(juce::Image::ARGB, sidebar().getWidth(), sidebar().getHeight(), true,
                          juce::SoftwareImageType());
        juce::Graphics g(image);
        sidebar().paintEntireComponent(g, true);
        return std::pair{image.getPixelAt(0, sidebar().getHeight() / 2),
                         image.getPixelAt(sidebar().getWidth() / 2, sidebar().getHeight() - 1)};
    };
    ASSERT_TRUE(mc_->getFocusRegionsForTest().focusRegionById("toolbar"));
    synth::test::flushMessageQueue();
    ASSERT_FALSE(sidebar().hasKeyboardFocus(true));
    const auto without = edge();
    ASSERT_TRUE(arriveByRegionCycle());
    ASSERT_EQ(focused(), &sidebar());
    const auto with = edge();
    EXPECT_NE(with.first, without.first) << "the left edge shows the ring";
    EXPECT_NE(with.second, without.second) << "the bottom edge shows the ring: it is the whole column's";
}
