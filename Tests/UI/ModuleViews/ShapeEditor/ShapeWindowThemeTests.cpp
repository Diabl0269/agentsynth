// ShapeWindowThemeTests.cpp -- the shape editor window (and every dialog that goes through launchDialog) is backed
// with an opaque, themed colour: a glass theme's translucent surface must never leave JUCE's light grey showing.
#include "../../Theme/ThemeSweep/ThemeSweepSupport.h"
#include "ShapeEditorTestHelpers.h"
#include "UI/Layout/PopupMotion.h"
#include "UI/ModuleViews/ShapeEditor/ShapeListEditor.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Theme/BuiltInThemes.h"

using namespace synth::ui;
using namespace synth::ui::test;

namespace {

constexpr int kWidth = 720;
constexpr int kHeight = 520;

} // namespace

TEST(ShapeWindowTheme, WindowBackgroundIsOpaqueAndThemedForEveryBuiltInTheme) {
    std::vector<juce::Colour> seen;
    for (const auto& theme : synth::theme::builtInThemes()) {
        const auto bg = synth::theme::windowBackgroundFor(theme);
        EXPECT_EQ(bg.getAlpha(), 255) << theme.name;
        EXPECT_NE(bg, juce::Colours::lightgrey) << theme.name;
        for (const auto& other : seen)
            EXPECT_NE(bg, other) << theme.name;
        seen.push_back(bg);
    }
    EXPECT_EQ(seen.size(), 4u);
}

TEST(ShapeWindowTheme, ShapeEditorPaintsOpaqueThemedPixelsInEveryTheme) {
    juce::MessageManager::getInstance();
    for (const auto& theme : synth::theme::builtInThemes()) {
        ShapeRig rig;
        synth::theme::AppLookAndFeel lookAndFeel;
        lookAndFeel.applyTheme(theme);
        ShapeListEditor editor(rig.adapter, 0);
        editor.setLookAndFeel(&lookAndFeel);
        editor.setSize(kWidth, kHeight);

        juce::Image image(juce::Image::ARGB, kWidth, kHeight, true, juce::SoftwareImageType());
        {
            juce::Graphics g(image);
            editor.paintEntireComponent(g, false);
        }
        EXPECT_TRUE(editor.isOpaque()) << theme.name;
        const juce::Point<int> probes[] = {
            {0, 0}, {kWidth - 1, 0}, {0, kHeight - 1}, {kWidth - 1, kHeight - 1}, {2, kHeight / 2}};
        for (const auto& pt : probes) {
            const auto pixel = image.getPixelAt(pt.x, pt.y);
            EXPECT_EQ(pixel.getAlpha(), 255) << theme.name << " at " << pt.x << "," << pt.y;
            EXPECT_NE(pixel, juce::Colours::lightgrey) << theme.name;
            EXPECT_EQ(pixel, synth::theme::windowBackgroundFor(theme)) << theme.name << " at " << pt.x << "," << pt.y;
        }

        synth::test::theme_sweep::writeSweepPng("shape-window", theme.id, image);
        editor.setLookAndFeel(nullptr);
    }
}

TEST(ShapeWindowTheme, LaunchDialogGivesDialogsTheThemedBackground) {
    const auto themes = synth::theme::builtInThemes();
    synth::theme::AppLookAndFeel lookAndFeel;
    lookAndFeel.applyTheme(themes[1]); // Neon: translucent surface
    juce::Component anchor;
    anchor.setLookAndFeel(&lookAndFeel);

    juce::DialogWindow::LaunchOptions options;
    options.componentToCentreAround = &anchor;
    popup_motion::applyThemedBackground(options);
    EXPECT_EQ(options.dialogBackgroundColour, synth::theme::windowBackgroundFor(themes[1]));
    EXPECT_NE(options.dialogBackgroundColour, juce::Colours::lightgrey);

    juce::DialogWindow::LaunchOptions explicitOptions;
    explicitOptions.componentToCentreAround = &anchor;
    explicitOptions.dialogBackgroundColour = juce::Colours::hotpink;
    popup_motion::applyThemedBackground(explicitOptions);
    EXPECT_EQ(explicitOptions.dialogBackgroundColour, juce::Colours::hotpink);

    anchor.setLookAndFeel(nullptr);
}
