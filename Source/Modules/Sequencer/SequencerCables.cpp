#include "SequencerCables.h"
#include <cmath>

namespace synth {

// The knob's own value moved by the cable in the knob's normalised range, so +1.0 sweeps it to its maximum: the same
// movement the card's ring draws. Transpose lands on a whole semitone.
SequencerSettings SequencerCables::at(const SequencerSettings& base, int sample) const noexcept {
    if (block_ == nullptr)
        return base;
    SequencerSettings out = base;
    out.transpose = (int)std::lround(list_.valueAt(*block_, transpose_, sample));
    out.gateAmount = list_.valueAt(*block_, gate_, sample);
    out.velocityAmount = list_.valueAt(*block_, velocity_, sample);
    out.chanceAmount = list_.valueAt(*block_, chance_, sample);
    return out;
}

} // namespace synth
