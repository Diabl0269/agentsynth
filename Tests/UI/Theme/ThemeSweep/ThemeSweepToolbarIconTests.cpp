// ThemeSweepToolbarIconTests.cpp -- icon hosts that set their glyph in a constructor, before they have a parent. The
// theme is set on the root first, the host is built afterwards and then added, so the glyph's first build resolved
// the default look and feel; it must still show the root's theme colour, in every theme (docs/layout/theming.md,
// "Theme sweep guard"). Images use SoftwareImageType() so the pixels read back on every platform.
#include "ThemeSweepSupport.h"
#include "UI/Chrome/ToolbarButton/ToolbarButton.h"
#include "UI/Mixer/MixerHeader/MixerIconButton.h"
#include "UI/Mixer/MixerPanelComponent/MixerPanelToolbar.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Theme/BuiltInThemes.h"
#include "UI/Timeline/ClipSizingButton.h"
#include <gtest/gtest.h>

using namespace synth::test::theme_sweep;
using synth::ui::ToolbarGroup;

namespace {

// Pixels of exactly `colour`: the glyph's strokes are thin, so a fully covered pixel of the hue is the mark of the
// theme's colour (an anti-aliased edge blends it with its neighbours and never equals it).
int solidPixelsOf(const juce::Image& image, juce::Colour colour) {
    int count = 0;
    for (int y = 0; y < image.getHeight(); ++y)
        for (int x = 0; x < image.getWidth(); ++x)
            if (image.getPixelAt(x, y) == colour)
                ++count;
    return count;
}

void collectToolbarButtons(juce::Component& parent, std::vector<synth::ui::ToolbarButton*>& out) {
    for (auto* child : parent.getChildren()) {
        if (auto* button = dynamic_cast<synth::ui::ToolbarButton*>(child))
            out.push_back(button);
        collectToolbarButtons(*child, out);
    }
}

// The Desktop default look and feel stands for the app's default theme; the root carries the sweep theme.
struct ThemedRoot {
    ThemedRoot(const synth::theme::Theme& theme, const synth::theme::Theme& defaultTheme)
        : previous(&juce::Desktop::getInstance().getDefaultLookAndFeel()) {
        defaultLook.applyTheme(defaultTheme);
        look.applyTheme(theme);
        juce::Desktop::getInstance().setDefaultLookAndFeel(&defaultLook);
        root.setLookAndFeel(&look);
        root.setSize(400, 80);
    }
    ~ThemedRoot() {
        root.deleteAllChildren();
        root.setLookAndFeel(nullptr);
        juce::Desktop::getInstance().setDefaultLookAndFeel(previous);
    }
    synth::theme::AppLookAndFeel defaultLook;
    synth::theme::AppLookAndFeel look;
    juce::Component root;
    juce::LookAndFeel* previous;
};

const synth::theme::Theme& otherTheme(const std::vector<synth::theme::Theme>& themes, size_t i) {
    for (size_t k = 1; k < themes.size(); ++k)
        if (themes[(i + k) % themes.size()].id != themes[i].id &&
            themes[(i + k) % themes.size()].colors.hueAmber != themes[i].colors.hueAmber)
            return themes[(i + k) % themes.size()];
    return themes[(i + 1) % themes.size()];
}

} // namespace

// Regression test for AGE203: the mixer toolbar's buttons showed the default group hue until a theme switch.
TEST(ThemeSweepToolbarIcons, MixerToolbarButtonsTakeTheRootsThemeColourFromTheFirstOpen) {
    juce::MessageManager::getInstance();
    const auto themes = sweepThemes();
    for (size_t i = 0; i < themes.size(); ++i) {
        ThemedRoot env(themes[i], otherTheme(themes, i));
        auto* toolbar = new synth::ui::MixerPanelToolbar();
        env.root.addAndMakeVisible(toolbar);
        toolbar->setBounds(env.root.getLocalBounds());
        toolbar->resized();
        std::vector<synth::ui::ToolbarButton*> buttons;
        collectToolbarButtons(*toolbar, buttons);
        int checked = 0;
        for (auto* button : buttons) {
            if (button->getWidth() <= 0 || button->getIcon() == synth::theme::Icon::kCount)
                continue;
            const auto hue = synth::ui::toolbarGroupHue(themes[i], button->getGroup());
            EXPECT_GT(solidPixelsOf(renderToImage(*button), hue), 0)
                << themes[i].id << ": " << button->getName() << " does not show the theme's colour";
            ++checked;
        }
        EXPECT_EQ(checked, 5) << themes[i].id;
    }
}

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
