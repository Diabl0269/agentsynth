#pragma once

#include "../Transport/TransportService.h"
#include "ModuleBase.h"
#include <cmath>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

class SequencerModule : public ModuleBase {
public:
    SequencerModule()
        : ModuleBase("Sequencer", 0, 0) // Generates MIDI
    {
        addParameter(runParam = new juce::AudioParameterBool("run", "Run", false));
        addParameter(bpmParam = new juce::AudioParameterFloat("bpm", "BPM", 30.0f, 300.0f, 120.0f));

        // Gate Lengths
        for (int i = 0; i < 8; ++i) {
            juce::String name = "Gate " + juce::String(i + 1);
            addParameter(gateParams[i] = new juce::AudioParameterFloat(name, name, 0.1f, 1.0f, 0.5f));
        }

        // Pitches
        // Pitches (F Phrygian Dominant: F, Gb, A, Bb, C, Db, Eb)
        // Notes around C3 (48) - C4 (60)

        // Seed random (basic) - actually just deterministic pseudo-random for now
        // or manually pick interesting defaults.
        int defaults[] = {53, 65, 54, 61, 53, 57, 54, 60}; // F3, F4, Gb3, Db4, F3, A3, Gb3, C4

        for (int i = 0; i < 8; ++i) {
            juce::String name = "Pitch " + juce::String(i + 1);

            auto stringFromInt = [](int value, int) {
                return juce::MidiMessage::getMidiNoteName(value, true, true, 3);
            };

            auto valueFromText = [](const juce::String& text) {
                for (int n = 0; n < 128; ++n) {
                    if (juce::MidiMessage::getMidiNoteName(n, true, true, 3).equalsIgnoreCase(text))
                        return n;
                }
                return text.getIntValue();
            };

            addParameter(
                stepParams[i] = new juce::AudioParameterInt(
                    juce::ParameterID(name, 1), name, 0, 127, defaults[i],
                    juce::AudioParameterIntAttributes()
                        .withStringFromValueFunction([stringFromInt](int v, int len) { return stringFromInt(v, len); })
                        .withValueFromStringFunction(valueFromText)));
        }

        // Filter Envelope Amounts
        for (int i = 0; i < 8; ++i) {
            juce::String name = "F.Env " + juce::String(i + 1);
            addParameter(filterEnvParams[i] = new juce::AudioParameterFloat(name, name, 0.0f, 1.0f, 0.5f));
        }

        // Opt-in transport sync (TL1-8). Default OFF: added last so every existing positional
        // getParameters()[n] lookup (including this file's own tests) keeps resolving to the same
        // parameter, and every preset that predates this parameter loads with sync off.
        addParameter(syncParam = new juce::AudioParameterBool("syncToTransport", "Sync to Transport", false));
    }

    // Exposed for UI
    std::atomic<int> currentActiveStep{0};

    void prepareToPlay(double sampleRate, int samplesPerBlock) override {
        localSampleRate = sampleRate;
        juce::ignoreUnused(samplesPerBlock);
        currentStep = 0;
        currentActiveStep = 0;
        samplesUntilNextBeat = 0;
        samplesUntilNoteOff = 0;
        lastNote = -1;
        lastRunState = false;
        lastFiredBeat = kNoBeatFired;
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override {
        if (isBypassed()) {
            buffer.clear();
            return;
        }

        juce::ignoreUnused(buffer);

#if SYNTH_ENABLE_TIMELINE
        // Sync-to-Transport (TL1-8, opt-in, default off). Only taken when syncParam is on AND
        // getPlayHead() downcasts to our own TransportService — AudioEngine installs one on every
        // node on every render pass, but a foreign VST/AU host's playhead won't downcast, and
        // nothing else in this app installs an AudioPlayHead today. In that case we deliberately
        // fall through to the legacy free-running clock below instead of going silent.
        // TL1-9: compiled out with the flag OFF — the syncToTransport parameter itself stays (param
        // sets are golden-pinned and preset round-trip must not depend on the flag), it's simply
        // inert: every patch runs the legacy free-running clock below regardless of its value.
        if (syncParam->get()) {
            if (auto* transport = dynamic_cast<synth::TransportService*>(getPlayHead())) {
                processSynced(*transport, buffer, midiMessages);
                return;
            }
        }
#endif

        const bool running = *runParam;
        if (!running) {
            if (lastRunState) {
                if (lastNote > 0) {
                    midiMessages.addEvent(juce::MidiMessage::noteOff(1, lastNote), 0);
                    lastNote = -1;
                }
                samplesUntilNoteOff = 0;
                lastRunState = false;
            }
            return;
        }
        lastRunState = true;

        auto samplesPerBeat = (60.0 / *bpmParam) * localSampleRate;
        auto numSamples = buffer.getNumSamples();

        samplesUntilNextBeat -= numSamples;

        // Note On Logic
        if (samplesUntilNextBeat <= 0) {

            // Update UI atomics always
            currentActiveStep = currentStep;

            // Read params for the NEW step
            int noteVal = *stepParams[currentStep];
            float gateLen = *gateParams[currentStep];
            float filterAmt = *filterEnvParams[currentStep];

            int noteDuration = (int)(samplesPerBeat * gateLen);

            // Handle Note Off for the PREVIOUS note if it's still playing
            if (lastNote > 0) {
                auto noteOff = juce::MidiMessage::noteOff(1, lastNote);
                midiMessages.addEvent(noteOff, 0);
                lastNote = -1; // Flag as off
            }

            samplesUntilNoteOff = 0; // Reset timer

            if (noteVal > 0) {
                // Send CC for Filter Env Amount (CC 74)
                auto cc = juce::MidiMessage::controllerEvent(1, 74, (int)(filterAmt * 127.0f));
                midiMessages.addEvent(cc, 0);

                // Note On
                int noteOnOffset = std::min(1, numSamples - 1);
                auto note = juce::MidiMessage::noteOn(1, noteVal, (juce::uint8)100);
                midiMessages.addEvent(note, noteOnOffset);

                // Update state
                lastNote = noteVal;
                samplesUntilNoteOff = noteDuration;
            } else {
                // Rest logic
                if (lastNote > 0) {
                    auto noteOff = juce::MidiMessage::noteOff(1, lastNote);
                    midiMessages.addEvent(noteOff, 0);
                }
                lastNote = -1;
                samplesUntilNoteOff = 0;
            }

            // Advance
            currentStep = (currentStep + 1) % 8;

            // Reset beat timer
            samplesUntilNextBeat += (int)samplesPerBeat;
        }

        // Note Off Logic (Global for the active note)
        // Only decrement if we actually have a note playing
        if (lastNote > 0 && samplesUntilNoteOff > 0) {
            samplesUntilNoteOff -= numSamples;
            if (samplesUntilNoteOff <= 0) {
                auto noteOff = juce::MidiMessage::noteOff(1, lastNote);
                midiMessages.addEvent(noteOff, 0); // Immediate
                lastNote = -1;
            }
        }
    }

    ModulationCategory getModulationCategory() const override { return ModulationCategory::Sequencer; }
    ModuleType getModuleType() const override { return ModuleType::Sequencer; }

private:
    // Locks step advance to the graph transport instead of the free-running local clock above.
    // Reached only from processBlock() once syncParam is on and the playhead is our TransportService.
    //
    // One step per beat; the step INDEX is a pure function of the beat number (beat B plays step
    // B % 8), so locates/loops never need to reconstruct "where the pattern is" — whatever beat
    // crosses next just plays its own step. BPM comes from the transport (bpmParam is ignored).
    void processSynced(synth::TransportService& transport, juce::AudioBuffer<float>& buffer,
                       juce::MidiBuffer& midiMessages) {
        const int numSamples = buffer.getNumSamples();

        // The run switch still gates everything, exactly like the legacy path above — this
        // duplicates that check (rather than sharing it) so the legacy body above stays untouched.
        const bool running = *runParam;
        if (!running) {
            if (lastRunState) {
                if (lastNote > 0) {
                    midiMessages.addEvent(juce::MidiMessage::noteOff(1, lastNote), 0);
                    lastNote = -1;
                }
                samplesUntilNoteOff = 0;
                lastRunState = false;
            }
            return;
        }
        lastRunState = true;

        const auto& info = transport.getCurrentBlockInfo();

        // Resolve a gate-length note-off scheduled by an earlier block. samplesUntilNoteOff is the
        // same field the legacy path uses, kept as "samples remaining, measured from the start of
        // the block currently being processed" — decrementing by numSamples every call and firing
        // when it crosses zero is the same idiom as the legacy path; the only difference is that we
        // emit at the REAL sample offset where it crossed zero instead of always offset 0.
        if (lastNote > 0) {
            const int samplesRemainingAtBlockStart = samplesUntilNoteOff;
            samplesUntilNoteOff -= numSamples;
            if (samplesUntilNoteOff <= 0) {
                const int offset = juce::jlimit(0, numSamples - 1, samplesRemainingAtBlockStart);
                midiMessages.addEvent(juce::MidiMessage::noteOff(1, lastNote), offset);
                lastNote = -1;
            }
        }

        if (!info.playing) {
            // All-notes-off hygiene: kill anything still held, once, then stay silent and don't
            // advance. lastFiredBeat resets too, so resuming play doesn't skip a beat it thinks it
            // "already played" before the stop.
            if (lastNote > 0) {
                midiMessages.addEvent(juce::MidiMessage::noteOff(1, lastNote), 0);
                lastNote = -1;
            }
            lastFiredBeat = kNoBeatFired;
            return;
        }

        const double beatsPerSample = info.beatsPerSample();
        if (beatsPerSample <= 0.0)
            return; // defensive: no valid tempo to lock to

        auto fireStep = [&](juce::int64 beat, int rawOffset) {
            if (beat == lastFiredBeat)
                return; // never fire the identical instant twice
            const int offset = juce::jlimit(0, numSamples - 1, rawOffset);
            const int stepIndex = (int)(((beat % 8) + 8) % 8);
            currentActiveStep = stepIndex;

            // Kill whatever's currently held so the new step starts clean, exactly like the legacy
            // path's unconditional "Handle Note Off for the PREVIOUS note" step.
            if (lastNote > 0) {
                midiMessages.addEvent(juce::MidiMessage::noteOff(1, lastNote), offset);
                lastNote = -1;
            }

            const int noteVal = *stepParams[stepIndex];
            const float gateLen = *gateParams[stepIndex];
            const float filterAmt = *filterEnvParams[stepIndex];

            if (noteVal > 0) {
                midiMessages.addEvent(juce::MidiMessage::controllerEvent(1, 74, (int)(filterAmt * 127.0f)), offset);
                midiMessages.addEvent(juce::MidiMessage::noteOn(1, noteVal, (juce::uint8)100), offset);

                const double samplesPerBeatTransport = (60.0 / info.bpm) * info.sampleRate;
                const int noteDuration = (int)std::llround(gateLen * samplesPerBeatTransport);
                const int distanceFromBlockEnd = offset + noteDuration - numSamples;

                if (distanceFromBlockEnd <= 0) {
                    // Gate expires before this same block ends — fire the matching note-off now,
                    // at its own real offset, instead of deferring to a countdown.
                    const int offOffset = juce::jlimit(0, numSamples - 1, offset + noteDuration);
                    midiMessages.addEvent(juce::MidiMessage::noteOff(1, noteVal), offOffset);
                } else {
                    lastNote = noteVal;
                    samplesUntilNoteOff = distanceFromBlockEnd;
                }
            } else {
                lastNote = -1;
            }

            lastFiredBeat = beat;
        };

        // Beats [startPpq, end1) landed in this block with no loop involved. When this block wraps
        // the loop, the pre-wrap segment only actually covers [startPpq, loopEndPpq) — see the
        // BlockTimeInfo class comment — so end1 is capped there instead of at the raw endPpq (which
        // is "where the transport would be with no loop" and can overshoot loopEndPpq).
        const double end1 = (info.loopWrapSample >= 0) ? info.loopEndPpq : info.endPpq;
        for (juce::int64 b = (juce::int64)std::ceil(info.startPpq - 1e-9); (double)b < end1; ++b) {
            const int offset = (int)std::llround(((double)b - info.startPpq) / beatsPerSample);
            fireStep(b, offset);
        }

        if (info.loopWrapSample >= 0) {
            // The wrapped remainder: beats from loopStartPpq up to the position the transport
            // actually reaches after wrapping (derived from the same fields tick() used to compute
            // the wrap, since BlockTimeInfo doesn't publish the post-wrap end directly).
            const double wrapEnd = info.loopStartPpq + (info.endPpq - info.loopEndPpq);
            for (juce::int64 b = (juce::int64)std::ceil(info.loopStartPpq - 1e-9); (double)b < wrapEnd; ++b) {
                const int offset =
                    info.loopWrapSample + (int)std::llround(((double)b - info.loopStartPpq) / beatsPerSample);
                fireStep(b, offset);
            }
        }
    }

    static constexpr juce::int64 kNoBeatFired = -1;

    double localSampleRate = 44100.0;
    int samplesUntilNextBeat = 0;
    int currentStep = 0;
    int lastNote = -1;
    int samplesUntilNoteOff = 0;
    bool lastRunState = false;
    juce::int64 lastFiredBeat = kNoBeatFired;

    juce::AudioParameterFloat* bpmParam;
    juce::AudioParameterBool* runParam;
    juce::AudioParameterBool* syncParam;
    juce::AudioParameterInt* stepParams[8];
    juce::AudioParameterFloat* gateParams[8];
    juce::AudioParameterFloat* filterEnvParams[8];
};
