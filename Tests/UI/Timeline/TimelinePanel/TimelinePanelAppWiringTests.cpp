// TimelinePanelAppWiringTests.cpp
//
// The app-level timeline wiring MainComponent owns: publish-on-mutation, the compound
// add-track flow, auto-wire ambiguity, binding-chip states + one-click rebind, reconciliation
// after an undo/redo restore, New Patch, .agsproj round trips, recorder wiring, and the P key's
// loop-selection shortcut. Uses the TimelineAppWiringTest fixture from
// TimelinePanelTestFixture.h. findNodeOfType/countNodesOfType/countMidiConnections/
// hasMidiConnection below are local to this file's tests.

#include "AI/AIProvider.h"
#include "AI/AIStateMapper/AIStateMapper.h"
#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "MainComponent/MainComponent.h"
#include "ProjectBundle.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "TimelinePanelTestFixture.h"
#include "Transport/TransportService.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Theme/BuiltInThemes.h"
#include "UI/Timeline/EdgeAutoScroll.h"
#include "UI/Timeline/TimelinePanelComponent/TimelinePanelComponent.h"
#include "UI/Timeline/TrackColour.h"
#include "UserSettings.h"
#include <algorithm>
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace {

using synth::TrackKind;
using synth::ui::TimelineTrackHeaderComponent;

juce::AudioProcessorGraph::Node* findNodeOfType(juce::AudioProcessorGraph& graph, ModuleType type) {
    for (auto* node : graph.getNodes()) {
        if (node == nullptr)
            continue;
        if (auto* module = dynamic_cast<ModuleBase*>(node->getProcessor()))
            if (module->getModuleType() == type)
                return node;
    }
    return nullptr;
}

int countNodesOfType(juce::AudioProcessorGraph& graph, ModuleType type) {
    int count = 0;
    for (auto* node : graph.getNodes())
        if (node != nullptr)
            if (auto* module = dynamic_cast<ModuleBase*>(node->getProcessor()))
                if (module->getModuleType() == type)
                    ++count;
    return count;
}

int countMidiConnections(juce::AudioProcessorGraph& graph) {
    int count = 0;
    for (const auto& connection : graph.getConnections())
        if (connection.source.isMIDI())
            ++count;
    return count;
}

bool hasMidiConnection(juce::AudioProcessorGraph& graph, juce::AudioProcessorGraph::NodeID from,
                       juce::AudioProcessorGraph::NodeID to) {
    for (const auto& connection : graph.getConnections())
        if (connection.source.isMIDI() && connection.source.nodeID == from && connection.destination.nodeID == to)
            return true;
    return false;
}

} // namespace

// ---- 1. Publish-on-mutation ----

TEST_F(TimelineAppWiringTest, PublishOnMutation) {
    MainComponent mc(std::make_unique<MockProviderTL>());
    quiesceEngine(mc);

    auto& engine = mc.getAudioEngine();
    EXPECT_EQ(engine.getTimelineSnapshots().beginAudioBlock().tracks.size(), 0u);

    auto& doc = mc.getTimelineDoc();
    const auto trackId = doc.addTrack(TrackKind::Midi, "Track 1");
    ASSERT_TRUE(trackId.isValid());
    doc.setTrackBinding(trackId, "uuid-under-test");
    doc.setTrackMuted(trackId, true);

    // No explicit publish call anywhere above: the doc notified, and MainComponent republished.
    const auto& snapshot = engine.getTimelineSnapshots().beginAudioBlock();
    ASSERT_EQ(snapshot.tracks.size(), 1u);
    EXPECT_STREQ(snapshot.tracks[0].bindingUuid, "uuid-under-test");
    EXPECT_TRUE(snapshot.tracks[0].muted);

    doc.removeTrack(trackId);
    EXPECT_EQ(engine.getTimelineSnapshots().beginAudioBlock().tracks.size(), 0u);
}

// ---- 2. The add-track flow: one node, one track, one wire, ONE undo step ----

TEST_F(TimelineAppWiringTest, AddMidiTrackFlowCompoundUndo) {
    MainComponent mc(std::make_unique<MockProviderTL>());
    mc.setSize(1600, 900);
    quiesceEngine(mc);
    prepareCanvas(mc, 1);

    auto& graph = mc.getAudioEngine().getGraph();
    auto& doc = mc.getTimelineDoc();
    auto* polyMidi = findNodeOfType(graph, ModuleType::PolyMidi);
    ASSERT_NE(polyMidi, nullptr);

    mc.simulateAddMidiTrackClick();

    // The graph half.
    ASSERT_EQ(countNodesOfType(graph, ModuleType::TimelineMidiSource), 1);
    auto* trackIn = findNodeOfType(graph, ModuleType::TimelineMidiSource);
    ASSERT_NE(trackIn, nullptr);
    const juce::String uuid = trackIn->properties["uuid"].toString();
    EXPECT_TRUE(uuid.isNotEmpty()) << "the flow must assign a uuid — the binding keys on it";
    if (auto* module = dynamic_cast<ModuleBase*>(trackIn->getProcessor()))
        EXPECT_EQ(juce::String(module->getNodeUuid()), uuid) << "and mirror it into the processor";
    EXPECT_TRUE(hasMidiConnection(graph, trackIn->nodeID, polyMidi->nodeID));

    // The timeline half.
    ASSERT_EQ(doc.getTracks().size(), 1u);
    EXPECT_EQ(doc.getTracks()[0].name, "Track 1");
    EXPECT_EQ(doc.getTracks()[0].bindingUuid, uuid);
    EXPECT_FALSE(doc.getTracks()[0].orphaned);
    EXPECT_EQ(doc.getTracks()[0].colourArgb, synth::ui::trackPaletteColour(0).getARGB());

    // The view half.
    ASSERT_EQ(mc.getTimelinePanel().getTrackHeaderCount(), 1);
    EXPECT_FALSE(mc.getTimelinePanel().getTrackHeaderAt(0)->isBindingChipWarning());

    // ONE undo takes all of it back.
    ASSERT_TRUE(mc.getUndoManager().canUndo());
    mc.getUndoManager().undo();
    EXPECT_TRUE(doc.isEmpty());
    EXPECT_EQ(countNodesOfType(graph, ModuleType::TimelineMidiSource), 0);
    EXPECT_EQ(countMidiConnections(graph), 0);
    EXPECT_EQ(mc.getTimelinePanel().getTrackHeaderCount(), 0);

    // ...and redo restores both halves, with the SAME uuid, so the binding still resolves.
    mc.getUndoManager().redo();
    ASSERT_EQ(doc.getTracks().size(), 1u);
    auto* restored = findNodeOfType(graph, ModuleType::TimelineMidiSource);
    ASSERT_NE(restored, nullptr);
    EXPECT_EQ(restored->properties["uuid"].toString(), uuid);
    EXPECT_EQ(doc.getTracks()[0].bindingUuid, uuid);
    EXPECT_FALSE(doc.getTracks()[0].orphaned) << "the post-restore reconcile re-derived this";
}

TEST_F(TimelineAppWiringTest, AddTrackAtTheCapAddsNoNode) {
    // The doc refuses a track past kMaxTracks. The node has to be created AFTER that refusal is
    // known, or the graph keeps a Track In / Track Audio node no track will ever play through —
    // recordCombinedChange records the mutation, it does not roll one back.
    MainComponent mc(std::make_unique<MockProviderTL>());
    mc.setSize(1600, 900);
    quiesceEngine(mc);
    prepareCanvas(mc, 1);

    auto& doc = mc.getTimelineDoc();
    auto& graph = mc.getAudioEngine().getGraph();
    while ((int)doc.getTracks().size() < synth::TimelineDoc::kMaxTracks)
        ASSERT_TRUE(doc.addTrack(TrackKind::Midi, "Filler").isValid());

    const int nodesBefore = graph.getNumNodes();

    mc.simulateAddMidiTrackClick();
    EXPECT_EQ((int)doc.getTracks().size(), synth::TimelineDoc::kMaxTracks);
    EXPECT_EQ(countNodesOfType(graph, ModuleType::TimelineMidiSource), 0)
        << "a refused MIDI track must leave no orphan Track In node";
    EXPECT_EQ(graph.getNumNodes(), nodesBefore);

    mc.simulateAddAudioTrackClick();
    EXPECT_EQ((int)doc.getTracks().size(), synth::TimelineDoc::kMaxTracks);
    EXPECT_EQ(countNodesOfType(graph, ModuleType::TimelineAudioSource), 0)
        << "a refused audio track must leave no orphan Track Audio node";
    EXPECT_EQ(graph.getNumNodes(), nodesBefore);

    // Nothing changed in either domain, so there is no undo step to take back either.
    EXPECT_FALSE(mc.getUndoManager().canUndo());
}

// ---- 3. Auto-wire only when the target is unambiguous ----

TEST_F(TimelineAppWiringTest, AmbiguousTargetNoAutoWire) {
    MainComponent mc(std::make_unique<MockProviderTL>());
    mc.setSize(1600, 900);
    quiesceEngine(mc);
    prepareCanvas(mc, 2);

    auto& graph = mc.getAudioEngine().getGraph();
    ASSERT_EQ(countNodesOfType(graph, ModuleType::PolyMidi), 2);
    ASSERT_EQ(countMidiConnections(graph), 0);

    mc.simulateAddMidiTrackClick();

    // The node and the track are still created and bound — only the CABLE is left to the user.
    EXPECT_EQ(countNodesOfType(graph, ModuleType::TimelineMidiSource), 1);
    ASSERT_EQ(mc.getTimelineDoc().getTracks().size(), 1u);
    EXPECT_TRUE(mc.getTimelineDoc().getTracks()[0].bindingUuid.isNotEmpty());
    EXPECT_EQ(countMidiConnections(graph), 0) << "two candidates: guessing one would be worse than no wire";
}

TEST_F(TimelineAppWiringTest, NoTargetNoAutoWire) {
    MainComponent mc(std::make_unique<MockProviderTL>());
    mc.setSize(1600, 900);
    quiesceEngine(mc);
    prepareCanvas(mc, 0);

    mc.simulateAddMidiTrackClick();

    auto& graph = mc.getAudioEngine().getGraph();
    EXPECT_EQ(countNodesOfType(graph, ModuleType::TimelineMidiSource), 1);
    EXPECT_EQ(countMidiConnections(graph), 0);
    EXPECT_EQ(mc.getTimelineDoc().getTracks().size(), 1u);
}

// ---- 4. Binding chip states end to end, and the one-click re-bind ----

TEST_F(TimelineAppWiringTest, BindingChipGoesAmberWhenTheNodeIsDeletedAndRebindsInOneClick) {
    MainComponent mc(std::make_unique<MockProviderTL>());
    mc.setSize(1600, 900);
    quiesceEngine(mc);
    prepareCanvas(mc, 1);
    mc.simulateAddMidiTrackClick();

    auto& graph = mc.getAudioEngine().getGraph();
    auto& doc = mc.getTimelineDoc();
    auto& panel = mc.getTimelinePanel();
    auto* trackIn = findNodeOfType(graph, ModuleType::TimelineMidiSource);
    ASSERT_NE(trackIn, nullptr);
    const juce::String originalUuid = trackIn->properties["uuid"].toString();
    const auto trackInNodeId = trackIn->nodeID;

    ASSERT_EQ(panel.getTrackHeaderCount(), 1);
    EXPECT_TRUE(panel.getTrackHeaderAt(0)->getBindingChipText().startsWith("Track In"));

    // Delete the node the way the canvas does (recordStructuralChange — NOT an undo restore, so
    // this is the path the onGraphStructureChanged safety net exists for).
    mc.getGraphEditor().requestDeleteModule(trackInNodeId);

    ASSERT_EQ(doc.getTracks().size(), 1u);
    EXPECT_TRUE(doc.getTracks()[0].orphaned) << "reconciled with no explicit call from this test";
    EXPECT_EQ(doc.getTracks()[0].bindingUuid, originalUuid) << "an orphaned binding is retained";
    ASSERT_EQ(panel.getTrackHeaderCount(), 1);
    EXPECT_EQ(panel.getTrackHeaderAt(0)->getBindingChipText(), "Missing");
    EXPECT_TRUE(panel.getTrackHeaderAt(0)->isBindingChipWarning());

    // One click on the chip menu's "New Track In node" recovers it.
    panel.getTrackHeaderAt(0)->applyBindingMenuChoice(TimelineTrackHeaderComponent::kNewTrackInNodeMenuId);

    auto* replacement = findNodeOfType(graph, ModuleType::TimelineMidiSource);
    ASSERT_NE(replacement, nullptr);
    const juce::String newUuid = replacement->properties["uuid"].toString();
    EXPECT_NE(newUuid, originalUuid);
    EXPECT_EQ(doc.getTracks()[0].bindingUuid, newUuid);
    EXPECT_FALSE(doc.getTracks()[0].orphaned);
    EXPECT_FALSE(panel.getTrackHeaderAt(0)->isBindingChipWarning());
}

TEST_F(TimelineAppWiringTest, ChipMenuOffersOnlyUnclaimedTrackInNodes) {
    MainComponent mc(std::make_unique<MockProviderTL>());
    mc.setSize(1600, 900);
    quiesceEngine(mc);
    prepareCanvas(mc, 1);

    mc.simulateAddMidiTrackClick();
    mc.simulateAddMidiTrackClick();

    auto& panel = mc.getTimelinePanel();
    ASSERT_EQ(panel.getTrackHeaderCount(), 2);
    ASSERT_EQ(countNodesOfType(mc.getAudioEngine().getGraph(), ModuleType::TimelineMidiSource), 2);

    // Two Track In nodes exist, but the OTHER track already claims one of them.
    const auto options = panel.getTrackHeaderAt(0)->collectBindingOptions();
    ASSERT_EQ(options.size(), 1u);
    EXPECT_EQ(options[0].uuid, mc.getTimelineDoc().getTracks()[0].bindingUuid)
        << "a track's own binding stays on its menu; another track's does not";
}

// ---- 5. A restore re-derives the orphan flags ----

TEST_F(TimelineAppWiringTest, UndoRestoreReconciles) {
    MainComponent mc(std::make_unique<MockProviderTL>());
    mc.setSize(1600, 900);
    quiesceEngine(mc);
    prepareCanvas(mc, 1);
    mc.simulateAddMidiTrackClick();

    auto& graph = mc.getAudioEngine().getGraph();
    auto& doc = mc.getTimelineDoc();
    auto* trackIn = findNodeOfType(graph, ModuleType::TimelineMidiSource);
    ASSERT_NE(trackIn, nullptr);

    mc.getGraphEditor().requestDeleteModule(trackIn->nodeID);
    ASSERT_EQ(doc.getTracks().size(), 1u);
    ASSERT_TRUE(doc.getTracks()[0].orphaned);

    // Undo brings the node back. NOTHING in this test reconciles by hand — the AppUndoManager's
    // post-restore hook does it.
    mc.getUndoManager().undo();
    ASSERT_EQ(doc.getTracks().size(), 1u);
    EXPECT_FALSE(doc.getTracks()[0].orphaned);
    EXPECT_FALSE(mc.getTimelinePanel().getTrackHeaderAt(0)->isBindingChipWarning());
}

// ---- 6. New Patch empties the timeline too ----

TEST_F(TimelineAppWiringTest, NewPatchClearsTheTimeline) {
    MainComponent mc(std::make_unique<MockProviderTL>());
    mc.setSize(1600, 900);
    quiesceEngine(mc);
    prepareCanvas(mc, 1);
    mc.simulateAddMidiTrackClick();
    ASSERT_EQ(mc.getTimelineDoc().getTracks().size(), 1u);

    // invokeDirectly(..., false) runs the command synchronously — the toolbar button posts it
    // asynchronously, which a headless test has no message loop to deliver.
    mc.getCommandManager().invokeDirectly(AppCommands::newPatch, false);

    EXPECT_TRUE(mc.getTimelineDoc().isEmpty());
    EXPECT_EQ(countNodesOfType(mc.getAudioEngine().getGraph(), ModuleType::TimelineMidiSource), 0);
    EXPECT_EQ(mc.getTimelinePanel().getTrackHeaderCount(), 0);
    EXPECT_EQ(mc.getAudioEngine().getTimelineSnapshots().beginAudioBlock().tracks.size(), 0u);
}

// ---- 7. .agsproj round trip, and .json unaffected ----

TEST_F(TimelineAppWiringTest, AgsprojRoundTripThroughMainComponent) {
    const juce::File scratch = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                   .getChildFile("agentsynth-tl53-" + juce::Uuid().toDashedString());
    ASSERT_TRUE(scratch.createDirectory());
    const juce::File bundle = scratch.getChildFile("Project.agsproj");
    const juce::File preset = scratch.getChildFile("Preset.json");

    {
        MainComponent mc(std::make_unique<MockProviderTL>());
        mc.setSize(1600, 900);
        quiesceEngine(mc);
        prepareCanvas(mc, 1);
        mc.simulateAddMidiTrackClick();

        auto& doc = mc.getTimelineDoc();
        auto& graph = mc.getAudioEngine().getGraph();
        const juce::String savedUuid = doc.getTracks()[0].bindingUuid;

        mc.saveProjectForTest(bundle);
        ASSERT_TRUE(synth::ProjectBundle::isBundle(bundle));

        // Mutate after saving — none of this may survive the reopen.
        doc.addTrack(TrackKind::Midi, "Scratch");
        mc.getGraphEditor().addModuleAtCanvasPosition("Reverb", {900, 500}, {});
        ASSERT_EQ(doc.getTracks().size(), 2u);

        ASSERT_TRUE(mc.openProjectForTest(bundle));

        ASSERT_EQ(doc.getTracks().size(), 1u);
        EXPECT_EQ(doc.getTracks()[0].bindingUuid, savedUuid);
        EXPECT_FALSE(doc.getTracks()[0].orphaned) << "reconciled against the reloaded graph";
        EXPECT_EQ(countNodesOfType(graph, ModuleType::Reverb), 0) << "the graph came back too";
        auto* reloaded = findNodeOfType(graph, ModuleType::TimelineMidiSource);
        ASSERT_NE(reloaded, nullptr);
        EXPECT_EQ(reloaded->properties["uuid"].toString(), savedUuid);

        // Published, and the header column rebuilt from the loaded doc.
        EXPECT_EQ(mc.getAudioEngine().getTimelineSnapshots().beginAudioBlock().tracks.size(), 1u);
        EXPECT_EQ(mc.getTimelinePanel().getTrackHeaderCount(), 1);

        // A plain .json save is untouched by any of this: no "timeline" key, and loading one back
        // leaves the live timeline exactly as it was.
        mc.saveProjectForTest(preset);
        ASSERT_TRUE(preset.existsAsFile());
        const juce::var json = juce::JSON::parse(preset);
        ASSERT_TRUE(json.isObject());
        EXPECT_FALSE(json.getDynamicObject()->hasProperty("timeline"));

        doc.addTrack(TrackKind::Midi, "Kept");
        ASSERT_EQ(doc.getTracks().size(), 2u);
        ASSERT_TRUE(mc.openProjectForTest(preset));
        EXPECT_EQ(doc.getTracks().size(), 2u) << "a .json preset carries no timeline, so it clears none";
    }

    scratch.deleteRecursively();
}

namespace {

/** Records the clip streamer's asset root at every doc notification. The one that matters fires
 *  from INSIDE ProjectBundle::load (fromVar moves the timeline into the live doc, which publishes
 *  synchronously) — long before the load returns. */
class AssetRootWatcher : public synth::TimelineDoc::Listener {
public:
    AssetRootWatcher(synth::TimelineDoc& doc, AudioEngine& engine)
        : doc_(doc)
        , engine_(engine) {
        doc_.addListener(this);
    }
    ~AssetRootWatcher() override { doc_.removeListener(this); }

    void timelineChanged(const synth::TimelineDoc&) override {
        if (notifications++ == 0)
            firstRoot = engine_.getAudioClipStreamer().getBundleRoot();
        lastRoot = engine_.getAudioClipStreamer().getBundleRoot();
    }

    juce::File firstRoot;
    juce::File lastRoot;
    int notifications = 0;

private:
    synth::TimelineDoc& doc_;
    AudioEngine& engine_;
};

/** A bundle with one audio track whose clip names `assetName`, and that file physically present in
 *  its Audio/ folder. Content is irrelevant — nothing here decodes it. */
void writeBundleWithClip(MainComponent& mc, const juce::File& bundleDir, const juce::String& assetName) {
    auto& doc = mc.getTimelineDoc();
    doc.clear();
    const auto trackId = doc.addTrack(TrackKind::Audio, "Audio 1");
    ASSERT_TRUE(trackId.isValid());
    const auto clipId = doc.addClip(trackId, 0.0, 4.0, assetName);
    ASSERT_TRUE(clipId.isValid());
    ASSERT_TRUE(doc.setClipAsset(clipId, "Audio/" + assetName, 0.0));

    mc.saveProjectForTest(bundleDir);
    ASSERT_TRUE(synth::ProjectBundle::isBundle(bundleDir));
    bundleDir.getChildFile(synth::ProjectBundle::kAudioSubdirName)
        .getChildFile(assetName)
        .replaceWithText("not really audio");
}

} // namespace

TEST_F(TimelineAppWiringTest, OpeningABundlePublishesAgainstItsOwnAssetRoots) {
    const juce::File scratch = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                   .getChildFile("agentsynth-bundle-roots-" + juce::Uuid().toDashedString());
    ASSERT_TRUE(scratch.createDirectory());

    const juce::File bundleA = scratch.getChildFile("A.agsproj");
    const juce::File bundleB = scratch.getChildFile("B.agsproj");

    {
        MainComponent mc(std::make_unique<MockProviderTL>());
        mc.setSize(1600, 900);
        quiesceEngine(mc);
        prepareCanvas(mc, 0);

        writeBundleWithClip(mc, bundleA, "a.wav");
        writeBundleWithClip(mc, bundleB, "b.wav");

        auto& engine = mc.getAudioEngine();
        ASSERT_TRUE(mc.openProjectForTest(bundleA));
        ASSERT_EQ(engine.getAudioClipStreamer().getBundleRoot(), bundleA);

        // Opening B: the publish that fires from inside the load must already see B's root. With
        // the roots still on A, B's "Audio/b.wav" resolves inside A — the wrong file, or silence.
        {
            AssetRootWatcher watcher(mc.getTimelineDoc(), engine);
            ASSERT_TRUE(mc.openProjectForTest(bundleB));
            ASSERT_GT(watcher.notifications, 0) << "the load never notified, so this proves nothing";
            EXPECT_EQ(watcher.firstRoot, bundleB) << "the load published against the PREVIOUS bundle's roots";
        }
        EXPECT_EQ(engine.getAudioClipStreamer().getBundleRoot(), bundleB);
        EXPECT_EQ(engine.getAudioClipStreamer().resolveAssetRef("Audio/b.wav"),
                  bundleB.getChildFile("Audio").getChildFile("b.wav"));

        // A failed load is all-or-nothing, roots included: the still-open project stays on B.
        const juce::File corrupt = scratch.getChildFile("Corrupt.agsproj");
        ASSERT_TRUE(corrupt.createDirectory());
        corrupt.getChildFile(synth::ProjectBundle::kProjectFileName).replaceWithText("{ not json at all");

        const auto tracksBefore = mc.getTimelineDoc().getTracks().size();
        EXPECT_FALSE(mc.openProjectForTest(corrupt));
        EXPECT_EQ(engine.getAudioClipStreamer().getBundleRoot(), bundleB)
            << "a refused load must leave the previous project's asset roots in place";
        EXPECT_EQ(mc.getTimelineDoc().getTracks().size(), tracksBefore);
    }

    scratch.deleteRecursively();
}

// ---- 8. Recorder wiring, and the programmatic-apply guard ----

TEST_F(TimelineAppWiringTest, RecorderCapturesAGestureAndAProgrammaticLoadRecordsNothing) {
    MainComponent mc(std::make_unique<MockProviderTL>());
    mc.setSize(1600, 900);
    quiesceEngine(mc);

    auto& graphEditor = mc.getGraphEditor();
    graphEditor.newPatch();
    graphEditor.addModuleAtCanvasPosition("Filter", {200, 200}, {});
    mc.getUndoManager().clearUndoHistory();

    auto& graph = mc.getAudioEngine().getGraph();
    // graphToJSON assigns the lazily-generated uuids; the add-track flow does it for its own node,
    // but an ordinary module only gets one when the graph is next serialised.
    juce::ignoreUnused(synth::AIStateMapper::graphToJSON(graph));

    auto* filterNode = findNodeOfType(graph, ModuleType::Filter);
    ASSERT_NE(filterNode, nullptr);
    const juce::String filterUuid = filterNode->properties["uuid"].toString();
    ASSERT_TRUE(filterUuid.isNotEmpty());
    auto* cutoff = findParameterByID(filterNode->getProcessor(), "cutoff");
    ASSERT_NE(cutoff, nullptr);

    // A Touch lane on that parameter. Adding it is the ONLY wiring step: the doc notifies,
    // MainComponent republishes, and the recorder's bindings are rebuilt from the same resolution.
    auto& doc = mc.getTimelineDoc();
    const auto trackId = doc.addTrack(TrackKind::Midi, "Track 1");
    synth::AutomationLane::RangeSnapshot range;
    range.minValue = cutoff->getNormalisableRange().start;
    range.maxValue = cutoff->getNormalisableRange().end;
    range.defaultValue = cutoff->convertFrom0to1(cutoff->getDefaultValue());
    const auto laneId = doc.addLane(trackId, filterUuid, "cutoff", range);
    ASSERT_TRUE(laneId.isValid());
    ASSERT_TRUE(doc.setLaneRecordMode(laneId, (int)synth::LaneRecordMode::Touch));

    auto& recorder = mc.getAutomationRecorder();
    EXPECT_EQ(recorder.getNumBindings(), 1) << "publish-on-change rebound the recorder";

    auto& transport = mc.getAudioEngine().getTransport();
    recorder.setGlobalRecordEnable(true);
    ASSERT_TRUE(transport.play());
    transport.tick(512);
    ASSERT_TRUE(transport.getPositionSnapshot().playing);
    recorder.update();

    // The gesture. Ticking between the two writes puts the captured points on different beats.
    cutoff->beginChangeGesture();
    cutoff->setValueNotifyingHost(0.25f);
    transport.tick(512);
    cutoff->setValueNotifyingHost(0.75f);
    cutoff->endChangeGesture();
    recorder.update();

    const auto* lane = doc.getLane(laneId);
    ASSERT_NE(lane, nullptr);
    ASSERT_FALSE(lane->points.empty()) << "a Touch gesture on a bound parameter must be captured";
    const auto capturedPoints = lane->points.size();

    // A factory preset load pushes a whole patch's worth of parameter values. It must record
    // nothing — and it runs inside a ScopedProgrammaticApply, which this observes from the
    // graph-structure callback that fires while the load is still in progress.
    bool suspendedDuringLoad = false;
    graphEditor.onGraphStructureChanged = [&] { suspendedDuringLoad = recorder.isSuspended(); };
    mc.simulateLoadFactoryPresetForTest(0);
    EXPECT_TRUE(suspendedDuringLoad) << "the preset load must run inside a programmatic-apply scope";

    const auto* laneAfter = doc.getLane(laneId);
    ASSERT_NE(laneAfter, nullptr);
    EXPECT_EQ(laneAfter->points.size(), capturedPoints) << "nothing the preset wrote was captured";
    EXPECT_TRUE(laneAfter->orphaned) << "the lane's node is gone: orphaned and retained, never deleted";

    recorder.setGlobalRecordEnable(false);
    transport.stop();
    transport.tick(512);
}

// ---- "P" on the clip lanes points the transport's loop at the selected clips ----

TEST_F(TimelineAppWiringTest, LoopSelectionKeySetsTransportLoop) {
    MainComponent mc(std::make_unique<MockProviderTL>());
    mc.setSize(1600, 900);
    quiesceEngine(mc);

    auto& doc = mc.getTimelineDoc();
    const auto trackId = doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipA = doc.addClip(trackId, 4.0, 4.0, "A");
    const auto clipB = doc.addClip(trackId, 12.0, 2.0, "B");
    ASSERT_TRUE(clipA.isValid());
    ASSERT_TRUE(clipB.isValid());

    auto& panel = mc.getTimelinePanel();
    auto& transport = mc.getAudioEngine().getTransport();
    ASSERT_FALSE(transport.getPositionSnapshot().looping);

    // Nothing selected: the key falls through and the transport is untouched.
    EXPECT_FALSE(panel.getClipLaneArea().keyPressed(juce::KeyPress('p')));
    transport.tick(512);
    EXPECT_FALSE(transport.getPositionSnapshot().looping);

    // Two clips selected -> the loop spans both, and looping is switched ON by the same gesture.
    panel.getClipSelection().setSelection({clipA, clipB});
    EXPECT_TRUE(panel.getClipLaneArea().keyPressed(juce::KeyPress('p')));
    transport.tick(512);

    const auto snap = transport.getPositionSnapshot();
    EXPECT_TRUE(snap.looping);
    EXPECT_DOUBLE_EQ(snap.loopStartPpq, 4.0);
    EXPECT_DOUBLE_EQ(snap.loopEndPpq, 14.0);
}
