// HostedPluginOutputsTests.cpp
//
// Multi-output hosted plugins: every output bus the plugin accepts is switched on and renders (up to 64 channels),
// and the card shows only the buses chosen to show. A raw node output channel is the instance's own channel index, so
// cables never move when an output is shown or hidden. Runs against a fake juce::AudioPluginInstance shaped like a
// multi-output sampler; there is no real plugin to load in CI.

#include "../StubPluginInstance.h"
#include "AudioEngine/AudioEngine.h"
#include "Modules/FX/DelayModule.h"
#include "Plugin/Hosting/HostedPluginModule.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include <chrono>
#include <gtest/gtest.h>
#include <memory>

using synth::HostedPluginBackend;
using synth::HostedPluginModule;
using synth::test::StubHostedParameter;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 64;

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

juce::PluginDescription description(const juce::String& name = "Fake Multi Out") {
    juce::PluginDescription d;
    d.name = name;
    d.pluginFormatName = "VST3";
    d.uniqueId = 0x4d554c54;
    d.deprecatedUid = 0x4d554c54;
    d.fileOrIdentifier = "/nonexistent/test/path/" + name + ".vst3";
    return d;
}

struct OutBus {
    juce::String name;
    int channels = 2;
    bool onByDefault = true;
};

/** A plugin with the given input channels and output buses. Writes 0.1 * (outputChannel + 1) into every output channel,
 *  so a rendered channel is told from a cleared one. `refuseChanges` makes it accept only its default layout (the shape
 *  of Maschine 2's Audio Unit). */
class FakeMultiOut : public juce::AudioPluginInstance {
public:
    static BusesProperties makeBuses(int ins, const std::vector<OutBus>& outs) {
        BusesProperties props;
        if (ins > 0)
            props = props.withInput("Input", juce::AudioChannelSet::discreteChannels(ins), true);
        for (const auto& bus : outs)
            props = props.withOutput(bus.name,
                                     bus.channels == 2 ? juce::AudioChannelSet::stereo()
                                                       : juce::AudioChannelSet::discreteChannels(bus.channels),
                                     bus.onByDefault);
        return props;
    }

    FakeMultiOut(int ins, std::vector<OutBus> outs, bool refuseChanges = false)
        : juce::AudioPluginInstance(makeBuses(ins, outs))
        , refuseChanges_(refuseChanges) {
        defaultLayout_ = getBusesLayout();
        addHostedParameter(std::make_unique<StubHostedParameter>("cutoff", "Cutoff", 0.2f));
    }

    void fillInPluginDescription(juce::PluginDescription& d) const override { d = description(); }
    const juce::String getName() const override { return "Fake Multi Out"; }
    bool isBusesLayoutSupported(const BusesLayout& layout) const override {
        return !refuseChanges_ || layout == defaultLayout_;
    }
    void prepareToPlay(double, int) override {}
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override {
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            juce::FloatVectorOperations::fill(buffer.getWritePointer(channel), 0.1f * (float)(channel + 1),
                                              buffer.getNumSamples());
    }
    double getTailLengthSeconds() const override { return 0.0; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool hasEditor() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}

private:
    bool refuseChanges_ = false;
    BusesLayout defaultLayout_;
};

class FakeBackend : public HostedPluginBackend {
public:
    using Factory = std::function<std::unique_ptr<juce::AudioPluginInstance>()>;
    explicit FakeBackend(Factory factory)
        : factory_(std::move(factory)) {}
    using HostedPluginBackend::createInstanceAsync;

    void createInstanceAsync(const juce::PluginDescription&, double, int, InstanceCallback callback) override {
        auto instance = std::make_shared<std::unique_ptr<juce::AudioPluginInstance>>(factory_());
        auto shared = std::make_shared<InstanceCallback>(std::move(callback));
        juce::MessageManager::callAsync([instance, shared] { (*shared)(std::move(*instance), juce::String()); });
    }
    bool resolveIdentity(const synth::PluginIdentity& identity, juce::PluginDescription& out) const override {
        if (!identity.isValid())
            return false;
        out = description(identity.name);
        return true;
    }

private:
    Factory factory_;
};

/** Main stereo + three aux stereo buses (off by default), named like a drum sampler's. */
std::unique_ptr<juce::AudioPluginInstance> samplerWithAux() {
    return std::make_unique<FakeMultiOut>(
        0, std::vector<OutBus>{{"Main", 2, true}, {"Kick", 2, false}, {"Snare", 2, false}, {"Perc", 2, false}});
}

/** The Maschine 2 AU shape: 8 in, 16 stereo output buses on by default (32 channels), refuses any layout change. */
std::unique_ptr<juce::AudioPluginInstance> maschineShape() {
    std::vector<OutBus> outs;
    for (int i = 0; i < 16; ++i)
        outs.push_back({i == 0 ? "Main" : "Group " + juce::String(i), 2, true});
    return std::make_unique<FakeMultiOut>(8, outs, /*refuseChanges*/ true);
}

struct Loaded {
    explicit Loaded(FakeBackend::Factory factory)
        : backend(std::move(factory)) {
        module.prepareToPlay(kSampleRate, kBlockSize);
        module.loadPlugin(description(), backend);
        pumpUntil([&] { return !module.isLoading(); });
    }
    FakeBackend backend;
    HostedPluginModule module;
};

float rendered(HostedPluginModule& module, int channel) {
    juce::AudioBuffer<float> buffer(HostedPluginModule::kMaxPluginOutputChannels, kBlockSize);
    buffer.clear();
    juce::MidiBuffer midi;
    module.processBlock(buffer, midi);
    return buffer.getSample(channel, 0);
}

void setDual(juce::AudioProcessor& processor, bool dual) {
    auto* param = findParameterByID(&processor, "dualIO");
    ASSERT_NE(param, nullptr);
    param->setValueNotifyingHost(dual ? 1.0f : 0.0f);
}

} // namespace

TEST(HostedPluginOutputsTest, EveryAuxBusIsSwitchedOnAndRendersWhileAFreshLoadShowsTheMainBusOnly) {
    Loaded l(samplerWithAux);
    ASSERT_TRUE(l.module.hasInstance()) << l.module.getStatusMessage();

    EXPECT_EQ(l.module.getActiveInstanceForEditor()->getTotalNumOutputChannels(), 8);
    const auto& buses = l.module.getOutputBuses();
    ASSERT_EQ(buses.size(), 4u);
    for (size_t i = 0; i < buses.size(); ++i) {
        EXPECT_EQ(buses[i].busIndex, (int)i);
        EXPECT_EQ(buses[i].firstRawChannel, (int)i * 2);
        EXPECT_EQ(buses[i].numChannels, 2);
        EXPECT_EQ(buses[i].shown, i == 0);
    }
    EXPECT_EQ(buses[1].name, "Kick");
    EXPECT_EQ(l.module.getVisibleOutputPortCount(), 2);
    EXPECT_EQ(l.module.getOutputPortLabel(0), "Out L");
    EXPECT_EQ(l.module.getOutputPortLabel(1), "Out R");

    for (int channel = 0; channel < 8; ++channel)
        EXPECT_FLOAT_EQ(rendered(l.module, channel), 0.1f * (float)(channel + 1)) << "channel " << channel;
    EXPECT_FLOAT_EQ(rendered(l.module, 8), 0.0f) << "above the instance's outputs is silent";
}

TEST(HostedPluginOutputsTest, ThePluginThatRefusesEveryLayoutChangePublishesWithAll32OutputsRendering) {
    Loaded l(maschineShape);
    ASSERT_TRUE(l.module.hasInstance()) << l.module.getStatusMessage();

    EXPECT_EQ(l.module.getActiveInstanceForEditor()->getTotalNumOutputChannels(), 32);
    EXPECT_EQ(l.module.getActiveInstanceForEditor()->getTotalNumInputChannels(), 8);
    EXPECT_EQ(l.module.getOutputBuses().size(), 16u);
    EXPECT_EQ(l.module.getVisibleOutputPortCount(), 2) << "main bus only on a fresh load";
    EXPECT_TRUE(l.module.getStatusMessage().isEmpty());
    for (int channel : {0, 1, 2, 17, 31})
        EXPECT_FLOAT_EQ(rendered(l.module, channel), 0.1f * (float)(channel + 1)) << "channel " << channel;
}

TEST(HostedPluginOutputsTest, ShowingHidingAndJoiningBusesMovesJacksAndLabelsButNeverRawChannels) {
    Loaded l(samplerWithAux);
    ASSERT_TRUE(l.module.hasInstance());
    int changes = 0;
    l.module.onOutputsChanged = [&] { ++changes; };

    EXPECT_FALSE(l.module.setOutputShown(0, false)) << "the main bus is always shown";
    EXPECT_FALSE(l.module.setOutputShown(7, true)) << "no such bus";
    EXPECT_FALSE(l.module.setOutputJoined(0, true)) << "the main bus follows Dual I/O";
    EXPECT_EQ(changes, 0);

    // Perc (raw 6, 7) before Kick: jacks follow bus order, raw channels do not move.
    ASSERT_TRUE(l.module.setOutputShown(3, true));
    EXPECT_FALSE(l.module.setOutputShown(3, true)) << "no change";
    EXPECT_EQ(changes, 1);
    EXPECT_EQ(l.module.getVisibleOutputPortCount(), 4);
    EXPECT_EQ(l.module.getOutputPortLabel(2), "Perc L");
    EXPECT_EQ(l.module.getOutputPortLabel(3), "Perc R");
    EXPECT_EQ(l.module.getJackTargets(2, false).front().rawHeadChannel, 6);
    EXPECT_EQ(l.module.mapOutputChannel(6).visibleJackIndex, 2);
    EXPECT_TRUE(l.module.mapOutputChannel(6).isPolyGroupHead);

    ASSERT_TRUE(l.module.setOutputShown(1, true));
    EXPECT_EQ(l.module.getVisibleOutputPortCount(), 6);
    EXPECT_EQ(l.module.getOutputPortLabel(2), "Kick L");
    EXPECT_EQ(l.module.getOutputPortLabel(4), "Perc L");
    EXPECT_EQ(l.module.getJackTargets(2, false).front().rawHeadChannel, 2);
    EXPECT_EQ(l.module.getJackTargets(4, false).front().rawHeadChannel, 6) << "Perc's raw channel did not move";
    EXPECT_EQ(l.module.mapOutputChannel(6).visibleJackIndex, 4);

    ASSERT_TRUE(l.module.setOutputJoined(1, true));
    EXPECT_FALSE(l.module.setOutputJoined(1, true));
    EXPECT_EQ(l.module.getVisibleOutputPortCount(), 5);
    EXPECT_EQ(l.module.getOutputPortLabel(2), "Kick");
    const auto head = l.module.mapOutputChannel(2);
    EXPECT_EQ(head.visibleJackIndex, 2);
    EXPECT_TRUE(head.isPolyGroupHead);
    EXPECT_EQ(head.polyVoiceSpan, 2);
    EXPECT_EQ(l.module.mapOutputChannel(3).visibleJackIndex, 2);
    EXPECT_FALSE(l.module.mapOutputChannel(3).isPolyGroupHead);
    EXPECT_EQ(l.module.mapOutputChannel(6).visibleJackIndex, 3);
    EXPECT_EQ(l.module.getOutputPortLabel(3), "Perc L");

    // The hidden Snare bus (raw 4, 5) owns no jack and heads nothing.
    EXPECT_FALSE(l.module.mapOutputChannel(4).isPolyGroupHead);
    EXPECT_FALSE(l.module.mapOutputChannel(5).isPolyGroupHead);

    ASSERT_TRUE(l.module.setOutputShown(1, false));
    EXPECT_EQ(l.module.getVisibleOutputPortCount(), 4);
    EXPECT_FALSE(l.module.mapOutputChannel(2).isPolyGroupHead);
    EXPECT_EQ(changes, 4);

    // The main bus keeps today's labels, and Dual I/O off still joins it.
    setDual(l.module, false);
    EXPECT_EQ(l.module.getVisibleOutputPortCount(), 3);
    EXPECT_EQ(l.module.getOutputPortLabel(0), "Audio");
    EXPECT_EQ(l.module.mapOutputChannel(0).polyVoiceSpan, 2);
    EXPECT_EQ(l.module.getOutputPortLabel(1), "Perc L");
    EXPECT_EQ(l.module.rightAudioLegChannel(), 1);
}

TEST(HostedPluginOutputsTest, ABusWithAGenericNameIsLabelledByItsRawChannels) {
    Loaded l([] {
        return std::make_unique<FakeMultiOut>(
            0, std::vector<OutBus>{{"Main", 2, true}, {"Output #2", 2, false}, {"", 1, false}});
    });
    ASSERT_TRUE(l.module.hasInstance());
    l.module.setOutputShown(1, true);
    l.module.setOutputShown(2, true);
    EXPECT_EQ(l.module.getOutputPortLabel(2), "Out 3");
    EXPECT_EQ(l.module.getOutputPortLabel(3), "Out 4");
    EXPECT_EQ(l.module.getOutputPortLabel(4), "Out 5");
    l.module.setOutputJoined(1, true);
    EXPECT_EQ(l.module.getOutputPortLabel(2), "Out 3/4");
    EXPECT_FALSE(l.module.setOutputJoined(2, true)) << "only a 2-channel bus joins";
}

TEST(HostedPluginOutputsTest, TheShownAndJoinedChoiceSurvivesAStateRoundTrip) {
    Loaded l(samplerWithAux);
    ASSERT_TRUE(l.module.hasInstance());
    l.module.setOutputShown(2, true);
    l.module.setOutputShown(3, true);
    l.module.setOutputJoined(3, true);
    const juce::var state = l.module.getExtraState();
    const auto* outputs = state.getProperty("outputs", {}).getArray();
    ASSERT_NE(outputs, nullptr);
    EXPECT_EQ(outputs->size(), 4);

    HostedPluginBackend::ScopedDefault installed(&l.backend);
    HostedPluginModule restored;
    restored.prepareToPlay(kSampleRate, kBlockSize);
    restored.setExtraState(state);
    ASSERT_TRUE(pumpUntil([&] { return restored.hasInstance(); }));

    const auto& buses = restored.getOutputBuses();
    ASSERT_EQ(buses.size(), 4u);
    EXPECT_TRUE(buses[0].shown);
    EXPECT_FALSE(buses[1].shown);
    EXPECT_TRUE(buses[2].shown);
    EXPECT_FALSE(buses[2].joined);
    EXPECT_TRUE(buses[3].shown);
    EXPECT_TRUE(buses[3].joined);
    EXPECT_EQ(restored.getVisibleOutputPortCount(), 5);
}

TEST(HostedPluginOutputsTest, AProjectSavedBeforeOutputsWereChosenShowsTheBusesTheInstanceHadOnByDefault) {
    // Main + bus 1 on by default, bus 2 off: before this existed the raw channels were exactly those four.
    FakeBackend backend([] {
        return std::make_unique<FakeMultiOut>(
            0, std::vector<OutBus>{{"Main", 2, true}, {"Aux 1", 2, true}, {"Aux 2", 2, false}});
    });
    HostedPluginBackend::ScopedDefault installed(&backend);

    {
        Loaded fresh([] {
            return std::make_unique<FakeMultiOut>(
                0, std::vector<OutBus>{{"Main", 2, true}, {"Aux 1", 2, true}, {"Aux 2", 2, false}});
        });
        ASSERT_TRUE(fresh.module.hasInstance());
        EXPECT_EQ(fresh.module.getVisibleOutputPortCount(), 2) << "a fresh load shows the main bus only";
    }

    juce::DynamicObject::Ptr object = new juce::DynamicObject();
    object->setProperty("pluginFormat", "VST3");
    object->setProperty("pluginName", "Fake Multi Out");
    object->setProperty("pluginUid", 0x4d554c54);
    HostedPluginModule restored;
    restored.prepareToPlay(kSampleRate, kBlockSize);
    restored.setExtraState(juce::var(object.get())); // no "outputs" key
    ASSERT_TRUE(pumpUntil([&] { return restored.hasInstance(); }));

    EXPECT_EQ(restored.getVisibleOutputPortCount(), 4) << "every jack the old project had";
    EXPECT_TRUE(restored.getOutputBuses()[1].shown);
    EXPECT_FALSE(restored.getOutputBuses()[1].joined);
    EXPECT_EQ(restored.getTotalNumOutputChannels(), HostedPluginModule::kMaxPluginOutputChannels);
}

TEST(HostedPluginOutputsTest, AnOldProjectWhosePluginWasWiderThan16ChannelsReopensWithTheMainBusOnly) {
    // Before outputs were chosen, a plugin on by default with more than 16 outputs (Maschine 2's 32) was cut to its
    // main bus or refused, so an old project never had its aux jacks: it must not reopen with 32 of them.
    FakeBackend backend(maschineShape);
    HostedPluginBackend::ScopedDefault installed(&backend);

    juce::DynamicObject::Ptr object = new juce::DynamicObject();
    object->setProperty("pluginFormat", "VST3");
    object->setProperty("pluginName", "Fake Multi Out");
    object->setProperty("pluginUid", 0x4d554c54);
    HostedPluginModule restored;
    restored.prepareToPlay(kSampleRate, kBlockSize);
    restored.setExtraState(juce::var(object.get())); // no "outputs" key
    ASSERT_TRUE(pumpUntil([&] { return restored.hasInstance(); })) << restored.getStatusMessage();

    EXPECT_EQ(restored.getVisibleOutputPortCount(), 2) << "main bus only";
    EXPECT_EQ(restored.getActiveInstanceForEditor()->getTotalNumOutputChannels(), 32) << "every output still renders";
}

TEST(HostedPluginOutputsTest, ModulationCvOnChannelsAbove16StillReachesTheParameterWith32Outputs) {
    // Regression test for FRO753: the output-only clear used to wipe the CV channels 16..31 before they were read.
    Loaded l(maschineShape);
    ASSERT_TRUE(l.module.hasInstance());
    const int channel = l.module.bindModSlot("cutoff");
    ASSERT_EQ(channel, HostedPluginModule::kModChannelBase);

    juce::AudioBuffer<float> buffer(HostedPluginModule::kMaxPluginInputChannels + HostedPluginModule::kMaxModSlots,
                                    kBlockSize);
    buffer.clear();
    juce::FloatVectorOperations::fill(buffer.getWritePointer(channel), 0.3f, kBlockSize);
    juce::MidiBuffer midi;
    l.module.processBlock(buffer, midi);

    EXPECT_NEAR(l.module.findInstanceParameter("cutoff")->getValue(), 0.5f, 1e-5f);
}

TEST(HostedPluginOutputsTest, AMainBusWiderThan64ChannelsIsRefusedWithTheLimitInTheMessage) {
    Loaded l([] { return std::make_unique<FakeMultiOut>(0, std::vector<OutBus>{{"Main", 66, true}}); });
    EXPECT_FALSE(l.module.hasInstance());
    EXPECT_TRUE(l.module.getStatusMessage().contains("64 out")) << l.module.getStatusMessage();
    EXPECT_TRUE(l.module.getStatusMessage().contains("66"));
}

TEST(HostedPluginOutputsTest, HidingAnOutputThroughTheCardDropsTheCablesLeftOnIt) {
    FakeBackend backend(samplerWithAux);
    HostedPluginBackend::ScopedDefault installed(&backend);
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);
    auto& graph = engine.getGraph();

    auto* hosted = new HostedPluginModule();
    auto hostedNode = graph.addNode(std::unique_ptr<juce::AudioProcessor>(hosted));
    hosted->loadPlugin(description(), backend);
    ASSERT_TRUE(pumpUntil([&] { return hosted->hasInstance(); }));
    auto delayNode = graph.addNode(std::make_unique<DelayModule>());
    ASSERT_TRUE(graph.addConnection({{hostedNode->nodeID, 0}, {delayNode->nodeID, 0}}));
    ASSERT_TRUE(hosted->setOutputShown(2, true)); // before a card exists: nothing to clean up yet
    ASSERT_TRUE(graph.addConnection({{hostedNode->nodeID, 4}, {delayNode->nodeID, 1}}));
    editor.updateComponents();
    ASSERT_NE(std::find_if(editor.getModuleComponents().begin(), editor.getModuleComponents().end(),
                           [&](auto* c) { return c->getModule() == hosted; }),
              editor.getModuleComponents().end());

    ASSERT_TRUE(hosted->setOutputShown(2, false));

    for (const auto& c : graph.getConnections())
        if (c.source.nodeID == hostedNode->nodeID && !c.source.isMIDI())
            EXPECT_LT(c.source.channelIndex, 2) << "a cable was left on the hidden Snare output";
    EXPECT_TRUE(graph.isConnected({{hostedNode->nodeID, 0}, {delayNode->nodeID, 0}})) << "the main bus's cable stays";
}
