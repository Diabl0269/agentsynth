#include "AudioEngine.h"
#include "Modules/ADSRModule.h"
#include "Modules/AttenuverterModule.h"
#include "Modules/ExternalMidiModule.h"
#include "Modules/FX/DelayModule.h"
#include "Modules/FX/DistortionModule.h"
#include "Modules/FX/ReverbModule.h"
#include "Modules/FilterModule.h"
#include "Modules/LFOModule.h"
#include "Modules/MidiKeyboardModule.h"
#include "Modules/OscillatorModule.h"
#include "Modules/SequencerModule.h"
#include "Modules/VCAModule.h"
#include "PresetManager.h"
#include <map>

AudioEngine::AudioEngine() {}

AudioEngine::~AudioEngine() { shutdown(); }

void AudioEngine::initialise() {
    deviceManager.initialiseWithDefaultDevices(0, 2);
    deviceManager.addAudioCallback(this);

    // Initialise MIDI input collector
    midiMessageCollector.reset(deviceManager.getAudioDeviceSetup().sampleRate);

    // Enable all available MIDI inputs by default
    for (auto& info : juce::MidiInput::getAvailableDevices()) {
        juce::Logger::writeToLog("Attempting to open MIDI input: " + info.name);
        auto input = juce::MidiInput::createNewDevice(info.name, this);
        if (input != nullptr) {
            input->start();
            midiInputs.push_back(std::move(input));
            juce::Logger::writeToLog("Successfully opened MIDI input: " + info.name);
        } else {
            juce::Logger::writeToLog("Failed to open MIDI input: " + info.name);
        }
    }

    if (!gsynth::PresetManager::loadDefaultPreset(mainProcessorGraph)) {
        createDefaultPatch(); // Fallback
    }
}

void AudioEngine::shutdown() {
    deviceManager.removeAudioCallback(this);
#if JUCE_LINUX || JUCE_BSD || JUCE_MAC || JUCE_IOS
    for (auto& input : midiInputs) {
        input->stop();
    }
    midiInputs.clear();
#endif
    mainProcessorGraph.clear();
}

std::vector<AudioEngine::ModRoutingInfo> AudioEngine::getActiveModRoutings() const {
    std::vector<ModRoutingInfo> routings;
    for (auto* node : mainProcessorGraph.getNodes()) {
        if (dynamic_cast<AttenuverterModule*>(node->getProcessor()) != nullptr) {
            ModRoutingInfo info;
            info.attenuverterNodeID = node->nodeID;
            info.sourceChannelIndex = 0;
            for (auto& conn : mainProcessorGraph.getConnections()) {
                if (conn.destination.nodeID == node->nodeID && conn.destination.channelIndex == 0) {
                    info.sourceNodeID = conn.source.nodeID;
                    info.sourceChannelIndex = conn.source.channelIndex;
                    break;
                }
            }
            for (auto& conn : mainProcessorGraph.getConnections()) {
                if (conn.source.nodeID == node->nodeID && conn.source.channelIndex == 0) {
                    info.destNodeID = conn.destination.nodeID;
                    info.destChannelIndex = conn.destination.channelIndex;
                    break;
                }
            }

            // Get bypass state
            info.isBypassed = false;
            if (auto* module = dynamic_cast<ModuleBase*>(node->getProcessor())) {
                info.isBypassed = module->isBypassed();
            }

            routings.push_back(info);
        }
    }
    return routings;
}

std::vector<AudioEngine::ModulationDisplayInfo> AudioEngine::getModulationDisplayInfo() const {
    std::vector<ModulationDisplayInfo> result;
    for (auto* node : mainProcessorGraph.getNodes()) {
        if (auto* atten = dynamic_cast<AttenuverterModule*>(node->getProcessor())) {
            ModulationDisplayInfo info;
            info.attenuverterNodeID = node->nodeID;
            info.modSignalValue = atten->getLastModValue();
            info.modSignalPeak = atten->getLastOutputPeak();
            info.isBypassed = false;
            if (auto* bp = dynamic_cast<juce::AudioParameterBool*>(node->getProcessor()->getParameters()[2]))
                info.isBypassed = bp->get();

            bool foundDest = false;
            for (auto& conn : mainProcessorGraph.getConnections()) {
                if (conn.source.nodeID == node->nodeID && conn.source.channelIndex == 0) {
                    info.destNodeID = conn.destination.nodeID;
                    info.destChannelIndex = conn.destination.channelIndex;
                    foundDest = true;
                    break;
                }
            }
            if (foundDest)
                result.push_back(info);
        }
    }
    return result;
}

void AudioEngine::addModRouting(juce::AudioProcessorGraph::NodeID sourceNodeID, int sourceChannelIndex,
                                juce::AudioProcessorGraph::NodeID destNodeID, int destChannelIndex) {
    auto attenuverterNode = mainProcessorGraph.addNode(std::make_unique<AttenuverterModule>());
    if (attenuverterNode == nullptr)
        return;
    if (auto* param = dynamic_cast<juce::AudioParameterFloat*>(attenuverterNode->getProcessor()->getParameters()[1]))
        param->setValueNotifyingHost(param->convertTo0to1(1.0f));
    mainProcessorGraph.addConnection({{sourceNodeID, sourceChannelIndex}, {attenuverterNode->nodeID, 0}});
    mainProcessorGraph.addConnection({{attenuverterNode->nodeID, 0}, {destNodeID, destChannelIndex}});
}

void AudioEngine::addEmptyModRouting() { mainProcessorGraph.addNode(std::make_unique<AttenuverterModule>()); }

void AudioEngine::removeModRouting(juce::AudioProcessorGraph::NodeID attenuverterNodeID) {
    mainProcessorGraph.removeNode(attenuverterNodeID);
}

void AudioEngine::toggleModBypass(juce::AudioProcessorGraph::NodeID attenuverterNodeID) {
    if (auto* node = mainProcessorGraph.getNodeForId(attenuverterNodeID)) {
        if (auto* module = dynamic_cast<ModuleBase*>(node->getProcessor())) {
            module->setBypassed(!module->isBypassed());
        }
    }
}

bool AudioEngine::isModBypassed(juce::AudioProcessorGraph::NodeID attenuverterNodeID) const {
    if (auto* node = mainProcessorGraph.getNodeForId(attenuverterNodeID)) {
        if (auto* module = dynamic_cast<ModuleBase*>(node->getProcessor())) {
            return module->isBypassed();
        }
    }
    return false;
}

void AudioEngine::updateModuleNames() {
    std::map<juce::String, int> typeCounts;
    for (auto* node : mainProcessorGraph.getNodes()) {
        if (auto* module = dynamic_cast<ModuleBase*>(node->getProcessor())) {
            juce::String baseName = module->getName();
            int lastSpace = baseName.lastIndexOf(" ");
            if (lastSpace != -1 && baseName.substring(lastSpace + 1).containsOnly("0123456789"))
                baseName = baseName.substring(0, lastSpace);
            if (baseName.startsWith("Attenuverter"))
                baseName = "Mod Slot";
            int index = ++typeCounts[baseName];
            module->setModuleName(baseName + " " + juce::String(index));
        }
    }
}

void AudioEngine::createDefaultPatch() {
    mainProcessorGraph.clear();
    using AudioGraphIOProcessor = juce::AudioProcessorGraph::AudioGraphIOProcessor;
    auto inputNode =
        mainProcessorGraph.addNode(std::make_unique<AudioGraphIOProcessor>(AudioGraphIOProcessor::audioInputNode));
    auto outputNode =
        mainProcessorGraph.addNode(std::make_unique<AudioGraphIOProcessor>(AudioGraphIOProcessor::audioOutputNode));

    auto sequencerNode = mainProcessorGraph.addNode(std::make_unique<SequencerModule>());
    auto oscillatorNode = mainProcessorGraph.addNode(std::make_unique<OscillatorModule>());
    auto filterNode = mainProcessorGraph.addNode(std::make_unique<FilterModule>());
    auto vcaNode = mainProcessorGraph.addNode(std::make_unique<VCAModule>());
    auto adsrNode = mainProcessorGraph.addNode(std::make_unique<ADSRModule>("Amp Env"));
    auto filterAdsrNode = mainProcessorGraph.addNode(std::make_unique<ADSRModule>("Filter Env"));
    auto lfoNode = mainProcessorGraph.addNode(std::make_unique<LFOModule>());
    auto distortionNode = mainProcessorGraph.addNode(std::make_unique<DistortionModule>());
    auto delayNode = mainProcessorGraph.addNode(std::make_unique<DelayModule>());
    auto reverbNode = mainProcessorGraph.addNode(std::make_unique<ReverbModule>());

    inputNode->properties.set("x", 10.0f);
    inputNode->properties.set("y", 10.0f);
    sequencerNode->properties.set("x", 10.0f);
    sequencerNode->properties.set("y", 80.0f);
    oscillatorNode->properties.set("x", 540.0f);
    oscillatorNode->properties.set("y", 50.0f);
    filterNode->properties.set("x", 830.0f);
    filterNode->properties.set("y", 50.0f);
    vcaNode->properties.set("x", 1120.0f);
    vcaNode->properties.set("y", 50.0f);
    adsrNode->properties.set("x", 540.0f);
    adsrNode->properties.set("y", 450.0f);
    filterAdsrNode->properties.set("x", 830.0f);
    filterAdsrNode->properties.set("y", 450.0f);
    lfoNode->properties.set("x", 10.0f);
    lfoNode->properties.set("y", 500.0f);
    distortionNode->properties.set("x", 1410.0f);
    distortionNode->properties.set("y", 50.0f);
    delayNode->properties.set("x", 1690.0f);
    delayNode->properties.set("y", 50.0f);
    reverbNode->properties.set("x", 1970.0f);
    reverbNode->properties.set("y", 50.0f);
    outputNode->properties.set("x", 2250.0f);
    outputNode->properties.set("y", 300.0f);

    addModRouting(adsrNode->nodeID, 0, vcaNode->nodeID, 1);
    addModRouting(filterAdsrNode->nodeID, 0, filterNode->nodeID, 1);
    for (int i = 0; i < 4; ++i)
        addEmptyModRouting();

    mainProcessorGraph.addConnection({{sequencerNode->nodeID, juce::AudioProcessorGraph::midiChannelIndex},
                                      {oscillatorNode->nodeID, juce::AudioProcessorGraph::midiChannelIndex}});
    mainProcessorGraph.addConnection({{sequencerNode->nodeID, juce::AudioProcessorGraph::midiChannelIndex},
                                      {adsrNode->nodeID, juce::AudioProcessorGraph::midiChannelIndex}});
    mainProcessorGraph.addConnection({{sequencerNode->nodeID, juce::AudioProcessorGraph::midiChannelIndex},
                                      {filterAdsrNode->nodeID, juce::AudioProcessorGraph::midiChannelIndex}});
    mainProcessorGraph.addConnection({{oscillatorNode->nodeID, 0}, {filterNode->nodeID, 0}});
    mainProcessorGraph.addConnection({{filterNode->nodeID, 0}, {vcaNode->nodeID, 0}});
    mainProcessorGraph.addConnection({{sequencerNode->nodeID, juce::AudioProcessorGraph::midiChannelIndex},
                                      {filterNode->nodeID, juce::AudioProcessorGraph::midiChannelIndex}});
    mainProcessorGraph.addConnection({{vcaNode->nodeID, 0}, {distortionNode->nodeID, 0}});
    mainProcessorGraph.addConnection({{vcaNode->nodeID, 0}, {distortionNode->nodeID, 1}});
    mainProcessorGraph.addConnection({{distortionNode->nodeID, 0}, {delayNode->nodeID, 0}});
    mainProcessorGraph.addConnection({{distortionNode->nodeID, 1}, {delayNode->nodeID, 1}});
    mainProcessorGraph.addConnection({{delayNode->nodeID, 0}, {reverbNode->nodeID, 0}});
    mainProcessorGraph.addConnection({{delayNode->nodeID, 1}, {reverbNode->nodeID, 1}});
    mainProcessorGraph.addConnection({{reverbNode->nodeID, 0}, {outputNode->nodeID, 0}});
    mainProcessorGraph.addConnection({{reverbNode->nodeID, 1}, {outputNode->nodeID, 1}});
}

void AudioEngine::handleIncomingMidiMessage(juce::MidiInput* source, const juce::MidiMessage& message) {
    std::cout << "MIDI Received from: " << source->getName().toStdString()
              << " message: " << message.getDescription().toStdString() << std::endl;
    bool routed = false;
    for (auto* node : mainProcessorGraph.getNodes()) {
        if (auto* extMidi = dynamic_cast<ExternalMidiModule*>(node->getProcessor())) {
            std::cout << "Checking against ExternalMidiModule: " << extMidi->getName().toStdString() << std::endl;
            if (source->getName() == extMidi->getName()) {
                std::cout << "Routing to ExternalMidiModule: " << extMidi->getName().toStdString() << std::endl;
                extMidi->pushMidiMessage(message);
                routed = true;
            }
        }
    }
    if (!routed) {
        std::cout << "No ExternalMidiModule found for device: " << source->getName().toStdString() << std::endl;
    }
    midiMessageCollector.addMessageToQueue(message);
}

void AudioEngine::audioDeviceIOCallbackWithContext(const float* const* inputChannelData, int numInputChannels,
                                                   float* const* outputChannelData, int numOutputChannels,
                                                   int numSamples, const juce::AudioIODeviceCallbackContext& context) {
    juce::ignoreUnused(inputChannelData, numInputChannels, context);
    for (int i = 0; i < numOutputChannels; ++i) {
        if (outputChannelData[i])
            std::fill(outputChannelData[i], outputChannelData[i] + numSamples, 0.0f);
    }
    juce::AudioBuffer<float> buffer(const_cast<float**>(outputChannelData), numOutputChannels, numSamples);
    juce::MidiBuffer midiMessages;
    midiMessageCollector.removeNextBlockOfMessages(midiMessages, numSamples);

    // Collect MIDI from active ExternalMidiModules
    for (auto* node : mainProcessorGraph.getNodes()) {
        if (auto* extMidi = dynamic_cast<ExternalMidiModule*>(node->getProcessor())) {
            juce::MidiBuffer extMidiMessages;
            // We shouldn't call processBlock on the module directly as the graph does that.
            // The module's internal buffer is populated by handleIncomingMidiMessage.
            // Let's pass an empty buffer to processBlock to just collect the MIDI.
            juce::AudioBuffer<float> silentBuffer(numOutputChannels, numSamples);
            silentBuffer.clear();
            extMidi->processBlock(silentBuffer, extMidiMessages);
            midiMessages.addEvents(extMidiMessages, 0, numSamples, 0);
        }
    }

    mainProcessorGraph.processBlock(buffer, midiMessages);
}

void AudioEngine::audioDeviceAboutToStart(juce::AudioIODevice* device) {
    if (device) {
        mainProcessorGraph.setPlayConfigDetails(device->getActiveInputChannels().countNumberOfSetBits(),
                                                device->getActiveOutputChannels().countNumberOfSetBits(),
                                                device->getCurrentSampleRate(), device->getCurrentBufferSizeSamples());
        mainProcessorGraph.prepareToPlay(device->getCurrentSampleRate(), device->getCurrentBufferSizeSamples());
    }
}

void AudioEngine::audioDeviceStopped() { mainProcessorGraph.releaseResources(); }
