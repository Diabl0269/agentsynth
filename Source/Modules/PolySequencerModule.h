#pragma once

#include "ModuleBase.h"
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
    }

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
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override {
        if (isBypassed()) {
            buffer.clear();
            return;
        }

        juce::ignoreUnused(buffer);

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
    double localSampleRate = 44100.0;
    int samplesUntilNextBeat = 0;
    int currentStep = 0;
    int samplesUntilNoteOff = 0;
    std::vector<int> activeNotes;
    bool lastRunState = false;

    juce::AudioParameterFloat* bpmParam;
    juce::AudioParameterBool* runParam;
    juce::AudioParameterInt* rootParams[8];
    juce::AudioParameterChoice* chordParams[8];
    juce::AudioParameterFloat* gateParams[8];
    juce::Random random;
};
