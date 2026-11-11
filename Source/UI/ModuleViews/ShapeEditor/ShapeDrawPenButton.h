#pragma once

// ShapeDrawPenButton.h -- the shape window's Draw button: a text button that also shows the chosen pen's icon (the same
// DrawShapeIcons the timeline's pen button and the flyout rows use) and lights while a pen is on. The icon and the word
// are drawn centred together as one group.
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Timeline/AutomationLanes/LaneShapes/DrawShapeIcons.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>

namespace synth::ui {

class ShapeDrawPenButton : public juce::TextButton {
public:
    using juce::TextButton::TextButton;

    /** The pen the button shows; none is the button at rest (the last pen's icon, dimmed). */
    void setPen(std::optional<DrawShape> pen, DrawShape lastShape) {
        if (pen_ == pen && shape_ == (pen.value_or(lastShape)))
            return;
        pen_ = pen;
        shape_ = pen.value_or(lastShape);
        setToggleState(pen.has_value(), juce::dontSendNotification);
        repaint();
    }
    std::optional<DrawShape> getPen() const noexcept { return pen_; }
    DrawShape getShownShape() const noexcept { return shape_; }

    static constexpr int kIconSize = 14;
    /** The space between the icon and the word. */
    static constexpr float kIconGap = 6.0f;

    /** Where the icon and the word sit: one group, centred in the button; the word shrinks if it must. */
    struct ContentLayout {
        juce::Rectangle<float> icon, text;
    };
    ContentLayout contentLayout() {
        constexpr float kPad = 4.0f;
        const float height = (float)getHeight();
        const auto font = getLookAndFeel().getTextButtonFont(*this, getHeight());
        const float wanted = juce::GlyphArrangement::getStringWidth(font, getButtonText());
        const float room = juce::jmax(0.0f, (float)getWidth() - 2.0f * kPad - kIconSize - kIconGap);
        const float textWidth = juce::jmin(wanted, room);
        const float group = (float)kIconSize + kIconGap + textWidth;
        const float left = juce::jmax(kPad, ((float)getWidth() - group) * 0.5f);
        return {juce::Rectangle<float>(left, (height - kIconSize) * 0.5f, (float)kIconSize, (float)kIconSize),
                juce::Rectangle<float>(left + kIconSize + kIconGap, 0.0f, textWidth, height)};
    }

    // The word goes through the look-and-feel's own button text (its colours, hover ease and dimming), shifted right so
    // that the icon and the word together sit centred.
    void paintButton(juce::Graphics& g, bool highlighted, bool down) override {
        auto& laf = getLookAndFeel();
        laf.drawButtonBackground(g, *this, findColour(getToggleState() ? buttonOnColourId : buttonColourId),
                                 highlighted, down);
        const auto layout = contentLayout();
        {
            juce::Graphics::ScopedSaveState state(g);
            g.addTransform(
                juce::AffineTransform::translation(layout.text.getCentreX() - (float)getWidth() * 0.5f, 0.0f));
            laf.drawButtonText(g, *this, highlighted, down);
        }
        const auto& colors = synth::theme::themeOf(*this).colors;
        paintDrawShapeIcon(g, shape_, layout.icon, pen_.has_value() ? findColour(textColourOnId) : colors.textPrimary,
                           pen_.has_value() ? 1.0f : 0.55f);
    }

private:
    std::optional<DrawShape> pen_;
    DrawShape shape_ = DrawShape::Free;
};

} // namespace synth::ui
