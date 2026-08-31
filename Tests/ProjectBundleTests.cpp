#include "../Source/AI/AIStateMapper.h"
#include "../Source/Branding.h"
#include "../Source/Modules/FilterModule.h"
#include "../Source/Modules/OscillatorModule.h"
#include "../Source/Modules/VCAModule.h"
#include "../Source/PatchDocument.h"
#include "../Source/ProjectBundle.h"
#include "../Source/Timeline/TimelineDoc.h"
#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <map>
#include <set>

// ProjectBundle (.agsproj): project.json = patch dialect + "timeline" key, Audio/ and
// Peaks/ asset subdirectories. See docs/architecture.md §5 for the fixed load order this pins.

using synth::AutomationLane;
using synth::BreakpointCurve;
using synth::MidiNote;
using synth::PatchDocument;
using synth::ProjectBundle;
using synth::ProjectLoadResult;
using synth::TimelineDoc;
using synth::TrackKind;

namespace {

void buildSampleGraph(juce::AudioProcessorGraph& graph) {
    graph.clear();
    auto osc = graph.addNode(std::make_unique<OscillatorModule>());
    osc->properties.set("x", 0);
    osc->properties.set("y", 0);
    auto filter = graph.addNode(std::make_unique<FilterModule>());
    filter->properties.set("x", 300);
    filter->properties.set("y", 0);
    auto vca = graph.addNode(std::make_unique<VCAModule>());
    vca->properties.set("x", 600);
    vca->properties.set("y", 0);

    ASSERT_TRUE(graph.addConnection({{osc->nodeID, 0}, {filter->nodeID, 0}}));
    ASSERT_TRUE(graph.addConnection({{filter->nodeID, 0}, {vca->nodeID, 0}}));
}

void buildSampleTimeline(TimelineDoc& doc) {
    auto track = doc.addTrack(TrackKind::Midi, "Lead");
    auto clip = doc.addClip(track, 0.0, 4.0, "Clip A");
    ASSERT_TRUE(clip.isValid());

    MidiNote note;
    note.startBeat = 0.0;
    note.lengthBeats = 1.0;
    note.pitch = 60;
    note.velocity = 100;
    note.channel = 1;
    ASSERT_TRUE(doc.addNote(clip, note).isValid());

    AutomationLane::RangeSnapshot range;
    range.minValue = 0.0f;
    range.maxValue = 1.0f;
    range.defaultValue = 0.5f;
    auto lane = doc.addLane(track, "some-node-uuid", "cutoff", range);
    ASSERT_TRUE(lane.isValid());
    ASSERT_TRUE(doc.addBreakpoint(lane, 0.0, 0.2, 0.0f, static_cast<int>(BreakpointCurve::Linear)));
    ASSERT_TRUE(doc.addBreakpoint(lane, 2.0, 0.8, 0.0f, static_cast<int>(BreakpointCurve::Linear)));
}

std::set<juce::String> nodeUuids(juce::AudioProcessorGraph& graph) {
    std::set<juce::String> uuids;
    for (auto* node : graph.getNodes())
        uuids.insert(node->properties["uuid"].toString());
    return uuids;
}

// A known-only patch (schemaVersion/nodes/connections), the same base PatchDocumentTests.cpp
// uses, for probing what a PatchDocument stash merges into a fresh save.
juce::var makeKnownOnlyPatch() {
    auto* root = new juce::DynamicObject();
    root->setProperty("schemaVersion", 1);
    root->setProperty("nodes", juce::var(juce::Array<juce::var>()));
    root->setProperty("connections", juce::var(juce::Array<juce::var>()));
    return juce::var(root);
}

} // namespace

class ProjectBundleTest : public ::testing::Test {
protected:
    void SetUp() override {
        root = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("agentsynth-projectbundle-tests");
        root.deleteRecursively();
        root.createDirectory();
    }
    void TearDown() override { root.deleteRecursively(); }

    juce::File bundleDir(const juce::String& name) { return root.getChildFile(name + ProjectBundle::kBundleExtension); }

    juce::File root;
};

TEST_F(ProjectBundleTest, SaveCreatesBundleStructure) {
    juce::AudioProcessorGraph graph;
    buildSampleGraph(graph);
    TimelineDoc timeline;
    buildSampleTimeline(timeline);
    PatchDocument patchDocument;

    auto dir = bundleDir("Save");
    auto result = ProjectBundle::save(dir, graph, timeline, patchDocument);
    ASSERT_TRUE(result.ok) << result.message;

    EXPECT_TRUE(dir.isDirectory());
    auto projectFile = dir.getChildFile(ProjectBundle::kProjectFileName);
    EXPECT_TRUE(projectFile.existsAsFile());
    EXPECT_TRUE(dir.getChildFile(ProjectBundle::kAudioSubdirName).isDirectory());
    EXPECT_TRUE(dir.getChildFile(ProjectBundle::kPeaksSubdirName).isDirectory());
    EXPECT_TRUE(ProjectBundle::isBundle(dir));

    auto json = juce::JSON::parse(projectFile);
    ASSERT_TRUE(json.isObject());
    EXPECT_TRUE(json.hasProperty("nodes"));
    EXPECT_TRUE(json.hasProperty("connections"));
    EXPECT_TRUE(json.hasProperty("schemaVersion"));
    EXPECT_TRUE(json.hasProperty("timeline"));
}

TEST_F(ProjectBundleTest, RoundTripGraphAndTimeline) {
    juce::AudioProcessorGraph originalGraph;
    buildSampleGraph(originalGraph);
    TimelineDoc originalTimeline;
    buildSampleTimeline(originalTimeline);
    PatchDocument originalPatchDoc;

    auto dir = bundleDir("RoundTrip");
    ASSERT_TRUE(ProjectBundle::save(dir, originalGraph, originalTimeline, originalPatchDoc).ok);

    // save() assigns/persists node uuids via graphToJSON as a side effect — capture AFTER save.
    auto originalJson = synth::AIStateMapper::graphToJSON(originalGraph);
    auto originalTimelineJson = juce::JSON::toString(originalTimeline.toVar());
    auto originalUuids = nodeUuids(originalGraph);
    ASSERT_EQ(originalUuids.size(), 3u);

    juce::AudioProcessorGraph freshGraph;
    TimelineDoc freshTimeline;
    PatchDocument freshPatchDoc;
    auto result = ProjectBundle::load(dir, freshGraph, freshTimeline, freshPatchDoc);
    ASSERT_TRUE(result.ok) << result.message;

    EXPECT_EQ(freshGraph.getNumNodes(), originalGraph.getNumNodes());
    EXPECT_EQ(freshGraph.getConnections().size(), originalGraph.getConnections().size());

    // Node uuids are stable across the round trip even though integer node ids may not be.
    EXPECT_EQ(nodeUuids(freshGraph), originalUuids);

    // Compare node type + params keyed by uuid (not by the possibly-renumbered integer id).
    auto byUuid = [](const juce::var& json) {
        std::map<juce::String, juce::var> result;
        if (auto* nodes = json.getProperty("nodes", {}).getArray())
            for (auto& n : *nodes)
                result[n.getProperty("uuid", {}).toString()] = n;
        return result;
    };
    auto freshJson = synth::AIStateMapper::graphToJSON(freshGraph);
    auto freshByUuid = byUuid(freshJson);
    auto originalByUuid = byUuid(originalJson);
    ASSERT_EQ(freshByUuid.size(), originalByUuid.size());
    for (const auto& [uuid, node] : originalByUuid) {
        ASSERT_TRUE(freshByUuid.count(uuid) > 0) << uuid;
        const auto& freshNode = freshByUuid[uuid];
        EXPECT_EQ(freshNode.getProperty("type", {}).toString(), node.getProperty("type", {}).toString());
        EXPECT_EQ(juce::JSON::toString(freshNode.getProperty("params", {})),
                  juce::JSON::toString(node.getProperty("params", {})));
    }

    // Timeline: deep-equal via toVar's JSON string.
    EXPECT_EQ(juce::JSON::toString(freshTimeline.toVar()), originalTimelineJson);
}

TEST_F(ProjectBundleTest, LoadOrderIsAllOrNothing_BadPatch) {
    auto dir = bundleDir("BadPatch");
    dir.createDirectory();

    // Hand-write a project.json: corrupt patch (unknown module type) + otherwise well-formed
    // timeline, so a failure here can only be attributed to the patch gate.
    juce::DynamicObject::Ptr rootObj = new juce::DynamicObject();
    rootObj->setProperty("schemaVersion", 1);
    juce::Array<juce::var> nodes;
    juce::DynamicObject::Ptr badNode = new juce::DynamicObject();
    badNode->setProperty("id", 1);
    badNode->setProperty("type", "NotARealModuleType");
    nodes.add(juce::var(badNode.get()));
    rootObj->setProperty("nodes", juce::var(nodes));
    rootObj->setProperty("connections", juce::var(juce::Array<juce::var>()));

    TimelineDoc timelineForJson;
    buildSampleTimeline(timelineForJson);
    rootObj->setProperty("timeline", timelineForJson.toVar());

    dir.getChildFile(ProjectBundle::kProjectFileName).replaceWithText(juce::JSON::toString(juce::var(rootObj.get())));

    // Non-empty graph, non-empty timeline, non-empty patchDocument stash as the load target.
    juce::AudioProcessorGraph graph;
    buildSampleGraph(graph);
    const int originalNodeCount = graph.getNumNodes();
    const auto originalUuids = nodeUuids(graph);

    TimelineDoc timeline;
    buildSampleTimeline(timeline);
    const auto originalRevision = timeline.getRevision();
    const auto originalTimelineJson = juce::JSON::toString(timeline.toVar());

    PatchDocument patchDocument;
    juce::DynamicObject::Ptr stashRoot = new juce::DynamicObject();
    stashRoot->setProperty("nodes", juce::var(juce::Array<juce::var>()));
    stashRoot->setProperty("connections", juce::var(juce::Array<juce::var>()));
    stashRoot->setProperty("preExistingStash", "should-not-change");
    patchDocument.loadFromVar(juce::var(stashRoot.get()));
    ASSERT_FALSE(patchDocument.empty());
    const auto originalStashJson = juce::JSON::toString(patchDocument.toVar(makeKnownOnlyPatch()));

    auto result = ProjectBundle::load(dir, graph, timeline, patchDocument);

    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(result.message.contains("patch validation")) << result.message;

    EXPECT_EQ(graph.getNumNodes(), originalNodeCount);
    EXPECT_EQ(nodeUuids(graph), originalUuids);

    EXPECT_EQ(timeline.getRevision(), originalRevision);
    EXPECT_EQ(juce::JSON::toString(timeline.toVar()), originalTimelineJson);

    EXPECT_EQ(juce::JSON::toString(patchDocument.toVar(makeKnownOnlyPatch())), originalStashJson);
}

TEST_F(ProjectBundleTest, LoadOrderIsAllOrNothing_BadTimeline) {
    auto dir = bundleDir("BadTimeline");
    dir.createDirectory();

    // A VALID patch (built from an unrelated source graph)...
    juce::AudioProcessorGraph sourceGraph;
    buildSampleGraph(sourceGraph);
    auto patchJson = synth::AIStateMapper::graphToJSON(sourceGraph);
    auto* rootObj = patchJson.getDynamicObject();
    ASSERT_NE(rootObj, nullptr);

    // ...paired with a corrupt timeline (an out-of-range note pitch inside an otherwise
    // well-formed document).
    TimelineDoc validTimeline;
    buildSampleTimeline(validTimeline);
    auto timelineVar = validTimeline.toVar();
    auto* tracks = timelineVar.getProperty("tracks", {}).getArray();
    ASSERT_NE(tracks, nullptr);
    ASSERT_FALSE(tracks->isEmpty());
    auto* clips = (*tracks)[0].getProperty("clips", {}).getArray();
    ASSERT_NE(clips, nullptr);
    ASSERT_FALSE(clips->isEmpty());
    auto* notes = (*clips)[0].getProperty("notes", {}).getArray();
    ASSERT_NE(notes, nullptr);
    ASSERT_FALSE(notes->isEmpty());
    (*notes).getReference(0).getDynamicObject()->setProperty("pitch", 300); // out of 0..127

    rootObj->setProperty("timeline", timelineVar);
    dir.getChildFile(ProjectBundle::kProjectFileName).replaceWithText(juce::JSON::toString(patchJson));

    // Non-empty graph + non-empty timeline as the load target — this is the key ordering pin: the
    // patch is valid, but the graph must NOT be applied because the timeline gate fails too.
    juce::AudioProcessorGraph graph;
    buildSampleGraph(graph);
    const int originalNodeCount = graph.getNumNodes();
    const auto originalUuids = nodeUuids(graph);

    TimelineDoc timeline;
    buildSampleTimeline(timeline);
    const auto originalTimelineJson = juce::JSON::toString(timeline.toVar());

    PatchDocument patchDocument;

    auto result = ProjectBundle::load(dir, graph, timeline, patchDocument);

    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(result.message.contains("timeline validation")) << result.message;

    EXPECT_EQ(graph.getNumNodes(), originalNodeCount)
        << "the graph must not be touched even though the patch was valid";
    EXPECT_EQ(nodeUuids(graph), originalUuids);
    EXPECT_EQ(juce::JSON::toString(timeline.toVar()), originalTimelineJson);
}

TEST_F(ProjectBundleTest, MissingTimelineKeyLoadsEmptyDoc) {
    auto dir = bundleDir("NoTimeline");
    dir.createDirectory();

    juce::AudioProcessorGraph sourceGraph;
    buildSampleGraph(sourceGraph);
    auto patchJson = synth::AIStateMapper::graphToJSON(sourceGraph);
    ASSERT_FALSE(patchJson.hasProperty("timeline"));
    dir.getChildFile(ProjectBundle::kProjectFileName).replaceWithText(juce::JSON::toString(patchJson));

    juce::AudioProcessorGraph graph;
    TimelineDoc timeline;
    buildSampleTimeline(timeline); // non-empty beforehand, to prove load empties it
    PatchDocument patchDocument;

    auto result = ProjectBundle::load(dir, graph, timeline, patchDocument);

    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_TRUE(timeline.isEmpty());
    EXPECT_EQ(graph.getNumNodes(), sourceGraph.getNumNodes());
}

TEST_F(ProjectBundleTest, UnknownTopLevelKeysSurviveBundleRoundTrip) {
    auto dir = bundleDir("UnknownKeys");
    dir.createDirectory();

    juce::AudioProcessorGraph sourceGraph;
    buildSampleGraph(sourceGraph);
    auto patchJson = synth::AIStateMapper::graphToJSON(sourceGraph);
    auto* rootObj = patchJson.getDynamicObject();
    ASSERT_NE(rootObj, nullptr);

    TimelineDoc timelineForJson;
    buildSampleTimeline(timelineForJson);
    rootObj->setProperty("timeline", timelineForJson.toVar());

    juce::DynamicObject::Ptr futureThing = new juce::DynamicObject();
    futureThing->setProperty("nested", "unrecognised-by-this-build");
    rootObj->setProperty("futureThing", juce::var(futureThing.get()));

    dir.getChildFile(ProjectBundle::kProjectFileName).replaceWithText(juce::JSON::toString(patchJson));

    juce::AudioProcessorGraph graph;
    TimelineDoc timeline;
    PatchDocument patchDocument;
    ASSERT_TRUE(ProjectBundle::load(dir, graph, timeline, patchDocument).ok);
    ASSERT_FALSE(patchDocument.empty());

    auto newDir = bundleDir("UnknownKeysResaved");
    auto saveResult = ProjectBundle::save(newDir, graph, timeline, patchDocument);
    ASSERT_TRUE(saveResult.ok) << saveResult.message;

    auto resaved = juce::JSON::parse(newDir.getChildFile(ProjectBundle::kProjectFileName));
    ASSERT_TRUE(resaved.isObject());
    EXPECT_EQ(resaved.getProperty("futureThing", {}).getProperty("nested", {}).toString(),
              "unrecognised-by-this-build");

    // "timeline" is present exactly once (a JSON object cannot carry a duplicate key) and equals
    // the live doc, not whatever the stash carried through.
    EXPECT_EQ(juce::JSON::toString(resaved.getProperty("timeline", {})), juce::JSON::toString(timeline.toVar()));
}

TEST_F(ProjectBundleTest, StaleStashedTimelineNeverLeaks) {
    // A PatchDocument that already stashed a bogus "timeline" from an earlier plain-.json load.
    PatchDocument patchDocument;
    juce::DynamicObject::Ptr stashRoot = new juce::DynamicObject();
    stashRoot->setProperty("nodes", juce::var(juce::Array<juce::var>()));
    stashRoot->setProperty("connections", juce::var(juce::Array<juce::var>()));
    juce::DynamicObject::Ptr bogusTimeline = new juce::DynamicObject();
    bogusTimeline->setProperty("version", 999);
    bogusTimeline->setProperty("thisIsNotTheRealDoc", true);
    stashRoot->setProperty("timeline", juce::var(bogusTimeline.get()));
    patchDocument.loadFromVar(juce::var(stashRoot.get()));
    ASSERT_FALSE(patchDocument.empty());

    juce::AudioProcessorGraph graph;
    buildSampleGraph(graph);
    TimelineDoc realTimeline;
    buildSampleTimeline(realTimeline);

    auto dir = bundleDir("StaleStash");
    auto result = ProjectBundle::save(dir, graph, realTimeline, patchDocument);
    ASSERT_TRUE(result.ok) << result.message;

    auto saved = juce::JSON::parse(dir.getChildFile(ProjectBundle::kProjectFileName));
    ASSERT_TRUE(saved.isObject());
    EXPECT_EQ(juce::JSON::toString(saved.getProperty("timeline", {})), juce::JSON::toString(realTimeline.toVar()));
    EXPECT_NE(juce::JSON::toString(saved.getProperty("timeline", {})),
              juce::JSON::toString(juce::var(bogusTimeline.get())));
}

TEST_F(ProjectBundleTest, UntrustedGateRejectsHandEditedGarbage) {
    auto dir = bundleDir("Garbage");
    dir.createDirectory();

    juce::DynamicObject::Ptr rootObj = new juce::DynamicObject();
    rootObj->setProperty("schemaVersion", 1);

    juce::Array<juce::var> nodes;
    juce::DynamicObject::Ptr n1 = new juce::DynamicObject();
    n1->setProperty("id", 1);
    n1->setProperty("type", "Oscillator");
    nodes.add(juce::var(n1.get()));
    rootObj->setProperty("nodes", juce::var(nodes));

    // A connection referencing a node id that doesn't exist anywhere in the patch.
    juce::Array<juce::var> connections;
    juce::DynamicObject::Ptr c1 = new juce::DynamicObject();
    c1->setProperty("src", 1);
    c1->setProperty("srcPort", 0);
    c1->setProperty("dst", 999);
    c1->setProperty("dstPort", 0);
    connections.add(juce::var(c1.get()));
    rootObj->setProperty("connections", juce::var(connections));

    dir.getChildFile(ProjectBundle::kProjectFileName).replaceWithText(juce::JSON::toString(juce::var(rootObj.get())));

    juce::AudioProcessorGraph graph;
    TimelineDoc timeline;
    PatchDocument patchDocument;
    auto result = ProjectBundle::load(dir, graph, timeline, patchDocument);

    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(result.message.contains("patch validation")) << result.message;
    EXPECT_TRUE(result.message.contains("unknown destination node")) << result.message;
    EXPECT_EQ(graph.getNumNodes(), 0);
}

TEST_F(ProjectBundleTest, IsBundleDetection) {
    auto properBundle = bundleDir("Proper");
    juce::AudioProcessorGraph graph;
    buildSampleGraph(graph);
    TimelineDoc timeline;
    PatchDocument patchDocument;
    ASSERT_TRUE(ProjectBundle::save(properBundle, graph, timeline, patchDocument).ok);
    EXPECT_TRUE(ProjectBundle::isBundle(properBundle));

    // Wrong extension, otherwise identical contents.
    auto wrongExtension = root.getChildFile("WrongExtension.notagsproj");
    wrongExtension.createDirectory();
    properBundle.getChildFile(ProjectBundle::kProjectFileName)
        .copyFileTo(wrongExtension.getChildFile(ProjectBundle::kProjectFileName));
    EXPECT_FALSE(ProjectBundle::isBundle(wrongExtension));

    // Right extension, but no project.json inside.
    auto missingProjectFile = root.getChildFile("Empty.agsproj");
    missingProjectFile.createDirectory();
    EXPECT_FALSE(ProjectBundle::isBundle(missingProjectFile));

    // A plain file, not a directory at all.
    auto plainFile = root.getChildFile("NotADirectory.agsproj");
    plainFile.replaceWithText("not a bundle");
    EXPECT_FALSE(ProjectBundle::isBundle(plainFile));
}

TEST_F(ProjectBundleTest, DefaultProjectsDirectoryLivesUnderUserMusic) {
    const auto dir = ProjectBundle::getDefaultProjectsDirectory();
    const auto music = juce::File::getSpecialLocation(juce::File::userMusicDirectory);

    EXPECT_TRUE(dir.isAChildOf(music));
    EXPECT_TRUE(dir.getFileName().equalsIgnoreCase(synth::branding::kProjectsFolderName));
    EXPECT_TRUE(dir.isDirectory());
}
