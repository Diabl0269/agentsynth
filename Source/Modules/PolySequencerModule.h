#pragma once

#include "../Transport/TransportService.h"
#include "ModuleBase.h"
#include <cmath>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <vector>

class PolySequencerModule : public ModuleBase {
public:
    PolySequencerModule()
        : ModuleBase("Poly Sequencer", 0, 0) // Generates MIDI
    {
        addParameter(runParam = new juce::AudioParameterBool("run", "Run", false));
        addParameter(bpmParam = new juce::AudioParameterFloat("bpm", "BPM", 30.0f, 300.0f, 120.0f));

        // Gate Lengths
        for (int i = 0; i < 8; ++i) {
            juce::String name = "Gate " + juce::String(i + 1);
            addParameter(gateParams[i] = new juce::AudioParameterFloat(name, name, 0.1f, 1.0f, 0.5f));
        }

        // Step Parameters
        int defaultRoots[] = {48, 52, 55, 60, 48, 55, 52, 60}; // C3, E3, G3, C4...

        for (int i = 0; i < 8; ++i) {
            juce::String namePrefix = "Step " + juce::String(i + 1) + " ";

            // Root Note
            auto stringFromInt = [](int value, int) {
                return juce::MidiMessage::getMidiNoteName(value, true, true, 3);
            };
            auto valueFromText = [](const juce::String& text) { return text.getIntValue(); };

            auto attributes = juce::AudioParameterIntAttributes()
                                  .withLabel("")
                                  .withStringFromValueFunction(stringFromInt)
                                  .withValueFromStringFunction(valueFromText);

            addParameter(rootParams[i] =
                             new juce::AudioParameterInt(juce::ParameterID(namePrefix + "Root", 1), namePrefix + "Root",
                                                         0, 127, defaultRoots[i], attributes));

            // Chord Type
            juce::StringArray chords = {"Unison", "Major", "Minor", "Maj7", "Min7", "5ths", "Octs", "Random"};
            addParameter(chordParams[i] =
                             new juce::AudioParameterChoice(namePrefix + "Chord", namePrefix + "Chord", chords, 0));
        }
        // Seed random
        random.setSeed(juce::Time::currentTimeMillis());

        // Opt-in transport sync. Default OFF: added last so every existing positional
        // getParameters()[n] lookup (including this file's own tests) keeps resolving to the same
        // parameter, and every preset that predates this parameter loads with sync off.
        addParameter(syncParam = new juce::AudioParameterBool("syncToTransport", "Sync to Transport", false));
    }

    // Same self-contained design as SequencerModule: every step comes from its own parameters,
    // so processBlock never reads the incoming MIDI buffer — it only writes note/CC events onto it.
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return true; }

    // Exposed for UI
    std::atomic<int> currentActiveStep{0};

    void prepareToPlay(double sampleRate, int samplesPerBlock) override {
        localSampleRate = sampleRate;
        juce::ignoreUnused(samplesPerBlock);
        currentStep = 0;
        currentActiveStep = 0;
        samplesUntilNextBeat = 0;
        samplesUntilNoteOff = 0;
        activeNotes.clear();
        lastRunState = false;
        lastFiredBeat = kNoBeatFired;
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override {
        if (isBypassed()) {
            buffer.clear();
            return;
        }

        juce::ignoreUnused(buffer);

        // Sync-to-Transport (opt-in, default off). Only taken when syncParam is on AND
        // getPlayHead() downcasts to our own TransportService — AudioEngine installs one on every
        // node on every render pass, but a foreign VST/AU host's playhead won't downcast, and
        // nothing else in this app installs an AudioPlayHead today. In that case we deliberately
        // fall through to the legacy free-running clock below instead of going silent.
        // Compiled out with the flag OFF — the syncToTransport parameter itself stays (param
        // sets are golden-pinned and preset round-trip must not depend on the flag), it's simply
        // inert: every patch runs the legacy free-running clock below regardless of its value.
        if (syncParam->get()) {
            if (auto* transport = dynamic_cast<synth::TransportService*>(getPlayHead())) {
                processSynced(*transport, buffer, midiMessages);
                return;
            }
        }

        const bool running = *runParam;
        if (!running) {
            if (lastRunState) {
                for (int note : activeNotes)
                    midiMessages.addEvent(juce::MidiMessage::noteOff(1, note), 0);
                activeNotes.clear();
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
            // Kill previous notes
            for (int note : activeNotes) {
                auto noteOff = juce::MidiMessage::noteOff(1, note);
                midiMessages.addEvent(noteOff, 0);
            }
            activeNotes.clear();

            // Update UI
            currentActiveStep = currentStep;

            // Read params
            int root = *rootParams[currentStep];
            if (root < 24)
                root = 48; // Force C3 if state is stuck at 0

            int chordType = *chordParams[currentStep];
            float gateLen = *gateParams[currentStep];

            int noteDuration = (int)(samplesPerBeat * gateLen);
            samplesUntilNoteOff = noteDuration;

            // Generate Chord Notes
            std::vector<int> notesToPlay;
            notesToPlay.push_back(root);

            switch (chordType) {
            case 0: // Unison
                break;
            case 1: // Major (0, 4, 7)
                notesToPlay.push_back(root + 4);
                notesToPlay.push_back(root + 7);
                break;
            case 2: // Minor (0, 3, 7)
                notesToPlay.push_back(root + 3);
                notesToPlay.push_back(root + 7);
                break;
            case 3: // Maj7 (0, 4, 7, 11)
                notesToPlay.push_back(root + 4);
                notesToPlay.push_back(root + 7);
                notesToPlay.push_back(root + 11);
                break;
            case 4: // Min7 (0, 3, 7, 10)
                notesToPlay.push_back(root + 3);
                notesToPlay.push_back(root + 7);
                notesToPlay.push_back(root + 10);
                break;
            case 5: // 5ths (0, 7)
                notesToPlay.push_back(root + 7);
                break;
            case 6: // Octaves (0, 12)
                notesToPlay.push_back(root + 12);
                break;
            case 7: // Random
            {
                notesToPlay.push_back(root + random.nextInt(12));
                notesToPlay.push_back(root - random.nextInt(12));
            } break;
            }

            // Send Note Ons
            int noteOnOffset = std::min(1, numSamples - 1);
            for (int note : notesToPlay) {
                if (note >= 0 && note <= 127) {
                    auto msg = juce::MidiMessage::noteOn(1, note, (juce::uint8)100);
                    midiMessages.addEvent(msg, noteOnOffset);
                    activeNotes.push_back(note);
                }
            }

            // Advance
            currentStep = (currentStep + 1) % 8;
            samplesUntilNextBeat += (int)samplesPerBeat;
        }

        // Note Off Logic
        if (!activeNotes.empty() && samplesUntilNoteOff > 0) {
            samplesUntilNoteOff -= numSamples;
            if (samplesUntilNoteOff <= 0) {
                for (int note : activeNotes) {
                    auto noteOff = juce::MidiMessage::noteOff(1, note);
                    midiMessages.addEvent(noteOff, 0);
                }
                activeNotes.clear();
            }
        }
    }

    ModulationCategory getModulationCategory() const override { return ModulationCategory::Sequencer; }
    ModuleType getModuleType() const override { return ModuleType::PolySequencer; }

private:
    // Same chord-note construction as the legacy Note-On branch above, kept as its own copy (rather
    // than extracted into a shared call site) so the legacy processBlock body stays byte-for-byte
    // untouched. Only processSynced() calls this.
    std::vector<int> buildChordNotes(int root, int chordType) {
        std::vector<int> notesToPlay;
        notesToPlay.push_back(root);

        switch (chordType) {
        case 0: // Unison
            break;
        case 1: // Major (0, 4, 7)
            notesToPlay.push_back(root + 4);
            notesToPlay.push_back(root + 7);
            break;
        case 2: // Minor (0, 3, 7)
            notesToPlay.push_back(root + 3);
            notesToPlay.push_back(root + 7);
            break;
        case 3: // Maj7 (0, 4, 7, 11)
            notesToPlay.push_back(root + 4);
            notesToPlay.push_back(root + 7);
            notesToPlay.push_back(root + 11);
            break;
        case 4: // Min7 (0, 3, 7, 10)
            notesToPlay.push_back(root + 3);
            notesToPlay.push_back(root + 7);
            notesToPlay.push_back(root + 10);
            break;
        case 5: // 5ths (0, 7)
            notesToPlay.push_back(root + 7);
            break;
        case 6: // Octaves (0, 12)
            notesToPlay.push_back(root + 12);
            break;
        case 7: // Random
        {
            notesToPlay.push_back(root + random.nextInt(12));
            notesToPlay.push_back(root - random.nextInt(12));
        } break;
        }
        return notesToPlay;
    }

    // Locks step advance to the graph transport instead of the free-running local clock above.
    // Reached only from processBlock() once syncParam is on and the playhead is our TransportService.
    //
    // One step per beat; the step INDEX is a pure function of the beat number (beat B plays step
    // B % 8), so locates/loops never need to reconstruct "where the pattern is" — whatever beat
    // crosses next just plays its own step (chord and all). BPM comes from the transport (bpmParam
    // is ignored).
    void processSynced(synth::TransportService& transport, juce::AudioBuffer<float>& buffer,
                       juce::MidiBuffer& midiMessages) {
        const int numSamples = buffer.getNumSamples();

        // The run switch still gates everything, exactly like the legacy path above — this
        // duplicates that check (rather than sharing it) so the legacy body above stays untouched.
        const bool running = *runParam;
        if (!running) {
            if (lastRunState) {
                for (int note : activeNotes)
                    midiMessages.addEvent(juce::MidiMessage::noteOff(1, note), 0);
                activeNotes.clear();
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
        if (!activeNotes.empty()) {
            const int samplesRemainingAtBlockStart = samplesUntilNoteOff;
            samplesUntilNoteOff -= numSamples;
            if (samplesUntilNoteOff <= 0) {
                const int offset = juce::jlimit(0, numSamples - 1, samplesRemainingAtBlockStart);
                for (int note : activeNotes)
                    midiMessages.addEvent(juce::MidiMessage::noteOff(1, note), offset);
                activeNotes.clear();
            }
        }

        if (!info.playing) {
            // All-notes-off hygiene: kill anything still held, once, then stay silent and don't
            // advance. lastFiredBeat resets too, so resuming play doesn't skip a beat it thinks it
            // "already played" before the stop.
            if (!activeNotes.empty()) {
                for (int note : activeNotes)
                    midiMessages.addEvent(juce::MidiMessage::noteOff(1, note), 0);
                activeNotes.clear();
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

            // Kill whatever chord's currently held so the new step starts clean, exactly like the
            // legacy path's unconditional "Kill previous notes" step.
            for (int note : activeNotes)
                midiMessages.addEvent(juce::MidiMessage::noteOff(1, note), offset);
            activeNotes.clear();

            int root = *rootParams[stepIndex];
            if (root < 24)
                root = 48; // Force C3 if state is stuck at 0
            const int chordType = *chordParams[stepIndex];
            const float gateLen = *gateParams[stepIndex];

            const auto notesToPlay = buildChordNotes(root, chordType);
            for (int note : notesToPlay) {
                if (note >= 0 && note <= 127) {
                    midiMessages.addEvent(juce::MidiMessage::noteOn(1, note, (juce::uint8)100), offset);
                    activeNotes.push_back(note);
                }
            }

            if (!activeNotes.empty()) {
                const double samplesPerBeatTransport = (60.0 / info.bpm) * info.sampleRate;
                const int noteDuration = (int)std::llround(gateLen * samplesPerBeatTransport);
                const int distanceFromBlockEnd = offset + noteDuration - numSamples;

                if (distanceFromBlockEnd <= 0) {
                    // Gate expires before this same block ends — fire the matching note-offs now,
                    // at their own real offset, instead of deferring to a countdown.
                    const int offOffset = juce::jlimit(0, numSamples - 1, offset + noteDuration);
                    for (int note : activeNotes)
                        midiMessages.addEvent(juce::MidiMessage::noteOff(1, note), offOffset);
                    activeNotes.clear();
                } else {
                    samplesUntilNoteOff = distanceFromBlockEnd;
                }
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
    int samplesUntilNoteOff = 0;
    std::vector<int> activeNotes;
    bool lastRunState = false;
    juce::int64 lastFiredBeat = kNoBeatFired;

    juce::AudioParameterFloat* bpmParam;
    juce::AudioParameterBool* runParam;
    juce::AudioParameterBool* syncParam;
    juce::AudioParameterInt* rootParams[8];
    juce::AudioParameterChoice* chordParams[8];
    juce::AudioParameterFloat* gateParams[8];
    juce::Random random;
};
