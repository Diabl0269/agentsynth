// DetachablePanelHostTests.cpp
//
// synth::ui::DetachablePanelHost -- the ONE mechanism that moves a panel between its dock slot and
// its own top-level window (FRO12, P9-6, docs/mixer/panel.md). Every window this test creates is
// addToDesktop=false (see DetachedPanelWindow's own header comment), so nothing here ever creates
// a native peer -- see DetachedPanelWindowTests.cpp for that side of the mechanism.
//
// Groups:
//   1. Detach/redock preserves the panel's identity and any live state on it.
//   2. Tooltip + button-text/idempotence of the detach control.
//   3. Embedded-header suppression (Tab-placement chrome).
//   4. onDetachedStateChanged fires for both directions, including a window-driven redock.
//   5. Native-window promotion (FRO12 follow-up: setDetached(true) alone never created a peer) --
//      see DetachablePanelHost.h's setCreatesNativeWindows() doc comment for the bug this guards.

#include "ShortcutManager/ShortcutManager.h"
#include "UI/Layout/DetachablePanelHost/DetachablePanelHost.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <gtest/gtest.h>

using synth::ui::DetachablePanelHost;

namespace {

// A trivial panel that carries one mutable field -- standing in for real live state (scroll
// position, zoom, selection) a real Timeline/Mixer panel carries. DetachablePanelHost holds its
// panel BY REFERENCE and never copies or recreates it, so this field must survive a detach/redock
// round trip untouched.
struct StubPanel : public juce::Component {
    int state = 0;
};

class DetachablePanelHostTest : public ::testing::Test {
protected:
    void SetUp() override {
        juce::PropertiesFile::Options options;
        options.applicationName = "DetachablePanelHostTest";
        options.filenameSuffix = "test";
        options.storageFormat = juce::PropertiesFile::storeAsXML;
        appProperties.setStorageParameters(options);
    }

    void TearDown() override {
        if (auto* userSettings = appProperties.getUserSettings())
            userSettings->clear();
    }

    StubPanel panel;
    juce::ApplicationProperties appProperties;
    ShortcutManager shortcutManager;
};

} // namespace

// ============================================================================
// 1. Detach/redock preserves identity and state
// ============================================================================

TEST_F(DetachablePanelHostTest, DetachThenRedockPreservesPanelIdentityAndState) {
    DetachablePanelHost host(panel, "Test Panel", "testPanelWindowBounds", &appProperties,
                             /*lookAndFeel*/ nullptr, &shortcutManager);
    panel.state = 7;

    host.setDetached(true);
    ASSERT_TRUE(host.isDetached());
    ASSERT_NE(host.getDetachedWindowForTest(), nullptr);
    EXPECT_EQ(&host.getPanelForTest(), &panel) << "the SAME instance, never a copy or a rebuild";

    panel.state = 42; // mutate while detached -- a rebuilt panel would lose this

    host.setDetached(false);
    EXPECT_FALSE(host.isDetached());
    EXPECT_EQ(host.getDetachedWindowForTest(), nullptr);
    EXPECT_EQ(&host.getPanelForTest(), &panel);
    EXPECT_EQ(panel.state, 42) << "redock must not have destroyed/recreated the panel";
}

TEST_F(DetachablePanelHostTest, SetDetachedIsIdempotent) {
    DetachablePanelHost host(panel, "Test Panel", "testPanelWindowBounds", &appProperties, nullptr, &shortcutManager);

    host.setDetached(true);
    auto* firstWindow = host.getDetachedWindowForTest();
    ASSERT_NE(firstWindow, nullptr);

    int callbackCount = 0;
    host.onDetachedStateChanged = [&] { ++callbackCount; };
    host.setDetached(true); // already detached -- must be a no-op
    EXPECT_EQ(host.getDetachedWindowForTest(), firstWindow);
    EXPECT_EQ(callbackCount, 0);

    host.setDetached(false);
    EXPECT_EQ(callbackCount, 1);
    host.setDetached(false); // already docked -- must be a no-op
    EXPECT_EQ(callbackCount, 1);
}

// ============================================================================
// 2. Tooltip / button text / detach-button click
// ============================================================================

TEST_F(DetachablePanelHostTest, SetDetachedTogglesTooltip) {
    DetachablePanelHost host(panel, "Test Panel", "testPanelWindowBounds", &appProperties, nullptr, &shortcutManager);
    EXPECT_EQ(host.getButtonTooltipForTest(), "Open in window");

    host.setDetached(true);
    EXPECT_EQ(host.getButtonTooltipForTest(), "Dock back");

    host.setDetached(false);
    EXPECT_EQ(host.getButtonTooltipForTest(), "Open in window");
}

TEST_F(DetachablePanelHostTest, ButtonTextAlwaysEmpty) {
    // The detach control is ICON-ONLY (FRO12's own scope statement) -- no text label, docked or
    // detached, so it can never fall back to rendering a text caption if the icon fails to load.
    DetachablePanelHost host(panel, "Test Panel", "testPanelWindowBounds", &appProperties, nullptr, &shortcutManager);
    EXPECT_TRUE(host.getButtonTextForTest().isEmpty());
    host.setDetached(true);
    EXPECT_TRUE(host.getButtonTextForTest().isEmpty());
}

TEST_F(DetachablePanelHostTest, ClickingTheDetachButtonToggles) {
    DetachablePanelHost host(panel, "Test Panel", "testPanelWindowBounds", &appProperties, nullptr, &shortcutManager);
    ASSERT_TRUE(host.getDetachButton().onClick);

    host.getDetachButton().onClick();
    EXPECT_TRUE(host.isDetached());

    host.getDetachButton().onClick();
    EXPECT_FALSE(host.isDetached());
}

// ============================================================================
// 3. Embedded-header suppression (Tab placement's chrome -- see MixerDockComponent)
// ============================================================================

TEST_F(DetachablePanelHostTest, EmbeddedHeaderHidesOwnButtonWhileDocked) {
    DetachablePanelHost host(panel, "Test Panel", "testPanelWindowBounds", &appProperties, nullptr, &shortcutManager);
    EXPECT_TRUE(host.isDetachButtonVisibleForTest()) << "own header shown by default while docked";

    host.setEmbeddedHeader(true);
    EXPECT_FALSE(host.isDetachButtonVisibleForTest())
        << "owner (e.g. MixerDockComponent's tab strip) supplies the control instead";

    // Detaching always shows the REAL button inside the window, regardless of embeddedHeader_.
    host.setDetached(true);
    EXPECT_TRUE(host.isDetachButtonVisibleForTest());
}

// ============================================================================
// 4. onDetachedStateChanged, including a window-driven redock (the close button)
// ============================================================================

TEST_F(DetachablePanelHostTest, WindowCloseButtonRedocksThroughTheSameCallback) {
    DetachablePanelHost host(panel, "Test Panel", "testPanelWindowBounds", &appProperties, nullptr, &shortcutManager);
    host.setDetached(true);
    auto* window = host.getDetachedWindowForTest();
    ASSERT_NE(window, nullptr);

    bool changed = false;
    host.onDetachedStateChanged = [&] { changed = true; };
    window->closeButtonPressed(); // never self-destroys -- see DetachedPanelWindow's own tests

    EXPECT_TRUE(changed);
    EXPECT_FALSE(host.isDetached()) << "the close button must redock, not just hide the window";
    EXPECT_EQ(&host.getPanelForTest(), &panel);
}

// ============================================================================
// 5. Native-window promotion (FRO12 follow-up)
// ============================================================================

namespace {

// Overrides both native-window seam points (DetachablePanelHost.h's protected
// hasPrimaryDisplayForNativeWindow()/addDetachedWindowToDesktop()) so a test can simulate either
// environment deterministically on ANY runner -- including a developer's Mac, which always has a
// real display. addDetachedWindowToDesktop() deliberately never forwards to the base
// implementation: this must never create an actual native peer, no matter which machine runs the
// test suite.
class RecordingNativeWindowHost : public DetachablePanelHost {
public:
    using DetachablePanelHost::DetachablePanelHost;

    bool simulatedPrimaryDisplay = true;
    bool addToDesktopCallReached = false;

protected:
    bool hasPrimaryDisplayForNativeWindow() const override { return simulatedPrimaryDisplay; }
    void addDetachedWindowToDesktop(synth::ui::DetachedPanelWindow&) override {
        addToDesktopCallReached = true; // recorded only -- never creates a real peer
    }
};

} // namespace

TEST_F(DetachablePanelHostTest, FlagFalseNeverCreatesAPeerEvenWhenADisplayExists) {
    DetachablePanelHost host(panel, "Test Panel", "testPanelWindowBounds", &appProperties, nullptr, &shortcutManager);
    ASSERT_FALSE(host.isCreatingNativeWindows()) << "false is the default -- every headless test relies on this";

    host.setDetached(true);
    ASSERT_NE(host.getDetachedWindowForTest(), nullptr);
    EXPECT_EQ(host.getDetachedWindowForTest()->getPeer(), nullptr)
        << "the flag is off -- setDetached(true) must never promote the window to a real peer";
}

TEST_F(DetachablePanelHostTest, FlagTrueButNoPrimaryDisplayStillCreatesNoPeer) {
    RecordingNativeWindowHost host(panel, "Test Panel", "testPanelWindowBounds", &appProperties, nullptr,
                                   &shortcutManager);
    host.setCreatesNativeWindows(true);
    host.simulatedPrimaryDisplay = false; // simulates a genuinely headless runner, on ANY machine

    host.setDetached(true);
    EXPECT_FALSE(host.addToDesktopCallReached) << "no display -- the promotion call must never be reached";
    ASSERT_NE(host.getDetachedWindowForTest(), nullptr);
    EXPECT_EQ(host.getDetachedWindowForTest()->getPeer(), nullptr);
}

TEST_F(DetachablePanelHostTest, FlagTrueWithAPrimaryDisplayReachesThePromotionCall) {
    RecordingNativeWindowHost host(panel, "Test Panel", "testPanelWindowBounds", &appProperties, nullptr,
                                   &shortcutManager);
    host.setCreatesNativeWindows(true);
    host.simulatedPrimaryDisplay = true;

    host.setDetached(true);
    EXPECT_TRUE(host.addToDesktopCallReached) << "flag on + a display -- production code would promote the window here";
    // The override never forwarded to the real addToDesktop() -- this must still be no real peer,
    // regardless of whether the machine running this test actually has a display.
    ASSERT_NE(host.getDetachedWindowForTest(), nullptr);
    EXPECT_EQ(host.getDetachedWindowForTest()->getPeer(), nullptr);
}
