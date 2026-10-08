// The split button's two halves: painting (lit state, glyph, label, focus ring) and the Left/Right hop between them.

#include "SplitButton.h"

#include "FocusRing.h"
#include "UI/Graph/ModDot/ModDotPalette.h"

namespace synth::ui {

class SplitButton::Half final : public juce::Button {
public:
    Half(const SplitButtonHalfSpec& spec, bool leftHalf)
        : juce::Button(spec.title)
        , glyph_(spec.glyph)
        , label_(spec.label)
        , spec_(spec)
        , leftHalf_(leftHalf)
        , hover_(*this) {
        setTitle(spec.title);
        setTooltip(spec.tooltip);
        setWantsKeyboardFocus(true);
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }

    void setLit(bool lit) {
        if (lit_ == lit)
            return;
        lit_ = lit;
        if (spec_.litTitle.isNotEmpty())
            setTitle(lit ? spec_.litTitle : spec_.title);
        repaint();
    }
    void setSibling(Half* sibling) { sibling_ = sibling; }

    bool keyPressed(const juce::KeyPress& key) override {
        if (key == juce::KeyPress::returnKey || key == juce::KeyPress::spaceKey) {
            triggerClick();
            return true;
        }
        const bool toward =
            leftHalf_ ? key.isKeyCode(juce::KeyPress::rightKey) : key.isKeyCode(juce::KeyPress::leftKey);
        if (toward && sibling_ != nullptr) {
            sibling_->grabKeyboardFocus();
            return true;
        }
        return false;
    }
    void mouseEnter(const juce::MouseEvent& e) override {
        juce::Button::mouseEnter(e);
        hover_.setHovered(true);
    }
    void mouseExit(const juce::MouseEvent& e) override {
        juce::Button::mouseExit(e);
        hover_.setHovered(false);
    }

    void paintButton(juce::Graphics& g, bool, bool down) override {
        const auto p = modDotPaletteFor(*this);
        const auto area = getLocalBounds().toFloat().reduced(leftHalf_ ? 2.0f : 1.0f, 1.0f);
        const float r = 6.0f;
        juce::Path shape;
        shape.addRoundedRectangle(area.getX(), area.getY(), area.getWidth(), area.getHeight(), r, r, leftHalf_,
                                  !leftHalf_, leftHalf_, !leftHalf_);
        g.setColour(p.hover.withAlpha(down ? 1.0f : hover_.value()));
        g.fillPath(shape);
        if (lit_) {
            g.setColour(p.accent.withAlpha(0.16f));
            g.fillPath(shape);
        }
        const auto ink = lit_ ? p.accent : modDotGlyphColour(p, glyph_, hover_.value());
        auto inner = getLocalBounds().reduced(10, 0);
        if (label_.isEmpty()) {
            paintModDotGlyph(g, glyph_, inner.toFloat().withSizeKeepingCentre(14.0f, 14.0f), ink);
        } else {
            paintModDotGlyph(g, glyph_, inner.removeFromLeft(14).toFloat().withSizeKeepingCentre(14.0f, 14.0f), ink);
            inner.removeFromLeft(6);
            g.setColour(lit_ ? p.accent : p.text);
            g.setFont(juce::Font(juce::FontOptions(12.5f)));
            g.drawText(label_, inner, juce::Justification::centredLeft, true);
        }
        paintFocusRing(g, area, *this, r);
    }

private:
    ModDotGlyph glyph_;
    juce::String label_;
    SplitButtonHalfSpec spec_;
    bool leftHalf_;
    ModDotHoverFade hover_;
    Half* sibling_ = nullptr;
    bool lit_ = false;
};

SplitButton::SplitButton(const SplitButtonHalfSpec& left, const SplitButtonHalfSpec& right,
                         const juce::String& groupTitle)
    : left_(std::make_unique<Half>(left, true))
    , right_(std::make_unique<Half>(right, false))
    , iconsOnly_(left.label.isEmpty() && right.label.isEmpty()) {
    right_->setClickingTogglesState(true); // the right half is the mode switch: a screen reader reads it as on or off
    left_->setSibling(right_.get());
    right_->setSibling(left_.get());
    addAndMakeVisible(*left_);
    addAndMakeVisible(*right_);
    setTitle(groupTitle);
}

SplitButton::~SplitButton() = default;

juce::Button& SplitButton::leftHalf() noexcept { return *left_; }
juce::Button& SplitButton::rightHalf() noexcept { return *right_; }

void SplitButton::setLeftLit(bool lit) {
    leftLit_ = lit;
    left_->setLit(lit);
}

void SplitButton::setRightLit(bool lit) {
    rightLit_ = lit;
    right_->setLit(lit);
    right_->setToggleState(lit, juce::dontSendNotification);
}

void SplitButton::resized() {
    auto r = getLocalBounds();
    const int leftWidth = iconsOnly_ ? r.getWidth() / 2 : r.getWidth() * 11 / 20;
    left_->setBounds(r.removeFromLeft(leftWidth));
    right_->setBounds(r);
}

void SplitButton::paintOverChildren(juce::Graphics& g) {
    const auto p = modDotPaletteFor(*this);
    g.setColour(p.border);
    g.fillRect(juce::Rectangle<int>(left_->getRight(), 5, 1, getHeight() - 10));
}

} // namespace synth::ui
