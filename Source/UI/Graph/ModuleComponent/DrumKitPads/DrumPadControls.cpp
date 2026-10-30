// DrumPadControls.cpp -- the picked pad's panel: binding its knobs to the pad's parameters, the cross-fade between
// pads, and the dimmed Decay of a pad that plays a recording.
#include "UI/Graph/ModuleComponent/DrumKitPads/DrumPadControls.h"
#include "UI/DrumMachine/DrumLanes.h"
#include "UI/Graph/CardBody/CardBodyGeometry.h"
#include "UI/Layout/FadeVisibility.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

using PadParam = DrumKitModule::PadParam;
using synth::drums::PadKind;

namespace {

constexpr std::array<const char*, 4> kFieldWords = {"Level", "Tune", "Decay", "Start"};
constexpr float kDot = 8.0f;
constexpr int kDotSpace = 14;

bool showsStart(const synth::drums::Pad& pad) { return pad.kind == PadKind::File; }

} // namespace

bool DrumPadControls::PadKnob::keyPressed(const juce::KeyPress& key) {
    if (key.isKeyCode(juce::KeyPress::escapeKey) && !key.getModifiers().isAnyModifierKeyDown() && onEscape) {
        if (cancelModDotOnEscape(key))
            return true;
        onEscape();
        return true;
    }
    return CardKnobSlider::keyPressed(key);
}

int DrumPadControls::height() {
    return kHeaderHeight + kHeaderGap + synth::cardbody::kLabelHeight + synth::cardbody::kKnobHeight;
}

DrumPadControls::DrumPadControls(DrumKitModule& kit)
    : kit_(kit) {
    setComponentID("drumPadControls");
    name_.setFont(synth::theme::AppLookAndFeel::uiSemiBoldFont(13.0f));
    name_.setJustificationType(juce::Justification::centredLeft);
    name_.setMinimumHorizontalScale(1.0f);
    note_.setFont(synth::theme::AppLookAndFeel::uiFont(12.0f));
    note_.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(name_);
    addAndMakeVisible(note_);
    for (size_t f = 0; f < knobs_.size(); ++f) {
        auto& knob = knobs_[f];
        knob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        knob.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 20);
        knob.onEscape = [this] {
            if (onEscape)
                onEscape();
        };
        addAndMakeVisible(knob);
        captions_[f].setText(kFieldWords[f], juce::dontSendNotification);
        captions_[f].setJustificationType(juce::Justification::centred);
        addAndMakeVisible(captions_[f]);
    }
}

DrumPadControls::~DrumPadControls() { fade_.stop(); }

// ---- Binding ----

// The knobs re-attach to the pad's own parameters, so a drag on them is the parameter's gesture: the card's
// parameter listener makes it one undo step, like any card knob.
void DrumPadControls::bind(int index) {
    bound_ = index;
    for (size_t f = 0; f < knobs_.size(); ++f) {
        attachments_[f].reset();
        if (auto* param = kit_.padParameter(index, (PadParam)f))
            attachments_[f] = std::make_unique<juce::SliderParameterAttachment>(*param, knobs_[f]);
    }
    refresh();
}

void DrumPadControls::refresh() {
    if (bound_ < 0)
        return;
    const auto& pad = kit_.pad(bound_);
    setTitle(pad.name + " controls");
    name_.setText(pad.name, juce::dontSendNotification);
    note_.setText("plays note " + synth::drums::noteName(pad.note) + " (" + juce::String(pad.note) + ")",
                  juce::dontSendNotification);
    dot_ = drumLaneForPitch(pad.sound >= 0 ? pad.sound : pad.note).colour;
    for (size_t f = 0; f < knobs_.size(); ++f) {
        const juce::String name = juce::String(kFieldWords[f]) + ": " + pad.name;
        knobs_[f].setTitle(name);
        knobs_[f].setTooltip(name);
    }
    // A recording plays out at its own length, so the Decay has nothing to shape (it stays operable, like every
    // dimmed control).
    const bool moot = kit_.padPlaysRecording(bound_);
    auto& decay = knobs_[(size_t)PadParam::Decay];
    if ((bool)decay.getProperties()[synth::theme::AppLookAndFeel::kDimmedProperty] != moot) {
        decay.getProperties().set(synth::theme::AppLookAndFeel::kDimmedProperty, moot);
        decay.repaint();
    }
    const bool start = showsStart(pad);
    knobs_[(size_t)PadParam::Start].setVisible(start);
    captions_[(size_t)PadParam::Start].setVisible(start);
    resized();
    repaint();
}

// Out in 80 ms, the knobs re-bind while the panel is clear, in over 160 ms. Under Reduce Motion the new pad fades in
// over 80 ms; under Animations Off (or off screen) it lands at once.
void DrumPadControls::showPad(int index, bool animate) {
    if (fade_.isRunning() && pending_ >= 0) {
        pending_ = index; // still fading out: the newest pick is what fades in
        return;
    }
    if (index == bound_) {
        refresh();
        return;
    }
    if (!animate || bound_ < 0 || !FadeVisibility::canAnimateIn(this)) {
        fade_.stop();
        pending_ = -1;
        setAlpha(1.0f);
        bind(index);
        return;
    }
    const auto fadeIn = [this](double ms) {
        fade_.start(updater_, ms, easeOutCubic, [this](float t) { setAlpha(t); }, [this] { setAlpha(1.0f); });
    };
    if (prefersReducedMotion()) {
        bind(index);
        setAlpha(0.0f);
        fadeIn(kReducedMs);
        return;
    }
    pending_ = index;
    const float from = getAlpha();
    fade_.start(
        updater_, kFadeOutMs * from, [](float t) { return t; }, [this, from](float t) { setAlpha(from * (1.0f - t)); },
        [this, fadeIn] {
            const int next = pending_;
            pending_ = -1;
            bind(next);
            fadeIn(kFadeInMs);
        });
}

// ---- Layout and painting ----

void DrumPadControls::resized() {
    auto area = getLocalBounds();
    auto header = area.removeFromTop(kHeaderHeight);
    header.removeFromLeft(kDotSpace);
    name_.setBounds(header.removeFromLeft(header.getWidth() / 2));
    note_.setBounds(header);
    area.removeFromTop(kHeaderGap);
    const bool start = knobs_[(size_t)PadParam::Start].isVisible();
    const int columns = start ? 4 : 3;
    const int colW = area.getWidth() / columns;
    for (int f = 0; f < columns; ++f) {
        auto column = juce::Rectangle<int>(area.getX() + f * colW, area.getY(), colW, area.getHeight());
        captions_[(size_t)f].setBounds(column.removeFromTop(synth::cardbody::kLabelHeight));
        knobs_[(size_t)f].setBounds(column.removeFromTop(synth::cardbody::kKnobHeight));
    }
}

void DrumPadControls::paint(juce::Graphics& g) {
    if (bound_ < 0)
        return;
    g.setColour(dot_);
    g.fillEllipse(0.0f, ((float)kHeaderHeight - kDot) * 0.5f, kDot, kDot);
}

// The note reads in the theme's muted text, so it follows a theme change.
void DrumPadControls::lookAndFeelChanged() {
    note_.setColour(juce::Label::textColourId, synth::theme::themeOf(*this).colors.textMuted);
}

std::unique_ptr<juce::AccessibilityHandler> DrumPadControls::createAccessibilityHandler() {
    return std::make_unique<juce::AccessibilityHandler>(*this, juce::AccessibilityRole::group);
}

} // namespace synth::ui
