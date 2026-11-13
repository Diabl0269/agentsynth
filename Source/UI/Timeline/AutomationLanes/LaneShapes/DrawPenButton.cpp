#include "DrawPenButton.h"

#include "UI/Layout/ReducedMotion.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Timeline/AutomationLanes/LaneShapes/DrawShapeIcons.h"
#include "UI/Timeline/TrackColour.h"

namespace synth::ui {

// Concern: the pen button's current-shape icon, its crossfade, its chevron half and the gestures that
// open the flyout (chevron click, right-click, press and hold).

DrawPenButton::DrawPenButton(const juce::String& name)
    : juce::DrawableButton(name, juce::DrawableButton::ImageOnButtonBackground)
    , vblank_(this) {
    chevronHover_.onFrame = [this] { repaint(); };
    chevronHover_.repaintArea = [this] { return getLocalBounds().removeFromRight(kChevronWidth); };
}

DrawPenButton::~DrawPenButton() {
    stopTimer();
    fade_.stop(vblank_);
}

// A change of shape crossfades the old icon into the new in 160 ms (80 ms under Reduce Motion, none under Animations:
// Off). A change mid-fade restarts from the icon showing now, so it never jumps. Off screen no frame would come, so
// it lands at once.
void DrawPenButton::setShape(DrawShape shape) {
    if (shape == shape_)
        return;
    previous_ = crossfade_ < 0.5f ? previous_ : shape_;
    shape_ = shape;
    if (!isShowing() || animationsOff()) {
        fade_.stop(vblank_);
        crossfade_ = 1.0f;
        repaint();
        return;
    }
    crossfade_ = 0.0f;
    fade_.start(
        vblank_, motionMs(kCrossfadeMs, kCrossfadeReducedMs), easeOutCubic,
        [this](float t) {
            crossfade_ = t;
            repaint();
        },
        [this] {
            crossfade_ = 1.0f;
            repaint();
        });
}

bool DrawPenButton::isInChevron(juce::Point<int> p) const noexcept {
    return juce::Rectangle<int>(getWidth() - kChevronWidth, 0, kChevronWidth, getHeight()).contains(p);
}

juce::String DrawPenButton::getTooltipAt(juce::Point<int> p) {
    if (chevronTip_.isNotEmpty() && isInChevron(p))
        return chevronTip_;
    return juce::DrawableButton::getTooltip();
}

void DrawPenButton::mouseMove(const juce::MouseEvent& e) {
    chevronHover_.setShown(isInChevron(e.getPosition()));
    juce::DrawableButton::mouseMove(e);
}

// The background (the lit fill, the hover shift, the focus ring) is what juce::DrawableButton paints behind the Select,
// Range and other tool buttons: drawButtonBackground with the look-and-feel's button / button-on colours, read from the
// theme at paint time. The glyph takes
// the theme's primary ink like theirs; the chevron half is the app's combo chevron in that same ink, behind a divider.
// The lit fill is the theme accent, so the theme's primary text (white in a dark theme) can almost vanish on it. The
// lit glyph and chevron take whichever of the theme's inks reads best on that fill, which keeps 3:1 in every theme.
namespace {
juce::Colour litInk(const synth::theme::Colors& colours, juce::Colour fill) {
    const juce::Colour candidates[] = {colours.iconInk, colours.iconPaper, colours.textPrimary, colours.bg0};
    auto best = candidates[0];
    for (const auto& candidate : candidates)
        if (colourContrast(candidate, fill) > colourContrast(best, fill))
            best = candidate;
    return best;
}
} // namespace

void DrawPenButton::paintButton(juce::Graphics& g, bool over, bool down) {
    auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel());
    auto ink = lf != nullptr ? lf->getTheme().colors.textPrimary : juce::Colours::white;
    if (lf != nullptr && getToggleState())
        ink = litInk(lf->getTheme().colors, findColour(juce::TextButton::buttonOnColourId));
    getLookAndFeel().drawButtonBackground(
        g, *this, findColour(getToggleState() ? juce::TextButton::buttonOnColourId : juce::TextButton::buttonColourId),
        over, down);

    // The chevron half: a divider, a wash while the pointer is over the half, then the chevron.
    const auto chevronHalf = getLocalBounds().removeFromRight(kChevronWidth).toFloat();
    if (chevronHover_.value() > 0.0f) {
        g.setColour(ink.withAlpha(0.14f * chevronHover_.value()));
        g.fillRoundedRectangle(chevronHalf.reduced(1.0f), 3.0f);
    }
    g.setColour(ink.withAlpha(0.35f));
    g.fillRect(chevronHalf.getX(), chevronHalf.getY() + 5.0f, 1.0f, chevronHalf.getHeight() - 10.0f);

    const auto iconArea = getIconHalf().toFloat().reduced(6.0f);
    if (crossfade_ < 1.0f)
        paintDrawShapeIcon(g, previous_, iconArea, ink, 1.0f - crossfade_);
    paintDrawShapeIcon(g, shape_, iconArea, ink, crossfade_ < 1.0f ? crossfade_ : 1.0f);

    // This half opens more shapes.
    synth::theme::paintComboChevron(g, chevronHalf.getCentre(), ink);
}

// A chevron press or a right-click opens the flyout at once and is not a click; any other press is a normal button
// press that also arms the hold timer. A hold that fires opens the flyout and eats the release, so it does not also
// pick the tool.
void DrawPenButton::mouseDown(const juce::MouseEvent& e) {
    chevronPress_ = false;
    holdFired_ = false;
    if (e.mods.isPopupMenu()) { // a right-click opens the same flyout and, like the chevron, picks nothing
        chevronPress_ = true;
        if (onOpenFlyout)
            onOpenFlyout();
        return;
    }
    if (e.mods.isLeftButtonDown() && isInChevron(e.getPosition())) {
        chevronPress_ = true;
        if (onOpenFlyout)
            onOpenFlyout();
        return;
    }
    if (e.mods.isLeftButtonDown())
        startTimer(kHoldMs);
    juce::DrawableButton::mouseDown(e);
}

void DrawPenButton::mouseDrag(const juce::MouseEvent& e) {
    if (chevronPress_ || holdFired_)
        return;
    if (e.getDistanceFromDragStart() > 4)
        stopTimer();
    juce::DrawableButton::mouseDrag(e);
}

void DrawPenButton::mouseUp(const juce::MouseEvent& e) {
    stopTimer();
    if (chevronPress_) {
        chevronPress_ = false;
        return;
    }
    if (holdFired_) {
        setState(buttonNormal);
        // holdFired_ stays set across the callback: the flyout ignores outside input while the press is down.
        if (onHoldReleased)
            onHoldReleased(e.getScreenPosition());
        holdFired_ = false;
        return;
    }
    juce::DrawableButton::mouseUp(e);
}

void DrawPenButton::mouseExit(const juce::MouseEvent& e) {
    stopTimer();
    chevronHover_.setShown(false);
    juce::DrawableButton::mouseExit(e);
}

void DrawPenButton::timerCallback() {
    stopTimer();
    holdFired_ = true;
    setState(buttonNormal);
    if (onOpenFlyout)
        onOpenFlyout();
}

} // namespace synth::ui
