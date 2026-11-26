// CardLayoutOutline.cpp -- the outline, its grip and the lifted look of one control in the on-card
// layout editor.
#include "CardLayoutOutline.h"
#include "UI/Graph/CardBody/CardBodyGeometry.h"
#include "UI/Layout/FocusRing.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

namespace {

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
    setComponentID(idFor(paramId_));
    setWantsKeyboardFocus(true);
    setMouseClickGrabsKeyboardFocus(false);
    setCaption(caption);
}

juce::String CardLayoutOutline::idFor(const juce::String& paramId) { return "layoutOutline:" + paramId; }

// The component reaches kPad past the cell, and further right and down where the control's ink does: a lane Sync button
// sits at the end of its host's caption, past the cell, and its outline must not be clipped by the component.
void CardLayoutOutline::setCell(juce::Rectangle<int> cell) {
    auto bounds = cell.expanded(kPad);
    if (!widgetInk_.isEmpty()) {
        const auto reach = widgetInk_.expanded(kCaptionRoom);
        bounds.setRight(juce::jmax(bounds.getRight(), cell.getX() + (int)std::ceil(reach.getRight())));
        bounds.setBottom(juce::jmax(bounds.getBottom(), cell.getY() + (int)std::ceil(reach.getBottom())));
    }
    setBounds(bounds);
}

void CardLayoutOutline::setCaption(const juce::String& caption) {
    caption_ = caption;
    if (passThrough_) {
        setTitle(caption + ", layout: Space switches the look being edited, Return for options");
        setTooltip("Switches between the Time and Tempo looks you are editing. Right-click for options");
        return;
    }
    setTitle(caption + (panelOnly_ ? ", layout: Return for options" : ", layout: drag to move, Return for options"));
    setTooltip(panelOnly_
                   ? "Backspace removes. Right-click for options"
                   : "Drag to move (arrow keys nudge, Shift for 8px, Backspace removes). Right-click for options");
}

void CardLayoutOutline::setPanelOnly(bool panelOnly) {
    if (panelOnly == panelOnly_)
        return;
    panelOnly_ = panelOnly;
    setCaption(caption_);
    repaint();
}

void CardLayoutOutline::setPassThrough(bool passThrough) {
    if (passThrough == passThrough_)
        return;
    passThrough_ = passThrough;
    setCaption(caption_);
    repaint();
}

void CardLayoutOutline::setLift(float lift) {
    if (lift_ == lift)
        return;
    lift_ = lift;
    repaint();
}

static_assert((float)synth::cardbody::kCaptionSideInset - (float)CardLayoutOutline::kInset >=
                  CardLayoutOutline::kCaptionRoom,
              "a caption's side inset must hold the outline's caption room");

// The control's own pixels, not its cell: a cell is as wide as its column, so an outline drawn on it left a wide knob
// cell's sides far from the dial while its value box ran across the bottom line.
juce::Rectangle<float> CardLayoutOutline::getOutlineArea() const {
    juce::Rectangle<float> area;
    const auto add = [&area](juce::Rectangle<float> r) { area = area.isEmpty() ? r : area.getUnion(r); };
    if (!widgetInk_.isEmpty())
        add(widgetInk_.translated((float)kPad, (float)kPad).expanded(kCaptionRoom));
    if (!captionText_.isEmpty())
        add(captionText_.translated((float)kPad, (float)kPad).expanded(kCaptionRoom, kCaptionRoom + kCaptionRoomAbove));
    return area.isEmpty() ? getHitArea() : area;
}

juce::Rectangle<float> CardLayoutOutline::getHitArea() const {
    return getLocalBounds().reduced(kPad + kInset).toFloat();
}

juce::Rectangle<float> CardLayoutOutline::getGripArea() const {
    const auto area = getOutlineArea();
    return {area.getRight() - (float)kGripSize, area.getBottom() - (float)kGripSize, (float)kGripSize,
            (float)kGripSize};
}

// Only the drawn outline takes the mouse, so the padding of two neighbouring outlines never competes.
bool CardLayoutOutline::hitTest(int x, int y) { return !passThrough_ && getHitArea().contains((float)x, (float)y); }

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
    // The ring keeps the stroke's inner edge and grows outward, so a thick ring takes none of the content's clear room.
    const float grow = std::max(0.0f, focusRingThickness(theme) - kStroke);
    paintFocusRing(g, area.expanded(grow), *this, kCornerRadius + grow);

    if (panelOnly_ || passThrough_)
        return;
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
