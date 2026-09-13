// ChannelFlowTests.cpp
//
// T173a: "+ Track -> Audio Track" now builds a WHOLE mixer channel in one undo step —
//
//     Track Audio -> Parametric EQ (bypassed) -> Compressor (bypassed) -> Channel Strip (Stereo)
//                 -> Master (Mix)
//
// with {Track Audio, EQ, Compressor, Strip} boxed into ONE collapsed macro named after the track.
// Master stays OUTSIDE the macro and the Strip -> Master cable is a plain graph edge, never a macro
// port — see MainComponent::addAudioTrack's own comment and Source/Mixer/ChannelFlows.h for why.
//
// Drives the flow through a real MainComponent via the same headless seam
// AudioClipPlaybackTests.cpp's AddAudioTrackFlowTest uses (TimelinePanelComponent's
// applyAddTrackMenuChoice), so these tests exercise the whole app wiring, not just
// synth::buildDefaultAudioChannel in isolation.

#include "../Source/AI/AIProvider.h"
#include "../Source/AI/AIStateMapper.h"
#include "../Source/AudioEngine.h"
#include "../Source/Branding.h"
#include "../Source/MacroSet.h"
#include "../Source/Mixer/ChannelFlows.h"
#include "../Source/Mixer/MasterSplice.h"
#include "../Source/Modules/ChannelStripModule.h"
#include "../Source/Modules/MasterModule.h"
#include "../Source/Modules/ModuleBase.h"
#include "../Source/Modules/VCAModule.h"
#include "../Source/Plugin/Hosting/HostedPluginModule.h"
#include "../Source/Plugin/Hosting/PluginScanService.h"
#include "../Source/Timeline/TimelineDoc.h"
#include "../Source/UI/GraphEditor/GraphEditor.h"
#include "../Source/UI/ModuleComponent.h"
#include "../Source/UI/ModuleLibraryComponent.h"
#include "../Source/UI/TimelineTrackHeaderComponent.h"
#include "MainComponent/MainComponent.h"
#include "StubPluginInstance.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <gtest/gtest.h>
#include <map>
#include <memory>
#include <thread>

namespace {

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

int countNodesOfTypeCFT(juce::AudioProcessorGraph& graph, ModuleType type) {
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
juce::AudioProcessorGraph::Node* findNodeOfTypeCFT(juce::AudioProcessorGraph& graph, ModuleType type) {
    juce::AudioProcessorGraph::Node* found = nullptr;
    for (auto* node : graph.getNodes())
        if (node != nullptr)
            if (auto* module = dynamic_cast<ModuleBase*>(node->getProcessor()))
                if (module->getModuleType() == type)
                    found = node;
    return found;
}

juce::AudioProcessorGraph::Node* findNodeNamedCFT(juce::AudioProcessorGraph& graph, const juce::String& name) {
    for (auto* node : graph.getNodes())
        if (node != nullptr && node->getProcessor() != nullptr && node->getProcessor()->getName() == name)
            return node;
    return nullptr;
}

juce::String nodeUuid(juce::AudioProcessorGraph::Node* node) {
    return node != nullptr ? node->properties["uuid"].toString() : juce::String();
}

// The factory default preset (PresetManager::getPresetJSON(0), loaded by every fresh MainComponent)
// already contains its own ADSR-type nodes (Amp Env, Filter Env) and a VCA node, so
// findNodeOfTypeCFT's "last one seen" is not a safe way to find the ONE this test's own
// addInstrumentTrack call just created. Disambiguates by macro membership instead — the track's
// macro is freshly built and contains only this track's own nodes.
juce::AudioProcessorGraph::Node* findMacroMemberOfTypeCFT(juce::AudioProcessorGraph& graph, const synth::Macro& macro,
                                                          ModuleType type) {
    for (auto* node : graph.getNodes())
        if (node != nullptr)
            if (auto* module = dynamic_cast<ModuleBase*>(node->getProcessor()))
                if (module->getModuleType() == type && macro.hasMember(nodeUuid(node)))
                    return node;
    return nullptr;
}

// T183: flips a module's "poly" AudioParameterBool, when it has one. No-op (returns false) for
// Sampler, which has no poly parameter at all.
bool setPolyParamCFT(juce::AudioProcessor* processor, bool poly) {
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
juce::PluginDescription pluginDescriptionCFT(const juce::String& name, int uid, bool isInstrument,
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
juce::String knownPluginsXmlCFT(const std::vector<juce::PluginDescription>& descriptions) {
    juce::KnownPluginList list;
    for (const auto& description : descriptions)
        list.addType(description);
    auto xml = list.createXml();
    return xml != nullptr ? xml->toString() : juce::String();
}

/** Seeds `service` with `descriptions` the way a completed scan would, without running one. */
void seedScanListCFT(synth::PluginScanService& service, const std::vector<juce::PluginDescription>& descriptions) {
    auto xml = juce::parseXML(knownPluginsXmlCFT(descriptions));
    ASSERT_NE(xml, nullptr);
    service.loadFromXml(*xml);
}

const juce::PopupMenu::Item* findMenuItemByTextCFT(const juce::PopupMenu& menu, const juce::String& text) {
    juce::PopupMenu::MenuItemIterator it(menu, true);
    while (it.next())
        if (it.getItem().text == text)
            return &it.getItem();
    return nullptr;
}

} // namespace

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

TEST_F(ChannelFlowTest, AudioTrackBuildsDefaultChannel) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    auto& graph = mc.getAudioEngine().getGraph();

    addAudioTrack(mc);

    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::TimelineAudioSource), 1);
    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::ParametricEQ), 1);
    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::Compressor), 1);
    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::ChannelStrip), 1);
    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::Master), 1);

    auto* trackAudio = findNodeOfTypeCFT(graph, ModuleType::TimelineAudioSource);
    auto* eq = findNodeOfTypeCFT(graph, ModuleType::ParametricEQ);
    auto* comp = findNodeOfTypeCFT(graph, ModuleType::Compressor);
    auto* strip = findNodeOfTypeCFT(graph, ModuleType::ChannelStrip);
    auto* master = findNodeOfTypeCFT(graph, ModuleType::Master);
    ASSERT_NE(trackAudio, nullptr);
    ASSERT_NE(eq, nullptr);
    ASSERT_NE(comp, nullptr);
    ASSERT_NE(strip, nullptr);
    ASSERT_NE(master, nullptr);

    EXPECT_TRUE(graph.isConnected({{trackAudio->nodeID, 0}, {eq->nodeID, 0}}));
    EXPECT_TRUE(graph.isConnected({{trackAudio->nodeID, 1}, {eq->nodeID, 1}}));
    EXPECT_TRUE(graph.isConnected({{eq->nodeID, 0}, {comp->nodeID, 0}}));
    EXPECT_TRUE(graph.isConnected({{eq->nodeID, 1}, {comp->nodeID, 1}}));
    EXPECT_TRUE(graph.isConnected({{comp->nodeID, 0}, {strip->nodeID, 0}}));
    EXPECT_TRUE(graph.isConnected({{comp->nodeID, 1}, {strip->nodeID, ChannelStripModule::kRightBase}}))
        << "the right leg must land on kRightBase (4)";
    EXPECT_FALSE(graph.isConnected({{comp->nodeID, 1}, {strip->nodeID, 1}})) << "never ch1 for the right leg";

    EXPECT_TRUE(graph.isConnected({{strip->nodeID, 0}, {master->nodeID, MasterModule::kMixLeft}}));
    EXPECT_TRUE(graph.isConnected(
        {{strip->nodeID, ChannelStripModule::kRightBase}, {master->nodeID, MasterModule::kMixRight}}));

    auto* output = findNodeNamedCFT(graph, "Audio Output");
    ASSERT_NE(output, nullptr);
    EXPECT_TRUE(graph.isConnected({{master->nodeID, 0}, {output->nodeID, 0}}));
    EXPECT_TRUE(graph.isConnected({{master->nodeID, 1}, {output->nodeID, 1}}));

    EXPECT_FALSE(graph.isConnected({{trackAudio->nodeID, 0}, {output->nodeID, 0}}))
        << "Track Audio must no longer wire straight to the output";
    EXPECT_FALSE(graph.isConnected({{trackAudio->nodeID, 1}, {output->nodeID, 1}}));
}

TEST_F(ChannelFlowTest, DefaultInsertsAreBypassedAndStripIsStereo) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    auto& graph = mc.getAudioEngine().getGraph();

    addAudioTrack(mc);

    auto* eqNode = findNodeOfTypeCFT(graph, ModuleType::ParametricEQ);
    auto* compNode = findNodeOfTypeCFT(graph, ModuleType::Compressor);
    auto* stripNode = findNodeOfTypeCFT(graph, ModuleType::ChannelStrip);
    ASSERT_NE(eqNode, nullptr);
    ASSERT_NE(compNode, nullptr);
    ASSERT_NE(stripNode, nullptr);

    auto* eqModule = dynamic_cast<ModuleBase*>(eqNode->getProcessor());
    auto* compModule = dynamic_cast<ModuleBase*>(compNode->getProcessor());
    auto* stripModule = dynamic_cast<ChannelStripModule*>(stripNode->getProcessor());
    ASSERT_NE(eqModule, nullptr);
    ASSERT_NE(compModule, nullptr);
    ASSERT_NE(stripModule, nullptr);

    EXPECT_TRUE(eqModule->isBypassed());
    EXPECT_TRUE(compModule->isBypassed());
    EXPECT_FALSE(stripModule->isBypassed());
    EXPECT_EQ(stripModule->getShape(), ChannelStripModule::Shape::Stereo);
}

TEST_F(ChannelFlowTest, ChannelIsOneCollapsedMacroNamedAfterTrack) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    auto& graph = mc.getAudioEngine().getGraph();
    auto& doc = mc.getTimelineDoc();
    auto& macros = mc.getGraphEditor().getMacros();

    addAudioTrack(mc);

    ASSERT_EQ(macros.size(), 1);
    const auto& macro = macros.getAll().front();
    ASSERT_EQ(doc.getTracks().size(), 1u);
    const auto& track = doc.getTracks().back();

    EXPECT_EQ(macro.name, track.name);
    EXPECT_TRUE(macro.collapsed);
    EXPECT_EQ(track.bindingUuid, nodeUuid(findNodeOfTypeCFT(graph, ModuleType::TimelineAudioSource)));

    std::vector<juce::String> expected = {nodeUuid(findNodeOfTypeCFT(graph, ModuleType::TimelineAudioSource)),
                                          nodeUuid(findNodeOfTypeCFT(graph, ModuleType::ParametricEQ)),
                                          nodeUuid(findNodeOfTypeCFT(graph, ModuleType::Compressor)),
                                          nodeUuid(findNodeOfTypeCFT(graph, ModuleType::ChannelStrip))};
    auto actual = macro.members;
    std::sort(expected.begin(), expected.end());
    std::sort(actual.begin(), actual.end());
    EXPECT_EQ(actual, expected);

    EXPECT_FALSE(macro.hasMember(nodeUuid(findNodeOfTypeCFT(graph, ModuleType::Master))))
        << "Master must stay outside the macro";
}

TEST_F(ChannelFlowTest, OneUndoStepRevertsEverythingAndRedoRestores) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    auto& graph = mc.getAudioEngine().getGraph();
    auto& doc = mc.getTimelineDoc();
    auto& macros = mc.getGraphEditor().getMacros();

    const juce::String graphBefore = juce::JSON::toString(synth::AIStateMapper::graphToJSON(graph));
    const juce::String docBefore = juce::JSON::toString(doc.toVar());
    const juce::String macrosBefore = juce::JSON::toString(macros.toVar());

    addAudioTrack(mc);
    ASSERT_EQ(countNodesOfTypeCFT(graph, ModuleType::ChannelStrip), 1) << "the channel must have been built";
    const juce::String graphAfter = juce::JSON::toString(synth::AIStateMapper::graphToJSON(graph));
    const juce::String docAfter = juce::JSON::toString(doc.toVar());
    const juce::String macrosAfter = juce::JSON::toString(macros.toVar());

    ASSERT_TRUE(mc.getUndoManager().canUndo());
    ASSERT_TRUE(mc.getUndoManager().undo());
    EXPECT_EQ(juce::JSON::toString(synth::AIStateMapper::graphToJSON(graph)), graphBefore);
    EXPECT_EQ(juce::JSON::toString(doc.toVar()), docBefore);
    EXPECT_EQ(juce::JSON::toString(macros.toVar()), macrosBefore);
    EXPECT_FALSE(mc.getUndoManager().canUndo()) << "the whole channel was ONE undo step";

    ASSERT_TRUE(mc.getUndoManager().redo());
    EXPECT_EQ(juce::JSON::toString(synth::AIStateMapper::graphToJSON(graph)), graphAfter);
    EXPECT_EQ(juce::JSON::toString(doc.toVar()), docAfter);
    EXPECT_EQ(juce::JSON::toString(macros.toVar()), macrosAfter);
}

TEST_F(ChannelFlowTest, SecondAudioTrackReusesMaster) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    auto& graph = mc.getAudioEngine().getGraph();
    auto& macros = mc.getGraphEditor().getMacros();

    addAudioTrack(mc);
    addAudioTrack(mc);

    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::Master), 1) << "Master is a singleton";
    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::ChannelStrip), 2);
    EXPECT_EQ(macros.size(), 2);

    auto* master = findNodeOfTypeCFT(graph, ModuleType::Master);
    ASSERT_NE(master, nullptr);
    int stripsIntoMix = 0;
    for (auto* node : graph.getNodes()) {
        if (node == nullptr)
            continue;
        auto* module = dynamic_cast<ModuleBase*>(node->getProcessor());
        if (module == nullptr || module->getModuleType() != ModuleType::ChannelStrip)
            continue;
        if (graph.isConnected({{node->nodeID, 0}, {master->nodeID, MasterModule::kMixLeft}}) &&
            graph.isConnected(
                {{node->nodeID, ChannelStripModule::kRightBase}, {master->nodeID, MasterModule::kMixRight}}))
            ++stripsIntoMix;
    }
    EXPECT_EQ(stripsIntoMix, 2) << "both strips must land on Mix";

    // One undo removes only the SECOND channel; Master (and the first channel) remain.
    ASSERT_TRUE(mc.getUndoManager().canUndo());
    ASSERT_TRUE(mc.getUndoManager().undo());
    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::ChannelStrip), 1);
    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::Master), 1);
    EXPECT_EQ(macros.size(), 1);
}

// T187: before this fix, GraphEditor::newPatch() left the graph with zero nodes, so
// synth::spliceMasterNode (called from buildDefaultAudioChannel) had no Audio Output to target
// and silently returned nullptr — a bare Track Audio in a brand-new project was unheard until the
// user manually added an Audio Output. newPatch() now seeds one as part of the same undo step.
TEST_F(ChannelFlowTest, AudioTrackOnFreshNewPatchGetsMaster) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    auto& graph = mc.getAudioEngine().getGraph();

    mc.newPatchForTest();
    ASSERT_NE(findNodeNamedCFT(graph, "Audio Output"), nullptr) << "newPatch must seed an Audio Output";

    addAudioTrack(mc);

    auto* strip = findNodeOfTypeCFT(graph, ModuleType::ChannelStrip);
    auto* master = findNodeOfTypeCFT(graph, ModuleType::Master);
    ASSERT_NE(strip, nullptr);
    ASSERT_NE(master, nullptr) << "the first channel must splice Master immediately, not need a manual Audio Output";

    EXPECT_TRUE(graph.isConnected({{strip->nodeID, 0}, {master->nodeID, MasterModule::kMixLeft}}));
    EXPECT_TRUE(graph.isConnected(
        {{strip->nodeID, ChannelStripModule::kRightBase}, {master->nodeID, MasterModule::kMixRight}}));

    auto* output = findNodeNamedCFT(graph, "Audio Output");
    ASSERT_NE(output, nullptr);
    EXPECT_TRUE(graph.isConnected({{master->nodeID, 0}, {output->nodeID, 0}}));
    EXPECT_TRUE(graph.isConnected({{master->nodeID, 1}, {output->nodeID, 1}}));

    // T187 layout follow-up: a bare Audio Output left at the newPatch seed's canvas origin while
    // Master lands right of the freshly-built chain read as backwards wiring on screen (Master "on
    // the right", Audio Output "top left" with a cable snaking back across everything to reach it —
    // caught live via computer-use testing). addAudioTrack now relocates the seeded Audio Output to
    // sit right of Master once Master is first spliced, so the row reads left to right.
    const int outputX = static_cast<int>(output->properties.getWithDefault("x", 0));
    const int masterX = static_cast<int>(master->properties.getWithDefault("x", 0));
    EXPECT_GT(outputX, masterX) << "Audio Output must be relocated to terminate the row after Master, "
                                   "not left behind at the newPatch seed position";
}

// Same pinning pattern as AudioClipPlaybackTest.AbsentFromTheLibraryWithAPinnedSizeEstimate /
// RecordTapTest's own — but for two cards at once, since both were missing an estimateModuleSize
// entry (silently falling back to the generic {280, 360} default, which is wrong for either card
// and was part of why Master ended up hidden under the EQ card — see this file's header comment).
// The strip is measured Stereo, matching what buildDefaultAudioChannel always builds; width does
// not move between Mono/Stereo (only the input jack-row count would), so this also stands in for
// the Mono shape.
TEST_F(ChannelFlowTest, ChannelStripAndMasterHaveAPinnedSizeEstimate) {
    ModuleLibraryComponent library;
    EXPECT_FALSE(library.getDraggableModuleNames().contains("Channel Strip"))
        << "Channel Strip is internal-only and must stay out of the module library";
    EXPECT_FALSE(library.getDraggableModuleNames().contains("Master"))
        << "Master is internal-only and must stay out of the module library";

    AudioEngine engine;
    GraphEditor editor(engine);

    auto stripProcessor = synth::AIStateMapper::createModule("Channel Strip");
    ASSERT_NE(stripProcessor, nullptr);
    if (auto* stripModule = dynamic_cast<ChannelStripModule*>(stripProcessor.get()))
        stripModule->setShape(ChannelStripModule::Shape::Stereo);
    ModuleComponent stripComp(stripProcessor.get(), juce::AudioProcessorGraph::NodeID(1), editor);
    const auto stripEstimate = GraphEditor::estimateModuleSize("Channel Strip");
    EXPECT_EQ(stripEstimate.x, stripComp.getWidth());
    EXPECT_EQ(stripEstimate.y, stripComp.getHeight());

    auto masterProcessor = synth::AIStateMapper::createModule("Master");
    ASSERT_NE(masterProcessor, nullptr);
    ModuleComponent masterComp(masterProcessor.get(), juce::AudioProcessorGraph::NodeID(2), editor);
    const auto masterEstimate = GraphEditor::estimateModuleSize("Master");
    EXPECT_EQ(masterEstimate.x, masterComp.getWidth());
    EXPECT_EQ(masterEstimate.y, masterComp.getHeight());
}

// The bug this file's header comment describes, reproduced against the REAL ModuleComponent
// bounds: on the old fixed-300px stride, Parametric EQ's double-width (560px) card overlapped the
// Compressor, and Master (placed at trackAudioPosition + kSingleWidth + gap, i.e. still inside the
// expanded chain) landed underneath the EQ card too. addAudioTrack now derives every card's x from
// GraphEditor::estimateModuleSize, so none of the five cards below should overlap and Master should
// sit to the right of everything else.
TEST_F(ChannelFlowTest, ChannelCardsDoNotOverlapAndMasterIsRightOfStrip) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(2400, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    auto& graph = mc.getAudioEngine().getGraph();
    auto& macros = mc.getGraphEditor().getMacros();

    addAudioTrack(mc);

    ASSERT_EQ(macros.size(), 1);
    const auto macroId = macros.getAll().front().id;

    // Expand the macro through the same API the "Expand" menu item and the collapsed card's own
    // click use (GraphEditor::setMacroCollapsed) so member ModuleComponents are laid out for real
    // (applyMacroCollapsed calls updateComponents()) rather than inferring bounds ourselves.
    mc.getGraphEditor().setMacroCollapsed(macroId, false);
    ASSERT_FALSE(macros.find(macroId)->collapsed);

    auto* trackAudioNode = findNodeOfTypeCFT(graph, ModuleType::TimelineAudioSource);
    auto* eqNode = findNodeOfTypeCFT(graph, ModuleType::ParametricEQ);
    auto* compNode = findNodeOfTypeCFT(graph, ModuleType::Compressor);
    auto* stripNode = findNodeOfTypeCFT(graph, ModuleType::ChannelStrip);
    auto* masterNode = findNodeOfTypeCFT(graph, ModuleType::Master);
    ASSERT_NE(trackAudioNode, nullptr);
    ASSERT_NE(eqNode, nullptr);
    ASSERT_NE(compNode, nullptr);
    ASSERT_NE(stripNode, nullptr);
    ASSERT_NE(masterNode, nullptr);

    auto findComp = [&mc](juce::AudioProcessorGraph::Node* node) -> ModuleComponent* {
        for (auto* comp : mc.getGraphEditor().getModuleComponents())
            if (comp != nullptr && comp->getNodeId() == node->nodeID)
                return comp;
        return nullptr;
    };

    auto* trackAudioComp = findComp(trackAudioNode);
    auto* eqComp = findComp(eqNode);
    auto* compComp = findComp(compNode);
    auto* stripComp = findComp(stripNode);
    auto* masterComp = findComp(masterNode);
    // All five nodes are ordinary graph nodes with their own ModuleComponent (a hidden macro
    // member's component still exists — only setVisible(false) — and expanding just flips that
    // back on), so every lookup above must resolve; a silent nullptr here would make the
    // assertions below pass vacuously.
    ASSERT_NE(trackAudioComp, nullptr) << "Track Audio must have a real ModuleComponent once expanded";
    ASSERT_NE(eqComp, nullptr) << "Parametric EQ must have a real ModuleComponent once expanded";
    ASSERT_NE(compComp, nullptr) << "Compressor must have a real ModuleComponent once expanded";
    ASSERT_NE(stripComp, nullptr) << "Channel Strip must have a real ModuleComponent once expanded";
    ASSERT_NE(masterComp, nullptr) << "Master must have a real ModuleComponent (it is never boxed into the macro)";

    // Anchor the coordinate space once: content-component bounds should track the node's own
    // "x"/"y" properties directly (no zoom/scroll in a freshly-built headless MainComponent), so a
    // mismatch here means the two are in different coordinate spaces rather than a real overlap.
    EXPECT_EQ(trackAudioComp->getX(), static_cast<int>(trackAudioNode->properties.getWithDefault("x", -1)));
    EXPECT_EQ(trackAudioComp->getY(), static_cast<int>(trackAudioNode->properties.getWithDefault("y", -1)));

    const std::array<ModuleComponent*, 5> cards = {trackAudioComp, eqComp, compComp, stripComp, masterComp};
    for (size_t i = 0; i < cards.size(); ++i) {
        for (size_t j = i + 1; j < cards.size(); ++j) {
            EXPECT_FALSE(cards[i]->getBounds().intersects(cards[j]->getBounds()))
                << "card " << i << " " << cards[i]->getBounds().toString().toStdString() << " overlaps card " << j
                << " " << cards[j]->getBounds().toString().toStdString();
        }
    }

    EXPECT_LT(trackAudioComp->getX(), eqComp->getX());
    EXPECT_LT(eqComp->getX(), compComp->getX());
    EXPECT_LT(compComp->getX(), stripComp->getX());
    EXPECT_LT(stripComp->getX(), masterComp->getX());
    EXPECT_GE(masterComp->getX(), stripComp->getRight()) << "Master must be fully clear of the Strip card";
}

TEST_F(ChannelFlowTest, RefusedAtMaxTracksCreatesNothing) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    auto& doc = mc.getTimelineDoc();
    auto& graph = mc.getAudioEngine().getGraph();
    auto& macros = mc.getGraphEditor().getMacros();

    // Fixture setup goes straight through the doc, not the undo-recording flow, so it pushes no
    // undo step of its own — see TimelinePanelTests.cpp's AddTrackAtTheCapAddsNoNode for the same
    // pattern.
    while ((int)doc.getTracks().size() < synth::TimelineDoc::kMaxTracks)
        ASSERT_TRUE(doc.addTrack(synth::TrackKind::Midi, "Filler").isValid());
    ASSERT_FALSE(mc.getUndoManager().canUndo());
    const int nodesBefore = graph.getNumNodes();

    addAudioTrack(mc);

    EXPECT_EQ((int)doc.getTracks().size(), synth::TimelineDoc::kMaxTracks);
    EXPECT_EQ(graph.getNumNodes(), nodesBefore) << "a refused audio track must leave no orphan node";
    EXPECT_EQ(macros.size(), 0) << "a refused audio track must leave no macro";
    EXPECT_FALSE(mc.getUndoManager().canUndo()) << "nothing changed in any domain: no undo step";
}

// ---------------------------------------------------------------------------------------------
// T183 (P9-3b): "+ Track -> Instrument -> {Oscillator/Wavetable/Sampler}" builds
//
//     Track In -> instrument -> Parametric EQ (bypassed) -> Compressor (bypassed)
//              -> Channel Strip (Stereo) -> Master (Mix)
//
// as ONE undo step, with {Track In, instrument, EQ, Compressor, Strip} boxed into one collapsed
// macro named after the track — the MIDI-track mirror of the Audio Track tests above. See
// MainComponent::addInstrumentTrack's own comment for why this stays a TrackKind::Midi track
// rather than a new TrackKind, and Source/Mixer/ChannelFlows.h for the poly/Voice Mixer contract.
// ---------------------------------------------------------------------------------------------

TEST_F(ChannelFlowTest, InstrumentTrackSamplerBuildsDefaultChannelDirectlyOnAContiguousPair) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    auto& graph = mc.getAudioEngine().getGraph();

    addInstrumentTrack(mc, "Sampler");

    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::TimelineMidiSource), 1);
    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::Sampler), 1);
    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::VoiceMixer), 0)
        << "Sampler is a contiguous stereo pair — no Voice Mixer needed";
    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::ParametricEQ), 1);
    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::Compressor), 1);
    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::ChannelStrip), 1);
    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::Master), 1);

    auto* trackIn = findNodeOfTypeCFT(graph, ModuleType::TimelineMidiSource);
    auto* sampler = findNodeOfTypeCFT(graph, ModuleType::Sampler);
    auto* eq = findNodeOfTypeCFT(graph, ModuleType::ParametricEQ);
    ASSERT_NE(trackIn, nullptr);
    ASSERT_NE(sampler, nullptr);
    ASSERT_NE(eq, nullptr);

    EXPECT_TRUE(graph.isConnected({{trackIn->nodeID, juce::AudioProcessorGraph::midiChannelIndex},
                                   {sampler->nodeID, juce::AudioProcessorGraph::midiChannelIndex}}));

    auto* samplerModule = dynamic_cast<ModuleBase*>(sampler->getProcessor());
    ASSERT_NE(samplerModule, nullptr);
    ASSERT_EQ(samplerModule->rightAudioLegChannel(), 1) << "Sampler's legs ARE the contiguous ch0/ch1 pair";
    EXPECT_TRUE(graph.isConnected({{sampler->nodeID, 0}, {eq->nodeID, 0}}));
    EXPECT_TRUE(graph.isConnected({{sampler->nodeID, 1}, {eq->nodeID, 1}}));
}

TEST_F(ChannelFlowTest, InstrumentTrackOscillatorWiresSplitBlockRightLegNeverCh1) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    auto& graph = mc.getAudioEngine().getGraph();
    auto& macros = mc.getGraphEditor().getMacros();

    addInstrumentTrack(mc, "Oscillator");

    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::VoiceMixer), 0)
        << "a freshly created Oscillator defaults to poly OFF — no Voice Mixer needed";

    ASSERT_EQ(macros.size(), 1);
    const auto& macro = macros.getAll().front();
    auto* oscillator = findNodeOfTypeCFT(graph, ModuleType::Oscillator);
    // The factory default preset every fresh MainComponent loads already has its own VCA node —
    // disambiguate via macro membership, not "last one seen" (see findMacroMemberOfTypeCFT).
    auto* vca = findMacroMemberOfTypeCFT(graph, macro, ModuleType::VCA);
    ASSERT_NE(oscillator, nullptr);
    ASSERT_NE(vca, nullptr) << "P9-3i: Oscillator has no envelope of its own, so the default chain must insert a VCA";

    auto* oscModule = dynamic_cast<ModuleBase*>(oscillator->getProcessor());
    ASSERT_NE(oscModule, nullptr);
    const int rightLeg = oscModule->rightAudioLegChannel();
    ASSERT_GT(rightLeg, 1) << "Oscillator's right leg is a dedicated kRightBase block, never ch1";

    // Oscillator -> VCA (never ch1 for the right leg — same split-block contract, one hop earlier).
    EXPECT_TRUE(graph.isConnected({{oscillator->nodeID, 0}, {vca->nodeID, 0}}));
    EXPECT_TRUE(graph.isConnected({{oscillator->nodeID, rightLeg}, {vca->nodeID, VCAModule::kRightBase}}));
    EXPECT_FALSE(graph.isConnected({{oscillator->nodeID, 1}, {vca->nodeID, 1}}))
        << "must never assume ch1 for a split-block source, and ch1 on the VCA is its Gain CV";

    // VCA -> EQ, now that the VCA sits between the instrument and the rest of the chain.
    auto* eq = findNodeOfTypeCFT(graph, ModuleType::ParametricEQ);
    ASSERT_NE(eq, nullptr);
    EXPECT_TRUE(graph.isConnected({{vca->nodeID, 0}, {eq->nodeID, 0}}));
    EXPECT_TRUE(graph.isConnected({{vca->nodeID, VCAModule::kRightBase}, {eq->nodeID, 1}}));
    EXPECT_FALSE(graph.isConnected({{oscillator->nodeID, 0}, {eq->nodeID, 0}}))
        << "Oscillator must no longer feed EQ directly — it goes through the VCA";
}

// P9-3i (FRO43): the ADSR gating the VCA above is driven by the same Track In MIDI as the
// instrument, and its Env output lands on the VCA's mono Gain CV (ch1) — never the audio legs.
TEST_F(ChannelFlowTest, InstrumentTrackOscillatorEnvelopeGatesTheVCA) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    auto& graph = mc.getAudioEngine().getGraph();
    auto& macros = mc.getGraphEditor().getMacros();

    addInstrumentTrack(mc, "Oscillator");

    ASSERT_EQ(macros.size(), 1);
    const auto& macro = macros.getAll().front();
    auto* trackIn = findNodeOfTypeCFT(graph, ModuleType::TimelineMidiSource);
    // The factory default preset every fresh MainComponent loads already has its own ADSR-type
    // nodes (Amp Env, Filter Env) and VCA node — disambiguate via macro membership.
    auto* adsr = findMacroMemberOfTypeCFT(graph, macro, ModuleType::ADSR);
    auto* vca = findMacroMemberOfTypeCFT(graph, macro, ModuleType::VCA);
    ASSERT_NE(trackIn, nullptr);
    ASSERT_NE(adsr, nullptr);
    ASSERT_NE(vca, nullptr);

    EXPECT_TRUE(graph.isConnected({{trackIn->nodeID, juce::AudioProcessorGraph::midiChannelIndex},
                                   {adsr->nodeID, juce::AudioProcessorGraph::midiChannelIndex}}))
        << "the ADSR must be gated by the same Track In MIDI as the instrument";
    EXPECT_TRUE(graph.isConnected({{adsr->nodeID, 0}, {vca->nodeID, 1}}))
        << "ADSR's Env output must land on the VCA's mono Gain CV (ch1)";

    auto* adsrModule = dynamic_cast<ModuleBase*>(adsr->getProcessor());
    ASSERT_NE(adsrModule, nullptr);
    for (auto* param : adsr->getProcessor()->getParameters()) {
        if (auto* boolParam = dynamic_cast<juce::AudioParameterBool*>(param))
            if (boolParam->paramID == "poly")
                EXPECT_FALSE(boolParam->get())
                    << "the auto-wired ADSR must be non-poly — its poly branch ignores MIDI entirely";
        if (auto* floatParam = dynamic_cast<juce::AudioParameterFloat*>(param))
            if (floatParam->paramID == "sustain")
                EXPECT_FLOAT_EQ(floatParam->get(), 0.7f)
                    << "sustain must be overridden so a held note doesn't decay to silence";
    }
    for (auto* param : vca->getProcessor()->getParameters()) {
        if (auto* boolParam = dynamic_cast<juce::AudioParameterBool*>(param))
            if (boolParam->paramID == "poly")
                EXPECT_FALSE(boolParam->get()) << "the auto-wired VCA must be non-poly";
        if (auto* floatParam = dynamic_cast<juce::AudioParameterFloat*>(param))
            if (floatParam->paramID == "gain")
                EXPECT_FLOAT_EQ(floatParam->get(), 1.0f)
                    << "gain must be overridden so the envelope alone governs level";
    }
}

// The ticket's own bug: a held-then-released note must not drone forever. Renders the ADSR and VCA
// nodes directly (Track In is a timeline MIDI source and won't forward an injected MidiBuffer, so a
// full-graph render can't inject a note) — this is the render-level check topology assertions above
// cannot give: it proves the envelope actually gates audio, not just that the wires exist.
TEST_F(ChannelFlowTest, InstrumentTrackOscillatorEnvelopeActuallySilencesAfterNoteOff) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    auto& graph = mc.getAudioEngine().getGraph();
    auto& macros = mc.getGraphEditor().getMacros();

    addInstrumentTrack(mc, "Oscillator");

    ASSERT_EQ(macros.size(), 1);
    const auto& macro = macros.getAll().front();
    // The factory default preset every fresh MainComponent loads already has its own ADSR-type
    // nodes (Amp Env, Filter Env) — disambiguate via macro membership.
    auto* adsrNode = findMacroMemberOfTypeCFT(graph, macro, ModuleType::ADSR);
    ASSERT_NE(adsrNode, nullptr);
    auto* adsr = adsrNode->getProcessor();

    constexpr double sampleRate = 44100.0;
    constexpr int blockSize = 256;
    adsr->prepareToPlay(sampleRate, blockSize);

    // ch0 is the mono "Gate" CV input, unused by this test — leave it at 0 (below the default
    // 0.5 threshold) so ONLY the injected MIDI drives the gate. Filling it with any value above
    // threshold would latch the Schmitt trigger permanently high, masking the MIDI path entirely.
    auto renderAdsrBlock = [&](juce::MidiBuffer midi) {
        juce::AudioBuffer<float> buf(9, blockSize);
        buf.clear();
        adsr->processBlock(buf, midi);
        return buf.getSample(0, blockSize - 1);
    };

    // No note yet: the envelope must be at rest.
    EXPECT_NEAR(renderAdsrBlock({}), 0.0f, 1e-4f) << "envelope must start silent with no note held";

    // Note on: after enough blocks to clear attack+decay, the envelope must have risen and settled
    // at (roughly) the overridden sustain level, not fallen back to 0 — this is the actual bug fix.
    juce::MidiBuffer noteOn;
    noteOn.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
    float lastEnv = renderAdsrBlock(noteOn);
    for (int i = 0; i < 50; ++i)
        lastEnv = renderAdsrBlock({});
    EXPECT_GT(lastEnv, 0.5f) << "a held note must sustain, not decay to silence while still held";

    // Note off: after enough blocks for the release stage, the envelope must return to silence —
    // this is the drone this ticket exists to fix.
    juce::MidiBuffer noteOff;
    noteOff.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
    lastEnv = renderAdsrBlock(noteOff);
    for (int i = 0; i < 50; ++i)
        lastEnv = renderAdsrBlock({});
    EXPECT_NEAR(lastEnv, 0.0f, 1e-3f) << "the envelope must return to silence after note-off + release";
}

TEST_F(ChannelFlowTest, InstrumentTrackDefaultInsertsAreBypassedAndStripIsStereo) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    auto& graph = mc.getAudioEngine().getGraph();

    addInstrumentTrack(mc, "Sampler");

    auto* eqModule = dynamic_cast<ModuleBase*>(findNodeOfTypeCFT(graph, ModuleType::ParametricEQ)->getProcessor());
    auto* compModule = dynamic_cast<ModuleBase*>(findNodeOfTypeCFT(graph, ModuleType::Compressor)->getProcessor());
    auto* stripModule =
        dynamic_cast<ChannelStripModule*>(findNodeOfTypeCFT(graph, ModuleType::ChannelStrip)->getProcessor());
    ASSERT_NE(eqModule, nullptr);
    ASSERT_NE(compModule, nullptr);
    ASSERT_NE(stripModule, nullptr);

    EXPECT_TRUE(eqModule->isBypassed());
    EXPECT_TRUE(compModule->isBypassed());
    EXPECT_FALSE(stripModule->isBypassed());
    EXPECT_EQ(stripModule->getShape(), ChannelStripModule::Shape::Stereo);
}

// P9-3i (FRO43) is scoped to Oscillator/Wavetable only — Sampler already has its own one-shot
// playback envelope and must be completely unaffected: no ADSR/VCA nodes, no change to its wiring
// or macro membership.
TEST_F(ChannelFlowTest, InstrumentTrackSamplerGetsNoEnvelopeOrVCA) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    auto& graph = mc.getAudioEngine().getGraph();
    auto& macros = mc.getGraphEditor().getMacros();

    addInstrumentTrack(mc, "Sampler");

    // The factory default preset every fresh MainComponent loads already has its own ADSR-type
    // nodes and a VCA node, so a raw graph-wide count can't tell "none created" from "the preset's
    // own" — check the track's own macro membership instead (freshly built, contains only this
    // track's own nodes).
    ASSERT_EQ(macros.size(), 1);
    const auto& macro = macros.getAll().front();
    EXPECT_EQ(findMacroMemberOfTypeCFT(graph, macro, ModuleType::ADSR), nullptr);
    EXPECT_EQ(findMacroMemberOfTypeCFT(graph, macro, ModuleType::VCA), nullptr);

    auto* sampler = findNodeOfTypeCFT(graph, ModuleType::Sampler);
    auto* eq = findNodeOfTypeCFT(graph, ModuleType::ParametricEQ);
    ASSERT_NE(sampler, nullptr);
    ASSERT_NE(eq, nullptr);
    EXPECT_TRUE(graph.isConnected({{sampler->nodeID, 0}, {eq->nodeID, 0}}))
        << "Sampler must still feed EQ directly, unchanged by P9-3i";
}

TEST_F(ChannelFlowTest, InstrumentTrackIsOneCollapsedMacroNamedAfterTrackAndStaysMidiKind) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    auto& graph = mc.getAudioEngine().getGraph();
    auto& doc = mc.getTimelineDoc();
    auto& macros = mc.getGraphEditor().getMacros();

    addInstrumentTrack(mc, "Sampler");

    ASSERT_EQ(macros.size(), 1);
    const auto& macro = macros.getAll().front();
    ASSERT_EQ(doc.getTracks().size(), 1u);
    const auto& track = doc.getTracks().back();

    EXPECT_EQ(macro.name, track.name);
    EXPECT_TRUE(macro.collapsed);
    // T183's scope decision: an instrument track is TrackKind::Midi (a Track In feeding exactly
    // one instrument), not a new TrackKind — see MainComponent::addInstrumentTrack's own comment.
    EXPECT_EQ(track.kind, synth::TrackKind::Midi);
    EXPECT_EQ(track.bindingUuid, nodeUuid(findNodeOfTypeCFT(graph, ModuleType::TimelineMidiSource)));

    std::vector<juce::String> expected = {nodeUuid(findNodeOfTypeCFT(graph, ModuleType::TimelineMidiSource)),
                                          nodeUuid(findNodeOfTypeCFT(graph, ModuleType::Sampler)),
                                          nodeUuid(findNodeOfTypeCFT(graph, ModuleType::ParametricEQ)),
                                          nodeUuid(findNodeOfTypeCFT(graph, ModuleType::Compressor)),
                                          nodeUuid(findNodeOfTypeCFT(graph, ModuleType::ChannelStrip))};
    auto actual = macro.members;
    std::sort(expected.begin(), expected.end());
    std::sort(actual.begin(), actual.end());
    EXPECT_EQ(actual, expected);

    EXPECT_FALSE(macro.hasMember(nodeUuid(findNodeOfTypeCFT(graph, ModuleType::Master))))
        << "Master must stay outside the macro";
}

// P9-3i (FRO43): an Oscillator track's macro must include the new ADSR+VCA members too —
// InstrumentTrackIsOneCollapsedMacroNamedAfterTrackAndStaysMidiKind above uses Sampler, which never
// exercises this membership change.
TEST_F(ChannelFlowTest, InstrumentTrackOscillatorMacroIncludesEnvelopeAndVCA) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    auto& graph = mc.getAudioEngine().getGraph();
    auto& macros = mc.getGraphEditor().getMacros();

    addInstrumentTrack(mc, "Oscillator");

    ASSERT_EQ(macros.size(), 1);
    const auto& macro = macros.getAll().front();

    // The factory default preset every fresh MainComponent loads already has its own ADSR-type
    // nodes and a VCA node — disambiguate via macro membership, not "last one seen".
    std::vector<juce::String> expected = {nodeUuid(findNodeOfTypeCFT(graph, ModuleType::TimelineMidiSource)),
                                          nodeUuid(findNodeOfTypeCFT(graph, ModuleType::Oscillator)),
                                          nodeUuid(findMacroMemberOfTypeCFT(graph, macro, ModuleType::ADSR)),
                                          nodeUuid(findMacroMemberOfTypeCFT(graph, macro, ModuleType::VCA)),
                                          nodeUuid(findNodeOfTypeCFT(graph, ModuleType::ParametricEQ)),
                                          nodeUuid(findNodeOfTypeCFT(graph, ModuleType::Compressor)),
                                          nodeUuid(findNodeOfTypeCFT(graph, ModuleType::ChannelStrip))};
    auto actual = macro.members;
    std::sort(expected.begin(), expected.end());
    std::sort(actual.begin(), actual.end());
    EXPECT_EQ(actual, expected);
}

TEST_F(ChannelFlowTest, InstrumentTrackOneUndoStepRevertsEverythingAndRedoRestores) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    auto& graph = mc.getAudioEngine().getGraph();
    auto& doc = mc.getTimelineDoc();
    auto& macros = mc.getGraphEditor().getMacros();

    const juce::String graphBefore = juce::JSON::toString(synth::AIStateMapper::graphToJSON(graph));
    const juce::String docBefore = juce::JSON::toString(doc.toVar());
    const juce::String macrosBefore = juce::JSON::toString(macros.toVar());

    addInstrumentTrack(mc, "Oscillator");
    ASSERT_EQ(countNodesOfTypeCFT(graph, ModuleType::ChannelStrip), 1) << "the channel must have been built";
    const juce::String graphAfter = juce::JSON::toString(synth::AIStateMapper::graphToJSON(graph));
    const juce::String docAfter = juce::JSON::toString(doc.toVar());
    const juce::String macrosAfter = juce::JSON::toString(macros.toVar());

    ASSERT_TRUE(mc.getUndoManager().canUndo());
    ASSERT_TRUE(mc.getUndoManager().undo());
    EXPECT_EQ(juce::JSON::toString(synth::AIStateMapper::graphToJSON(graph)), graphBefore);
    EXPECT_EQ(juce::JSON::toString(doc.toVar()), docBefore);
    EXPECT_EQ(juce::JSON::toString(macros.toVar()), macrosBefore);
    EXPECT_FALSE(mc.getUndoManager().canUndo()) << "the whole channel was ONE undo step";

    ASSERT_TRUE(mc.getUndoManager().redo());
    EXPECT_EQ(juce::JSON::toString(synth::AIStateMapper::graphToJSON(graph)), graphAfter);
    EXPECT_EQ(juce::JSON::toString(doc.toVar()), docAfter);
    EXPECT_EQ(juce::JSON::toString(macros.toVar()), macrosAfter);
}

// The Wavetable card and the Parametric EQ card are BOTH double-width — the exact overlap shape
// P9-3a's own bug (see this file's header comment) reproduced for, now two nodes to the left of
// where it was (P9-3i inserted ADSR+VCA between them). Reuses
// ChannelCardsDoNotOverlapAndMasterIsRightOfStrip's real-ModuleComponent approach rather than
// inferring bounds from positions.
TEST_F(ChannelFlowTest, InstrumentTrackWavetableCardsDoNotOverlap) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(2600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    auto& graph = mc.getAudioEngine().getGraph();
    auto& macros = mc.getGraphEditor().getMacros();

    addInstrumentTrack(mc, "Wavetable");

    ASSERT_EQ(macros.size(), 1);
    const auto macroId = macros.getAll().front().id;
    mc.getGraphEditor().setMacroCollapsed(macroId, false);
    ASSERT_FALSE(macros.find(macroId)->collapsed);

    // The factory default preset every fresh MainComponent loads already has its own ADSR-type
    // nodes and a VCA node — disambiguate via macro membership, not "last one seen".
    const auto& macro = *macros.find(macroId);
    auto* trackInNode = findNodeOfTypeCFT(graph, ModuleType::TimelineMidiSource);
    auto* wavetableNode = findNodeOfTypeCFT(graph, ModuleType::Wavetable);
    auto* adsrNode = findMacroMemberOfTypeCFT(graph, macro, ModuleType::ADSR);
    auto* vcaNode = findMacroMemberOfTypeCFT(graph, macro, ModuleType::VCA);
    auto* eqNode = findNodeOfTypeCFT(graph, ModuleType::ParametricEQ);
    auto* compNode = findNodeOfTypeCFT(graph, ModuleType::Compressor);
    auto* stripNode = findNodeOfTypeCFT(graph, ModuleType::ChannelStrip);
    auto* masterNode = findNodeOfTypeCFT(graph, ModuleType::Master);
    ASSERT_NE(trackInNode, nullptr);
    ASSERT_NE(wavetableNode, nullptr);
    ASSERT_NE(adsrNode, nullptr) << "P9-3i: Wavetable also has no envelope of its own";
    ASSERT_NE(vcaNode, nullptr);
    ASSERT_NE(eqNode, nullptr);
    ASSERT_NE(compNode, nullptr);
    ASSERT_NE(stripNode, nullptr);
    ASSERT_NE(masterNode, nullptr);

    auto findComp = [&mc](juce::AudioProcessorGraph::Node* node) -> ModuleComponent* {
        for (auto* comp : mc.getGraphEditor().getModuleComponents())
            if (comp != nullptr && comp->getNodeId() == node->nodeID)
                return comp;
        return nullptr;
    };

    const std::array<ModuleComponent*, 8> cards = {findComp(trackInNode), findComp(wavetableNode), findComp(adsrNode),
                                                   findComp(vcaNode),     findComp(eqNode),        findComp(compNode),
                                                   findComp(stripNode),   findComp(masterNode)};
    for (auto* card : cards)
        ASSERT_NE(card, nullptr) << "every macro member must have a real ModuleComponent once expanded";

    for (size_t i = 0; i < cards.size(); ++i)
        for (size_t j = i + 1; j < cards.size(); ++j)
            EXPECT_FALSE(cards[i]->getBounds().intersects(cards[j]->getBounds()))
                << "card " << i << " " << cards[i]->getBounds().toString().toStdString() << " overlaps card " << j
                << " " << cards[j]->getBounds().toString().toStdString();

    for (size_t i = 0; i + 1 < cards.size(); ++i)
        EXPECT_LT(cards[i]->getX(), cards[i + 1]->getX());
}

TEST_F(ChannelFlowTest, InstrumentTrackRefusedAtMaxTracksCreatesNothing) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    auto& doc = mc.getTimelineDoc();
    auto& graph = mc.getAudioEngine().getGraph();
    auto& macros = mc.getGraphEditor().getMacros();

    while ((int)doc.getTracks().size() < synth::TimelineDoc::kMaxTracks)
        ASSERT_TRUE(doc.addTrack(synth::TrackKind::Midi, "Filler").isValid());
    ASSERT_FALSE(mc.getUndoManager().canUndo());
    const int nodesBefore = graph.getNumNodes();

    addInstrumentTrack(mc, "Sampler");

    EXPECT_EQ((int)doc.getTracks().size(), synth::TimelineDoc::kMaxTracks);
    EXPECT_EQ(graph.getNumNodes(), nodesBefore) << "a refused instrument track must leave no orphan node";
    EXPECT_EQ(macros.size(), 0) << "a refused instrument track must leave no macro";
    EXPECT_FALSE(mc.getUndoManager().canUndo()) << "nothing changed in any domain: no undo step";
}

// ============================================================================
// FRO42 (P9-3h): "+ Track -> Instrument -> Plugin -> <name>" — a hosted plugin as the instrument.
// Loading is asynchronous (StubBackend's own contract), so every test here pumps the message loop
// after driving the SAME applyAddTrackMenuChoice/menu-id path the tests above use.
// ============================================================================

TEST_F(ChannelFlowTest, PluginInstrumentTrackBuildsDefaultChannelWithNoAdsr) {
    InstrumentPluginStubBackendCFT backend;
    backend.factories["Stub Synth"] = [] {
        return std::make_unique<synth::test::StubPluginInstance>(0, 2, "Stub Synth");
    };
    synth::HostedPluginBackend::ScopedDefault installed(&backend);

    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    auto& graph = mc.getAudioEngine().getGraph();
    auto& macros = mc.getGraphEditor().getMacros();
    mc.getPluginScanService().setCandidateSource([](const juce::String&) { return juce::StringArray(); });
    seedScanListCFT(mc.getPluginScanService(), {pluginDescriptionCFT("Stub Synth", 0xA1FA, /*isInstrument=*/true)});

    // FRO42 fix: applyAddTrackMenuChoice now resolves against the snapshot buildAddTrackMenu()
    // captures, so a real menu open has to run first — the same flow a real click always goes
    // through (see TimelinePanelComponent.h's instrumentPluginMenuSnapshot_ comment).
    mc.getTimelinePanel().buildAddTrackMenu();
    mc.getTimelinePanel().applyAddTrackMenuChoice(synth::ui::TimelinePanelComponent::kAddInstrumentPluginMenuIdBase +
                                                  0);

    ASSERT_TRUE(pumpUntilCFT([&] { return countNodesOfTypeCFT(graph, ModuleType::ChannelStrip) == 1; }))
        << "the async load/chain-build never completed";

    auto* trackIn = findNodeOfTypeCFT(graph, ModuleType::TimelineMidiSource);
    auto* plugin = findNodeOfTypeCFT(graph, ModuleType::HostedPlugin);
    auto* eq = findNodeOfTypeCFT(graph, ModuleType::ParametricEQ);
    auto* comp = findNodeOfTypeCFT(graph, ModuleType::Compressor);
    auto* strip = findNodeOfTypeCFT(graph, ModuleType::ChannelStrip);
    ASSERT_NE(trackIn, nullptr);
    ASSERT_NE(plugin, nullptr);
    ASSERT_NE(eq, nullptr);
    ASSERT_NE(comp, nullptr);
    ASSERT_NE(strip, nullptr);
    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::Master), 1);

    ASSERT_EQ(macros.size(), 1);
    const auto& macro = macros.getAll().front();
    EXPECT_EQ(findMacroMemberOfTypeCFT(graph, macro, ModuleType::ADSR), nullptr)
        << "a hosted synth has its own envelope — no P9-3i ADSR+VCA for it";
    EXPECT_EQ(findMacroMemberOfTypeCFT(graph, macro, ModuleType::VCA), nullptr);
    std::vector<juce::String> expectedMembers{nodeUuid(trackIn), nodeUuid(plugin), nodeUuid(eq), nodeUuid(comp),
                                              nodeUuid(strip)};
    auto actualMembers = macro.members;
    std::sort(expectedMembers.begin(), expectedMembers.end());
    std::sort(actualMembers.begin(), actualMembers.end());
    EXPECT_EQ(actualMembers, expectedMembers) << "exactly Track In/plugin/EQ/Compressor/Strip, nothing else";

    bool midiWired = false;
    for (const auto& c : graph.getConnections())
        if (c.source.nodeID == trackIn->nodeID &&
            c.source.channelIndex == juce::AudioProcessorGraph::midiChannelIndex &&
            c.destination.nodeID == plugin->nodeID &&
            c.destination.channelIndex == juce::AudioProcessorGraph::midiChannelIndex)
            midiWired = true;
    EXPECT_TRUE(midiWired) << "Track In's MIDI must reach the plugin's MIDI input";

    bool leftWired = false;
    bool rightWired = false;
    for (const auto& c : graph.getConnections()) {
        if (c.source.nodeID != plugin->nodeID || c.destination.nodeID != eq->nodeID)
            continue;
        if (c.source.channelIndex == 0 && c.destination.channelIndex == 0)
            leftWired = true;
        if (c.source.channelIndex == 1 && c.destination.channelIndex == 1)
            rightWired = true;
    }
    EXPECT_TRUE(leftWired) << "the plugin's L output must reach EQ L";
    EXPECT_TRUE(rightWired) << "the plugin's real published R output (raw ch1) must reach EQ R — "
                               "rightAudioLegChannel() read AFTER the load completed, never assumed ch1 blind";
}

TEST_F(ChannelFlowTest, PluginInstrumentTrackOneUndoStepRevertsEverythingAndRedoRestores) {
    InstrumentPluginStubBackendCFT backend;
    backend.factories["Stub Synth"] = [] {
        return std::make_unique<synth::test::StubPluginInstance>(0, 2, "Stub Synth");
    };
    synth::HostedPluginBackend::ScopedDefault installed(&backend);

    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    auto& graph = mc.getAudioEngine().getGraph();
    auto& doc = mc.getTimelineDoc();
    auto& macros = mc.getGraphEditor().getMacros();
    mc.getPluginScanService().setCandidateSource([](const juce::String&) { return juce::StringArray(); });
    seedScanListCFT(mc.getPluginScanService(), {pluginDescriptionCFT("Stub Synth", 0xA1FA, /*isInstrument=*/true)});

    // Settle first: GraphEditor::updateComponents() (already run once, for the factory preset the
    // constructor loads) posts a mod-matrix refresh via callAsync, and that refresh renumbers every
    // module's display name THE FIRST TIME IT EVER RUNS (AudioEngine::updateModuleNames() — cosmetic,
    // never undo-tracked). The synchronous factory-instrument flow never pumps the loop, so that
    // rename never gets a chance to fire there; THIS flow necessarily pumps it for the async plugin
    // load below, so it must be allowed to happen and settle BEFORE "before" is captured, or it would
    // land in the gap between "before" and "after undo" and make them differ over nothing this
    // feature touched.
    pumpUntilCFT([] { return false; }, 50);

    const juce::String graphBefore = juce::JSON::toString(synth::AIStateMapper::graphToJSON(graph));
    const juce::String docBefore = juce::JSON::toString(doc.toVar());
    const juce::String macrosBefore = juce::JSON::toString(macros.toVar());

    // FRO42 fix: resolve against the buildAddTrackMenu() snapshot, the real click flow.
    mc.getTimelinePanel().buildAddTrackMenu();
    mc.getTimelinePanel().applyAddTrackMenuChoice(synth::ui::TimelinePanelComponent::kAddInstrumentPluginMenuIdBase +
                                                  0);
    ASSERT_TRUE(pumpUntilCFT([&] { return countNodesOfTypeCFT(graph, ModuleType::ChannelStrip) == 1; }));

    const juce::String graphAfter = juce::JSON::toString(synth::AIStateMapper::graphToJSON(graph));
    const juce::String docAfter = juce::JSON::toString(doc.toVar());
    const juce::String macrosAfter = juce::JSON::toString(macros.toVar());

    ASSERT_TRUE(mc.getUndoManager().canUndo());
    ASSERT_TRUE(mc.getUndoManager().undo());
    EXPECT_EQ(juce::JSON::toString(synth::AIStateMapper::graphToJSON(graph)), graphBefore);
    EXPECT_EQ(juce::JSON::toString(doc.toVar()), docBefore);
    EXPECT_EQ(juce::JSON::toString(macros.toVar()), macrosBefore);
    EXPECT_FALSE(mc.getUndoManager().canUndo()) << "the whole channel was ONE undo step";

    ASSERT_TRUE(mc.getUndoManager().redo());
    EXPECT_EQ(juce::JSON::toString(synth::AIStateMapper::graphToJSON(graph)), graphAfter);
    EXPECT_EQ(juce::JSON::toString(doc.toVar()), docAfter);
    EXPECT_EQ(juce::JSON::toString(macros.toVar()), macrosAfter);
}

TEST_F(ChannelFlowTest, PluginInstrumentTrackEffectsAreNeverOfferedInTheMenu) {
    InstrumentPluginStubBackendCFT backend;
    synth::HostedPluginBackend::ScopedDefault installed(&backend);

    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    auto& graph = mc.getAudioEngine().getGraph();
    auto& doc = mc.getTimelineDoc();
    mc.getPluginScanService().setCandidateSource([](const juce::String&) { return juce::StringArray(); });
    seedScanListCFT(mc.getPluginScanService(), {pluginDescriptionCFT("Stub Delay", 0xB37A, /*isInstrument=*/false)});

    EXPECT_TRUE(mc.getTimelinePanel().collectInstrumentPluginMenuOptions().empty())
        << "an effect (isInstrument=false) must never appear in the Instrument -> Plugin submenu";

    // Build the real menu (as a real click would) so the disabled "No instrument plugins found"
    // row is what actually snapshots — not a panel that never opened its menu at all.
    mc.getTimelinePanel().buildAddTrackMenu();

    const int tracksBefore = (int)doc.getTracks().size();
    const int nodesBefore = graph.getNumNodes();
    // Nothing at this id — the snapshot is empty, so the index is out of range and this must be a
    // no-op, exactly like a stale/out-of-range id on any other dynamically-built menu in this app.
    mc.getTimelinePanel().applyAddTrackMenuChoice(synth::ui::TimelinePanelComponent::kAddInstrumentPluginMenuIdBase +
                                                  0);

    EXPECT_EQ((int)doc.getTracks().size(), tracksBefore);
    EXPECT_EQ(graph.getNumNodes(), nodesBefore);
    EXPECT_FALSE(mc.getUndoManager().canUndo());
}

TEST_F(ChannelFlowTest, PluginInstrumentTrackFailedLoadLeavesGraphAndUndoUntouched) {
    // "Broken Synth" is scanned (isInstrument=true, so it IS offered) but has no factory entry —
    // InstrumentPluginStubBackendCFT fails its load, exactly like a plugin whose binary the machine
    // can no longer find or whose format crashed on load.
    InstrumentPluginStubBackendCFT backend;
    synth::HostedPluginBackend::ScopedDefault installed(&backend);

    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    auto& graph = mc.getAudioEngine().getGraph();
    auto& doc = mc.getTimelineDoc();
    mc.getPluginScanService().setCandidateSource([](const juce::String&) { return juce::StringArray(); });
    seedScanListCFT(mc.getPluginScanService(), {pluginDescriptionCFT("Broken Synth", 0xC0DE, /*isInstrument=*/true)});

    const int tracksBefore = (int)doc.getTracks().size();
    const int nodesBefore = graph.getNumNodes();
    const juce::String messageBefore = mc.getStatusBar().getTransientMessageForTest();

    // FRO42 fix: resolve against the buildAddTrackMenu() snapshot, the real click flow.
    mc.getTimelinePanel().buildAddTrackMenu();
    mc.getTimelinePanel().applyAddTrackMenuChoice(synth::ui::TimelinePanelComponent::kAddInstrumentPluginMenuIdBase +
                                                  0);

    ASSERT_TRUE(pumpUntilCFT([&] { return mc.getStatusBar().getTransientMessageForTest() != messageBefore; }))
        << "the failure was never reported";

    EXPECT_EQ((int)doc.getTracks().size(), tracksBefore) << "a failed load must leave no orphan track";
    EXPECT_EQ(graph.getNumNodes(), nodesBefore) << "a failed load must leave no orphan node — not even a bare "
                                                   "Hosted Plugin placeholder";
    EXPECT_FALSE(mc.getUndoManager().canUndo()) << "a failed load must push no undo step";

    // The pending processor is torn down on a LATER message-loop turn (see
    // MainComponent::addInstrumentPluginTrack's own comment on why); give it that turn and confirm
    // nothing changed as a result either.
    pumpUntilCFT([] { return false; }, 100);
    EXPECT_EQ((int)doc.getTracks().size(), tracksBefore);
    EXPECT_EQ(graph.getNumNodes(), nodesBefore);
    EXPECT_FALSE(mc.getUndoManager().canUndo());
}

TEST_F(ChannelFlowTest, PluginInstrumentMenuShowsScanningThenNoInstrumentPluginsFound) {
    InstrumentPluginStubBackendCFT backend;
    synth::HostedPluginBackend::ScopedDefault installed(&backend);

    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();

    // One VST3 candidate with a launcher that deliberately takes a moment — long enough that
    // isScanning() is still reliably true the instant buildAddTrackMenu() returns (with zero
    // candidates the background thread can finish before this thread's very next line runs, which
    // would make that assertion flaky). It reports "not found" either way, so the scan still
    // finishes with nothing known — the "nothing installed on this machine" case.
    mc.getPluginScanService().setCandidateSource([](const juce::String& format) {
        return format == "VST3" ? juce::StringArray("/plugins/Slow.vst3") : juce::StringArray();
    });
    mc.getPluginScanService().setChildLauncher([](const juce::String&, const juce::String&, int, juce::String&) {
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        return false;
    });

    juce::PopupMenu firstOpen = mc.getTimelinePanel().buildAddTrackMenu();
    EXPECT_TRUE(mc.getPluginScanService().isScanning()) << "opening the menu must kick off the eager scan";
    const auto* scanningItem = findMenuItemByTextCFT(firstOpen, "Scanning for plugins...");
    ASSERT_NE(scanningItem, nullptr);
    EXPECT_FALSE(scanningItem->isEnabled);

    ASSERT_TRUE(pumpUntilCFT([&] { return !mc.getPluginScanService().isScanning(); }));

    juce::PopupMenu secondOpen = mc.getTimelinePanel().buildAddTrackMenu();
    const auto* noneItem = findMenuItemByTextCFT(secondOpen, "No instrument plugins found");
    ASSERT_NE(noneItem, nullptr);
    EXPECT_FALSE(noneItem->isEnabled);
}

// ============================================================================
// FRO42 review fixes (P9-3h follow-up). See TimelinePanelComponent.h's instrumentPluginMenuSnapshot_
// and kAddInstrumentPluginNoneMenuId comments, and MainComponent::getInstrumentPluginOptions'/
// addInstrumentPluginTrack's own comments, for the mechanism each of these proves.
// ============================================================================

TEST_F(ChannelFlowTest, PluginInstrumentMenuChoiceResolvesAgainstSnapshotNotALaterRescan) {
    // BLOCKER regression test. Before the fix, applyAddTrackMenuChoice re-ran
    // collectInstrumentPluginMenuOptions() at CLICK time and indexed it with the id baked in at
    // BUILD time — so anything that changed the known-plugin list between open and click (the live
    // repro: a background PluginScanService::runScan finishing) silently changed what index 0
    // meant, resolving the click against a plugin the menu never actually showed there. Reproduced
    // here without a real background-thread race: build the menu with ONE plugin known (so it lands
    // at index 0), capture that item's real id, THEN seed a second plugin that sorts alphabetically
    // BEFORE it — WITHOUT rebuilding the menu — and apply the ORIGINAL id. It must still resolve to
    // the plugin the menu actually showed at that id, never the one a fresh collect would now put
    // there.
    InstrumentPluginStubBackendCFT backend;
    backend.factories["Zebra Synth"] = [] {
        return std::make_unique<synth::test::StubPluginInstance>(0, 2, "Zebra Synth");
    };
    backend.factories["Aardvark Synth"] = [] {
        return std::make_unique<synth::test::StubPluginInstance>(0, 2, "Aardvark Synth");
    };
    synth::HostedPluginBackend::ScopedDefault installed(&backend);

    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    auto& graph = mc.getAudioEngine().getGraph();
    mc.getPluginScanService().setCandidateSource([](const juce::String&) { return juce::StringArray(); });
    seedScanListCFT(mc.getPluginScanService(), {pluginDescriptionCFT("Zebra Synth", 0xEEEE, /*isInstrument=*/true)});

    juce::PopupMenu menu = mc.getTimelinePanel().buildAddTrackMenu();
    const auto* zebraItem = findMenuItemByTextCFT(menu, "Zebra Synth (VST3)");
    ASSERT_NE(zebraItem, nullptr) << "the menu must show the one known instrument plugin, format-labelled";
    const int capturedId = zebraItem->itemID;

    // A second plugin sorting BEFORE "Zebra Synth" — a fresh collectInstrumentPluginMenuOptions()
    // would now put THIS at index 0. The menu is deliberately never rebuilt after this point.
    seedScanListCFT(mc.getPluginScanService(), {pluginDescriptionCFT("Zebra Synth", 0xEEEE, /*isInstrument=*/true),
                                                pluginDescriptionCFT("Aardvark Synth", 0xAAAA, /*isInstrument=*/true)});
    ASSERT_EQ(mc.getTimelinePanel().collectInstrumentPluginMenuOptions()[0].name, juce::String("Aardvark Synth"))
        << "sanity: a fresh collect now DOES put a different plugin at index 0";

    mc.getTimelinePanel().applyAddTrackMenuChoice(capturedId);

    ASSERT_TRUE(pumpUntilCFT([&] { return countNodesOfTypeCFT(graph, ModuleType::ChannelStrip) == 1; }))
        << "the async load/chain-build never completed";
    auto* plugin = findNodeOfTypeCFT(graph, ModuleType::HostedPlugin);
    ASSERT_NE(plugin, nullptr);
    auto* hosted = dynamic_cast<synth::HostedPluginModule*>(plugin->getProcessor());
    ASSERT_NE(hosted, nullptr);
    EXPECT_EQ(hosted->getPluginName(), juce::String("Zebra Synth"))
        << "the instance loaded must be the one shown at the captured id — never Aardvark Synth, "
           "which is what a re-collected, freshly re-sorted lookup would have resolved to";
}

TEST_F(ChannelFlowTest, PluginInstrumentMenuAppendsFormatLabelSoIdenticalNamesAreDistinguishable) {
    // SHOULD-FIX: a VST3 and an AU build of the same product must not show as two identical,
    // unlabelled rows — see shortPluginFormatLabel's own comment in TimelinePanelComponent.cpp.
    InstrumentPluginStubBackendCFT backend;
    synth::HostedPluginBackend::ScopedDefault installed(&backend);

    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    mc.getPluginScanService().setCandidateSource([](const juce::String&) { return juce::StringArray(); });
    seedScanListCFT(mc.getPluginScanService(),
                    {pluginDescriptionCFT("Massive", 0x1001, /*isInstrument=*/true, "VST3"),
                     pluginDescriptionCFT("Massive", 0x1002, /*isInstrument=*/true, "AudioUnit")});

    juce::PopupMenu menu = mc.getTimelinePanel().buildAddTrackMenu();
    EXPECT_NE(findMenuItemByTextCFT(menu, "Massive (VST3)"), nullptr) << "the VST3 build must be labelled";
    EXPECT_NE(findMenuItemByTextCFT(menu, "Massive (AU)"), nullptr)
        << "the AudioUnit build must be labelled with the SHORT form, matching the sidebar's own "
           "format disambiguation (ModuleLibraryComponent)";
    EXPECT_EQ(findMenuItemByTextCFT(menu, "Massive"), nullptr) << "the bare, unlabelled name must never appear";
}

TEST_F(ChannelFlowTest, PluginInstrumentMenuExcludesThisAppsOwnPluginBuild) {
    // DECISION (finding 3): never offer to host AgentSynth's own VST3/AU build as an instrument
    // inside itself. Matched against synth::branding's product identity (name AND manufacturer),
    // never a literal re-typed in the picker — see MainComponent::getInstrumentPluginOptions.
    InstrumentPluginStubBackendCFT backend;
    synth::HostedPluginBackend::ScopedDefault installed(&backend);

    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    mc.getPluginScanService().setCandidateSource([](const juce::String&) { return juce::StringArray(); });
    seedScanListCFT(mc.getPluginScanService(),
                    {pluginDescriptionCFT(synth::branding::kProductName, 0x2001, /*isInstrument=*/true, "VST3",
                                          synth::branding::kCompanyName),
                     pluginDescriptionCFT(synth::branding::kProductName, 0x2002, /*isInstrument=*/true, "AudioUnit",
                                          synth::branding::kCompanyName),
                     pluginDescriptionCFT("Stub Synth", 0x2003, /*isInstrument=*/true)});

    const auto options = mc.getTimelinePanel().collectInstrumentPluginMenuOptions();
    for (const auto& option : options)
        EXPECT_FALSE(option.name.equalsIgnoreCase(synth::branding::kProductName))
            << "this app's own build must never appear in its own Instrument -> Plugin picker";
    ASSERT_EQ(options.size(), 1u) << "exactly the one real third-party instrument must remain";
    EXPECT_EQ(options[0].name, juce::String("Stub Synth"));

    // The library sidebar goes through a DIFFERENT collector (getKnownPluginIdentities(), never
    // getInstrumentPluginOptions()) and is deliberately left unfiltered.
    const auto sidebarPlugins = mc.getPluginScanService().getKnownPluginIdentities();
    EXPECT_EQ(sidebarPlugins.size(), 3u) << "the library sidebar must still list this app's own build";
}

TEST_F(ChannelFlowTest, PluginInstrumentLoadCompletingAfterNewPatchIsDroppedNotAddedToTheFreshDocument) {
    // SHOULD-FIX (finding 4): a plugin instrument load still in flight when New Patch replaces the
    // document must not land as a track in the FRESH document once it completes.
    InstrumentPluginStubBackendCFT backend;
    backend.factories["Stub Synth"] = [] {
        return std::make_unique<synth::test::StubPluginInstance>(0, 2, "Stub Synth");
    };
    synth::HostedPluginBackend::ScopedDefault installed(&backend);

    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    auto& graph = mc.getAudioEngine().getGraph();
    auto& doc = mc.getTimelineDoc();
    mc.getPluginScanService().setCandidateSource([](const juce::String&) { return juce::StringArray(); });
    seedScanListCFT(mc.getPluginScanService(), {pluginDescriptionCFT("Stub Synth", 0xA1FA, /*isInstrument=*/true)});

    mc.getTimelinePanel().buildAddTrackMenu();
    mc.getTimelinePanel().applyAddTrackMenuChoice(synth::ui::TimelinePanelComponent::kAddInstrumentPluginMenuIdBase +
                                                  0);
    // The load is now in flight (InstrumentPluginStubBackendCFT completes via callAsync, i.e. a
    // later message-loop turn) — nothing has touched the graph or doc yet, since
    // buildInstrumentTrackAndChain only ever runs once onLoadCompleted fires.
    ASSERT_TRUE(doc.getTracks().empty());

    // New Patch — the REAL guarded flow (guardUnsavedChanges("New Patch", ...), same seam a menu
    // click or Cmd+N goes through) — replaces the document WHILE that load is still pending. Let
    // New Patch's OWN (unrelated) undo-tracked steps settle before touching the stale load at all,
    // so the edit-serial comparison below isolates just the stale completion's effect.
    mc.simulateNewPatchClick();
    pumpUntilCFT([] { return false; }, 50);
    const int editSerialAfterNewPatchSettled = mc.getUndoManager().getEditSerial();

    // Now let the stale load's completion land. There is deliberately nothing to poll FOR beyond
    // that: a correctly dropped completion has no observable side effect at all, which is exactly
    // what this proves.
    pumpUntilCFT([] { return false; }, 300);

    EXPECT_TRUE(doc.getTracks().empty()) << "the stale load must not land a track in the fresh document";
    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::HostedPlugin), 0)
        << "the stale plugin instance must never join the fresh document's graph";
    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::ChannelStrip), 0);
    EXPECT_EQ(mc.getUndoManager().getEditSerial(), editSerialAfterNewPatchSettled)
        << "the dropped completion must push no undo step of its own, on top of New Patch's own";
}

TEST_F(ChannelFlowTest, PluginInstrumentTrackMonoInstanceDuplicatesOntoBothChannelLegs) {
    // TEST GAP (finding 5a): the real menu -> applyAddTrackMenuChoice -> buildInstrumentTrackAndChain
    // path for a genuinely MONO instrument instance. HostedPluginModule::rightAudioLegChannel()
    // reads 0 (not -1, not a real ch1) once the published instance has exactly one real output —
    // see HostedPluginTest.RightAudioLegChannelFollowsThePublishedInstancesRealOutputCount for the
    // module-level contract this proves end-to-end through the real menu/chain-build path.
    InstrumentPluginStubBackendCFT backend;
    backend.factories["Mono Synth"] = [] {
        return std::make_unique<synth::test::StubPluginInstance>(0, 1, "Mono Synth");
    };
    synth::HostedPluginBackend::ScopedDefault installed(&backend);

    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    auto& graph = mc.getAudioEngine().getGraph();
    mc.getPluginScanService().setCandidateSource([](const juce::String&) { return juce::StringArray(); });
    seedScanListCFT(mc.getPluginScanService(), {pluginDescriptionCFT("Mono Synth", 0x3001, /*isInstrument=*/true)});

    mc.getTimelinePanel().buildAddTrackMenu();
    mc.getTimelinePanel().applyAddTrackMenuChoice(synth::ui::TimelinePanelComponent::kAddInstrumentPluginMenuIdBase +
                                                  0);
    ASSERT_TRUE(pumpUntilCFT([&] { return countNodesOfTypeCFT(graph, ModuleType::ChannelStrip) == 1; }))
        << "the async load/chain-build never completed";

    auto* plugin = findNodeOfTypeCFT(graph, ModuleType::HostedPlugin);
    auto* eq = findNodeOfTypeCFT(graph, ModuleType::ParametricEQ);
    ASSERT_NE(plugin, nullptr);
    ASSERT_NE(eq, nullptr);
    auto* hostedModule = dynamic_cast<ModuleBase*>(plugin->getProcessor());
    ASSERT_NE(hostedModule, nullptr);
    EXPECT_EQ(hostedModule->rightAudioLegChannel(), 0) << "a genuinely mono instance's right leg is its own ch0";

    bool leftWired = false, rightFromCh0Wired = false, rightFromCh1Wired = false;
    for (const auto& c : graph.getConnections()) {
        if (c.source.nodeID != plugin->nodeID || c.destination.nodeID != eq->nodeID)
            continue;
        if (c.source.channelIndex == 0 && c.destination.channelIndex == 0)
            leftWired = true;
        if (c.source.channelIndex == 0 && c.destination.channelIndex == 1)
            rightFromCh0Wired = true;
        if (c.source.channelIndex == 1 && c.destination.channelIndex == 1)
            rightFromCh1Wired = true;
    }
    EXPECT_TRUE(leftWired) << "the plugin's one real output must reach EQ L";
    EXPECT_TRUE(rightFromCh0Wired) << "the SAME ch0 output must also reach EQ R — a mono instance duplicates "
                                      "onto both legs rather than going silent on the right";
    EXPECT_FALSE(rightFromCh1Wired) << "there is no real ch1 to wire from on a mono instance";
}

TEST_F(ChannelFlowTest, PluginInstrumentTrackOverMaxChannelsIsRefusedGraphAndUndoUntouched) {
    // TEST GAP (finding 5b): a plugin instance whose real channel count exceeds
    // HostedPluginModule::kMaxPluginChannels is REFUSED inside publishInstance(), never truncated
    // (Source/Modules/CLAUDE.md's invariant). hasInstance() reads that refusal identically to an
    // outright backend failure, so this must behave exactly like
    // PluginInstrumentTrackFailedLoadLeavesGraphAndUndoUntouched: no track, no node, no undo step.
    InstrumentPluginStubBackendCFT backend;
    backend.factories["Huge Synth"] = [] {
        return std::make_unique<synth::test::StubPluginInstance>(0, synth::HostedPluginModule::kMaxPluginChannels + 1,
                                                                 "Huge Synth");
    };
    synth::HostedPluginBackend::ScopedDefault installed(&backend);

    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    auto& graph = mc.getAudioEngine().getGraph();
    auto& doc = mc.getTimelineDoc();
    mc.getPluginScanService().setCandidateSource([](const juce::String&) { return juce::StringArray(); });
    seedScanListCFT(mc.getPluginScanService(), {pluginDescriptionCFT("Huge Synth", 0x4001, /*isInstrument=*/true)});

    const int tracksBefore = (int)doc.getTracks().size();
    const int nodesBefore = graph.getNumNodes();
    const juce::String messageBefore = mc.getStatusBar().getTransientMessageForTest();

    mc.getTimelinePanel().buildAddTrackMenu();
    mc.getTimelinePanel().applyAddTrackMenuChoice(synth::ui::TimelinePanelComponent::kAddInstrumentPluginMenuIdBase +
                                                  0);

    ASSERT_TRUE(pumpUntilCFT([&] { return mc.getStatusBar().getTransientMessageForTest() != messageBefore; }))
        << "the refusal was never reported";

    EXPECT_EQ((int)doc.getTracks().size(), tracksBefore) << "an over-max instance must leave no orphan track";
    EXPECT_EQ(graph.getNumNodes(), nodesBefore) << "an over-max instance must leave no orphan node — not even a "
                                                   "bare Hosted Plugin placeholder";
    EXPECT_FALSE(mc.getUndoManager().canUndo()) << "an over-max refusal must push no undo step";

    // Same later-turn teardown as the failed-load test above.
    pumpUntilCFT([] { return false; }, 100);
    EXPECT_EQ((int)doc.getTracks().size(), tracksBefore);
    EXPECT_EQ(graph.getNumNodes(), nodesBefore);
    EXPECT_FALSE(mc.getUndoManager().canUndo());
}

// synth::addVoiceMixerForPolyInstrument / the poly branch of MainComponent::addInstrumentTrack's
// chain-source selection, exercised directly at the ChannelFlows level: a factory-default
// Oscillator is poly OFF (see InstrumentTrackOscillatorWiresSplitBlockRightLegNeverCh1 above), so
// the golden "+ Track -> Instrument" path never takes this branch today — this proves it wires
// correctly for whenever an instrument IS poly (docs/mixer.md §5.4/§5.8).
TEST_F(ChannelFlowTest, PolyInstrumentGetsVoiceMixerAheadOfStripAndFeedsTheChannel) {
    AudioEngine engine;
    auto& graph = engine.getGraph();

    auto oscProcessor = synth::AIStateMapper::createModule("Oscillator");
    ASSERT_NE(oscProcessor, nullptr);
    ASSERT_TRUE(setPolyParamCFT(oscProcessor.get(), true));
    auto oscNode = graph.addNode(std::move(oscProcessor));
    ASSERT_NE(oscNode, nullptr);

    juce::String voiceMixerUuid;
    auto* voiceMixer = synth::addVoiceMixerForPolyInstrument(graph, *oscNode, {0, 0}, voiceMixerUuid);
    ASSERT_NE(voiceMixer, nullptr);
    EXPECT_FALSE(voiceMixerUuid.isEmpty());
    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::VoiceMixer), 1);

    for (int voice = 0; voice < 8; ++voice)
        EXPECT_TRUE(graph.isConnected({{oscNode->nodeID, voice}, {voiceMixer->nodeID, voice}}))
            << "voice " << voice << " must be summed into the Voice Mixer";

    const synth::DefaultChannelLayout layout{{100, 0}, {200, 0}, {300, 0}, {400, 0}};
    const auto channel = synth::buildDefaultAudioChannel(graph, *voiceMixer, layout);
    ASSERT_FALSE(channel.stripUuid.isEmpty());

    auto* eq = findNodeOfTypeCFT(graph, ModuleType::ParametricEQ);
    ASSERT_NE(eq, nullptr);
    EXPECT_TRUE(graph.isConnected({{voiceMixer->nodeID, 0}, {eq->nodeID, 0}}))
        << "Voice Mixer's own ch0/ch1 output satisfies buildDefaultAudioChannel's default contiguous-pair contract";
    EXPECT_TRUE(graph.isConnected({{voiceMixer->nodeID, 1}, {eq->nodeID, 1}}));
}

TEST_F(ChannelFlowTest, NonPolyInstrumentGetsNoVoiceMixer) {
    AudioEngine engine;
    auto& graph = engine.getGraph();

    auto oscProcessor = synth::AIStateMapper::createModule("Oscillator");
    ASSERT_NE(oscProcessor, nullptr);
    // Factory default: poly OFF — no setPolyParamCFT call.
    auto oscNode = graph.addNode(std::move(oscProcessor));
    ASSERT_NE(oscNode, nullptr);

    juce::String voiceMixerUuid;
    auto* voiceMixer = synth::addVoiceMixerForPolyInstrument(graph, *oscNode, {0, 0}, voiceMixerUuid);
    EXPECT_EQ(voiceMixer, nullptr);
    EXPECT_TRUE(voiceMixerUuid.isEmpty());
    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::VoiceMixer), 0);
}

// synth::addEnvelopeAndVCAForRawInstrument, exercised directly at the ChannelFlows level for the
// poly case (a factory-default Oscillator is poly OFF, so the golden "+ Track -> Instrument" path
// never exercises composition with a Voice Mixer today — see PolyInstrumentGetsVoiceMixerAheadOf-
// StripAndFeedsTheChannel above for the same reasoning). Proves the VCA is inserted AFTER the Voice
// Mixer's poly-voice sum, both forced non-poly, exactly as MainComponent::addInstrumentTrack does.
TEST_F(ChannelFlowTest, EnvelopeAndVCAComposeAfterVoiceMixerForPolyInstrument) {
    AudioEngine engine;
    auto& graph = engine.getGraph();

    auto trackInProcessor = synth::AIStateMapper::createModule("Track In");
    ASSERT_NE(trackInProcessor, nullptr);
    auto trackInNode = graph.addNode(std::move(trackInProcessor));
    ASSERT_NE(trackInNode, nullptr);

    auto oscProcessor = synth::AIStateMapper::createModule("Oscillator");
    ASSERT_NE(oscProcessor, nullptr);
    ASSERT_TRUE(setPolyParamCFT(oscProcessor.get(), true));
    auto oscNode = graph.addNode(std::move(oscProcessor));
    ASSERT_NE(oscNode, nullptr);

    juce::String voiceMixerUuid;
    auto* voiceMixer = synth::addVoiceMixerForPolyInstrument(graph, *oscNode, {100, 0}, voiceMixerUuid);
    ASSERT_NE(voiceMixer, nullptr);

    const auto envAndVca = synth::addEnvelopeAndVCAForRawInstrument(graph, *trackInNode, *voiceMixer,
                                                                    /*chainSourceRightChannel=*/1, {200, 0}, {300, 0});
    ASSERT_NE(envAndVca.vca, nullptr);
    EXPECT_FALSE(envAndVca.adsrUuid.isEmpty());
    EXPECT_FALSE(envAndVca.vcaUuid.isEmpty());

    auto* adsrNode = findNodeOfTypeCFT(graph, ModuleType::ADSR);
    ASSERT_NE(adsrNode, nullptr);
    for (auto* param : adsrNode->getProcessor()->getParameters())
        if (auto* boolParam = dynamic_cast<juce::AudioParameterBool*>(param))
            if (boolParam->paramID == "poly")
                EXPECT_FALSE(boolParam->get()) << "the ADSR must stay non-poly even for a poly instrument";
    for (auto* param : envAndVca.vca->getProcessor()->getParameters())
        if (auto* boolParam = dynamic_cast<juce::AudioParameterBool*>(param))
            if (boolParam->paramID == "poly")
                EXPECT_FALSE(boolParam->get()) << "the VCA must stay non-poly even for a poly instrument";

    // Voice Mixer's summed ch0/ch1 -> VCA Audio L/R (never the poly instrument's raw ch0-7 directly).
    EXPECT_TRUE(graph.isConnected({{voiceMixer->nodeID, 0}, {envAndVca.vca->nodeID, 0}}));
    EXPECT_TRUE(graph.isConnected({{voiceMixer->nodeID, 1}, {envAndVca.vca->nodeID, VCAModule::kRightBase}}));

    const synth::DefaultChannelLayout layout{{400, 0}, {500, 0}, {600, 0}, {700, 0}};
    const auto channel = synth::buildDefaultAudioChannel(graph, *envAndVca.vca, layout, VCAModule::kRightBase);
    ASSERT_FALSE(channel.stripUuid.isEmpty()) << "the VCA's output must satisfy buildDefaultAudioChannel too";
}

// FRO46 (P9-3j): synth::addPolyEnvelopeAndVCAForInstrument, exercised directly at the ChannelFlows
// level (a factory-default Oscillator is poly OFF, so the golden "+ Track -> Instrument" path never
// takes this branch today, same reasoning as the poly tests above) — proves a poly instrument gets
// a TRUE per-voice envelope: Poly MIDI's Pitch/Gate fans feed the instrument and a poly ADSR
// directly, no Voice Mixer, both ADSR and VCA stay poly (never forced non-poly).
TEST_F(ChannelFlowTest, PolyEnvelopeAndVCAWiresPerVoicePitchGateAndAudioWithNoVoiceMixer) {
    AudioEngine engine;
    auto& graph = engine.getGraph();

    auto trackInProcessor = synth::AIStateMapper::createModule("Track In");
    ASSERT_NE(trackInProcessor, nullptr);
    auto trackInNode = graph.addNode(std::move(trackInProcessor));
    ASSERT_NE(trackInNode, nullptr);

    auto oscProcessor = synth::AIStateMapper::createModule("Oscillator");
    ASSERT_NE(oscProcessor, nullptr);
    ASSERT_TRUE(setPolyParamCFT(oscProcessor.get(), true));
    auto oscNode = graph.addNode(std::move(oscProcessor));
    ASSERT_NE(oscNode, nullptr);

    const auto polyEnv =
        synth::addPolyEnvelopeAndVCAForInstrument(graph, *trackInNode, *oscNode, {100, 0}, {200, 0}, {300, 0});
    ASSERT_NE(polyEnv.vca, nullptr);
    EXPECT_FALSE(polyEnv.polyMidiUuid.isEmpty());
    EXPECT_FALSE(polyEnv.adsrUuid.isEmpty());
    EXPECT_FALSE(polyEnv.vcaUuid.isEmpty());

    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::PolyMidi), 1);
    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::VoiceMixer), 0) << "the poly VCA does its own 8-voice summing";

    auto* polyMidiNode = findNodeOfTypeCFT(graph, ModuleType::PolyMidi);
    auto* adsrNode = findNodeOfTypeCFT(graph, ModuleType::ADSR);
    ASSERT_NE(polyMidiNode, nullptr);
    ASSERT_NE(adsrNode, nullptr);

    for (auto* param : adsrNode->getProcessor()->getParameters()) {
        if (auto* boolParam = dynamic_cast<juce::AudioParameterBool*>(param))
            if (boolParam->paramID == "poly")
                EXPECT_TRUE(boolParam->get()) << "the ADSR must be poly — its gate now comes from Poly MIDI CV";
        if (auto* floatParam = dynamic_cast<juce::AudioParameterFloat*>(param))
            if (floatParam->paramID == "sustain")
                EXPECT_FLOAT_EQ(floatParam->get(), 0.7f) << "same sustain override as the non-poly path";
    }
    for (auto* param : polyEnv.vca->getProcessor()->getParameters()) {
        if (auto* boolParam = dynamic_cast<juce::AudioParameterBool*>(param))
            if (boolParam->paramID == "poly")
                EXPECT_TRUE(boolParam->get()) << "the VCA must be poly";
        if (auto* floatParam = dynamic_cast<juce::AudioParameterFloat*>(param))
            if (floatParam->paramID == "gain")
                EXPECT_FLOAT_EQ(floatParam->get(), 1.0f) << "same gain override as the non-poly path";
    }

    EXPECT_TRUE(graph.isConnected({{trackInNode->nodeID, juce::AudioProcessorGraph::midiChannelIndex},
                                   {polyMidiNode->nodeID, juce::AudioProcessorGraph::midiChannelIndex}}))
        << "Poly MIDI, not the ADSR, must be gated by Track In's MIDI";

    for (int voice = 0; voice < 8; ++voice) {
        EXPECT_TRUE(graph.isConnected({{polyMidiNode->nodeID, voice}, {oscNode->nodeID, voice}}))
            << "voice " << voice << ": Poly MIDI's Pitch fan must feed the instrument's poly Pitch CV";
        EXPECT_TRUE(graph.isConnected({{polyMidiNode->nodeID, 8 + voice}, {adsrNode->nodeID, voice}}))
            << "voice " << voice << ": Poly MIDI's Gate fan must feed the ADSR's poly Gate CV";
        EXPECT_TRUE(
            graph.isConnected({{adsrNode->nodeID, voice}, {polyEnv.vca->nodeID, VCAModule::kPolyCVBase + voice}}))
            << "voice " << voice << ": ADSR's poly Env out must feed the VCA's poly Gain CV";
        EXPECT_TRUE(graph.isConnected({{oscNode->nodeID, voice}, {polyEnv.vca->nodeID, voice}}))
            << "voice " << voice << ": the instrument's poly Audio L must feed the VCA's poly Audio L in";
    }

    const synth::DefaultChannelLayout layout{{400, 0}, {500, 0}, {600, 0}, {700, 0}};
    const auto channel = synth::buildDefaultAudioChannel(graph, *polyEnv.vca, layout, /*sourceRightChannel=*/1);
    ASSERT_FALSE(channel.stripUuid.isEmpty())
        << "the VCA's summed ch0/ch1 output must satisfy buildDefaultAudioChannel";
}

// FRO48 (P9-3k): the "+ Track -> Instrument -> Oscillator (Poly)" menu entry is the first real UI
// entry point for the poly-envelope auto-wire above — it must set the freshly created Oscillator's
// "poly" parameter BEFORE MainComponent::addInstrumentTrack's own isProcessorPoly check runs, so
// the golden "+ Track" path (never poly today — see InstrumentTrackOscillatorWiresSplitBlockRight-
// LegNeverCh1 above) can actually reach PolyEnvelopeAndVCAWiresPerVoicePitchGateAndAudioWithNo-
// VoiceMixer's wiring. Drives the real menu seam (applyAddTrackMenuChoice), not
// MainComponent::addInstrumentTrack or synth::addPolyEnvelopeAndVCAForInstrument directly.
TEST_F(ChannelFlowTest, AddInstrumentTrackMenuOscillatorPolyWiresPolyEnvelopeAndVCA) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    auto& graph = mc.getAudioEngine().getGraph();
    auto& macros = mc.getGraphEditor().getMacros();

    addInstrumentTrack(mc, "Oscillator", true);

    auto* oscillator = findNodeOfTypeCFT(graph, ModuleType::Oscillator);
    ASSERT_NE(oscillator, nullptr);
    EXPECT_TRUE(synth::isProcessorPoly(oscillator->getProcessor()))
        << "the (Poly) menu entry must have turned the instrument's own poly parameter on";

    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::PolyMidi), 1);
    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::VoiceMixer), 0)
        << "the poly VCA does its own 8-voice summing — no separate Voice Mixer stage";

    ASSERT_EQ(macros.size(), 1);
    const auto& macro = macros.getAll().front();
    // The factory default preset every fresh MainComponent loads already has its own ADSR-type
    // nodes and a VCA node — disambiguate via macro membership, not "last one seen".
    auto* adsrNode = findMacroMemberOfTypeCFT(graph, macro, ModuleType::ADSR);
    auto* vcaNode = findMacroMemberOfTypeCFT(graph, macro, ModuleType::VCA);
    ASSERT_NE(adsrNode, nullptr);
    ASSERT_NE(vcaNode, nullptr);
    for (auto* param : adsrNode->getProcessor()->getParameters())
        if (auto* boolParam = dynamic_cast<juce::AudioParameterBool*>(param))
            if (boolParam->paramID == "poly")
                EXPECT_TRUE(boolParam->get()) << "the ADSR must be poly — its gate comes from Poly MIDI CV";
    for (auto* param : vcaNode->getProcessor()->getParameters())
        if (auto* boolParam = dynamic_cast<juce::AudioParameterBool*>(param))
            if (boolParam->paramID == "poly")
                EXPECT_TRUE(boolParam->get()) << "the VCA must be poly";

    // The default downstream chain must still be built, exactly as the non-poly path gets.
    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::ParametricEQ), 1);
    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::Compressor), 1);
    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::ChannelStrip), 1);
    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::Master), 1);
}

// Same as AddInstrumentTrackMenuOscillatorPolyWiresPolyEnvelopeAndVCA above, for the Wavetable
// entry — Wavetable gets the same poly-envelope auto-wire as Oscillator (P9-3j).
TEST_F(ChannelFlowTest, AddInstrumentTrackMenuWavetablePolyWiresPolyEnvelopeAndVCA) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    auto& graph = mc.getAudioEngine().getGraph();
    auto& macros = mc.getGraphEditor().getMacros();

    addInstrumentTrack(mc, "Wavetable", true);

    auto* wavetable = findNodeOfTypeCFT(graph, ModuleType::Wavetable);
    ASSERT_NE(wavetable, nullptr);
    EXPECT_TRUE(synth::isProcessorPoly(wavetable->getProcessor()))
        << "the (Poly) menu entry must have turned the instrument's own poly parameter on";

    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::PolyMidi), 1);
    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::VoiceMixer), 0)
        << "the poly VCA does its own 8-voice summing — no separate Voice Mixer stage";

    ASSERT_EQ(macros.size(), 1);
    const auto& macro = macros.getAll().front();
    auto* adsrNode = findMacroMemberOfTypeCFT(graph, macro, ModuleType::ADSR);
    auto* vcaNode = findMacroMemberOfTypeCFT(graph, macro, ModuleType::VCA);
    ASSERT_NE(adsrNode, nullptr);
    ASSERT_NE(vcaNode, nullptr);
    for (auto* param : adsrNode->getProcessor()->getParameters())
        if (auto* boolParam = dynamic_cast<juce::AudioParameterBool*>(param))
            if (boolParam->paramID == "poly")
                EXPECT_TRUE(boolParam->get()) << "the ADSR must be poly — its gate comes from Poly MIDI CV";
    for (auto* param : vcaNode->getProcessor()->getParameters())
        if (auto* boolParam = dynamic_cast<juce::AudioParameterBool*>(param))
            if (boolParam->paramID == "poly")
                EXPECT_TRUE(boolParam->get()) << "the VCA must be poly";

    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::ParametricEQ), 1);
    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::Compressor), 1);
    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::ChannelStrip), 1);
    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::Master), 1);
}

// Regression guard for the two tests above: the plain (non-poly) "Oscillator" menu entry must not
// have started taking the poly-envelope branch for everyone.
TEST_F(ChannelFlowTest, AddInstrumentTrackMenuOscillatorNonPolyStaysNonPoly) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    auto& graph = mc.getAudioEngine().getGraph();

    addInstrumentTrack(mc, "Oscillator");

    auto* oscillator = findNodeOfTypeCFT(graph, ModuleType::Oscillator);
    ASSERT_NE(oscillator, nullptr);
    EXPECT_FALSE(synth::isProcessorPoly(oscillator->getProcessor()))
        << "the plain menu entry must leave the instrument's poly parameter off";
    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::PolyMidi), 0)
        << "the non-poly menu entry must never take the poly-envelope branch";
    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::VoiceMixer), 0)
        << "a freshly created Oscillator defaults to poly OFF — no Voice Mixer needed";
}

// =================================================================================================
// T184 (P9-3c, docs/mixer.md §5.2 "main workflow"): a MIDI track auto-creates the destination's
// mixer channel on connect. Core-level tests for synth::findUnchanneledOutputFeeds /
// synth::buildChannelForFeeds first, then real-mouse-gesture coverage through GraphEditor's
// endConnectionDrag (docs/testing.md's "test the real mouse path" guidance — the same reason
// Tests/MacroPortRealMouseDragTests.cpp drives ModuleComponent::mouseDown/mouseDrag/mouseUp
// directly rather than calling GraphEditor's drag methods).
// =================================================================================================

namespace {

// mouseDownLocalPos is the FIXED press-point (local to eventComp), unchanged across an entire
// gesture; localPos is where the cursor is RIGHT NOW for this specific event. Same shape as
// MacroPortRealMouseDragTests.cpp's own helper — duplicated here since that one is scoped to its
// own translation unit's anonymous namespace.
juce::MouseEvent realMouseEventCFT(juce::Component& eventComp, juce::Point<int> localPos,
                                   juce::Point<int> mouseDownLocalPos, juce::ModifierKeys mods,
                                   bool wasDragged = false) {
    const auto pos = localPos.toFloat();
    const auto downPos = mouseDownLocalPos.toFloat();
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), pos, mods, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                            &eventComp, &eventComp, juce::Time::getCurrentTime(), downPos, juce::Time::getCurrentTime(),
                            1, wasDragged);
}

ModuleComponent* compForCFT(GraphEditor& editor, juce::AudioProcessorGraph::NodeID id) {
    for (auto* c : editor.getModuleComponents())
        if (c != nullptr && c->getNodeId() == id)
            return c;
    return nullptr;
}

// The MIDI analog of MacroPortRealMouseDragTests.cpp's dragRealCableBetween (which only drives
// audio/CV jacks via ModuleComponent::getPortCenter): drives a full real mouseDown -> mouseDrag ->
// mouseUp gesture from `srcComp`'s MIDI OUT jack to `dstComp`'s MIDI IN jack, exactly like a real
// user's press-drag-release. A MIDI jack's position comes from ModuleComponent::getMidiPortCenter
// (fixed top-right/top-left, not part of the indexed audio-jack gutter — see
// ModuleComponent::getPortForPoint's own MIDI special-case).
void dragRealMidiCableBetweenCFT(ModuleComponent& srcComp, ModuleComponent& dstComp) {
    const juce::ModifierKeys leftClick(juce::ModifierKeys::leftButtonModifier);
    const auto srcJackLocal = srcComp.getMidiPortCenter(true);
    srcComp.mouseDown(realMouseEventCFT(srcComp, srcJackLocal, srcJackLocal, leftClick));

    const auto targetScreenPos = dstComp.localPointToGlobal(dstComp.getMidiPortCenter(false));
    const auto srcLocalForTarget = srcComp.getLocalPoint(nullptr, targetScreenPos);
    srcComp.mouseDrag(realMouseEventCFT(srcComp, srcLocalForTarget, srcJackLocal, leftClick, /*wasDragged=*/true));
    srcComp.mouseUp(realMouseEventCFT(srcComp, srcLocalForTarget, srcJackLocal, leftClick, /*wasDragged=*/true));
}

// Adds one node through the factory, mirrors a fresh uuid into BOTH the node property and the
// processor (ModuleBase::setNodeUuid — Source/CLAUDE.md's uuid-mirroring invariant), and records
// its canvas position. Returns nullptr on a factory/addNode failure. Deliberately bypasses
// GraphEditor/AppUndoManager entirely — these tests build their "instrument already wired to the
// output, by hand" starting state the same non-undoable way the Core-level tests above do.
juce::AudioProcessorGraph::Node* addPlainNodeCFT(juce::AudioProcessorGraph& graph, const juce::String& typeName,
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

} // namespace

// -------------------------------------------------------------------------------------------
// Latency (per the T184 brief: STOP and report if either is nonzero rather than working around
// it — the whole feature premise is inserting this chain into a path that previously went
// straight to the output).
// -------------------------------------------------------------------------------------------

TEST(ChannelFlowAutoChannelCore, BypassedEQAndCompressorReportZeroLatencyAfterPrepare) {
    auto eq = synth::AIStateMapper::createModule("Parametric EQ");
    auto compressor = synth::AIStateMapper::createModule("Compressor");
    ASSERT_NE(eq, nullptr);
    ASSERT_NE(compressor, nullptr);
    if (auto* m = dynamic_cast<ModuleBase*>(eq.get()))
        m->setBypassed(true);
    if (auto* m = dynamic_cast<ModuleBase*>(compressor.get()))
        m->setBypassed(true);
    eq->prepareToPlay(44100.0, 512);
    compressor->prepareToPlay(44100.0, 512);
    EXPECT_EQ(eq->getLatencySamples(), 0) << "an inserted-but-bypassed EQ must add no latency to the chain";
    EXPECT_EQ(compressor->getLatencySamples(), 0) << "an inserted-but-bypassed Compressor must add no latency";
}

// -------------------------------------------------------------------------------------------
// synth::findUnchanneledOutputFeeds
// -------------------------------------------------------------------------------------------

TEST(ChannelFlowAutoChannelCore, FindUnchanneledOutputFeedsOnInstrumentToOutputFindsTwoExits) {
    AudioEngine engine;
    auto& graph = engine.getGraph();
    graph.setPlayConfigDetails(0, 2, 44100.0, 512);
    auto outNode = graph.addNode(synth::AIStateMapper::createModule("Audio Output"));
    ASSERT_NE(outNode, nullptr);

    juce::String oscUuid;
    auto* osc = addPlainNodeCFT(graph, "Oscillator", {0, 0}, oscUuid);
    ASSERT_NE(osc, nullptr);
    graph.addConnection({{osc->nodeID, 0}, {outNode->nodeID, 0}});
    graph.addConnection({{osc->nodeID, 1}, {outNode->nodeID, 1}});

    const auto exits = synth::findUnchanneledOutputFeeds(graph, osc->nodeID);
    EXPECT_EQ(exits.size(), 2u);
}

TEST(ChannelFlowAutoChannelCore, FindUnchanneledOutputFeedsOnStripChanneledInstrumentFindsZero) {
    AudioEngine engine;
    auto& graph = engine.getGraph();
    graph.setPlayConfigDetails(0, 2, 44100.0, 512);
    auto outNode = graph.addNode(synth::AIStateMapper::createModule("Audio Output"));
    ASSERT_NE(outNode, nullptr);

    auto stripProcessor = synth::AIStateMapper::createModule("Channel Strip");
    ASSERT_NE(stripProcessor, nullptr);
    if (auto* stripModule = dynamic_cast<ChannelStripModule*>(stripProcessor.get()))
        stripModule->setShape(ChannelStripModule::Shape::Stereo);
    auto stripNode = graph.addNode(std::move(stripProcessor));
    ASSERT_NE(stripNode, nullptr);

    juce::String oscUuid;
    auto* osc = addPlainNodeCFT(graph, "Oscillator", {0, 0}, oscUuid);
    ASSERT_NE(osc, nullptr);
    graph.addConnection({{osc->nodeID, 0}, {stripNode->nodeID, 0}});
    graph.addConnection({{osc->nodeID, 1}, {stripNode->nodeID, ChannelStripModule::kRightBase}});
    graph.addConnection({{stripNode->nodeID, 0}, {outNode->nodeID, 0}});
    graph.addConnection({{stripNode->nodeID, ChannelStripModule::kRightBase}, {outNode->nodeID, 1}});

    const auto exits = synth::findUnchanneledOutputFeeds(graph, osc->nodeID);
    EXPECT_TRUE(exits.empty()) << "already reaches the output through a Channel Strip";
}

TEST(ChannelFlowAutoChannelCore, FindUnchanneledOutputFeedsNotReachingOutputFindsZero) {
    AudioEngine engine;
    auto& graph = engine.getGraph();
    juce::String oscUuid, filterUuid;
    auto* osc = addPlainNodeCFT(graph, "Oscillator", {0, 0}, oscUuid);
    auto* filter = addPlainNodeCFT(graph, "Filter", {200, 0}, filterUuid);
    ASSERT_NE(osc, nullptr);
    ASSERT_NE(filter, nullptr);
    graph.addConnection({{osc->nodeID, 0}, {filter->nodeID, 0}});

    const auto exits = synth::findUnchanneledOutputFeeds(graph, osc->nodeID);
    EXPECT_TRUE(exits.empty()) << "no Audio Output/Rec Tap/Master node exists downstream at all";
}

TEST(ChannelFlowAutoChannelCore,
     FindUnchanneledOutputFeedsIgnoresModulationBranchAndSweepsTheUnrelatedPathOntoMasterDirect) {
    AudioEngine engine;
    auto& graph = engine.getGraph();
    graph.setPlayConfigDetails(0, 2, 44100.0, 512);
    auto outNode = graph.addNode(synth::AIStateMapper::createModule("Audio Output"));
    ASSERT_NE(outNode, nullptr);

    juce::String oscAUuid, oscBUuid, filterUuid;
    auto* oscA = addPlainNodeCFT(graph, "Oscillator", {0, 0}, oscAUuid);
    auto* oscB = addPlainNodeCFT(graph, "Oscillator", {0, 300}, oscBUuid);
    auto* filter = addPlainNodeCFT(graph, "Filter", {200, 300}, filterUuid);
    ASSERT_NE(oscA, nullptr);
    ASSERT_NE(oscB, nullptr);
    ASSERT_NE(filter, nullptr);

    // oscA's own straight path to the output -- the two exits this search should find.
    graph.addConnection({{oscA->nodeID, 0}, {outNode->nodeID, 0}});
    graph.addConnection({{oscA->nodeID, 1}, {outNode->nodeID, 1}});

    // oscA ALSO modulates the unrelated Filter's cutoff -- a hidden AttenuverterModule leg that
    // must be neither traversed nor counted as an exit.
    auto* filterModule = dynamic_cast<ModuleBase*>(filter->getProcessor());
    ASSERT_NE(filterModule, nullptr);
    int cutoffChannel = -1;
    for (const auto& target : filterModule->getModulationTargets()) {
        cutoffChannel = target.channelIndex;
        break;
    }
    ASSERT_GE(cutoffChannel, 0) << "Filter must declare at least one modulation target";
    engine.addModRouting(oscA->nodeID, 0, filter->nodeID, cutoffChannel);

    // The unrelated Filter's OWN, totally separate signal path to the output -- must survive
    // untouched by a search rooted at oscA.
    graph.addConnection({{oscB->nodeID, 0}, {filter->nodeID, 0}});
    graph.addConnection({{oscB->nodeID, 1}, {filter->nodeID, 1}});
    graph.addConnection({{filter->nodeID, 0}, {outNode->nodeID, 0}});
    graph.addConnection({{filter->nodeID, 1}, {outNode->nodeID, 1}});

    const auto exits = synth::findUnchanneledOutputFeeds(graph, oscA->nodeID);
    ASSERT_EQ(exits.size(), 2u);
    for (const auto& exit : exits)
        EXPECT_EQ(exit.source.nodeID, oscA->nodeID) << "only oscA's own direct path may appear";

    const synth::DefaultChannelLayout layout{{500, 0}, {600, 0}, {700, 0}, {800, 0}};
    const auto channel = synth::buildChannelForFeeds(graph, exits, layout);
    ASSERT_FALSE(channel.stripUuid.isEmpty());
    ASSERT_NE(channel.master, nullptr);

    // The unrelated Filter -> oscB leg itself is completely untouched by the build.
    EXPECT_TRUE(graph.isConnected({{oscB->nodeID, 0}, {filter->nodeID, 0}}));
    EXPECT_TRUE(graph.isConnected({{oscB->nodeID, 1}, {filter->nodeID, 1}}));

    // Filter's own straight-to-output feed pre-dated Master, so spliceMasterNode's sweep (run as
    // part of building oscA's channel, since no Master yet existed) re-routes it onto Master's
    // Direct bus like every other pre-existing direct feed -- see MasterSplice.cpp's own contract
    // ("every audio connection that fed that node's ch0/ch1 is re-routed into Master"). It still
    // reaches the output, just via Master now, exactly like oscA's own feed does.
    EXPECT_FALSE(graph.isConnected({{filter->nodeID, 0}, {outNode->nodeID, 0}}))
        << "swept onto Master's Direct bus by the same-transaction spliceMasterNode call";
    EXPECT_FALSE(graph.isConnected({{filter->nodeID, 1}, {outNode->nodeID, 1}}));
    EXPECT_TRUE(graph.isConnected({{filter->nodeID, 0}, {channel.master->nodeID, MasterModule::kDirectLeft}}));
    EXPECT_TRUE(graph.isConnected({{filter->nodeID, 1}, {channel.master->nodeID, MasterModule::kDirectRight}}));
    // Master's OWN outputs are plain 0/1 (MasterModule::kNumOutputs) -- kDirectLeft/kDirectRight
    // are input-side indices only, never valid on the output side.
    EXPECT_TRUE(graph.isConnected({{channel.master->nodeID, 0}, {outNode->nodeID, 0}}));
    EXPECT_TRUE(graph.isConnected({{channel.master->nodeID, 1}, {outNode->nodeID, 1}}));
}

// -------------------------------------------------------------------------------------------
// synth::buildChannelForFeeds
// -------------------------------------------------------------------------------------------

TEST(ChannelFlowAutoChannelCore, BuildChannelForFeedsRemovesExitEdgesAndWiresThroughToANewMaster) {
    AudioEngine engine;
    auto& graph = engine.getGraph();
    graph.setPlayConfigDetails(0, 2, 44100.0, 512);
    auto outNode = graph.addNode(synth::AIStateMapper::createModule("Audio Output"));
    ASSERT_NE(outNode, nullptr);

    juce::String oscUuid;
    auto* osc = addPlainNodeCFT(graph, "Oscillator", {0, 0}, oscUuid);
    ASSERT_NE(osc, nullptr);
    graph.addConnection({{osc->nodeID, 0}, {outNode->nodeID, 0}});
    graph.addConnection({{osc->nodeID, 1}, {outNode->nodeID, 1}});

    const auto exits = synth::findUnchanneledOutputFeeds(graph, osc->nodeID);
    ASSERT_EQ(exits.size(), 2u);

    const synth::DefaultChannelLayout layout{{500, 0}, {600, 0}, {700, 0}, {800, 0}};
    const auto channel = synth::buildChannelForFeeds(graph, exits, layout);
    ASSERT_FALSE(channel.stripUuid.isEmpty());
    ASSERT_NE(channel.master, nullptr) << "no Master existed yet; this call must splice one";

    EXPECT_FALSE(graph.isConnected({{osc->nodeID, 0}, {outNode->nodeID, 0}})) << "the exit edges must be gone";
    EXPECT_FALSE(graph.isConnected({{osc->nodeID, 1}, {outNode->nodeID, 1}}));

    auto* eq = findNodeOfTypeCFT(graph, ModuleType::ParametricEQ);
    auto* strip = findNodeOfTypeCFT(graph, ModuleType::ChannelStrip);
    ASSERT_NE(eq, nullptr);
    ASSERT_NE(strip, nullptr);
    EXPECT_TRUE(graph.isConnected({{osc->nodeID, 0}, {eq->nodeID, 0}}));
    EXPECT_TRUE(graph.isConnected({{osc->nodeID, 1}, {eq->nodeID, 1}}));
    EXPECT_TRUE(graph.isConnected({{strip->nodeID, 0}, {channel.master->nodeID, MasterModule::kMixLeft}}));
    EXPECT_TRUE(graph.isConnected(
        {{strip->nodeID, ChannelStripModule::kRightBase}, {channel.master->nodeID, MasterModule::kMixRight}}));
    EXPECT_TRUE(graph.isConnected({{channel.master->nodeID, 0}, {outNode->nodeID, 0}}));
}

TEST(ChannelFlowAutoChannelCore, BuildChannelForFeedsReusesAnExistingMasterAndClearsDirectFeeds) {
    AudioEngine engine;
    auto& graph = engine.getGraph();
    graph.setPlayConfigDetails(0, 2, 44100.0, 512);
    auto outNode = graph.addNode(synth::AIStateMapper::createModule("Audio Output"));
    ASSERT_NE(outNode, nullptr);
    auto* master = synth::spliceMasterNode(graph, {400, 0});
    ASSERT_NE(master, nullptr);

    juce::String oscUuid;
    auto* osc = addPlainNodeCFT(graph, "Oscillator", {0, 0}, oscUuid);
    ASSERT_NE(osc, nullptr);
    graph.addConnection({{osc->nodeID, 0}, {master->nodeID, MasterModule::kDirectLeft}});
    graph.addConnection({{osc->nodeID, 1}, {master->nodeID, MasterModule::kDirectRight}});

    const auto exits = synth::findUnchanneledOutputFeeds(graph, osc->nodeID);
    ASSERT_EQ(exits.size(), 2u);

    const synth::DefaultChannelLayout layout{{500, 0}, {600, 0}, {700, 0}, {800, 0}};
    const auto channel = synth::buildChannelForFeeds(graph, exits, layout);
    ASSERT_FALSE(channel.stripUuid.isEmpty());
    EXPECT_EQ(channel.master, master) << "the pre-existing Master singleton must be reused, not duplicated";

    EXPECT_FALSE(graph.isConnected({{osc->nodeID, 0}, {master->nodeID, MasterModule::kDirectLeft}}))
        << "the exit edges (on Direct) must be gone";
    EXPECT_FALSE(graph.isConnected({{osc->nodeID, 1}, {master->nodeID, MasterModule::kDirectRight}}));

    auto* eq = findNodeOfTypeCFT(graph, ModuleType::ParametricEQ);
    auto* strip = findNodeOfTypeCFT(graph, ModuleType::ChannelStrip);
    ASSERT_NE(eq, nullptr);
    ASSERT_NE(strip, nullptr);
    EXPECT_TRUE(graph.isConnected({{osc->nodeID, 0}, {eq->nodeID, 0}}));
    EXPECT_TRUE(graph.isConnected({{osc->nodeID, 1}, {eq->nodeID, 1}}));
    EXPECT_TRUE(graph.isConnected({{strip->nodeID, 0}, {master->nodeID, MasterModule::kMixLeft}}));
    EXPECT_TRUE(graph.isConnected(
        {{strip->nodeID, ChannelStripModule::kRightBase}, {master->nodeID, MasterModule::kMixRight}}));
    EXPECT_FALSE(graph.isConnected({{osc->nodeID, 0}, {master->nodeID, MasterModule::kMixLeft}}))
        << "nothing of osc's own feeds Master Direct any more";
}

// -------------------------------------------------------------------------------------------
// Real-mouse-gesture coverage through GraphEditor::endConnectionDrag, via a live MainComponent.
// FIXTURE ORDER MATTERS: createTrackInNode() auto-wires a fresh Track In to the sole existing MIDI
// instrument when there is exactly one — so every test below starts from newPatchForTest() (zero
// nodes but a seeded Audio Output, T187) and adds the MIDI track BEFORE any instrument exists,
// keeping Track In unwired until the real-mouse gesture under test wires it.
// -------------------------------------------------------------------------------------------

TEST_F(ChannelFlowTest, AutoChannelOnConnect_ToggleOnBuildsOneChannelAsOneUndoStep) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    mc.newPatchForTest();

    mc.getTimelinePanel().applyAddTrackMenuChoice(synth::ui::TimelinePanelComponent::kAddMidiTrackMenuId);
    auto& graph = mc.getAudioEngine().getGraph();
    auto* trackIn = findNodeOfTypeCFT(graph, ModuleType::TimelineMidiSource);
    ASSERT_NE(trackIn, nullptr);

    // The instrument already wired to the output BY HAND -- exactly the "no channel yet" starting
    // state T184 targets.
    juce::String instrumentUuid;
    auto* instrument = addPlainNodeCFT(graph, "Oscillator", {600, 600}, instrumentUuid);
    ASSERT_NE(instrument, nullptr);
    auto* output = findNodeNamedCFT(graph, "Audio Output");
    ASSERT_NE(output, nullptr);
    graph.addConnection({{instrument->nodeID, 0}, {output->nodeID, 0}});
    graph.addConnection({{instrument->nodeID, 1}, {output->nodeID, 1}});
    mc.getGraphEditor().updateComponents();

    auto* trackInComp = compForCFT(mc.getGraphEditor(), trackIn->nodeID);
    auto* instrumentComp = compForCFT(mc.getGraphEditor(), instrument->nodeID);
    ASSERT_NE(trackInComp, nullptr);
    ASSERT_NE(instrumentComp, nullptr);

    const juce::String graphBefore = juce::JSON::toString(synth::AIStateMapper::graphToJSON(graph));
    const juce::String macrosBefore = juce::JSON::toString(mc.getGraphEditor().getMacros().toVar());

    dragRealMidiCableBetweenCFT(*trackInComp, *instrumentComp);

    EXPECT_TRUE(graph.isConnected({{trackIn->nodeID, juce::AudioProcessorGraph::midiChannelIndex},
                                   {instrument->nodeID, juce::AudioProcessorGraph::midiChannelIndex}}));
    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::ChannelStrip), 1);
    EXPECT_FALSE(graph.isConnected({{instrument->nodeID, 0}, {output->nodeID, 0}}))
        << "the instrument's straight-to-output feed must have been re-routed through the new channel";

    const juce::String graphAfter = juce::JSON::toString(synth::AIStateMapper::graphToJSON(graph));
    const juce::String macrosAfter = juce::JSON::toString(mc.getGraphEditor().getMacros().toVar());

    ASSERT_TRUE(mc.getUndoManager().canUndo());
    ASSERT_TRUE(mc.getUndoManager().undo());
    EXPECT_EQ(juce::JSON::toString(synth::AIStateMapper::graphToJSON(graph)), graphBefore)
        << "one undo must remove BOTH the connection and the channel";
    EXPECT_EQ(juce::JSON::toString(mc.getGraphEditor().getMacros().toVar()), macrosBefore);

    ASSERT_TRUE(mc.getUndoManager().redo());
    EXPECT_EQ(juce::JSON::toString(synth::AIStateMapper::graphToJSON(graph)), graphAfter);
    EXPECT_EQ(juce::JSON::toString(mc.getGraphEditor().getMacros().toVar()), macrosAfter);
}

TEST_F(ChannelFlowTest, AutoChannelOnConnect_ToggleOffOnlyConnectsNoChannel) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    mc.newPatchForTest();
    mc.getGraphEditor().setAutoCreateChannelOnConnectEnabled(false);

    mc.getTimelinePanel().applyAddTrackMenuChoice(synth::ui::TimelinePanelComponent::kAddMidiTrackMenuId);
    auto& graph = mc.getAudioEngine().getGraph();
    auto* trackIn = findNodeOfTypeCFT(graph, ModuleType::TimelineMidiSource);
    ASSERT_NE(trackIn, nullptr);

    juce::String instrumentUuid;
    auto* instrument = addPlainNodeCFT(graph, "Oscillator", {600, 600}, instrumentUuid);
    ASSERT_NE(instrument, nullptr);
    auto* output = findNodeNamedCFT(graph, "Audio Output");
    ASSERT_NE(output, nullptr);
    graph.addConnection({{instrument->nodeID, 0}, {output->nodeID, 0}});
    graph.addConnection({{instrument->nodeID, 1}, {output->nodeID, 1}});
    mc.getGraphEditor().updateComponents();

    auto* trackInComp = compForCFT(mc.getGraphEditor(), trackIn->nodeID);
    auto* instrumentComp = compForCFT(mc.getGraphEditor(), instrument->nodeID);
    ASSERT_NE(trackInComp, nullptr);
    ASSERT_NE(instrumentComp, nullptr);

    const int nodesBefore = graph.getNodes().size();

    dragRealMidiCableBetweenCFT(*trackInComp, *instrumentComp);

    EXPECT_TRUE(graph.isConnected({{trackIn->nodeID, juce::AudioProcessorGraph::midiChannelIndex},
                                   {instrument->nodeID, juce::AudioProcessorGraph::midiChannelIndex}}));
    EXPECT_EQ(graph.getNodes().size(), nodesBefore) << "no new nodes — the toggle is OFF";
    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::ChannelStrip), 0);
    EXPECT_TRUE(graph.isConnected({{instrument->nodeID, 0}, {output->nodeID, 0}}))
        << "the pre-existing straight-to-output feed must be untouched";
    EXPECT_TRUE(graph.isConnected({{instrument->nodeID, 1}, {output->nodeID, 1}}));
}

TEST_F(ChannelFlowTest, AutoChannelOnConnect_AlreadyChanneledInstrumentGetsNoNewStrip) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    mc.newPatchForTest();

    // "+ Track -> Instrument" (T183) builds Track In -> Oscillator -> EQ -> Compressor -> Strip ->
    // Master in one step; the instrument this test's SECOND Track In targets already has a channel.
    addInstrumentTrack(mc, "Oscillator");
    auto& graph = mc.getAudioEngine().getGraph();
    ASSERT_EQ(countNodesOfTypeCFT(graph, ModuleType::ChannelStrip), 1);
    auto* instrument = findNodeOfTypeCFT(graph, ModuleType::Oscillator);
    ASSERT_NE(instrument, nullptr);

    auto& macros = mc.getGraphEditor().getMacros();
    ASSERT_EQ(macros.size(), 1);
    const auto macroId = macros.getAll().front().id;
    // Expand: the instrument's own ModuleComponent must be a real, visible drop target for the
    // direct-jack drag below (a collapsed macro's hidden members are not "on the canvas" —
    // endConnectionDrag's own comment).
    mc.getGraphEditor().setMacroCollapsed(macroId, false);

    // A second, independent MIDI track — added directly (not through createTrackInNode, whose own
    // "exactly one instrument" auto-wire would otherwise wire it for us and never exercise this
    // gesture at all) — exactly what a user would drag onto the already-channeled instrument by
    // hand.
    juce::String trackIn2Uuid;
    auto* trackIn2 = addPlainNodeCFT(graph, "Track In", {50, 900}, trackIn2Uuid);
    ASSERT_NE(trackIn2, nullptr);
    // trackIn2 lives outside the instrument's macro, so a live macro-boundary crossing would
    // otherwise auto-mint a macro port here (T148) -- a real, separately-tested behaviour this
    // test isn't about. Disabled so the drag exercises T184's own direct-jack path in isolation.
    mc.getGraphEditor().setAutoCreateMacroPortsOnDragEnabled(false);
    mc.getGraphEditor().updateComponents();

    auto* trackIn2Comp = compForCFT(mc.getGraphEditor(), trackIn2->nodeID);
    auto* instrumentComp = compForCFT(mc.getGraphEditor(), instrument->nodeID);
    ASSERT_NE(trackIn2Comp, nullptr);
    ASSERT_NE(instrumentComp, nullptr);
    ASSERT_TRUE(instrumentComp->isVisible());

    dragRealMidiCableBetweenCFT(*trackIn2Comp, *instrumentComp);

    EXPECT_TRUE(graph.isConnected({{trackIn2->nodeID, juce::AudioProcessorGraph::midiChannelIndex},
                                   {instrument->nodeID, juce::AudioProcessorGraph::midiChannelIndex}}));
    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::ChannelStrip), 1)
        << "the instrument already has a channel; nothing new should be created";
}

// Both T148 (macro-boundary auto-porting) and T184 (auto-channel) are ON here — the instrument
// lives inside a macro and the Track In driving it lives outside, so the drag crosses a macro
// boundary AND lands on an unchanneled instrument. This is the highest-risk combined path: a
// stray nested undo transaction in either feature would split "port + connection + channel" into
// more than one Cmd+Z step and every OTHER test in this file (which each disable one feature or
// the other, or avoid crossing a macro boundary) would still pass.
TEST_F(ChannelFlowTest, AutoChannelOnConnect_NewChainNodesJoinTheInstrumentsExistingMacro) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    mc.newPatchForTest();
    ASSERT_TRUE(mc.getGraphEditor().getAutoCreateMacroPortsOnDragEnabled()) << "T148 stays ON for this test";
    ASSERT_TRUE(mc.getGraphEditor().getAutoCreateChannelOnConnectEnabled()) << "T184 stays ON for this test";

    mc.getTimelinePanel().applyAddTrackMenuChoice(synth::ui::TimelinePanelComponent::kAddMidiTrackMenuId);
    auto& graph = mc.getAudioEngine().getGraph();
    auto* trackIn = findNodeOfTypeCFT(graph, ModuleType::TimelineMidiSource);
    ASSERT_NE(trackIn, nullptr);

    juce::String instrumentUuid;
    auto* instrument = addPlainNodeCFT(graph, "Oscillator", {600, 600}, instrumentUuid);
    ASSERT_NE(instrument, nullptr);
    auto* output = findNodeNamedCFT(graph, "Audio Output");
    ASSERT_NE(output, nullptr);
    graph.addConnection({{instrument->nodeID, 0}, {output->nodeID, 0}});
    graph.addConnection({{instrument->nodeID, 1}, {output->nodeID, 1}});

    // Box the instrument ALONE, as an ORDINARY member, in its own macro (no ports) — a module
    // that's already someone's macro member, unrelated to a channel, is a realistic starting state.
    // Track In stays OUTSIDE this macro, which is exactly what makes the coming drag a
    // boundary-crossing one.
    const auto macroId = mc.getGraphEditor().addMacroForMembers({instrumentUuid}, "TestMacro", {600, 600});
    ASSERT_FALSE(macroId.isEmpty());
    mc.getGraphEditor().setMacroCollapsed(macroId, false); // expand: instrument becomes visible again

    auto* trackInComp = compForCFT(mc.getGraphEditor(), trackIn->nodeID);
    auto* instrumentComp = compForCFT(mc.getGraphEditor(), instrument->nodeID);
    ASSERT_NE(trackInComp, nullptr);
    ASSERT_NE(instrumentComp, nullptr);
    ASSERT_TRUE(instrumentComp->isVisible());

    const juce::String graphBefore = juce::JSON::toString(synth::AIStateMapper::graphToJSON(graph));
    const juce::String macrosBefore = juce::JSON::toString(mc.getGraphEditor().getMacros().toVar());

    dragRealMidiCableBetweenCFT(*trackInComp, *instrumentComp);

    // (a) T148: the boundary crossing minted a macro MIDI inlet port, and Track In wires to it
    // rather than straight to the instrument.
    auto* port = findNodeOfTypeCFT(graph, ModuleType::MacroMidiInlet);
    ASSERT_NE(port, nullptr) << "the macro boundary crossing must have minted a port";
    EXPECT_TRUE(graph.isConnected({{trackIn->nodeID, juce::AudioProcessorGraph::midiChannelIndex},
                                   {port->nodeID, juce::AudioProcessorGraph::midiChannelIndex}}));
    EXPECT_TRUE(graph.isConnected({{port->nodeID, juce::AudioProcessorGraph::midiChannelIndex},
                                   {instrument->nodeID, juce::AudioProcessorGraph::midiChannelIndex}}));
    EXPECT_FALSE(graph.isConnected({{trackIn->nodeID, juce::AudioProcessorGraph::midiChannelIndex},
                                    {instrument->nodeID, juce::AudioProcessorGraph::midiChannelIndex}}))
        << "the drag crossed a macro boundary, so it must route through the port, not directly";

    // (b) T184: the strip was built.
    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::ChannelStrip), 1);

    auto* macro = mc.getGraphEditor().getMacros().find(macroId);
    ASSERT_NE(macro, nullptr);
    EXPECT_TRUE(macro->hasMember(instrumentUuid));
    EXPECT_TRUE(macro->hasMember(nodeUuid(findNodeOfTypeCFT(graph, ModuleType::ParametricEQ))));
    EXPECT_TRUE(macro->hasMember(nodeUuid(findNodeOfTypeCFT(graph, ModuleType::Compressor))));
    auto* strip = findNodeOfTypeCFT(graph, ModuleType::ChannelStrip);
    ASSERT_NE(strip, nullptr);
    EXPECT_TRUE(macro->hasMember(nodeUuid(strip)));

    // docs/mixer.md §8 item 2: Master stays OUTSIDE the macro, and Strip -> Master is a PLAIN
    // graph edge, never a macro port — spliceMasterNode/ensureMasterNode classify Mix vs Direct by
    // checking whether the connection's SOURCE NODE is itself a ChannelStripModule, which a
    // MacroOutlet sitting in between would defeat.
    auto* master = synth::findMasterNode(graph);
    ASSERT_NE(master, nullptr);
    EXPECT_FALSE(macro->hasMember(nodeUuid(master))) << "Master must never join the instrument's macro";
    EXPECT_TRUE(graph.isConnected({{strip->nodeID, 0}, {master->nodeID, MasterModule::kMixLeft}}))
        << "Strip -> Master must be a plain edge landing on Mix, not routed through a macro outlet";
    EXPECT_TRUE(graph.isConnected(
        {{strip->nodeID, ChannelStripModule::kRightBase}, {master->nodeID, MasterModule::kMixRight}}));

    // (c) ONE Cmd+Z reverts the port, the connection AND the channel together.
    const juce::String graphAfter = juce::JSON::toString(synth::AIStateMapper::graphToJSON(graph));
    const juce::String macrosAfter = juce::JSON::toString(mc.getGraphEditor().getMacros().toVar());

    ASSERT_TRUE(mc.getUndoManager().canUndo());
    ASSERT_TRUE(mc.getUndoManager().undo());
    EXPECT_EQ(juce::JSON::toString(synth::AIStateMapper::graphToJSON(graph)), graphBefore)
        << "one undo must remove the port, the connection AND the channel together";
    EXPECT_EQ(juce::JSON::toString(mc.getGraphEditor().getMacros().toVar()), macrosBefore);

    ASSERT_TRUE(mc.getUndoManager().redo());
    EXPECT_EQ(juce::JSON::toString(synth::AIStateMapper::graphToJSON(graph)), graphAfter);
    EXPECT_EQ(juce::JSON::toString(mc.getGraphEditor().getMacros().toVar()), macrosAfter);
}

// -------------------------------------------------------------------------------------------
// "Create Channels" for existing projects (FRO26, P9-3e, docs/mixer.md §5.13)
//
// docs/mixer.md §5.13: an old project opens UNCHANGED -- no automatic migration on load. The
// "+ Track" menu's "Create Channels" entry (TimelinePanelComponent::kCreateChannelsMenuId, driven
// here through the same applyAddTrackMenuChoice() headless seam addAudioTrack()/addInstrumentTrack()
// use above) wraps every channel-less track's chain into a strip, as ONE undo step covering all of
// them. These tests build the "old project" starting state directly -- a "Track Audio" or
// "Track In" -> instrument chain wired straight to the output, exactly what addAudioTrack()
// produced before T173a and what a real .agsproj predating this feature still loads as, since
// nothing here ever migrates it automatically -- the same "build the pre-existing state directly"
// pattern the AutoChannelOnConnect_* tests above use for T184's own "no channel yet" starting point.
// -------------------------------------------------------------------------------------------

TEST_F(ChannelFlowTest, CreateChannelsWrapsEveryChannellessTrackAsOneUndoStep) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    mc.newPatchForTest();
    auto& graph = mc.getAudioEngine().getGraph();
    auto& doc = mc.getTimelineDoc();
    auto* output = findNodeNamedCFT(graph, "Audio Output");
    ASSERT_NE(output, nullptr);

    // Legacy audio track: "Track Audio" wired straight to the output, no insert chain.
    juce::String trackAudioUuid;
    auto* trackAudioNode = addPlainNodeCFT(graph, "Track Audio", {50, 50}, trackAudioUuid);
    ASSERT_NE(trackAudioNode, nullptr);
    graph.addConnection({{trackAudioNode->nodeID, 0}, {output->nodeID, 0}});
    graph.addConnection({{trackAudioNode->nodeID, 1}, {output->nodeID, 1}});
    const auto audioTrackId = doc.addTrack(synth::TrackKind::Audio, "Legacy Audio");
    ASSERT_TRUE(audioTrackId.isValid());
    ASSERT_TRUE(doc.setTrackBinding(audioTrackId, trackAudioUuid));

    // Legacy instrument track: Track In -> Oscillator wired straight to the output -- the same
    // starting shape the AutoChannelOnConnect_* tests above build for T184's own case.
    mc.getTimelinePanel().applyAddTrackMenuChoice(synth::ui::TimelinePanelComponent::kAddMidiTrackMenuId);
    auto* trackInNode = findNodeOfTypeCFT(graph, ModuleType::TimelineMidiSource);
    ASSERT_NE(trackInNode, nullptr);
    juce::String instrumentUuid;
    auto* instrumentNode = addPlainNodeCFT(graph, "Oscillator", {300, 300}, instrumentUuid);
    ASSERT_NE(instrumentNode, nullptr);
    graph.addConnection({{trackInNode->nodeID, juce::AudioProcessorGraph::midiChannelIndex},
                         {instrumentNode->nodeID, juce::AudioProcessorGraph::midiChannelIndex}});
    graph.addConnection({{instrumentNode->nodeID, 0}, {output->nodeID, 0}});
    graph.addConnection({{instrumentNode->nodeID, 1}, {output->nodeID, 1}});
    mc.getGraphEditor().updateComponents();

    ASSERT_EQ(countNodesOfTypeCFT(graph, ModuleType::ChannelStrip), 0)
        << "opens unchanged -- no channel exists yet, on either track";

    const juce::String graphBefore = juce::JSON::toString(synth::AIStateMapper::graphToJSON(graph));
    const juce::String docBefore = juce::JSON::toString(doc.toVar());
    const juce::String macrosBefore = juce::JSON::toString(mc.getGraphEditor().getMacros().toVar());

    mc.getTimelinePanel().applyAddTrackMenuChoice(synth::ui::TimelinePanelComponent::kCreateChannelsMenuId);

    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::ChannelStrip), 2)
        << "both channel-less tracks must have gotten their own strip";
    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::Master), 1) << "Master is a singleton shared by both";
    EXPECT_FALSE(graph.isConnected({{trackAudioNode->nodeID, 0}, {output->nodeID, 0}}))
        << "the audio track's straight-to-output feed must be re-routed through its new channel";
    EXPECT_FALSE(graph.isConnected({{trackAudioNode->nodeID, 1}, {output->nodeID, 1}}));
    EXPECT_FALSE(graph.isConnected({{instrumentNode->nodeID, 0}, {output->nodeID, 0}}))
        << "the instrument track's straight-to-output feed must be re-routed through its new channel";
    EXPECT_FALSE(graph.isConnected({{instrumentNode->nodeID, 1}, {output->nodeID, 1}}));

    const juce::String graphAfter = juce::JSON::toString(synth::AIStateMapper::graphToJSON(graph));
    const juce::String docAfter = juce::JSON::toString(doc.toVar());
    const juce::String macrosAfter = juce::JSON::toString(mc.getGraphEditor().getMacros().toVar());

    ASSERT_TRUE(mc.getUndoManager().canUndo());
    ASSERT_TRUE(mc.getUndoManager().undo());
    EXPECT_EQ(juce::JSON::toString(synth::AIStateMapper::graphToJSON(graph)), graphBefore)
        << "ONE undo must remove BOTH new channels together";
    EXPECT_EQ(juce::JSON::toString(doc.toVar()), docBefore);
    EXPECT_EQ(juce::JSON::toString(mc.getGraphEditor().getMacros().toVar()), macrosBefore);

    ASSERT_TRUE(mc.getUndoManager().redo());
    EXPECT_EQ(juce::JSON::toString(synth::AIStateMapper::graphToJSON(graph)), graphAfter);
    EXPECT_EQ(juce::JSON::toString(doc.toVar()), docAfter);
    EXPECT_EQ(juce::JSON::toString(mc.getGraphEditor().getMacros().toVar()), macrosAfter);
}

TEST_F(ChannelFlowTest, CreateChannelsLeavesAlreadyChanneledTracksUntouched) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    mc.newPatchForTest();
    auto& graph = mc.getAudioEngine().getGraph();
    auto& doc = mc.getTimelineDoc();

    // An already-channeled track, built the normal way.
    addAudioTrack(mc);
    ASSERT_EQ(countNodesOfTypeCFT(graph, ModuleType::ChannelStrip), 1);
    auto* existingStrip = findNodeOfTypeCFT(graph, ModuleType::ChannelStrip);
    ASSERT_NE(existingStrip, nullptr);
    const juce::String existingStripUuid = nodeUuid(existingStrip);
    auto* master = synth::findMasterNode(graph);
    ASSERT_NE(master, nullptr);

    // A second, channel-less legacy audio track.
    auto* output = findNodeNamedCFT(graph, "Audio Output");
    ASSERT_NE(output, nullptr);
    juce::String legacyUuid;
    auto* legacyNode = addPlainNodeCFT(graph, "Track Audio", {800, 50}, legacyUuid);
    ASSERT_NE(legacyNode, nullptr);
    graph.addConnection({{legacyNode->nodeID, 0}, {output->nodeID, 0}});
    graph.addConnection({{legacyNode->nodeID, 1}, {output->nodeID, 1}});
    const auto legacyTrackId = doc.addTrack(synth::TrackKind::Audio, "Legacy Audio");
    ASSERT_TRUE(legacyTrackId.isValid());
    ASSERT_TRUE(doc.setTrackBinding(legacyTrackId, legacyUuid));
    mc.getGraphEditor().updateComponents();

    mc.getTimelinePanel().applyAddTrackMenuChoice(synth::ui::TimelinePanelComponent::kCreateChannelsMenuId);

    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::ChannelStrip), 2)
        << "one pre-existing channel plus one newly-created one for the legacy track";

    // The pre-existing channel's own strip is untouched: the same node, same uuid, still wired to
    // Master exactly as before.
    EXPECT_EQ(graph.getNodeForId(existingStrip->nodeID), existingStrip);
    EXPECT_EQ(nodeUuid(existingStrip), existingStripUuid);
    EXPECT_TRUE(graph.isConnected({{existingStrip->nodeID, 0}, {master->nodeID, MasterModule::kMixLeft}}));
    EXPECT_TRUE(graph.isConnected(
        {{existingStrip->nodeID, ChannelStripModule::kRightBase}, {master->nodeID, MasterModule::kMixRight}}));

    // The legacy track's own feed must have been re-routed through its NEW strip.
    EXPECT_FALSE(graph.isConnected({{legacyNode->nodeID, 0}, {output->nodeID, 0}}));
    EXPECT_FALSE(graph.isConnected({{legacyNode->nodeID, 1}, {output->nodeID, 1}}));
}

TEST_F(ChannelFlowTest, CreateChannelsIsANoOpWhenNothingNeedsAChannel) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    // Deliberately NOT newPatchForTest() here: GraphEditor::newPatch() itself pushes TWO undo
    // steps of its own (its own comment: graph clear + timeline clear, kept separate on purpose),
    // which would make "one undo empties the stack" a false negative below for a reason that has
    // nothing to do with Create Channels. The default factory preset starts with zero tracks
    // (same starting point every non-newPatch test above relies on), so it's a clean baseline.
    auto& graph = mc.getAudioEngine().getGraph();

    addAudioTrack(mc); // already fully channeled -- nothing for "Create Channels" to do
    ASSERT_EQ(countNodesOfTypeCFT(graph, ModuleType::ChannelStrip), 1);
    ASSERT_TRUE(mc.getUndoManager().canUndo()) << "addAudioTrack itself pushed one undo step";

    const int nodesBefore = (int)graph.getNodes().size();
    const juce::String graphBefore = juce::JSON::toString(synth::AIStateMapper::graphToJSON(graph));

    mc.getTimelinePanel().applyAddTrackMenuChoice(synth::ui::TimelinePanelComponent::kCreateChannelsMenuId);

    EXPECT_EQ((int)graph.getNodes().size(), nodesBefore) << "no new nodes -- nothing needed a channel";
    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::ChannelStrip), 1);
    EXPECT_EQ(juce::JSON::toString(synth::AIStateMapper::graphToJSON(graph)), graphBefore)
        << "byte-for-byte unchanged -- a true no-op";

    // No new undo step was pushed: the ONE undo available must be addAudioTrack's own, removing
    // the whole channel it built, not a no-op Create Channels step sitting on top of it.
    ASSERT_TRUE(mc.getUndoManager().undo());
    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::ChannelStrip), 0)
        << "undoing once removed the whole audio track, proving Create Channels pushed nothing of its own";
    EXPECT_FALSE(mc.getUndoManager().canUndo());
}

// D1 (docs/mixer.md §7): "channels follow audio," not tracks, so two tracks that share one
// unchanneled instrument must come out of the sweep with exactly ONE channel between them, not
// two. This falls out for free from reusing T184's own per-node builder: the first track's call
// builds the channel and removes the shared instrument's exit edges, so the second track's call
// sees findUnchanneledOutputFeeds already empty and does nothing.
TEST_F(ChannelFlowTest, CreateChannelsGivesTwoTracksSharingOneInstrumentJustOneChannel) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    mc.newPatchForTest();
    auto& graph = mc.getAudioEngine().getGraph();
    auto* output = findNodeNamedCFT(graph, "Audio Output");
    ASSERT_NE(output, nullptr);

    // Two legacy MIDI tracks (bare Track In, auto-created+bound by the menu action) both feeding
    // one shared, channel-less Oscillator wired straight to the output.
    mc.getTimelinePanel().applyAddTrackMenuChoice(synth::ui::TimelinePanelComponent::kAddMidiTrackMenuId);
    mc.getTimelinePanel().applyAddTrackMenuChoice(synth::ui::TimelinePanelComponent::kAddMidiTrackMenuId);
    std::vector<juce::AudioProcessorGraph::Node*> trackIns;
    for (auto* node : graph.getNodes())
        if (node != nullptr)
            if (auto* module = dynamic_cast<ModuleBase*>(node->getProcessor()))
                if (module->getModuleType() == ModuleType::TimelineMidiSource)
                    trackIns.push_back(node);
    ASSERT_EQ(trackIns.size(), 2u);

    juce::String instrumentUuid;
    auto* instrumentNode = addPlainNodeCFT(graph, "Oscillator", {300, 300}, instrumentUuid);
    ASSERT_NE(instrumentNode, nullptr);
    for (auto* trackIn : trackIns)
        graph.addConnection({{trackIn->nodeID, juce::AudioProcessorGraph::midiChannelIndex},
                             {instrumentNode->nodeID, juce::AudioProcessorGraph::midiChannelIndex}});
    graph.addConnection({{instrumentNode->nodeID, 0}, {output->nodeID, 0}});
    graph.addConnection({{instrumentNode->nodeID, 1}, {output->nodeID, 1}});
    mc.getGraphEditor().updateComponents();

    ASSERT_EQ(countNodesOfTypeCFT(graph, ModuleType::ChannelStrip), 0) << "opens unchanged";

    mc.getTimelinePanel().applyAddTrackMenuChoice(synth::ui::TimelinePanelComponent::kCreateChannelsMenuId);

    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::ChannelStrip), 1)
        << "one shared instrument gets one channel, not one per track (D1: channels follow audio)";
    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::Master), 1);
    EXPECT_FALSE(graph.isConnected({{instrumentNode->nodeID, 0}, {output->nodeID, 0}}))
        << "the shared instrument's feed must be re-routed through its one new channel";
    EXPECT_FALSE(graph.isConnected({{instrumentNode->nodeID, 1}, {output->nodeID, 1}}));

    ASSERT_TRUE(mc.getUndoManager().canUndo());
    ASSERT_TRUE(mc.getUndoManager().undo());
    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::ChannelStrip), 0)
        << "the one undo step removes the whole (single) channel";
}

// The spec's other required no-op clause: the "+ Track" menu's "Create Channels" entry itself must
// be DISABLED (not just a silent no-op) whenever nothing needs a channel, and enabled the moment
// something does. openAddTrackMenu() reads this straight off
// TrackHeaderHost::hasTracksNeedingChannels() (TimelinePanelComponent.cpp), so exercising that same
// public seam here proves the real menu's enabled state without needing to open the async
// juce::PopupMenu itself.
TEST_F(ChannelFlowTest, HasTracksNeedingChannelsBacksTheMenusEnabledState) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    mc.newPatchForTest();
    auto& graph = mc.getAudioEngine().getGraph();

    EXPECT_FALSE(mc.hasTracksNeedingChannelsForTest()) << "a brand-new patch has no tracks at all";

    addAudioTrack(mc); // fully channeled via the normal flow
    EXPECT_FALSE(mc.hasTracksNeedingChannelsForTest())
        << "the menu entry must stay disabled -- this track already has a channel";

    // A legacy audio track, wired straight to the output with no insert chain -- the same
    // pre-P9-3 shape the tests above build.
    auto* output = findNodeNamedCFT(graph, "Audio Output");
    ASSERT_NE(output, nullptr);
    juce::String legacyUuid;
    auto* legacyNode = addPlainNodeCFT(graph, "Track Audio", {800, 50}, legacyUuid);
    ASSERT_NE(legacyNode, nullptr);
    graph.addConnection({{legacyNode->nodeID, 0}, {output->nodeID, 0}});
    graph.addConnection({{legacyNode->nodeID, 1}, {output->nodeID, 1}});
    auto& doc = mc.getTimelineDoc();
    const auto legacyTrackId = doc.addTrack(synth::TrackKind::Audio, "Legacy Audio");
    ASSERT_TRUE(legacyTrackId.isValid());
    ASSERT_TRUE(doc.setTrackBinding(legacyTrackId, legacyUuid));
    mc.getGraphEditor().updateComponents();

    EXPECT_TRUE(mc.hasTracksNeedingChannelsForTest())
        << "the legacy track has no channel yet -- the menu entry must now be enabled";

    mc.getTimelinePanel().applyAddTrackMenuChoice(synth::ui::TimelinePanelComponent::kCreateChannelsMenuId);
    EXPECT_FALSE(mc.hasTracksNeedingChannelsForTest()) << "both tracks are channeled now -- disabled again";
}

// =================================================================================================
// FRO25 (P9-3d, docs/mixer.md §5.8): "Make channel" on a track (header menu) or a selected chain
// (canvas / module menu). The track's exclusive chain moves into a channel macro with the default
// EQ -> Compressor -> Channel Strip -> Master chain; a module another track also uses stays outside
// (a shared LFO reaches in through an auto-created port); a merge point becomes its own bus channel;
// "Duplicate into Channel" gives this channel an independent copy of a shared module; a poly chain
// gets a Voice Mixer; an already-channeled target is a no-op; every action is ONE undo step.
//
// The render-identity tests build the same legacy patch in two Hosted engines, convert one through a
// standalone GraphEditor, and compare the offline renders sample for sample.
// =================================================================================================

namespace {

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

int cutoffChannelCFT(juce::AudioProcessorGraph::Node* filter) {
    if (auto* module = dynamic_cast<ModuleBase*>(filter->getProcessor()))
        for (const auto& target : module->getModulationTargets())
            return target.channelIndex;
    return -1;
}

LegacyRigCFT buildLegacyRigCFT(AudioEngine& engine, juce::AudioProcessorGraph::Node* output, RigShapeCFT shape) {
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
void expectIdenticalRendersCFT(const std::vector<float>& reference, const std::vector<float>& converted) {
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

juce::AudioProcessorGraph::Node* nodeForUuidCFT(juce::AudioProcessorGraph& graph, const juce::String& uuid) {
    for (auto* node : graph.getNodes())
        if (node != nullptr && node->properties["uuid"].toString() == uuid)
            return node;
    return nullptr;
}

bool isModuleOfTypeCFT(juce::AudioProcessorGraph::Node* node, ModuleType type) {
    auto* module = node != nullptr ? dynamic_cast<ModuleBase*>(node->getProcessor()) : nullptr;
    return module != nullptr && module->getModuleType() == type;
}

// True when `source` modulates `dest`'s `channel` through a hidden attenuverter — straight, or with
// a macro inlet port between the attenuverter and `dest`.
bool modulatesCFT(juce::AudioProcessorGraph& graph, juce::AudioProcessorGraph::Node* source,
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
McRigCFT buildMcRigCFT(MainComponent& mc, RigShapeCFT shape) {
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

synth::ui::TimelineTrackHeaderComponent* headerForCFT(MainComponent& mc, synth::TrackId track) {
    auto& panel = mc.getTimelinePanel();
    for (int i = 0; i < panel.getTrackHeaderCount(); ++i)
        if (auto* header = panel.getTrackHeaderAt(i); header != nullptr && header->getTrackId() == track)
            return header;
    return nullptr;
}

// A real right-click on the track header — mouseDown() builds the context menu and hands it to the
// test hook instead of showing it.
juce::PopupMenu rightClickHeaderMenuCFT(synth::ui::TimelineTrackHeaderComponent& header) {
    juce::PopupMenu captured;
    header.setShowContextMenuHookForTest([&captured](juce::PopupMenu& menu) { captured = menu; });
    const juce::Point<int> point(4, 4);
    header.mouseDown(
        realMouseEventCFT(header, point, point, juce::ModifierKeys(juce::ModifierKeys::rightButtonModifier)));
    header.setShowContextMenuHookForTest(nullptr);
    return captured;
}

juce::PopupMenu rightClickModuleMenuCFT(ModuleComponent& comp) {
    juce::PopupMenu captured;
    comp.setShowContextMenuHookForTest([&captured](juce::PopupMenu& menu) { captured = menu; });
    const juce::Point<int> body(comp.getWidth() / 2, comp.getHeight() - 10);
    comp.mouseDown(realMouseEventCFT(comp, body, body, juce::ModifierKeys(juce::ModifierKeys::rightButtonModifier)));
    return captured;
}

// Chooses "Make Channel" from `track`'s header menu (asserting it is offered and enabled).
void makeChannelFromHeaderCFT(MainComponent& mc, synth::TrackId track) {
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

SnapshotCFT snapshotCFT(MainComponent& mc) {
    return {juce::JSON::toString(synth::AIStateMapper::graphToJSON(mc.getAudioEngine().getGraph())),
            juce::JSON::toString(mc.getTimelineDoc().toVar()),
            juce::JSON::toString(mc.getGraphEditor().getMacros().toVar())};
}

void expectSameSnapshotCFT(const SnapshotCFT& actual, const SnapshotCFT& expected) {
    EXPECT_EQ(actual.graph, expected.graph);
    EXPECT_EQ(actual.doc, expected.doc);
    EXPECT_EQ(actual.macros, expected.macros);
}

// One undo fully reverts the action to `before`; redo restores `after`.
void expectOneUndoStepCFT(MainComponent& mc, const SnapshotCFT& before) {
    const auto after = snapshotCFT(mc);
    EXPECT_NE(after.graph, before.graph) << "the action must have changed the graph";
    ASSERT_TRUE(mc.getUndoManager().undo());
    expectSameSnapshotCFT(snapshotCFT(mc), before);
    ASSERT_TRUE(mc.getUndoManager().redo());
    expectSameSnapshotCFT(snapshotCFT(mc), after);
}

} // namespace

// -------------------------------------------------------------------------------------------
// Core behaviour, through a standalone GraphEditor (it owns the macros and the port splicing).
// -------------------------------------------------------------------------------------------

TEST(ChannelFlowMakeChannelCore, ExclusiveChainAndItsOwnLfoMoveIntoTheChannelMacro) {
    HostedPatchCFT patch;
    GraphEditor editor(patch.engine);
    auto& graph = patch.engine.getGraph();
    const auto rig = buildLegacyRigCFT(patch.engine, patch.output, RigShapeCFT::SharedLfo);
    ASSERT_GE(rig.cutoffChannel, 0);

    ASSERT_TRUE(editor.nodeNeedsChannel(rig.trackInA->nodeID));
    ASSERT_TRUE(editor.makeChannelFromNode(rig.trackInA->nodeID, "Lead"));

    const auto* macro = editor.getMacros().findByMember(nodeUuid(rig.trackInA));
    ASSERT_NE(macro, nullptr);
    EXPECT_EQ(macro->name, "Lead");
    EXPECT_TRUE(macro->collapsed);
    for (auto* member : {rig.trackInA, rig.oscA, rig.filterA})
        EXPECT_TRUE(macro->hasMember(nodeUuid(member))) << "modules used only by this track move in";
    EXPECT_TRUE(macro->hasMember(nodeUuid(rig.ownLfo))) << "an LFO modulating only this chain moves in too";
    for (auto* outside : {rig.sharedLfo, rig.trackInB, rig.oscB, rig.filterB})
        EXPECT_FALSE(macro->hasMember(nodeUuid(outside))) << "another track's modules never move";

    auto* eq = findMacroMemberOfTypeCFT(graph, *macro, ModuleType::ParametricEQ);
    auto* compressor = findMacroMemberOfTypeCFT(graph, *macro, ModuleType::Compressor);
    auto* strip = findMacroMemberOfTypeCFT(graph, *macro, ModuleType::ChannelStrip);
    ASSERT_NE(eq, nullptr);
    ASSERT_NE(compressor, nullptr);
    ASSERT_NE(strip, nullptr);
    EXPECT_TRUE(dynamic_cast<ModuleBase*>(eq->getProcessor())->isBypassed());
    EXPECT_TRUE(dynamic_cast<ModuleBase*>(compressor->getProcessor())->isBypassed());
    EXPECT_TRUE(graph.isConnected({{rig.filterA->nodeID, 0}, {eq->nodeID, 0}}));
    EXPECT_TRUE(graph.isConnected(
        {{rig.filterA->nodeID, dynamic_cast<ModuleBase*>(rig.filterA->getProcessor())->rightAudioLegChannel()},
         {eq->nodeID, 1}}))
        << "the right leg is read off rightAudioLegChannel(), never assumed to be ch1";

    auto* master = findNodeOfTypeCFT(graph, ModuleType::Master);
    ASSERT_NE(master, nullptr);
    EXPECT_FALSE(macro->hasMember(nodeUuid(master))) << "Master stays outside the macro";
    EXPECT_TRUE(graph.isConnected({{strip->nodeID, 0}, {master->nodeID, MasterModule::kMixLeft}}))
        << "Strip -> Master stays a PLAIN edge, never a macro port";
    EXPECT_TRUE(graph.isConnected(
        {{strip->nodeID, ChannelStripModule::kRightBase}, {master->nodeID, MasterModule::kMixRight}}));
    EXPECT_FALSE(graph.isConnected({{rig.filterA->nodeID, 0}, {rig.output->nodeID, 0}}));
    EXPECT_TRUE(graph.isConnected({{rig.filterB->nodeID, 0}, {master->nodeID, MasterModule::kDirectLeft}}))
        << "track B keeps its own direct path, now through Master's Direct input";

    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::ChannelStrip), 1);
    EXPECT_FALSE(editor.nodeNeedsChannel(rig.trackInA->nodeID));
    EXPECT_TRUE(editor.nodeNeedsChannel(rig.trackInB->nodeID)) << "track B is untouched and still channel-less";
}

TEST(ChannelFlowMakeChannelCore, SharedLfoStaysOutsideThroughAnAutoPortAndTheRenderIsIdentical) {
    HostedPatchCFT reference, converted;
    buildLegacyRigCFT(reference.engine, reference.output, RigShapeCFT::SharedLfo);
    const auto rig = buildLegacyRigCFT(converted.engine, converted.output, RigShapeCFT::SharedLfo);
    auto& graph = converted.engine.getGraph();

    GraphEditor editor(converted.engine);
    ASSERT_TRUE(editor.makeChannelFromNode(rig.trackInA->nodeID, "Lead"));
    const auto* macro = editor.getMacros().findByMember(nodeUuid(rig.trackInA));
    ASSERT_NE(macro, nullptr);
    EXPECT_FALSE(macro->hasMember(nodeUuid(rig.sharedLfo)));

    // The shared LFO's leg into Filter A now enters through one of the channel's own inlet ports.
    bool viaOwnInlet = false;
    for (const auto& c : graph.getConnections()) {
        if (c.destination.nodeID != rig.filterA->nodeID || c.destination.channelIndex != rig.cutoffChannel)
            continue;
        auto* source = graph.getNodeForId(c.source.nodeID);
        viaOwnInlet =
            viaOwnInlet || (isModuleOfTypeCFT(source, ModuleType::MacroInlet) && macro->memberIsPort(nodeUuid(source)));
    }
    EXPECT_TRUE(viaOwnInlet) << "a shared module reaches in via an auto-created macro port";
    EXPECT_TRUE(modulatesCFT(graph, rig.sharedLfo, rig.filterA, rig.cutoffChannel));
    EXPECT_TRUE(modulatesCFT(graph, rig.sharedLfo, rig.filterB, rig.cutoffChannel))
        << "the other track keeps the original LFO";

    expectIdenticalRendersCFT(reference.render(16), converted.render(16));
}

TEST(ChannelFlowMakeChannelCore, MergePointBecomesItsOwnBusChannelAndTheRenderIsIdentical) {
    HostedPatchCFT reference, converted;
    buildLegacyRigCFT(reference.engine, reference.output, RigShapeCFT::Merge);
    const auto rig = buildLegacyRigCFT(converted.engine, converted.output, RigShapeCFT::Merge);
    auto& graph = converted.engine.getGraph();

    GraphEditor editor(converted.engine);
    ASSERT_TRUE(editor.makeChannelFromNode(rig.trackInA->nodeID, "Lead"));
    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::ChannelStrip), 2) << "the track's own strip plus the bus strip";

    const auto* lead = editor.getMacros().findByMember(nodeUuid(rig.trackInA));
    const auto* bus = editor.getMacros().findByMember(nodeUuid(rig.filterB));
    ASSERT_NE(lead, nullptr);
    ASSERT_NE(bus, nullptr);
    ASSERT_NE(lead, bus);
    EXPECT_EQ(bus->name, "Filter Bus");
    EXPECT_TRUE(lead->hasMember(nodeUuid(rig.filterA)));
    EXPECT_FALSE(lead->hasMember(nodeUuid(rig.filterB))) << "the shared effect is never assigned to one track";
    EXPECT_FALSE(bus->hasMember(nodeUuid(rig.oscB))) << "track B's own modules stay outside the bus";

    auto* leadStrip = findMacroMemberOfTypeCFT(graph, *lead, ModuleType::ChannelStrip);
    auto* busStrip = findMacroMemberOfTypeCFT(graph, *bus, ModuleType::ChannelStrip);
    auto* master = findNodeOfTypeCFT(graph, ModuleType::Master);
    ASSERT_NE(leadStrip, nullptr);
    ASSERT_NE(busStrip, nullptr);
    ASSERT_NE(master, nullptr);
    EXPECT_TRUE(graph.isConnected({{busStrip->nodeID, 0}, {master->nodeID, MasterModule::kMixLeft}}));
    EXPECT_FALSE(graph.isConnected({{leadStrip->nodeID, 0}, {master->nodeID, MasterModule::kMixLeft}}))
        << "the track reaches Master only through the bus, never twice";
    EXPECT_TRUE(editor.nodeNeedsChannel(rig.trackInB->nodeID)) << "track B can still get its own channel";

    expectIdenticalRendersCFT(reference.render(16), converted.render(16));

    // ...and giving track B its channel afterwards feeds the SAME bus, still rendering identically.
    const auto planB = synth::planMakeChannel(graph, rig.trackInB->nodeID, editor.getMacros());
    ASSERT_TRUE(planB.refusal.isEmpty()) << planB.refusal;
    ASSERT_TRUE(editor.makeChannelFromNode(rig.trackInB->nodeID, "Pad"));
    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::ChannelStrip), 3);
    // Re-fetched: MacroSet::add may reallocate, so `bus` above is not safe to compare against.
    const auto* busAfter = editor.getMacros().findByMember(nodeUuid(rig.filterB));
    const auto* pad = editor.getMacros().findByMember(nodeUuid(rig.trackInB));
    ASSERT_NE(busAfter, nullptr);
    ASSERT_NE(pad, nullptr);
    EXPECT_EQ(busAfter->name, "Filter Bus") << "the second track joins the SAME bus, no second one";
    EXPECT_NE(pad, busAfter);
    EXPECT_FALSE(pad->hasMember(nodeUuid(rig.filterB)));
    expectIdenticalRendersCFT(reference.render(16), converted.render(16));
}

TEST(ChannelFlowMakeChannelCore, AlreadyChanneledOrGroupedTargetIsANoOp) {
    HostedPatchCFT patch;
    GraphEditor editor(patch.engine);
    auto& graph = patch.engine.getGraph();
    const auto rig = buildLegacyRigCFT(patch.engine, patch.output, RigShapeCFT::SharedLfo);
    ASSERT_TRUE(editor.makeChannelFromNode(rig.trackInA->nodeID, "Lead"));

    const auto graphBefore = juce::JSON::toString(synth::AIStateMapper::graphToJSON(graph));
    const auto macrosBefore = juce::JSON::toString(editor.getMacros().toVar());
    EXPECT_FALSE(editor.nodeNeedsChannel(rig.trackInA->nodeID));
    EXPECT_FALSE(editor.makeChannelFromNode(rig.trackInA->nodeID, "Again"));
    EXPECT_EQ(juce::JSON::toString(synth::AIStateMapper::graphToJSON(graph)), graphBefore);
    EXPECT_EQ(juce::JSON::toString(editor.getMacros().toVar()), macrosBefore);

    // A node that would move but is already in a macro refuses the whole action (flat model).
    synth::Macro handMade;
    handMade.name = "Hand";
    handMade.members = {nodeUuid(rig.oscB)};
    editor.getMacros().add(handMade);
    const auto plan = synth::planMakeChannel(graph, rig.trackInB->nodeID, editor.getMacros());
    EXPECT_TRUE(plan.needsChannel);
    EXPECT_TRUE(plan.refusal.isNotEmpty());
    EXPECT_FALSE(editor.makeChannelFromNode(rig.trackInB->nodeID, "Pad"));
    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::ChannelStrip), 1);
}

// -------------------------------------------------------------------------------------------
// The real app wiring: header / canvas / module right-click menus, one undo step each.
// -------------------------------------------------------------------------------------------

TEST_F(ChannelFlowTest, TrackHeaderMakeChannelThroughTheRealRightClickIsOneUndoStep) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    mc.newPatchForTest();
    auto& graph = mc.getAudioEngine().getGraph();
    const auto setup = buildMcRigCFT(mc, RigShapeCFT::SharedLfo);
    ASSERT_NE(setup.rig.trackInA, nullptr);
    const auto before = snapshotCFT(mc);

    makeChannelFromHeaderCFT(mc, setup.trackA);

    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::ChannelStrip), 1);
    const auto* macro = mc.getGraphEditor().getMacros().findByMember(nodeUuid(setup.rig.trackInA));
    ASSERT_NE(macro, nullptr);
    EXPECT_EQ(macro->name, "Lead") << "the channel macro is named after the track";
    EXPECT_FALSE(macro->hasMember(nodeUuid(setup.rig.sharedLfo)));
    const auto after = snapshotCFT(mc);

    // Already channeled: the entry stays in place but disabled, and choosing it anyway is a no-op.
    auto* header = headerForCFT(mc, setup.trackA);
    ASSERT_NE(header, nullptr);
    const auto menu = rightClickHeaderMenuCFT(*header);
    const auto* item = findMenuItemByTextCFT(menu, "Make Channel");
    ASSERT_NE(item, nullptr);
    EXPECT_FALSE(item->isEnabled);
    header->applyContextMenuChoice(synth::ui::TimelineTrackHeaderComponent::kMakeChannelMenuId);
    expectSameSnapshotCFT(snapshotCFT(mc), after);

    ASSERT_TRUE(mc.getUndoManager().undo());
    expectSameSnapshotCFT(snapshotCFT(mc), before);
    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::ChannelStrip), 0) << "the ONE undo step reverts everything";
    ASSERT_TRUE(mc.getUndoManager().redo());
    expectSameSnapshotCFT(snapshotCFT(mc), after);
}

TEST_F(ChannelFlowTest, CanvasSelectionMakeChannelThroughTheRealRightClick) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    mc.newPatchForTest();
    const auto setup = buildMcRigCFT(mc, RigShapeCFT::SharedLfo);
    ASSERT_NE(setup.rig.trackInA, nullptr);
    auto& editor = mc.getGraphEditor();

    // The module card's own menu offers it for the chain it belongs to...
    auto* filterComp = compForCFT(editor, setup.rig.filterA->nodeID);
    ASSERT_NE(filterComp, nullptr);
    const auto moduleMenu = rightClickModuleMenuCFT(*filterComp);
    const auto* moduleItem = findMenuItemByTextCFT(moduleMenu, "Make Channel");
    ASSERT_NE(moduleItem, nullptr) << "a module card's menu offers Make Channel for its chain";
    EXPECT_TRUE(moduleItem->isEnabled);

    // ...and so does the canvas menu for a selected chain (the real empty-canvas right-click).
    editor.setSelectedNodes({setup.rig.oscA->nodeID, setup.rig.filterA->nodeID});
    juce::PopupMenu canvasMenu;
    bool shown = false;
    editor.setShowCanvasContextMenuHookForTest([&](juce::PopupMenu& menu) {
        canvasMenu = menu;
        shown = true;
    });
    const juce::Point<int> emptyCanvas(editor.getWidth() - 20, 20);
    editor.mouseDown(realMouseEventCFT(editor, emptyCanvas, emptyCanvas,
                                       juce::ModifierKeys(juce::ModifierKeys::rightButtonModifier)));
    editor.setShowCanvasContextMenuHookForTest(nullptr);
    ASSERT_TRUE(shown) << "the right-click must reach the canvas menu";
    const auto* item = findMenuItemByTextCFT(canvasMenu, "Make Channel");
    ASSERT_NE(item, nullptr);
    EXPECT_TRUE(item->isEnabled);
    ASSERT_TRUE(item->action != nullptr);

    const auto before = snapshotCFT(mc);
    item->action();
    const auto* macro = editor.getMacros().findByMember(nodeUuid(setup.rig.filterA));
    ASSERT_NE(macro, nullptr);
    EXPECT_EQ(macro->name, "Lead") << "the selection resolves to its track, whose name the channel takes";
    EXPECT_TRUE(macro->hasMember(nodeUuid(setup.rig.trackInA)));
    expectOneUndoStepCFT(mc, before);
}

TEST_F(ChannelFlowTest, MergePointBecomesABusChannelInOneUndoStep) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    mc.newPatchForTest();
    auto& graph = mc.getAudioEngine().getGraph();
    const auto setup = buildMcRigCFT(mc, RigShapeCFT::Merge);
    ASSERT_NE(setup.rig.trackInA, nullptr);
    const auto before = snapshotCFT(mc);

    makeChannelFromHeaderCFT(mc, setup.trackA);

    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::ChannelStrip), 2);
    const auto* bus = mc.getGraphEditor().getMacros().findByMember(nodeUuid(setup.rig.filterB));
    ASSERT_NE(bus, nullptr);
    EXPECT_EQ(bus->name, "Filter Bus");
    expectOneUndoStepCFT(mc, before);
}

TEST_F(ChannelFlowTest, PolyChainGetsAVoiceMixerAheadOfTheStripInOneUndoStep) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    mc.newPatchForTest();
    auto& graph = mc.getAudioEngine().getGraph();
    auto* output = findNodeNamedCFT(graph, "Audio Output");
    ASSERT_NE(output, nullptr);

    juce::String trackInUuid, oscUuid;
    auto* trackIn = addPlainNodeCFT(graph, "Track In", {0, 0}, trackInUuid);
    auto* osc = addPlainNodeCFT(graph, "Oscillator", {200, 0}, oscUuid);
    ASSERT_NE(trackIn, nullptr);
    ASSERT_NE(osc, nullptr);
    ASSERT_TRUE(setPolyParamCFT(osc->getProcessor(), true));
    graph.addConnection({{trackIn->nodeID, juce::AudioProcessorGraph::midiChannelIndex},
                         {osc->nodeID, juce::AudioProcessorGraph::midiChannelIndex}});
    graph.addConnection({{osc->nodeID, 0}, {output->nodeID, 0}});
    graph.addConnection(
        {{osc->nodeID, dynamic_cast<ModuleBase*>(osc->getProcessor())->rightAudioLegChannel()}, {output->nodeID, 1}});
    auto& doc = mc.getTimelineDoc();
    const auto track = doc.addTrack(synth::TrackKind::Midi, "Poly Lead");
    ASSERT_TRUE(doc.setTrackBinding(track, trackInUuid));
    mc.getGraphEditor().updateComponents();
    const auto before = snapshotCFT(mc);

    makeChannelFromHeaderCFT(mc, track);

    const auto* macro = mc.getGraphEditor().getMacros().findByMember(trackInUuid);
    ASSERT_NE(macro, nullptr);
    auto* voiceMixer = findMacroMemberOfTypeCFT(graph, *macro, ModuleType::VoiceMixer);
    auto* eq = findMacroMemberOfTypeCFT(graph, *macro, ModuleType::ParametricEQ);
    ASSERT_NE(voiceMixer, nullptr) << "a chain ending poly gets a Voice Mixer ahead of the strip";
    ASSERT_NE(eq, nullptr);
    for (int voice = 0; voice < 8; ++voice)
        EXPECT_TRUE(graph.isConnected({{osc->nodeID, voice}, {voiceMixer->nodeID, voice}}));
    EXPECT_TRUE(graph.isConnected({{voiceMixer->nodeID, 0}, {eq->nodeID, 0}}));
    EXPECT_TRUE(graph.isConnected({{voiceMixer->nodeID, 1}, {eq->nodeID, 1}}));
    EXPECT_FALSE(graph.isConnected({{osc->nodeID, 0}, {eq->nodeID, 0}}));
    EXPECT_TRUE(synth::isProcessorPoly(osc->getProcessor())) << "poly is never forced on or off";
    expectOneUndoStepCFT(mc, before);
}

TEST_F(ChannelFlowTest, DuplicateIntoChannelGivesAnIndependentCopyAndLeavesTheOtherTrackOnTheOriginal) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    mc.newPatchForTest();
    auto& graph = mc.getAudioEngine().getGraph();
    const auto setup = buildMcRigCFT(mc, RigShapeCFT::SharedLfo);
    ASSERT_NE(setup.rig.trackInA, nullptr);
    const auto& rig = setup.rig;
    makeChannelFromHeaderCFT(mc, setup.trackA);
    auto& editor = mc.getGraphEditor();
    const auto* macro = editor.getMacros().findByMember(nodeUuid(rig.trackInA));
    ASSERT_NE(macro, nullptr);

    auto* lfoComp = compForCFT(editor, rig.sharedLfo->nodeID);
    ASSERT_NE(lfoComp, nullptr);
    const auto menu = rightClickModuleMenuCFT(*lfoComp);
    const auto* item = findMenuItemByTextCFT(menu, "Duplicate into Channel: Lead");
    ASSERT_NE(item, nullptr) << "a module shared into a channel from outside offers Duplicate into Channel";
    ASSERT_TRUE(item->action != nullptr);

    // Every hidden attenuverter fed by `source`'s output, with its "amount" parameter.
    auto amountsFedBy = [&graph](const juce::AudioProcessorGraph::Node* source) {
        std::vector<juce::RangedAudioParameter*> amounts;
        for (const auto& c : graph.getConnections()) {
            if (c.source.nodeID != source->nodeID)
                continue;
            auto* dest = graph.getNodeForId(c.destination.nodeID);
            if (!isModuleOfTypeCFT(dest, ModuleType::Attenuverter))
                continue;
            for (auto* p : dest->getProcessor()->getParameters())
                if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(p))
                    if (ranged->getParameterID() == "amount")
                        amounts.push_back(ranged);
        }
        return amounts;
    };
    // A non-default routing depth, so a silently reset amount on the copy would show.
    constexpr float kAmount = 0.37f;
    for (auto* amount : amountsFedBy(rig.sharedLfo))
        amount->setValueNotifyingHost(amount->convertTo0to1(kAmount));

    const int lfosBefore = countNodesOfTypeCFT(graph, ModuleType::LFO);
    const auto before = snapshotCFT(mc);
    item->action();

    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::LFO), lfosBefore + 1);
    macro = editor.getMacros().findByMember(nodeUuid(rig.trackInA));
    ASSERT_NE(macro, nullptr);
    juce::AudioProcessorGraph::Node* copy = nullptr;
    for (auto* node : graph.getNodes())
        if (isModuleOfTypeCFT(node, ModuleType::LFO) && node != rig.ownLfo && macro->hasMember(nodeUuid(node)))
            copy = node;
    ASSERT_NE(copy, nullptr) << "the copy lives inside this channel's macro";
    EXPECT_FALSE(macro->hasMember(nodeUuid(rig.sharedLfo))) << "the original stays outside";

    EXPECT_TRUE(modulatesCFT(graph, copy, rig.filterA, rig.cutoffChannel)) << "this channel is rewired to the copy";
    EXPECT_FALSE(modulatesCFT(graph, rig.sharedLfo, rig.filterA, rig.cutoffChannel));
    EXPECT_TRUE(modulatesCFT(graph, rig.sharedLfo, rig.filterB, rig.cutoffChannel))
        << "the other track stays on the original";

    // The copy's modulation keeps the original routing's depth.
    const auto copyAmounts = amountsFedBy(copy);
    ASSERT_FALSE(copyAmounts.empty()) << "the copy modulates through its own attenuverter";
    for (auto* amount : copyAmounts)
        EXPECT_NEAR(amount->convertFrom0to1(amount->getValue()), kAmount, 1e-4f);

    // Independent: retuning the copy leaves the original alone.
    auto* originalParam = rig.sharedLfo->getProcessor()->getParameters()[0];
    auto* copyParam = copy->getProcessor()->getParameters()[0];
    EXPECT_FLOAT_EQ(copyParam->getValue(), originalParam->getValue()) << "the copy starts with the same settings";
    const float originalValue = originalParam->getValue();
    copyParam->setValueNotifyingHost(originalValue > 0.5f ? 0.1f : 0.9f);
    EXPECT_FLOAT_EQ(originalParam->getValue(), originalValue);
    copyParam->setValueNotifyingHost(originalValue);

    expectOneUndoStepCFT(mc, before);
}

// -------------------------------------------------------------------------------------------
// Adversarial probes (review): exactly-one CV path (no double-drive), a three-way merge, and a
// merge point followed by a SECOND shared effect before the output.
// -------------------------------------------------------------------------------------------

// Counts every path `source` modulates `dest`'s `channel` through, including through a macro
// inlet port -- unlike modulatesCFT (which only proves at least one exists), this catches a splice
// that leaves the old direct edge AND adds a new ported one (double-driving the CV, not just
// changing where it enters).
int countModulationPathsCFT(juce::AudioProcessorGraph& graph, juce::AudioProcessorGraph::Node* source,
                            juce::AudioProcessorGraph::Node* dest, int channel) {
    const auto connections = graph.getConnections();
    int count = 0;
    for (const auto& in : connections) {
        if (in.source.nodeID != source->nodeID ||
            !isModuleOfTypeCFT(graph.getNodeForId(in.destination.nodeID), ModuleType::Attenuverter))
            continue;
        for (const auto& out : connections) {
            if (out.source.nodeID != in.destination.nodeID)
                continue;
            if (out.destination.nodeID == dest->nodeID && out.destination.channelIndex == channel)
                ++count;
            if (isModuleOfTypeCFT(graph.getNodeForId(out.destination.nodeID), ModuleType::MacroInlet))
                for (const auto& viaPort : connections)
                    if (viaPort.source.nodeID == out.destination.nodeID && viaPort.destination.nodeID == dest->nodeID &&
                        viaPort.destination.channelIndex == channel)
                        ++count;
        }
    }
    return count;
}

TEST(ChannelFlowMakeChannelCore, SharedLfoEntersThroughExactlyOnePathNoDoubleDrive) {
    HostedPatchCFT patch;
    GraphEditor editor(patch.engine);
    auto& graph = patch.engine.getGraph();
    const auto rig = buildLegacyRigCFT(patch.engine, patch.output, RigShapeCFT::SharedLfo);
    ASSERT_TRUE(editor.makeChannelFromNode(rig.trackInA->nodeID, "Lead"));
    EXPECT_EQ(countModulationPathsCFT(graph, rig.sharedLfo, rig.filterA, rig.cutoffChannel), 1)
        << "the splice must retarget the crossing, never add a second path alongside the original";
    EXPECT_EQ(countModulationPathsCFT(graph, rig.sharedLfo, rig.filterB, rig.cutoffChannel), 1)
        << "the untouched track's own routing must not be duplicated either";
}

namespace {

// Three MIDI tracks whose Oscillators all feed ONE shared Filter (a three-way merge), which then
// goes straight to Audio Output. No LFOs -- isolates the merge-head/bus logic itself.
struct ThreeWayRigCFT {
    juce::AudioProcessorGraph::Node* trackInA = nullptr;
    juce::AudioProcessorGraph::Node* trackInB = nullptr;
    juce::AudioProcessorGraph::Node* trackInC = nullptr;
    juce::AudioProcessorGraph::Node* oscA = nullptr;
    juce::AudioProcessorGraph::Node* oscB = nullptr;
    juce::AudioProcessorGraph::Node* oscC = nullptr;
    juce::AudioProcessorGraph::Node* filter = nullptr;
};

ThreeWayRigCFT buildThreeWayRigCFT(AudioEngine& engine, juce::AudioProcessorGraph::Node* output) {
    auto& graph = engine.getGraph();
    constexpr int midi = juce::AudioProcessorGraph::midiChannelIndex;
    ThreeWayRigCFT rig;
    juce::String unused;
    rig.trackInA = addPlainNodeCFT(graph, "Track In", {0, 0}, unused);
    rig.trackInB = addPlainNodeCFT(graph, "Track In", {0, 300}, unused);
    rig.trackInC = addPlainNodeCFT(graph, "Track In", {0, 600}, unused);
    rig.oscA = addPlainNodeCFT(graph, "Oscillator", {200, 0}, unused);
    rig.oscB = addPlainNodeCFT(graph, "Oscillator", {200, 300}, unused);
    rig.oscC = addPlainNodeCFT(graph, "Oscillator", {200, 600}, unused);
    rig.filter = addPlainNodeCFT(graph, "Filter", {500, 300}, unused);

    for (const auto& pair : {std::array<juce::AudioProcessorGraph::Node*, 2>{rig.trackInA, rig.oscA},
                             {rig.trackInB, rig.oscB},
                             {rig.trackInC, rig.oscC}})
        graph.addConnection({{pair[0]->nodeID, midi}, {pair[1]->nodeID, midi}});

    const int oscRight = dynamic_cast<ModuleBase*>(rig.oscA->getProcessor())->rightAudioLegChannel();
    const int filterRight = dynamic_cast<ModuleBase*>(rig.filter->getProcessor())->rightAudioLegChannel();
    for (const auto [oscLeg, filterLeg, outLeg] : {std::array<int, 3>{0, 0, 0}, {oscRight, filterRight, 1}}) {
        for (auto* osc : {rig.oscA, rig.oscB, rig.oscC})
            graph.addConnection({{osc->nodeID, oscLeg}, {rig.filter->nodeID, filterLeg}});
        graph.addConnection({{rig.filter->nodeID, filterLeg}, {output->nodeID, outLeg}});
    }
    return rig;
}

} // namespace

TEST(ChannelFlowMakeChannelCore, ThreeWayMergeBecomesOneBusChannelAndTheRenderIsIdentical) {
    HostedPatchCFT reference, converted;
    buildThreeWayRigCFT(reference.engine, reference.output);
    const auto rig = buildThreeWayRigCFT(converted.engine, converted.output);
    auto& graph = converted.engine.getGraph();

    GraphEditor editor(converted.engine);
    ASSERT_TRUE(editor.makeChannelFromNode(rig.trackInA->nodeID, "A"));
    // Each track has its own exclusive Oscillator feeding the shared Filter, so A gets its own
    // strip (for oscA -> filter) PLUS the shared filter's bus strip -- same shape as the two-track
    // merge test, one track further.
    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::ChannelStrip), 2) << "A's own strip plus the bus strip";
    auto* busAfterA = editor.getMacros().findByMember(nodeUuid(rig.filter));
    ASSERT_NE(busAfterA, nullptr);
    EXPECT_FALSE(busAfterA->hasMember(nodeUuid(rig.oscB))) << "track B's own module stays outside the bus";
    EXPECT_FALSE(busAfterA->hasMember(nodeUuid(rig.oscC))) << "track C's own module stays outside the bus";
    expectIdenticalRendersCFT(reference.render(16), converted.render(16));

    ASSERT_TRUE(editor.makeChannelFromNode(rig.trackInB->nodeID, "B"));
    ASSERT_TRUE(editor.makeChannelFromNode(rig.trackInC->nodeID, "C"));
    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::ChannelStrip), 4)
        << "three own strips (A, B, C) plus exactly ONE shared bus strip -- never a second bus for "
           "the same merge head, and the bus is never duplicated across a three-way merge";
    const auto* busAfterAll = editor.getMacros().findByMember(nodeUuid(rig.filter));
    ASSERT_NE(busAfterAll, nullptr);
    EXPECT_EQ(busAfterAll->name, "Filter Bus");
    auto* aMacro = editor.getMacros().findByMember(nodeUuid(rig.oscA));
    auto* bMacro = editor.getMacros().findByMember(nodeUuid(rig.oscB));
    auto* cMacro = editor.getMacros().findByMember(nodeUuid(rig.oscC));
    ASSERT_NE(aMacro, nullptr);
    ASSERT_NE(bMacro, nullptr);
    ASSERT_NE(cMacro, nullptr);
    EXPECT_NE(aMacro, busAfterAll);
    EXPECT_NE(bMacro, busAfterAll);
    EXPECT_NE(cMacro, busAfterAll);
    EXPECT_NE(aMacro, bMacro);
    EXPECT_NE(bMacro, cMacro);
    expectIdenticalRendersCFT(reference.render(16), converted.render(16));
}

// Two MIDI tracks sharing ONE Oscillator directly (no per-track audio node at all, the T173e
// "two tracks, one shared instrument" shape) -- Make Channel on either track must build only the
// shared instrument's bus, no separate (redundant) strip for the track itself.
TEST(ChannelFlowMakeChannelCore, TwoMidiTracksSharingOneInstrumentGetJustTheSharedBus) {
    HostedPatchCFT reference, converted;
    auto buildShared = [](AudioEngine& engine, juce::AudioProcessorGraph::Node* output, juce::String& aUuid,
                          juce::String& bUuid) {
        auto& graph = engine.getGraph();
        constexpr int midi = juce::AudioProcessorGraph::midiChannelIndex;
        auto* trackInA = addPlainNodeCFT(graph, "Track In", {0, 0}, aUuid);
        auto* trackInB = addPlainNodeCFT(graph, "Track In", {0, 300}, bUuid);
        juce::String unused;
        auto* osc = addPlainNodeCFT(graph, "Oscillator", {200, 150}, unused);
        graph.addConnection({{trackInA->nodeID, midi}, {osc->nodeID, midi}});
        graph.addConnection({{trackInB->nodeID, midi}, {osc->nodeID, midi}});
        const int oscRight = dynamic_cast<ModuleBase*>(osc->getProcessor())->rightAudioLegChannel();
        graph.addConnection({{osc->nodeID, 0}, {output->nodeID, 0}});
        graph.addConnection({{osc->nodeID, oscRight}, {output->nodeID, 1}});
        return osc;
    };
    juce::String refA, refB, aUuid, bUuid;
    buildShared(reference.engine, reference.output, refA, refB);
    auto* osc = buildShared(converted.engine, converted.output, aUuid, bUuid);
    auto& graph = converted.engine.getGraph();

    GraphEditor editor(converted.engine);
    auto* trackInANode = nodeForUuidCFT(graph, aUuid);
    auto* trackInBNode = nodeForUuidCFT(graph, bUuid);
    ASSERT_NE(trackInANode, nullptr);
    ASSERT_NE(trackInBNode, nullptr);
    ASSERT_TRUE(editor.nodeNeedsChannel(trackInANode->nodeID));
    ASSERT_TRUE(editor.makeChannelFromNode(trackInANode->nodeID, "A"));

    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::ChannelStrip), 1)
        << "the shared instrument gets exactly one channel -- no separate strip for a MIDI track "
           "that contributes no exclusive audio of its own";
    const auto* macro = editor.getMacros().findByMember(nodeUuid(osc));
    ASSERT_NE(macro, nullptr) << "the shared oscillator itself must end up in a channel";
    EXPECT_FALSE(macro->hasMember(aUuid)) << "the Track In itself never moves -- it isn't the channel's audio source";
    // Track B's own header entry must now report "no-op" -- it already shares this channel.
    EXPECT_FALSE(editor.nodeNeedsChannel(trackInBNode->nodeID))
        << "track B already shares this channel and must not offer a second Make Channel";

    expectIdenticalRendersCFT(reference.render(16), converted.render(16));
}

namespace {

// A merge into Filter M1, which then feeds a SECOND shared effect Filter M2 before Audio Output --
// the bus must absorb BOTH shared nodes, not just the merge head.
struct ChainedMergeRigCFT {
    juce::AudioProcessorGraph::Node* trackInA = nullptr;
    juce::AudioProcessorGraph::Node* trackInB = nullptr;
    juce::AudioProcessorGraph::Node* oscA = nullptr;
    juce::AudioProcessorGraph::Node* oscB = nullptr;
    juce::AudioProcessorGraph::Node* filterA = nullptr;
    juce::AudioProcessorGraph::Node* m1 = nullptr;
    juce::AudioProcessorGraph::Node* m2 = nullptr;
};

ChainedMergeRigCFT buildChainedMergeRigCFT(AudioEngine& engine, juce::AudioProcessorGraph::Node* output) {
    auto& graph = engine.getGraph();
    constexpr int midi = juce::AudioProcessorGraph::midiChannelIndex;
    ChainedMergeRigCFT rig;
    juce::String unused;
    rig.trackInA = addPlainNodeCFT(graph, "Track In", {0, 0}, unused);
    rig.trackInB = addPlainNodeCFT(graph, "Track In", {0, 400}, unused);
    rig.oscA = addPlainNodeCFT(graph, "Oscillator", {200, 0}, unused);
    rig.oscB = addPlainNodeCFT(graph, "Oscillator", {200, 400}, unused);
    rig.filterA = addPlainNodeCFT(graph, "Filter", {400, 0}, unused);
    rig.m1 = addPlainNodeCFT(graph, "Filter", {600, 200}, unused);
    rig.m2 = addPlainNodeCFT(graph, "Filter", {800, 200}, unused);

    graph.addConnection({{rig.trackInA->nodeID, midi}, {rig.oscA->nodeID, midi}});
    graph.addConnection({{rig.trackInB->nodeID, midi}, {rig.oscB->nodeID, midi}});

    const int oscRight = dynamic_cast<ModuleBase*>(rig.oscA->getProcessor())->rightAudioLegChannel();
    const int filterRight = dynamic_cast<ModuleBase*>(rig.filterA->getProcessor())->rightAudioLegChannel();
    for (const auto [oscLeg, filterLeg, outLeg] : {std::array<int, 3>{0, 0, 0}, {oscRight, filterRight, 1}}) {
        graph.addConnection({{rig.oscA->nodeID, oscLeg}, {rig.filterA->nodeID, filterLeg}});
        graph.addConnection({{rig.filterA->nodeID, filterLeg}, {rig.m1->nodeID, filterLeg}});
        graph.addConnection({{rig.oscB->nodeID, oscLeg}, {rig.m1->nodeID, filterLeg}});
        graph.addConnection({{rig.m1->nodeID, filterLeg}, {rig.m2->nodeID, filterLeg}});
        graph.addConnection({{rig.m2->nodeID, filterLeg}, {output->nodeID, outLeg}});
    }
    return rig;
}

} // namespace

TEST(ChannelFlowMakeChannelCore, MergeFollowedByASecondSharedEffectBoxesBothIntoOneBus) {
    HostedPatchCFT reference, converted;
    buildChainedMergeRigCFT(reference.engine, reference.output);
    const auto rig = buildChainedMergeRigCFT(converted.engine, converted.output);
    auto& graph = converted.engine.getGraph();

    GraphEditor editor(converted.engine);
    ASSERT_TRUE(editor.makeChannelFromNode(rig.trackInA->nodeID, "Lead"));
    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::ChannelStrip), 2) << "the track's own strip plus the bus strip";

    const auto* bus = editor.getMacros().findByMember(nodeUuid(rig.m1));
    ASSERT_NE(bus, nullptr) << "the merge head (m1) must be boxed into a bus";
    EXPECT_TRUE(bus->hasMember(nodeUuid(rig.m2)))
        << "the SECOND shared effect downstream of the merge joins the SAME bus, not left dangling or double-strapped";

    auto* busStrip = findMacroMemberOfTypeCFT(graph, *bus, ModuleType::ChannelStrip);
    ASSERT_NE(busStrip, nullptr);
    auto* master = findNodeOfTypeCFT(graph, ModuleType::Master);
    ASSERT_NE(master, nullptr);
    EXPECT_TRUE(graph.isConnected({{busStrip->nodeID, 0}, {master->nodeID, MasterModule::kMixLeft}}))
        << "the bus strip sits after BOTH shared effects, not spliced in the middle";
    EXPECT_FALSE(graph.isConnected({{rig.m1->nodeID, 0}, {master->nodeID, MasterModule::kMixLeft}}))
        << "m1 must not leak straight to Master now that m2 sits between it and the bus strip";

    expectIdenticalRendersCFT(reference.render(16), converted.render(16));
}
