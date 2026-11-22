#include "DrawPenButton.h"

#include "UI/Chrome/ToolbarButton/SideMenuMark.h"
#include "UI/Layout/ButtonStateEase.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Timeline/AutomationLanes/LaneShapes/DrawShapeIcons.h"
#include "UI/Timeline/TrackColour.h"

namespace synth::ui {

// Concern: the pen button's current-shape icon, its crossfade, its menu notch and the gestures that
// open the flyout (corner press, right-click, press and hold).

DrawPenButton::DrawPenButton(const juce::String& name)
    : juce::DrawableButton(name, juce::DrawableButton::ImageOnButtonBackground)
    , vblank_(this) {
    gesture_ = std::make_unique<SideMenuGesture>();
    gesture_->chipBounds = [this] { return getLocalBounds(); };
    gesture_->onOpen = [this] {
        if (onOpenFlyout)
            onOpenFlyout();
    };
    gesture_->onPressSwallowed = [this] { setState(buttonNormal); };
    gesture_->onHoldReleased = [this](juce::Point<int> screenPoint) {
        if (onHoldReleased)
            onHoldReleased(screenPoint);
    };
}

DrawPenButton::~DrawPenButton() { fade_.stop(vblank_); }

bool DrawPenButton::isHoldGestureDown() const noexcept { return gesture_->isHoldGestureDown(); }

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

// The background (the lit fill, the hover shift, the focus ring) is what juce::DrawableButton paints behind the Select,
// Range and other tool buttons: drawButtonBackground with the look-and-feel's button / button-on colours, read from the
// theme at paint time. The glyph takes
// the theme's primary ink like theirs and sits centred in the whole button; the menu notch is the shared one.
// The lit fill is the theme accent, so the theme's primary text (white in a dark theme) can almost vanish on it. The
// lit glyph takes whichever of the theme's inks reads best on that fill, which keeps 3:1 in every theme; the notch
// takes that same ink, eased in with the pen's on amount and softened toward the fill in a dark theme, over the fill's
// hover and press shades.
namespace {
juce::Colour litInk(const synth::theme::Colors& colours, juce::Colour fill) {
    const juce::Colour candidates[] = {colours.iconInk, colours.iconPaper, colours.textPrimary, colours.bg0};
    auto best = candidates[0];
    for (const auto& candidate : candidates)
        if (colourContrast(candidate, fill) > colourContrast(best, fill))
            best = candidate;
    return best;
}

// The fill drawButtonBackground paints for `button` at its current eased amounts: the off / on colours, brighter on
// hover and darker pressed.
juce::Colour paintedFill(const juce::Button& button) {
    const auto off = button.findColour(juce::TextButton::buttonColourId);
    const auto on = button.findColour(juce::TextButton::buttonOnColourId);
    return ButtonStateEase::blend(ButtonStateEase::current(button), [&](bool isOn, bool hot, bool down) {
        const auto base = isOn ? on : off;
        return down ? base.darker(0.2f) : (hot ? base.brighter(0.12f) : base);
    });
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

    const auto iconArea = getLocalBounds().toFloat().reduced(6.0f);
    if (crossfade_ < 1.0f)
        paintDrawShapeIcon(g, previous_, iconArea, ink, 1.0f - crossfade_);
    paintDrawShapeIcon(g, shape_, iconArea, ink, crossfade_ < 1.0f ? crossfade_ : 1.0f);

    if (lf == nullptr)
        return;
    // The notch's ink reads on the fill drawButtonBackground just painted: the same ButtonStateEase blend at the same
    // eased amounts, over either bar colour the button can sit on.
    const auto& colours = lf->getTheme().colors;
    const auto painted = paintedFill(*this);
    const auto notchInk = sideMenuNotchInk(
        lf->getTheme(), {colours.bg0.overlaidWith(painted), colours.bg1.overlaidWith(painted)},
        litInk(colours, findColour(juce::TextButton::buttonOnColourId)), ButtonStateEase::current(*this).on);
    paintSideMenuNotch(g, getLocalBounds().toFloat().reduced(0.5f), lf->getTheme().metrics.pillRadius, {1.0f, 1.0f},
                       notchInk, isEnabled());
}

// The gesture decides whether a press opens the flyout; any other press is a normal button press. A hold that fires
// opens the flyout and eats the release, so it does not also pick the tool.
void DrawPenButton::mouseDown(const juce::MouseEvent& e) {
    if (!gesture_->mouseDown(e))
        juce::DrawableButton::mouseDown(e);
}

void DrawPenButton::mouseDrag(const juce::MouseEvent& e) {
    if (!gesture_->mouseDrag(e))
        juce::DrawableButton::mouseDrag(e);
}

void DrawPenButton::mouseUp(const juce::MouseEvent& e) {
    if (!gesture_->mouseUp(e))
        juce::DrawableButton::mouseUp(e);
}

void DrawPenButton::mouseExit(const juce::MouseEvent& e) {
    gesture_->mouseExit();
    juce::DrawableButton::mouseExit(e);
}

} // namespace synth::ui
