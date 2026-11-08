// DrumKitPadCableTests.cpp
// A cable dropped on a pad knob binds to that pad's parameter through a modulation slot (synth::ModSlotHost): it is
// read when its pad is struck and never written into a parameter, 32 fit, a slot lives only while a cable reaches it,
// the kit saves them only when used, and the AI never sees them.
#include "AI/AIStateMapper/AIStateMapper.h"
#include "Modules/DrumKit/DrumKitModule.h"
#include <algorithm>
#include <cmath>
#include <gtest/gtest.h>

namespace padcable_test {

constexpr double kRate = 44100.0;
constexpr int kBlock = 256;
constexpr int kKick = 36, kSnare = 38;
constexpr int kBase = DrumKitModule::kPadModSlotBase;

std::unique_ptr<DrumKitModule> makeKit() {
    auto kit = std::make_unique<DrumKitModule>();
    kit->prepareToPlay(kRate, kBlock);
    return kit;
}

// One hit of `note` with a constant `cv` on raw `channel` (none when channel < 0).
std::vector<float> hit(DrumKitModule& kit, int note, int channel, float cv) {
    std::vector<float> out;
    for (int start = 0; start < (int)(0.5 * kRate); start += kBlock) {
        juce::AudioBuffer<float> block(kit.getTotalNumInputChannels(), kBlock);
        block.clear();
        if (channel >= 0)
            juce::FloatVectorOperations::fill(block.getWritePointer(channel), cv, kBlock);
        juce::MidiBuffer midi;
        if (start == 0)
            midi.addEvent(juce::MidiMessage::noteOn(10, note, (juce::uint8)110), 100);
        kit.processBlock(block, midi);
        out.insert(out.end(), block.getReadPointer(0), block.getReadPointer(0) + kBlock);
    }
    return out;
}

float peak(const std::vector<float>& s) {
    float p = 0.0f;
    for (float v : s)
        p = std::max(p, std::fabs(v));
    return p;
}

juce::String padId(int pad, const char* field) { return "pad" + juce::String(pad) + field; }

} // namespace padcable_test

using namespace padcable_test;

TEST(DrumKitPadCable, ASlotOnSnareLevelQuietsTheSnareAndLeavesTheKickAndEveryKnobAlone) {
    auto kit = makeKit();
    const auto snarePlain = hit(*kit, kSnare, -1, 0.0f);
    const auto kickPlain = hit(*makeKit(), kKick, -1, 0.0f);
    std::vector<float> values;
    for (auto* p : kit->getParameters())
        values.push_back(p->getValue());

    const int channel = kit->bindModSlot("pad2Level");
    ASSERT_EQ(channel, kBase);
    const auto snareCabled = hit(*kit, kSnare, channel, -0.5f);
    auto kickKit = makeKit(); // a second hit on one kit starts from the first one's state, so compare fresh kits
    ASSERT_EQ(kickKit->bindModSlot("pad2Level"), kBase);
    const auto kickCabled = hit(*kickKit, kKick, kBase, -0.5f);

    EXPECT_LT(peak(snareCabled), peak(snarePlain) * 0.6f);
    EXPECT_GT(peak(snareCabled), 0.0f);
    EXPECT_EQ(kickCabled, kickPlain) << "a kick hit does not hear the snare's cable";
    for (int i = 0; i < kit->getParameters().size(); ++i)
        EXPECT_EQ(kit->getParameters()[i]->getValue(), values[(size_t)i]) << kit->getParameters()[i]->getName(64);
}

TEST(DrumKitPadCable, ASlotNeverMovesTheGroupKnobOrAnotherPadsKnob) {
    auto kit = makeKit();
    ASSERT_EQ(kit->bindModSlot("pad2Tune"), kBase);
    const auto plain = hit(*kit, kSnare, -1, 0.0f);
    const auto tuned = hit(*kit, kSnare, kBase, 0.5f);
    EXPECT_NE(plain, tuned);
    EXPECT_EQ(kit->padParameter(1, DrumKitModule::PadParam::Tune)->get(), 0.0f);
    EXPECT_EQ(findParameterByID(kit.get(), "snareTune")->getValue(),
              findParameterByID(kit.get(), "snareTune")->getDefaultValue());
}

TEST(DrumKitPadCable, ThirtyTwoSlotsBindAndTheThirtyThirdIsRefused) {
    auto kit = makeKit();
    int made = 0;
    for (int pad = 1; pad <= 8; ++pad)
        for (const char* field : {"Level", "Tune", "Decay", "Start"}) {
            ASSERT_TRUE(kit->canModulateParam(padId(pad, field)));
            EXPECT_EQ(kit->bindModSlot(padId(pad, field)), kBase + made++);
        }
    ASSERT_EQ(made, DrumKitModule::kMaxPadModSlots);
    EXPECT_EQ(kit->maxModSlots(), 32);
    EXPECT_FALSE(kit->canModulateParam("pad9Level"));
    EXPECT_EQ(kit->bindModSlot("pad9Level"), -1);
    EXPECT_EQ(kit->bindModSlot("pad1Level"), kBase) << "a pad that is bound already keeps its channel";
    EXPECT_TRUE(kit->canModulateParam("pad1Level"));
}

TEST(DrumKitPadCable, OnlyPadKnobsBind) {
    auto kit = makeKit();
    for (const char* id : {"kickLevel", "level", "pad33Level", "pad0Level", "pad1Pitch", "", "pad"}) {
        EXPECT_FALSE(kit->canModulateParam(id)) << id;
        EXPECT_EQ(kit->bindModSlot(id), -1) << id;
    }
}

TEST(DrumKitPadCable, ReleaseFreesOnlyTheSlotsNoCableReaches) {
    auto kit = makeKit();
    kit->bindModSlot("pad1Level");
    kit->bindModSlot("pad2Level");
    kit->releaseUnconnectedModSlots([](int channel) { return channel == kBase + 1; });
    EXPECT_EQ(kit->modChannelForParam("pad1Level"), -1);
    EXPECT_EQ(kit->modChannelForParam("pad2Level"), kBase + 1);
    EXPECT_EQ(kit->bindModSlot("pad3Level"), kBase) << "the freed slot is the next one claimed";

    // A freed slot stops reading its channel.
    kit->releaseUnconnectedModSlots([](int) { return false; });
    EXPECT_EQ(hit(*kit, kSnare, kBase + 1, -0.5f), hit(*makeKit(), kSnare, -1, 0.0f));
}

TEST(DrumKitPadCable, AnUntouchedKitSavesNothingAndABoundSlotRoundTrips) {
    auto kit = makeKit();
    EXPECT_TRUE(kit->getExtraState().isVoid());
    EXPECT_EQ(juce::JSON::toString(kit->getExtraState()), juce::JSON::toString(DrumKitModule().getExtraState()));

    kit->bindModSlot("pad2Level");
    kit->bindModSlot("pad5Decay");
    const auto saved = kit->getExtraState();
    ASSERT_FALSE(saved.isVoid());
    ASSERT_NE(saved.getDynamicObject(), nullptr);
    EXPECT_TRUE(saved.getDynamicObject()->hasProperty("modSlots"));

    auto restored = makeKit();
    restored->setExtraState(saved);
    EXPECT_EQ(restored->modChannelForParam("pad2Level"), kBase);
    EXPECT_EQ(restored->modChannelForParam("pad5Decay"), kBase + 1);
    EXPECT_EQ(juce::JSON::toString(restored->getExtraState()), juce::JSON::toString(saved));
    EXPECT_LT(peak(hit(*restored, kSnare, kBase, -0.5f)), peak(hit(*makeKit(), kSnare, -1, 0.0f)) * 0.6f);

    restored->setExtraState({});
    EXPECT_EQ(restored->modChannelForParam("pad2Level"), -1) << "a state with no slots clears them";
    kit->releaseUnconnectedModSlots([](int) { return false; });
    EXPECT_TRUE(kit->getExtraState().isVoid()) << "with every cable gone the kit saves nothing again";
}

TEST(DrumKitPadCable, ASlotIsAKnobOnlyTargetNamedAfterItsPadAndNeverAutoPromoted) {
    auto kit = makeKit();
    const auto before = kit->getModulationTargets().size();
    EXPECT_TRUE(kit->isReservedInputChannel(kBase));
    kit->bindModSlot("pad2Level");
    EXPECT_FALSE(kit->isReservedInputChannel(kBase));
    EXPECT_TRUE(kit->isReservedInputChannel(kBase + 1));

    const auto targets = kit->getModulationTargets();
    ASSERT_EQ(targets.size(), before + 1);
    EXPECT_EQ(targets.back().channelIndex, kBase);
    EXPECT_EQ(targets.back().paramId, "pad2Level");
    EXPECT_EQ(targets.back().name, "Pad 2 level");
    EXPECT_TRUE(targets.back().knobOnly);
    EXPECT_EQ(kit->getInputPortLabel(kBase), "Pad 2 level");
    EXPECT_EQ(kit->modulationChannelForParam("pad2Level"), kBase);

    EXPECT_FALSE(kit->isAutoPromotableModTarget(kBase));
    EXPECT_TRUE(kit->isAutoPromotableModTarget(1)) << "a group knob still is";
    EXPECT_EQ(kit->mapInputChannel(kBase).visibleJackIndex, kBase);
    EXPECT_TRUE(kit->mapInputChannel(kBase).isPolyGroupHead);
}

TEST(DrumKitPadCable, TheAiNeverSeesAPadCable) {
    const auto catalog = synth::AIStateMapper::getModuleCatalogJson();
    const auto schema = synth::AIStateMapper::getModuleSchema();
    EXPECT_FALSE(catalog.contains("pad2Level"));
    EXPECT_FALSE(schema.contains("pad2Level"));
    EXPECT_FALSE(schema.contains("Pad 2 level"));

    juce::AudioProcessorGraph graph;
    auto kitOwned = makeKit();
    auto* kit = kitOwned.get();
    const auto kitNode = graph.addNode(std::move(kitOwned));
    const auto lfoNode = graph.addNode(synth::AIStateMapper::createModule("LFO"));
    ASSERT_EQ(kit->bindModSlot("pad2Level"), kBase);

    // The model cannot name the slot's jack, by parameter or by port.
    const auto modulation = [&](const juce::String& key, const juce::var& value) {
        juce::DynamicObject::Ptr entry = new juce::DynamicObject();
        entry->setProperty("source", (int)lfoNode->nodeID.uid);
        entry->setProperty("dest", (int)kitNode->nodeID.uid);
        entry->setProperty(key, value);
        juce::DynamicObject::Ptr root = new juce::DynamicObject();
        root->setProperty("nodes", juce::Array<juce::var>());
        root->setProperty("modulations", juce::Array<juce::var>({juce::var(entry.get())}));
        return synth::AIStateMapper::validatePatch(juce::var(root.get()), graph, /*clearExisting=*/false,
                                                   /*trusted=*/false);
    };
    EXPECT_FALSE(modulation("destParam", "pad2Level").ok);
    EXPECT_TRUE(modulation("destParam", "kickLevel").ok);
}

TEST(DrumKitPadCable, AProjectWithAPadCableKeepsTheSlotAndTheCableThroughTheTrustedPath) {
    juce::AudioProcessorGraph graph;
    auto kitOwned = makeKit();
    auto* kit = kitOwned.get();
    const auto kitNode = graph.addNode(std::move(kitOwned));
    const auto lfoNode = graph.addNode(synth::AIStateMapper::createModule("LFO"));
    const auto attenNode = graph.addNode(synth::AIStateMapper::createModule("Attenuverter"));
    ASSERT_EQ(kit->bindModSlot("pad2Level"), kBase);
    ASSERT_TRUE(graph.addConnection({{lfoNode->nodeID, 0}, {attenNode->nodeID, 0}}));
    ASSERT_TRUE(graph.addConnection({{attenNode->nodeID, 0}, {kitNode->nodeID, kBase}}));

    const auto json = synth::AIStateMapper::graphToJSON(graph);
    juce::AudioProcessorGraph reloaded;
    ASSERT_TRUE(synth::AIStateMapper::applyJSONToGraph(json, reloaded, /*clearExisting=*/true, /*trusted=*/true));

    DrumKitModule* kit2 = nullptr;
    juce::AudioProcessorGraph::NodeID kitId;
    for (auto* node : reloaded.getNodes())
        if (auto* k = dynamic_cast<DrumKitModule*>(node->getProcessor())) {
            kit2 = k;
            kitId = node->nodeID;
        }
    ASSERT_NE(kit2, nullptr);
    EXPECT_EQ(kit2->modChannelForParam("pad2Level"), kBase);
    const auto connections = reloaded.getConnections();
    EXPECT_TRUE(std::any_of(connections.begin(), connections.end(), [&](const auto& c) {
        return c.destination.nodeID == kitId && c.destination.channelIndex == kBase;
    }));
}
