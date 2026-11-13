// CardFader.cpp -- a module card's fader: its own drag handling (Shift-fine, Cmd-click and double-click
// reset), routed through the card gestures first, and the geometry of its modulation bar and cable
// landing point. The look is AppLookAndFeel's fader painter, chosen by the slider's own size.
#include "CardFader.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeelFader.h"

namespace synth::ui {

namespace {

// Gap between the cap's edge and the modulation bar.
constexpr float kModBarGap = 1.0f;
// Extra px around the bar's strip that still counts as a press on it.
constexpr float kModBarHitPad = 3.0f;
// A narrow fader's value text, the theme's `type.label` size (the same as a knob's name).
constexpr float kValueFontPx = 10.5f;

} // namespace

CardFader::CardFader(Orientation orientation)
    : orientation_(orientation) {
    setWantsKeyboardFocus(true);
    if (orientation == Orientation::Vertical) {
        setSliderStyle(juce::Slider::LinearVertical);
        setTextBoxStyle(juce::Slider::TextBoxBelow, false, kVerticalWidth, 18);
    } else {
        setSliderStyle(juce::Slider::LinearHorizontal);
        setTextBoxStyle(juce::Slider::TextBoxRight, false, 48, 20);
    }
}

juce::String CardFader::compactValueText(const juce::String& parameterText) {
    return parameterText.removeCharacters(" ");
}

void CardFader::useCompactValueText(const juce::RangedAudioParameter& param) {
    textFromValueFunction = [&param](double value) {
        return compactValueText(param.getText(param.convertTo0to1((float)value), 0));
    };
    compactValueText_ = true;
    styleCompactValueBox();
    updateText();
}

// The slider's value box is a child label the slider rebuilds on a look change, so the style is reapplied.
void CardFader::lookAndFeelChanged() {
    juce::Slider::lookAndFeelChanged();
    if (compactValueText_)
        styleCompactValueBox();
}

void CardFader::styleCompactValueBox() {
    for (auto* child : getChildren())
        if (auto* box = dynamic_cast<juce::Label*>(child)) {
            box->setBorderSize({0, 0, 0, 0});
            box->setFont(juce::Font(juce::FontOptions(kValueFontPx)));
        }
}

juce::Rectangle<float> CardFader::travelBounds() const {
    return getLookAndFeel().getSliderLayout(const_cast<CardFader&>(*this)).sliderBounds.toFloat();
}

// The same mapping juce::Slider uses to place the cap (Slider::Pimpl::getLinearSliderPos): a vertical
// fader's zero sits at the bottom of the travel.
float CardFader::positionForProportion(double proportion) const {
    const auto t = travelBounds();
    const auto p = (float)juce::jlimit(0.0, 1.0, proportion);
    return isVerticalFader() ? t.getBottom() - p * t.getHeight() : t.getX() + p * t.getWidth();
}

// Beside the slot, just clear of the cap: right of a vertical fader's cap, under a horizontal one's.
// The cap size comes from the painter's own metrics for this fader's bounds, so the bar never sits
// under the cap whatever size the fader is laid out at.
juce::Rectangle<float> CardFader::modBarTrack() const {
    const auto t = travelBounds();
    const auto m = synth::theme::fader::metricsFor(isVerticalFader(), getWidth(), getHeight());
    if (isVerticalFader())
        return {t.getCentreX() + m.capW * 0.5f + kModBarGap, t.getY(), kModBarThickness, t.getHeight()};
    return {t.getX(), t.getCentreY() + m.capH * 0.5f + kModBarGap, t.getWidth(), kModBarThickness};
}

juce::Rectangle<float> CardFader::modBarBetween(double fromProportion, double toProportion) const {
    const auto track = modBarTrack();
    const float a = positionForProportion(fromProportion);
    const float b = positionForProportion(toProportion);
    if (isVerticalFader())
        return {track.getX(), std::min(a, b), track.getWidth(), std::abs(a - b)};
    return {std::min(a, b), track.getY(), std::abs(a - b), track.getHeight()};
}

bool CardFader::hitsModBar(juce::Point<float> local) const {
    return modBarTrack().expanded(kModBarHitPad).contains(local);
}

// Past the bar's zero end by the dot's radius and a 2 px gap, so the landing dot never covers the bar.
juce::Point<float> CardFader::landingPoint(float dotDiameter) const {
    const auto track = modBarTrack();
    const float push = dotDiameter * 0.5f + 2.0f;
    if (isVerticalFader())
        return {track.getCentreX(), track.getBottom() + push};
    return {track.getX() - push, track.getCentreY()};
}

void CardFader::resetToDefault() {
    if (!isDoubleClickReturnEnabled())
        return;
    juce::Slider::ScopedDragNotification gesture(*this);
    setValue(getDoubleClickReturnValue(), juce::sendNotificationSync);
}

void CardFader::reanchor(juce::Point<float> mouse) {
    anchorMouse_ = mouse;
    anchorProportion_ = valueToProportionOfLength(getValue());
}

// None of these call juce::Slider's own mouse handlers: those reach platform mouse-capture code that
// hangs headless tests (MixerFaderSlider.h), and the drag maths here is ours. The value drag is one
// change gesture, bracketed by a ScopedDragNotification held from the press to the release, so the
// parameter attachment sees exactly one begin/end and the edit is one undo step.
void CardFader::mouseDown(const juce::MouseEvent& e) {
    if (!isEnabled() || e.mods.isPopupMenu())
        return; // a right click is the card's control menu
    if (claimMouseDown(e))
        return;
    if (e.getNumberOfClicks() > 1)
        return; // a double-click's second press: mouseDoubleClick resets, no drag may nest inside it
    if (e.mods.isCommandDown()) {
        resetToDefault();
        return;
    }
    shiftWasDown_ = e.mods.isShiftDown();
    reanchor(e.position);
    drag_.emplace(*this);
}

// Proportion space, like the mixer fader: Shift re-anchors where the drag is now, so only the rate
// changes, never the value.
void CardFader::mouseDrag(const juce::MouseEvent& e) {
    if (forwardClaimed(e, 1) || !drag_.has_value())
        return;
    const bool shift = e.mods.isShiftDown();
    if (shift != shiftWasDown_) {
        reanchor(e.position);
        shiftWasDown_ = shift;
    }
    const auto t = travelBounds();
    const double length = std::max(1.0f, isVerticalFader() ? t.getHeight() : t.getWidth());
    const double pixels = isVerticalFader() ? anchorMouse_.y - e.position.y : e.position.x - anchorMouse_.x;
    const double proportion = juce::jlimit(0.0, 1.0, anchorProportion_ + pixels / length * (shift ? kFineRate : 1.0));
    setValue(proportionOfLengthToValue(proportion), juce::sendNotificationSync);
}

void CardFader::mouseUp(const juce::MouseEvent& e) {
    if (forwardClaimed(e, 2))
        return;
    drag_.reset();
    repaint();
}

void CardFader::mouseDoubleClick(const juce::MouseEvent& e) {
    if (isEnabled() && !e.mods.isPopupMenu() && !modDotClaimedLastPress())
        resetToDefault();
}

void CardFader::mouseEnter(const juce::MouseEvent& e) {
    notifyHover(true);
    juce::Slider::mouseEnter(e);
}

void CardFader::mouseExit(const juce::MouseEvent& e) {
    notifyHover(false);
    juce::Slider::mouseExit(e);
}

// The painter draws the focus ring around the cap from hasKeyboardFocus, so a focus change repaints.
void CardFader::focusGained(FocusChangeType cause) {
    juce::Slider::focusGained(cause);
    repaint();
}

void CardFader::focusLost(FocusChangeType cause) {
    juce::Slider::focusLost(cause);
    repaint();
}

void paintCardFaderModBar(juce::Graphics& g, juce::Rectangle<float> bar, juce::Colour colour, bool hovered) {
    if (bar.isEmpty())
        return;
    const auto drawn = hovered ? bar.expanded(0.5f) : bar;
    g.setColour(hovered ? colour.brighter(0.3f) : colour);
    g.fillRoundedRectangle(drawn, std::min(drawn.getWidth(), drawn.getHeight()) * 0.5f);
}

} // namespace synth::ui
