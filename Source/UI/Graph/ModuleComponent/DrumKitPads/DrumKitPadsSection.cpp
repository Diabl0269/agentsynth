// DrumKitPadsSection.cpp -- the pads area: its geometry (shared with the card's size estimate), the count stepper and
// its undo step, the caption and pick hint, and keeping the grid and the controls on the kit.
#include "UI/Graph/ModuleComponent/DrumKitPads/DrumKitPadsSection.h"
#include "AppUndoManager.h"
#include "UI/Graph/CardBody/CardBodyGeometry.h"
#include "UI/Graph/ModuleComponent/DrumKitPads/DrumPadShown.h"

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

// One bit per instrument pad that reaches its module, so the card notices a module deleted under it.
std::uint32_t linkedInstrumentPads(const DrumKitModule& kit) {
    std::uint32_t mask = 0;
    for (int i = 0; i < kMaxPads; ++i)
        if (kit.padInstrumentLinked(i))
            mask |= 1u << (unsigned)i;
    return mask;
}

} // namespace

int DrumKitPadsSection::height(const DrumKitModule& kit) {
    return synth::cardbody::kRowHeight + kGridGap + DrumPadGrid::heightFor((int)kit.shownPads().size()) + kCaptionGap +
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

    showAll_.setComponentID("drumPadsShowAll");
    showAll_.setTitle("Show all hidden pads");
    showAll_.setTooltip("Show every hidden pad again. Undo hides them again.");
    showAll_.setWantsKeyboardFocus(true);
    showAll_.setVisible(false);
    showAll_.onClick = [this] { showAllPads(); };
    host_.card->addChildComponent(showAll_);

    grid_.onToggleShown = [this](int index) { setPadShown(index, !kit_.padShown(index)); };
    controls_.onShownChanged = [this](int index, bool shown) { setPadShown(index, shown); };
    controls_.onEscape = [this] { grid_.grabKeyboardFocus(); };
    host_.card->addAndMakeVisible(controls_);
    wireSoundControls();

    refreshFromKit();
    controls_.showPad(grid_.picked(), false);
}

DrumKitPadsSection::~DrumKitPadsSection() { *alive_ = false; }

int DrumKitPadsSection::layout(int y, int x, int w, bool apply) {
    if (apply) {
        auto row = juce::Rectangle<int>(x, y, w, synth::cardbody::kRowHeight);
        stepper_.setBounds(row.removeFromRight(kStepperWidth));
        stepperLabel_.setBounds(row);
    }
    y += synth::cardbody::kRowHeight + kGridGap;
    const int gridH = DrumPadGrid::heightFor((int)kit_.shownPads().size());
    if (apply)
        grid_.setBounds(x, y, w, gridH);
    y += gridH + kCaptionGap;
    if (apply) {
        captionRow_ = {x, y, w, kCaptionHeight};
        placeCaption();
    }
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

// ---- Shown ----

void DrumKitPadsSection::setPadShown(int index, bool shown) {
    if (index < 0 || index >= kMaxPads || kit_.padShown(index) == shown)
        return;
    const auto apply = [this, index, shown, alive = alive_] {
        if (!*alive || !synth::ui::setPadShownUndoably(host_.undo, host_.graph, kit_, index, shown))
            return;
        hint_.clear(); // the count of hidden pads, not "picked", is what the caption says now
        refreshFromKit();
        relayoutIfGridResized();
    };
    if (!shown && host_.confirmHidePad)
        host_.confirmHidePad(kit_.pad(index).note, kit_.pad(index).name, apply);
    else
        apply();
}

void DrumKitPadsSection::showAllPads() {
    if (synth::ui::showAllPadsUndoably(host_.undo, host_.graph, kit_) == 0)
        return;
    hint_.clear();
    refreshFromKit();
    relayoutIfGridResized();
}

// The card's height follows the rows the shown pads need.
void DrumKitPadsSection::relayoutIfGridResized() {
    if (grid_.getHeight() != DrumPadGrid::heightFor(grid_.visibleCount()) && host_.relayout)
        host_.relayout();
}

juce::String DrumKitPadsSection::hiddenCaption() const {
    const int hidden = kit_.hiddenPadCount();
    return hidden > 0 ? juce::String(hidden) + " hidden" : juce::String();
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
    if (const auto drop = dropHint(); drop.isNotEmpty()) {
        text = drop;
    } else if (hint_.isNotEmpty()) {
        text = hint_;
    } else if (kit_.hiddenPadCount() > 0) {
        text = hiddenCaption() + juce::String::fromUTF8(" \xC2\xB7");
    } else if (const int hidden = hiddenPadsWithSettings(kit_); hidden > 0) {
        text = juce::String(hidden) + (hidden == 1 ? " hidden pad keeps its sound" : " hidden pads keep their sounds");
    } else {
        text = "Click to play, right-click for a sound";
    }
    if (caption_.getText() != text)
        caption_.setText(text, juce::dontSendNotification);
    placeCaption();
}

// "2 hidden · Show all": the link sits right after the count (and is away while a pick hint has the line).
void DrumKitPadsSection::placeCaption() {
    const bool link = hint_.isEmpty() && kit_.hiddenPadCount() > 0;
    const float textWidth = juce::GlyphArrangement::getStringWidth(caption_.getFont(), caption_.getText());
    constexpr int kLinkWidth = 56;
    const int textW = link ? juce::jmin(captionRow_.getWidth() - kLinkWidth,
                                        (int)std::ceil(textWidth) + caption_.getBorderSize().getLeftAndRight() + 2)
                           : captionRow_.getWidth();
    caption_.setBounds(captionRow_.withWidth(textW));
    showAll_.setBounds(captionRow_.withTrimmedLeft(textW).withWidth(kLinkWidth));
    showAll_.setVisible(link);
}

// ---- Following the kit ----

void DrumKitPadsSection::refreshFromKit() {
    relinkInstruments();
    shownLinks_ = linkedInstrumentPads(kit_);
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
    if (grid_.isShowingSoundOutline() != shownOutline_) {
        shownOutline_ = grid_.isShowingSoundOutline();
        refreshCaption();
    }
    const bool countChanged = kit_.padCount() != shownTable_.count;
    if (countChanged || !sameTable(kit_.padTable(), shownTable_) || kit_.hasRecordedKit() != shownRecordedKit_ ||
        kit_.getInstrumentGeneration() != shownGeneration_ || linkedInstrumentPads(kit_) != shownLinks_) {
        refreshFromKit();
        if (countChanged && host_.relayout)
            host_.relayout();
        else
            relayoutIfGridResized(); // a pad switched off or on elsewhere (undo, a load)
    }
}

} // namespace synth::ui
