// GraphSnapshotCache (Source/AudioEngine/GraphSnapshotCache.h): the undo history's graph snapshots reuse each unchanged
// node's object from the last capture, so the oracle here is graphToJSON itself -- after every kind of edit a capture
// must write exactly graphToJSON's text -- plus which node objects a capture rebuilt and which it shared.
#include "AI/AIStateMapper/AIStateMapper.h"
#include "AudioEngine/GraphSnapshotCache.h"
#include "Modules/ADSRModule.h"
#include "Modules/AttenuverterModule.h"
#include "Modules/CardLayout.h"
#include "Modules/FilterModule.h"
#include "Modules/ModuleBase.h"
#include "Modules/OscillatorModule.h"
#include <gtest/gtest.h>
#include <limits>

namespace {

using NodeID = juce::AudioProcessorGraph::NodeID;

/** ModuleBase stand-in whose non-parameter state the test sets directly. */
class StateProbe : public ModuleBase {
public:
    StateProbe()
        : ModuleBase("Oscillator", 1, 1) {}
    void prepareToPlay(double, int) override {}
    void processModuleBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
    ModuleType getModuleType() const override { return ModuleType::Oscillator; }
    juce::var getExtraState() const override {
        juce::DynamicObject::Ptr state = new juce::DynamicObject();
        state->setProperty("token", token);
        return juce::var(state.get());
    }
    void setExtraState(const juce::var&) override {}
    juce::String token{"a"};
};

struct Graph {
    juce::AudioProcessorGraph graph;
    synth::GraphSnapshotCache cache;
    std::vector<NodeID> ids;

    explicit Graph(int pairs) {
        for (int i = 0; i < pairs; ++i) {
            const auto osc = add(std::make_unique<OscillatorModule>(), 100 * i, 0);
            const auto filter = add(std::make_unique<FilterModule>(), 100 * i, 300);
            graph.addConnection({{osc, 0}, {filter, 0}});
        }
    }
    NodeID add(std::unique_ptr<juce::AudioProcessor> p, int x, int y) {
        auto node = graph.addNode(std::move(p));
        node->properties.set("x", x);
        node->properties.set("y", y);
        ids.push_back(node->nodeID);
        return node->nodeID;
    }
    juce::AudioProcessorGraph::Node& node(NodeID id) { return *graph.getNodeForId(id); }
    juce::RangedAudioParameter& param(NodeID id, int index = 0) {
        return *dynamic_cast<juce::RangedAudioParameter*>(node(id).getProcessor()->getParameters()[index]);
    }
    // A capture, checked against graphToJSON's text.
    juce::var capture() {
        const auto snapshot = cache.capture(graph);
        EXPECT_EQ(juce::JSON::toString(snapshot), juce::JSON::toString(synth::AIStateMapper::graphToJSON(graph)));
        return snapshot;
    }
};

const juce::Array<juce::var>& nodesOf(const juce::var& s) { return *s["nodes"].getArray(); }

// How many node objects `b` shares with `a` (by identity).
int shared(const juce::var& a, const juce::var& b) {
    int n = 0;
    for (const auto& x : nodesOf(a))
        for (const auto& y : nodesOf(b))
            if (x.getDynamicObject() == y.getDynamicObject())
                ++n;
    return n;
}

} // namespace

TEST(GraphSnapshotCache, EveryKindOfEditCapturesGraphToJsonsText) {
    Graph g(3);
    g.capture();
    g.param(g.ids[0]).setValueNotifyingHost(0.9f);
    g.capture();
    g.node(g.ids[1]).properties.set("x", 4242); // a drag
    g.capture();
    g.node(g.ids[2]).properties.set("displayName", "Lead"); // a rename
    g.capture();
    juce::DynamicObject::Ptr layout = new juce::DynamicObject();
    layout->setProperty("rows", 2);
    g.node(g.ids[3]).properties.set(synth::kCardLayoutNodeProperty, juce::var(layout.get()));
    g.capture();
    layout->setProperty("rows", 3); // the live layout changed in place: still seen
    g.capture();
    auto* view = dynamic_cast<ModuleBase*>(g.node(g.ids[4]).getProcessor());
    synth::CardViewState shown;
    shown.showScope = true;
    view->setCardViewState(shown);
    g.capture();
    const auto probe = g.add(std::make_unique<StateProbe>(), 9, 9); // an add
    g.capture();
    dynamic_cast<StateProbe*>(g.node(probe).getProcessor())->token = "b"; // extra state
    g.capture();
    g.graph.addConnection({{g.ids[0], 0}, {g.ids[3], 0}}); // a cable
    g.capture();
    g.graph.removeConnection({{g.ids[0], 0}, {g.ids[1], 0}});
    g.capture();
    const auto atten = g.add(std::make_unique<AttenuverterModule>(), 0, 0); // a modulation routing
    g.graph.addConnection({{g.ids[2], 0}, {atten, 0}});
    g.graph.addConnection({{atten, 0}, {g.ids[5], 1}});
    g.capture();
    g.param(atten).setValueNotifyingHost(0.25f); // its amount
    g.capture();
    const auto uuid = g.node(g.ids[5]).properties["uuid"].toString(); // replaced by a node of another type
    g.graph.removeNode(g.ids[5]);
    auto adsr = g.graph.addNode(std::make_unique<ADSRModule>(), g.ids[5]);
    adsr->properties.set("uuid", uuid);
    g.capture();
    g.graph.removeNode(probe); // a removal
    g.capture();
    g.param(g.ids[0]).setValueNotifyingHost(std::numeric_limits<float>::quiet_NaN());
    g.capture();
}

TEST(GraphSnapshotCache, ACaptureRebuildsOnlyTheNodesThatChanged) {
    for (int pairs : {2, 40}) { // the work per edit must not depend on the project's size
        Graph g(pairs);
        const auto first = g.capture();
        EXPECT_EQ(g.cache.lastRebuiltNodes(), 2 * pairs);

        const auto again = g.capture();
        EXPECT_EQ(g.cache.lastRebuiltNodes(), 0) << "nothing changed, nothing is written";
        EXPECT_EQ(shared(first, again), 2 * pairs);
        EXPECT_EQ(first["connections"].getArray(), again["connections"].getArray()) << "same cables, same list";

        g.param(g.ids[1]).setValueNotifyingHost(0.1f);
        const auto knob = g.capture();
        EXPECT_EQ(g.cache.lastRebuiltNodes(), 1);
        EXPECT_EQ(shared(again, knob), 2 * pairs - 1);

        g.add(std::make_unique<OscillatorModule>(), 1, 1);
        g.add(std::make_unique<FilterModule>(), 1, 2);
        const auto added = g.capture();
        EXPECT_EQ(g.cache.lastRebuiltNodes(), 2) << "a duplicate writes the copy, not the project";
        EXPECT_EQ(shared(knob, added), 2 * pairs);
    }
}

TEST(GraphSnapshotCache, AnotherGraphStartsAfresh) {
    Graph a(2), b(2);
    synth::GraphSnapshotCache cache;
    cache.capture(a.graph);
    const auto fromB = cache.capture(b.graph);
    EXPECT_EQ(cache.lastRebuiltNodes(), 4);
    EXPECT_EQ(juce::JSON::toString(fromB), juce::JSON::toString(synth::AIStateMapper::graphToJSON(b.graph)));
}

TEST(GraphSnapshotCache, SameJsonAgreesWithTheText) {
    auto obj = [](std::initializer_list<std::pair<const char*, juce::var>> props) {
        juce::DynamicObject::Ptr o = new juce::DynamicObject();
        for (const auto& [k, v] : props)
            o->setProperty(k, v);
        return juce::var(o.get());
    };
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const std::vector<std::pair<juce::var, juce::var>> pairs = {
        {1, (juce::int64)1},
        {1, 1.0},
        {1, true},
        {juce::var(), nan},
        {juce::var(), juce::var::undefined()},
        {"1", 1},
        {"a", "a"},
        {0.5, 0.5},
        {obj({{"a", 1}, {"b", 2}}), obj({{"b", 2}, {"a", 1}})},
        {obj({{"a", 1}}), obj({{"a", 1}})},
        {obj({{"a", 1}}), obj({{"a", 1}, {"b", juce::var()}})},
        {juce::Array<juce::var>{1, 2}, juce::Array<juce::var>{1, 2}},
        {juce::Array<juce::var>{1, 2}, juce::Array<juce::var>{2, 1}},
    };
    for (const auto& [a, b] : pairs)
        EXPECT_EQ(synth::sameJson(a, b), juce::JSON::toString(a) == juce::JSON::toString(b))
            << juce::JSON::toString(a) << " vs " << juce::JSON::toString(b);
}

TEST(GraphSnapshotCache, SizesOfTheLatestCaptureAreKnown) {
    Graph g(3);
    const auto s = g.capture();
    for (const auto& n : nodesOf(s))
        EXPECT_EQ(g.cache.knownSize(n.getDynamicObject()), synth::estimateJsonSize(n));
    EXPECT_EQ(g.cache.knownSize(s["connections"].getArray()), synth::estimateJsonSize(s["connections"]));
    const auto counted = synth::estimateJsonSize(s, [&g](const void* id) { return g.cache.knownSize(id); });
    EXPECT_EQ(counted, synth::estimateJsonSize(s)) << "a known size is the size counting would find";
    EXPECT_GT(synth::estimateJsonSize(g.capture()), 0);
}
