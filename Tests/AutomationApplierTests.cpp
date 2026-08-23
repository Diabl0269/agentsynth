// The engine-level net for automation actually reaching a live parameter.
//
// AutomationKernelTests pins the evaluator; this file pins everything between a TimelineDoc lane
// and a juce::RangedAudioParameter on a node in a running graph: binding resolution against node
// uuids, the playing-only rule, range clamping, what happens when the bound node is deleted, and
// that republishing mid-render swaps tables without tearing.
//
// Headless/deterministic house rules, same as TimelineE2ETests: HostMode::Hosted only, no audio
// device, no sleeps, everything driven through synth::OfflineTransportDriver.
//
// Timing arithmetic: 48 kHz, 512-sample blocks, 120 BPM => 24000 samples/beat.

#include "../Source/AI/AIStateMapper.h"
#include "../Source/AudioEngine.h"
#include "../Source/Modules/ModuleBase.h"
#include "../Source/Transport/OfflineTransportDriver.h"
#include "Timeline/AutomationApplier.h"
#include "Timeline/TimelineDoc.h"
#include "Timeline/TimelineSnapshot.h"
#include <cmath>
#include <gtest/gtest.h>
#include <memory>

using synth::AutomationLane;
using synth::TimelineDoc;
using synth::TrackKind;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

constexpr const char* kOscUuid = "a0100000-0000-0000-0000-000000000001";
constexpr const char* kFilterUuid = "a0100000-0000-0000-0000-000000000002";

// Osc -> Filter -> Audio Output. Nothing here has to be audible: every assertion in this file is
// about a parameter's value, and the graph exists so those parameters belong to nodes a real
// render pass actually walks.
juce::String buildPatchJson() {
    return juce::String(R"({
        "nodes": [
            {"id": 1, "type": "Oscillator",   "uuid": ")") +
           kOscUuid + R"(", "params": {"waveform": "Sine", "level": 1.0}},
            {"id": 2, "type": "Filter",       "uuid": ")" +
           kFilterUuid + R"("},
            {"id": 3, "type": "Audio Output", "uuid": "a0100000-0000-0000-0000-000000000003"}
        ],
        "connections": [
            {"src": 1, "srcPort": 0, "dst": 2, "dstPort": 0},
            {"src": 2, "srcPort": 0, "dst": 3, "dstPort": 0}
        ]
    })";
}

AutomationLane::RangeSnapshot range(float minValue, float maxValue, float defaultValue) {
    AutomationLane::RangeSnapshot snapshot;
    snapshot.minValue = minValue;
    snapshot.maxValue = maxValue;
    snapshot.defaultValue = defaultValue;
    return snapshot;
}

struct Fixture {
    AudioEngine engine{AudioEngine::HostMode::Hosted};
    std::unique_ptr<synth::OfflineTransportDriver> driver;
    TimelineDoc doc;
    synth::TrackId trackId;

    bool build() {
        engine.initialise();

        const juce::var patch = juce::JSON::parse(buildPatchJson());
        if (!patch.isObject())
            return false;
        if (!synth::AIStateMapper::applyJSONToGraph(patch, engine.getGraph(), /*clearExisting=*/true,
                                                    /*trusted=*/true))
            return false;

        // After the graph, like TimelineE2ETests: the ctor prepareForHost's the nodes just created.
        driver = std::make_unique<synth::OfflineTransportDriver>(engine, kSampleRate, kBlockSize, 2);

        trackId = doc.addTrack(TrackKind::Midi, "Track 1");
        return trackId.isValid();
    }

    void publish() { engine.publishTimeline(doc); }

    juce::AudioProcessorGraph::Node* nodeFor(const char* uuid) {
        for (auto* node : engine.getGraph().getNodes())
            if (node != nullptr && node->properties["uuid"].toString() == juce::String(uuid))
                return node;
        return nullptr;
    }

    juce::RangedAudioParameter* param(const char* uuid, const char* paramId) {
        auto* node = nodeFor(uuid);
        return node != nullptr ? findParameterByID(node->getProcessor(), paramId) : nullptr;
    }

    static double denormalised(const juce::RangedAudioParameter* parameter) {
        return static_cast<double>(parameter->convertFrom0to1(parameter->getValue()));
    }

    int bindingCount() { return static_cast<int>(engine.getAutomationBindings().beginAudioBlock().bindings.size()); }

    ~Fixture() {
        if (driver) {
            engine.releaseFromHost();
            engine.shutdown();
        }
    }
};

} // namespace

// ============================================================================
// 1. A lane really moves the parameter, block by block
// ============================================================================

TEST(AutomationApplierTest, LaneRampFollowsTheBeatPosition) {
    Fixture f;
    ASSERT_TRUE(f.build());

    constexpr double kStart = 100.0;
    constexpr double kEnd = 8100.0;
    constexpr double kLengthBeats = 4.0;

    const auto laneId = f.doc.addLane(f.trackId, kFilterUuid, "cutoff", range(20.0f, 20000.0f, 440.0f));
    ASSERT_TRUE(laneId.isValid());
    ASSERT_TRUE(f.doc.addBreakpoint(laneId, 0.0, kStart));
    ASSERT_TRUE(f.doc.addBreakpoint(laneId, kLengthBeats, kEnd));
    f.publish();
    ASSERT_EQ(f.bindingCount(), 1);

    auto* cutoff = f.param(kFilterUuid, "cutoff");
    ASSERT_NE(cutoff, nullptr);

    ASSERT_TRUE(f.driver->getTransport().play());

    int blocksChecked = 0;
    f.driver->renderToBeat(kLengthBeats, [&](const juce::AudioBuffer<float>&, const synth::BlockTimeInfo& info) {
        if (!info.playing)
            return;
        // The applier evaluates at the block's START position and writes before the graph runs, so
        // by the time this observer sees the rendered block the parameter holds exactly the ramp's
        // value at info.startPpq — no lag to allow for, only float round-trip error.
        const double beat = juce::jlimit(0.0, kLengthBeats, info.startPpq);
        const double expected = kStart + (kEnd - kStart) * (beat / kLengthBeats);
        EXPECT_NEAR(Fixture::denormalised(cutoff), expected, 1.0)
            << "at beat " << info.startPpq << " (block " << blocksChecked << ")";
        ++blocksChecked;
    });

    EXPECT_GT(blocksChecked, 100) << "the ramp must be sampled across many blocks, not just one";

    // renderToBeat stops on the block whose END passes the target, so the last block it rendered
    // still STARTED before beat 4 — two more blocks put the start position past the last breakpoint,
    // where the kernel holds the final value rather than extrapolating.
    f.driver->renderBlocks(2);
    EXPECT_NEAR(Fixture::denormalised(cutoff), kEnd, 1.0);
}

// ============================================================================
// 2. A stopped transport leaves the knob alone
// ============================================================================

TEST(AutomationApplierTest, StoppedTransportLeavesParameterUntouched) {
    Fixture f;
    ASSERT_TRUE(f.build());

    const auto laneId = f.doc.addLane(f.trackId, kFilterUuid, "cutoff", range(20.0f, 20000.0f, 440.0f));
    ASSERT_TRUE(laneId.isValid());
    ASSERT_TRUE(f.doc.addBreakpoint(laneId, 0.0, 100.0));
    ASSERT_TRUE(f.doc.addBreakpoint(laneId, 4.0, 8100.0));
    f.publish();
    ASSERT_EQ(f.bindingCount(), 1);

    auto* cutoff = f.param(kFilterUuid, "cutoff");
    ASSERT_NE(cutoff, nullptr);
    const double before = Fixture::denormalised(cutoff);
    ASSERT_NEAR(before, 440.0, 0.01) << "FilterModule's cutoff default";

    // No play(): the graph still renders every block (see OfflineTransportDriverTest), the applier
    // simply must not write.
    ASSERT_FALSE(f.driver->getTransport().getPositionSnapshot().playing);
    f.driver->renderBlocks(64);

    EXPECT_NEAR(Fixture::denormalised(cutoff), before, 1e-6)
        << "automation must not fight the user for a knob while the transport is stopped";
}

// ============================================================================
// 3. Deleting the bound node: no crash, and the binding is gone on republish
// ============================================================================

TEST(AutomationApplierTest, DeletedNodeKeepsRenderingSafeUntilRepublish) {
    Fixture f;
    ASSERT_TRUE(f.build());

    const auto laneId = f.doc.addLane(f.trackId, kFilterUuid, "cutoff", range(20.0f, 20000.0f, 440.0f));
    ASSERT_TRUE(laneId.isValid());
    ASSERT_TRUE(f.doc.addBreakpoint(laneId, 0.0, 1000.0));
    ASSERT_TRUE(f.doc.addBreakpoint(laneId, 4.0, 5000.0));
    f.publish();
    ASSERT_EQ(f.bindingCount(), 1);

    auto* node = f.nodeFor(kFilterUuid);
    ASSERT_NE(node, nullptr);
    const auto nodeId = node->nodeID;

    ASSERT_TRUE(f.driver->getTransport().play());
    f.driver->renderBlocks(4);

    // The node goes away AFTER the table was published, so the published binding is now the only
    // thing keeping that processor alive. Rendering must keep working: the applier writes into a
    // parameter nobody reads any more, which is exactly the harmless case the refcounted Node::Ptr
    // buys.
    ASSERT_NE(f.engine.getGraph().removeNode(nodeId).get(), nullptr);
    EXPECT_EQ(f.nodeFor(kFilterUuid), nullptr);
    f.driver->renderBlocks(64);
    EXPECT_EQ(f.bindingCount(), 1) << "the stale table is still published and still safe";

    // Republishing resolves against the CURRENT graph, so the orphaned lane now binds to nothing.
    f.publish();
    EXPECT_EQ(f.bindingCount(), 0);
    f.driver->renderBlocks(4);
}

// ============================================================================
// 4. Lanes that cannot resolve produce no bindings at all
// ============================================================================

TEST(AutomationApplierTest, UnresolvableLanesProduceNoBinding) {
    Fixture f;
    ASSERT_TRUE(f.build());

    // (a) a uuid no node carries
    ASSERT_TRUE(
        f.doc.addLane(f.trackId, "deadbeef-0000-0000-0000-000000000000", "cutoff", range(20.0f, 20000.0f, 440.0f))
            .isValid());
    // (b) a real node, a parameter it does not have
    ASSERT_TRUE(f.doc.addLane(f.trackId, kFilterUuid, "notAParameter", range(0.0f, 1.0f, 0.0f)).isValid());
    // (c) a lane with an EMPTY nodeUuid cannot exist — TimelineDoc::addLane rejects one outright,
    //     which is why the applier's "unbound" guard is belt-and-braces rather than reachable here.
    ASSERT_FALSE(f.doc.addLane(f.trackId, "", "cutoff", range(20.0f, 20000.0f, 440.0f)).isValid());

    f.publish();
    EXPECT_EQ(f.bindingCount(), 0);

    // And one resolvable lane alongside them still binds — the unresolvable ones don't poison the
    // whole table.
    const auto good = f.doc.addLane(f.trackId, kOscUuid, "level", range(0.0f, 1.0f, 1.0f));
    ASSERT_TRUE(good.isValid());
    f.publish();
    EXPECT_EQ(f.bindingCount(), 1);
}

// ============================================================================
// 5. A lane authored against a wider range clamps into the parameter's own
// ============================================================================

TEST(AutomationApplierTest, ValuesOutsideTheParameterRangeClamp) {
    Fixture f;
    ASSERT_TRUE(f.build());

    // The Oscillator's "level" is 0..1. This lane's range is deliberately wider on both sides, the
    // case a lane authored before a range narrowed (or by hand) produces.
    const auto laneId = f.doc.addLane(f.trackId, kOscUuid, "level", range(-1.0f, 2.0f, 1.0f));
    ASSERT_TRUE(laneId.isValid());
    ASSERT_TRUE(f.doc.addBreakpoint(laneId, 0.0, 2.0, 0.0f, static_cast<int>(synth::BreakpointCurve::Hold)));
    ASSERT_TRUE(f.doc.addBreakpoint(laneId, 2.0, -1.0, 0.0f, static_cast<int>(synth::BreakpointCurve::Hold)));
    f.publish();
    ASSERT_EQ(f.bindingCount(), 1);

    auto* level = f.param(kOscUuid, "level");
    ASSERT_NE(level, nullptr);

    ASSERT_TRUE(f.driver->getTransport().play());
    f.driver->renderBlocks(4);
    EXPECT_NEAR(Fixture::denormalised(level), 1.0, 1e-5) << "2.0 must pin at the parameter's maximum";

    ASSERT_TRUE(f.driver->getTransport().locateBeat(2.5));
    f.driver->renderBlocks(4);
    EXPECT_NEAR(Fixture::denormalised(level), 0.0, 1e-5) << "-1.0 must pin at the parameter's minimum";
}

// ============================================================================
// 6. Republishing between blocks swaps tables without tearing
// ============================================================================

TEST(AutomationApplierTest, RepublishBetweenBlocksSwapsTablesCleanly) {
    // The epoch machinery itself is stress-tested by TimelineSnapshotTest::StressPublishAcquire —
    // this is a smoke test that the NEW instantiation (EpochExchange<AutomationBindingTable>, whose
    // payload owns refcounted Node::Ptrs) behaves the same when publishes and render passes
    // interleave: no crash, no leak, and the last publish is what the parameter follows.
    Fixture f;
    ASSERT_TRUE(f.build());

    const auto laneId = f.doc.addLane(f.trackId, kFilterUuid, "cutoff", range(20.0f, 20000.0f, 440.0f));
    ASSERT_TRUE(laneId.isValid());
    ASSERT_TRUE(f.doc.addBreakpoint(laneId, 0.0, 1000.0, 0.0f, static_cast<int>(synth::BreakpointCurve::Hold)));

    auto* cutoff = f.param(kFilterUuid, "cutoff");
    ASSERT_NE(cutoff, nullptr);
    ASSERT_TRUE(f.driver->getTransport().play());

    constexpr int kPublishes = 100;
    for (int i = 0; i < kPublishes; ++i) {
        // Every iteration is a real document edit, so every publish is a genuinely new snapshot and
        // a genuinely new table (new lane array => the cursors' pointer-identity guard fires too).
        ASSERT_TRUE(f.doc.addBreakpoint(laneId, 8.0 + i, 2000.0 + i));
        f.publish();
        f.driver->renderBlocks(1);
    }

    EXPECT_EQ(f.bindingCount(), 1);
    // The transport never left beat 0's Hold segment, so the value the last table produced is the
    // first breakpoint's, whatever else was appended past beat 8.
    EXPECT_NEAR(Fixture::denormalised(cutoff), 1000.0, 1.0);

    // Reclamation kept up: retirees are freed two epochs after they are displaced, so at most a
    // couple can still be waiting after 100 publishes interleaved with renders.
    EXPECT_LE(f.engine.getTimelineSnapshots().retiredCount(), 3);
    EXPECT_LE(f.engine.getAutomationBindings().retiredCount(), 3);
}

// ============================================================================
// 7. Publishing an empty document is legal and clears the table
// ============================================================================

TEST(AutomationApplierTest, PublishingAnEmptyDocClearsBindings) {
    Fixture f;
    ASSERT_TRUE(f.build());

    const auto laneId = f.doc.addLane(f.trackId, kOscUuid, "level", range(0.0f, 1.0f, 1.0f));
    ASSERT_TRUE(laneId.isValid());
    ASSERT_TRUE(f.doc.addBreakpoint(laneId, 0.0, 0.25, 0.0f, static_cast<int>(synth::BreakpointCurve::Hold)));
    f.publish();
    ASSERT_EQ(f.bindingCount(), 1);

    ASSERT_TRUE(f.driver->getTransport().play());
    f.driver->renderBlocks(4);

    auto* level = f.param(kOscUuid, "level");
    ASSERT_NE(level, nullptr);
    ASSERT_NEAR(Fixture::denormalised(level), 0.25, 1e-5);

    f.doc.clear();
    f.publish();
    EXPECT_EQ(f.bindingCount(), 0);

    f.driver->renderBlocks(8);
    EXPECT_NEAR(Fixture::denormalised(level), 0.25, 1e-5)
        << "removing a lane must leave the parameter where automation last put it, not reset it";
}
