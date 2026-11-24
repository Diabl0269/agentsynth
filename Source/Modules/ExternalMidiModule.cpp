#include "ExternalMidiModule.h"

#include "AudioEngine/RtMidiCollector.h"

ExternalMidiModule::ExternalMidiModule()
    : ModuleBase("External MIDI", 0, 0)
    , collector(std::make_unique<synth::RtMidiCollector>()) {
    addParameter(deviceIndexParam = new juce::AudioParameterInt("deviceIndex", "Device", 0, 100, 0));
    addParameter(channelParam = new juce::AudioParameterInt("channel", "Channel", 0, 16, 0)); // 0 = All
    enableVisualBuffer(true);

    // The collector's defaults (44.1 kHz) already make a processBlock() that lands before prepareToPlay()
    // safe; reset() here only stamps its clock.
    collector->reset(44100.0);
}

ExternalMidiModule::~ExternalMidiModule() = default;

void ExternalMidiModule::prepareToPlay(double sampleRate, int samplesPerBlock) {
    juce::ignoreUnused(samplesPerBlock);
    collector->reset(sampleRate);
}

void ExternalMidiModule::processModuleBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) {
    const int numSamples = buffer.getNumSamples() > 0 ? buffer.getNumSamples() : 512;

    if (isBypassed()) {
        buffer.clear();
        // Drain and discard whatever is queued so messages can't accumulate unboundedly
        // while this module sits bypassed.
        collector->discardNextBlock();
        return;
    }

    // The graph hands this module an empty buffer (it has no MIDI input). Messages are filtered by channel
    // as they are placed: channelParam is 0 = All, 1 = Ch1, 2 = Ch2, ...
    midiMessages.clear();
    collector->removeNextBlockOfMessages(midiMessages, numSamples, channelParam->get());

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
        vb->pushConstant(activity, numSamples);
    }
}

void ExternalMidiModule::pushMidiMessage(const juce::MidiMessage& msg) { collector->push(msg); }
