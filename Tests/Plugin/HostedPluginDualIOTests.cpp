// HostedPluginDualIOTests.cpp
//
// A hosted plugin follows the "Dual I/O for new modules" preference like every other stereo module.
//  1. Stereo negotiation -- a plugin whose default main bus is mono but which runs in stereo is published with two
//     jacks; a genuinely mono, a wide (3+) or an input-less plugin is left as it is.
//  2. The toggle -- exactly 2 outputs (inputs): off is one "Audio" jack owning both raw legs, on is Left/Right.
//  3. Backward compatibility -- a project saved before the toggle (no "dualIO" value) loads with Left/Right.
//  4. The preference reaches a NEW hosted module on the canvas path; collapsing never drops a cable.
//  5. Loading a patch numbers duplicate hosted plugins ("Diva", "Diva 2") immediately.

#include "../StubPluginInstance.h"
#include "AI/AIStateMapper/AIStateMapper.h"
#include "AudioEngine/AudioEngine.h"
#include "AudioEngine/ModuleTitle.h"
#include "Modules/FX/DelayModule.h"
#include "Plugin/Hosting/HostedPluginModule.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include <chrono>
#include <gtest/gtest.h>
#include <tuple>

using synth::HostedPluginBackend;
using synth::HostedPluginModule;
using synth::PluginIdentity;
using synth::test::StubBackend;
using synth::test::StubPluginInstance;

namespace {

template <typename Predicate>
bool pumpUntil(Predicate predicate, int timeoutMs = 2000) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    do {
        if (predicate())
            return true;
        juce::MessageManager::getInstance()->runDispatchLoopUntil(5);
    } while (std::chrono::steady_clock::now() < deadline);
    return predicate();
}

juce::PluginDescription descriptionNamed(const juce::String& name = "Stub Plugin", int uid = 0x5754424) {
    juce::PluginDescription description;
    description.name = name;
    description.pluginFormatName = "VST3";
    description.uniqueId = uid;
    description.deprecatedUid = uid;
    description.fileOrIdentifier = "/nonexistent/test/path/" + name + ".vst3";
    return description;
}

PluginIdentity identityNamed(const juce::String& name, int uid) {
    PluginIdentity identity;
    identity.format = "VST3";
    identity.name = name;
    identity.uid = uid;
    return identity;
}

/** A stub that is flexible about its bus layout (many real plugins), with the given DEFAULT channel counts. */
StubBackend flexibleBackend(int inputs, int outputs, const juce::String& name = "Flexible") {
    return StubBackend([=] {
        return std::make_unique<StubPluginInstance>(inputs, outputs, name, 0x5754424, "VST3",
                                                    std::vector<synth::test::StubParamSpec>{}, false, 0, true);
    });
}

void setDual(juce::AudioProcessor& processor, bool dual) {
    auto* param = findParameterByID(&processor, "dualIO");
    ASSERT_NE(param, nullptr);
    param->setValueNotifyingHost(dual ? 1.0f : 0.0f);
}

HostedPluginModule* findHosted(juce::AudioProcessorGraph& graph) {
    HostedPluginModule* found = nullptr;
    for (auto* node : graph.getNodes())
        if (auto* hosted = dynamic_cast<HostedPluginModule*>(node->getProcessor()))
            found = hosted;
    return found;
}

std::unique_ptr<HostedPluginModule> loaded(StubBackend& backend) {
    auto module = std::make_unique<HostedPluginModule>();
    module->prepareToPlay(48000.0, 64);
    module->loadPlugin(descriptionNamed(), backend);
    EXPECT_TRUE(pumpUntil([&] { return module->hasInstance(); }));
    return module;
}

} // namespace

// ---------------------------------------------------------------------------
// 1. Stereo negotiation
// ---------------------------------------------------------------------------

TEST(HostedPluginDualIOTest, AMonoDefaultSynthThatSupportsStereoIsPublishedWithTwoOutputs) {
    auto backend = flexibleBackend(0, 1, "Mono Default Synth");
    auto module = loaded(backend);

    EXPECT_EQ(module->getVisibleOutputPortCount(), 2) << "the stereo main output was asked for and granted";
    EXPECT_EQ(module->getVisibleInputPortCount(), 0) << "an instrument's disabled input is not switched on";
    EXPECT_EQ(module->rightAudioLegChannel(), 1);
}

TEST(HostedPluginDualIOTest, AMonoInMonoOutEffectThatSupportsStereoGetsTwoJacksOnBothSides) {
    auto backend = flexibleBackend(1, 1, "Mono Default Fx");
    auto module = loaded(backend);

    EXPECT_EQ(module->getVisibleInputPortCount(), 2);
    EXPECT_EQ(module->getVisibleOutputPortCount(), 2);
}

TEST(HostedPluginDualIOTest, AGenuinelyMonoPluginStaysMono) {
    StubBackend backend([] { return std::make_unique<StubPluginInstance>(1, 1, "Mono Only"); }); // layout locked
    auto module = loaded(backend);

    EXPECT_EQ(module->getVisibleInputPortCount(), 1);
    EXPECT_EQ(module->getVisibleOutputPortCount(), 1);
    EXPECT_EQ(module->rightAudioLegChannel(), 0);
}

TEST(HostedPluginDualIOTest, AWidePluginKeepsAllItsRawJacksWhateverTheToggleSays) {
    auto backend = flexibleBackend(0, 6, "Surround Synth");
    auto module = loaded(backend);

    for (bool dual : {true, false}) {
        setDual(*module, dual);
        EXPECT_EQ(module->getVisibleOutputPortCount(), 6) << "dual=" << dual;
        EXPECT_EQ(module->getOutputPortLabel(2), "Out 3") << "dual=" << dual;
        EXPECT_EQ(module->mapOutputChannel(5).visibleJackIndex, 5) << "dual=" << dual;
    }
}

// ---------------------------------------------------------------------------
// 2. The toggle on a stereo pair
// ---------------------------------------------------------------------------

TEST(HostedPluginDualIOTest, DualOffCollapsesAStereoPluginToOneAudioJackOwningBothLegs) {
    StubBackend backend; // 2 in / 2 out
    auto module = loaded(backend);

    // On (the default): today's separate jacks.
    EXPECT_TRUE(module->isDualIO());
    EXPECT_EQ(module->getVisibleOutputPortCount(), 2);
    EXPECT_EQ(module->getOutputPortLabel(0), "Out L");
    EXPECT_EQ(module->getOutputPortLabel(1), "Out R");
    EXPECT_EQ(module->mapOutputChannel(1).visibleJackIndex, 1);

    setDual(*module, false);
    EXPECT_EQ(module->getVisibleOutputPortCount(), 1);
    EXPECT_EQ(module->getVisibleInputPortCount(), 1);
    EXPECT_EQ(module->getOutputPortLabel(0), "Audio");
    EXPECT_EQ(module->getInputPortLabel(0), "Audio");

    const auto head = module->mapOutputChannel(0);
    EXPECT_EQ(head.visibleJackIndex, 0);
    EXPECT_TRUE(head.isPolyGroupHead);
    EXPECT_EQ(head.polyVoiceSpan, 2) << "the one jack owns both raw legs";
    EXPECT_EQ(module->mapOutputChannel(1).visibleJackIndex, 0);
    EXPECT_FALSE(module->mapOutputChannel(1).isPolyGroupHead);
    EXPECT_EQ(module->mapInputChannel(0).polyVoiceSpan, 2);
    EXPECT_EQ(module->mapInputChannel(1).visibleJackIndex, 0);

    // The raw legs the cables and the mixer channel use do not move.
    EXPECT_EQ(module->rightAudioLegChannel(), 1);
    EXPECT_EQ(module->getTotalNumOutputChannels(), HostedPluginModule::kMaxPluginChannels);
}

TEST(HostedPluginDualIOTest, TheToggleOnlyTouchesTheSideThatHasExactlyTwoChannels) {
    auto backend = flexibleBackend(0, 2, "Stereo Synth"); // no input at all
    auto module = loaded(backend);
    setDual(*module, false);
    EXPECT_EQ(module->getVisibleInputPortCount(), 0);
    EXPECT_EQ(module->getVisibleOutputPortCount(), 1);
}

TEST(HostedPluginDualIOTest, ABareModuleIsUnaffectedByTheToggle) {
    HostedPluginModule module;
    setDual(module, false);
    EXPECT_EQ(module.getVisibleInputPortCount(), 1);
    EXPECT_EQ(module.getVisibleOutputPortCount(), 1);
    EXPECT_EQ(module.rightAudioLegChannel(), -1);
}

// ---------------------------------------------------------------------------
// 3. Backward compatibility
// ---------------------------------------------------------------------------

TEST(HostedPluginDualIOTest, AProjectSavedBeforeTheToggleLoadsWithSeparateLeftAndRightJacks) {
    StubBackend backend;
    AudioEngine engine;
    engine.initialise();
    engine.getGraph().clear();
    {
        auto module = std::make_unique<HostedPluginModule>();
        auto* raw = module.get();
        engine.getGraph().addNode(std::move(module));
        raw->loadPlugin(descriptionNamed(), backend);
        ASSERT_TRUE(pumpUntil([&] { return raw->hasInstance(); }));
    }

    // What an older build wrote: the same graph with no "dualIO" value on the hosted node.
    juce::var json = synth::AIStateMapper::graphToJSON(engine.getGraph());
    int stripped = 0;
    if (auto* nodes = json.getProperty("nodes", {}).getArray())
        for (auto& node : *nodes)
            if (auto* params = node.getProperty("params", {}).getDynamicObject())
                if (params->hasProperty("dualIO")) {
                    params->removeProperty("dualIO");
                    ++stripped;
                }
    ASSERT_EQ(stripped, 1);

    HostedPluginBackend::ScopedDefault installed(&backend);
    AudioEngine other;
    other.initialise();
    other.getGraph().clear();
    ASSERT_TRUE(synth::AIStateMapper::applyJSONToGraph(json, other.getGraph(), true, /*trusted=*/true));
    auto* restored = findHosted(other.getGraph());
    ASSERT_NE(restored, nullptr);
    ASSERT_TRUE(pumpUntil([&] { return restored->hasInstance(); }));

    EXPECT_TRUE(restored->isDualIO());
    EXPECT_EQ(restored->getVisibleOutputPortCount(), 2);
    EXPECT_EQ(restored->getOutputPortLabel(1), "Out R");
}

TEST(HostedPluginDualIOTest, ACollapsedChoiceSurvivesASaveAndLoad) {
    StubBackend backend;
    AudioEngine engine;
    engine.initialise();
    engine.getGraph().clear();
    auto module = std::make_unique<HostedPluginModule>();
    auto* raw = module.get();
    engine.getGraph().addNode(std::move(module));
    raw->loadPlugin(descriptionNamed(), backend);
    ASSERT_TRUE(pumpUntil([&] { return raw->hasInstance(); }));
    setDual(*raw, false);

    const juce::var json = synth::AIStateMapper::graphToJSON(engine.getGraph());
    HostedPluginBackend::ScopedDefault installed(&backend);
    AudioEngine other;
    other.initialise();
    other.getGraph().clear();
    ASSERT_TRUE(synth::AIStateMapper::applyJSONToGraph(json, other.getGraph(), true, true));
    auto* restored = findHosted(other.getGraph());
    ASSERT_NE(restored, nullptr);
    ASSERT_TRUE(pumpUntil([&] { return restored->hasInstance(); }));
    EXPECT_EQ(restored->getVisibleOutputPortCount(), 1);
}

TEST(HostedPluginDualIOTest, TheToggleDoesNotShiftTheHostedPluginsOwnParameters) {
    StubBackend backend([] {
        return std::make_unique<StubPluginInstance>(
            2, 2, "Params", 0x5754424, "VST3",
            std::vector<synth::test::StubParamSpec>{{"cutoff", "Cutoff", 0.5f}, {"res", "Resonance", 0.25f}});
    });
    auto module = loaded(backend);
    const auto infos = module->getInstanceParameters();
    ASSERT_EQ(infos.size(), 2u);
    EXPECT_EQ(infos[0].index, 0);
    EXPECT_EQ(infos[0].paramId, "cutoff");
    EXPECT_NE(module->findInstanceParameter("res"), nullptr);
    EXPECT_NE(findParameterByID(module.get(), "dualIO"), nullptr);
    EXPECT_NE(findParameterByID(module.get(), "muted"), nullptr);
}

// ---------------------------------------------------------------------------
// 4. The preference on the canvas path; collapsing keeps the cables
// ---------------------------------------------------------------------------

TEST(HostedPluginDualIOTest, ANewHostedPluginOnTheCanvasFollowsThePreference) {
    StubBackend backend;
    HostedPluginBackend::ScopedDefault installed(&backend);

    for (bool dual : {true, false}) {
        AudioEngine engine;
        GraphEditor editor(engine);
        editor.setSize(800, 600);
        editor.setDefaultDualIOForNewModules(dual);
        editor.addHostedPluginAtCanvasPosition(identityNamed("Stub Plugin", 1), {100, 100});

        auto* added = findHosted(engine.getGraph());
        ASSERT_NE(added, nullptr);
        ASSERT_TRUE(pumpUntil([&] { return added->hasInstance(); }));
        EXPECT_EQ(added->isDualIO(), dual);
        EXPECT_EQ(added->getVisibleOutputPortCount(), dual ? 2 : 1);
        EXPECT_EQ(added->getVisibleInputPortCount(), dual ? 2 : 1);
    }
}

TEST(HostedPluginDualIOTest, ThePerModuleOverrideForHostedPluginsWinsOverTheGlobalDefault) {
    StubBackend backend;
    HostedPluginBackend::ScopedDefault installed(&backend);
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);
    editor.setDefaultDualIOForNewModules(false);
    editor.setDualIOPerModuleOverrides({{"Hosted Plugin", true}});
    editor.addHostedPluginAtCanvasPosition(identityNamed("Stub Plugin", 1), {100, 100});

    auto* added = findHosted(engine.getGraph());
    ASSERT_NE(added, nullptr);
    ASSERT_TRUE(pumpUntil([&] { return added->hasInstance(); }));
    EXPECT_EQ(added->getVisibleOutputPortCount(), 2);
    EXPECT_TRUE(synth::AIStateMapper::dualIOCapableModuleTypes().contains("Hosted Plugin"))
        << "the Preferences per-module popup lists it";
}

TEST(HostedPluginDualIOTest, CollapsingAHostedPluginKeepsBothRawCables) {
    StubBackend backend;
    HostedPluginBackend::ScopedDefault installed(&backend);
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);
    auto& graph = engine.getGraph();

    auto* hosted = new HostedPluginModule();
    auto hostedNode = graph.addNode(std::unique_ptr<juce::AudioProcessor>(hosted));
    hosted->loadPlugin(descriptionNamed(), backend);
    ASSERT_TRUE(pumpUntil([&] { return hosted->hasInstance(); }));
    auto delayNode = graph.addNode(std::make_unique<DelayModule>());
    ASSERT_TRUE(graph.addConnection({{hostedNode->nodeID, 0}, {delayNode->nodeID, 0}}));
    ASSERT_TRUE(graph.addConnection({{hostedNode->nodeID, 1}, {delayNode->nodeID, 1}}));
    editor.updateComponents();

    ModuleComponent* card = nullptr;
    for (auto* comp : editor.getModuleComponents())
        if (comp->getModule() == hosted)
            card = comp;
    ASSERT_NE(card, nullptr);

    setDual(*hosted, false);
    card->applyDualIOLayoutChange();

    EXPECT_EQ(hosted->getVisibleOutputPortCount(), 1);
    int edges = 0;
    for (const auto& c : graph.getConnections())
        if (c.source.nodeID == hostedNode->nodeID && c.destination.nodeID == delayNode->nodeID)
            ++edges;
    EXPECT_EQ(edges, 2) << "a collapsed jack still owns both raw legs; no cable may be dropped";
}

// ---------------------------------------------------------------------------
// 5. Loading a patch numbers duplicates immediately
// ---------------------------------------------------------------------------

TEST(HostedPluginDualIOTest, LoadingAPatchWithTwoOfTheSamePluginNamesThemDivaAndDiva2AtOnce) {
    StubBackend backend;
    HostedPluginBackend::ScopedDefault installed(&backend);

    AudioEngine source;
    source.initialise();
    source.getGraph().clear();
    for (int i = 0; i < 2; ++i) {
        auto module = std::make_unique<HostedPluginModule>();
        module->loadPlugin(descriptionNamed("Diva", 1), backend);
        source.getGraph().addNode(std::move(module));
    }
    const juce::var json = synth::AIStateMapper::graphToJSON(source.getGraph());

    auto file = juce::File::createTempFile(".json");
    ASSERT_TRUE(file.replaceWithText(juce::JSON::toString(json)));

    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);
    editor.loadPreset(file, /*append=*/false);
    file.deleteFile();

    // No updateModuleNames() here: the load itself must have numbered them.
    std::vector<juce::String> titles;
    for (auto* node : engine.getGraph().getNodes())
        if (dynamic_cast<HostedPluginModule*>(node->getProcessor()) != nullptr)
            titles.push_back(synth::moduleTitle(*node));
    ASSERT_EQ(titles.size(), 2u);
    EXPECT_TRUE((titles[0] == "Diva" && titles[1] == "Diva 2") || (titles[0] == "Diva 2" && titles[1] == "Diva"));
}

// ---------------------------------------------------------------------------
// 6. The header button only exists for a stereo pair
// ---------------------------------------------------------------------------

namespace {
bool dualIOButtonVisible(ModuleComponent& card) {
    for (auto* child : card.getChildren())
        if (auto* button = dynamic_cast<juce::DrawableButton*>(child))
            if (button->getName() == "Dual I/O")
                return button->isVisible();
    return false;
}
} // namespace

TEST(HostedPluginDualIOTest, TheDualIOButtonShowsOnlyOnceAStereoInstancePublishes) {
    for (const auto& [inputs, outputs, expected] :
         {std::tuple{2, 2, true}, std::tuple{0, 2, true}, std::tuple{1, 1, false}, std::tuple{0, 6, false}}) {
        SCOPED_TRACE(juce::String(inputs) + " in / " + juce::String(outputs) + " out");
        StubBackend backend([=] { return std::make_unique<StubPluginInstance>(inputs, outputs, "Card Plugin"); });
        AudioEngine engine;
        GraphEditor editor(engine);
        HostedPluginModule module;
        ModuleComponent card(&module, juce::AudioProcessorGraph::NodeID(1), editor);
        card.setSize(280, 400);
        EXPECT_FALSE(dualIOButtonVisible(card)) << "a bare card has nothing to split";

        module.loadPlugin(descriptionNamed("Card Plugin", 7), backend);
        ASSERT_TRUE(pumpUntil([&] { return module.hasInstance(); }));
        EXPECT_EQ(dualIOButtonVisible(card), expected);
    }
}
