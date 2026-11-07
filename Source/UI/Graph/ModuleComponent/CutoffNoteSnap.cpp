// CutoffNoteSnap.cpp -- the Filter cutoff knob's Snap to notes: the snapping, the note steps, the readout ("A3 220":
// the note and the whole hertz, short enough for the card's value box) and the knob menu's three choices.
#include "CutoffNoteSnap.h"
#include "CardKnobSlider.h"
#include "Modules/FilterModule.h"
#include "UI/ModuleViews/ShapeEditor/PitchGrid.h"
#include <cmath>

namespace synth::ui::cutoffsnap {

namespace {
constexpr double kMinHz = 20.0;
constexpr double kMaxHz = 20000.0;

bool inKey(int midi, Mode mode, const std::optional<MusicalScale>& key) {
    return mode != Mode::key || !key.has_value() || key->isChromatic() || key->contains(midi);
}

bool customType(FilterModule& filter) { return filter.getCurrentFilterType() == FilterModule::kCustomTypeIndex; }

Mode liveMode(FilterModule& filter) { return customType(filter) ? modeOf(filter.getCardViewState()) : Mode::free; }
} // namespace

Mode modeOf(const CardViewState& state) {
    if (state.cutoffSnap == "notes")
        return Mode::notes;
    if (state.cutoffSnap == "key")
        return Mode::key;
    return Mode::free;
}

void setMode(CardViewState& state, Mode mode) {
    state.cutoffSnap = mode == Mode::notes ? "notes" : mode == Mode::key ? "key" : "";
}

// Searches outward from the nearest semitone so By key lands on the closest note of the key (a tie goes down).
double snapHz(double hz, Mode mode, const std::optional<MusicalScale>& key) {
    if (mode == Mode::free || hz <= 0.0)
        return hz;
    const double midi = pitchgrid::midiOf(hz);
    const int nearest = (int)std::lround(midi);
    for (int distance = 0; distance <= 12; ++distance)
        for (const int candidate : {nearest - distance, nearest + distance}) {
            const double candidateHz = pitchgrid::noteHz(candidate);
            if (inKey(candidate, mode, key) && candidateHz >= kMinHz && candidateHz <= kMaxHz)
                return candidateHz;
        }
    return hz;
}

double stepHz(double hz, int direction, Mode mode, const std::optional<MusicalScale>& key) {
    const int from = (int)std::lround(pitchgrid::midiOf(snapHz(hz, mode, key)));
    for (int midi = from + direction; std::abs(midi - from) <= 12; midi += direction) {
        const double next = pitchgrid::noteHz(midi);
        if (next < kMinHz || next > kMaxHz)
            return hz;
        if (inKey(midi, mode, key))
            return next;
    }
    return hz;
}

juce::String readout(double hz) {
    return pitchgrid::noteName((int)std::lround(pitchgrid::midiOf(hz))) + " " + juce::String((int)std::lround(hz));
}

juce::String spokenReadout(double hz) { return readout(hz) + " hertz"; }

void wire(CardKnobSlider& knob, FilterModule& filter, std::function<std::optional<MusicalScale>()> songKey) {
    auto* f = &filter;
    knob.snapHook = [f, songKey](double hz) { return snapHz(hz, liveMode(*f), songKey ? songKey() : std::nullopt); };
    knob.stepHook = [f, songKey](double hz, int direction) -> std::optional<double> {
        const auto mode = liveMode(*f);
        if (mode == Mode::free)
            return std::nullopt;
        return stepHz(hz, direction, mode, songKey ? songKey() : std::nullopt);
    };
    knob.readoutHook = [f](double hz) -> std::optional<juce::String> {
        if (liveMode(*f) == Mode::free)
            return std::nullopt;
        return readout(hz);
    };
    knob.spokenHook = [f](double hz) -> std::optional<juce::String> {
        if (liveMode(*f) == Mode::free)
            return std::nullopt;
        return spokenReadout(hz);
    };
    knob.invalidateAccessibilityHandler();
    knob.setValueBoxMinimumScale(kValueBoxMinimumScale);
}

// The pick re-snaps the current cutoff at once, as one change gesture, so the knob never sits between notes in a
// snapped mode.
void appendMenuItems(juce::PopupMenu& menu, juce::AudioProcessor* module, const juce::RangedAudioParameter* param,
                     juce::Slider* knob) {
    auto* filter = dynamic_cast<FilterModule*>(module);
    if (filter == nullptr || knob == nullptr || param == nullptr || param->paramID != "cutoff" || !customType(*filter))
        return;
    const auto current = modeOf(filter->getCardViewState());
    juce::Component::SafePointer<juce::Slider> safeKnob(knob);
    juce::PopupMenu snap;
    const std::pair<Mode, const char*> choices[] = {
        {Mode::free, "Free"}, {Mode::notes, "Notes"}, {Mode::key, "By key"}};
    for (const auto& [mode, name] : choices)
        snap.addItem(name, true, mode == current, [filter, safeKnob, mode = mode] {
            if (safeKnob == nullptr) // the card (and maybe its module) went while the menu was open
                return;
            auto state = filter->getCardViewState();
            setMode(state, mode);
            filter->setCardViewState(state);
            const double snapped = safeKnob->snapValue(safeKnob->getValue(), juce::Slider::notDragging);
            if (snapped != safeKnob->getValue()) {
                juce::Slider::ScopedDragNotification gesture(*safeKnob);
                safeKnob->setValue(snapped, juce::sendNotificationSync);
            }
            safeKnob->updateText();
            safeKnob->repaint();
        });
    menu.addSubMenu("Snap to notes", snap);
}

} // namespace synth::ui::cutoffsnap
