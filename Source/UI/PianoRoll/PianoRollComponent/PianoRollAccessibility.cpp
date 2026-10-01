// PianoRollAccessibility.cpp
//
// How PianoRollComponent reaches a keyboard-only or screen-reader user: the focused note and its
// ring, the grid's spoken value, and the Tab-stop buttons laid over the painted header chips.
// PianoRollComponent is declared in PianoRollComponent.h; sibling PianoRoll*.cpp files hold the rest
// of the class.
//
// The focused note is not a second piece of state: it is the one note the selection holds, so
// anything that changes the selection (a click, a marquee, Alt+Left/Right, Delete, undo) moves or
// ends it without this file being told, and every selection-based verb acts on it unchanged.

#include "PianoRollComponent.h"

#include "PianoRollHeaderChip.h"
#include "UI/Layout/FocusRing.h"
#include "UI/PianoRoll/NoteAccessibilityText.h"

namespace synth::ui {

namespace {

using HeaderButtonId = PianoRollComponent::HeaderButtonId;

// Exposes the grid's value text, so a screen reader reads the note the keys landed on rather than
// just "Piano roll".
class RollValueInterface : public juce::AccessibilityTextValueInterface {
public:
    explicit RollValueInterface(const PianoRollComponent& roll)
        : roll_(roll) {}

    bool isReadOnly() const override { return true; }
    juce::String getCurrentValueAsString() const override { return roll_.getAccessibilityValueText(); }
    void setValueAsString(const juce::String&) override {}

private:
    const PianoRollComponent& roll_;
};

struct ChipSpec {
    HeaderButtonId id;
    const char* title;
    const char* componentId;
    bool toggles;
};

constexpr ChipSpec kChipSpecs[] = {
    {HeaderButtonId::Back, "Back to clips", "pianoRollBackChip", false},
    {HeaderButtonId::Quantise, "Quantize note starts", "pianoRollQuantiseChip", false},
    {HeaderButtonId::QuantiseLength, "Quantize note lengths", "pianoRollQuantiseLengthChip", false},
    {HeaderButtonId::QuantisePitches, "Quantize note pitches to the scale", "pianoRollQuantisePitchChip", false},
    {HeaderButtonId::Scale, "Scale assist", "pianoRollScaleChip", true},
    {HeaderButtonId::ScaleFilter, "Show only scale notes", "pianoRollScaleFilterChip", true},
    {HeaderButtonId::Velocity, "Show velocity strip", "pianoRollVelocityChip", true},
    {HeaderButtonId::Humanize, "Humanize velocities", "pianoRollHumanizeChip", false},
};

std::size_t chipIndex(HeaderButtonId id) { return (std::size_t)id - 1; }

} // namespace

//==============================================================================
// ---- Focused note and screen reader ----

synth::NoteId PianoRollComponent::getFocusedNote() const noexcept {
    if (doc_ == nullptr || !clipId_.isValid() || selection_.size() != 1)
        return {};
    const auto id = selection_.getSelected().front();
    return doc_->getNote(id) != nullptr ? id : synth::NoteId{};
}

juce::String PianoRollComponent::getAccessibilityValueText() const {
    const auto* clip = doc_ != nullptr && clipId_.isValid() ? doc_->getClip(clipId_) : nullptr;
    if (clip == nullptr)
        return {};
    if (const auto* note = doc_->getNote(getFocusedNote()))
        return describeNoteForAccessibility(*note, clip->startBeat + note->startBeat, currentBeatsPerBar());
    return describeRollForAccessibility(clip->name, (int)clip->notes.size(), selection_.size());
}

std::unique_ptr<juce::AccessibilityHandler> PianoRollComponent::createAccessibilityHandler() {
    return std::make_unique<juce::AccessibilityHandler>(
        *this, juce::AccessibilityRole::group, juce::AccessibilityActions{},
        juce::AccessibilityHandler::Interfaces{std::make_unique<RollValueInterface>(*this)});
}

// A value-changed event is posted only when the text differs from the last one handed out, so a
// doc change that leaves the focused note alone never re-reads it.
void PianoRollComponent::refreshAccessibilityValue() {
    const auto text = getAccessibilityValueText();
    if (text == announcedValueText_)
        return;
    announcedValueText_ = text;
    if (auto* handler = getAccessibilityHandler())
        handler->notifyAccessibilityEvent(juce::AccessibilityEvent::valueChanged);
}

void PianoRollComponent::paintFocusedNoteRing(juce::Graphics& g) {
    if (!focusRingForced_ && !hasKeyboardFocus(false))
        return;
    const auto rect = getNoteRect(getFocusedNote());
    if (rect.isEmpty())
        return;
    // The ring keeps clear of the keys column and the header, exactly like the note it surrounds.
    const juce::Graphics::ScopedSaveState state(g);
    g.reduceClipRegion(gridRegion());
    paintFocusRingAlways(g, rect.toFloat().expanded(2.0f), *this, 2.0f);
}

void PianoRollComponent::focusGained(juce::Component::FocusChangeType) {
    if (getFocusedNote().isValid())
        repaint();
}

void PianoRollComponent::focusLost(juce::Component::FocusChangeType) {
    if (getFocusedNote().isValid())
        repaint();
}

void PianoRollComponent::setFocusRingForcedForTest(bool forced) noexcept {
    focusRingForced_ = forced;
    repaint();
}

//==============================================================================
// ---- Header chips ----

void PianoRollComponent::initHeaderChips() {
    for (const auto& spec : kChipSpecs) {
        const auto id = spec.id;
        auto chip = std::make_unique<PianoRollHeaderChip>(spec.title, [this, id] { return headerTooltipText(id); });
        chip->setComponentID(spec.componentId);
        chip->setClickingTogglesState(spec.toggles);
        chip->onClick = [this, id] { activateHeaderButton(id); };
        addAndMakeVisible(*chip);
        headerChips_[chipIndex(id)] = std::move(chip);
    }
}

void PianoRollComponent::layoutHeaderChips() {
    for (const auto& spec : kChipSpecs)
        headerChips_[chipIndex(spec.id)]->setBounds(headerButtonBoundsFor(spec.id));
}

// The three toggle chips mirror the roll's own state (the panel's logical target, the row filter, the
// strip's logical target), so the screen reader hears "on" exactly when the chip paints lit.
void PianoRollComponent::syncHeaderChipStates() {
    const auto set = [this](HeaderButtonId id, bool on) {
        headerChips_[chipIndex(id)]->setToggleState(on, juce::dontSendNotification);
    };
    set(HeaderButtonId::Scale, scalePanelVisible_);
    set(HeaderButtonId::ScaleFilter, isScaleFilterOn());
    set(HeaderButtonId::Velocity, velocityLaneVisible_);
}

PianoRollComponent::HeaderButtonId PianoRollComponent::headerButtonAt(juce::Point<int> pos) const noexcept {
    for (const auto& spec : kChipSpecs)
        if (headerButtonBoundsFor(spec.id).contains(pos))
            return spec.id;
    return HeaderButtonId::None;
}

// Each chip does exactly ONE thing, whether it was clicked or reached with Tab and Return -- no
// modifier variants anywhere in the header. There is no Snap chip: it would duplicate the timeline
// toolbar's own Snap button on the same shared TimelineViewState::snapEnabled; toggleSnap() and the
// J key cover it.
void PianoRollComponent::activateHeaderButton(HeaderButtonId which) {
    switch (which) {
    case HeaderButtonId::Back:
        requestClose(); // the roll is closed from here on: nothing below may touch it
        return;
    case HeaderButtonId::Quantise:
        flashQuantiseButton(); // feedback even when the click is a no-op
        performQuantise();
        break;
    case HeaderButtonId::QuantiseLength:
        flashQuantiseLengthButton(); // mirrors the chip above: same isQuantiseEnabled() gate
        performQuantiseLength();
        break;
    case HeaderButtonId::QuantisePitches:
        quantisePitchesToActiveScale(); // silently a no-op with no scale chosen, like the chip's dim
        break;
    case HeaderButtonId::Scale:
        toggleScalePanel();
        break;
    case HeaderButtonId::ScaleFilter:
        toggleScaleFilter();
        break;
    case HeaderButtonId::Velocity:
        toggleVelocityLane();
        break;
    case HeaderButtonId::Humanize:
        showHumanizeMenu();
        break;
    case HeaderButtonId::None:
        break;
    }
    syncHeaderChipStates(); // a toggle chip that acted on nothing must not keep the state its click flipped
}

juce::String PianoRollComponent::headerTooltipText(HeaderButtonId which) const {
    switch (which) {
    case HeaderButtonId::Back:
        return "Close the piano roll and go back to the clips (Esc)";
    case HeaderButtonId::Quantise:
        return quantiseTooltipText();
    case HeaderButtonId::QuantiseLength:
        return quantiseLengthTooltipText();
    case HeaderButtonId::QuantisePitches:
        return quantisePitchTooltipText();
    case HeaderButtonId::Scale:
        return scaleTooltipText();
    case HeaderButtonId::ScaleFilter:
        return scaleFilterTooltipText();
    case HeaderButtonId::Velocity:
        return velocityTooltipText();
    case HeaderButtonId::Humanize:
        return juce::String::fromUTF8("Humanize velocities \xE2\x80\x94 adds a small random offset to the selected "
                                      "notes, or to every note when none is selected");
    case HeaderButtonId::None:
        break;
    }
    return {};
}

juce::Button* PianoRollComponent::getHeaderChipButtonForTest(HeaderButtonId which) noexcept {
    return which == HeaderButtonId::None ? nullptr : headerChips_[chipIndex(which)].get();
}

} // namespace synth::ui
