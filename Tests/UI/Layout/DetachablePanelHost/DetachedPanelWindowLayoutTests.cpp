// DetachedPanelWindowLayoutTests.cpp -- render-level coverage for DetachedPanelWindow's content
// layout: the window has no header strip, the hosted panel fills it, and the "Dock back" button
// (DockBackButton) sits at the right end of the panel's own toolbar (docs/mixer/panel.md).
// Sibling to DetachedPanelWindowTests.cpp (behaviour/bounds-persistence/focus). Same
// addToDesktop=false shape throughout: no test here ever creates a native peer.
//
// Groups:
//   1. Panel bounds == content bounds at several window sizes, no header strip.
//   2. Degenerate sizes lay out without crashing.
//   3. Render-to-image -- the hosted panel paints across the whole window.
//   4. Themed background + the dock-back button's icon paints; a real click docks.

#include "../../../TestWait.h"
#include "ShortcutManager/ShortcutManager.h"
#include "UI/Layout/DetachablePanelHost/DetachedPanelWindow.h"
#include "UI/Layout/DetachablePanelHost/DockBackButton.h"
#include "UI/Layout/DetachablePanelHost/DockBackSlot.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <gtest/gtest.h>

using synth::ui::DetachedPanelWindow;
using synth::ui::DockBackButton;
using synth::ui::DockBackSlot;

namespace {

// Paints a solid fill so a snapshot can tell "the hosted panel's area" apart from empty background.
struct FillPanel
    : public juce::Component
    , public DockBackSlot {
    void paint(juce::Graphics& g) override { g.fillAll(juce::Colours::limegreen); }
    void resized() override {
        if (dockBack_ != nullptr)
            dockBack_->setBounds(getWidth() - kButtonWidth, 0, kButtonWidth, 24);
    }
    void setDockBackButton(juce::Button* button) override {
        DockBackSlot::adopt(*this, dockBack_, button);
        resized();
    }
    juce::Button* dockBack_ = nullptr;
};

class DetachedPanelWindowLayoutTest : public ::testing::Test {
protected:
    void SetUp() override {
        juce::PropertiesFile::Options options;
        options.applicationName = "DetachedPanelWindowLayoutTest";
        options.filenameSuffix = "test";
        options.storageFormat = juce::PropertiesFile::storeAsXML;
        appProperties.setStorageParameters(options);
    }

    void TearDown() override {
        panel.setDockBackButton(nullptr);
        if (auto* userSettings = appProperties.getUserSettings())
            userSettings->clear();
    }

    FillPanel panel;
    DockBackButton button;
    juce::String title{"Test Panel"};
    juce::ApplicationProperties appProperties;
    ShortcutManager shortcutManager;
};

} // namespace

// ============================================================================
// 1. No header strip
// ============================================================================

TEST_F(DetachedPanelWindowLayoutTest, ThePanelFillsTheWindowContentAtSeveralSizes) {
    DetachedPanelWindow window(panel, title, "testWindowBounds", &appProperties, nullptr, &shortcutManager);

    for (const auto size : {juce::Point<int>{400, 300}, juce::Point<int>{800, 600}, juce::Point<int>{1024, 768}}) {
        window.setBounds(0, 0, size.x, size.y);
        auto* content = window.getContentForTest();
        ASSERT_NE(content, nullptr);
        EXPECT_EQ(panel.getBounds(), content->getLocalBounds())
            << "no header strip: the panel is the whole content at " << size.x << "x" << size.y;
        EXPECT_EQ(panel.getY(), 0);
    }
}

TEST_F(DetachedPanelWindowLayoutTest, TheDockBackButtonIsVisibleAtThePanelsRightEdgeWhileHandedOver) {
    DetachedPanelWindow window(panel, title, "testWindowBounds", &appProperties, nullptr, &shortcutManager);
    panel.setDockBackButton(&button);
    window.setBounds(0, 0, 640, 420);

    EXPECT_TRUE(button.isVisible());
    EXPECT_EQ(button.getParentComponent(), &panel);
    EXPECT_EQ(button.getRight(), panel.getWidth()) << "flush with the toolbar's right edge";
    EXPECT_EQ(button.getY(), 0) << "on the panel's toolbar, not in a strip above it";

    panel.setDockBackButton(nullptr);
    EXPECT_EQ(button.getParentComponent(), nullptr) << "docked toolbars carry no button";
}

TEST_F(DetachedPanelWindowLayoutTest, ARealMouseClickOnTheDockBackButtonFiresItsOnClick) {
    DetachedPanelWindow window(panel, title, "testWindowBounds", &appProperties, nullptr, &shortcutManager);
    panel.setDockBackButton(&button);
    window.setBounds(0, 0, 640, 420);

    int clicks = 0;
    button.onClick = [&] { ++clicks; };
    const auto centre = button.getLocalBounds().getCentre().toFloat();
    const auto now = juce::Time::getCurrentTime();
    const juce::MouseInputSource source = juce::Desktop::getInstance().getMainMouseSource();
    const auto make = [&](juce::ModifierKeys mods) {
        return juce::MouseEvent(source, centre, mods, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &button, &button, now, centre, now,
                                1, false);
    };
    juce::Component& asComponent = button;
    asComponent.mouseEnter(make(juce::ModifierKeys()));
    asComponent.mouseDown(make(juce::ModifierKeys::leftButtonModifier));
    asComponent.mouseUp(make(juce::ModifierKeys()));
    EXPECT_EQ(clicks, 1);
}

TEST_F(DetachedPanelWindowLayoutTest, TheDockBackButtonIsATabStopNamedAndTooltipped) {
    EXPECT_TRUE(button.getWantsKeyboardFocus());
    EXPECT_EQ(button.getTitle(), "Dock back");
    EXPECT_EQ(button.getTooltip(), "Dock back  (Esc)");
}

// ============================================================================
// 2. Degenerate sizes
// ============================================================================

TEST_F(DetachedPanelWindowLayoutTest, ZeroWidthAndTinyWindowsLayOutWithoutCrashing) {
    DetachedPanelWindow window(panel, title, "testWindowBounds", &appProperties, nullptr, &shortcutManager);
    panel.setDockBackButton(&button);
    window.setBounds(0, 0, 0, 200);
    EXPECT_GE(panel.getWidth(), 0);
    window.setBounds(0, 0, 300, 10);
    EXPECT_GE(panel.getHeight(), 0);
}

// ============================================================================
// 3. Render-to-image
// ============================================================================

TEST_F(DetachedPanelWindowLayoutTest, ThePanelPaintsFromTheWindowsTopEdge) {
    DetachedPanelWindow window(panel, title, "testWindowBounds", &appProperties, nullptr, &shortcutManager);
    for (const auto size : {juce::Point<int>{400, 300}, juce::Point<int>{1024, 768}}) {
        window.setBounds(0, 0, size.x, size.y);
        auto* content = window.getContentForTest();
        ASSERT_NE(content, nullptr);
        const auto img = content->createComponentSnapshot(content->getLocalBounds());
        EXPECT_EQ(img.getPixelAt(2, 2), juce::Colours::limegreen)
            << "the panel's own pixels start at the very top-left at " << size.x << "x" << size.y;
    }
}

// ============================================================================
// 4. Themed background + the dock-back button's own icon actually paints
// ============================================================================

namespace {
synth::theme::Theme themeWithDistinctiveColours() {
    synth::theme::Theme theme;
    theme.colors.surface = juce::Colour(0xff7744CC);
    theme.colors.textMuted = juce::Colour(0xffAABBCC);
    theme.colors.textPrimary = juce::Colour(0xffFFEE11);
    return theme;
}
} // namespace

TEST_F(DetachedPanelWindowLayoutTest, BackgroundMatchesThemeSurfaceAndTheDockBackButtonPaintsItsIcon) {
    synth::theme::AppLookAndFeel lf;
    lf.applyTheme(themeWithDistinctiveColours());

    DetachedPanelWindow window(panel, title, "testWindowBounds", &appProperties, &lf, &shortcutManager);
    panel.setDockBackButton(&button);
    window.setBounds(0, 0, 640, 420);

    EXPECT_EQ(window.getBackgroundColour(), juce::Colour(0xff7744CC))
        << "the window's own background must resolve to the theme's surface token";

    // The button paints on the panel's own pixels (limegreen here), so any pixel that is neither
    // the panel fill nor the button's no-fill background proves the icon painted.
    const auto img = button.createComponentSnapshot(button.getLocalBounds());
    bool sawIconPixel = false;
    for (int x = 0; x < img.getWidth() && !sawIconPixel; ++x)
        for (int y = 0; y < img.getHeight() && !sawIconPixel; ++y)
            if (img.getPixelAt(x, y).getAlpha() > 0 && img.getPixelAt(x, y) != juce::Colours::limegreen)
                sawIconPixel = true;
    EXPECT_TRUE(sawIconPixel) << "the dock-back button must actually paint its icon";
}

// A "glass" theme's surface is translucent; filling a top-level window with it let the OS
// window backing show through as flat grey. The background must be the surface laid over bg0, opaque.
TEST_F(DetachedPanelWindowLayoutTest, TranslucentThemeSurfaceStillGivesAnOpaqueBackground) {
    synth::theme::Theme theme;
    theme.colors.bg0 = juce::Colour(0xff101010);
    theme.colors.surface = juce::Colour(0x997744CC);
    synth::theme::AppLookAndFeel lf;
    lf.applyTheme(theme);

    DetachedPanelWindow window(panel, title, "testWindowBounds", &appProperties, &lf, &shortcutManager);

    EXPECT_TRUE(window.getBackgroundColour().isOpaque());
    EXPECT_EQ(window.getBackgroundColour(), theme.colors.bg0.overlaidWith(theme.colors.surface));
}

// Regression test for FRO974: the button registered Space as a window-wide shortcut, so Space anywhere in a detached
// panel's window docked it back instead of reaching the transport.
TEST_F(DetachedPanelWindowLayoutTest, SpacePressesTheDockBackButtonOnlyWhileItHasFocus) {
    EXPECT_FALSE(button.isRegisteredForShortcut(juce::KeyPress(juce::KeyPress::spaceKey)))
        << "no window-wide Space shortcut";
    int clicks = 0;
    button.onClick = [&] { ++clicks; };
    EXPECT_TRUE(button.keyPressed(juce::KeyPress(juce::KeyPress::spaceKey))) << "a focused button consumes Space";
    synth::test::waitUntil([&] { return (clicks) == (1); });
    EXPECT_EQ(clicks, 1);
}
