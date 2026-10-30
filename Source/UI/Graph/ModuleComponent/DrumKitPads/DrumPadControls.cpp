// DrumPadControls.cpp -- the picked pad's panel: binding its knobs to the pad's parameters, the Sound row, the
// cross-fade between pads, and the dimmed Decay of a pad that plays a recording or another instrument.
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
bool isInstrument(const synth::drums::Pad& pad) { return pad.kind == PadKind::Instrument; }

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

bool DrumPadControls::ShownSwitch::keyPressed(const juce::KeyPress& key) {
    if (key.isKeyCode(juce::KeyPress::escapeKey) && !key.getModifiers().isAnyModifierKeyDown() && onEscape) {
        onEscape();
        return true;
    }
    return juce::ToggleButton::keyPressed(key);
}

int DrumPadControls::height() {
    return kHeaderHeight + kHeaderGap + synth::cardbody::kRowHeight + kRowGap + synth::cardbody::kLabelHeight +
           synth::cardbody::kKnobHeight + kShownGap + kShownHeight;
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
    soundLabel_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(soundLabel_);
    sound_.setComponentID("drumPadSound");
    sound_.onClick = [this] {
        if (onSoundMenu)
            onSoundMenu();
    };
    addAndMakeVisible(sound_);
    open_.setComponentID("drumPadOpenInstrument");
    open_.onClick = [this] {
        if (onOpenInstrument)
            onOpenInstrument();
    };
    addChildComponent(open_);
    pitch_.setComponentID("drumPadPitch");
    pitch_.setTooltip("The note this pad sends to its instrument, before Tune");
    pitch_.setTypable(true);
    pitch_.onStep = [this](int delta) {
        if (onPitchStep)
            onPitchStep(delta);
    };
    pitch_.onTypedValue = [this](const juce::String& text) {
        if (onPitchTyped)
            onPitchTyped(text);
    };
    addChildComponent(pitch_);
    pitchCaption_.setJustificationType(juce::Justification::centred);
    addChildComponent(pitchCaption_);
    shown_.setButtonText("Shown");
    shown_.setComponentID("drumPadShown");
    shown_.onEscape = [this] {
        if (onEscape)
            onEscape();
    };
    shown_.onClick = [this] {
        if (target_ >= 0 && onShownChanged)
            onShownChanged(target_, shown_.getToggleState());
    };
    addAndMakeVisible(shown_);
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
    if (pending_ < 0)
        target_ = index;
    for (size_t f = 0; f < knobs_.size(); ++f) {
        attachments_[f].reset();
        if (auto* param = kit_.padParameter(index, (PadParam)f))
            attachments_[f] = std::make_unique<juce::SliderParameterAttachment>(*param, knobs_[f]);
    }
    refresh();
}

// The switch always speaks for the pad picked now, also while the rest of the panel is still fading to it.
void DrumPadControls::refreshShownSwitch() {
    if (target_ < 0)
        return;
    const auto& pad = kit_.pad(target_);
    shown_.setToggleState(pad.shown, juce::dontSendNotification);
    shown_.setTitle("Shown: " + pad.name);
    shown_.setTooltip("Shown: " + pad.name +
                      " is on the card, in the clip editor and in the drum machine. Off hides it; it still plays "
                      "(H on the pads)");
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
    refreshShownSwitch();
    for (size_t f = 0; f < knobs_.size(); ++f) {
        const juce::String name = juce::String(kFieldWords[f]) + ": " + pad.name;
        knobs_[f].setTitle(name);
        knobs_[f].setTooltip(name);
    }
    refreshSoundRow(pad);
    // A recording plays out at its own length and an instrument shapes its own notes, so the Decay has nothing to
    // shape (it stays operable, like every dimmed control).
    const bool moot = kit_.padPlaysRecording(bound_) || isInstrument(pad);
    auto& decay = knobs_[(size_t)PadParam::Decay];
    if ((bool)decay.getProperties()[synth::theme::AppLookAndFeel::kDimmedProperty] != moot) {
        decay.getProperties().set(synth::theme::AppLookAndFeel::kDimmedProperty, moot);
        decay.repaint();
    }
    const bool start = showsStart(pad);
    knobs_[(size_t)PadParam::Start].setVisible(start);
    captions_[(size_t)PadParam::Start].setVisible(start);
    pitch_.setVisible(isInstrument(pad));
    pitchCaption_.setVisible(isInstrument(pad));
    resized();
    repaint();
}

// The Sound row names what the pad plays; an instrument pad adds Open and its pitch.
void DrumPadControls::refreshSoundRow(const synth::drums::Pad& pad) {
    auto [name, detail] = describeSound ? describeSound(bound_) : std::make_pair(pad.name, juce::String());
    sound_.setInstrument(name, detail);
    sound_.setTitle("Sound for " + pad.name + ", " + name + (detail.isNotEmpty() ? ", " + detail : juce::String()));
    sound_.setTooltip("Choose what " + pad.name + " plays: a file, a built-in drum or an instrument (Enter)");
    open_.setVisible(isInstrument(pad));
    open_.setTexts("Open " + name + " on the canvas", "Show the instrument " + pad.name + " plays on the canvas");
    pitch_.setValueText(synth::drums::noteName(pad.instrumentPitch) + " (" + juce::String(pad.instrumentPitch) + ")");
}

// Out in 80 ms, the knobs re-bind while the panel is clear, in over 160 ms. Under Reduce Motion the new pad fades in
// over 80 ms; under Animations Off (or off screen) it lands at once.
void DrumPadControls::showPad(int index, bool animate) {
    target_ = index;
    refreshShownSwitch();
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
    auto row = area.removeFromTop(synth::cardbody::kRowHeight);
    soundLabel_.setBounds(row.removeFromLeft(kSoundLabelWidth));
    if (open_.isVisible())
        open_.setBounds(row.removeFromRight(TrackShowModuleButton::kSize)
                            .withSizeKeepingCentre(TrackShowModuleButton::kSize, TrackShowModuleButton::kSize));
    row.removeFromRight(open_.isVisible() ? kRowGap : 0);
    sound_.setBounds(row);
    area.removeFromTop(kRowGap);
    shown_.setBounds(area.removeFromBottom(kShownHeight).withWidth(96));
    area.removeFromBottom(kShownGap);
    layoutColumns(area);
}

// Level, Tune, Decay, then Start (a file pad) or Plays pitch (an instrument pad) in a fourth column. Plays pitch
// takes two fifths of the row so its stepper has room for "C3 (60)" between its buttons.
void DrumPadControls::layoutColumns(juce::Rectangle<int> area) {
    const bool start = knobs_[(size_t)PadParam::Start].isVisible();
    const bool pitch = pitch_.isVisible();
    const int columns = start || pitch ? 4 : 3;
    const int colW = area.getWidth() / (pitch ? 5 : columns);
    for (int f = 0; f < columns; ++f) {
        auto column = juce::Rectangle<int>(area.getX() + f * colW, area.getY(), colW, area.getHeight());
        if (f == 3 && pitch) {
            column.setRight(area.getRight());
            pitchCaption_.setBounds(column.removeFromTop(synth::cardbody::kLabelHeight));
            pitch_.setBounds(column.removeFromTop(synth::cardbody::kRowHeight).reduced(2, 0));
            continue;
        }
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

// The note reads in the theme's muted text, a step toward the primary text so it stays above 4.5:1 on a light card,
// and follows a theme change.
void DrumPadControls::lookAndFeelChanged() {
    const auto& colors = synth::theme::themeOf(*this).colors;
    note_.setColour(juce::Label::textColourId, colors.textMuted.interpolatedWith(colors.textPrimary, 0.2f));
}

std::unique_ptr<juce::AccessibilityHandler> DrumPadControls::createAccessibilityHandler() {
    return std::make_unique<juce::AccessibilityHandler>(*this, juce::AccessibilityRole::group);
}

} // namespace synth::ui
