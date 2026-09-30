// Concern: a node's "cardView" key -- the optional card panels a card has open -- in the patch JSON: emitted only
// when something is open, restored on the trusted path only, and never a reason to reject anything.
#include "AI/AIStateMapper/AIStateMapper.h"
#include "Modules/CardViewState.h"
#include "Modules/FilterModule.h"
#include "Modules/OscillatorModule.h"
#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>

using synth::AIStateMapper;
using synth::CardViewState;

namespace {

// The "nodes" entry of graphToJSON's output for the graph's first node.
juce::DynamicObject* firstNode(const juce::var& json) {
    auto* nodes = json.getProperty("nodes", {}).getArray();
    return nodes != nullptr && !nodes->isEmpty() ? nodes->getReference(0).getDynamicObject() : nullptr;
}

} // namespace

TEST(CardViewStateTest, DefaultSavesNothing) {
    EXPECT_TRUE(CardViewState{}.isDefault());
    EXPECT_TRUE(CardViewState{}.toVar().isVoid());
}

TEST(CardViewStateTest, RoundTripsEveryCombination) {
    for (int bits = 0; bits < 8; ++bits) {
        for (const auto spectrum : {std::optional<bool>(), std::optional<bool>(false), std::optional<bool>(true)}) {
            CardViewState state;
            state.showScope = (bits & 1) != 0;
            state.showResponse = (bits & 2) != 0;
            state.showSpectrum = spectrum;
            const auto back = CardViewState::fromVar(juce::JSON::parse(juce::JSON::toString(state.toVar())));
            EXPECT_EQ(back.showScope, state.showScope);
            EXPECT_EQ(back.showResponse, state.showResponse);
            EXPECT_EQ(back.showSpectrum, state.showSpectrum);
        }
    }
}

TEST(CardViewStateTest, MalformedValuesReadAsDefaults) {
    EXPECT_TRUE(CardViewState::fromVar(juce::var()).isDefault());
    EXPECT_TRUE(CardViewState::fromVar(juce::var("open")).isDefault());
    EXPECT_TRUE(
        CardViewState::fromVar(juce::JSON::parse(R"({"scope":"yes","response":1,"spectrum":"on"})")).isDefault());
}

TEST(CardViewStateTest, GraphJsonCarriesTheKeyOnlyForACardWithAnOpenPanel) {
    juce::AudioProcessorGraph graph;
    auto node = graph.addNode(std::make_unique<OscillatorModule>());
    const auto untouched = AIStateMapper::graphToJSON(graph);
    ASSERT_NE(firstNode(untouched), nullptr);
    EXPECT_FALSE(firstNode(untouched)->hasProperty("cardView"));

    CardViewState open;
    open.showScope = true;
    dynamic_cast<OscillatorModule*>(node->getProcessor())->setCardViewState(open);
    const auto opened = AIStateMapper::graphToJSON(graph);
    auto* obj = firstNode(opened);
    ASSERT_NE(obj, nullptr);
    ASSERT_TRUE(obj->hasProperty("cardView"));
    EXPECT_TRUE(CardViewState::fromVar(obj->getProperty("cardView")).showScope);
}

TEST(CardViewStateTest, TrustedApplyRestoresItAndUntrustedApplyIgnoresIt) {
    juce::AudioProcessorGraph source;
    auto srcNode = source.addNode(std::make_unique<FilterModule>());
    CardViewState open;
    open.showResponse = true;
    open.showSpectrum = true;
    dynamic_cast<FilterModule*>(srcNode->getProcessor())->setCardViewState(open);
    const auto json = AIStateMapper::graphToJSON(source);

    auto stateOf = [](juce::AudioProcessorGraph& g) {
        return dynamic_cast<FilterModule*>(g.getNodes()[0]->getProcessor())->getCardViewState();
    };

    juce::AudioProcessorGraph trusted;
    ASSERT_TRUE(AIStateMapper::applyJSONToGraph(json, trusted, /*clearExisting=*/true, /*trusted=*/true));
    EXPECT_TRUE(stateOf(trusted).showResponse);
    EXPECT_EQ(stateOf(trusted).showSpectrum, std::optional<bool>(true));

    juce::AudioProcessorGraph untrusted;
    ASSERT_TRUE(AIStateMapper::applyJSONToGraph(json, untrusted, /*clearExisting=*/true, /*trusted=*/false));
    EXPECT_TRUE(stateOf(untrusted).isDefault()) << "a patch suggestion must not open panels on the user's cards";
}
