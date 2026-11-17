// DrumPadControls.cpp -- the picked pad's panel: binding its knobs to the pad's parameters, the Sound row, and the
// dimmed Decay of a pad that plays a recording or another instrument. The glide of the knob values and columns between
// pads is DrumPadControlsGlide.cpp.
#include "UI/Graph/ModuleComponent/DrumKitPads/DrumPadControls.h"
#include "UI/DrumMachine/DrumLanes.h"
#include "UI/Graph/CardBody/CardBodyGeometry.h"
#include "UI/Graph/ModuleComponent/TuneScaleSnap.h"
#include "UI/Layout/FollowKeyField.h"
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

// The parameter's own change (a pick, an undo, automation) tells the panel what was shown before it, so the panel can
// glide; a change the user makes tells it to stop gliding that knob. Which is which: a change that comes from the
// parameter finds the parameter already at the new value, one from the knob finds it still at the old one.
void DrumPadControls::PadKnob::valueChanged() {
    const double before = shown_;
    shown_ = valueToProportionOfLength(getValue());
    if (param_ == nullptr)
        return;
    const bool fromModel = std::abs(param_->convertTo0to1((float)getValue()) - param_->getValue()) < 1.0e-4f;
    if (fromModel && onModelValue)
        onModelValue(before, shown_);
    else if (!fromModel && onUserValue)
        onUserValue();
}

void DrumPadControls::PadKnob::showProportion(double proportion) {
    shown_ = juce::jlimit(0.0, 1.0, proportion);
    setValue(proportionOfLengthToValue(shown_), juce::dontSendNotification);
}

int DrumPadControls::height() {
    return kHeaderHeight + kHeaderGap + synth::cardbody::kRowHeight + kRowGap + synth::cardbody::kLabelHeight +
           synth::cardbody::kKnobHeight + kShownGap + kShownHeight + kTuneKeyGap + kShownHeight;
}

DrumPadControls::DrumPadControls(DrumKitModule& kit)
    : kit_(kit) {
    setComponentID("drumPadControls");
    name_.setFont(synth::theme::AppLookAndFeel::uiSemiBoldFont(13.0f));
    name_.setJustificationType(juce::Justification::centredLeft);
    name_.setMinimumHorizontalScale(1.0f);
    noteCaption_.setFont(synth::theme::AppLookAndFeel::uiFont(12.0f));
    noteCaption_.setJustificationType(juce::Justification::centredRight);
    noteCaption_.setMinimumHorizontalScale(1.0f);
    addAndMakeVisible(name_);
    addAndMakeVisible(noteCaption_);
    note_.setComponentID("drumPadNote");
    note_.setTypable(true);
    note_.setWidestValueText("C#-2 (127)");
    note_.onStep = [this](int delta) {
        if (onNoteStep)
            onNoteStep(delta);
    };
    note_.onTypedValue = [this](const juce::String& text) {
        if (onNoteTyped)
            onNoteTyped(text);
    };
    addAndMakeVisible(note_);
    tuneKeyLabel_.setFont(synth::theme::AppLookAndFeel::uiFont(12.0f));
    tuneKeyLabel_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(tuneKeyLabel_);
    tuneKey_ = std::make_unique<FollowKeyField>();
    tuneKey_->setComponentID("drumPadTuneScale");
    tuneKey_->setWhat("Tune scale", "Tune snaps to this key's notes, 0 being its root, so a tuned drum stays in key.");
    tuneKey_->setNoKeyText("No scale");
    tuneKey_->onPickOwn = [this](std::optional<synth::MusicalScale> key) {
        if (onTuneKeyPick)
            onTuneKeyPick(key);
    };
    tuneKey_->onFollow = [this] {
        if (onTuneKeyFollow)
            onTuneKeyFollow();
    };
    addAndMakeVisible(*tuneKey_);
    tunescale::wire(knobs_[(size_t)PadParam::Tune], [this] { return activeTuneKey(); });
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
    shown_.setButtonText("Show on card");
    shown_.setComponentID("drumPadShown");
    shown_.onEscape = [this] {
        if (onEscape)
            onEscape();
    };
    shown_.onClick = [this] {
        if (bound_ >= 0 && onShownChanged)
            onShownChanged(bound_, shown_.getToggleState());
    };
    addAndMakeVisible(shown_);
    valueGlide_.onDirty = [this](juce::Rectangle<int>) {
        applyDrawnValues();
        repaintCardAroundKnobs();
    };
    for (size_t f = 0; f < knobs_.size(); ++f) {
        auto& knob = knobs_[f];
        knob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        knob.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 20);
        knob.onEscape = [this] {
            if (onEscape)
                onEscape();
        };
        knob.onModelValue = [this, f](double before, double after) { knobFollowedModel(f, before, after); };
        knob.onUserValue = [this, f] {
            if (bindingKnobs_)
                return;
            released_[f] = true;
            valueGlide_.erase((KeyedPointTween::Key)f);
        };
        knob.onDragStart = knob.onUserValue;
        addAndMakeVisible(knob);
        captions_[f].setText(kFieldWords[f], juce::dontSendNotification);
        captions_[f].setJustificationType(juce::Justification::centred);
        addAndMakeVisible(captions_[f]);
    }
}

DrumPadControls::~DrumPadControls() {
    columnDriver_.stop(updater_);
    ringDriver_.stop(updater_);
}

// ---- Binding ----

// An attachment removes its listener from the parameter it was made on; once the kit is freed that removal would lock
// a freed parameter, so the attachments are given up without running it (the knobs die with the card).
void DrumPadControls::releaseBindings() {
    for (auto& attachment : attachments_)
        (void)attachment.release();
}

// The knobs re-attach to the pad's own parameters, so a drag on them is the parameter's gesture: the card's
// parameter listener makes it one undo step, like any card knob.
void DrumPadControls::bind(int index) {
    // Once per pad change, before the knobs leave the old pad, so its cable ends glide from where they are drawn.
    if (index != bound_ && bound_ >= 0 && onBeforeRebind)
        onBeforeRebind();
    const int old = bound_;
    bound_ = index;
    bindingKnobs_ = true;
    for (size_t f = 0; f < knobs_.size(); ++f) {
        attachments_[f].reset();
        auto* param = kit_.padParameter(index, (PadParam)f);
        knobs_[f].setModelParam(param);
        if (param != nullptr)
            attachments_[f] = std::make_unique<juce::SliderParameterAttachment>(*param, knobs_[f]);
    }
    bindingKnobs_ = false;
    if (glideBinding_)
        startRingFade(old);
    refresh();
}

void DrumPadControls::refreshShownSwitch() {
    if (bound_ < 0)
        return;
    const auto& pad = kit_.pad(bound_);
    // A pad Show all brought back is visible but not chosen: unticked until the person ticks it.
    shown_.setToggleState(kit_.padKept(bound_), juce::dontSendNotification);
    shown_.setTitle("Show on card: " + pad.name);
    shown_.setTooltip("Show " + pad.name +
                      " on the card, in the clip editor and in the drum machine even when Show all is off. Off hides "
                      "it; it still plays (H on the pads)");
}

void DrumPadControls::refresh() {
    if (bound_ < 0)
        return;
    const auto& pad = kit_.pad(bound_);
    setTitle(pad.name + " controls");
    name_.setText(pad.name, juce::dontSendNotification);
    refreshNote(pad);
    dot_ = drumLaneForPitch(pad.sound >= 0 ? pad.sound : pad.note).colour;
    refreshShownSwitch();
    refreshTuneKey();
    for (size_t f = 0; f < knobs_.size(); ++f) {
        const juce::String name = juce::String(kFieldWords[f]) + ": " + pad.name;
        knobs_[f].setTitle(name);
        knobs_[f].setTooltip(name);
        // After the tooltip, so the owner's MIDI Learn registry keeps it as the knob's own text.
        if (onKnobBound)
            onKnobBound(knobs_[f], kit_.padParameter(bound_, (PadParam)f));
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
    // Start, Plays pitch and Open fade in and out with the pad's kind; the layout follows the kind, not the fades.
    startOn_ = showsStart(pad);
    pitchOn_ = isInstrument(pad);
    openOn_ = pitchOn_;
    fadeStart_.setShown(startOn_);
    fadePitch_.setShown(pitchOn_);
    fadeOpen_.setShown(openOn_);
    resized();
    repaint();
}

// The note field says which key plays the pad (a drum map's input note), apart from the pitch Tune gives the drum.
void DrumPadControls::refreshNote(const synth::drums::Pad& pad) {
    note_.setRange(0, 127, synth::drums::defaultPad(bound_).note);
    note_.setValue(pad.note);
    note_.setValueText(synth::drums::noteName(pad.note) + " (" + juce::String(pad.note) + ")");
    note_.setTitle("Plays on key for " + pad.name);
    note_.setTooltip(pad.name + " plays when this note arrives: a key, a clip or a controller sending " +
                     synth::drums::noteName(pad.note) +
                     ". It sets which key triggers the pad, not the drum's pitch (Tune does). Type a note such as C1 "
                     "or a number; Cmd-click resets.");
    noteCaption_.setTooltip({});
}

std::optional<synth::MusicalScale> DrumPadControls::activeTuneKey() const {
    const auto& table = kit_.padTable();
    if (table.tuneFollowsSong)
        return songKey ? songKey() : std::nullopt;
    return table.tuneKey;
}

void DrumPadControls::refreshTuneKey() {
    const auto& table = kit_.padTable();
    tuneKey_->setState(table.tuneFollowsSong, songKey ? songKey() : std::nullopt, table.tuneKey);
}

// The Sound row names what the pad plays; an instrument pad adds Open and its pitch.
void DrumPadControls::refreshSoundRow(const synth::drums::Pad& pad) {
    auto [name, detail] = describeSound ? describeSound(bound_) : std::make_pair(pad.name, juce::String());
    sound_.setInstrument(name, detail);
    sound_.setTitle("Sound for " + pad.name + ", " + name + (detail.isNotEmpty() ? ", " + detail : juce::String()));
    sound_.setTooltip("Choose what " + pad.name + " plays: a file, a built-in drum or an instrument (Enter)");
    open_.setTexts("Open " + name + " on the canvas", "Show the instrument " + pad.name + " plays on the canvas");
    pitch_.setValueText(synth::drums::noteName(pad.instrumentPitch) + " (" + juce::String(pad.instrumentPitch) + ")");
}

// The panel never fades: the text and the Sound row change at once and the knobs glide to the new pad's values
// (DrumPadControlsGlide.cpp). Under Animations Off, or off screen, everything lands at once.
void DrumPadControls::showPad(int index, bool animate) {
    if (index == bound_) {
        refresh();
        return;
    }
    glideBinding_ = animate && bound_ >= 0;
    armColumns_ = glideBinding_;
    bind(index);
    glideBinding_ = armColumns_ = false;
}

// ---- Layout and painting ----

void DrumPadControls::resized() {
    auto area = getLocalBounds();
    auto header = area.removeFromTop(kHeaderHeight);
    header.removeFromLeft(kDotSpace);
    note_.setBounds(header.removeFromRight(note_.getIdealWidth(kHeaderHeight)));
    noteCaption_.setBounds(header.removeFromRight(kNoteCaptionWidth));
    name_.setBounds(header);
    area.removeFromTop(kHeaderGap);
    auto row = area.removeFromTop(synth::cardbody::kRowHeight);
    soundLabel_.setBounds(row.removeFromLeft(kSoundLabelWidth));
    if (openOn_)
        open_.setBounds(row.removeFromRight(TrackShowModuleButton::kSize)
                            .withSizeKeepingCentre(TrackShowModuleButton::kSize, TrackShowModuleButton::kSize));
    row.removeFromRight(openOn_ ? kRowGap : 0);
    sound_.setBounds(row);
    area.removeFromTop(kRowGap);
    auto tuneRow = area.removeFromBottom(kShownHeight);
    tuneKeyLabel_.setBounds(tuneRow.removeFromLeft(kTuneKeyLabelWidth));
    tuneKey_->setBounds(tuneRow);
    area.removeFromBottom(kTuneKeyGap);
    shown_.setBounds(area.removeFromBottom(kShownHeight).withWidth(juce::jmin(area.getWidth(), 118)));
    area.removeFromBottom(kShownGap);
    layoutColumns(area);
}

void DrumPadControls::paint(juce::Graphics& g) {
    if (bound_ < 0)
        return;
    g.setColour(dot_);
    g.fillEllipse(0.0f, ((float)kHeaderHeight - kDot) * 0.5f, kDot, kDot);
}

// The captions read in the theme's muted text, a step toward the primary text so it stays above 4.5:1 on a light card,
// and follows a theme change.
void DrumPadControls::lookAndFeelChanged() {
    const auto& colors = synth::theme::themeOf(*this).colors;
    const auto muted = colors.textMuted.interpolatedWith(colors.textPrimary, 0.2f);
    noteCaption_.setColour(juce::Label::textColourId, muted);
    tuneKeyLabel_.setColour(juce::Label::textColourId, muted);
}

std::unique_ptr<juce::AccessibilityHandler> DrumPadControls::createAccessibilityHandler() {
    return std::make_unique<juce::AccessibilityHandler>(*this, juce::AccessibilityRole::group);
}

} // namespace synth::ui
