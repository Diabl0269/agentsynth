// TL4-5: automated parameter changes reflected in the UI, without a feedback loop.
//
// AutomationApplier::applyBlock writes into live parameters with a plain, non-notifying setValue()
// (see AutomationApplier.h) — attachments never hear it, which is exactly what stops the applier's
// own write from re-triggering AutomationRecorder or the host. That's correct for the audio thread
// but leaves sliders on the canvas frozen while automation plays. This file pins the fix: a small
// lock-free ring (synth::AutomationUiFeed) carries {nodeId, param, normalised value} from the
// applier to GraphEditor's existing 30 Hz timer, which finds the right ModuleComponent by NodeID and
// calls ModuleComponent::reflectParameterValue(), a setValue(..., dontSendNotification) that moves
// the slider without writing the parameter, without firing a gesture, and without the recorder ever
// hearing about it.
//
// House style, same as AutomationApplierTests/AutomationRecordTests: test 1 is a bare applier +
// hand-built binding table, no engine and no graph. Tests 2-5 need real components (ModuleComponent,
// GraphEditor, AudioEngine), so the whole file is gated like its sibling automation suites.

#include "../Source/AppUndoManager.h"
#include "../Source/AudioEngine.h"
#include "../Source/Modules/FilterModule.h"
#include "../Source/Modules/ModuleBase.h"
#include "../Source/Modules/OscillatorModule.h"
#include "../Source/Timeline/AutomationApplier.h"
#include "../Source/Timeline/AutomationRecorder.h"
#include "../Source/Timeline/TimelineDoc.h"
#include "../Source/Timeline/TimelineSnapshot.h"
#include "../Source/Transport/TransportService.h"
#include "../Source/UI/GraphEditor.h"
#include "../Source/UI/ModuleComponent.h"
#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

#if SYNTH_ENABLE_TIMELINE

using synth::AutomationApplier;
using synth::AutomationBindingTable;
using synth::AutomationUiEvent;
using synth::AutomationUiFeed;
using synth::TimelineSnapshot;

namespace {

// Finds a slider the generic auto-UI built, by the componentID createControls() gives it (the
// parameter's display name). Mirrors the lookup pattern GraphEditorTests/ModuleComponentTests
// already use for e.g. Wavetable's "Position" knob.
juce::Slider* findSliderByComponentId(ModuleComponent& comp, const juce::String& id) {
    for (auto* child : comp.getChildren())
        if (auto* slider = dynamic_cast<juce::Slider*>(child); slider != nullptr && slider->getComponentID() == id)
            return slider;
    return nullptr;
}

} // namespace

// ============================================================================
// 1. FeedDedupes — a bare applier + hand-built binding table, no engine, no graph.
// ============================================================================

TEST(AutomationUiReflectionTest, FeedDedupes) {
    // A flat lane (zero points -> the kernel's fallbackValue, which never changes) must push AT
    // MOST once across many blocks, whatever its value is.
    {
        juce::AudioParameterFloat flatParam("flatUiFeedTestParam", "Flat", 0.0f, 100.0f, 50.0f);

        TimelineSnapshot snapshot;
        TimelineSnapshot::LaneInfo lane;
        lane.defaultValue = 50.0f;
        lane.firstPoint = 0;
        lane.numPoints = 0;
        snapshot.lanes.push_back(lane);

        AutomationBindingTable table;
        table.snapshot = &snapshot;
        AutomationBindingTable::Binding binding;
        binding.laneIndex = 0;
        binding.param = &flatParam;
        binding.nodeID = juce::AudioProcessorGraph::NodeID(1);
        table.bindings.push_back(binding);

        AutomationApplier applier;
        AutomationUiFeed feed;
        synth::BlockTimeInfo info;
        info.playing = true;

        for (int i = 0; i < 20; ++i) {
            info.startPpq = static_cast<double>(i);
            applier.applyBlock(table, info, nullptr, &feed);
        }

        int events = 0;
        feed.drain([&](const AutomationUiEvent&) { ++events; });
        EXPECT_LE(events, 1) << "a static lane must not spam the UI feed once per block";
    }

    // A ramping lane (a genuine Linear segment) must push roughly once per block — its value keeps
    // moving, so every write clears the dedupe check.
    {
        juce::AudioParameterFloat rampParam("rampUiFeedTestParam", "Ramp", 0.0f, 1000.0f, 0.0f);

        TimelineSnapshot snapshot;
        snapshot.points.push_back({0.0, 0.0, 0.0f, static_cast<int>(synth::BreakpointCurve::Linear)});
        snapshot.points.push_back({20.0, 1000.0, 0.0f, static_cast<int>(synth::BreakpointCurve::Linear)});
        TimelineSnapshot::LaneInfo lane;
        lane.defaultValue = 0.0f;
        lane.firstPoint = 0;
        lane.numPoints = 2;
        snapshot.lanes.push_back(lane);

        AutomationBindingTable table;
        table.snapshot = &snapshot;
        AutomationBindingTable::Binding binding;
        binding.laneIndex = 0;
        binding.param = &rampParam;
        binding.nodeID = juce::AudioProcessorGraph::NodeID(2);
        table.bindings.push_back(binding);

        AutomationApplier applier;
        AutomationUiFeed feed;
        synth::BlockTimeInfo info;
        info.playing = true;

        constexpr int kBlocks = 20;
        for (int i = 0; i < kBlocks; ++i) {
            info.startPpq = static_cast<double>(i);
            applier.applyBlock(table, info, nullptr, &feed);
        }

        int events = 0;
        feed.drain([&](const AutomationUiEvent&) { ++events; });
        EXPECT_GE(events, kBlocks - 2) << "a moving lane should push on almost every block";
        EXPECT_LE(events, kBlocks) << "dedupe must never push MORE than once per block";
    }
}

// ============================================================================
// 2. ReflectUpdatesSliderWithoutNotifying — a real ModuleComponent for a Filter node.
// ============================================================================

TEST(AutomationUiReflectionTest, ReflectUpdatesSliderWithoutNotifying) {
    AudioEngine engine;
    GraphEditor editor(engine);
    FilterModule filter;
    ModuleComponent comp(&filter, juce::AudioProcessorGraph::NodeID(1), editor);

    auto* cutoff = findParameterByID(&filter, "cutoff");
    ASSERT_NE(cutoff, nullptr);

    auto* cutoffSlider = findSliderByComponentId(comp, "Cutoff");
    ASSERT_NE(cutoffSlider, nullptr);

    struct SpyListener : juce::AudioProcessorParameter::Listener {
        int valueCalls = 0;
        int gestureCalls = 0;
        void parameterValueChanged(int, float) override { ++valueCalls; }
        void parameterGestureChanged(int, bool) override { ++gestureCalls; }
    } spy;
    cutoff->addListener(&spy);

    const float beforeNormalized = cutoff->getValue();

    constexpr float kReflectedNormalized = 0.7f;
    comp.reflectParameterValue(cutoff, kReflectedNormalized);

    const double expectedDenormalised = static_cast<double>(cutoff->convertFrom0to1(kReflectedNormalized));
    EXPECT_NEAR(cutoffSlider->getValue(), expectedDenormalised, 0.5)
        << "the slider must show the reflected value, denormalised through the parameter's own range";

    EXPECT_FLOAT_EQ(cutoff->getValue(), beforeNormalized) << "reflection must never write the parameter itself";
    EXPECT_EQ(spy.valueCalls, 0) << "reflection must not fire parameterValueChanged";
    EXPECT_EQ(spy.gestureCalls, 0) << "reflection must not fire a gesture";

    cutoff->removeListener(&spy);
}

// ============================================================================
// 3. RecorderHearsNothingFromReflection — an armed Touch lane bound to the reflected parameter.
// ============================================================================

TEST(AutomationUiReflectionTest, RecorderHearsNothingFromReflection) {
    AudioEngine engine;
    GraphEditor editor(engine);
    FilterModule filter;
    ModuleComponent comp(&filter, juce::AudioProcessorGraph::NodeID(1), editor);

    auto* cutoff = findParameterByID(&filter, "cutoff");
    ASSERT_NE(cutoff, nullptr);

    synth::TransportService transport;
    transport.prepare(48000.0, 512);
    synth::TimelineDoc doc;
    AppUndoManager undo;
    synth::AutomationRecorder recorder;
    recorder.attachTo(doc, undo, transport);

    synth::AutomationLane::RangeSnapshot range;
    range.minValue = 20.0f;
    range.maxValue = 20000.0f;
    range.defaultValue = 440.0f;

    const auto trackId = doc.addTrack(synth::TrackKind::Midi, "Track 1");
    const auto laneId = doc.addLane(trackId, "reflection-test-node", "cutoff", range);
    ASSERT_TRUE(laneId.isValid());
    ASSERT_TRUE(doc.setLaneRecordMode(laneId, static_cast<int>(synth::LaneRecordMode::Touch)));

    recorder.bindLane(laneId, cutoff, {});
    recorder.setGlobalRecordEnable(true);

    ASSERT_EQ(doc.getLane(laneId)->points.size(), 0u);

    comp.reflectParameterValue(cutoff, 0.7f);
    recorder.update();

    EXPECT_FALSE(recorder.getAudioState().claims.isClaimed(cutoff))
        << "reflection must never open a gesture claim — nothing ever touched the parameter";
    EXPECT_EQ(doc.getLane(laneId)->points.size(), 0u) << "an armed Touch lane must capture nothing from reflection";

    recorder.setGlobalRecordEnable(false);
}

// ============================================================================
// 4. GraphEditorDrainRoutesToTheRightModule — two live modules, events routed by NodeID.
// ============================================================================

TEST(AutomationUiReflectionTest, GraphEditorDrainRoutesToTheRightModule) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    auto filterNode = engine.getGraph().addNode(std::make_unique<FilterModule>());
    auto oscNode = engine.getGraph().addNode(std::make_unique<OscillatorModule>());
    editor.updateComponents();

    ModuleComponent* filterComp = nullptr;
    ModuleComponent* oscComp = nullptr;
    for (auto* comp : editor.getModuleComponents()) {
        if (comp->getNodeId() == filterNode->nodeID)
            filterComp = comp;
        if (comp->getNodeId() == oscNode->nodeID)
            oscComp = comp;
    }
    ASSERT_NE(filterComp, nullptr);
    ASSERT_NE(oscComp, nullptr);

    auto* cutoff = findParameterByID(filterNode->getProcessor(), "cutoff");
    auto* level = findParameterByID(oscNode->getProcessor(), "level");
    ASSERT_NE(cutoff, nullptr);
    ASSERT_NE(level, nullptr);

    auto* cutoffSlider = findSliderByComponentId(*filterComp, "Cutoff");
    auto* levelSlider = findSliderByComponentId(*oscComp, "Level");
    ASSERT_NE(cutoffSlider, nullptr);
    ASSERT_NE(levelSlider, nullptr);

    auto& feed = engine.getAutomationUiFeed();
    feed.push({filterNode->nodeID.uid, cutoff, cutoff->convertTo0to1(5000.0f)});
    feed.push({oscNode->nodeID.uid, level, level->convertTo0to1(0.25f)});
    // A stale event for a NodeID with no live component — must be discarded, not crash.
    feed.push({987654321u, nullptr, 0.5f});

    EXPECT_NO_THROW(editor.timerCallback());

    EXPECT_NEAR(cutoffSlider->getValue(), 5000.0, 1.0);
    EXPECT_NEAR(levelSlider->getValue(), 0.25, 0.01);
}

// ============================================================================
// 5. RingOverflowIsSilent — 5000 pushes into a 4096-capacity ring.
// ============================================================================

TEST(AutomationUiReflectionTest, RingOverflowIsSilent) {
    AutomationUiFeed feed;
    juce::AudioParameterFloat testParam("ringOverflowTestParam", "P", 0.0f, 1.0f, 0.0f);

    constexpr int kPushed = 5000;
    for (int i = 0; i < kPushed; ++i) {
        const float normalized = static_cast<float>(i) / static_cast<float>(kPushed - 1);
        feed.push({1u, &testParam, normalized});
    }

    std::vector<AutomationUiEvent> drained;
    EXPECT_NO_THROW(feed.drain([&](const AutomationUiEvent& e) { drained.push_back(e); }));

    EXPECT_GT(drained.size(), 0u);
    EXPECT_LE(drained.size(), static_cast<std::size_t>(AutomationUiFeed::kCapacity))
        << "the ring must never report more events than its capacity";

    // No corruption: the ring can only ever have dropped THE NEWEST pushes once full, never
    // reordered or duplicated what made it in — so whatever was observed is a strictly increasing
    // run of the sequence push() was fed, and the last one observed is the value a real reflect
    // would apply.
    for (std::size_t i = 1; i < drained.size(); ++i)
        EXPECT_GT(drained[i].newNormalized, drained[i - 1].newNormalized);

    int secondDrainCount = 0;
    feed.drain([&](const AutomationUiEvent&) { ++secondDrainCount; });
    EXPECT_EQ(secondDrainCount, 0) << "a second drain against an empty ring must be a safe no-op";
}

#endif // SYNTH_ENABLE_TIMELINE
