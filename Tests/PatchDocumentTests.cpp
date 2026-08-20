#include "../Source/AI/AIStateMapper.h"
#include "../Source/AudioEngine.h"
#include "../Source/Modules/SamplerModule.h"
#include "../Source/PatchDocument.h"
#include "../Source/UI/GraphEditor.h"
#include <gtest/gtest.h>

// PatchDocument preserves top-level JSON keys this build doesn't understand (e.g. a
// future "timeline") across a save/load round-trip, so an older build never destroys a newer
// build's data just by re-saving. See docs/plans/timeline-plan.html §6.

class PatchDocumentTest : public ::testing::Test {};

namespace {

juce::var makeKnownOnlyPatch() {
    auto* root = new juce::DynamicObject();
    root->setProperty("schemaVersion", 1);
    root->setProperty("nodes", juce::var(juce::Array<juce::var>()));
    root->setProperty("connections", juce::var(juce::Array<juce::var>()));
    return juce::var(root);
}

juce::var makePatchWithUnknownKeys() {
    auto* root = new juce::DynamicObject();
    root->setProperty("schemaVersion", 1);
    root->setProperty("nodes", juce::var(juce::Array<juce::var>()));
    root->setProperty("connections", juce::var(juce::Array<juce::var>()));

    auto* timeline = new juce::DynamicObject();
    timeline->setProperty("tracks", juce::var(juce::Array<juce::var>()));
    timeline->setProperty("version", 3);
    root->setProperty("timeline", juce::var(timeline));

    root->setProperty("futureThing", "unrecognised-by-this-build");
    return juce::var(root);
}

} // namespace

// A plain patch with only known keys round-trips with nothing added.
TEST_F(PatchDocumentTest, PlainPatchRoundTripsUnchanged) {
    synth::PatchDocument doc;
    auto original = makeKnownOnlyPatch();
    doc.loadFromVar(original);
    EXPECT_TRUE(doc.empty());

    auto fresh = makeKnownOnlyPatch();
    auto result = doc.toVar(fresh);

    EXPECT_TRUE(juce::JSON::toString(result) == juce::JSON::toString(fresh));
}

// Unknown top-level keys ("timeline", "futureThing") survive a load -> save round-trip
// byte-equivalently, while the known keys come from the freshly generated JSON.
TEST_F(PatchDocumentTest, UnknownTopLevelKeysSurviveRoundTrip) {
    synth::PatchDocument doc;
    doc.loadFromVar(makePatchWithUnknownKeys());
    EXPECT_FALSE(doc.empty());

    // Simulate a fresh save from live graph state — nodes/connections regenerated, no memory of
    // the previously loaded "timeline"/"futureThing" keys.
    auto* freshRoot = new juce::DynamicObject();
    freshRoot->setProperty("schemaVersion", 1);
    auto* node = new juce::DynamicObject();
    node->setProperty("id", 1);
    node->setProperty("type", "Oscillator");
    juce::Array<juce::var> nodes;
    nodes.add(juce::var(node));
    freshRoot->setProperty("nodes", juce::var(nodes));
    freshRoot->setProperty("connections", juce::var(juce::Array<juce::var>()));
    juce::var fresh(freshRoot);

    auto result = doc.toVar(fresh);

    ASSERT_TRUE(result.isObject());
    // Known keys regenerated fresh.
    EXPECT_EQ((int)result.getProperty("schemaVersion", {}), 1);
    auto* resultNodes = result.getProperty("nodes", {}).getArray();
    ASSERT_NE(resultNodes, nullptr);
    EXPECT_EQ(resultNodes->size(), 1);

    // Unknown keys preserved byte-equivalently against the original load.
    auto original = makePatchWithUnknownKeys();
    EXPECT_EQ(juce::JSON::toString(result.getProperty("timeline", {})),
              juce::JSON::toString(original.getProperty("timeline", {})));
    EXPECT_EQ(result.getProperty("futureThing", {}).toString(), "unrecognised-by-this-build");
}

// A "new patch" action must clear the stash — preserved keys are per-loaded-file and must never
// be resurrected into a patch the user didn't load them from.
TEST_F(PatchDocumentTest, ClearDropsStashedKeys) {
    synth::PatchDocument doc;
    doc.loadFromVar(makePatchWithUnknownKeys());
    ASSERT_FALSE(doc.empty());

    doc.clear();
    EXPECT_TRUE(doc.empty());

    auto fresh = makeKnownOnlyPatch();
    auto result = doc.toVar(fresh);
    EXPECT_FALSE(result.hasProperty("timeline"));
    EXPECT_FALSE(result.hasProperty("futureThing"));
}

// Loading a second file replaces the stash rather than accumulating across loads.
TEST_F(PatchDocumentTest, LoadReplacesPreviousStash) {
    synth::PatchDocument doc;
    doc.loadFromVar(makePatchWithUnknownKeys());
    ASSERT_FALSE(doc.empty());

    doc.loadFromVar(makeKnownOnlyPatch());
    EXPECT_TRUE(doc.empty());
}

// "schemaVersion" is a known key — regenerated fresh, never stashed even though it's technically
// a scalar that could be mistaken for arbitrary unknown data.
TEST_F(PatchDocumentTest, SchemaVersionIsNeverStashed) {
    synth::PatchDocument doc;
    doc.loadFromVar(makePatchWithUnknownKeys());

    auto fresh = makeKnownOnlyPatch();
    fresh.getDynamicObject()->setProperty("schemaVersion", 99); // pretend a newer build wrote this
    auto result = doc.toVar(fresh);
    // The fresh value wins — PatchDocument never overrides a known key.
    EXPECT_EQ((int)result.getProperty("schemaVersion", {}), 99);
}

// Full GraphEditor integration: a preset file with an unknown top-level "timeline" key round-
// trips through loadPreset -> savePreset unchanged, and newPatch() clears it.
TEST_F(PatchDocumentTest, GraphEditorSaveLoadRoundTripsUnknownKeysAndNewPatchClears) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    auto file =
        juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("agentsynth-patchdocument-test.json");
    file.deleteFile();

    file.replaceWithText(juce::JSON::toString(makePatchWithUnknownKeys()));

    editor.loadPreset(file);

    auto resavePath = juce::File::getSpecialLocation(juce::File::tempDirectory)
                          .getChildFile("agentsynth-patchdocument-test-resave.json");
    resavePath.deleteFile();
    editor.savePreset(resavePath);

    auto resaved = juce::JSON::parse(resavePath.loadFileAsString());
    ASSERT_TRUE(resaved.isObject());
    auto original = makePatchWithUnknownKeys();
    EXPECT_EQ(juce::JSON::toString(resaved.getProperty("timeline", {})),
              juce::JSON::toString(original.getProperty("timeline", {})));
    EXPECT_EQ(resaved.getProperty("futureThing", {}).toString(), "unrecognised-by-this-build");

    // A fresh patch must not resurrect the stash into a subsequent save.
    editor.newPatch();
    auto afterNewPatchPath = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                 .getChildFile("agentsynth-patchdocument-test-after-new.json");
    afterNewPatchPath.deleteFile();
    editor.savePreset(afterNewPatchPath);
    auto afterNewPatch = juce::JSON::parse(afterNewPatchPath.loadFileAsString());
    EXPECT_FALSE(afterNewPatch.hasProperty("timeline"));
    EXPECT_FALSE(afterNewPatch.hasProperty("futureThing"));

    file.deleteFile();
    resavePath.deleteFile();
    afterNewPatchPath.deleteFile();
}

// Preserved top-level keys are inert: PatchDocument never touches per-node "state" (the payload
// ModuleBase::setExtraState consumes), so a Sampler node's file-path state applies normally
// regardless of unrelated unknown top-level keys sitting alongside it in the same file.
TEST_F(PatchDocumentTest, PreservedKeysNeverReachSetExtraState) {
    AudioEngine engine;
    auto& graph = engine.getGraph();

    // Build a patch with an unknown top-level key plus a Sampler node carrying extra state.
    auto* root = new juce::DynamicObject();
    root->setProperty("schemaVersion", 1);
    root->setProperty("timeline", "some-future-arrangement-data");

    auto* node = new juce::DynamicObject();
    node->setProperty("id", 1);
    node->setProperty("type", "Sampler");
    node->setProperty("x", 0);
    node->setProperty("y", 0);
    // Deliberately no "state" (no sample file) — the point is that the unrelated "timeline" key
    // must not be visible to, or interpreted as, node state at all.
    juce::Array<juce::var> nodes;
    nodes.add(juce::var(node));
    root->setProperty("nodes", juce::var(nodes));
    root->setProperty("connections", juce::var(juce::Array<juce::var>()));
    juce::var patch(root);

    synth::PatchDocument doc;
    doc.loadFromVar(patch);
    ASSERT_FALSE(doc.empty());

    ASSERT_TRUE(synth::AIStateMapper::applyJSONToGraph(patch, graph, true, /*trusted=*/true));

    auto* samplerNode = graph.getNodeForId(juce::AudioProcessorGraph::NodeID(1));
    ASSERT_NE(samplerNode, nullptr);
    auto* sampler = dynamic_cast<SamplerModule*>(samplerNode->getProcessor());
    ASSERT_NE(sampler, nullptr);
    // No sample was ever loaded — the stashed "timeline" key was never fed into setExtraState.
    EXPECT_EQ(sampler->getExtraState(), juce::var());
}
