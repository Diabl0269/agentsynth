#include "AudioEngine.h"
#include "Modules/ADSRModule.h"
#include "Modules/AttenuverterModule.h"
#include "Modules/AudioInputModule.h"
#include "Modules/ExternalMidiModule.h"
#include "Modules/FX/DelayModule.h"
#include "Modules/FX/DistortionModule.h"
#include "Modules/FX/ReverbModule.h"
#include "Modules/FilterModule.h"
#include "Modules/LFOModule.h"
#include "Modules/MidiKeyboardModule.h"
#include "Modules/OscillatorModule.h"
#include "Modules/PolyMidiModule.h"
#include "Modules/RecordTapModule.h"
#include "Modules/SequencerModule.h"
#include "Modules/VCAModule.h"
#include "PresetManager.h"
#include "Timeline/AutomationBinding.h"
#include "Timeline/MidiRecorder.h"
#include "Timeline/TimelineDoc.h"
#include <algorithm>
#include <bit>
#include <map>
#include <set>

namespace {

/** Counts a render pass in and out for the whole of renderNextBlock — the audio half of the
 *  teardown handshake in AudioEngine::drainAudioCallbacks(). Allocation-free and lock-free; the
 *  cost is two atomic RMWs per pass.
 *
 *  Two monotonic counters rather than one in-flight count, because a plugin's audio thread renders
 *  back-to-back: a drain polling "is anything in flight?" can miss the microscopic gap between one
 *  block and the next forever, where "have the passes that were already running finished?" always
 *  completes within one block. */
struct ScopedRenderPass {
    ScopedRenderPass(std::atomic<std::uint64_t>& started, std::atomic<std::uint64_t>& finished) noexcept
        : finished_(finished) {
        // seq_cst, not acq_rel: this increment must not be reordered after the borrowed-pointer
        // reads it protects, and a message thread that has already published a new pointer must
        // then see this pass. Anything weaker leaves the Dekker-style store/load pair open.
        started.fetch_add(1, std::memory_order_seq_cst);
    }
    ~ScopedRenderPass() { finished_.fetch_add(1, std::memory_order_release); }

    std::atomic<std::uint64_t>& finished_;
};

} // namespace

AudioEngine::AudioEngine(HostMode mode)
    : hostMode_(mode) {
    // Install the transport as the graph's playhead exactly once, here. JUCE's
    // AudioProcessorGraph re-applies graph.getPlayHead() to every node processor on every render
    // pass, so every module — including nodes re-created later by a preset load or an undo restore
    // — sees it through the standard getPlayHead() API. No per-node injection, and nothing to
    // re-wire when the graph's node set changes.
    mainProcessorGraph.setPlayHead(&transport);
}

AudioEngine::~AudioEngine() { shutdown(); }

void AudioEngine::initialise() {
    // Hosted mode: the plugin wrapper owns the audio clock and forwards the host's MIDI, so we
    // skip device/MIDI acquisition entirely and only build the initial patch. prepareForHost()
    // supplies the real sample rate/channel count later, since there is no device to ask.
    if (!isHosted()) {
        initialiseDevices(savedDeviceState_.get());
        // A machine with NO usable audio device (headless Linux, CI) never receives the
        // audioDeviceAboutToStart() that normally configures the graph before the patch is built
        // below. Unconfigured, the "Audio Output" IO node snapshots 0 channels and every
        // connection into it is rejected as out-of-range — silently gutting the default patch.
        // Mirror the hosted placeholder; a device appearing later reconfigures on start.
        if (deviceManager.getCurrentAudioDevice() == nullptr)
            mainProcessorGraph.setPlayConfigDetails(0, 2, 44100.0, 512);
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

void AudioEngine::setSavedDeviceState(std::unique_ptr<juce::XmlElement> state) { savedDeviceState_ = std::move(state); }

void AudioEngine::initialiseDevices(const juce::XmlElement* savedDeviceState) {
    if (savedDeviceState != nullptr) {
        // Restore the user's own device setup — which is the only way audio INPUT ever gets
        // enabled, since the Audio tab writes the channel mask into this XML the moment the user
        // ticks an input channel (juce::AudioDeviceSelectorComponent sets useDefaultInputChannels
        // = false, and juce::AudioDeviceManager then persists "audioDeviceInChans").
        //
        // The 0 input channels NEEDED is load-bearing, and deliberately not 2: it is the count JUCE
        // falls back to whenever the saved setup does NOT pin its input channels (a state saved
        // after the user changed only their output device or sample rate) and whenever the saved
        // device can't be opened at all (interface unplugged — selectDefaultDeviceOnFailure below
        // re-runs with these same counts). Asking for 2 there would silently switch a microphone on
        // for a user who never asked for one. It costs nothing when the saved state IS explicit:
        // juce::AudioDeviceManager::setAudioDeviceSetup re-derives the needed count from the mask.
        deviceManager.initialise(0, 2, savedDeviceState, /*selectDefaultDeviceOnFailure*/ true);
    } else {
        // No saved state — a fresh install, or an existing user who has never touched the Audio
        // tab. This is byte-identical to what initialise() has always done (output only, input
        // hard-off), which is what makes this need no migration step: absence of state IS the
        // legacy behaviour, and inputs stay opt-in.
        deviceManager.initialiseWithDefaultDevices(0, 2);
    }

    deviceManager.addAudioCallback(this);
    deviceCallbackAttached_ = true;

    // Persist-on-change: every device/rate/channel change the user makes broadcasts here,
    // and changeListenerCallback hands the new state to whoever installed onDeviceStateChanged.
    deviceManager.addChangeListener(this);

    // Initialise MIDI input collector
    // With no device the setup reports 0 Hz, which MidiMessageCollector::reset asserts on;
    // collected messages are only consumed from render passes, which a device-less engine
    // never runs, so the placeholder rate is inert.
    const double collectorRate = deviceManager.getAudioDeviceSetup().sampleRate;
    midiMessageCollector.reset(collectorRate > 0.0 ? collectorRate : 44100.0);

    // Enable all available MIDI inputs by default
    for (auto& info : juce::MidiInput::getAvailableDevices()) {
        auto input = juce::MidiInput::openDevice(info.identifier, this);
        if (input != nullptr) {
            input->start();
            midiInputs.push_back(std::move(input));
        }
    }
}

void AudioEngine::changeListenerCallback(juce::ChangeBroadcaster* source) {
    // Only ever subscribed to our own device manager, and only in Standalone mode; both checks are
    // here so a future subscription can't silently start persisting something else's state.
    if (isHosted() || source != &deviceManager)
        return;

    if (onDeviceStateChanged)
        onDeviceStateChanged(deviceManager.createStateXml());
}

void AudioEngine::shutdown() {
    if (!isHosted()) {
        deviceManager.removeChangeListener(this);
        deviceManager.removeAudioCallback(this);
        deviceCallbackAttached_ = false;
#if JUCE_LINUX || JUCE_BSD || JUCE_MAC || JUCE_IOS
        for (auto& input : midiInputs) {
            input->stop();
        }
        midiInputs.clear();
#endif
    }
    mainProcessorGraph.clear();

    // Last, after the device callback is gone and the graph is empty: nothing can call
    // beginAudioBlock() any more, which is the precondition reclaimAllUnsafe() demands. The
    // binding tables go too: each holds refcounted Node::Ptrs, so leaving them until the
    // destructor would keep the just-cleared graph's processors alive for no reason.
    timelineSnapshots.reclaimAllUnsafe();
    automationBindings_.reclaimAllUnsafe();
    // Same precondition and same place: nothing can render any more, so the streamer's
    // prefetch thread can be stopped and every open reader closed.
    clipStreamer_.releaseAll();
}

void AudioEngine::publishTimeline(const synth::TimelineDoc& doc) {
    auto snapshot = synth::TimelineSnapshot::buildFrom(doc);

    // The snapshot's address is stable across the move into publish() (unique_ptr moves the
    // pointer, not the object), so the binding table can point at it before it is handed over.
    const synth::TimelineSnapshot* snapshotPtr = snapshot.get();

    auto table = std::make_unique<synth::AutomationBindingTable>();
    table->snapshot = snapshotPtr;

    if (snapshotPtr != nullptr && !snapshotPtr->lanes.empty()) {
        // uuid -> node, built once: resolving lane-by-lane against getNodes() would be
        // O(lanes * nodes) on a doc that can carry hundreds of lanes.
        std::map<juce::String, juce::AudioProcessorGraph::Node*> nodesByUuid;
        for (auto* node : mainProcessorGraph.getNodes()) {
            if (node == nullptr)
                continue;
            const juce::String uuid = node->properties["uuid"].toString();
            if (uuid.isNotEmpty())
                nodesByUuid.emplace(uuid, node);
        }

        table->bindings.reserve(snapshotPtr->lanes.size());

        for (std::size_t laneIndex = 0; laneIndex < snapshotPtr->lanes.size(); ++laneIndex) {
            const auto& lane = snapshotPtr->lanes[laneIndex];
            if (lane.nodeUuid[0] == '\0' || lane.paramId[0] == '\0')
                continue; // never bound to anything — not orphaned, just unbound

            const auto found = nodesByUuid.find(juce::String(lane.nodeUuid));
            if (found == nodesByUuid.end())
                continue; // orphaned: the lane is retained in the doc but automates nothing

            // The shared resolver — exact id match on a hosted plugin's LIVE instance
            // parameter, a narrow legacy index fallback, or (for every non-plugin node) exactly the
            // findParameterByID this replaced. `resolved.orphaned` is the doc's business (surfaced by
            // TimelineReconciler, not decided here); the binding build only cares whether something
            // came back to write into.
            const auto resolved = synth::resolveLaneParameter(found->second->getProcessor(), juce::String(lane.paramId),
                                                              lane.paramIndexHint);
            if (!resolved.resolved())
                continue; // the node exists but no longer has a safely-resolvable parameter

            synth::AutomationBindingTable::Binding binding;
            binding.laneIndex = static_cast<int>(laneIndex);
            binding.node = found->second; // refcounted — see AutomationApplier.h
            binding.param = resolved.rangedParam;
            binding.hostedParam = resolved.hostedParam;
            binding.nodeID = found->second->nodeID; // identity for the UI feed's events
            table->bindings.push_back(std::move(binding));
        }
    }

    // The streamer is brought to THIS snapshot before it is published, so a clip the audio
    // thread is about to see either has a stream already opening or is one the streamer deliberately
    // declined (unresolvable, or past its pool cap) — never one it has not been told about. Runs on
    // the message thread and opens no files itself: it resolves refs and hands the resulting paths
    // to its prefetch thread.
    if (snapshotPtr != nullptr)
        clipStreamer_.syncToSnapshot(*snapshotPtr);

    // Snapshot FIRST, bindings SECOND. The whole coherence argument in AutomationApplier.h rests on
    // this order — do not reorder these two lines.
    timelineSnapshots.publish(std::move(snapshot));
    automationBindings_.publish(std::move(table));
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

void AudioEngine::setTransportEnabled(bool enabled) noexcept {
    transportEnabled_.store(enabled, std::memory_order_relaxed);
}

bool AudioEngine::isTransportEnabled() const noexcept { return transportEnabled_.load(std::memory_order_relaxed); }

void AudioEngine::setInputMonitoringEnabled(bool enabled) noexcept {
    inputMonitoringEnabled_.store(enabled, std::memory_order_relaxed);
}

bool AudioEngine::isInputMonitoringEnabled() const noexcept {
    return inputMonitoringEnabled_.load(std::memory_order_relaxed);
}

bool AudioEngine::consumeFeedbackGuardTripped() noexcept {
    return feedbackGuardTripped_.exchange(false, std::memory_order_relaxed);
}

void AudioEngine::setAutomationSlicingEnabled(bool enabled) noexcept {
    automationSlicingEnabled_.store(enabled, std::memory_order_relaxed);
}

bool AudioEngine::isAutomationSlicingEnabled() const noexcept {
    return automationSlicingEnabled_.load(std::memory_order_relaxed);
}

void AudioEngine::createDefaultPatch() {
    mainProcessorGraph.clear();
    using AudioGraphIOProcessor = juce::AudioProcessorGraph::AudioGraphIOProcessor;
    // The input side is a real module (max channels fixed, visible jacks following the
    // device) reading the block's captured input off the playhead; the OUTPUT side stays a JUCE IO
    // node, because the graph's output channel count is tied to it.
    auto inputNode = mainProcessorGraph.addNode(std::make_unique<AudioInputModule>());
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
    juce::ignoreUnused(context);

    // The channel-aliasing subtlety, which is why this is not simply a buffer wrapped
    // around outputChannelData:
    //
    // juce::AudioProcessor renders IN PLACE over ONE buffer of max(numIn, numOut) channels.
    // Channels [0, numInputChannels) are what the graph's "Audio Input" node reads and channels
    // [0, numOutputChannels) are what its "Audio Output" node writes — the SAME memory. So the
    // device's input has to be COPIED into that buffer before the graph runs: the device's input
    // block is const and the graph is going to overwrite these channels with its result. Any
    // channel the input doesn't cover starts silent, exactly as before.
    //
    // The channels the device's outputs don't cover (a device with more inputs than outputs) come
    // from deviceScratch_. Both it and the pointer array are sized in audioDeviceAboutToStart, so
    // nothing here allocates; if a block ever arrives wider or longer than that, we serve the
    // output channels alone rather than allocate. This is juce::AudioProcessorPlayer's pattern.
    const auto pointerCapacity = static_cast<int>(deviceChannelPointers_.size());
    const int scratchChannels = deviceScratch_.getNumChannels();
    const bool scratchFitsBlock = deviceScratch_.getNumSamples() >= numSamples;

    int totalChannels = std::min(std::max(numInputChannels, numOutputChannels), pointerCapacity);
    int nextScratchChannel = 0;

    for (int channel = 0; channel < totalChannels; ++channel) {
        float* dest =
            (channel < numOutputChannels && outputChannelData != nullptr) ? outputChannelData[channel] : nullptr;
        if (dest == nullptr) {
            // An input channel past the output count, or an output channel the device handed us as
            // null. Borrow a scratch channel; if there is none to borrow, stop here — the render
            // buffer keeps every channel it has already resolved, which always includes the ones
            // the speakers will actually read.
            if (!scratchFitsBlock || nextScratchChannel >= scratchChannels) {
                totalChannels = channel;
                break;
            }
            dest = deviceScratch_.getWritePointer(nextScratchChannel++);
        }

        const float* src =
            (channel < numInputChannels && inputChannelData != nullptr) ? inputChannelData[channel] : nullptr;

        // This copy stays unconditional — it is the CAPTURE path (it feeds the render
        // buffer's now-vestigial IO-node channels and, below, captureDeviceInput's snapshot), and
        // the engine always captures input, armed or monitored or not. What actually gates the mic
        // -> speaker loop lives downstream of here: AudioInputModule::processBlock silences its own
        // graph output while monitoring is disabled (TransportService::isInputMonitoringEnabledForBlock),
        // and AudioEngine::runFeedbackGuard (called from renderPass, post-graph) disables monitoring
        // and zeroes the block outright if the output stays near-clip too long. See
        // docs/architecture.md's "Input monitoring & feedback guard".
        if (src != nullptr)
            std::copy(src, src + numSamples, dest);
        else
            std::fill(dest, dest + numSamples, 0.0f);

        deviceChannelPointers_[static_cast<std::size_t>(channel)] = dest;
    }

    // Any output channel that did not make it into the render buffer (only reachable in the
    // degraded no-scratch path above, or if a block somehow arrived before a prepare) still has to
    // leave the speakers silent rather than replaying whatever the device left in it.
    for (int channel = totalChannels; channel < numOutputChannels; ++channel)
        if (outputChannelData != nullptr && outputChannelData[channel] != nullptr)
            std::fill(outputChannelData[channel], outputChannelData[channel] + numSamples, 0.0f);

    // A SECOND copy of the input, into storage the graph never renders over, taken before
    // the graph runs. The copy above put the input into the render buffer for the "Audio Input" IO
    // node's benefit; the graph then overwrites those very channels with its result, so a module
    // that reads them mid-graph sees output, not input. See captureDeviceInput.
    captureDeviceInput(inputChannelData, numInputChannels, numSamples);

    juce::AudioBuffer<float> buffer(deviceChannelPointers_.data(), totalChannels, numSamples);
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

void AudioEngine::drainAudioCallbacks() noexcept {
    // MESSAGE THREAD. The other half of ScopedRenderPass's handshake. The caller has already
    // published its new pointer with a seq_cst store, so every pass that STARTS from here on reads
    // the new value; this only has to outlast the passes that had already started, which is exactly
    // what waiting for the finished count to catch up with the started count says. It does NOT wait
    // for the audio thread to go idle — an engine inside a host never does.
    //
    // Bounded on purpose. A device thread wedged inside a callback (or a host that never returns
    // from processBlock) would otherwise hang the message thread during teardown; giving up after
    // the timeout is strictly better than a deadlock. Returns immediately whenever nothing is
    // clocking the graph — a suspended device callback, a headless test, a hosted engine between
    // blocks.
    static constexpr int kDrainTimeoutMs = 2000;
    const auto deadline = juce::Time::getMillisecondCounter() + (juce::uint32)kDrainTimeoutMs;
    const auto target = renderPassesStarted_.load(std::memory_order_seq_cst);

    int spins = 0;
    while (renderPassesFinished_.load(std::memory_order_acquire) < target) {
        if (juce::Time::getMillisecondCounter() > deadline) {
            jassertfalse; // a render pass outlasted the drain: something is holding the audio thread
            return;
        }
        // A block is milliseconds at most, so spinning wins the common case; fall back to sleeping
        // rather than burning a core if the pass really is long (a huge buffer, a stalled host).
        if (++spins < 1000)
            juce::Thread::yield();
        else
            juce::Thread::sleep(1);
    }
}

void AudioEngine::renderNextBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) {
    // The one place a render pass is declared in flight. Both entry points (the standalone device
    // callback and the hosted processBlock) funnel through here, so this single guard covers every
    // read of the borrowed midiCaptureSink_ / automationRecordState_ pointers below — see
    // drainAudioCallbacks(), which is what an owner destroying either of them waits on.
    const ScopedRenderPass renderPassGuard(renderPassesStarted_, renderPassesFinished_);

    // Off by default, in which case this is one pass over the whole buffer and the
    // behaviour is byte-identical to what it was before slicing existed. The scratch check is a
    // belt-and-braces fallback: a callback arriving with more channels than prepare() sized for
    // would otherwise have to allocate, and allocating here is not allowed.
    const int numChannels = buffer.getNumChannels();
    const bool canSlice = numChannels > 0 && buffer.getNumSamples() > kAutomationSliceSamples &&
                          static_cast<std::size_t>(numChannels) <= sliceChannelPointers_.size();

    if (automationSlicingEnabled_.load(std::memory_order_relaxed) && canSlice)
        renderSliced(buffer, midiMessages);
    else
        renderPass(buffer, midiMessages);

    // Zero-fill AFTER the graph has run (and after every slice, not per slice) so sequencers /
    // LFOs / envelopes keep advancing.
    if (masterMuted_.load(std::memory_order_relaxed))
        buffer.clear();
}

void AudioEngine::renderPass(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages, int inputSampleOffset) {
    // The one clock site: both the standalone device callback and the hosted processBlock funnel
    // through here, so the transport advances exactly once per render pass in either mode. Must run
    // before the graph so every node renders against this pass's position. Gated on the runtime
    // setting too: disabling it mid-session simply freezes the transport in place.
    if (transportEnabled_.load(std::memory_order_relaxed))
        transport.tick(buffer.getNumSamples());

    // Open this pass's timeline snapshot, exactly like the tick, and park it on the
    // transport so every node can reach it through the playhead it already has (see
    // TransportService::setCurrentTimelineSnapshot). Exactly one beginAudioBlock() per RENDER PASS
    // is the epoch-reclamation contract (it used to read "per callback"; with slicing on, a
    // callback is several passes — the contract is unchanged, only the unit is named more
    // precisely, and slicing only makes the epoch advance faster). The borrowed reference must not
    // outlive this pass, which is why the transport's copy is overwritten at the top of the next
    // one. Deliberately NOT gated on transportEnabled_: freezing the transport is a musical
    // decision, and stalling reclamation with it would let retired snapshots pile up for as long as
    // the setting is off.
    transport.setCurrentTimelineSnapshot(&timelineSnapshots.beginAudioBlock());

    // The second passenger on the same carrier. Unlike the snapshot this pointer is not
    // per-block — the streamer lives as long as the engine does — but it is installed here, beside
    // the snapshot, so the two always arrive together and a node never sees one without the other.
    transport.setAudioClipStreamer(&clipStreamer_);

    // The single MIDI-recording capture point. `midiMessages` here is already the buffer
    // that BOTH standalone (collector drain, see audioDeviceIOCallbackWithContext) and hosted
    // (delivered directly by the host into processHostBlock) modes converge on before the graph
    // ever sees it — the one place every external MIDI message is guaranteed to appear exactly
    // once. This must never read the ExternalMidiModule pushMidiMessage() copies
    // handleIncomingMidiMessage also makes (those flow only inside that module's own processing,
    // never through this buffer) — recording from both paths would double-record any note whose
    // source also has an ExternalMidi node bound to it.
    // Loaded ONCE into a local, here, and used only within this pass — the pointer is borrowed, and
    // the in-flight guard in renderNextBlock is what makes that borrow safe against an owner
    // destroying the recorder (acquire pairs with the setter's publish).
    if (auto* recorder = midiCaptureSink_.load(std::memory_order_acquire))
        recorder->captureBlock(midiMessages, transport.getCurrentBlockInfo());

    // Push this pass's automation values into their bound parameters, after the tick (so the
    // beat position is this pass's) and before the graph (so every node reads the automated value
    // in the same pass it was written).
    // The recorder's audio-visible half rides along so per-lane record modes and in-flight
    // gesture claims are honoured. Null unless an owner installed a recorder.
    // The UI reflection feed rides along too, so a slider can follow automation without a
    // notifying write. Always passed (see getAutomationUiFeed()) — nothing drains it without a
    // GraphEditor around to do so, so a headless render pass just fills a ring nobody reads.
    automationApplier_.applyBlock(automationBindings_.beginAudioBlock(), transport.getCurrentBlockInfo(),
                                  automationRecordState_.load(std::memory_order_acquire), &automationUiFeed_);

    // Deliberately OUTSIDE the timeline flag: device input is not a timeline feature.
    // Point the playhead at this pass's slice of the captured input before the graph runs, and
    // take it away immediately after — nothing outside a render pass may read those pointers.
    publishDeviceInputForPass(inputSampleOffset, buffer.getNumSamples());

    // Deliberately OUTSIDE the timeline flag for the same reason as the device-input
    // publish above: whether Audio Input's graph output is gated is an input-path property, not a
    // timeline one. Read once per render pass and handed to the carrier BEFORE the graph runs, so
    // every module — and the feedback guard below — agree on the same answer this pass.
    const bool monitoringEnabledThisPass = inputMonitoringEnabled_.load(std::memory_order_relaxed);
    transport.setInputMonitoringEnabledForBlock(monitoringEnabledThisPass);

    mainProcessorGraph.processBlock(buffer, midiMessages);

    transport.setDeviceInputForBlock(nullptr, 0, 0);

    // The metronome click, generated from the transport and summed POST-graph — after the
    // graph has produced its own output (so the click can never appear in anything the graph itself
    // taps or that a bounce renders from inside the graph — see BounceExporter's force-off guard)
    // and BEFORE renderNextBlock's master-mute zero-fill, which runs after renderPass/renderSliced
    // return. That ordering is deliberate: master mute clears the WHOLE buffer, so it silences the
    // click along with everything else the engine produces, exactly as it silences the graph.
    metronome_.renderClicks(buffer, transport.getCurrentBlockInfo());

    // The feedback guard. Post-graph (so it sees exactly what would reach the speakers,
    // metronome click included) and pre-master-mute (renderNextBlock's zero-fill runs after this
    // returns) — ungated, like the monitoring flag above.
    runFeedbackGuard(buffer, monitoringEnabledThisPass);
}

void AudioEngine::runFeedbackGuard(juce::AudioBuffer<float>& buffer, bool monitoringEnabledThisPass) noexcept {
    // Never evaluates while monitoring is disabled — a loud synth alone can never trip it. Reset the
    // run rather than merely skipping: a run that was building up before monitoring was switched off
    // must not survive into whenever it's switched back on.
    if (!monitoringEnabledThisPass) {
        feedbackGuardConsecutiveSamples_ = 0;
        return;
    }

    // Scope the peak scan to the graph's OUTPUT channels (mainProcessorGraph.getTotalNumOutputChannels(),
    // set by setPlayConfigDetails in both host modes) rather than every channel `buffer` carries: a
    // device with more inputs than outputs parks the extra input channels past the output count in
    // this same render buffer (see audioDeviceIOCallbackWithContext), and a hot mic sitting there
    // unconnected to anything is not feedback — it never reaches the speakers.
    const int numChannels = std::min(buffer.getNumChannels(), mainProcessorGraph.getTotalNumOutputChannels());
    const int numSamples = buffer.getNumSamples();
    if (numChannels <= 0 || numSamples <= 0) {
        feedbackGuardConsecutiveSamples_ = 0;
        return;
    }

    float peak = 0.0f;
    for (int channel = 0; channel < numChannels; ++channel)
        peak = std::max(peak, buffer.getMagnitude(channel, 0, numSamples));

    if (peak < kFeedbackPeakThreshold) {
        feedbackGuardConsecutiveSamples_ = 0;
        return;
    }

    feedbackGuardConsecutiveSamples_ += numSamples;

    const double sampleRate = transport.getSampleRate();
    const double consecutiveSeconds = sampleRate > 0.0 ? (double)feedbackGuardConsecutiveSamples_ / sampleRate : 0.0;
    if (consecutiveSeconds < kFeedbackSustainSeconds)
        return;

    // Tripped. Disable monitoring — the SAME flag AudioInputModule reads next render pass and the
    // one MainComponent's poll will see and stop re-enabling — latch the one-shot report for the UI,
    // and zero THIS block's output immediately rather than waiting for the disabled flag to take
    // effect next render pass (which could be a full device block away). The whole block, not just
    // from some in-block sample: the peak was measured over the whole block, so there is no single
    // sample position to call "the" trip point.
    inputMonitoringEnabled_.store(false, std::memory_order_relaxed);
    feedbackGuardTripped_.store(true, std::memory_order_relaxed);
    buffer.clear();
    feedbackGuardConsecutiveSamples_ = 0;
}

void AudioEngine::renderSliced(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) {
    const int numChannels = buffer.getNumChannels();
    const int totalSamples = buffer.getNumSamples();

    for (int offset = 0; offset < totalSamples; offset += kAutomationSliceSamples) {
        const int sliceLength = std::min(kAutomationSliceSamples, totalSamples - offset);

        for (int channel = 0; channel < numChannels; ++channel)
            sliceChannelPointers_[static_cast<std::size_t>(channel)] = buffer.getWritePointer(channel) + offset;

        // A view, not a copy: this ctor wraps the caller's channel pointers and allocates nothing.
        juce::AudioBuffer<float> sliceView(sliceChannelPointers_.data(), numChannels, sliceLength);

        // Re-base this slice's MIDI to slice-relative positions. clear() keeps the storage
        // ensureSize() reserved in prepare, and the (data, numBytes, position) overload of addEvent
        // avoids constructing a juce::MidiMessage (which would allocate for a sysex).
        sliceMidi_.clear();
        for (const auto metadata : midiMessages) {
            if (metadata.samplePosition >= offset && metadata.samplePosition < offset + sliceLength)
                sliceMidi_.addEvent(metadata.data, metadata.numBytes, metadata.samplePosition - offset);
        }

        renderPass(sliceView, sliceMidi_, offset);
    }
}

void AudioEngine::prepareSliceScratch(int numChannels, int blockSize) {
    // Message thread (both prepare paths). Sized with headroom so a host that hands us a wider
    // buffer than it declared still takes the sliced path instead of silently falling back.
    const int channels = std::max(numChannels, 2) + 2;
    sliceChannelPointers_.assign(static_cast<std::size_t>(channels), nullptr);

    // Worst case for one slice is every event in the block landing inside it. juce::MidiBuffer
    // stores 4 bytes of header + the message bytes per event; 16 bytes per event is generous for
    // the note/CC traffic this path sees, and ensureSize is a no-op once the storage is big enough.
    sliceMidi_.clear();
    sliceMidi_.ensureSize(static_cast<std::size_t>(std::max(blockSize, kAutomationSliceSamples)) * 16u);
}

void AudioEngine::handleStreamFormatChange(double newRate, int newBlockSize) {
    // Order matters — see docs/architecture.md's "Device & sample-rate changes".
    //
    // 1. TRANSPORT FIRST: every other consumer below, and every module's NEXT processBlock, must see
    //    the new rate consistently once this call returns. TransportService::prepare() keeps the
    //    beat (not the sample) canonical, so the musical position survives untouched; only
    //    the sample-domain mirror of it moves.
    transport.prepare(newRate, newBlockSize);
    onFormatChangeStepForTest(1);

    // 2. METRONOME: a voice ringing across the boundary was computed for the OLD rate (see
    //    Metronome::startClick) and would otherwise continue at the wrong pitch and the wrong
    //    length. See Metronome::resetVoices().
    metronome_.resetVoices();
    onFormatChangeStepForTest(2);

    // 3. STREAMER: force every Track Audio ring to miss until the prefetch thread has refilled it at
    //    the NEW mapping, rather than risk a coincidental hit on content filled under the OLD one.
    //    See AudioClipStreamer::invalidateAllStreams().
    clipStreamer_.invalidateAllStreams();
    onFormatChangeStepForTest(3);

    // 4. IN-FLIGHT TAKES: neither an audio take's WAV (fixed header rate, written at whatever rate
    //    was active at startCapture()) nor a MIDI take's beat-domain events can honestly span a
    //    format change — an audio take that kept recording across the boundary would mix two actual
    //    sample rates under one header, and there is no clean way to splice that. Rather than try,
    //    flag it here (at the exact moment of the change) so MainComponent's 10 Hz poll finalizes it
    //    through the SAME commit choke points a manual Record-off or a transport stop already use —
    //    see consumeFormatChangedDuringCapture().
    bool anyCapturing = false;
    for (auto* node : mainProcessorGraph.getNodes()) {
        if (node == nullptr)
            continue;
        if (auto* tap = dynamic_cast<RecordTapModule*>(node->getProcessor())) {
            if (tap->isCapturing()) {
                anyCapturing = true;
                break;
            }
        }
    }
    if (!anyCapturing) {
        if (auto* recorder = midiCaptureSink_.load(std::memory_order_relaxed))
            anyCapturing = recorder->isRecording();
    }
    if (anyCapturing)
        formatChangedDuringCapture_.store(true, std::memory_order_relaxed);
    onFormatChangeStepForTest(4);
}

void AudioEngine::audioDeviceAboutToStart(juce::AudioIODevice* device) {
    if (device) {
        const int numInputChannels = device->getActiveInputChannels().countNumberOfSetBits();
        const int numOutputChannels = device->getActiveOutputChannels().countNumberOfSetBits();
        const double sampleRate = device->getCurrentSampleRate();
        const int blockSize = device->getCurrentBufferSizeSamples();

        mainProcessorGraph.setPlayConfigDetails(numInputChannels, numOutputChannels, sampleRate, blockSize);
        // Before the graph: nodes read the playhead from their first prepared block onwards, so the
        // transport must already be on the device's sample rate when they do. This also
        // resets the metronome, invalidates the clip streamer and flags any in-flight take.
        handleStreamFormatChange(sampleRate, blockSize);
        // Both scratches are sized against max(in, out) — that is the channel count the
        // render buffer has once the device's input is copied into it, so sizing either on the
        // output count alone would make a 2-in/1-out device fall out of the sliced path (and, for
        // the device scratch, force an allocation in the callback).
        prepareDeviceScratch(numInputChannels, numOutputChannels, blockSize);
        prepareSliceScratch(std::max(numInputChannels, numOutputChannels), blockSize);
        prepareDeviceInputSnapshot(numInputChannels, blockSize);
        deviceInputLatencySamples_.store(device->getInputLatencyInSamples(), std::memory_order_relaxed);
        deviceOutputLatencySamples_.store(device->getOutputLatencyInSamples(), std::memory_order_relaxed);
        mainProcessorGraph.prepareToPlay(sampleRate, blockSize);
    }
}

void AudioEngine::prepareDeviceScratch(int numInputChannels, int numOutputChannels, int blockSize) {
    // Message thread (or whichever thread JUCE prepares the device on) — never the callback. Two
    // spare channels of headroom for the same reason prepareSliceScratch keeps them: a device that
    // hands us a wider block than it declared must not push the callback into allocating.
    const int channels = std::max({numInputChannels, numOutputChannels, 2}) + 2;
    deviceChannelPointers_.assign(static_cast<std::size_t>(channels), nullptr);

    // Sized to hold EVERY channel, not just the ones past the output count: a device may also hand
    // the callback a null pointer for an output channel, and that channel then needs scratch too.
    deviceScratch_.setSize(channels, std::max(blockSize, 1), /*keepExistingContent*/ false,
                           /*clearExtraSpace*/ true, /*avoidReallocating*/ false);
    deviceScratch_.clear();
}

void AudioEngine::prepareDeviceInputSnapshot(int numInputChannels, int blockSize) {
    // Message thread (both prepare paths) — never the callback. Always the full channel width, so
    // a device change that ADDS inputs never has to resize anything from the audio thread; only
    // the per-block "how many did we capture" count varies. Two blocks' worth of length is the same
    // headroom the other scratches keep, for a device or host that overruns what it declared.
    deviceInputSnapshot_.setSize(synth::TransportService::kMaxDeviceInputChannels, std::max(blockSize, 1) * 2,
                                 /*keepExistingContent*/ false, /*clearExtraSpace*/ true,
                                 /*avoidReallocating*/ false);
    deviceInputSnapshot_.clear();
    deviceInputChannelsThisBlock_ = 0;
    deviceInputPointers_.fill(nullptr);
    deviceInputChannelCount_.store(std::max(0, numInputChannels), std::memory_order_relaxed);
}

void AudioEngine::captureDeviceInput(const float* const* inputChannelData, int numInputChannels,
                                     int numSamples) noexcept {
    deviceInputChannelsThisBlock_ = 0;

    if (inputChannelData == nullptr || numInputChannels <= 0 || numSamples <= 0)
        return;

    // Longer than the snapshot was sized for: publish nothing rather than allocate or copy a
    // truncated block that would make the module render this block's tail as silence.
    if (numSamples > deviceInputSnapshot_.getNumSamples())
        return;

    const int channels =
        std::min({numInputChannels, deviceInputSnapshot_.getNumChannels(), (int)deviceInputPointers_.size()});

    for (int channel = 0; channel < channels; ++channel) {
        const float* src = inputChannelData[channel];
        float* dest = deviceInputSnapshot_.getWritePointer(channel);
        if (src != nullptr)
            std::copy(src, src + numSamples, dest);
        else
            std::fill(dest, dest + numSamples, 0.0f);
    }

    deviceInputChannelsThisBlock_ = channels;
}

void AudioEngine::publishDeviceInputForPass(int sampleOffset, int numSamples) noexcept {
    const int offset = std::max(0, sampleOffset);
    if (deviceInputChannelsThisBlock_ <= 0 || numSamples <= 0 ||
        offset + numSamples > deviceInputSnapshot_.getNumSamples()) {
        transport.setDeviceInputForBlock(nullptr, 0, 0);
        return;
    }

    for (int channel = 0; channel < deviceInputChannelsThisBlock_; ++channel)
        deviceInputPointers_[(std::size_t)channel] = deviceInputSnapshot_.getReadPointer(channel) + offset;

    transport.setDeviceInputForBlock(deviceInputPointers_.data(), deviceInputChannelsThisBlock_, numSamples);
}

void AudioEngine::audioDeviceStopped() {
    deviceInputLatencySamples_.store(0, std::memory_order_relaxed);
    deviceOutputLatencySamples_.store(0, std::memory_order_relaxed);
    deviceInputChannelCount_.store(0, std::memory_order_relaxed);
    deviceInputChannelsThisBlock_ = 0;
    mainProcessorGraph.releaseResources();
}

void AudioEngine::prepareForHost(double sampleRate, int blockSize, int numInputChannels, int numOutputChannels) {
    // The collector is still used in hosted mode: ExternalMidiModule-bound messages and any
    // future UI-generated MIDI go through it, and it must be reset to the host's rate or its
    // timestamps land in the wrong block.
    midiMessageCollector.reset(sampleRate);
    mainProcessorGraph.setPlayConfigDetails(numInputChannels, numOutputChannels, sampleRate, blockSize);
    // Before the graph, for the same reason as audioDeviceAboutToStart: the musical position is
    // preserved across the rate change, the sample position is re-derived. This also resets
    // the metronome, invalidates the clip streamer and flags any in-flight take.
    handleStreamFormatChange(sampleRate, blockSize);
    prepareSliceScratch(std::max(numInputChannels, numOutputChannels), blockSize);
    // Hosted mode takes the same input snapshot as the device callback. The host's buffer is
    // one in/out buffer the graph renders over in place, so the input has to be copied out of it
    // before the graph runs or "Audio Input" would tap the mix instead of the input.
    prepareDeviceInputSnapshot(numInputChannels, blockSize);
    mainProcessorGraph.prepareToPlay(sampleRate, blockSize);
}

void AudioEngine::processHostBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) {
    // The host's input lives in the low channels of the same buffer it wants the output in, so the
    // capture has to happen HERE, before the graph gets a chance to overwrite them.
    const int hostInputChannels =
        std::min(deviceInputChannelCount_.load(std::memory_order_relaxed), buffer.getNumChannels());
    if (hostInputChannels > 0)
        captureDeviceInput(buffer.getArrayOfReadPointers(), hostInputChannels, buffer.getNumSamples());
    else
        captureDeviceInput(nullptr, 0, 0);

    renderNextBlock(buffer, midiMessages);
}

void AudioEngine::releaseFromHost() { mainProcessorGraph.releaseResources(); }

bool AudioEngine::suspendDeviceCallback() {
    if (!deviceCallbackAttached_)
        return false;

    // The exact inverse of initialise()'s addAudioCallback. JUCE calls audioDeviceStopped() on us
    // from inside this (which releases the graph's resources) and, importantly, does not return
    // until the device thread is out of our callback — so once this returns, nothing is clocking
    // the graph and an offline renderer may take it over.
    deviceManager.removeAudioCallback(this);
    deviceCallbackAttached_ = false;
    return true;
}

void AudioEngine::resumeDeviceCallback() {
    if (isHosted() || deviceCallbackAttached_)
        return;

    // juce::AudioDeviceManager::addAudioCallback calls audioDeviceAboutToStart() on the new
    // callback BEFORE adding it to its list whenever a device is open, so by the time the device
    // thread can reach us the transport is back on the device's sample rate and the graph is
    // prepared for the device's block size. Nothing here re-applies that by hand.
    deviceManager.addAudioCallback(this);
    deviceCallbackAttached_ = true;
}
