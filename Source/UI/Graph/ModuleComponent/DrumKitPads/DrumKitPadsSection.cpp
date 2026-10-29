// DrumKitPadsSection.cpp -- the pads area: its geometry (shared with the card's size estimate), the count stepper and
// its undo step, the caption and pick hint, and keeping the grid and the controls on the kit.
#include "UI/Graph/ModuleComponent/DrumKitPads/DrumKitPadsSection.h"
#include "AppUndoManager.h"
#include "UI/Graph/CardBody/CardBodyGeometry.h"

namespace synth::ui {

using synth::drums::kMaxPads;
using synth::drums::kPadStep;
using synth::drums::PadKind;

namespace {

constexpr int kStepperWidth = 104;

// A pad holds a setting of its own when it differs from what a fresh kit holds there: its note, name, file or
// instrument, a sound other than its default (or the one the count gives a free pad), or a knob moved off its
// default. Kit and built-in count as the same kind: the Recorded switches move pads between them.
bool padHasSettings(const DrumKitModule& kit, int index) {
    const auto& pad = kit.pad(index);
    const auto fresh = synth::drums::defaultPad(index);
    if (pad.kind == PadKind::File || pad.kind == PadKind::Instrument || pad.note != fresh.note ||
        pad.name != fresh.name)
        return true;
    if (pad.sound != fresh.sound && pad.sound != synth::drums::revealSoundFor(index))
        return true;
    for (auto which : {DrumKitModule::PadParam::Level, DrumKitModule::PadParam::Tune, DrumKitModule::PadParam::Decay,
                       DrumKitModule::PadParam::Start})
        if (const juce::AudioProcessorParameter* p = kit.padParameter(index, which);
            p != nullptr && p->getValue() != p->getDefaultValue())
            return true;
    return false;
}

bool sameTable(const synth::drums::PadTable& a, const synth::drums::PadTable& b) {
    return a.count == b.count && a.pads == b.pads;
}

} // namespace

int DrumKitPadsSection::height(const DrumKitModule& kit) {
    return synth::cardbody::kRowHeight + kGridGap + DrumPadGrid::heightFor(kit.padCount()) + kCaptionGap +
           kCaptionHeight + kControlsGap + DrumPadControls::height() + kSectionGap;
}

int DrumKitPadsSection::hiddenPadsWithSettings(const DrumKitModule& kit) {
    int hidden = 0;
    for (int i = kit.padCount(); i < kMaxPads; ++i)
        hidden += padHasSettings(kit, i) ? 1 : 0;
    return hidden;
}

// Up/Down (and Left/Right) step the count by four through the CardStepper; + and - do the same with Shift or without.
bool DrumKitPadsSection::CountStepper::keyPressed(const juce::KeyPress& key) {
    const auto c = key.getTextCharacter();
    if ((c == '+' || c == '=' || c == '-') && onStep && !key.getModifiers().isCommandDown()) {
        onStep(c == '-' ? -1 : +1);
        return true;
    }
    return CardStepper::keyPressed(key);
}

DrumKitPadsSection::DrumKitPadsSection(const InstrumentChrome::Host& host, DrumKitModule& kit)
    : host_(host)
    , kit_(kit)
    , grid_(kit)
    , controls_(kit) {
    stepperLabel_.setJustificationType(juce::Justification::centredLeft);
    host_.card->addAndMakeVisible(stepperLabel_);
    stepper_.setComponentID("drumPadCount");
    stepper_.setTooltip("How many pads the card shows. Hidden pads keep their sounds.");
    stepper_.setTypable(true);
    stepper_.onStep = [this](int delta) { setPadCountUndoably(kit_.padCount() + delta * kPadStep); };
    stepper_.onTypedValue = [this](const juce::String& text) {
        if (text.containsAnyOf("0123456789"))
            setPadCountUndoably(text.getIntValue());
        refreshStepper();
    };
    host_.card->addAndMakeVisible(stepper_);

    grid_.onPicked = [this](int index, bool played) { picked(index, played); };
    host_.card->addAndMakeVisible(grid_);

    caption_.setComponentID("drumPadsCaption");
    caption_.setJustificationType(juce::Justification::centredLeft);
    caption_.setMinimumHorizontalScale(1.0f);
    host_.card->addAndMakeVisible(caption_);

    controls_.onEscape = [this] { grid_.grabKeyboardFocus(); };
    host_.card->addAndMakeVisible(controls_);

    refreshFromKit();
    controls_.showPad(grid_.picked(), false);
}

int DrumKitPadsSection::layout(int y, int x, int w, bool apply) {
    if (apply) {
        auto row = juce::Rectangle<int>(x, y, w, synth::cardbody::kRowHeight);
        stepper_.setBounds(row.removeFromRight(kStepperWidth));
        stepperLabel_.setBounds(row);
    }
    y += synth::cardbody::kRowHeight + kGridGap;
    const int gridH = DrumPadGrid::heightFor(kit_.padCount());
    if (apply)
        grid_.setBounds(x, y, w, gridH);
    y += gridH + kCaptionGap;
    if (apply)
        caption_.setBounds(x, y, w, kCaptionHeight);
    y += kCaptionHeight + kControlsGap;
    if (apply)
        controls_.setBounds(x, y, w, DrumPadControls::height());
    return y + DrumPadControls::height() + kSectionGap;
}

// ---- The count ----

void DrumKitPadsSection::setPadCountUndoably(int count) {
    count = synth::drums::clampPadCount(count);
    if (count == kit_.padCount())
        return;
    const bool record = host_.undo != nullptr && host_.graph != nullptr;
    if (record)
        host_.undo->captureBeforeState(*host_.graph);
    kit_.setPadCount(count);
    if (record)
        host_.undo->pushSnapshotFromCapture(*host_.graph);
    refreshFromKit();
    if (host_.relayout)
        host_.relayout();
}

void DrumKitPadsSection::refreshStepper() {
    const int count = kit_.padCount();
    stepper_.setValueText(juce::String(count));
    stepper_.setTitle("Pads, " + juce::String(count) + " of " + juce::String(kMaxPads) + " maximum");
}

// ---- Picks ----

// A pick that did not sound says so for a moment on the caption line, so a quiet click never reads as broken.
void DrumKitPadsSection::picked(int index, bool played) {
    const auto& name = kit_.pad(index).name;
    if (!played) {
        hint_ = name + " picked (no sound)";
        hintUntil_ = juce::Time::getMillisecondCounter() + (juce::uint32)kHintMs;
    } else {
        hint_.clear();
    }
    refreshCaption();
    lastAnnouncement_ = name + " picked";
    juce::AccessibilityHandler::postAnnouncement(lastAnnouncement_,
                                                 juce::AccessibilityHandler::AnnouncementPriority::medium);
    controls_.showPad(index, true);
}

void DrumKitPadsSection::refreshCaption() {
    juce::String text;
    if (hint_.isNotEmpty()) {
        text = hint_;
    } else if (const int hidden = hiddenPadsWithSettings(kit_); hidden > 0) {
        text = juce::String(hidden) + (hidden == 1 ? " hidden pad keeps its sound" : " hidden pads keep their sounds");
    } else {
        text = "Click a pad to hear it, Shift+click to pick it quietly";
    }
    if (caption_.getText() != text)
        caption_.setText(text, juce::dontSendNotification);
}

// ---- Following the kit ----

void DrumKitPadsSection::refreshFromKit() {
    shownTable_ = kit_.padTable();
    shownRecordedKit_ = kit_.hasRecordedKit();
    shownGeneration_ = kit_.getInstrumentGeneration();
    grid_.refresh();
    refreshStepper();
    refreshCaption();
    controls_.refresh();
}

void DrumKitPadsSection::poll() {
    if (hint_.isNotEmpty() && juce::Time::getMillisecondCounter() >= hintUntil_) {
        hint_.clear();
        refreshCaption();
    }
    const bool countChanged = kit_.padCount() != shownTable_.count;
    if (countChanged || !sameTable(kit_.padTable(), shownTable_) || kit_.hasRecordedKit() != shownRecordedKit_ ||
        kit_.getInstrumentGeneration() != shownGeneration_) {
        refreshFromKit();
        if (countChanged && host_.relayout)
            host_.relayout();
    }
}

} // namespace synth::ui
