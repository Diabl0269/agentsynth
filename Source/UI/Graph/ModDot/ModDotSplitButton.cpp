// The split button's two halves: painting (lit state, glyph, label, focus ring) and the Left/Right hop between them.

#include "ModDotSplitButton.h"

#include "ModDotPalette.h"
#include "UI/Layout/FocusRing.h"

namespace synth::ui {

class ModDotSplitButton::Half final : public juce::Button {
public:
    Half(ModDotGlyph glyph, juce::String label, bool leftHalf)
        : juce::Button(label)
        , glyph_(glyph)
        , label_(std::move(label))
        , leftHalf_(leftHalf)
        , hover_(*this) {
        setWantsKeyboardFocus(true);
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }

    void setLit(bool lit) {
        if (lit_ == lit)
            return;
        lit_ = lit;
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
        paintModDotGlyph(g, glyph_, inner.removeFromLeft(14).toFloat().withSizeKeepingCentre(14.0f, 14.0f), ink);
        inner.removeFromLeft(6);
        g.setColour(lit_ ? p.accent : p.text);
        g.setFont(juce::Font(juce::FontOptions(12.5f)));
        g.drawText(label_, inner, juce::Justification::centredLeft, true);
        paintFocusRing(g, area, *this, r);
    }

private:
    ModDotGlyph glyph_;
    juce::String label_;
    bool leftHalf_;
    ModDotHoverFade hover_;
    Half* sibling_ = nullptr;
    bool lit_ = false;
};

ModDotSplitButton::ModDotSplitButton()
    : list_(std::make_unique<Half>(ModDotGlyph::List, "Add source", true))
    , pick_(std::make_unique<Half>(ModDotGlyph::Crosshair, "Pick on canvas", false)) {
    list_->setTitle("Add source");
    list_->setTooltip("Add source from a list");
    pick_->setTitle("Pick on canvas");
    pick_->setTooltip("Pick a source on the canvas. Esc stops");
    list_->setSibling(pick_.get());
    pick_->setSibling(list_.get());
    addAndMakeVisible(*list_);
    addAndMakeVisible(*pick_);
    setTitle("Add a source");
}

ModDotSplitButton::~ModDotSplitButton() = default;

juce::Button& ModDotSplitButton::listHalf() noexcept { return *list_; }
juce::Button& ModDotSplitButton::pickHalf() noexcept { return *pick_; }

void ModDotSplitButton::setListOpen(bool open) {
    listOpen_ = open;
    list_->setLit(open);
    list_->setTitle(open ? "Add source, list open" : "Add source");
}

void ModDotSplitButton::setPicking(bool on) {
    picking_ = on;
    pick_->setLit(on);
    pick_->setTitle(on ? "Pick on canvas, on" : "Pick on canvas");
}

void ModDotSplitButton::resized() {
    auto r = getLocalBounds();
    const int leftWidth = r.getWidth() * 11 / 20;
    list_->setBounds(r.removeFromLeft(leftWidth));
    pick_->setBounds(r);
}

void ModDotSplitButton::paintOverChildren(juce::Graphics& g) {
    const auto p = modDotPaletteFor(*this);
    g.setColour(p.border);
    g.fillRect(juce::Rectangle<int>(list_->getRight(), 5, 1, getHeight() - 10));
}

} // namespace synth::ui
