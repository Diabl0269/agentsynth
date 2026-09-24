// Concern: the one continuousTargetDisplayName() implementation -- see ContinuousTarget.h.
#include "MidiRemote/ContinuousTarget.h"

namespace synth {

juce::String continuousTargetDisplayName(ContinuousTargetKind kind) {
    switch (kind) {
    case ContinuousTargetKind::bpm:
        return "Tempo (BPM)";
    case ContinuousTargetKind::playhead:
        return "Playhead Position";
    case ContinuousTargetKind::masterVolume:
        return "Master Volume";
    }
    return "Tempo (BPM)";
}

} // namespace synth
