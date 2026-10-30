// Hidden automation runs on macOS: a window made while the flag is set is invisible and click-through, the app
// cannot be made to show it again, and bringing it to the front does not activate the app.

#include "TestModeRig.h"

#if JUCE_MAC

#import <AppKit/AppKit.h>

using testmode_rig::Rig;

namespace {

struct RestoreHideFlag {
    const bool saved = juce::ComponentPeer::hideWindowsForAutomation;
    explicit RestoreHideFlag(bool value) { juce::ComponentPeer::hideWindowsForAutomation = value; }
    ~RestoreHideFlag() { juce::ComponentPeer::hideWindowsForAutomation = saved; }
};

NSWindow* nativeWindowOf(Rig& rig) {
    auto* peer = rig.getPeer();
    return peer != nullptr ? [(NSView*)peer->getNativeHandle() window] : nil;
}

} // namespace

TEST(AutomationHidingMac, ANewWindowIsTransparentAndClickThrough) {
    RestoreHideFlag hide(true);
    Rig rig;
    if (!rig.show())
        GTEST_SKIP() << "no native window in this environment";
    NSWindow* window = nativeWindowOf(rig);
    ASSERT_NE(window, nil);
    EXPECT_EQ([window alphaValue], 0.0);
    EXPECT_TRUE([window ignoresMouseEvents]);
    EXPECT_FALSE([window hasShadow]);
}

TEST(AutomationHidingMac, SetAlphaCannotMakeAHiddenWindowVisible) {
    RestoreHideFlag hide(true);
    Rig rig;
    if (!rig.show())
        GTEST_SKIP() << "no native window in this environment";
    rig.setAlpha(1.0f);
    rig.setAlpha(0.5f);
    EXPECT_EQ([nativeWindowOf(rig) alphaValue], 0.0);
}

TEST(AutomationHidingMac, ToFrontDoesNotActivateTheApp) {
    RestoreHideFlag hide(true);
    Rig rig;
    if (!rig.show())
        GTEST_SKIP() << "no native window in this environment";
    const bool wasActive = [NSApp isActive];
    rig.toFront(true);
    testmode_rig::pump(50);
    EXPECT_EQ([NSApp isActive], wasActive);
}

TEST(AutomationHidingMac, WithTheFlagOffAWindowIsOrdinary) {
    RestoreHideFlag show(false);
    Rig rig;
    if (!rig.show())
        GTEST_SKIP() << "no native window in this environment";
    NSWindow* window = nativeWindowOf(rig);
    ASSERT_NE(window, nil);
    EXPECT_EQ([window alphaValue], 1.0);
    rig.setAlpha(0.5f);
    EXPECT_NEAR([window alphaValue], 0.5, 0.01);
}

#endif
