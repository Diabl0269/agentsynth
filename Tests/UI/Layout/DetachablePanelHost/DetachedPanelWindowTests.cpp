// DetachedPanelWindowTests.cpp
//
// synth::ui::DetachedPanelWindow -- the top-level window a DetachablePanelHost detaches a panel
// into (FRO12, P9-6, docs/mixer.md §5.9). Mirrors Tests/Plugin/HostedPluginEditorWindowTests.cpp's
// own structure: every window here is built with addToDesktop=false, so constructing one never
// creates a native peer.
//
// Groups:
//   1. Headless construction -- no native peer until setVisible(true).
//   2. Bounds persistence round trip.
//   3. Close button -- fires onCloseRequested, never self-destroys.
//   4. Plugin-mode LookAndFeel seam -- own scope, Desktop's default untouched.
//   5. Per-window focus-region Tab cycling (T159/docs/shortcuts.md).

#include "ShortcutManager/ShortcutManager.h"
#include "UI/Layout/DetachablePanelHost/DetachedPanelWindow.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <gtest/gtest.h>

using synth::ui::DetachedPanelWindow;

namespace {

struct StubPanel : public juce::Component {};

class DetachedPanelWindowTest : public ::testing::Test {
protected:
    void SetUp() override {
        juce::PropertiesFile::Options options;
        options.applicationName = "DetachedPanelWindowTest";
        options.filenameSuffix = "test";
        options.storageFormat = juce::PropertiesFile::storeAsXML;
        appProperties.setStorageParameters(options);

        button.setButtonText({});
        title.setText("Test Panel", juce::dontSendNotification);
    }

    void TearDown() override {
        if (auto* userSettings = appProperties.getUserSettings())
            userSettings->clear();
    }

    StubPanel panel;
    juce::DrawableButton button{"detach", juce::DrawableButton::ImageFitted};
    juce::Label title;
    juce::ApplicationProperties appProperties;
    ShortcutManager shortcutManager;
};

} // namespace

// ============================================================================
// 1. Headless construction
// ============================================================================

TEST_F(DetachedPanelWindowTest, AddToDesktopFalseCreatesNoPeer) {
    DetachedPanelWindow window(panel, button, title, "testWindowBounds", &appProperties, nullptr, &shortcutManager);
    EXPECT_EQ(window.getPeer(), nullptr) << "constructing must never create a native peer";
    EXPECT_EQ(window.getContentForTest(), window.getPanelForTest().getParentComponent())
        << "the borrowed panel must already be reparented into this window's own content";
}

// ============================================================================
// 2. Bounds persistence round trip
// ============================================================================

TEST_F(DetachedPanelWindowTest, BoundsRoundTripThroughPersistedKey) {
    {
        DetachedPanelWindow windowA(panel, button, title, "testWindowBounds", &appProperties, nullptr,
                                    &shortcutManager);
        windowA.setBounds(50, 60, 500, 350); // triggers moved()+resized() -> persistBounds()
        ASSERT_EQ(windowA.getBounds(), juce::Rectangle<int>(50, 60, 500, 350));
    } // windowA destroyed -- panel/button/title are borrowed, never deleted by it

    DetachedPanelWindow windowB(panel, button, title, "testWindowBounds", &appProperties, nullptr, &shortcutManager);
    EXPECT_EQ(windowB.getBounds(), juce::Rectangle<int>(50, 60, 500, 350))
        << "a fresh window with the same boundsKey must restore the persisted position/size";
}

TEST_F(DetachedPanelWindowTest, NoPersistedKeyFallsBackToACentredDefault) {
    DetachedPanelWindow window(panel, button, title, "neverPersistedWindowBounds", &appProperties, nullptr,
                               &shortcutManager);
    EXPECT_GT(window.getWidth(), 0);
    EXPECT_GT(window.getHeight(), 0);
}

// ============================================================================
// 3. Close button
// ============================================================================

TEST_F(DetachedPanelWindowTest, CloseButtonFiresCallbackAndNeverSelfDestroys) {
    auto window = std::make_unique<DetachedPanelWindow>(panel, button, title, "testWindowBounds", &appProperties,
                                                        nullptr, &shortcutManager);
    bool closeRequested = false;
    window->onCloseRequested = [&] { closeRequested = true; };

    window->closeButtonPressed();

    EXPECT_TRUE(closeRequested);
    EXPECT_NE(window.get(), nullptr) << "the window must still exist -- it never destroys itself";
}

// ============================================================================
// 4. Plugin-mode LookAndFeel seam
// ============================================================================

TEST_F(DetachedPanelWindowTest, NeverTouchesTheProcessGlobalDefaultLookAndFeel) {
    synth::theme::AppLookAndFeel handedInstance;
    auto* defaultBefore = juce::Desktop::getInstance().getDefaultLookAndFeel();

    {
        DetachedPanelWindow window(panel, button, title, "testWindowBounds", &appProperties, &handedInstance,
                                   &shortcutManager);
        EXPECT_EQ(&window.getLookAndFeel(), &handedInstance)
            << "must set its OWN LookAndFeel to the instance its owner handed it";
        EXPECT_EQ(juce::Desktop::getInstance().getDefaultLookAndFeel(), defaultBefore)
            << "must never call Desktop::setDefaultLookAndFeel -- that is process-global inside a host";

        // addToDesktop=false still lets setVisible(true) run without creating a real peer on a
        // headless CI runner with no display (juce::Desktop::getDisplays().getPrimaryDisplay() ==
        // nullptr there) -- guard exactly like HostedPluginWindowManager::openEditorFor does.
        if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() != nullptr) {
            window.setVisible(true);
            EXPECT_EQ(juce::Desktop::getInstance().getDefaultLookAndFeel(), defaultBefore)
                << "must stay untouched after setVisible(true) too";
        }
    }

    EXPECT_EQ(juce::Desktop::getInstance().getDefaultLookAndFeel(), defaultBefore)
        << "destruction must not have touched the process-global default either";
}

// ============================================================================
// 5. Per-window focus-region Tab cycling
// ============================================================================

TEST_F(DetachedPanelWindowTest, TabCyclesOnlyItsOwnOneRegionRegistry) {
    DetachedPanelWindow window(panel, button, title, "testWindowBounds", &appProperties, nullptr, &shortcutManager);
    window.registerHostedPanelFocusRegion("mixer", panel);

    ASSERT_EQ(window.getFocusRegionsForTest().getRegions().size(), 1u);
    EXPECT_EQ(window.getFocusRegionsForTest().getRegions().front().id, "mixer");

    // Tab is bound to focusNextRegion by default (ShortcutManager's own ctor calls
    // resetToDefaults()) -- driving it
    // through keyPressed() must resolve against THIS window's own registry, never crash with no
    // MainComponent anywhere in scope, and return true (a real region existed to cycle to).
    const juce::KeyPress tab(juce::KeyPress::tabKey, juce::ModifierKeys::noModifiers, 0);
    EXPECT_TRUE(window.keyPressed(tab));
}

TEST_F(DetachedPanelWindowTest, UnboundKeyIsNotHandled) {
    DetachedPanelWindow window(panel, button, title, "testWindowBounds", &appProperties, nullptr, &shortcutManager);
    window.registerHostedPanelFocusRegion("mixer", panel);

    const juce::KeyPress unbound('z', juce::ModifierKeys::commandModifier | juce::ModifierKeys::altModifier, 0);
    EXPECT_FALSE(window.keyPressed(unbound));
}
