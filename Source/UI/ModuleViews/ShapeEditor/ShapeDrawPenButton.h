#pragma once

// ShapeDrawPenButton.h -- the shape window's Draw button: a text button that also shows the chosen pen's icon (the same
// DrawShapeIcons the timeline's pen button and the flyout rows use) and lights while a pen is on.
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

    void paintButton(juce::Graphics& g, bool highlighted, bool down) override {
        juce::TextButton::paintButton(g, highlighted, down);
        const auto& colors = synth::theme::themeOf(*this).colors;
        const auto area =
            juce::Rectangle<float>(7.0f, ((float)getHeight() - kIconSize) * 0.5f, (float)kIconSize, (float)kIconSize);
        paintDrawShapeIcon(g, shape_, area, pen_.has_value() ? findColour(textColourOnId) : colors.textPrimary,
                           pen_.has_value() ? 1.0f : 0.55f);
    }

private:
    std::optional<DrawShape> pen_;
    DrawShape shape_ = DrawShape::Free;
};

} // namespace synth::ui
