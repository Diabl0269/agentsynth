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
#include "Modules/PolyMidiModule.h"
#include "Modules/SequencerModule.h"
#include "Modules/VCAModule.h"
#include "PresetManager.h"
#include <bit>
#include <map>
#include <set>

AudioEngine::AudioEngine(HostMode mode)
    : hostMode_(mode) {}

AudioEngine::~AudioEngine() { shutdown(); }

void AudioEngine::initialise() {
    // Hosted mode: the plugin wrapper owns the audio clock and forwards the host's MIDI, so we
    // skip device/MIDI acquisition entirely and only build the initial patch. prepareForHost()
    // supplies the real sample rate/channel count later, since there is no device to ask.
    if (!isHosted()) {
        deviceManager.initialiseWithDefaultDevices(0, 2);
        deviceManager.addAudioCallback(this);

        // Initialise MIDI input collector
        midiMessageCollector.reset(deviceManager.getAudioDeviceSetup().sampleRate);

        // Enable all available MIDI inputs by default
        for (auto& info : juce::MidiInput::getAvailableDevices()) {
            auto input = juce::MidiInput::openDevice(info.identifier, this);
            if (input != nullptr) {
                input->start();
                midiInputs.push_back(std::move(input));
            }
        }
    } else {
        // A default-constructed AudioProcessorGraph reports 0 output channels until something
        // sets its channel layout. The graph's "Audio Output" IO node snapshots that count once,
        // the moment it's added below (AudioGraphIOProcessor::setParentGraph) — so any patch
        // connection into it made before the host's first prepareToPlay() would otherwise be
        // rejected as out-of-range and silently dropped. Standalone gets this for free from
        // audioDeviceAboutToStart(), which always runs before the patch is built; mirror it here
        // with the same placeholder (0 in / 2 out) the plugin's BusesProperties declares.
        // prepareForHost() reconciles this with the host's real layout before playback starts.
        mainProcessorGraph.setPlayConfigDetails(0, 2, 44100.0, 512);
    }

    if (!synth::PresetManager::loadDefaultPreset(mainProcessorGraph)) {
        createDefaultPatch(); // Fallback
    }
}

void AudioEngine::shutdown() {
    if (!isHosted()) {
        deviceManager.removeAudioCallback(this);
#if JUCE_LINUX || JUCE_BSD || JUCE_MAC || JUCE_IOS
        for (auto& input : midiInputs) {
            input->stop();
        }
        midiInputs.clear();
#endif
    }
    mainProcessorGraph.clear();
}

void AudioEngine::ensureMidiDeviceOpen(const juce::String& deviceName) {
    // Hosted mode never opens hardware MIDI itself — the host owns device routing and forwards
    // note data through processBlock. Grabbing the port here would double-trigger every note.
    if (isHosted())
        return;

    for (auto& input : midiInputs) {
        if (input->getName() == deviceName) {
            return; // Already open
        }
    }

    for (auto& info : juce::MidiInput::getAvailableDevices()) {
        if (info.name == deviceName) {
            auto input = juce::MidiInput::openDevice(info.identifier, this);
            if (input != nullptr) {
                input->start();
                midiInputs.push_back(std::move(input));
            }
            break;
        }
    }
}

std::vector<AudioEngine::ModulationRouting> AudioEngine::getModulationRoutings() const {
    std::vector<ModulationRouting> routings;

    // --- Pass 1: AttenuverterChain routings (unchanged) ---
    for (auto* node : mainProcessorGraph.getNodes()) {
        if (auto* atten = dynamic_cast<AttenuverterModule*>(node->getProcessor())) {
            ModulationRouting r;
            r.kind = RoutingKind::AttenuverterChain;
            r.attenuverterNodeID = node->nodeID;
            r.voiceCount = 1;
            r.role = PortRole::ModCV;

            // Find source: first connection whose destination is (node, channel 0)
            for (auto& conn : mainProcessorGraph.getConnections()) {
                if (conn.destination.nodeID == node->nodeID && conn.destination.channelIndex == 0) {
                    r.sourceNodeID = conn.source.nodeID;
                    r.sourceChannelIndex = conn.source.channelIndex;
                    r.hasSource = true;
                    break;
                }
            }

            // Find dest: first connection whose source is (node, channel 0)
            for (auto& conn : mainProcessorGraph.getConnections()) {
                if (conn.source.nodeID == node->nodeID && conn.source.channelIndex == 0) {
                    r.destNodeID = conn.destination.nodeID;
                    r.destChannelIndex = conn.destination.channelIndex;
                    r.hasDest = true;
                    break;
                }
            }

            // Bypass state
            if (auto* module = dynamic_cast<ModuleBase*>(node->getProcessor()))
                r.isBypassed = module->isBypassed();

            // Signal visualization values
            r.modSignalValue = atten->getLastModValue();
            r.modSignalPeak = atten->getLastOutputPeak();

            // sourceVisibleJack / destVisibleJack: left as 0 (not consumed yet)

            routings.push_back(r);
        }
    }

    // --- Pass 2: DirectCV / PolyBus routings ---
    // Helper: read a peak value from a VisualBuffer by scanning its contents
    auto readVisualPeak = [](VisualBuffer* vb) -> float {
        if (!vb)
            return 0.0f;
        std::vector<float> buf(static_cast<size_t>(vb->getSize()));
        vb->copyTo(buf);
        float peak = 0.0f;
        for (float s : buf)
            peak = std::max(peak, std::abs(s));
        return peak;
    };

    // Collect candidate direct edges: connections where neither endpoint is an AttenuverterModule
    // and the destination channel maps to role == ModCV on the dest module.
    struct CandidateEdge {
        juce::AudioProcessorGraph::NodeID srcNodeID;
        int srcChannel;
        juce::AudioProcessorGraph::NodeID dstNodeID;
        int dstChannel;
    };
    std::vector<CandidateEdge> candidates;

    for (auto& conn : mainProcessorGraph.getConnections()) {
        if (conn.source.isMIDI())
            continue;

        auto* srcNode = mainProcessorGraph.getNodeForId(conn.source.nodeID);
        auto* dstNode = mainProcessorGraph.getNodeForId(conn.destination.nodeID);
        if (!srcNode || !dstNode)
            continue;

        // Skip if either endpoint is an attenuverter
        if (dynamic_cast<AttenuverterModule*>(srcNode->getProcessor()))
            continue;
        if (dynamic_cast<AttenuverterModule*>(dstNode->getProcessor()))
            continue;

        auto* dstModule = dynamic_cast<ModuleBase*>(dstNode->getProcessor());
        if (!dstModule)
            continue;

        LogicalPort dstPort = dstModule->mapInputChannel(conn.destination.channelIndex);
        // Accept ModCV (parameter modulation), Pitch (poly pitch fan), and Gate (poly gate fan).
        // Exclude Audio (osc->filter / filter->VCA audio fans) and Other/Midi.
        if (dstPort.role != PortRole::ModCV && dstPort.role != PortRole::Pitch && dstPort.role != PortRole::Gate)
            continue;

        candidates.push_back(
            {conn.source.nodeID, conn.source.channelIndex, conn.destination.nodeID, conn.destination.channelIndex});
    }

    // Group candidates by (srcNodeID, dstNodeID)
    using NodeIDPair = std::pair<juce::AudioProcessorGraph::NodeID, juce::AudioProcessorGraph::NodeID>;
    std::map<NodeIDPair, std::vector<CandidateEdge>> groups;
    for (auto& e : candidates) {
        groups[{e.srcNodeID, e.dstNodeID}].push_back(e);
    }

    for (auto& [pair, edges] : groups) {
        auto [srcNodeID, dstNodeID] = pair;
        auto* srcNode = mainProcessorGraph.getNodeForId(srcNodeID);
        auto* dstNode = mainProcessorGraph.getNodeForId(dstNodeID);
        if (!srcNode || !dstNode)
            continue;

        auto* srcModule = dynamic_cast<ModuleBase*>(srcNode->getProcessor());
        auto* dstModule = dynamic_cast<ModuleBase*>(dstNode->getProcessor());
        if (!srcModule || !dstModule)
            continue;

        // Build a quick set of (srcCh, dstCh) edges in this group for collapse check
        std::set<std::pair<int, int>> edgeSet;
        for (auto& e : edges)
            edgeSet.insert({e.srcChannel, e.dstChannel});

        // Track which edges have been consumed by poly-bus collapse
        std::set<std::pair<int, int>> consumed;

        // Attempt poly-bus collapse: find source head channels
        for (auto& e : edges) {
            if (consumed.count({e.srcChannel, e.dstChannel}))
                continue;

            LogicalPort srcPort = srcModule->mapOutputChannel(e.srcChannel);
            LogicalPort dstPort = dstModule->mapInputChannel(e.dstChannel);

            if (srcPort.isPolyGroupHead && dstPort.isPolyGroupHead) {
                int Ns = srcPort.polyVoiceSpan;
                int Nd = dstPort.polyVoiceSpan;

                // A mono modulator broadcast across a per-voice mod-CV fan leaves one source channel
                // for all N destinations, so its edges are (Hs, Hd+i) rather than (Hs+i, Hd+i).
                // Collapse it too, otherwise it renders as N wires stacked on the same two jacks.
                const bool isBroadcast = (Ns == 1 && Nd > 1 && dstPort.role == PortRole::ModCV);
                const int srcStride = isBroadcast ? 0 : 1;

                if (isBroadcast || (Ns == Nd && Ns > 1)) {
                    int N = isBroadcast ? Nd : Ns;
                    int Hs = e.srcChannel;
                    int Hd = e.dstChannel;
                    // Check all N edges (Hs + i*srcStride, Hd+i) are present
                    bool complete = true;
                    for (int i = 0; i < N; ++i) {
                        if (!edgeSet.count({Hs + i * srcStride, Hd + i})) {
                            complete = false;
                            break;
                        }
                    }
                    if (complete) {
                        // Emit one PolyBus routing and consume all N edges
                        ModulationRouting r;
                        r.kind = RoutingKind::PolyBus;
                        r.sourceNodeID = srcNodeID;
                        r.sourceChannelIndex = Hs;
                        r.sourceVisibleJack = srcPort.visibleJackIndex;
                        r.destNodeID = dstNodeID;
                        r.destChannelIndex = Hd;
                        r.destVisibleJack = dstPort.visibleJackIndex;
                        r.voiceCount = N;
                        r.hasSource = true;
                        r.hasDest = true;
                        r.amount = 1.0f;
                        r.isBypassed = false;
                        r.role = dstPort.role; // Pitch, Gate, or ModCV depending on dest fan type
                        float peak = readVisualPeak(srcModule->getVisualBuffer());
                        r.modSignalPeak = peak;
                        r.modSignalValue = peak;
                        routings.push_back(r);
                        for (int i = 0; i < N; ++i)
                            consumed.insert({Hs + i * srcStride, Hd + i});
                    }
                }
            }
        }

        // Remaining (non-collapsed) edges become DirectCV routings
        for (auto& e : edges) {
            if (consumed.count({e.srcChannel, e.dstChannel}))
                continue;

            LogicalPort srcPort = srcModule->mapOutputChannel(e.srcChannel);
            LogicalPort dstPort = dstModule->mapInputChannel(e.dstChannel);

            ModulationRouting r;
            r.kind = RoutingKind::DirectCV;
            r.sourceNodeID = srcNodeID;
            r.sourceChannelIndex = e.srcChannel;
            r.sourceVisibleJack = srcPort.visibleJackIndex;
            r.destNodeID = dstNodeID;
            r.destChannelIndex = e.dstChannel;
            r.destVisibleJack = dstPort.visibleJackIndex;
            r.voiceCount = 1;
            r.hasSource = true;
            r.hasDest = true;
            r.amount = 1.0f;
            r.isBypassed = false;
            r.role = dstPort.role; // Pitch, Gate, or ModCV depending on dest port type
            float peak = readVisualPeak(srcModule->getVisualBuffer());
            r.modSignalPeak = peak;
            r.modSignalValue = peak;
            routings.push_back(r);
        }
    }

    return routings;
}

std::vector<AudioEngine::ModRoutingInfo> AudioEngine::getActiveModRoutings() const {
    std::vector<ModRoutingInfo> routings;
    for (const auto& r : getModulationRoutings()) {
        // Only AttenuverterChain routings are exposed to the ModMatrix
        if (r.kind != RoutingKind::AttenuverterChain)
            continue;
        ModRoutingInfo info;
        info.attenuverterNodeID = r.attenuverterNodeID;
        info.sourceNodeID = r.sourceNodeID;
        info.sourceChannelIndex = r.hasSource ? r.sourceChannelIndex : 0;
        info.destNodeID = r.destNodeID;
        info.destChannelIndex = r.destChannelIndex;
        info.isBypassed = r.isBypassed;
        routings.push_back(info);
    }
    return routings;
}

std::vector<AudioEngine::ModulationDisplayInfo> AudioEngine::getModulationDisplayInfo() const {
    return getModulationDisplayInfo(getModulationRoutings());
}

std::vector<AudioEngine::ModulationDisplayInfo>
AudioEngine::getModulationDisplayInfo(const std::vector<ModulationRouting>& allRoutings) const {
    std::vector<ModulationDisplayInfo> result;

    // Attenuverter routings first (as before)
    for (const auto& r : allRoutings) {
        if (r.kind != RoutingKind::AttenuverterChain)
            continue;
        if (!r.hasDest)
            continue;
        ModulationDisplayInfo info;
        info.attenuverterNodeID = r.attenuverterNodeID;
        info.destNodeID = r.destNodeID;
        info.destChannelIndex = r.destChannelIndex;
        info.modSignalValue = r.modSignalValue;
        info.modSignalPeak = r.modSignalPeak;
        info.isBypassed = r.isBypassed;
        result.push_back(info);
    }

    // DirectCV and PolyBus routings after — only emit display info for ModCV role
    // (Pitch/Gate poly-bus routings are signal distribution, not parameter modulation,
    //  so they must not produce knob-ring display info).
    for (const auto& r : allRoutings) {
        if (r.kind != RoutingKind::DirectCV && r.kind != RoutingKind::PolyBus)
            continue;
        if (!r.hasDest)
            continue;
        if (r.role != PortRole::ModCV)
            continue;
        ModulationDisplayInfo info;
        // attenuverterNodeID left default-constructed (invalid) for direct/poly routings
        info.destNodeID = r.destNodeID;
        info.destChannelIndex = r.destChannelIndex;
        info.modSignalValue = r.modSignalValue;
        info.modSignalPeak = r.modSignalPeak;
        info.isBypassed = false;
        result.push_back(info);
    }

    return result;
}

juce::AudioProcessorGraph::NodeID AudioEngine::addModRouting(juce::AudioProcessorGraph::NodeID sourceNodeID,
                                                             int sourceChannelIndex,
                                                             juce::AudioProcessorGraph::NodeID destNodeID,
                                                             int destChannelIndex) {
    auto attenuverterNode = mainProcessorGraph.addNode(std::make_unique<AttenuverterModule>());
    if (attenuverterNode == nullptr)
        return {};
    if (auto* param = dynamic_cast<juce::AudioParameterFloat*>(attenuverterNode->getProcessor()->getParameters()[1]))
        param->setValueNotifyingHost(param->convertTo0to1(1.0f));
    mainProcessorGraph.addConnection({{sourceNodeID, sourceChannelIndex}, {attenuverterNode->nodeID, 0}});
    mainProcessorGraph.addConnection({{attenuverterNode->nodeID, 0}, {destNodeID, destChannelIndex}});
    return attenuverterNode->nodeID;
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
            if (module->getModuleType() == ModuleType::ExternalMidi)
                continue; // Do not rename External MIDI modules as their name matches the device name

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

AudioEngine::VoiceInfo AudioEngine::getActiveVoiceInfo() const {
    VoiceInfo info;
    for (auto* node : mainProcessorGraph.getNodes()) {
        if (auto* pm = dynamic_cast<PolyMidiModule*>(node->getProcessor())) {
            info.maxVoices += 8;
            info.activeVoices += static_cast<int>(std::popcount(static_cast<unsigned>(pm->getActiveVoiceMask())));
        }
    }
    return info;
}

int AudioEngine::getDisplayVoiceCount() const { return getActiveVoiceInfo().activeVoices; }

void AudioEngine::setMasterMute(bool muted) noexcept { masterMuted_.store(muted, std::memory_order_relaxed); }

bool AudioEngine::isMasterMuted() const noexcept { return masterMuted_.load(std::memory_order_relaxed); }

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
    for (auto* node : mainProcessorGraph.getNodes()) {
        if (auto* extMidi = dynamic_cast<ExternalMidiModule*>(node->getProcessor())) {
            if (source->getName() == extMidi->getName()) {
                extMidi->pushMidiMessage(message);
            }
        }
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
    // The ExternalMidiModule is just a processor in the graph.
    // It should collect messages in handleIncomingMidiMessage,
    // and then processBlock should output those to its MIDI output port.
    // If we call processBlock on it, we might be clearing its buffer too early.
    // Let's rely on the graph to process all nodes.

    // MIDI messages from collector are already in midiMessages.

    renderNextBlock(buffer, midiMessages);
}

void AudioEngine::renderNextBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) {
    mainProcessorGraph.processBlock(buffer, midiMessages);

    // Zero-fill AFTER processBlock so sequencers / LFOs / envelopes keep advancing.
    if (masterMuted_.load(std::memory_order_relaxed))
        buffer.clear();
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

void AudioEngine::prepareForHost(double sampleRate, int blockSize, int numInputChannels, int numOutputChannels) {
    // The collector is still used in hosted mode: ExternalMidiModule-bound messages and any
    // future UI-generated MIDI go through it, and it must be reset to the host's rate or its
    // timestamps land in the wrong block.
    midiMessageCollector.reset(sampleRate);
    mainProcessorGraph.setPlayConfigDetails(numInputChannels, numOutputChannels, sampleRate, blockSize);
    mainProcessorGraph.prepareToPlay(sampleRate, blockSize);
}

void AudioEngine::processHostBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) {
    renderNextBlock(buffer, midiMessages);
}

void AudioEngine::releaseFromHost() { mainProcessorGraph.releaseResources(); }
