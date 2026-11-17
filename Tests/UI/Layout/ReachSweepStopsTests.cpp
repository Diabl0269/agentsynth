// ReachSweepStopsTests.cpp -- real keys reach the stops the reach sweep (ReachSweepTests.cpp) once listed as gaps: the
// timeline's Sections strip, its track column's width handle and its clip lane, and the library's Sounds column width
// handle. A real MainComponent is on the desktop and keys are delivered the way the operating system does
// (RealKeyDelivery.h), so a stop that no key reaches, or a handle whose arrows do not resize, fails here by name.
#include "../../TestSettingsHelpers.h"
#include "../../TestWait.h"
#include "KeyboardSweepShell.h"
#include "MainComponent/MainComponent.h"
#include "RealKeyDelivery.h"
#include "UI/Layout/FocusRegion.h"
#include "UI/Library/LibrarySidebarViews.h"
#include "UI/Timeline/TimelinePanelComponent/TimelinePanelComponent.h"
#include "UI/Timeline/TimelineSectionStrip/TimelineSectionStrip.h"
#include <gtest/gtest.h>
#include <iostream>

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

    // The timeline in the main area with its Sections strip showing, focus on the panel's root.
    synth::ui::TimelinePanelComponent& focusTimelineRoot() {
        mc_->setMainViewForTest(Tab::Timeline);
        mc_->resized();
        auto& panel = mc_->getTimelinePanel();
        EXPECT_TRUE(panel.getSectionStrip().isShowing()) << "the Sections strip is on by default";
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

// The timeline's Tab walk: root, the Sections strip, the track column's width handle, the first track row, and on to
// the track's clips.
TEST_F(ReachStopsShell, TabWalksTheTimelineFromTheRootThroughTheSectionsStripAndTheWidthHandleToTheTrackRow) {
#if !JUCE_MAC
    GTEST_SKIP() << "real key delivery is measured on macOS with the other sweeps";
#endif
    auto& panel = focusTimelineRoot();
    ASSERT_TRUE(press(plain(juce::KeyPress::tabKey)));
    EXPECT_EQ(focused(), &panel.getSectionStrip()) << "Tab from the root lands on the Sections strip";
    ASSERT_TRUE(press(plain(juce::KeyPress::tabKey)));
    EXPECT_EQ(focused(), &panel.getTrackHeaderWidthHandle()) << "then on the track column's width handle";
    ASSERT_TRUE(press(shifted(juce::KeyPress::tabKey)));
    EXPECT_EQ(focused(), &panel.getSectionStrip()) << "Shift+Tab goes back to the strip";
    ASSERT_TRUE(press(plain(juce::KeyPress::tabKey)));
    ASSERT_TRUE(press(plain(juce::KeyPress::tabKey)));
    EXPECT_EQ(panel.getFocusedTrackIndexForTest(), 0) << "Tab from the handle goes on to the first track row";
    EXPECT_NE(focused(), &panel.getTrackHeaderWidthHandle());
    EXPECT_NE(focused(), &panel.getSectionStrip());
}

// Escape from either stop returns to the panel's root.
TEST_F(ReachStopsShell, EscapeFromTheSectionsStripAndFromTheWidthHandleReturnsToTheTimelineRoot) {
#if !JUCE_MAC
    GTEST_SKIP() << "real key delivery is measured on macOS with the other sweeps";
#endif
    auto& panel = focusTimelineRoot();
    ASSERT_TRUE(press(plain(juce::KeyPress::tabKey)));
    ASSERT_EQ(focused(), &panel.getSectionStrip());
    ASSERT_TRUE(press(juce::KeyPress(juce::KeyPress::escapeKey, juce::ModifierKeys(), 0)));
    EXPECT_EQ(focused(), &panel);
    ASSERT_TRUE(press(plain(juce::KeyPress::tabKey)));
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

// The Sections strip shows that it has the keys even with no section to carry the ring.
TEST_F(ReachStopsShell, TheSectionsStripWithNoSectionDrawsAFocusRing) {
#if !JUCE_MAC
    GTEST_SKIP() << "real key delivery is measured on macOS with the other sweeps";
#endif
    auto& panel = focusTimelineRoot();
    ASSERT_TRUE(press(plain(juce::KeyPress::tabKey)));
    auto& strip = panel.getSectionStrip();
    ASSERT_EQ(focused(), &strip);
    ASSERT_FALSE(strip.getSelectedPlacement().isValid());
    const auto withFocus = strip.createComponentSnapshot(strip.getLocalBounds());
    ASSERT_TRUE(takeFocus(panel));
    const auto without = strip.createComponentSnapshot(strip.getLocalBounds());
    bool differs = false;
    for (int y = 0; y < withFocus.getHeight() && !differs; ++y)
        for (int x = 0; x < withFocus.getWidth() && !differs; ++x)
            differs = withFocus.getPixelAt(x, y) != without.getPixelAt(x, y);
    EXPECT_TRUE(differs);
}

// A track that holds a clip: Tab walks the row, then the clip lane takes the keys and shows its ring.
TEST_F(ReachStopsShell, TabFromATrackRowWithAClipEntersTheClipLane) {
#if !JUCE_MAC
    GTEST_SKIP() << "real key delivery is measured on macOS with the other sweeps";
#endif
    auto& panel = focusTimelineRoot();
    const auto track = mc_->getTimelineDoc().getTracks().front().id;
    mc_->getTimelineDoc().addClip(track, 0.0, 8.0, "Clip");
    panel.selectTracks({track}, track);
    mc_->resized();
    synth::test::flushMessageQueue();
    // The lane area takes focus only once it is on screen with the clip laid out; a key sent before that falls back to
    // the panel's root.
    synth::test::waitUntil([&] { return panel.getClipLaneArea().isShowing(); }, 2000, 20);
    ASSERT_TRUE(panel.getClipLaneArea().isShowing()) << "the clip lane is on screen";
    ASSERT_TRUE(panel.getClipLaneArea().getWantsKeyboardFocus()) << "the clip lane takes keyboard focus";
    for (int i = 0; i < 3; ++i) // the strip, the handle, the row
        ASSERT_TRUE(press(plain(juce::KeyPress::tabKey)));
    EXPECT_NE(focused(), &panel) << "the third Tab is on the track row, not the root";
    {
        auto* before = focused();
        std::cerr << "DIAG before4th=" << (before ? before->getComponentID().toStdString() : "null") << std::endl;
    }
    ASSERT_TRUE(press(plain(juce::KeyPress::tabKey)));
    synth::test::waitUntil([&] { return focused() == &panel.getClipLaneArea(); }, 1000, 20);
    {
        std::cerr << "DIAG kbClipValid=" << panel.getClipLaneArea().getKeyboardClip().isValid() << std::endl;
        auto& lane = panel.getClipLaneArea();
        auto* f = focused();
        std::cerr << "DIAG focused="
                  << (f ? f->getComponentID().toStdString() + "/" + f->getTitle().toStdString() : "null")
                  << " isPanel=" << (f == &panel) << " laneEnabled=" << lane.isEnabled()
                  << " showing=" << lane.isShowing() << " wants=" << lane.getWantsKeyboardFocus()
                  << " peer=" << (lane.getPeer() != nullptr) << std::endl;
    }
    EXPECT_EQ(focused(), &panel.getClipLaneArea()) << "Tab from the row enters its clips";
    ASSERT_TRUE(press(shifted(juce::KeyPress::tabKey)));
    EXPECT_NE(focused(), &panel.getClipLaneArea()) << "Shift+Tab comes back to the row";
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
