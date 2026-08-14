#pragma once

#include "ModuleBase.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>

class ExternalMidiModule : public ModuleBase {
public:
    ExternalMidiModule()
        : ModuleBase("External MIDI", 0, 0) {
        addParameter(deviceIndexParam = new juce::AudioParameterInt("deviceIndex", "Device", 0, 100, 0));
        addParameter(channelParam = new juce::AudioParameterInt("channel", "Channel", 0, 16, 0)); // 0 = All
        enableVisualBuffer(true);

        // juce::MidiMessageCollector jasserts if addMessageToQueue()/removeNextBlockOfMessages()
        // are used before reset() has established a sample rate. Give it a sane default here so
        // a processBlock() call that lands before prepareToPlay() can't hit that assertion.
        collector.reset(44100.0);
    }

    ~ExternalMidiModule() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override {
        juce::ignoreUnused(samplesPerBlock);
        collector.reset(sampleRate);
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override {
        const int numSamples = buffer.getNumSamples() > 0 ? buffer.getNumSamples() : 512;

        if (isBypassed()) {
            buffer.clear();
            // Drain and discard whatever is queued so messages can't accumulate unboundedly
            // while this module sits bypassed (the collector's internal queue has no cap, and
            // the old hand-rolled buffer had the same latent leak — fixed here too).
            scratchMidi.clear();
            collector.removeNextBlockOfMessages(scratchMidi, numSamples);
            return;
        }

        // Filter messages by channel
        int targetChannel = channelParam->get();
        // targetChannel is 0=All, 1=Ch1, 2=Ch2, ...

        scratchMidi.clear();
        collector.removeNextBlockOfMessages(scratchMidi, numSamples);

        if (targetChannel > 0) {
            juce::MidiBuffer filtered;
            for (auto it = scratchMidi.begin(); it != scratchMidi.end(); ++it) {
                auto msg = (*it).getMessage();
                if (msg.getChannel() == targetChannel) {
                    filtered.addEvent(msg, (*it).samplePosition);
                }
            }
            midiMessages = filtered;
        } else {
            midiMessages = scratchMidi;
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
            for (int i = 0; i < numSamples; ++i) {
                vb->pushSample(activity);
            }
        }
    }

    void pushMidiMessage(const juce::MidiMessage& msg) { collector.addMessageToQueue(msg); }

    void setMidiDeviceName(const juce::String& newName) { setModuleName(newName); }

    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return true; }

    ModuleType getModuleType() const override { return ModuleType::ExternalMidi; }

private:
    juce::AudioParameterInt* deviceIndexParam;
    juce::AudioParameterInt* channelParam;
    juce::MidiMessageCollector collector;
    juce::MidiBuffer scratchMidi; // reused every block to avoid per-block allocation
    int notesHeld = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ExternalMidiModule)
};
