#pragma once

// TuneScaleSnap.h -- gives a Tune or Pitch knob a scale to land on. The knob's value is the interval in semitones
// above the key's root, so a drag, a typed value, the arrow keys and the wheel all stop on the scale's notes. The
// engine is SequencerScaleSnap.h (shared with the piano roll and the sequencers); a card hands this the key to follow
// now (its own, the song's, or none for every semitone), read each time the knob moves.
#include "Modules/Sequencer/SequencerScaleSnap.h"
#include "UI/Graph/ModuleComponent/CardKnobSlider.h"
#include <functional>
#include <optional>

namespace synth::ui::tunescale {

using ActiveKey = std::function<std::optional<MusicalScale>()>;

/** Makes `knob` land only on the notes of `activeKey()` (nothing: the knob is free). Call once, after the knob's
 *  range is set; the range is read at each move. */
inline void wire(CardKnobSlider& knob, ActiveKey activeKey) {
    auto* k = &knob;
    knob.snapHook = [k, activeKey](double value) {
        return snapTuneToKey(value, activeKey ? activeKey() : std::nullopt, k->getMinimum(), k->getMaximum());
    };
    knob.stepHook = [k, activeKey](double value, int direction) {
        return stepTuneInKey(value, direction, activeKey ? activeKey() : std::nullopt, k->getMinimum(),
                             k->getMaximum());
    };
}

} // namespace synth::ui::tunescale
