#pragma once

#include "ModuleBase.h"
#include <juce_audio_basics/juce_audio_basics.h>

/**
 * @brief "Macro MIDI In" — a MIDI inlet jack on a Macro's boundary (P8-15 Macro I/O,
 * docs/macros.md §5, §7 item 1's MIDI-port decision).
 *
 * A SEPARATE type from MacroInlet rather than a "kind" flag on one type — the same shape of
 * choice TimelineMidiSource/TimelineAudioSource already made for Track In vs Track Audio. The
 * load-bearing reason there (confirmed against the actual code, not assumed from the type names)
 * is a CONSTRUCTION-TIME CHANNEL SHAPE difference: ModuleBase(name, 0, 0) here vs
 * ModuleBase(name, N, N) for MacroInletModule. A single type would have to branch its bus layout
 * — fixed forever once the ModuleBase constructor runs — on a runtime "kind" flag, which is
 * exactly what the fixed-channel-count invariant (Source/Modules/CLAUDE.md) makes awkward. It is
 * NOT because a MIDI processBlock and an audio processBlock happen to differ in general (here
 * both bodies are near-identical no-ops); it is specifically that MIDI carries no channel shape
 * at all — no Mono/Stereo/Poly-N — just acceptsMidi()/producesMidi(), like ExternalMidiModule and
 * PolyMidiModule.
 *
 * A pure PASS-THROUGH, same as MacroInletModule: ModuleBase's default acceptsMidi()/
 * producesMidi() (both true) already makes an unmodified `midiMessages` buffer flow straight
 * through when nothing here touches it, so there is no forwarding logic to write. Zero audio
 * channels, so there is no channel-shape question at all — unlike MacroInletModule this type
 * never needs a declare-max/vary-visible mechanism.
 *
 * Bypass: no dry-vs-processed distinction to make (there is no processing), so isBypassed() only
 * gates the activity LED below — the MIDI itself passes through either way, exactly like
 * RecordTapModule's audio does while bypassed ("pass-through is literally nothing").
 *
 * INTERNAL-ONLY, the same three exclusions as Track In / Rec Tap / Track Audio: not in the module
 * library, not offered by the replace menu, never authorable by a model
 * (kNonAuthorableModuleTypes, docs/macros.md §6). No "muted" parameter, matching those three.
 */
class MacroMidiInletModule : public ModuleBase {
public:
    MacroMidiInletModule()
        : ModuleBase("Macro MIDI In", 0, 0) {
        enableVisualBuffer(true);
    }

    ~MacroMidiInletModule() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override {
        juce::ignoreUnused(sampleRate, samplesPerBlock);
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override {
        if (isBypassed())
            return;

        if (auto* vb = getVisualBuffer()) {
            const float activity = midiMessages.isEmpty() ? 0.0f : 1.0f;
            for (int i = 0; i < buffer.getNumSamples(); ++i)
                vb->pushSample(activity);
        }
    }

    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }

    ModuleType getModuleType() const override { return ModuleType::MacroMidiInlet; }
    ModulationCategory getModulationCategory() const override { return ModulationCategory::Other; }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MacroMidiInletModule)
};
