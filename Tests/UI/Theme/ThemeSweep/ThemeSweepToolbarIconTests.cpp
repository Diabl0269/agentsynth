// ThemeSweepToolbarIconTests.cpp -- icon hosts that set their glyph in a constructor, before they have a parent. The
// theme is set on the root first, the host is built afterwards and then added, so the glyph's first build resolved
// the default look and feel; it must still show the root's theme colour, in every theme (docs/layout/theming.md,
// "Theme sweep guard"). Images use SoftwareImageType() so the pixels read back on every platform.
#include "ThemeSweepSupport.h"
#include "ThemeSweepToolbarHelpers.h"
#include "UI/Chrome/ToolbarButton/ToolbarButton.h"
#include "UI/Mixer/MixerHeader/MixerIconButton.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Timeline/ClipSizingButton.h"
#include <gtest/gtest.h>

using namespace synth::test::theme_sweep;
using synth::ui::ToolbarGroup;

TEST(ThemeSweepToolbarIcons, ClipSizingAndPlainToolbarButtonsTakeTheRootsThemeColourFromTheFirstOpen) {
    juce::MessageManager::getInstance();
    const auto themes = sweepThemes();
    for (size_t i = 0; i < themes.size(); ++i) {
        ThemedRoot env(themes[i], otherTheme(themes, i));
        auto* sizing = new synth::ui::ClipSizingButton();
        auto* plain = new synth::ui::ToolbarButton("plain");
        plain->setIcon(synth::theme::Icon::ToolbarAddTrack, ToolbarGroup::Edit);
        env.root.addAndMakeVisible(sizing);
        env.root.addAndMakeVisible(plain);
        sizing->setBounds(0, 0, sizing->getFixedWidth(), 28);
        plain->setBounds(200, 0, 40, 40);
        const auto hue = synth::ui::toolbarGroupHue(themes[i], ToolbarGroup::Edit);
        EXPECT_GT(solidPixelsOf(renderToImage(*sizing), hue), 0) << themes[i].id << ": clip sizing";
        EXPECT_GT(solidPixelsOf(renderToImage(*plain), hue), 0) << themes[i].id << ": plain button";
    }
}

TEST(ThemeSweepToolbarIcons, MixerIconButtonTakesTheRootsThemeColourFromTheFirstOpen) {
    juce::MessageManager::getInstance();
    const auto themes = sweepThemes();
    for (size_t i = 0; i < themes.size(); ++i) {
        ThemedRoot env(themes[i], otherTheme(themes, i));
        auto* button = new synth::ui::MixerIconButton("icon");
        button->setIcon(synth::theme::Icon::ModuleBypass);
        env.root.addAndMakeVisible(button);
        button->setBounds(0, 0, 24, 24);
        const auto& colours = themes[i].colors;
        // The glyph's resting tone is the theme's muted text colour: some solid pixels carry it, and none the
        // default theme's, which differs.
        EXPECT_GT(solidPixelsOf(renderToImage(*button), colours.textMuted), 0) << themes[i].id;
    }
}
