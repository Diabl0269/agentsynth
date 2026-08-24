// HostedPluginLaneTests.cpp
//
// Hosted plugin parameters as automation lanes — keyed on the plugin's paramID string with
// index fallback, orphaning on mismatch, never silently automating the wrong parameter.
//
// Everything here runs against Tests/StubPluginInstance.h (StubHostedParameter / StubLegacyParameter
// / StubParamSpec), never a real plugin — same reasoning as HostedPluginTests.cpp: there is no
// third-party binary to check into this repo, and the backend seam exists precisely so that is not a
// reason to leave any of this untested.
//
// Groups:
//   1. ExactParamIdBindsAndApplies      — the applier plays a lane straight into a stub instance
//                                          param, and only that one.
//   2. VersionChangeOrphansNotMisbinds  — THE motivating story: a plugin update moves a stable-id
//                                          parameter to a different index; the lane must ORPHAN,
//                                          never silently bind to whatever now sits there.
//   3. LegacyIndexFallbackBinds         — a plugin with no stable ids at all still resolves by index.
//   4. RecorderCapturesHostedParamGesture — AutomationRecorder captures a real gesture on a hosted
//                                            parameter; a programmatic write is still ignored.
//   5. LanePickerAddsPluginLanes        — the "Add lane..." creation surface's data contract:
//                                          RangeSnapshot {0, 1, default} + paramIndexHint stored.
//   6. InstanceUnloadOrphansLanesAndReloadRebinds — reconcile across unload/reload.
//   7. TimelineOpsWriteLaneValidatesAgainstInnerParam — 0..1 bounds via the shared resolver.
//   8. SerializationAdditive            — paramIndexHint absent -> -1 default; old files load.

#include "../Source/AppUndoManager.h"
#include "../Source/AudioEngine.h"
#include "../Source/Plugin/Hosting/HostedPluginModule.h"
#include "../Source/Timeline/AutomationBinding.h"
#include "../Source/Timeline/AutomationRecorder.h"
#include "../Source/Timeline/TimelineOps.h"
#include "../Source/Timeline/TimelineReconciler.h"
#include "../Source/Transport/OfflineTransportDriver.h"
#include "../Source/Transport/TransportService.h"
#include "StubPluginInstance.h"
#include <chrono>
#include <gtest/gtest.h>
#include <memory>
#include <vector>

using synth::AutomationLane;
using synth::AutomationRecorder;
using synth::HostedPluginModule;
using synth::LaneId;
using synth::LaneRecordMode;
using synth::TimelineDoc;
using synth::TimelineOps;
using synth::TimelineOpsResult;
using synth::TimelineReconciler;
using synth::TrackId;
using synth::TrackKind;
using synth::test::StubBackend;
using synth::test::StubParamSpec;
using synth::test::StubPluginInstance;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;
constexpr const char* kPluginUuid = "b0760000-0000-0000-0000-000000000001";

AutomationLane::RangeSnapshot range(float minValue, float maxValue, float defaultValue) {
    AutomationLane::RangeSnapshot snapshot;
    snapshot.minValue = minValue;
    snapshot.maxValue = maxValue;
    snapshot.defaultValue = defaultValue;
    return snapshot;
}

juce::PluginDescription stubDescription(const juce::String& name = "Stub Plugin", int uid = 0x5754424) {
    juce::PluginDescription description;
    description.name = name;
    description.pluginFormatName = "VST3";
    description.uniqueId = uid;
    description.deprecatedUid = uid;
    description.fileOrIdentifier = "/nonexistent/test/path/StubPlugin.vst3";
    return description;
}

/** Pumps the JUCE message loop until `predicate` holds or the timeout expires — the bounded-poll
 *  idiom from HostedPluginTests.cpp, needed because the backend callback is posted, never fired
 *  re-entrantly. */
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

// AudioEngine + OfflineTransportDriver + a single HostedPluginModule node, wired the way every
// other engine-level timeline test builds its fixture (AutomationApplierTests.cpp's Fixture).
struct Fixture {
    AudioEngine engine{AudioEngine::HostMode::Hosted};
    std::unique_ptr<synth::OfflineTransportDriver> driver;
    TimelineDoc doc;
    TrackId trackId;
    HostedPluginModule* hostedModule = nullptr;
    StubBackend backend;

    bool build(std::vector<StubParamSpec> params) {
        engine.initialise();

        auto module = std::make_unique<HostedPluginModule>();
        hostedModule = module.get();
        auto node = engine.getGraph().addNode(std::move(module));
        node->properties.set("uuid", juce::String(kPluginUuid));

        // Prepares every node in the graph, including the one just added — see the class comment on
        // AutomationApplierTests.cpp's own Fixture for why the node has to exist first.
        driver = std::make_unique<synth::OfflineTransportDriver>(engine, kSampleRate, kBlockSize, 2);

        if (!load(std::move(params)))
            return false;

        trackId = doc.addTrack(TrackKind::Midi, "Track 1");
        return trackId.isValid();
    }

    // Loads (or reloads, "the plugin updated") the instance with `params`. A reload retires the OLD
    // instance and publishes a new one, exactly HostedPluginModule's own supersede-in-flight
    // discipline — this is not a special path, just the same loadPlugin() call again.
    bool load(std::vector<StubParamSpec> params) {
        backend.setFactory(
            [params] { return std::make_unique<StubPluginInstance>(2, 2, "Stub Plugin", 1, "VST3", params); });
        hostedModule->loadPlugin(stubDescription(), backend);
        return pumpUntil([&] { return hostedModule->hasInstance() && !hostedModule->isLoading(); });
    }

    void publish() { engine.publishTimeline(doc); }

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
// 1. Exact paramID match binds, and the applier writes into it (and only it)
// ============================================================================

TEST(HostedPluginLaneTest, ExactParamIdBindsAndApplies) {
    Fixture f;
    ASSERT_TRUE(f.build({{"cutoff", "Cutoff", 0.2f}, {"reso", "Resonance", 0.5f}}));

    const auto laneId = f.doc.addLane(f.trackId, kPluginUuid, "cutoff", range(0.0f, 1.0f, 0.2f));
    ASSERT_TRUE(laneId.isValid());
    ASSERT_TRUE(f.doc.addBreakpoint(laneId, 0.0, 0.1));
    ASSERT_TRUE(f.doc.addBreakpoint(laneId, 4.0, 0.9));
    f.publish();
    ASSERT_EQ(f.bindingCount(), 1);

    auto* cutoff = f.hostedModule->findInstanceParameter("cutoff");
    auto* reso = f.hostedModule->findInstanceParameter("reso");
    ASSERT_NE(cutoff, nullptr);
    ASSERT_NE(reso, nullptr);

    ASSERT_TRUE(f.driver->getTransport().play());
    f.driver->renderBlocks(1);
    EXPECT_NEAR(cutoff->getValue(), 0.1, 0.02);

    f.driver->renderToBeat(4.0);
    f.driver->renderBlocks(2);
    EXPECT_NEAR(cutoff->getValue(), 0.9, 0.02);

    // The OTHER parameter must be untouched — proves this bound the right one, not merely "a" one.
    EXPECT_NEAR(reso->getValue(), 0.5, 1e-6);
}

// ============================================================================
// 2. VersionChangeOrphansNotMisbinds — THE motivating story
// ============================================================================

TEST(HostedPluginLaneTest, VersionChangeOrphansNotMisbinds) {
    Fixture f;
    ASSERT_TRUE(f.build({{"cutoff", "Cutoff", 0.2f}, {"reso", "Resonance", 0.5f}}));

    // The hint the picker captures at creation time, from the CURRENTLY resolved index.
    const int hint = f.hostedModule->getInstanceParamIndexFallback("reso");
    ASSERT_EQ(hint, 1);
    const auto laneId = f.doc.addLane(f.trackId, kPluginUuid, "reso", range(0.0f, 1.0f, 0.5f), hint);
    ASSERT_TRUE(laneId.isValid());

    ASSERT_FALSE(TimelineReconciler::reconcile(f.doc, f.engine.getGraph())) << "resolves fine before any update";
    ASSERT_FALSE(f.doc.getLane(laneId)->orphaned);
    f.publish();
    ASSERT_EQ(f.bindingCount(), 1);

    // "The plugin updated": index 1 now names a DIFFERENT, still-stably-identified parameter.
    ASSERT_TRUE(f.load({{"cutoff", "Cutoff", 0.2f}, {"drive", "Drive", 0.0f}}));

    ASSERT_TRUE(TimelineReconciler::reconcile(f.doc, f.engine.getGraph()));
    EXPECT_TRUE(f.doc.getLane(laneId)->orphaned) << "must orphan, never silently bind to 'drive'";
    ASSERT_NE(f.doc.getLane(laneId), nullptr) << "orphaned lanes are retained, never deleted";

    // The applier's own binding build must ALSO have dropped it.
    f.publish();
    EXPECT_EQ(f.bindingCount(), 0);

    auto* drive = f.hostedModule->findInstanceParameter("drive");
    ASSERT_NE(drive, nullptr);
    const float driveBefore = drive->getValue();
    ASSERT_TRUE(f.doc.addBreakpoint(laneId, 0.0, 0.95)); // if it were (mis)bound, this would move 'drive'
    f.publish();
    ASSERT_TRUE(f.driver->getTransport().play());
    f.driver->renderBlocks(4);
    EXPECT_FLOAT_EQ(drive->getValue(), driveBefore) << "the orphaned lane must never touch 'drive'";

    // Re-bind via the picker: there is no paramId-only rebind (TimelineDoc::rebindLane only ever
    // changes nodeUuid, never paramId — a parameter set moving is not "the same node, new place"),
    // so the picker's actual affordance is offering "drive" as a fresh, not-yet-automated parameter
    // and creating a NEW lane for it — exactly LanePickerAddsPluginLanes below.
    const auto newHint = f.hostedModule->getInstanceParamIndexFallback("drive");
    const auto reboundLane = f.doc.addLane(f.trackId, kPluginUuid, "drive", range(0.0f, 1.0f, 0.0f), newHint);
    ASSERT_TRUE(reboundLane.isValid());
    f.publish();
    EXPECT_EQ(f.bindingCount(), 1);

    ASSERT_TRUE(f.doc.addBreakpoint(reboundLane, 0.0, 0.75));
    f.publish();
    f.driver->getTransport().locateBeat(0.0);
    f.driver->renderBlocks(1);
    EXPECT_NEAR(drive->getValue(), 0.75, 0.02);
}

// ============================================================================
// 3. LegacyIndexFallbackBinds — a plugin with no stable ids at all
// ============================================================================

TEST(HostedPluginLaneTest, LegacyIndexFallbackBinds) {
    Fixture f;
    // An empty paramId builds a StubLegacyParameter — no HostedAudioProcessorParameter at all, the
    // "plugins without WithID parameters" case.
    ASSERT_TRUE(f.build({{"", "Legacy Cutoff", 0.3f}, {"", "Legacy Drive", 0.0f}}));

    const auto params = f.hostedModule->getInstanceParameters();
    ASSERT_EQ(params.size(), 2u);
    EXPECT_EQ(params[0].paramId, "legacy:0");
    EXPECT_EQ(params[1].paramId, "legacy:1");

    // The synthetic key must never accidentally exact-match a real getParameterID() — there is
    // nothing to match, by construction.
    EXPECT_EQ(f.hostedModule->findInstanceParameter("legacy:1"), nullptr);

    const auto laneId = f.doc.addLane(f.trackId, kPluginUuid, "legacy:1", range(0.0f, 1.0f, 0.0f), 1);
    ASSERT_TRUE(laneId.isValid());
    f.publish();
    EXPECT_EQ(f.bindingCount(), 1) << "the index fallback must bind: this plugin format has no ids at all";

    ASSERT_FALSE(TimelineReconciler::reconcile(f.doc, f.engine.getGraph()));
    EXPECT_FALSE(f.doc.getLane(laneId)->orphaned);

    ASSERT_TRUE(f.doc.addBreakpoint(laneId, 0.0, 0.8));
    f.publish();
    ASSERT_TRUE(f.driver->getTransport().play());
    f.driver->renderBlocks(1);

    auto* legacyDrive = f.hostedModule->findInstanceParameterByIndex(1);
    ASSERT_NE(legacyDrive, nullptr);
    EXPECT_NEAR(legacyDrive->getValue(), 0.8, 0.02);
}

// ============================================================================
// 4. RecorderCapturesHostedParamGesture
// ============================================================================

TEST(HostedPluginLaneTest, RecorderCapturesHostedParamGesture) {
    // A bare TransportService + AppUndoManager + TimelineDoc harness, no AudioEngine/graph needed —
    // the same headless shape AutomationRecordTests.cpp's own Harness uses.
    synth::TransportService transport;
    TimelineDoc doc;
    AppUndoManager undo;
    HostedPluginModule hosted;
    StubBackend backend([] {
        return std::make_unique<StubPluginInstance>(2, 2, "Stub Plugin", 1, "VST3",
                                                    std::vector<StubParamSpec>{{"gain", "Gain", 0.4f}});
    });
    AutomationRecorder recorder;

    transport.prepare(kSampleRate, kBlockSize);
    recorder.attachTo(doc, undo, transport);

    hosted.prepareToPlay(kSampleRate, kBlockSize);
    hosted.loadPlugin(stubDescription(), backend);
    ASSERT_TRUE(pumpUntil([&] { return hosted.hasInstance(); }));

    auto* gain = hosted.findInstanceParameter("gain");
    ASSERT_NE(gain, nullptr);

    const auto trackId = doc.addTrack(TrackKind::Midi, "Track 1");
    const auto laneId = doc.addLane(trackId, kPluginUuid, "gain", range(0.0f, 1.0f, 0.4f),
                                    hosted.getInstanceParamIndexFallback("gain"));
    ASSERT_TRUE(laneId.isValid());
    recorder.bindLane(laneId, gain, {});
    ASSERT_TRUE(doc.setLaneRecordMode(laneId, static_cast<int>(LaneRecordMode::Touch)));

    recorder.setGlobalRecordEnable(true);
    ASSERT_TRUE(transport.play());
    transport.tick(0);
    recorder.update();

    // The trio a JUCE slider attachment produces — direct 0..1, since a hosted parameter's native
    // domain already IS 0..1 (no convertTo0to1 to go through).
    gain->beginChangeGesture();
    gain->setValueNotifyingHost(0.9f);
    gain->endChangeGesture();

    const auto* lane = doc.getLane(laneId);
    ASSERT_NE(lane, nullptr);
    ASSERT_FALSE(lane->points.empty()) << "the gesture must be captured";
    EXPECT_NEAR(lane->points.back().value, 0.9, 0.01);
    EXPECT_TRUE(undo.canUndo()) << "one committed gesture, one undo step";

    // The programmatic-write guard still applies to a hosted parameter: no gesture, no capture.
    undo.undo();
    ASSERT_TRUE(doc.getLane(laneId)->points.empty());
    gain->setValueNotifyingHost(0.1f); // preset-load/AI-apply/undo-restore shape: no surrounding gesture
    recorder.update();
    EXPECT_TRUE(doc.getLane(laneId)->points.empty()) << "a value change with no gesture is not a performance";
    EXPECT_FALSE(undo.canUndo());
}

// ============================================================================
// 5. LanePickerAddsPluginLanes — the "Add lane..." creation surface's data contract
// ============================================================================
//
// The production entry points (MainComponent::getAvailablePluginLaneOptions /
// addPluginAutomationLane, wired into TimelinePanelComponent's lane picker) are thin glue over the
// pieces exercised directly here — HostedPluginModule::getInstanceParameters(),
// synth::resolveLaneParameter/laneValueBoundsFor/laneDefaultValueFor and TimelineDoc::addLane — so
// this is the headless test of the actual data contract without standing up a GUI MainComponent.

TEST(HostedPluginLaneTest, LanePickerAddsPluginLanes) {
    Fixture f;
    ASSERT_TRUE(f.build({{"cutoff", "Cutoff", 0.25f}, {"reso", "Resonance", 0.6f}}));

    auto instanceParams = f.hostedModule->getInstanceParameters();
    ASSERT_EQ(instanceParams.size(), 2u);
    for (const auto& p : instanceParams)
        EXPECT_EQ(f.doc.getLaneForParam(kPluginUuid, p.paramId), nullptr) << "nothing automated yet";

    const auto& cutoffInfo = instanceParams[0];
    ASSERT_EQ(cutoffInfo.paramId, "cutoff");

    const auto resolved = synth::resolveLaneParameter(f.hostedModule, cutoffInfo.paramId, cutoffInfo.index);
    ASSERT_TRUE(resolved.resolved());

    AutomationLane::RangeSnapshot rangeSnapshot;
    const auto bounds = synth::laneValueBoundsFor(resolved);
    rangeSnapshot.minValue = static_cast<float>(bounds.minValue);
    rangeSnapshot.maxValue = static_cast<float>(bounds.maxValue);
    rangeSnapshot.defaultValue = static_cast<float>(synth::laneDefaultValueFor(resolved));

    const auto laneId = f.doc.addLane(f.trackId, kPluginUuid, cutoffInfo.paramId, rangeSnapshot, cutoffInfo.index);
    ASSERT_TRUE(laneId.isValid());

    const auto* lane = f.doc.getLane(laneId);
    ASSERT_NE(lane, nullptr);
    EXPECT_FLOAT_EQ(lane->range.minValue, 0.0f);
    EXPECT_FLOAT_EQ(lane->range.maxValue, 1.0f);
    EXPECT_FLOAT_EQ(lane->range.defaultValue, 0.25f);
    EXPECT_EQ(lane->paramIndexHint, cutoffInfo.index);
    EXPECT_EQ(lane->paramId, "cutoff");
    EXPECT_EQ(lane->nodeUuid, kPluginUuid);

    // "cutoff" must have dropped off the still-available list; "reso" remains.
    instanceParams = f.hostedModule->getInstanceParameters();
    int stillAvailable = 0;
    for (const auto& p : instanceParams)
        if (f.doc.getLaneForParam(kPluginUuid, p.paramId) == nullptr)
            ++stillAvailable;
    EXPECT_EQ(stillAvailable, 1) << "only 'reso' should remain offered";
}

// ============================================================================
// 6. InstanceUnloadOrphansLanesAndReloadRebinds
// ============================================================================

TEST(HostedPluginLaneTest, InstanceUnloadOrphansLanesAndReloadRebinds) {
    Fixture f;
    ASSERT_TRUE(f.build({{"cutoff", "Cutoff", 0.25f}}));

    const auto laneId = f.doc.addLane(f.trackId, kPluginUuid, "cutoff", range(0.0f, 1.0f, 0.25f),
                                      f.hostedModule->getInstanceParamIndexFallback("cutoff"));
    ASSERT_TRUE(laneId.isValid());
    ASSERT_FALSE(TimelineReconciler::reconcile(f.doc, f.engine.getGraph()));
    ASSERT_FALSE(f.doc.getLane(laneId)->orphaned);
    f.publish();
    ASSERT_EQ(f.bindingCount(), 1);

    f.hostedModule->unloadPlugin();
    ASSERT_FALSE(f.hostedModule->hasInstance());

    ASSERT_TRUE(TimelineReconciler::reconcile(f.doc, f.engine.getGraph()));
    EXPECT_TRUE(f.doc.getLane(laneId)->orphaned)
        << "no live instance to verify against: orphan, don't silently do nothing";
    ASSERT_NE(f.doc.getLane(laneId), nullptr) << "retained, never deleted";
    f.publish();
    EXPECT_EQ(f.bindingCount(), 0);

    // Reload the SAME plugin (same parameter set) — the un-loaded/re-loaded cycle a real "update the
    // plugin binary and restart the DAW" would produce when nothing actually changed.
    ASSERT_TRUE(f.load({{"cutoff", "Cutoff", 0.25f}}));

    ASSERT_TRUE(TimelineReconciler::reconcile(f.doc, f.engine.getGraph()));
    EXPECT_FALSE(f.doc.getLane(laneId)->orphaned) << "reload must re-resolve the same lane";
    f.publish();
    EXPECT_EQ(f.bindingCount(), 1);
}

// ============================================================================
// 7. TimelineOpsWriteLaneValidatesAgainstInnerParam
// ============================================================================

TEST(HostedPluginLaneTest, TimelineOpsWriteLaneValidatesAgainstInnerParam) {
    StubBackend backend([] {
        return std::make_unique<StubPluginInstance>(2, 2, "Stub Plugin", 1, "VST3",
                                                    std::vector<StubParamSpec>{{"gain", "Gain", 0.5f}});
    });

    juce::AudioProcessorGraph graph;
    auto module = std::make_unique<HostedPluginModule>();
    auto* modulePtr = module.get();
    auto node = graph.addNode(std::move(module));
    node->properties.set("uuid", "plugin-uuid");

    modulePtr->prepareToPlay(kSampleRate, kBlockSize);
    modulePtr->loadPlugin(stubDescription(), backend);
    ASSERT_TRUE(pumpUntil([&] { return modulePtr->hasInstance(); }));

    TimelineDoc doc;
    AppUndoManager undoManager;

    // In bounds: [0, 1] is the whole legal range for an always-normalised hosted parameter.
    const auto inBounds = juce::JSON::parse(
        R"({"timelineOps": [{"op": "writeLane", "nodeUuid": "plugin-uuid", "paramId": "gain",
             "points": [{"beat": 0, "value": 0.2, "tension": 0, "curve": 1},
                        {"beat": 4, "value": 0.9, "tension": 0, "curve": 1}]}]})");
    ASSERT_TRUE(TimelineOps::validate(inBounds, doc, graph).ok);
    const auto applied = TimelineOps::apply(inBounds, doc, graph, undoManager);
    ASSERT_TRUE(applied.ok) << applied.message;

    const auto* lane = doc.getLaneForParam("plugin-uuid", "gain");
    ASSERT_NE(lane, nullptr);
    EXPECT_FLOAT_EQ(lane->range.minValue, 0.0f);
    EXPECT_FLOAT_EQ(lane->range.maxValue, 1.0f);
    EXPECT_FLOAT_EQ(lane->range.defaultValue, 0.5f);
    EXPECT_GE(lane->paramIndexHint, 0) << "captured at creation via the shared resolver";

    // Out of bounds: refused via the resolver's [0, 1] bounds, not a NormalisableRange this
    // parameter does not have.
    const auto outOfBounds = juce::JSON::parse(
        R"({"timelineOps": [{"op": "writeLane", "nodeUuid": "plugin-uuid", "paramId": "gain",
             "points": [{"beat": 0, "value": 1.5, "tension": 0, "curve": 1}]}]})");
    const auto rejected = TimelineOps::validate(outOfBounds, doc, graph);
    EXPECT_FALSE(rejected.ok);
    EXPECT_TRUE(rejected.message.contains("gain")) << rejected.message;

    // A paramId this instance does not have is refused outright.
    const auto noSuchParam = juce::JSON::parse(
        R"({"timelineOps": [{"op": "writeLane", "nodeUuid": "plugin-uuid", "paramId": "notAParam",
             "points": [{"beat": 0, "value": 0.5, "tension": 0, "curve": 1}]}]})");
    EXPECT_FALSE(TimelineOps::validate(noSuchParam, doc, graph).ok);
}

// ============================================================================
// 8. SerializationAdditive
// ============================================================================

TEST(HostedPluginLaneTest, SerializationAdditive) {
    TimelineDoc doc;
    const auto trackId = doc.addTrack(TrackKind::Midi, "Track 1");
    const auto laneId = doc.addLane(trackId, "some-uuid", "cutoff", range(0.0f, 1.0f, 0.5f), 3);
    ASSERT_TRUE(laneId.isValid());

    const juce::var saved = doc.toVar();

    TimelineDoc reloaded;
    ASSERT_TRUE(reloaded.fromVar(saved));
    ASSERT_NE(reloaded.getLane(laneId), nullptr);
    EXPECT_EQ(reloaded.getLane(laneId)->paramIndexHint, 3) << "round-trips through toVar/fromVar";

    // An older file simply has no "paramIndexHint" key — simulate by stripping it.
    auto* rootObj = saved.getDynamicObject();
    ASSERT_NE(rootObj, nullptr);
    auto* tracksArr = rootObj->getProperty("tracks").getArray();
    ASSERT_NE(tracksArr, nullptr);
    auto* trackObj = tracksArr->getReference(0).getDynamicObject();
    ASSERT_NE(trackObj, nullptr);
    auto* lanesArr = trackObj->getProperty("lanes").getArray();
    ASSERT_NE(lanesArr, nullptr);
    auto* laneObj = lanesArr->getReference(0).getDynamicObject();
    ASSERT_NE(laneObj, nullptr);
    laneObj->removeProperty("paramIndexHint");

    TimelineDoc legacyLoaded;
    ASSERT_TRUE(legacyLoaded.fromVar(saved)) << "a file predating paramIndexHint must still load";
    ASSERT_NE(legacyLoaded.getLane(laneId), nullptr);
    EXPECT_EQ(legacyLoaded.getLane(laneId)->paramIndexHint, -1) << "absent -> the -1 'no hint' default";
}
