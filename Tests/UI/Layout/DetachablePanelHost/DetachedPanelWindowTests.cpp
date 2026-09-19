// DetachedPanelWindowTests.cpp
//
// synth::ui::DetachedPanelWindow -- the top-level window a DetachablePanelHost detaches a panel
// into (FRO12, P9-6, docs/mixer/panel.md). Mirrors Tests/Plugin/HostedPluginEditorWindowTests.cpp's
// own structure: every window here is built with addToDesktop=false, so constructing one never
// creates a native peer.
//
// Groups:
//   1. Headless construction -- no native peer until setVisible(true).
//   2. Bounds persistence round trip, including FRO101's implausible-bounds rejection.
//   3. Close button -- fires onCloseRequested, never self-destroys.
//   4. Plugin-mode LookAndFeel seam -- own scope, Desktop's default untouched.
//   5. Per-window focus-region Tab cycling (T159/docs/control/shortcuts.md).
//   6. FRO102 -- themed background (theme's surface token, re-applied on lookAndFeelChanged()).

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

// FRO101: a real bug report -- a headless test run polluted the REAL on-disk settings with
// "0 62 128 128" (128x128 being juce::ComponentBoundsConstrainer's own default minimum, never a
// value a user actually dragged to), and the detached window restored it verbatim: 128x128 pinned
// at the screen edge instead of the documented centred default. Asserting SIZE only (not
// position) keeps this portable to a genuinely headless CI runner, where the "no displays"
// fallback below picks the same kDefaultWidth/kDefaultHeight but setBounds(0, 0, ...) instead of
// centreWithSize(...).
TEST_F(DetachedPanelWindowTest, TinyPersistedBoundsRejectedFallsBackToCentredDefaultSize) {
    if (auto* settings = appProperties.getUserSettings())
        settings->setValue("testWindowBounds", juce::Rectangle<int>(0, 62, 128, 128).toString());

    DetachedPanelWindow window(panel, button, title, "testWindowBounds", &appProperties, nullptr, &shortcutManager);
    EXPECT_EQ(window.getWidth(), 640) << "a 128x128 persisted rect must be rejected as implausible";
    EXPECT_EQ(window.getHeight(), 420);
}

// A plausibly-SIZED rect that sits nowhere any connected display can show it (e.g. left behind by
// a display that's since been unplugged) must fall back the same way. Only meaningful when this
// runner actually has a display to test "off of" -- a genuinely headless CI runner (no displays at
// all) has nothing to validate placement against, so isPlausibleRestoredBounds() deliberately skips
// the intersects-a-display check there and this case is a no-op (there's no persisted geometry that
// COULD be "off-screen" with zero screens).
TEST_F(DetachedPanelWindowTest, OffscreenPersistedBoundsRejectedFallsBackToCentredDefaultSize) {
    if (juce::Desktop::getInstance().getDisplays().displays.isEmpty())
        GTEST_SKIP() << "no display available to test off-screen rejection against";

    if (auto* settings = appProperties.getUserSettings())
        settings->setValue("testWindowBounds", juce::Rectangle<int>(200000, 200000, 640, 420).toString());

    DetachedPanelWindow window(panel, button, title, "testWindowBounds", &appProperties, nullptr, &shortcutManager);
    EXPECT_EQ(window.getWidth(), 640) << "a persisted rect off every display must be rejected too";
    EXPECT_EQ(window.getHeight(), 420);
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
    auto* defaultBefore = &juce::Desktop::getInstance().getDefaultLookAndFeel();

    {
        DetachedPanelWindow window(panel, button, title, "testWindowBounds", &appProperties, &handedInstance,
                                   &shortcutManager);
        EXPECT_EQ(&window.getLookAndFeel(), &handedInstance)
            << "must set its OWN LookAndFeel to the instance its owner handed it";
        EXPECT_EQ(&juce::Desktop::getInstance().getDefaultLookAndFeel(), defaultBefore)
            << "must never call Desktop::setDefaultLookAndFeel -- that is process-global inside a host";

        // addToDesktop=false still lets setVisible(true) run without creating a real peer on a
        // headless CI runner with no display (juce::Desktop::getDisplays().getPrimaryDisplay() ==
        // nullptr there) -- guard exactly like HostedPluginWindowManager::openEditorFor does.
        if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() != nullptr) {
            window.setVisible(true);
            EXPECT_EQ(&juce::Desktop::getInstance().getDefaultLookAndFeel(), defaultBefore)
                << "must stay untouched after setVisible(true) too";
        }
    }

    EXPECT_EQ(&juce::Desktop::getInstance().getDefaultLookAndFeel(), defaultBefore)
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

// ============================================================================
// 6. FRO102 -- themed background
// ============================================================================

namespace {
// A theme whose surface token is deliberately far from both the darkgrey literal FRO102 replaces
// AND from Theme.h's own default surface (0xff1B1F26, also the hardcoded fallback several
// Source/UI/Mixer/*.cpp paint() overrides use) -- asserting against either of those would pass even
// if DetachedPanelWindow never read the theme at all.
synth::theme::Theme themeWithDistinctiveSurface(juce::Colour surface) {
    synth::theme::Theme theme;
    theme.colors.surface = surface;
    return theme;
}
} // namespace

TEST_F(DetachedPanelWindowTest, BackgroundColourMatchesThemeSurfaceWhenLookAndFeelProvided) {
    const juce::Colour distinctiveSurface{0xff7744CC};
    synth::theme::AppLookAndFeel lf;
    lf.applyTheme(themeWithDistinctiveSurface(distinctiveSurface));

    DetachedPanelWindow window(panel, button, title, "testWindowBounds", &appProperties, &lf, &shortcutManager);
    EXPECT_EQ(window.getBackgroundColour(), distinctiveSurface)
        << "must read the theme's OWN surface token, not the darkgrey literal or Theme.h's default";
}

TEST_F(DetachedPanelWindowTest, BackgroundColourUpdatesAfterLookAndFeelSwap) {
    const juce::Colour firstSurface{0xff7744CC};
    const juce::Colour secondSurface{0xff22AA66};
    synth::theme::AppLookAndFeel lfA;
    lfA.applyTheme(themeWithDistinctiveSurface(firstSurface));
    synth::theme::AppLookAndFeel lfB;
    lfB.applyTheme(themeWithDistinctiveSurface(secondSurface));

    DetachedPanelWindow window(panel, button, title, "testWindowBounds", &appProperties, &lfA, &shortcutManager);
    ASSERT_EQ(window.getBackgroundColour(), firstSurface);

    // lookAndFeelChanged() must re-read the CURRENT LookAndFeel, not a value cached at
    // construction -- setLookAndFeel() fires it synchronously (Component::sendLookAndFeelChange()).
    window.setLookAndFeel(&lfB);
    EXPECT_EQ(window.getBackgroundColour(), secondSurface)
        << "must follow a later setLookAndFeel() swap, not stay pinned to the constructor's instance";
}
