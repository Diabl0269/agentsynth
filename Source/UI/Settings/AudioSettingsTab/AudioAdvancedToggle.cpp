// Concern: the Device section's "Advanced" disclosure button.
#include "UI/Settings/AudioSettingsTab/AudioAdvancedToggle.h"

#include "UI/Layout/FocusRing.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace {
constexpr float kChevronSize = 8.0f;
constexpr int kTextLeft = 16;
} // namespace

AudioAdvancedToggle::AudioAdvancedToggle()
    : SpaceClaimingButton<juce::Button>("Advanced") {
    setWantsKeyboardFocus(true);
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
    addShortcut(juce::KeyPress(juce::KeyPress::returnKey));
    refreshText();
    onClick = [this] {
        if (onOpenRequested)
            onOpenRequested(!open_);
    };
}

void AudioAdvancedToggle::setOpen(bool open, bool animate) {
    open_ = open;
    refreshText();
    if (animate)
        turn_.setOpen(open);
    else
        turn_.snapTo(open);
}

// The accessible name carries the state, so a screen reader hears it change.
void AudioAdvancedToggle::refreshText() {
    setTitle(juce::String("Advanced audio settings, ") + (open_ ? "expanded" : "collapsed"));
    setTooltip(open_ ? "Hide the audio device type, sample rate and buffer size"
                     : "Show the audio device type, sample rate and buffer size");
}

void AudioAdvancedToggle::paintButton(juce::Graphics& g, bool hot, bool /*down*/) {
    const auto bounds = getLocalBounds().toFloat();
    const auto& theme = synth::theme::themeOf(*this);
    const bool lit = hot || hasKeyboardFocus(false);
    synth::theme::paintDisclosureChevron(
        g, juce::Rectangle<float>(kChevronSize, kChevronSize).withCentre({bounds.getX() + 8.0f, bounds.getCentreY()}),
        turn_.openness(), theme, lit);
    g.setColour(lit ? theme.colors.textPrimary : theme.colors.textMuted);
    g.setFont(juce::Font(juce::FontOptions(12.5f)));
    g.drawText("Advanced", getLocalBounds().withTrimmedLeft(kTextLeft + 4), juce::Justification::centredLeft);
    synth::ui::paintFocusRing(g, bounds.reduced(1.0f), *this);
}
