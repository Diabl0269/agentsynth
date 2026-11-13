// Concern: the lane header's value slot -- which value it shows, its colour cross-fade and its accessible text.
#include "UI/Timeline/AutomationLanes/AutomationLaneHeader/LaneValueReadout.h"

#include "UI/Layout/ReducedMotion.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

namespace {
constexpr float kFontSize = 10.0f;
constexpr double kFadeMs = 130.0;

struct ReadoutColours {
    juce::Colour muted;
    juce::Colour accent;
};

ReadoutColours coloursFor(const juce::Component& c) {
    const auto& t = synth::theme::themeOf(c).colors;
    return {t.textMuted, t.accent};
}
} // namespace

LaneValueReadout::LaneValueReadout() {
    setComponentID("automationLaneValueReadout");
    setAccessible(true);
    refreshAccessibility();
}

LaneValueReadout::~LaneValueReadout() { driver_.stop(updater_); }

void LaneValueReadout::setParameterName(const juce::String& name) {
    if (name == parameterName_)
        return;
    parameterName_ = name;
    refreshAccessibility();
}

void LaneValueReadout::setPlayheadText(const juce::String& text) {
    if (text == playhead_)
        return;
    playhead_ = text;
    refreshAccessibility();
    if (!selected_.has_value())
        repaint();
}

// A selection that clears and returns within one message (a point move re-announces it) leaves the
// fade where it was and costs nothing: nothing below runs when the value and mode are unchanged.
void LaneValueReadout::setSelectedText(const std::optional<juce::String>& text) {
    if (text == selected_)
        return;
    const bool modeChanged = text.has_value() != selected_.has_value();
    selected_ = text;
    refreshAccessibility();
    if (modeChanged)
        retargetColour();
    repaint();
}

void LaneValueReadout::refreshAccessibility() {
    const bool selected = selected_.has_value();
    const auto name = parameterName_.isEmpty() ? juce::String("Lane") : parameterName_;
    setTitle(name + (selected ? " selected point value" : " value at playhead"));
    setDescription(getDisplayedText());
    setTooltip(selected ? "Value of the selected point" : "Value at the playhead");
}

// Starts from the colour the slot is showing now, so a reversal mid-fade turns around instead of jumping.
void LaneValueReadout::retargetColour() {
    const float target = selected_.has_value() ? 1.0f : 0.0f;
    if (!isShowing() || prefersReducedMotion()) {
        driver_.stop(updater_);
        mix_ = target;
        return;
    }
    const float from = mix_;
    driver_.start(
        updater_, kFadeMs, easeOutCubic, [this, from, target](float t) { applyMix(from + (target - from) * t); },
        [this, target] { mix_ = target; });
}

void LaneValueReadout::applyMix(float mix) {
    mix_ = mix;
    repaint();
}

void LaneValueReadout::paint(juce::Graphics& g) {
    const auto colours = coloursFor(*this);
    g.setColour(colours.muted.interpolatedWith(colours.accent, mix_));
    g.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), kFontSize, juce::Font::plain)));
    g.drawText(getDisplayedText(), getLocalBounds(), juce::Justification::centredRight, true);
}

} // namespace synth::ui
