// Unit coverage for Source/AI/PatchDiff.h: the human-readable diff the chat UI's PatchCard shows
// as its default (pre-Apply) view. Two things matter structurally, both exercised below:
//
//  1. Node identity is the graphToJSON "uuid" field, not the integer "id" (merge-mode's scratch
//     rebuild only preserves "id" for nodes the trusted replay recreates — see
//     AIIntegrationService::replayLiveGraphTrusted()).
//  2. computeDiff() must be fed two graphToJSON() SNAPSHOTS, never the raw patch JSON — that is
//     the only way to see merge mode's auto-wiring of new nodes, the untrusted-apply [0,1] rescale
//     heuristic, and (via AIStateMapper::graphToJSON already collapsing attenuverter chains into
//     "modulations") a clean "+ mod LFO -> Filter Cutoff" instead of raw Attenuverter node/wire
//     noise. MergeModeAutoWireAppearsInDiff and UntrustedRescale... below are the regression tests
//     proving snapshot-diffing catches what raw-patch-diffing would miss.

#include "AI/AIIntegrationService.h"
#include "AI/AIStateMapper.h"
#include "AI/PatchDiff.h"
#include <algorithm>
#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>

namespace synth {
namespace {

using Kind = PatchChange::Kind;

const PatchChange* firstOfKind(const std::vector<PatchChange>& changes, Kind kind) {
    auto it = std::find_if(changes.begin(), changes.end(), [&](const PatchChange& c) { return c.kind == kind; });
    return it == changes.end() ? nullptr : &*it;
}

int countOfKind(const std::vector<PatchChange>& changes, Kind kind) {
    return (int)std::count_if(changes.begin(), changes.end(), [&](const PatchChange& c) { return c.kind == kind; });
}

void applyStep(juce::AudioProcessorGraph& graph, const char* json, bool clearExisting) {
    juce::var patch = juce::JSON::parse(juce::String(json));
    ASSERT_TRUE(AIStateMapper::applyJSONToGraph(patch, graph, clearExisting, /*trusted=*/true));
}

// graphToJSON() of a graph built (trusted) from `json` — real ids/uuids, not hand-typed ones.
juce::var snapshotOf(const char* json) {
    juce::AudioProcessorGraph graph;
    applyStep(graph, json, /*clearExisting=*/true);
    return AIStateMapper::graphToJSON(graph);
}

// Builds "before" from `beforeJson`, then merges `afterDeltaJson` onto the SAME graph (trusted)
// so nodes the delta doesn't touch keep their id/uuid — the shape a real merge-mode rebuild
// produces, and what makes uuid-keyed identity meaningful across the two returned snapshots.
struct BeforeAfter {
    juce::var before, after;
};

BeforeAfter mergeSnapshot(const char* beforeJson, const char* afterDeltaJson) {
    juce::AudioProcessorGraph graph;
    applyStep(graph, beforeJson, /*clearExisting=*/true);
    juce::var before = AIStateMapper::graphToJSON(graph);
    applyStep(graph, afterDeltaJson, /*clearExisting=*/false);
    juce::var after = AIStateMapper::graphToJSON(graph);
    return {before, after};
}

//==============================================================================
// Pure computeDiff() cases — before/after come from the real AIStateMapper pipeline (or, where
// there is no patch operation to drive a case through it, hand-built graphToJSON-shaped JSON).

TEST(PatchDiffTest, NodeAdded) {
    auto ba = mergeSnapshot(R"({"nodes":[{"id":1,"type":"Oscillator"}],"connections":[]})",
                            R"({"nodes":[{"id":2,"type":"Reverb"}],"connections":[]})");

    auto changes = computeDiff(ba.before, ba.after);
    auto* c = firstOfKind(changes, Kind::NodeAdded);
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(c->nodeType, "Reverb");
    EXPECT_EQ(countOfKind(changes, Kind::NodeAdded), 1);
    EXPECT_EQ(countOfKind(changes, Kind::NodeRemoved), 0);
}

TEST(PatchDiffTest, NodeRemoved) {
    auto ba = mergeSnapshot(R"({"nodes":[{"id":1,"type":"Oscillator"},{"id":2,"type":"Reverb"}],"connections":[]})",
                            R"({"nodes":[],"connections":[],"remove":[2]})");

    auto changes = computeDiff(ba.before, ba.after);
    auto* c = firstOfKind(changes, Kind::NodeRemoved);
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(c->nodeType, "Reverb");
    EXPECT_EQ(countOfKind(changes, Kind::NodeAdded), 0);
}

TEST(PatchDiffTest, ParamChanged) {
    auto ba = mergeSnapshot(R"({"nodes":[{"id":1,"type":"Filter","params":{"cutoff":300.0}}],"connections":[]})",
                            R"({"nodes":[{"id":1,"type":"Filter","params":{"cutoff":800.0}}],"connections":[]})");

    auto changes = computeDiff(ba.before, ba.after);
    auto* c = firstOfKind(changes, Kind::ParamChanged);
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(c->nodeType, "Filter");
    EXPECT_EQ(c->paramId, "cutoff");
    EXPECT_EQ(c->paramName, "Cutoff"); // display name, not the raw paramID
    EXPECT_NEAR((double)c->beforeValue, 300.0, 1.0e-3);
    EXPECT_NEAR((double)c->afterValue, 800.0, 1.0e-3);
    EXPECT_EQ(c->beforeText, "300");
    EXPECT_EQ(c->afterText, "800");
    EXPECT_EQ(countOfKind(changes, Kind::NodeAdded), 0);
    EXPECT_EQ(countOfKind(changes, Kind::NodeRemoved), 0);
}

TEST(PatchDiffTest, ConnectionAdded) {
    auto ba = mergeSnapshot(R"({"nodes":[{"id":1,"type":"Oscillator"},{"id":2,"type":"Filter"}],"connections":[]})",
                            R"({"nodes":[],"connections":[{"src":1,"srcPort":0,"dst":2,"dstPort":0}]})");

    auto changes = computeDiff(ba.before, ba.after);
    auto* c = firstOfKind(changes, Kind::ConnectionAdded);
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(c->srcType, "Oscillator");
    EXPECT_EQ(c->dstType, "Filter");
    EXPECT_EQ(countOfKind(changes, Kind::ConnectionRemoved), 0);
}

TEST(PatchDiffTest, ConnectionRemoved) {
    // There is no "remove a connection without removing a node" patch operation, so this one
    // exercises computeDiff() directly against hand-built graphToJSON-shaped snapshots rather
    // than through applyJSONToGraph — it is a pure function over the JSON, per PatchDiff.h.
    const char* beforeJson = R"({
        "nodes": [
            {"id": 1, "uuid": "osc-1", "type": "Oscillator", "params": {}},
            {"id": 2, "uuid": "filt-1", "type": "Filter", "params": {}}
        ],
        "connections": [{"src": 1, "srcPort": 0, "dst": 2, "dstPort": 0, "isMidi": false}],
        "modulations": []
    })";
    const char* afterJson = R"({
        "nodes": [
            {"id": 1, "uuid": "osc-1", "type": "Oscillator", "params": {}},
            {"id": 2, "uuid": "filt-1", "type": "Filter", "params": {}}
        ],
        "connections": [],
        "modulations": []
    })";

    auto changes = computeDiff(juce::JSON::parse(juce::String(beforeJson)), juce::JSON::parse(juce::String(afterJson)));
    auto* c = firstOfKind(changes, Kind::ConnectionRemoved);
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(c->srcType, "Oscillator");
    EXPECT_EQ(c->dstType, "Filter");
    EXPECT_EQ(countOfKind(changes, Kind::NodeAdded), 0);
    EXPECT_EQ(countOfKind(changes, Kind::NodeRemoved), 0);
}

TEST(PatchDiffTest, ModulationAddedAndSuppressesAttenuverterNoise) {
    juce::AudioProcessorGraph graph;
    applyStep(graph, R"({"nodes":[{"id":1,"type":"LFO"},{"id":2,"type":"Filter"}],"connections":[]})",
              /*clearExisting=*/true);
    juce::var before = AIStateMapper::graphToJSON(graph);

    // Filter's mono "Cutoff" modulation target is channel 1 (FilterModule::getModulationTargets).
    applyStep(graph,
              R"({"nodes":[],"connections":[],
                 "modulations":[{"source":1,"sourcePort":0,"dest":2,"destPort":1,"amount":0.5}]})",
              /*clearExisting=*/false);
    juce::var after = AIStateMapper::graphToJSON(graph);

    auto changes = computeDiff(before, after);

    // The modulation is implemented as a real Attenuverter node + 2 connections — neither must
    // leak into the diff as raw node/connection noise; only the collapsed ModulationAdded should.
    EXPECT_EQ(countOfKind(changes, Kind::NodeAdded), 0);
    EXPECT_EQ(countOfKind(changes, Kind::ConnectionAdded), 0);

    auto* c = firstOfKind(changes, Kind::ModulationAdded);
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(c->srcType, "LFO");
    EXPECT_EQ(c->dstType, "Filter");
    EXPECT_EQ(c->destParamName, "Cutoff");
    EXPECT_NEAR(c->amount, 0.5, 1.0e-3);
}

TEST(PatchDiffTest, ModulationRemoved) {
    juce::AudioProcessorGraph graph;
    applyStep(graph, R"({"nodes":[{"id":1,"type":"LFO"},{"id":2,"type":"Filter"}],"connections":[]})",
              /*clearExisting=*/true);
    applyStep(graph,
              R"({"nodes":[],"connections":[],
                 "modulations":[{"source":1,"sourcePort":0,"dest":2,"destPort":1,"amount":0.5}]})",
              /*clearExisting=*/false);
    juce::var before = AIStateMapper::graphToJSON(graph);

    applyStep(graph,
              R"({"nodes":[],"connections":[],
                 "removeModulations":[{"source":1,"dest":2,"destPort":1}]})",
              /*clearExisting=*/false);
    juce::var after = AIStateMapper::graphToJSON(graph);

    auto changes = computeDiff(before, after);
    auto* c = firstOfKind(changes, Kind::ModulationRemoved);
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(c->srcType, "LFO");
    EXPECT_EQ(c->dstType, "Filter");
    // The attenuverter node's removal must not leak in as a NodeRemoved.
    EXPECT_EQ(countOfKind(changes, Kind::NodeRemoved), 0);
}

TEST(PatchDiffTest, IdenticalSnapshotsProduceEmptyDiff) {
    juce::var snap = snapshotOf(R"({
        "nodes": [
            {"id": 1, "type": "Oscillator"},
            {"id": 2, "type": "Filter", "params": {"cutoff": 333.0}},
            {"id": 3, "type": "Audio Output"}
        ],
        "connections": [
            {"src": 1, "srcPort": 0, "dst": 2, "dstPort": 0},
            {"src": 2, "srcPort": 0, "dst": 3, "dstPort": 0}
        ]
    })");

    EXPECT_TRUE(computeDiff(snap, snap).empty());
}

TEST(PatchDiffTest, DescribeRendersHumanReadableLines) {
    PatchChange added;
    added.kind = Kind::NodeAdded;
    added.nodeType = "Reverb";
    EXPECT_EQ(added.describe(), "+ Reverb");

    PatchChange removed;
    removed.kind = Kind::NodeRemoved;
    removed.nodeType = "Delay";
    EXPECT_EQ(removed.describe(), "- Delay");

    PatchChange paramChanged;
    paramChanged.kind = Kind::ParamChanged;
    paramChanged.nodeType = "Filter";
    paramChanged.paramName = "Cutoff";
    paramChanged.beforeText = "400";
    paramChanged.afterText = "800";
    EXPECT_EQ(paramChanged.describe(), "Filter: Cutoff 400 -> 800");

    PatchChange modAdded;
    modAdded.kind = Kind::ModulationAdded;
    modAdded.srcType = "LFO";
    modAdded.dstType = "Filter";
    modAdded.destParamName = "Cutoff";
    EXPECT_EQ(modAdded.describe(), "+ mod LFO -> Filter Cutoff");
}

//==============================================================================
// Integration-level regression tests: the whole reason to diff snapshots instead of the raw
// patch JSON is that applyJSONToGraph does things the patch itself never states.

class PatchDiffIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        graph = std::make_unique<juce::AudioProcessorGraph>();
        service = std::make_unique<AIIntegrationService>(*graph);
    }

    std::unique_ptr<juce::AudioProcessorGraph> graph;
    std::unique_ptr<AIIntegrationService> service;
};

TEST_F(PatchDiffIntegrationTest, MergeModeAutoWireAppearsInDiff) {
    ASSERT_TRUE(service->applyPatch(R"({"nodes":[{"id":1,"type":"Oscillator"},{"id":2,"type":"Audio Output"}],
                                        "connections":[{"src":1,"srcPort":0,"dst":2,"dstPort":0}]})"));

    // The candidate patch adds a Reverb with NO connection at all. Merge mode's auto-connect
    // affordance (applyJSONToGraph, autoConnectNewNodes) is what wires it to Audio Output — the
    // raw patch JSON never says so, which is exactly why diffing the raw patch would miss it.
    const char* candidate = R"({"mode":"merge","nodes":[{"id":99,"type":"Reverb"}],"connections":[]})";
    ASSERT_FALSE(juce::String(candidate).contains("Audio Output"));

    juce::var before, after;
    EXPECT_TRUE(service->computePatchPreview(candidate, /*mergeMode=*/true, before, after));

    auto changes = computeDiff(before, after);
    auto* nodeAdded = firstOfKind(changes, Kind::NodeAdded);
    ASSERT_NE(nodeAdded, nullptr);
    EXPECT_EQ(nodeAdded->nodeType, "Reverb");

    auto* connAdded = firstOfKind(changes, Kind::ConnectionAdded);
    ASSERT_NE(connAdded, nullptr);
    EXPECT_EQ(connAdded->srcType, "Reverb");
    EXPECT_EQ(connAdded->dstType, "Audio Output");
}

TEST_F(PatchDiffIntegrationTest, UntrustedRescaleShowsLandedValueNotRawPatchValue) {
    ASSERT_TRUE(service->applyPatch(
        R"({"nodes":[{"id":1,"type":"Oscillator"},{"id":2,"type":"Filter"},{"id":3,"type":"Audio Output"}],
            "connections":[{"src":1,"srcPort":0,"dst":2,"dstPort":0},{"src":2,"srcPort":0,"dst":3,"dstPort":0}]})"));

    // Filter cutoff's real range is [20, 20000] (FilterModule.h), linear (no skew). 0.5 is inside
    // [0,1], so AIStateMapper::applyParamsToProcessor's untrusted-apply heuristic treats it as a
    // normalised value the model forgot to denormalize and rescales it to
    // range.convertFrom0to1(0.5) == 10010.0 — not the literal 0.5 the patch states.
    const char* candidate =
        R"({"mode":"merge","nodes":[{"id":2,"type":"Filter","params":{"cutoff":0.5}}],"connections":[]})";

    juce::var before, after;
    EXPECT_TRUE(service->computePatchPreview(candidate, /*mergeMode=*/true, before, after));

    auto changes = computeDiff(before, after);
    auto* c = firstOfKind(changes, Kind::ParamChanged);
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(c->paramId, "cutoff");
    EXPECT_NEAR((double)c->afterValue, 10010.0, 1.0);
    EXPECT_GT(std::abs((double)c->afterValue - 0.5), 1.0) << "must show the RESCALED value, not the raw patch value";
}

TEST_F(PatchDiffIntegrationTest, NoOpPatchWithSkewedParamProducesEmptyDiff) {
    // LFO's "rateHz" uses a skewed NormalisableRange (LFOModule.h). computePatchPreview()'s
    // "before" must travel the SAME trusted-replay round-trip (denormalize -> setValueNotifyingHost
    // -> renormalize) as "after" does, or a merge-mode preview of an empty delta could show a
    // phantom ParamChanged purely from replay round-trip rounding on an untouched node.
    ASSERT_TRUE(service->applyPatch(R"({"nodes":[{"id":1,"type":"Oscillator"},{"id":2,"type":"Audio Output"},
                                          {"id":3,"type":"LFO"}],
                                "connections":[{"src":1,"srcPort":0,"dst":2,"dstPort":0}]})"));

    const char* candidate = R"({"mode":"merge","nodes":[],"connections":[]})";

    juce::var before, after;
    EXPECT_TRUE(service->computePatchPreview(candidate, /*mergeMode=*/true, before, after));

    EXPECT_TRUE(computeDiff(before, after).empty());
}

TEST_F(PatchDiffIntegrationTest, ComputePatchPreviewDoesNotClobberLastPatchError) {
    // computePatchPreview() runs entirely against a scratch graph and must never touch the
    // service's error state — a preview computed while a previous real Apply failure is still on
    // screen must not silently blank (or change) that message.
    ASSERT_FALSE(service->applyPatch(R"({"nodes":[],"connections":[]})")); // fails the structural gate
    const juce::String errorBefore = service->getLastPatchError();
    ASSERT_FALSE(errorBefore.isEmpty());

    juce::var before, after;
    service->computePatchPreview(R"({"mode":"merge","nodes":[{"id":1,"type":"Reverb"}],"connections":[]})", true,
                                 before, after);

    EXPECT_EQ(service->getLastPatchError(), errorBefore);
}

} // namespace
} // namespace synth
