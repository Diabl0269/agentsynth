#pragma once

#include <juce_core/juce_core.h>

namespace synth {

/** The transport state a project persists: tempo, time signature and loop. Play state and position are
 *  deliberately not part of it. */
struct TransportDoc {
    double bpm = 120.0;
    int timeSigNumerator = 4;
    int timeSigDenominator = 4;
    double loopStartBeat = 0.0;
    double loopEndBeat = 4.0;
    bool loopEnabled = false;

    juce::var toVar() const;

    /** All-or-nothing: returns false and leaves `*this` untouched unless `v` is a fully valid document. */
    bool fromVar(const juce::var& v);

    bool operator==(const TransportDoc& other) const noexcept;
    bool operator!=(const TransportDoc& other) const noexcept { return !(*this == other); }
};

} // namespace synth
