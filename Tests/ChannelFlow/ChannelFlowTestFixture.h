#pragma once

// ChannelFlowTestFixture.h
//
// Shared fixtures and helpers for the ChannelFlow test suite
// (Tests/ChannelFlow/ChannelFlow*Tests.cpp). Header-only; not compiled on its own and not
// registered in Tests/CMakeLists.txt.

#include "../../Source/AI/AIProvider.h"
#include "../../Source/AI/AIStateMapper/AIStateMapper.h"
#include "../../Source/AudioEngine.h"
#include "../../Source/MacroSet.h"
#include "../../Source/Modules/ModuleBase.h"
#include "../../Source/Plugin/Hosting/HostedPluginModule.h"
#include "../../Source/Plugin/Hosting/PluginScanService.h"
#include "../StubPluginInstance.h"
#include "MainComponent/MainComponent.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Timeline/TimelineTrackHeaderComponent.h"
#include <array>
#include <chrono>
#include <gtest/gtest.h>
#include <map>
#include <memory>

// ============================================================================
// Mock AI provider, plugin-scan stub backend, and small graph-inspection helpers used across
// (almost) every ChannelFlow*Tests.cpp file.
// ============================================================================

// Same minimal pattern as AudioClipPlaybackTests.cpp's MockProviderACP — a unique name of its own
// to avoid an ODR clash across test translation units.
class MockProviderCFT : public synth::AIProvider {
public:
    juce::String getProviderName() const override { return "MockCFT"; }
    void fetchAvailableModels(std::function<void(const juce::StringArray&, bool)> callback) override {
        callback({"MockModel"}, true);
    }
    RequestId sendPrompt(const std::vector<Message>&, CompletionCallback callback, const juce::var&,
                         std::function<void(const juce::String&)> = {}) override {
        AIResponse response;
        response.success = true;
        response.content = "Mock response.";
        if (callback)
            callback(response);
        return {};
    }
    void cancel(RequestId) override {}
    void setModel(const juce::String& name) override { model = name; }
    juce::String getCurrentModel() const override { return model; }
    void setRequestTimeoutMs(int timeoutMs) override { requestTimeoutMs = timeoutMs; }
    int getRequestTimeoutMs() const override { return requestTimeoutMs; }

private:
    juce::String model = "MockModel";
    int requestTimeoutMs = 240000;
};

inline int countNodesOfTypeCFT(juce::AudioProcessorGraph& graph, ModuleType type) {
    int count = 0;
    for (auto* node : graph.getNodes())
        if (node != nullptr)
            if (auto* module = dynamic_cast<ModuleBase*>(node->getProcessor()))
                if (module->getModuleType() == type)
                    ++count;
    return count;
}

// The type is a singleton at the point every test here calls it (checked separately by the
// count-based assertions), so "the last one seen" is unambiguous.
inline juce::AudioProcessorGraph::Node* findNodeOfTypeCFT(juce::AudioProcessorGraph& graph, ModuleType type) {
    juce::AudioProcessorGraph::Node* found = nullptr;
    for (auto* node : graph.getNodes())
        if (node != nullptr)
            if (auto* module = dynamic_cast<ModuleBase*>(node->getProcessor()))
                if (module->getModuleType() == type)
                    found = node;
    return found;
}

inline juce::AudioProcessorGraph::Node* findNodeNamedCFT(juce::AudioProcessorGraph& graph, const juce::String& name) {
    for (auto* node : graph.getNodes())
        if (node != nullptr && node->getProcessor() != nullptr && node->getProcessor()->getName() == name)
            return node;
    return nullptr;
}

inline juce::String nodeUuid(juce::AudioProcessorGraph::Node* node) {
    return node != nullptr ? node->properties["uuid"].toString() : juce::String();
}

// The factory default preset (PresetManager::getPresetJSON(0), loaded by every fresh MainComponent)
// already contains its own ADSR-type nodes (Amp Env, Filter Env) and a VCA node, so
// findNodeOfTypeCFT's "last one seen" is not a safe way to find the ONE this test's own
// addInstrumentTrack call just created. Disambiguates by macro membership instead — the track's
// macro is freshly built and contains only this track's own nodes.
inline juce::AudioProcessorGraph::Node* findMacroMemberOfTypeCFT(juce::AudioProcessorGraph& graph,
                                                                 const synth::Macro& macro, ModuleType type) {
    for (auto* node : graph.getNodes())
        if (node != nullptr)
            if (auto* module = dynamic_cast<ModuleBase*>(node->getProcessor()))
                if (module->getModuleType() == type && macro.hasMember(nodeUuid(node)))
                    return node;
    return nullptr;
}

// T183: flips a module's "poly" AudioParameterBool, when it has one. No-op (returns false) for
// Sampler, which has no poly parameter at all.
inline bool setPolyParamCFT(juce::AudioProcessor* processor, bool poly) {
    if (processor == nullptr)
        return false;
    for (auto* param : processor->getParameters())
        if (auto* boolParam = dynamic_cast<juce::AudioParameterBool*>(param))
            if (boolParam->paramID == "poly") {
                boolParam->setValueNotifyingHost(poly ? 1.0f : 0.0f);
                return true;
            }
    return false;
}

// ============================================================================
// FRO42 (P9-3h): "+ Track -> Instrument -> Plugin" — the fake plugin-format seam, same shape
// PluginScanTests.cpp's ScanningStubBackend/ScanListStubBackend use, extended with a name-keyed
// factory table so a single backend can make one scanned identity succeed (an instrument) and
// another fail (an effect never offered, or an instrument whose load is refused/broken) without
// juggling several ScopedDefault installs per test.
// ============================================================================

/** Pumps the JUCE message loop until `predicate` holds or the timeout expires — same bounded-poll
 *  idiom as HostedPluginTests.cpp/PluginScanTests.cpp. Also usable as a plain "drain the loop for a
 *  bit" call with an always-false predicate when a test has nothing else to wait on. */
template <typename Predicate>
bool pumpUntilCFT(Predicate predicate, int timeoutMs = 4000) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    do {
        if (predicate())
            return true;
        juce::MessageManager::getInstance()->runDispatchLoopUntil(5);
    } while (std::chrono::steady_clock::now() < deadline);
    return predicate();
}

class InstrumentPluginStubBackendCFT : public synth::DefaultHostedPluginBackend {
public:
    using synth::DefaultHostedPluginBackend::createInstanceAsync;

    // Keyed by juce::PluginDescription::name. No entry for a name means "this load fails" — the
    // FailedLoad test's whole point, and never needs a StubPluginInstance of its own.
    std::map<juce::String, std::function<std::unique_ptr<juce::AudioPluginInstance>()>> factories;
    juce::String failureError = "Plugin failed to load.";

    void createInstanceAsync(const juce::PluginDescription& description, double, int,
                             InstanceCallback callback) override {
        if (callback == nullptr)
            return;
        auto sharedCallback = std::make_shared<InstanceCallback>(std::move(callback));
        const auto it = factories.find(description.name);
        if (it == factories.end()) {
            const juce::String error = failureError;
            juce::MessageManager::callAsync([sharedCallback, error] { (*sharedCallback)(nullptr, error); });
            return;
        }
        auto factory = it->second;
        juce::MessageManager::callAsync([sharedCallback, factory] { (*sharedCallback)(factory(), juce::String()); });
    }
};

/** A minimal scanned-plugin description — `isInstrument` is the one field
 * PluginScanTests.cpp's own `descriptionXml()` helper never sets (it defaults false), and is
 * exactly what getInstrumentPluginOptions() filters on. `format`/`manufacturer` are deliberately
 * LAST and default to the common case (VST3, no manufacturer asserted on) — see FRO42 review
 * fixes: format disambiguation (PluginInstrumentMenuAppendsFormatLabel) and self-exclusion
 * (PluginInstrumentMenuExcludesOwnBuild) both need to set one or both explicitly. */
inline juce::PluginDescription pluginDescriptionCFT(const juce::String& name, int uid, bool isInstrument,
                                                    const juce::String& format = "VST3",
                                                    const juce::String& manufacturer = {}) {
    juce::PluginDescription description;
    description.name = name;
    description.pluginFormatName = format;
    description.uniqueId = uid;
    description.deprecatedUid = uid;
    description.manufacturerName = manufacturer;
    description.fileOrIdentifier = "/plugins/" + name + ".vst3";
    description.isInstrument = isInstrument;
    return description;
}

/** juce::KnownPluginList's own XML shape (PluginScanService::loadFromXml's input), preserving each
 *  description's isInstrument flag — see juce::PluginDescription::createXml/loadFromXml. */
inline juce::String knownPluginsXmlCFT(const std::vector<juce::PluginDescription>& descriptions) {
    juce::KnownPluginList list;
    for (const auto& description : descriptions)
        list.addType(description);
    auto xml = list.createXml();
    return xml != nullptr ? xml->toString() : juce::String();
}

/** Seeds `service` with `descriptions` the way a completed scan would, without running one. */
inline void seedScanListCFT(synth::PluginScanService& service,
                            const std::vector<juce::PluginDescription>& descriptions) {
    auto xml = juce::parseXML(knownPluginsXmlCFT(descriptions));
    ASSERT_NE(xml, nullptr);
    service.loadFromXml(*xml);
}

inline const juce::PopupMenu::Item* findMenuItemByTextCFT(const juce::PopupMenu& menu, const juce::String& text) {
    juce::PopupMenu::MenuItemIterator it(menu, true);
    while (it.next())
        if (it.getItem().text == text)
            return &it.getItem();
    return nullptr;
}

class ChannelFlowTest : public ::testing::Test {
protected:
    // Same settings-file hygiene as AudioClipPlaybackTests.cpp's AddAudioTrackFlowTest /
    // RecordTapTests.cpp / TimelinePanelTests.cpp: the delegating MainComponent ctor reads/writes
    // the shared on-disk "Agent Synth" settings, so pin the keys this flow depends on before AND
    // after every test.
    void resetKeys() {
        juce::PropertiesFile::Options opts;
        opts.applicationName = "Agent Synth";
        opts.folderName = "Agent Synth";
        opts.filenameSuffix = "settings";
        opts.osxLibrarySubFolder = "Application Support";
        opts.storageFormat = juce::PropertiesFile::storeAsXML;

        juce::ApplicationProperties props;
        props.setStorageParameters(opts);
        if (auto* s = props.getUserSettings()) {
            s->setValue("librarySidebarVisible", "1");
            s->setValue("aiPanelVisible", "0");
            s->setValue("minimapVisible", "1");
            s->setValue("timelinePanelVisible", "0");
            // T184: pinned ON (the default) so an earlier PreferencesSettingsTabTests run that
            // persisted "0" to this same shared on-disk settings file can't silently flip these
            // tests' trigger condition off.
            s->setValue("mixerAutoCreateChannelOnConnect", "1");
            // FRO42: the plugin-instrument tests below drive a REAL PluginScanService::ensureScanned()
            // through MainComponent, and a completed scan's pluginScanCompleted() unconditionally
            // persists the scan list (MainComponent::savePluginScanList()) to this SAME shared
            // on-disk file — same convention as PluginScanTests.cpp's PluginScanPersistenceTest::
            // clearScanList(). Left uncleared, a fake candidate path ("/plugins/Slow.vst3" etc.)
            // scanned once survives as a blacklist/known-plugin entry into every later run on this
            // machine, so a later test's "fresh" scan silently skips its own candidate as
            // already-known/blacklisted instead of actually invoking its child launcher.
            s->removeValue(MainComponent::kPluginScanListKey);
            s->saveIfNeeded();
        }
    }

    void SetUp() override { resetKeys(); }
    void TearDown() override { resetKeys(); }

    // The menu hook, not the async PopupMenu — the same headless seam the binding chip uses.
    static void addAudioTrack(MainComponent& mc) {
        mc.getTimelinePanel().applyAddTrackMenuChoice(synth::ui::TimelinePanelComponent::kAddAudioTrackMenuId);
    }

    // T183: the Instrument submenu's headless seam, keyed by module type name rather than the raw
    // menu id — matches how the picker itself is spelled everywhere else in this file. FRO48
    // (P9-3k): `poly` picks the "(Poly)" menu entry instead, for Oscillator/Wavetable (Sampler has
    // no poly parameter and no poly menu entry — see TimelinePanelComponent::openAddTrackMenu).
    static void addInstrumentTrack(MainComponent& mc, const juce::String& instrumentModuleType, bool poly = false) {
        int menuId = synth::ui::TimelinePanelComponent::kAddInstrumentSamplerMenuId;
        if (instrumentModuleType == "Oscillator")
            menuId = poly ? synth::ui::TimelinePanelComponent::kAddInstrumentOscillatorPolyMenuId
                          : synth::ui::TimelinePanelComponent::kAddInstrumentOscillatorMenuId;
        else if (instrumentModuleType == "Wavetable")
            menuId = poly ? synth::ui::TimelinePanelComponent::kAddInstrumentWavetablePolyMenuId
                          : synth::ui::TimelinePanelComponent::kAddInstrumentWavetableMenuId;
        mc.getTimelinePanel().applyAddTrackMenuChoice(menuId);
    }
};

// ============================================================================
// Real-mouse-gesture helpers (T184/FRO25): a hand-built MouseEvent and the module-component
// lookup they're driven against. dragRealMidiCableBetweenCFT is local to
// ChannelFlowAutoChannelTests.cpp (its only caller).
// ============================================================================

// mouseDownLocalPos is the FIXED press-point (local to eventComp), unchanged across an entire
// gesture; localPos is where the cursor is RIGHT NOW for this specific event. Same shape as
// MacroPortRealMouseDragTests.cpp's own helper — duplicated here since that one is scoped to its
// own translation unit's anonymous namespace.
inline juce::MouseEvent realMouseEventCFT(juce::Component& eventComp, juce::Point<int> localPos,
                                          juce::Point<int> mouseDownLocalPos, juce::ModifierKeys mods,
                                          bool wasDragged = false) {
    const auto pos = localPos.toFloat();
    const auto downPos = mouseDownLocalPos.toFloat();
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), pos, mods, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                            &eventComp, &eventComp, juce::Time::getCurrentTime(), downPos, juce::Time::getCurrentTime(),
                            1, wasDragged);
}

inline ModuleComponent* compForCFT(GraphEditor& editor, juce::AudioProcessorGraph::NodeID id) {
    for (auto* c : editor.getModuleComponents())
        if (c != nullptr && c->getNodeId() == id)
            return c;
    return nullptr;
}

// Adds one node through the factory, mirrors a fresh uuid into BOTH the node property and the
// processor (ModuleBase::setNodeUuid), and records its canvas position. Returns nullptr on a
// factory/addNode failure. Deliberately bypasses GraphEditor/AppUndoManager entirely.
// Adds one node through the factory, mirrors a fresh uuid into BOTH the node property and the
// processor (ModuleBase::setNodeUuid — Source/CLAUDE.md's uuid-mirroring invariant), and records
// its canvas position. Returns nullptr on a factory/addNode failure. Deliberately bypasses
// GraphEditor/AppUndoManager entirely — these tests build their "instrument already wired to the
// output, by hand" starting state the same non-undoable way the Core-level tests above do.
inline juce::AudioProcessorGraph::Node* addPlainNodeCFT(juce::AudioProcessorGraph& graph, const juce::String& typeName,
                                                        juce::Point<int> position, juce::String& uuidOut) {
    auto processor = synth::AIStateMapper::createModule(typeName);
    if (processor == nullptr)
        return nullptr;
    auto node = graph.addNode(std::move(processor));
    if (node == nullptr)
        return nullptr;
    const juce::String uuid = juce::Uuid().toDashedString();
    node->properties.set("uuid", uuid);
    if (auto* module = dynamic_cast<ModuleBase*>(node->getProcessor()))
        module->setNodeUuid(uuid);
    node->properties.set("x", position.x);
    node->properties.set("y", position.y);
    uuidOut = uuid;
    return node.get();
}

// ============================================================================
// FRO25 "Make channel" rig helpers: the legacy (pre-P9-3) two-track patch, a Hosted render-identity
// harness, and the snapshot/undo-step assertions shared by ChannelFlowMakeChannelCoreTests.cpp and
// ChannelFlowMakeChannelAppTests.cpp.
// ============================================================================

constexpr double kSampleRateMCH = 44100.0;
constexpr int kBlockSizeMCH = 256;

enum class RigShapeCFT { SharedLfo, Merge };

// A legacy (pre-P9-3) two-track patch, by hand, with no channel anywhere:
//   SharedLfo: Track In A -> Osc A -> Filter A -> Audio Output, the same for B, ONE LFO modulating
//              BOTH filters' cutoff (shared) and a second LFO modulating only Filter A (exclusive).
//   Merge:     Track In A -> Osc A -> Filter A -> Filter M -> Audio Output, Track In B -> Osc B ->
//              Filter M — Filter M (`filterB`) is the merge point both tracks feed.
struct LegacyRigCFT {
    juce::AudioProcessorGraph::Node* output = nullptr;
    juce::AudioProcessorGraph::Node* trackInA = nullptr;
    juce::AudioProcessorGraph::Node* oscA = nullptr;
    juce::AudioProcessorGraph::Node* filterA = nullptr;
    juce::AudioProcessorGraph::Node* trackInB = nullptr;
    juce::AudioProcessorGraph::Node* oscB = nullptr;
    juce::AudioProcessorGraph::Node* filterB = nullptr;
    juce::AudioProcessorGraph::Node* sharedLfo = nullptr;
    juce::AudioProcessorGraph::Node* ownLfo = nullptr;
    juce::String trackInAUuid, trackInBUuid;
    int cutoffChannel = -1;
};

inline int cutoffChannelCFT(juce::AudioProcessorGraph::Node* filter) {
    if (auto* module = dynamic_cast<ModuleBase*>(filter->getProcessor()))
        for (const auto& target : module->getModulationTargets())
            return target.channelIndex;
    return -1;
}

inline LegacyRigCFT buildLegacyRigCFT(AudioEngine& engine, juce::AudioProcessorGraph::Node* output, RigShapeCFT shape) {
    auto& graph = engine.getGraph();
    constexpr int midi = juce::AudioProcessorGraph::midiChannelIndex;
    LegacyRigCFT rig;
    rig.output = output;
    juce::String unused;
    rig.trackInA = addPlainNodeCFT(graph, "Track In", {0, 0}, rig.trackInAUuid);
    rig.oscA = addPlainNodeCFT(graph, "Oscillator", {200, 0}, unused);
    rig.filterA = addPlainNodeCFT(graph, "Filter", {500, 0}, unused);
    rig.trackInB = addPlainNodeCFT(graph, "Track In", {0, 400}, rig.trackInBUuid);
    rig.oscB = addPlainNodeCFT(graph, "Oscillator", {200, 400}, unused);
    rig.filterB = addPlainNodeCFT(graph, "Filter", {500, 400}, unused);
    rig.cutoffChannel = cutoffChannelCFT(rig.filterA);

    graph.addConnection({{rig.trackInA->nodeID, midi}, {rig.oscA->nodeID, midi}});
    graph.addConnection({{rig.trackInB->nodeID, midi}, {rig.oscB->nodeID, midi}});
    // Stereo legs paired via rightAudioLegChannel(), never ch1 (Source/Modules/CLAUDE.md): both
    // Oscillator and Filter keep Audio R on a split block, and Filter's ch1 is its Cutoff CV.
    const int oscRight = dynamic_cast<ModuleBase*>(rig.oscA->getProcessor())->rightAudioLegChannel();
    const int filterRight = dynamic_cast<ModuleBase*>(rig.filterA->getProcessor())->rightAudioLegChannel();
    for (const auto [oscLeg, filterLeg, outLeg] : {std::array<int, 3>{0, 0, 0}, {oscRight, filterRight, 1}}) {
        graph.addConnection({{rig.oscA->nodeID, oscLeg}, {rig.filterA->nodeID, filterLeg}});
        graph.addConnection({{rig.oscB->nodeID, oscLeg}, {rig.filterB->nodeID, filterLeg}});
        graph.addConnection({{rig.filterB->nodeID, filterLeg}, {output->nodeID, outLeg}});
        if (shape == RigShapeCFT::SharedLfo)
            graph.addConnection({{rig.filterA->nodeID, filterLeg}, {output->nodeID, outLeg}});
        else
            graph.addConnection({{rig.filterA->nodeID, filterLeg}, {rig.filterB->nodeID, filterLeg}});
    }

    if (shape == RigShapeCFT::SharedLfo) {
        rig.sharedLfo = addPlainNodeCFT(graph, "LFO", {0, 200}, unused);
        rig.ownLfo = addPlainNodeCFT(graph, "LFO", {0, 650}, unused);
        engine.addModRouting(rig.sharedLfo->nodeID, 0, rig.filterA->nodeID, rig.cutoffChannel);
        engine.addModRouting(rig.sharedLfo->nodeID, 0, rig.filterB->nodeID, rig.cutoffChannel);
        engine.addModRouting(rig.ownLfo->nodeID, 0, rig.filterA->nodeID, rig.cutoffChannel);
    }
    return rig;
}

// A Hosted engine (no device) with its Audio Output node, rendered offline via processHostBlock —
// MixerSoloTests.cpp's SoloRig shape.
struct HostedPatchCFT {
    AudioEngine engine{AudioEngine::HostMode::Hosted};
    juce::AudioProcessorGraph::Node* output = nullptr;
    bool prepared = false;

    HostedPatchCFT() {
        engine.getGraph().setPlayConfigDetails(2, 2, kSampleRateMCH, kBlockSizeMCH);
        output = engine.getGraph().addNode(synth::AIStateMapper::createModule("Audio Output")).get();
    }
    ~HostedPatchCFT() {
        if (prepared)
            engine.releaseFromHost();
    }
    std::vector<float> render(int blocks) {
        if (!prepared) {
            engine.prepareForHost(kSampleRateMCH, kBlockSizeMCH, 2, 2);
            prepared = true;
        }
        std::vector<float> samples;
        for (int block = 0; block < blocks; ++block) {
            juce::AudioBuffer<float> buffer(2, kBlockSizeMCH);
            buffer.clear();
            juce::MidiBuffer midi;
            engine.processHostBlock(buffer, midi);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < kBlockSizeMCH; ++i)
                    samples.push_back(buffer.getSample(ch, i));
        }
        return samples;
    }
};

// Bypassed EQ/Compressor are dry, zero-latency pass-throughs; Channel Strip and Master sit at unity
// by default; a macro port is a pure per-channel pass-through — so the converted render must match
// the legacy one sample for sample, not merely approximately.
inline void expectIdenticalRendersCFT(const std::vector<float>& reference, const std::vector<float>& converted) {
    ASSERT_EQ(reference.size(), converted.size());
    double energy = 0.0;
    float maxDiff = 0.0f;
    for (size_t i = 0; i < reference.size(); ++i) {
        energy += static_cast<double>(reference[i]) * reference[i];
        maxDiff = std::max(maxDiff, std::abs(reference[i] - converted[i]));
    }
    EXPECT_GT(energy, 1.0e-3) << "the reference render must not be silent, or the comparison proves nothing";
    EXPECT_LE(maxDiff, 1.0e-6f) << "Make channel must not change the sound";
}

inline juce::AudioProcessorGraph::Node* nodeForUuidCFT(juce::AudioProcessorGraph& graph, const juce::String& uuid) {
    for (auto* node : graph.getNodes())
        if (node != nullptr && node->properties["uuid"].toString() == uuid)
            return node;
    return nullptr;
}

inline bool isModuleOfTypeCFT(juce::AudioProcessorGraph::Node* node, ModuleType type) {
    auto* module = node != nullptr ? dynamic_cast<ModuleBase*>(node->getProcessor()) : nullptr;
    return module != nullptr && module->getModuleType() == type;
}

// True when `source` modulates `dest`'s `channel` through a hidden attenuverter — straight, or with
// a macro inlet port between the attenuverter and `dest`.
inline bool modulatesCFT(juce::AudioProcessorGraph& graph, juce::AudioProcessorGraph::Node* source,
                         juce::AudioProcessorGraph::Node* dest, int channel) {
    const auto connections = graph.getConnections();
    for (const auto& in : connections) {
        if (in.source.nodeID != source->nodeID ||
            !isModuleOfTypeCFT(graph.getNodeForId(in.destination.nodeID), ModuleType::Attenuverter))
            continue;
        for (const auto& out : connections) {
            if (out.source.nodeID != in.destination.nodeID)
                continue;
            if (out.destination.nodeID == dest->nodeID && out.destination.channelIndex == channel)
                return true;
            if (isModuleOfTypeCFT(graph.getNodeForId(out.destination.nodeID), ModuleType::MacroInlet))
                for (const auto& viaPort : connections)
                    if (viaPort.source.nodeID == out.destination.nodeID && viaPort.destination.nodeID == dest->nodeID &&
                        viaPort.destination.channelIndex == channel)
                        return true;
        }
    }
    return false;
}

struct McRigCFT {
    LegacyRigCFT rig;
    synth::TrackId trackA, trackB;
};

// The same legacy patch inside a real MainComponent, each Track In bound to its own track.
inline McRigCFT buildMcRigCFT(MainComponent& mc, RigShapeCFT shape) {
    McRigCFT setup;
    auto& graph = mc.getAudioEngine().getGraph();
    auto* output = findNodeNamedCFT(graph, "Audio Output");
    if (output == nullptr)
        return setup;
    // Park Audio Output bottom-left so the canvas's top-right corner stays empty of cards and cables
    // (the canvas right-click test below clicks there).
    output->properties.set("x", 0);
    output->properties.set("y", 900);
    setup.rig = buildLegacyRigCFT(mc.getAudioEngine(), output, shape);
    auto& doc = mc.getTimelineDoc();
    setup.trackA = doc.addTrack(synth::TrackKind::Midi, "Lead");
    setup.trackB = doc.addTrack(synth::TrackKind::Midi, "Pad");
    doc.setTrackBinding(setup.trackA, setup.rig.trackInAUuid);
    doc.setTrackBinding(setup.trackB, setup.rig.trackInBUuid);
    mc.getGraphEditor().updateComponents();
    return setup;
}

inline synth::ui::TimelineTrackHeaderComponent* headerForCFT(MainComponent& mc, synth::TrackId track) {
    auto& panel = mc.getTimelinePanel();
    for (int i = 0; i < panel.getTrackHeaderCount(); ++i)
        if (auto* header = panel.getTrackHeaderAt(i); header != nullptr && header->getTrackId() == track)
            return header;
    return nullptr;
}

// A real right-click on the track header — mouseDown() builds the context menu and hands it to the
// test hook instead of showing it.
inline juce::PopupMenu rightClickHeaderMenuCFT(synth::ui::TimelineTrackHeaderComponent& header) {
    juce::PopupMenu captured;
    header.setShowContextMenuHookForTest([&captured](juce::PopupMenu& menu) { captured = menu; });
    const juce::Point<int> point(4, 4);
    header.mouseDown(
        realMouseEventCFT(header, point, point, juce::ModifierKeys(juce::ModifierKeys::rightButtonModifier)));
    header.setShowContextMenuHookForTest(nullptr);
    return captured;
}

inline juce::PopupMenu rightClickModuleMenuCFT(ModuleComponent& comp) {
    juce::PopupMenu captured;
    comp.setShowContextMenuHookForTest([&captured](juce::PopupMenu& menu) { captured = menu; });
    const juce::Point<int> body(comp.getWidth() / 2, comp.getHeight() - 10);
    comp.mouseDown(realMouseEventCFT(comp, body, body, juce::ModifierKeys(juce::ModifierKeys::rightButtonModifier)));
    return captured;
}

// Chooses "Make Channel" from `track`'s header menu (asserting it is offered and enabled).
inline void makeChannelFromHeaderCFT(MainComponent& mc, synth::TrackId track) {
    auto* header = headerForCFT(mc, track);
    ASSERT_NE(header, nullptr);
    const auto menu = rightClickHeaderMenuCFT(*header);
    const auto* item = findMenuItemByTextCFT(menu, "Make Channel");
    ASSERT_NE(item, nullptr) << "the track header menu must offer Make Channel";
    EXPECT_TRUE(item->isEnabled);
    header->applyContextMenuChoice(item->itemID);
}

struct SnapshotCFT {
    juce::String graph, doc, macros;
};

inline SnapshotCFT snapshotCFT(MainComponent& mc) {
    return {juce::JSON::toString(synth::AIStateMapper::graphToJSON(mc.getAudioEngine().getGraph())),
            juce::JSON::toString(mc.getTimelineDoc().toVar()),
            juce::JSON::toString(mc.getGraphEditor().getMacros().toVar())};
}

inline void expectSameSnapshotCFT(const SnapshotCFT& actual, const SnapshotCFT& expected) {
    EXPECT_EQ(actual.graph, expected.graph);
    EXPECT_EQ(actual.doc, expected.doc);
    EXPECT_EQ(actual.macros, expected.macros);
}

// One undo fully reverts the action to `before`; redo restores `after`.
inline void expectOneUndoStepCFT(MainComponent& mc, const SnapshotCFT& before) {
    const auto after = snapshotCFT(mc);
    EXPECT_NE(after.graph, before.graph) << "the action must have changed the graph";
    ASSERT_TRUE(mc.getUndoManager().undo());
    expectSameSnapshotCFT(snapshotCFT(mc), before);
    ASSERT_TRUE(mc.getUndoManager().redo());
    expectSameSnapshotCFT(snapshotCFT(mc), after);
}
