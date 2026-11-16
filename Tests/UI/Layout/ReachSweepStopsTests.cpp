// ReachSweepStopsTests.cpp -- real keys reach the stops the reach sweep (ReachSweepTests.cpp) once listed as gaps: the
// timeline's track column width handle and the library's Sounds column width handle. A real MainComponent is on the
// desktop and keys are delivered the way the operating system does (RealKeyDelivery.h), so a stop that no key reaches,
// or a handle whose arrows do not resize, fails here by name.
#include "../../TestSettingsHelpers.h"
#include "KeyboardSweepShell.h"
#include "UI/Library/LibrarySidebarViews.h"
#include "UI/Timeline/TimelinePanelComponent/TimelinePanelComponent.h"
#include <gtest/gtest.h>

using namespace synth::test::keyboard_sweep;
using namespace synth::test::real_keys;
using Tab = synth::ui::BottomDockComponent::Tab;
using synth::ui::SidebarView;

namespace {

class ReachStopsShell : public KeyboardDesktopShell {
protected:
    static juce::KeyPress plain(int code) { return juce::KeyPress(code, juce::ModifierKeys(), 0); }
    static juce::KeyPress shifted(int code) {
        return juce::KeyPress(code, juce::ModifierKeys(juce::ModifierKeys::shiftModifier), 0);
    }

    juce::Component* focused() { return juce::Component::getCurrentlyFocusedComponent(); }

    // Presses `key` where focus is and waits for the keys' effects (a queued command, a glide) to settle.
    bool press(const juce::KeyPress& key) {
        auto* at = focused();
        if (at == nullptr)
            return false;
        const bool handled = deliver(*at, key).handled;
        synth::test::flushMessageQueue();
        return handled;
    }

    // The timeline in the main area, focus on the panel's root.
    synth::ui::TimelinePanelComponent& focusTimelineRoot() {
        mc_->setMainViewForTest(Tab::Timeline);
        mc_->resized();
        auto& panel = mc_->getTimelinePanel();
        EXPECT_TRUE(mc_->getFocusRegionsForTest().focusRegionById("timeline"));
        synth::test::waitUntil([&] { return focused() == &panel; }, 1000, 20);
        EXPECT_EQ(focused(), &panel) << "the region cycle lands on the panel's root";
        return panel;
    }

    void showSounds() {
        auto& lib = mc_->getLibrarySidebarForTest();
        lib.setView(SidebarView::Sounds, false);
        lib.completeViewMotionForTest();
        mc_->resized();
    }
};

} // namespace

// The timeline's Tab walk: root, the track column's width handle, then the first track row.
TEST_F(ReachStopsShell, TabWalksTheTimelineFromTheRootThroughTheWidthHandleToTheTrackRow) {
#if !JUCE_MAC
    GTEST_SKIP() << "real key delivery is measured on macOS with the other sweeps";
#endif
    auto& panel = focusTimelineRoot();
    ASSERT_TRUE(press(plain(juce::KeyPress::tabKey)));
    EXPECT_EQ(focused(), &panel.getTrackHeaderWidthHandle()) << "Tab from the root lands on the track column's handle";
    ASSERT_TRUE(press(plain(juce::KeyPress::tabKey)));
    EXPECT_EQ(panel.getFocusedTrackIndexForTest(), 0) << "Tab from the handle goes on to the first track row";
    EXPECT_NE(focused(), &panel.getTrackHeaderWidthHandle());
}

// Escape from the handle returns to the panel's root.
TEST_F(ReachStopsShell, EscapeFromTheWidthHandleReturnsToTheTimelineRoot) {
#if !JUCE_MAC
    GTEST_SKIP() << "real key delivery is measured on macOS with the other sweeps";
#endif
    auto& panel = focusTimelineRoot();
    ASSERT_TRUE(press(plain(juce::KeyPress::tabKey)));
    ASSERT_EQ(focused(), &panel.getTrackHeaderWidthHandle());
    ASSERT_TRUE(press(juce::KeyPress(juce::KeyPress::escapeKey, juce::ModifierKeys(), 0)));
    EXPECT_EQ(focused(), &panel);
}

// Left and Right on the focused handle resize the column through the drag's own setter (clamped, remembered), and
// Return resets it.
TEST_F(ReachStopsShell, LeftAndRightOnTheTrackColumnHandleResizeTheColumnAndReturnResetsIt) {
#if !JUCE_MAC
    GTEST_SKIP() << "real key delivery is measured on macOS with the other sweeps";
#endif
    auto& panel = focusTimelineRoot();
    ASSERT_TRUE(press(plain(juce::KeyPress::tabKey)));
    ASSERT_EQ(focused(), &panel.getTrackHeaderWidthHandle());
    const int start = panel.getTrackHeaderWidth();
    constexpr int step = synth::ui::TimelinePanelComponent::kTrackHeaderWidthKeyStep;

    ASSERT_TRUE(press(plain(juce::KeyPress::rightKey)));
    EXPECT_EQ(panel.getTrackHeaderWidth(), start + step) << "Right widens the column";
    ASSERT_TRUE(press(plain(juce::KeyPress::leftKey)));
    ASSERT_TRUE(press(plain(juce::KeyPress::leftKey)));
    EXPECT_EQ(panel.getTrackHeaderWidth(), start - step) << "Left narrows it";
    EXPECT_EQ(focused(), &panel.getTrackHeaderWidthHandle()) << "the handle keeps the keys while it resizes";
    ASSERT_TRUE(press(plain(juce::KeyPress::returnKey)));
    EXPECT_EQ(panel.getTrackHeaderWidth(), panel.defaultTrackHeaderWidth()) << "Return puts the default back";
}

// The library's Sounds column: Right on the root reaches the width handle, Left and Right resize it, Escape returns.
TEST_F(ReachStopsShell, RightOnTheSoundsColumnRootReachesTheWidthHandleAndLeftAndRightResizeIt) {
#if !JUCE_MAC
    GTEST_SKIP() << "real key delivery is measured on macOS with the other sweeps";
#endif
    showSounds();
    auto& lib = mc_->getLibrarySidebarForTest();
    ASSERT_TRUE(mc_->getFocusRegionsForTest().focusRegionById("library"));
    ASSERT_TRUE(synth::test::waitUntil([&] { return focused() == &lib; }, 1000, 20));
    ASSERT_TRUE(lib.getResizeHandle().isShowing());

    ASSERT_TRUE(press(plain(juce::KeyPress::rightKey)));
    EXPECT_EQ(focused(), &lib.getResizeHandle()) << "Right on the Sounds column's root reaches the handle";
    const int start = lib.getSoundsWidth();
    constexpr int step = synth::ui::LibrarySidebarViews::kWidthKeyStep;
    ASSERT_TRUE(press(plain(juce::KeyPress::rightKey)));
    lib.completeViewMotionForTest();
    EXPECT_EQ(lib.getSoundsWidth(), start + step) << "Right widens the column";
    ASSERT_TRUE(press(plain(juce::KeyPress::leftKey)));
    ASSERT_TRUE(press(plain(juce::KeyPress::leftKey)));
    EXPECT_EQ(lib.getSoundsWidth(), start - step) << "Left narrows it";
    EXPECT_EQ(focused(), &lib.getResizeHandle());

    ASSERT_TRUE(press(juce::KeyPress(juce::KeyPress::escapeKey, juce::ModifierKeys(), 0)));
    EXPECT_EQ(focused(), &lib) << "Escape returns to the column's root";
    EXPECT_EQ(lib.getView(), SidebarView::Sounds);
}

// In the Modules view there is no handle: Right still switches to Sounds.
TEST_F(ReachStopsShell, RightOnTheModulesColumnRootStillSwitchesToSounds) {
#if !JUCE_MAC
    GTEST_SKIP() << "real key delivery is measured on macOS with the other sweeps";
#endif
    auto& lib = mc_->getLibrarySidebarForTest();
    lib.setView(SidebarView::Modules, false);
    lib.completeViewMotionForTest();
    mc_->resized();
    ASSERT_TRUE(mc_->getFocusRegionsForTest().focusRegionById("library"));
    ASSERT_TRUE(synth::test::waitUntil([&] { return focused() == &lib; }, 1000, 20));
    ASSERT_TRUE(press(plain(juce::KeyPress::rightKey)));
    EXPECT_EQ(lib.getView(), SidebarView::Sounds);
}
