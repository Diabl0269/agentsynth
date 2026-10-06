#pragma once

// Shared fixture for the top-bar button tests: a themed look-and-feel, buttons built the way
// MainComponent::applyToolbarIcons builds them, and pixel helpers. Images use SoftwareImageType(): the
// default native image type reads back zeros on a Windows runner.

#include "UI/Chrome/ToolbarButton/ToolbarButton.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Theme/BuiltInThemes.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <vector>

namespace toolbartest {

using synth::theme::Icon;
using synth::ui::ToolbarButton;
using synth::ui::ToolbarGroup;

struct IconSpec {
    Icon icon;
    ToolbarGroup group;
    const char* caption;
};

// Every top-bar button, in toolbar order, with the group MainComponent gives it.
inline const std::vector<IconSpec>& allToolbarIcons() {
    static const std::vector<IconSpec> specs = {
        {Icon::ToggleLibrary, ToolbarGroup::View, "Hide Library"},
        {Icon::ActionNew, ToolbarGroup::File, "New"},
        {Icon::ActionSave, ToolbarGroup::File, "Save"},
        {Icon::ActionLoad, ToolbarGroup::File, "Load"},
        {Icon::ActionSettings, ToolbarGroup::Housekeeping, "Settings"},
        {Icon::ActionFeedback, ToolbarGroup::Feedback, "Feedback"},
        {Icon::ActionUndo, ToolbarGroup::Edit, "Undo"},
        {Icon::ActionRedo, ToolbarGroup::Edit, "Redo"},
        {Icon::ActionAutoArrange, ToolbarGroup::Edit, "Auto Arrange"},
        {Icon::ToggleMinimap, ToolbarGroup::View, "Hide Minimap"},
        {Icon::ToggleMatrix, ToolbarGroup::View, "Show Matrix"},
        {Icon::ToggleAI, ToolbarGroup::AI, "Show AI"},
        {Icon::TogglePanel, ToolbarGroup::View, "Hide Panel"},
        {Icon::ThemeToggle, ToolbarGroup::Housekeeping, "Light Mode"},
    };
    return specs;
}

// Restores the real reduced-motion answer whatever a test did to it.
struct ReducedMotionGuard {
    ~ReducedMotionGuard() { synth::ui::setReducedMotionForTest(std::nullopt); }
};

// Members are public so the shared check helpers in each test file can reach them.
class ToolbarButtonTest : public ::testing::Test {
public:
    ToolbarButtonTest() { lf_.applyTheme(synth::theme::makeObsidian()); }
    ~ToolbarButtonTest() override {
        for (auto& b : buttons_)
            b->setLookAndFeel(nullptr);
    }

    // A visible button with its glyph, group and caption, 88 x 44 like a wide-mode toolbar slot.
    ToolbarButton& make(Icon icon, ToolbarGroup group, const juce::String& caption = "Caption") {
        auto& b = *buttons_.emplace_back(std::make_unique<ToolbarButton>("b"));
        b.setLookAndFeel(&lf_);
        b.setIcon(icon, group);
        b.setButtonText(caption);
        b.setSize(88, 44);
        b.setVisible(true);
        return b;
    }

    void useTheme(const synth::theme::Theme& theme) {
        lf_.applyTheme(theme);
        for (auto& b : buttons_)
            b->refreshArt();
    }

    const synth::theme::Theme& theme() const { return lf_.getTheme(); }

    // The button painted over the theme's bar colour, as it sits in the toolbar.
    juce::Image render(ToolbarButton& b) const {
        juce::Image image(juce::Image::ARGB, b.getWidth(), b.getHeight(), true, juce::SoftwareImageType());
        juce::Graphics g(image);
        g.fillAll(theme().colors.bg0);
        b.paintEntireComponent(g, false);
        return image;
    }

    synth::theme::AppLookAndFeel lf_;
    std::vector<std::unique_ptr<ToolbarButton>> buttons_;
};

// A pixel of the chip that the glyph never covers: inside its left edge, at mid height.
inline juce::Point<int> chipOnlyPixel(const ToolbarButton& b) {
    const auto chip = synth::theme::toolbarChipBounds(b);
    return {(int)chip.getX() + 2, (int)chip.getCentreY()};
}

// The screen pixel under icon-grid point `p` (unlifted, unsquashed).
inline juce::Point<int> iconPixel(const ToolbarButton& b, juce::Point<float> p) {
    const auto mapped = p.transformedBy(synth::theme::toolbarIconTransform(b));
    return {(int)std::floor(mapped.x), (int)std::floor(mapped.y)};
}

inline bool near(juce::Colour a, juce::Colour b, int tol = 3) {
    return std::abs((int)a.getRed() - (int)b.getRed()) <= tol &&
           std::abs((int)a.getGreen() - (int)b.getGreen()) <= tol &&
           std::abs((int)a.getBlue() - (int)b.getBlue()) <= tol;
}

inline ::testing::AssertionResult pixelNear(const juce::Image& img, juce::Point<int> p, juce::Colour expected,
                                            int tol = 3) {
    const auto got = img.getPixelAt(p.x, p.y);
    if (near(got, expected, tol))
        return ::testing::AssertionSuccess();
    return ::testing::AssertionFailure() << "pixel (" << p.x << "," << p.y << ") is " << got.toDisplayString(true)
                                         << ", expected " << expected.toDisplayString(true);
}

// True when some pixel in `area` is within `tol` of `target`.
inline bool areaContains(const juce::Image& img, juce::Rectangle<int> area, juce::Colour target, int tol = 6) {
    area = area.getIntersection(img.getBounds());
    for (int y = area.getY(); y < area.getBottom(); ++y)
        for (int x = area.getX(); x < area.getRight(); ++x)
            if (near(img.getPixelAt(x, y), target, tol))
                return true;
    return false;
}

inline juce::Rectangle<int> iconArea(const ToolbarButton& b) {
    const auto chip = synth::theme::toolbarChipBounds(b);
    return chip.withSizeKeepingCentre(19.0f, 19.0f).getSmallestIntegerContainer();
}

inline juce::Rectangle<int> captionArea(const ToolbarButton& b) {
    const auto chip = synth::theme::toolbarChipBounds(b);
    return juce::Rectangle<float>(0.0f, chip.getBottom() + 1.0f, (float)b.getWidth(),
                                  (float)b.getHeight() - chip.getBottom() - 1.0f)
        .getSmallestIntegerContainer();
}

// A synthesized mouse event on `c`, delivered through the Component interface like the real one.
inline juce::MouseEvent mouseEventOn(juce::Component& c) {
    const auto pos = c.getLocalBounds().getCentre().toFloat();
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), pos, {}, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                            &c, &c, juce::Time::getCurrentTime(), pos, juce::Time::getCurrentTime(), 1, false);
}

} // namespace toolbartest
