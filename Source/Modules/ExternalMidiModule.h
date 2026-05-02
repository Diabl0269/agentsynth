#pragma once

#include "ModuleBase.h"
#include <juce_audio_basics/juce_audio_basics.h>

class ExternalMidiModule : public ModuleBase {
public:
    ExternalMidiModule()
        : ModuleBase("External MIDI", 0, 0) {
        addParameter(deviceIndexParam = new juce::AudioParameterInt("deviceIndex", "Device", 0, 100, 0));
        addParameter(channelParam = new juce::AudioParameterInt("channel", "Channel", 0, 16, 0)); // 0 = All
        enableVisualBuffer(true);
    }

    ~ExternalMidiModule() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override {
        juce::ignoreUnused(sampleRate, samplesPerBlock);
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override {
        if (isBypassed()) {
            buffer.clear();
            return;
        }

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

        // Push activity to visual buffer for the activity LED
        if (auto* vb = getVisualBuffer()) {
            float activity = notesHeld > 0 ? 1.0f : 0.0f;
            for (const auto metadata : midiMessages) {
                auto msg = metadata.getMessage();
                
                if (msg.isNoteOn()) {
                    notesHeld++;
                } else if (msg.isNoteOff() && notesHeld > 0) {
                    notesHeld--;
                } else if (msg.isAllNotesOff() || msg.isAllSoundOff()) {
                    notesHeld = 0;
                }

                if (!msg.isActiveSense() && !msg.isMidiClock()) {
                    activity = 1.0f;
                }
            }
            int numSamples = buffer.getNumSamples() > 0 ? buffer.getNumSamples() : 512;
            for (int i = 0; i < numSamples; ++i) {
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

private:
    juce::AudioParameterInt* deviceIndexParam;
    juce::AudioParameterInt* channelParam;
    juce::MidiBuffer incomingMessages;
    juce::CriticalSection lock;
    int notesHeld = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ExternalMidiModule)
};
