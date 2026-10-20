#include "DrawPenButton.h"

#include "UI/Layout/ReducedMotion.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Timeline/AutomationLanes/LaneShapes/DrawShapeIcons.h"
#include "UI/Timeline/TrackColour.h"

namespace synth::ui {

// Concern: the pen button's current-shape icon, its crossfade, its corner triangle and the gestures that
// open the flyout (corner click, right-click, press and hold).

DrawPenButton::DrawPenButton(const juce::String& name)
    : juce::DrawableButton(name, juce::DrawableButton::ImageOnButtonBackground)
    , vblank_(this) {}

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

bool DrawPenButton::isInCorner(juce::Point<int> p) const noexcept {
    return juce::Rectangle<int>(getWidth() - kCornerSize, getHeight() - kCornerSize, kCornerSize, kCornerSize)
        .contains(p);
}

// The lit fill is the light `toolActive` highlight, so the theme's primary text (white in a dark theme) would almost
// vanish on it. The glyph takes whichever of the theme's inks reads best on that fill, which keeps 3:1 in every theme.
namespace {
juce::Colour drawPenLitInk(const synth::theme::Colors& colours) {
    const juce::Colour candidates[] = {colours.iconInk, colours.iconPaper, colours.textPrimary, colours.bg0};
    auto best = candidates[0];
    for (const auto& candidate : candidates)
        if (colourContrast(candidate, colours.toolActive) > colourContrast(best, colours.toolActive))
            best = candidate;
    return best;
}
} // namespace

void DrawPenButton::paintButton(juce::Graphics& g, bool over, bool down) {
    auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel());
    const bool lit = getToggleState();
    auto ink = lf != nullptr ? lf->getTheme().colors.textPrimary : juce::Colours::white;
    auto secondary = lf != nullptr ? lf->getTheme().colors.textMuted : juce::Colours::grey;
    if (lit && lf != nullptr)
        ink = secondary = drawPenLitInk(lf->getTheme().colors);
    getLookAndFeel().drawButtonBackground(g, *this, findColour(lit ? backgroundOnColourId : backgroundColourId), over,
                                          down);

    const auto iconArea = getLocalBounds().toFloat().reduced(6.0f);
    if (crossfade_ < 1.0f)
        paintDrawShapeIcon(g, previous_, iconArea, ink, 1.0f - crossfade_);
    paintDrawShapeIcon(g, shape_, iconArea, ink, crossfade_ < 1.0f ? crossfade_ : 1.0f);

    // The corner triangle: this button opens more.
    const float right = (float)getWidth() - 2.5f, bottom = (float)getHeight() - 2.5f;
    juce::Path tri;
    tri.addTriangle(right, bottom, right, bottom - 4.5f, right - 4.5f, bottom);
    g.setColour(secondary);
    g.fillPath(tri);
}

// A corner press or a right-click opens the flyout at once and is not a click; any other press is a normal button press
// that also arms the hold timer. A hold that fires opens the flyout and eats the release, so it does not also pick the
// tool.
void DrawPenButton::mouseDown(const juce::MouseEvent& e) {
    cornerPress_ = false;
    holdFired_ = false;
    if (e.mods.isPopupMenu()) { // a right-click opens the same flyout and, like the corner, picks nothing
        cornerPress_ = true;
        if (onOpenFlyout)
            onOpenFlyout();
        return;
    }
    if (e.mods.isLeftButtonDown() && isInCorner(e.getPosition())) {
        cornerPress_ = true;
        if (onOpenFlyout)
            onOpenFlyout();
        return;
    }
    if (e.mods.isLeftButtonDown())
        startTimer(kHoldMs);
    juce::DrawableButton::mouseDown(e);
}

void DrawPenButton::mouseDrag(const juce::MouseEvent& e) {
    if (cornerPress_ || holdFired_)
        return;
    if (e.getDistanceFromDragStart() > 4)
        stopTimer();
    juce::DrawableButton::mouseDrag(e);
}

void DrawPenButton::mouseUp(const juce::MouseEvent& e) {
    stopTimer();
    if (cornerPress_) {
        cornerPress_ = false;
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
