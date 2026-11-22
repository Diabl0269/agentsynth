// ToolbarFocusRingBoxTests.cpp -- the guard for bug kind focus-ring-box on the top bar's button: nothing a
// ToolbarButton paints lies outside its focus ring's rounded box. Every layout (Stacked, Inline, InlineCompact,
// InlineStrip), lit and unlit, with and without a caption, at rest, hovered and pressed, in all four themes at Soft
// and Rich depth, is drawn with the ring on over a transparent image; every pixel wholly outside the ring's outer
// edge must still be untouched. Regression test for FRO2200: the lit inline pill filled the full bounds at its own
// radius, so it showed outside the ring. Images use SoftwareImageType() so the pixels read back on every platform.
#include "ToolbarButtonTestHelpers.h"
#include "UI/Layout/FocusRing.h"
#include "UI/Theme/DepthLevel.h"
#include <cmath>

using namespace toolbartest;
using synth::theme::DepthLevel;
using synth::ui::ToolbarButtonLayout;

namespace {

constexpr float kRingInset = 1.0f; // the ring's outer edge stands 1 px in from the button's bounds
// A pixel counts as outside when its whole square is: its centre is at least this far (half a pixel's diagonal, and a
// little more) beyond the edge.
constexpr float kOutsideBy = 0.8f;

enum class State { Rest, Hover, Pressed };
const char* name(State s) { return s == State::Rest ? "rest" : (s == State::Hover ? "hover" : "pressed"); }

struct RingsOn {
    RingsOn() { synth::ui::setFocusRingsShownForTest(true); }
    ~RingsOn() { synth::ui::setFocusRingsShownForTest(false); }
};

// How far `p` is outside the rounded rectangle (negative inside).
float distanceOutside(juce::Point<float> p, juce::Rectangle<float> box, float radius) {
    const float qx = std::abs(p.x - box.getCentreX()) - (box.getWidth() * 0.5f - radius);
    const float qy = std::abs(p.y - box.getCentreY()) - (box.getHeight() * 0.5f - radius);
    return std::hypot(std::max(qx, 0.0f), std::max(qy, 0.0f)) + std::min(std::max(qx, qy), 0.0f) - radius;
}

const char* layoutName(ToolbarButtonLayout l) {
    switch (l) {
    case ToolbarButtonLayout::Stacked:
        return "stacked";
    case ToolbarButtonLayout::Inline:
        return "inline";
    case ToolbarButtonLayout::InlineCompact:
        return "inline-compact";
    case ToolbarButtonLayout::InlineStrip:
        return "inline-strip";
    }
    return "?";
}

} // namespace

TEST_F(ToolbarButtonTest, NothingAToolbarButtonPaintsLiesOutsideItsFocusRing) {
    RingsOn rings;
    for (auto theme : synth::theme::builtInThemes())
        for (const auto depth : {DepthLevel::Soft, DepthLevel::Rich}) {
            theme.treatment.depth = depth;
            useTheme(theme);
            for (const auto layout : {ToolbarButtonLayout::Stacked, ToolbarButtonLayout::Inline,
                                      ToolbarButtonLayout::InlineCompact, ToolbarButtonLayout::InlineStrip})
                for (const bool captioned : {true, false})
                    for (const bool lit : {false, true})
                        for (const auto state : {State::Rest, State::Hover, State::Pressed}) {
                            auto& b = make(Icon::ToggleMinimap, ToolbarGroup::View, captioned ? "Minimap" : "");
                            b.setLayout(layout);
                            b.setSize(layout == ToolbarButtonLayout::Stacked ? 88 : b.getPreferredWidth() + 12,
                                      layout == ToolbarButtonLayout::Stacked ? 44 : 28);
                            b.setContentCentred(!captioned || lit);
                            b.setToggleState(lit, juce::dontSendNotification);
                            b.setState(state == State::Rest    ? juce::Button::buttonNormal
                                       : state == State::Hover ? juce::Button::buttonOver
                                                               : juce::Button::buttonDown);
                            juce::Image img(juce::Image::ARGB, b.getWidth(), b.getHeight(), true,
                                            juce::SoftwareImageType());
                            {
                                juce::Graphics g(img);
                                b.paintEntireComponent(g, false);
                            }
                            const auto box = b.getLocalBounds().toFloat().reduced(kRingInset);
                            int stray = 0;
                            juce::Point<int> first;
                            for (int y = 0; y < img.getHeight(); ++y)
                                for (int x = 0; x < img.getWidth(); ++x)
                                    if (distanceOutside({x + 0.5f, y + 0.5f}, box, theme.metrics.pillRadius) >
                                            kOutsideBy &&
                                        img.getPixelAt(x, y).getAlpha() != 0) {
                                        if (stray++ == 0)
                                            first = {x, y};
                                    }
                            EXPECT_EQ(stray, 0)
                                << theme.name << (depth == DepthLevel::Rich ? " rich " : " soft ") << layoutName(layout)
                                << (captioned ? " captioned " : " icon-only ") << (lit ? "lit " : "unlit ")
                                << name(state) << ": " << stray << " painted pixels outside the ring, first at ("
                                << first.x << "," << first.y << ")";
                        }
        }
}
