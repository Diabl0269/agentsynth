#pragma once

#include "../AudioEngine.h"
#include "Metronome.h"
#include <juce_core/juce_core.h>

namespace synth {

// Two small RAII guards shared by every offline-render session (BounceSession, StemSession) that
// drives an AudioEngine's graph through OfflineTransportDriver. Factored out here rather than
// duplicated per session, per the root CLAUDE.md invariant that a render suspends BOTH the
// metronome click and external MIDI for its whole duration.

// The metronome is summed POST-graph (AudioEngine::renderPass), so a session that captures exactly
// the graph's own output buffer would otherwise pick the click up. Forces BOTH the user toggle and
// the count-in forced-on flag off for the render and restores them afterwards, on every exit path.
struct MetronomeForceOffGuard {
    explicit MetronomeForceOffGuard(Metronome& metronomeIn) noexcept
        : metronome(metronomeIn)
        , savedEnabled(metronomeIn.isEnabled())
        , savedForcedOn(metronomeIn.isForcedOn()) {
        metronome.setEnabled(false);
        metronome.setForcedOn(false);
    }
    ~MetronomeForceOffGuard() noexcept {
        metronome.setEnabled(savedEnabled);
        metronome.setForcedOn(savedForcedOn);
    }

    Metronome& metronome;
    bool savedEnabled;
    bool savedForcedOn;

    JUCE_DECLARE_NON_COPYABLE(MetronomeForceOffGuard)
};

// Real hardware MIDI keeps arriving on its own driver thread throughout an offline render — there
// is no device callback to suspend it from, so this closes that hole alongside
// suspendDeviceCallback/resumeDeviceCallback. See AudioEngine::suspendExternalMidi()'s comment.
struct ExternalMidiSuspendGuard {
    explicit ExternalMidiSuspendGuard(AudioEngine& engineIn) noexcept
        : engine(engineIn) {
        engine.suspendExternalMidi();
    }
    ~ExternalMidiSuspendGuard() noexcept { engine.resumeExternalMidi(); }

    AudioEngine& engine;

    JUCE_DECLARE_NON_COPYABLE(ExternalMidiSuspendGuard)
};

} // namespace synth
