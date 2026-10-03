// CardLayoutOutline.cpp -- the outline, its grip and the lifted look of one control in the on-card
// layout editor.
#include "CardLayoutOutline.h"
#include "UI/Layout/FocusRing.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

namespace {

constexpr float kCornerRadius = 7.0f;
constexpr float kHoverWash = 0.07f;
constexpr double kHoverMs = 100.0;
constexpr double kHoverReducedMs = 80.0;

// Four dots in a square: the grip's mark.
void paintGripDots(juce::Graphics& g, juce::Rectangle<float> grip) {
    constexpr float dot = 1.6f;
    for (int row = 0; row < 2; ++row)
        for (int col = 0; col < 2; ++col)
            g.fillEllipse(grip.getX() + 2.4f + (float)col * 3.6f, grip.getY() + 2.4f + (float)row * 3.6f, dot, dot);
}

} // namespace

CardLayoutOutline::CardLayoutOutline(juce::String paramId, juce::String caption)
    : juce::Button(paramId)
    , paramId_(std::move(paramId)) {
    setWantsKeyboardFocus(true);
    setMouseClickGrabsKeyboardFocus(false);
    setCaption(caption);
}

void CardLayoutOutline::setCell(juce::Rectangle<int> cell) { setBounds(cell.expanded(kPad)); }

void CardLayoutOutline::setCaption(const juce::String& caption) {
    setTitle(caption + ", layout: drag to move, Return for options");
    setTooltip("Drag to move (arrow keys nudge, Shift for 8px). Right-click for options");
}

void CardLayoutOutline::setLift(float lift) {
    if (lift_ == lift)
        return;
    lift_ = lift;
    repaint();
}

juce::Rectangle<float> CardLayoutOutline::getOutlineArea() const {
    return getLocalBounds().reduced(kPad + kInset).toFloat();
}

juce::Rectangle<float> CardLayoutOutline::getGripArea() const {
    const auto area = getOutlineArea();
    return {area.getRight() - (float)kGripSize, area.getBottom() - (float)kGripSize, (float)kGripSize,
            (float)kGripSize};
}

// Only the drawn outline takes the mouse, so the padding of two neighbouring outlines never competes.
bool CardLayoutOutline::hitTest(int x, int y) { return getOutlineArea().contains((float)x, (float)y); }

void CardLayoutOutline::paintButton(juce::Graphics& g, bool, bool) {
    const auto& theme = synth::theme::themeOf(*this);
    const auto accent = theme.colors.accent;
    const auto area = getOutlineArea();
    const bool focused = hasKeyboardFocus(false);
    const float solid = focused ? 1.0f : hover_;

    if (lift_ > 0.0f)
        juce::DropShadow(juce::Colours::black.withAlpha(0.35f * lift_), 8, {0, 2})
            .drawForRectangle(g, area.getSmallestIntegerContainer());
    if (solid > 0.0f) {
        g.setColour(accent.withAlpha(kHoverWash * solid));
        g.fillRoundedRectangle(area, kCornerRadius);
    }
    if (solid < 1.0f) {
        juce::Path outline;
        outline.addRoundedRectangle(area.reduced(0.5f), kCornerRadius);
        juce::Path dashed;
        const float dashes[] = {4.0f, 3.0f};
        juce::PathStrokeType(1.0f).createDashedStroke(dashed, outline, dashes, 2);
        g.setColour(accent.withAlpha(0.75f * (1.0f - solid)));
        g.fillPath(dashed);
    }
    if (solid > 0.0f && !focused) {
        g.setColour(accent.withAlpha(solid));
        g.drawRoundedRectangle(area.reduced(0.75f), kCornerRadius, 1.5f);
    }
    paintFocusRing(g, area, *this, kCornerRadius);

    const auto grip = getGripArea();
    g.setColour(theme.colors.surfaceHi);
    g.fillRect(grip);
    g.setColour(accent);
    g.drawRect(grip, 1.0f);
    paintGripDots(g, grip);
}

void CardLayoutOutline::animateHover(bool over) {
    const float target = over ? 1.0f : 0.0f;
    if (!isShowing()) {
        hover_ = target;
        repaint();
        return;
    }
    const float from = hover_;
    hoverAnim_.start(updater_, prefersReducedMotion() ? kHoverReducedMs : kHoverMs, easeOutCubic,
                     [this, from, target](float t) {
                         hover_ = from + (target - from) * t;
                         repaint();
                     });
}

void CardLayoutOutline::mouseEnter(const juce::MouseEvent& e) {
    juce::Button::mouseEnter(e);
    animateHover(true);
}

void CardLayoutOutline::mouseExit(const juce::MouseEvent& e) {
    juce::Button::mouseExit(e);
    animateHover(false);
}

void CardLayoutOutline::mouseDown(const juce::MouseEvent& e) {
    grabKeyboardFocus();
    if (e.mods.isPopupMenu()) {
        if (onClick)
            onClick();
        return;
    }
    if (e.mods.isLeftButtonDown() && onPress)
        onPress(e);
}

void CardLayoutOutline::mouseDrag(const juce::MouseEvent& e) {
    if (onDrag)
        onDrag(e);
}

void CardLayoutOutline::mouseUp(const juce::MouseEvent& e) {
    if (onRelease)
        onRelease(e);
}

void CardLayoutOutline::mouseDoubleClick(const juce::MouseEvent& e) {
    if (!e.mods.isPopupMenu() && onClick)
        onClick();
}

// Return asks for the control's options (the button's own click would be posted, not run, so the hook
// is called here).
bool CardLayoutOutline::keyPressed(const juce::KeyPress& key) {
    if (onKey && onKey(key))
        return true;
    if (key != juce::KeyPress::returnKey)
        return false;
    if (onClick)
        onClick();
    return true;
}

} // namespace synth::ui
