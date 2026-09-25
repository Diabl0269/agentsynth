// DetachedPanelWindowLayoutTests.cpp -- FRO230: render-level coverage for DetachedPanelWindow's
// content layout (the private `Content` wrapper: header strip -- title + detach/close button --
// above the hosted panel). Sibling to DetachedPanelWindowTests.cpp (behaviour/bounds-persistence/
// focus), scoped to geometry and pixel content instead. Same addToDesktop=false shape throughout:
// no test here ever creates a native peer.
//
// Groups:
//   1. Header/content bounds at several window sizes -- no overlap, content fills the rest.
//   2. Minimum-size behaviour -- a window shorter than the header strip clips gracefully.
//   3. Render-to-image -- header and hosted-panel regions each paint non-empty pixels.
//   4. FRO228 -- themed background + the header button's own icon actually paints.

#include "ShortcutManager/ShortcutManager.h"
#include "UI/Layout/DetachablePanelHost/DetachedPanelWindow.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <gtest/gtest.h>

using synth::ui::DetachedPanelWindow;

namespace {

// Paints a solid fill so a snapshot can tell "the hosted panel's area" apart from empty
// background -- StubPanel in DetachedPanelWindowTests.cpp paints nothing, which is right for that
// file's behaviour-only tests but useless for asserting non-empty pixels here.
struct FillPanel : public juce::Component {
    void paint(juce::Graphics& g) override { g.fillAll(juce::Colours::limegreen); }
};

class DetachedPanelWindowLayoutTest : public ::testing::Test {
protected:
    void SetUp() override {
        juce::PropertiesFile::Options options;
        options.applicationName = "DetachedPanelWindowLayoutTest";
        options.filenameSuffix = "test";
        options.storageFormat = juce::PropertiesFile::storeAsXML;
        appProperties.setStorageParameters(options);

        button.setButtonText({});
        // An opaque background makes the title's own bounds trivially checkable for "painted
        // something" without depending on font/glyph rendering.
        title.setColour(juce::Label::backgroundColourId, juce::Colours::hotpink);
        title.setText("Test Panel", juce::dontSendNotification);
    }

    void TearDown() override {
        if (auto* userSettings = appProperties.getUserSettings())
            userSettings->clear();
    }

    FillPanel panel;
    juce::DrawableButton button{"detach", juce::DrawableButton::ImageFitted};
    juce::Label title;
    juce::ApplicationProperties appProperties;
    ShortcutManager shortcutManager;
};

} // namespace

// ============================================================================
// 1. Header/content bounds at several window sizes
// ============================================================================

TEST_F(DetachedPanelWindowLayoutTest, HeaderAndPanelBoundsAtSeveralSizes) {
    DetachedPanelWindow window(panel, button, title, "testWindowBounds", &appProperties, nullptr, &shortcutManager);

    for (const auto size : {juce::Point<int>{400, 300}, juce::Point<int>{800, 600}, juce::Point<int>{1024, 768}}) {
        window.setBounds(0, 0, size.x, size.y);

        // The header strip is title-bounds union button-bounds -- both borrowed components the
        // test itself owns, so their post-layout bounds are directly readable.
        const auto headerBottom = std::max(title.getBottom(), button.getBottom());
        EXPECT_LE(headerBottom, DetachedPanelWindow::kHeaderStripHeight)
            << "header contents must stay within the fixed header strip height at " << size.x << "x" << size.y;
        EXPECT_EQ(title.getY(), 0);
        EXPECT_EQ(button.getY(), 0);
        EXPECT_EQ(button.getWidth(), DetachedPanelWindow::kHeaderStripHeight)
            << "the button is a square the header strip's own height";
        EXPECT_EQ(button.getRight(), title.getParentComponent()->getWidth())
            << "the button sits flush against the content's right edge";

        // The panel fills everything below the header, no gap and no overlap.
        EXPECT_EQ(panel.getY(), DetachedPanelWindow::kHeaderStripHeight)
            << "the panel must start exactly where the header strip ends -- no overlap";
        EXPECT_EQ(panel.getWidth(), title.getParentComponent()->getWidth());
        EXPECT_GE(panel.getHeight(), 0);
        EXPECT_LE(panel.getBottom(), window.getContentForTest()->getHeight())
            << "the panel must never extend past its content's own bottom edge";
    }
}

TEST_F(DetachedPanelWindowLayoutTest, NoOverlapBetweenHeaderAndPanelAtEverySize) {
    DetachedPanelWindow window(panel, button, title, "testWindowBounds", &appProperties, nullptr, &shortcutManager);

    for (const auto size : {juce::Point<int>{320, 240}, juce::Point<int>{640, 420}, juce::Point<int>{1400, 900}}) {
        window.setBounds(0, 0, size.x, size.y);
        const juce::Rectangle<int> headerArea(0, 0, title.getParentComponent()->getWidth(),
                                              DetachedPanelWindow::kHeaderStripHeight);
        EXPECT_FALSE(headerArea.intersects(panel.getBounds()))
            << "the header strip and the hosted panel must never overlap at " << size.x << "x" << size.y;
    }
}

// ============================================================================
// 2. Minimum-size behaviour
// ============================================================================

// A window shorter than the header strip itself (never reachable through the real constrainer,
// which floors at kMinPlausibleWidth/Height -- but the `Content` wrapper's own resized() has no
// such floor, and DetachablePanelHost's docked header handles this the same way) must clip
// gracefully rather than assert or produce a negative-height panel.
TEST_F(DetachedPanelWindowLayoutTest, WindowShorterThanHeaderStripClipsGracefully) {
    DetachedPanelWindow window(panel, button, title, "testWindowBounds", &appProperties, nullptr, &shortcutManager);
    window.setBounds(0, 0, 300, 10); // shorter than kHeaderStripHeight (22)

    EXPECT_GE(panel.getHeight(), 0) << "the panel must never be laid out with a negative height";
    EXPECT_LE(title.getBottom(), 10);
    EXPECT_LE(button.getBottom(), 10);
}

TEST_F(DetachedPanelWindowLayoutTest, ZeroWidthWindowLaysOutWithoutCrashing) {
    DetachedPanelWindow window(panel, button, title, "testWindowBounds", &appProperties, nullptr, &shortcutManager);
    window.setBounds(0, 0, 0, 200);

    EXPECT_GE(panel.getWidth(), 0);
    EXPECT_GE(button.getWidth(), 0);
}

// ============================================================================
// 3. Render-to-image
// ============================================================================

TEST_F(DetachedPanelWindowLayoutTest, HeaderAndPanelRegionsBothPaintNonEmptyPixels) {
    DetachedPanelWindow window(panel, button, title, "testWindowBounds", &appProperties, nullptr, &shortcutManager);
    window.setBounds(0, 0, 640, 420);

    auto* content = window.getContentForTest();
    ASSERT_NE(content, nullptr);
    const auto img = content->createComponentSnapshot(content->getLocalBounds());
    ASSERT_GT(img.getWidth(), 0);
    ASSERT_GT(img.getHeight(), 0);

    // Header: the title's own opaque background (set in SetUp) must show up somewhere inside its
    // bounds -- proves the header strip is actually being painted, not just laid out.
    bool headerPainted = false;
    for (int x = title.getX(); x < title.getRight() && !headerPainted; ++x)
        for (int y = title.getY(); y < title.getBottom() && !headerPainted; ++y)
            if (img.getPixelAt(x, y) == juce::Colours::hotpink)
                headerPainted = true;
    EXPECT_TRUE(headerPainted) << "the header strip (title) must render its own opaque background";

    // Panel: FillPanel's solid fill must show up somewhere inside its bounds.
    bool panelPainted = false;
    for (int x = panel.getX(); x < panel.getRight() && !panelPainted; ++x)
        for (int y = panel.getY(); y < panel.getBottom() && !panelPainted; ++y)
            if (img.getPixelAt(x, y) == juce::Colours::limegreen)
                panelPainted = true;
    EXPECT_TRUE(panelPainted) << "the hosted panel must render its own content below the header";
}

TEST_F(DetachedPanelWindowLayoutTest, PanelRenderScalesWithSeveralWindowSizes) {
    DetachedPanelWindow window(panel, button, title, "testWindowBounds", &appProperties, nullptr, &shortcutManager);

    for (const auto size : {juce::Point<int>{400, 300}, juce::Point<int>{1024, 768}}) {
        window.setBounds(0, 0, size.x, size.y);
        auto* content = window.getContentForTest();
        ASSERT_NE(content, nullptr);
        const auto img = content->createComponentSnapshot(content->getLocalBounds());

        bool panelPainted = false;
        for (int x = panel.getX(); x < panel.getRight() && !panelPainted; ++x)
            for (int y = panel.getY(); y < panel.getBottom() && !panelPainted; ++y)
                if (img.getPixelAt(x, y) == juce::Colours::limegreen)
                    panelPainted = true;
        EXPECT_TRUE(panelPainted) << "the panel must still render at " << size.x << "x" << size.y;
    }
}

// ============================================================================
// 4. FRO228 -- themed background + the header button's own icon actually paints
// ============================================================================

namespace {
// Distinctive from both Theme.h's own default surface and the ctor's juce::Colours::darkgrey
// fallback -- same reasoning as DetachedPanelWindowTests.cpp's own themeWithDistinctiveSurface.
synth::theme::Theme themeWithDistinctiveColours() {
    synth::theme::Theme theme;
    theme.colors.surface = juce::Colour(0xff7744CC);
    theme.colors.textMuted = juce::Colour(0xffAABBCC);
    theme.colors.textPrimary = juce::Colour(0xffFFEE11);
    return theme;
}
} // namespace

TEST_F(DetachedPanelWindowLayoutTest, BackgroundMatchesThemeSurfaceAndHeaderButtonPaintsItsIcon) {
    synth::theme::AppLookAndFeel lf;
    lf.applyTheme(themeWithDistinctiveColours());

    DetachedPanelWindow window(panel, button, title, "testWindowBounds", &appProperties, &lf, &shortcutManager);
    window.setBounds(0, 0, 640, 420);

    EXPECT_EQ(window.getBackgroundColour(), juce::Colour(0xff7744CC))
        << "the window's own background must resolve to the theme's surface token";

    auto* content = window.getContentForTest();
    ASSERT_NE(content, nullptr);
    const auto img = content->createComponentSnapshot(content->getLocalBounds());

    // Before FRO228's fix, the reparented header button never had a Drawable assigned to it at
    // all (an icon-only ImageFitted DrawableButton with none set paints nothing), so every pixel in
    // its 22x22 bounds was the plain theme surface: AppLookAndFeel::drawDrawableButton() paints NO
    // fill at all for an enabled, at-rest, non-toggled button (Source/UI/Theme/AppLookAndFeel/
    // AppLookAndFeelButtons.cpp), so "any non-surface pixel here" is a real proof the icon painted,
    // not an artifact of some other themed background this test would pass without the fix.
    bool sawNonSurfacePixel = false;
    for (int x = button.getX(); x < button.getRight() && !sawNonSurfacePixel; ++x)
        for (int y = button.getY(); y < button.getBottom() && !sawNonSurfacePixel; ++y)
            if (img.getPixelAt(x, y) != juce::Colour(0xff7744CC))
                sawNonSurfacePixel = true;
    EXPECT_TRUE(sawNonSurfacePixel)
        << "the header's redock/close button must actually paint its icon, not just its background";
}

// FRO228: a "glass" theme's surface is translucent; filling a top-level window with it let the OS
// window backing show through as flat grey. The background must be the surface laid over bg0, opaque.
TEST_F(DetachedPanelWindowLayoutTest, TranslucentThemeSurfaceStillGivesAnOpaqueBackground) {
    synth::theme::Theme theme;
    theme.colors.bg0 = juce::Colour(0xff101010);
    theme.colors.surface = juce::Colour(0x997744CC);
    synth::theme::AppLookAndFeel lf;
    lf.applyTheme(theme);

    DetachedPanelWindow window(panel, button, title, "testWindowBounds", &appProperties, &lf, &shortcutManager);

    EXPECT_TRUE(window.getBackgroundColour().isOpaque());
    EXPECT_EQ(window.getBackgroundColour(), theme.colors.bg0.overlaidWith(theme.colors.surface));
}
