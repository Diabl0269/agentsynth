#pragma once

#include "ModuleBase.h"
#include <juce_audio_basics/juce_audio_basics.h>

class ExternalMidiModule : public ModuleBase {
public:
    ExternalMidiModule()
        : ModuleBase("External MIDI", 0, 16) {
        addParameter(deviceIndexParam = new juce::AudioParameterInt("deviceIndex", "Device", 0, 100, 0));
        addParameter(channelParam = new juce::AudioParameterInt("channel", "Channel", 0, 16, 0)); // 0 = All
        addParameter(polyParam = new juce::AudioParameterBool("poly", "Polyphony", false));
        enableVisualBuffer(true);
    }

    ~ExternalMidiModule() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override {
        juce::ignoreUnused(sampleRate, samplesPerBlock);
        for (auto& v : voices) {
            v.note = -1;
            v.active = false;
            v.lastUsedTime = 0;
            v.currentFreq = 0.0f;
            v.smoothedGate.reset(sampleRate, 0.005); // 5ms smoothing for gates
            v.smoothedFreq.reset(sampleRate, 0.005); // 5ms smoothing for pitch
        }
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override {
        if (isBypassed()) {
            buffer.clear();
            return;
        }

        buffer.clear();

        const juce::ScopedLock sl(lock);
        // Filter messages by channel
        int targetChannel = channelParam->get();
        // targetChannel is 0=All, 1=Ch1, 2=Ch2, ...

        // Debug:
        // std::cout << "Filtering MIDI, targetChannel: " << targetChannel << std::endl;

        if (targetChannel > 0) {
            juce::MidiBuffer filtered;
            for (auto it = incomingMessages.begin(); it != incomingMessages.end(); ++it) {
                auto msg = (*it).getMessage();
                if (msg.getChannel() == targetChannel) {
                    filtered.addEvent(msg, (*it).samplePosition);
                }
            }
            midiMessages = filtered;
        } else {
            midiMessages = incomingMessages;
        }

        int numSamples = buffer.getNumSamples();
        int currentSample = 0;
        int activeVoicesMax = polyParam->get() ? MAX_VOICES : 1;

        for (const auto metadata : midiMessages) {
            auto msg = metadata.getMessage();
            int triggerSample = msg.getTimeStamp();
            triggerSample = juce::jlimit(0, numSamples - 1, triggerSample);

            if (triggerSample > currentSample) {
                renderChunk(buffer, currentSample, triggerSample, activeVoicesMax);
                currentSample = triggerSample;
            }

            if (msg.isNoteOn()) {
                notesHeld++;
                handleNoteOn(msg.getNoteNumber(), msg.getFloatVelocity(), activeVoicesMax);
            } else if (msg.isNoteOff()) {
                if (notesHeld > 0) notesHeld--;
                handleNoteOff(msg.getNoteNumber());
            } else if (msg.isAllNotesOff() || msg.isAllSoundOff()) {
                notesHeld = 0;
                allNotesOff();
            }
        }

        if (currentSample < numSamples) {
            renderChunk(buffer, currentSample, numSamples, activeVoicesMax);
        }

        // Push activity to visual buffer for the activity LED
        if (auto* vb = getVisualBuffer()) {
            float activity = notesHeld > 0 ? 1.0f : 0.0f;
            for (const auto metadata : midiMessages) {
                auto msg = metadata.getMessage();
                if (!msg.isActiveSense() && !msg.isMidiClock()) {
                    activity = 1.0f;
                }
            }
            int samplesToPush = numSamples > 0 ? numSamples : 512;
            for (int i = 0; i < samplesToPush; ++i) {
                vb->pushSample(activity);
            }
        }

        incomingMessages.clear();
    }

    void pushMidiMessage(const juce::MidiMessage& msg) {
        const juce::ScopedLock sl(lock);
        incomingMessages.addEvent(msg, 0);
    }

    void setMidiDeviceName(const juce::String& newName) { setModuleName(newName); }

    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return true; }

    ModuleType getModuleType() const override { return ModuleType::ExternalMidi; }
    int getVisibleOutputPortCount() const override { return 1; }
    juce::String getOutputPortLabel(int) const override { return "CV Out"; }

private:
    static constexpr int MAX_VOICES = 8;

    struct Voice {
        int note = -1;
        bool active = false;
        juce::int64 lastUsedTime = 0;
        float currentFreq = 0.0f;
        juce::SmoothedValue<float> smoothedGate;
        juce::SmoothedValue<float> smoothedFreq;
    };
    Voice voices[MAX_VOICES];

    void renderChunk(juce::AudioBuffer<float>& buffer, int startSample, int endSample, int maxVoices) {
        if (endSample <= startSample || buffer.getNumChannels() < 16)
            return;

        for (int i = 0; i < maxVoices; ++i) {
            float* pitchCh = buffer.getWritePointer(i);
            float* gateCh = buffer.getWritePointer(i + 8);

            voices[i].smoothedFreq.setTargetValue(voices[i].currentFreq);
            voices[i].smoothedGate.setTargetValue(voices[i].active ? 1.0f : 0.0f);

            for (int s = startSample; s < endSample; ++s) {
                pitchCh[s] = voices[i].smoothedFreq.getNextValue();
                gateCh[s] = voices[i].smoothedGate.getNextValue();
            }
        }
    }

    void handleNoteOn(int note, float velocity, int maxVoices) {
        juce::ignoreUnused(velocity);
        juce::int64 now = juce::Time::getMillisecondCounter();
        float freq = juce::MidiMessage::getMidiNoteInHertz(note);

        for (int i = 0; i < maxVoices; ++i) {
            if (voices[i].active && voices[i].note == note) {
                voices[i].lastUsedTime = now;
                voices[i].currentFreq = freq;
                return;
            }
        }

        for (int i = 0; i < maxVoices; ++i) {
            if (!voices[i].active) {
                voices[i].note = note;
                voices[i].active = true;
                voices[i].lastUsedTime = now;
                voices[i].currentFreq = freq;
                return;
            }
        }

        int oldestIdx = findOldestVoiceIndex(maxVoices);
        voices[oldestIdx].note = note;
        voices[oldestIdx].active = true;
        voices[oldestIdx].lastUsedTime = now;
        voices[oldestIdx].currentFreq = freq;
    }

    void handleNoteOff(int note) {
        for (int i = 0; i < MAX_VOICES; ++i) {
            if (voices[i].active && voices[i].note == note) {
                voices[i].active = false;
            }
        }
    }

    void allNotesOff() {
        for (int i = 0; i < MAX_VOICES; ++i) {
            voices[i].active = false;
            voices[i].note = -1;
        }
    }

    int findOldestVoiceIndex(int maxVoices) const {
        int oldestIdx = 0;
        juce::int64 oldestTime = voices[0].lastUsedTime;

        for (int i = 1; i < maxVoices; ++i) {
            if (voices[i].lastUsedTime < oldestTime) {
                oldestTime = voices[i].lastUsedTime;
                oldestIdx = i;
            }
        }

        return oldestIdx;
    }

    juce::AudioParameterInt* deviceIndexParam;
    juce::AudioParameterInt* channelParam;
    juce::AudioParameterBool* polyParam;
    juce::MidiBuffer incomingMessages;
    juce::CriticalSection lock;
    int notesHeld = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ExternalMidiModule)
};
