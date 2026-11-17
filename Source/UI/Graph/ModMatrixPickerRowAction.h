#pragma once

#include "UI/Graph/ModMatrixPicker.h"
#include "UI/Layout/FocusRing.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

namespace synth::ui {

// A row's action button: a small icon at the row's right end. Keyboard focus stays in the search field, so the ring it
// paints follows the picker's own selection (Tab), not real focus.
class RowActionButton
    : public juce::Component
    , public juce::SettableTooltipClient {
public:
    std::function<void()> onPress;

    void bind(const ModMatrixPicker::RowAction& action) {
        setTitle(action.title);
        setTooltip(action.tooltip);
        if (icon_ != action.icon) {
            icon_ = action.icon;
            drawable_.reset();
        }
        repaint();
    }
    void setSelected(bool on) {
        if (selected_ != on) {
            selected_ = on;
            repaint();
        }
    }
    void setColours(juce::Colour muted, juce::Colour accent) {
        muted_ = muted;
        accent_ = accent;
        repaint();
    }

    void paint(juce::Graphics& g) override {
        const auto area = getLocalBounds().toFloat();
        if (hover_ || selected_) {
            g.setColour(accent_.withAlpha(0.2f));
            g.fillRoundedRectangle(area.reduced(1.0f), 4.0f);
        }
        const auto glyph = area.reduced(4.0f);
        if (drawable_ == nullptr)
            if (auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel()))
                drawable_ = lf->getIcon(icon_); // the theme's tinted copy; a theme switch drops it
        if (drawable_ != nullptr) {
            drawable_->drawWithin(g, glyph, juce::RectanglePlacement::centred, 1.0f);
        } else {
            // No themed icon (headless, or no assets): a plain window outline with its title bar.
            g.setColour(selected_ ? accent_ : muted_);
            g.drawRoundedRectangle(glyph.reduced(0.5f), 2.0f, 1.2f);
            g.drawLine(glyph.getX(), glyph.getY() + 3.5f, glyph.getRight(), glyph.getY() + 3.5f, 1.2f);
        }
        if (selected_)
            synth::ui::paintFocusRingAlways(g, area, *this, 4.0f);
    }
    void lookAndFeelChanged() override { drawable_.reset(); }
    void mouseEnter(const juce::MouseEvent&) override { setHover(true); }
    void mouseExit(const juce::MouseEvent&) override { setHover(false); }
    void mouseUp(const juce::MouseEvent& e) override {
        if (getLocalBounds().contains(e.getPosition()) && onPress)
            onPress();
    }

private:
    void setHover(bool on) {
        hover_ = on;
        repaint();
    }
    synth::theme::Icon icon_ = synth::theme::Icon::ActionDetachWindow;
    std::unique_ptr<juce::Drawable> drawable_;
    juce::Colour muted_ = juce::Colours::grey;
    juce::Colour accent_ = juce::Colours::lightblue;
    bool selected_ = false;
    bool hover_ = false;
};

} // namespace synth::ui
