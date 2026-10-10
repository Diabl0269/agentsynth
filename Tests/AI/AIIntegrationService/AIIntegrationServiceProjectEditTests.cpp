// Edit plans through the service, headless: the plan-wide rules (one id namespace, the mode rule,
// how a writeLane names its node), a preview that mutates nothing, the all-or-nothing backstop,
// and how a plan is asked for (project.generate hosted, sendPrompt locally). The apply order end to
// end through the real app host is Tests/Mixer/ChannelFlow/ChannelFlowProjectEditTests.cpp.
#include "AI/SoundShapeChecks.h"
#include "AIIntegrationServiceTestFixture.h"
#include "Modules/ADSRModule.h"
#include "Modules/AttenuverterModule.h"
#include "Modules/FilterModule.h"
#include "Modules/ModuleBase.h"
#include "PlanFakeHost.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"

namespace synth {

namespace {

juce::var parse(const juce::String& json) {
    const juce::var value = juce::JSON::parse(json);
    EXPECT_FALSE(value.isVoid()) << "test JSON did not parse: " << json;
    return value;
}

// Records what sendCapabilityRequest / sendPrompt were handed.
class PlanCapturingProvider : public AIProvider {
public:
    RequestId sendPrompt(const std::vector<Message>& conversation, CompletionCallback callback,
                         const juce::var& responseSchema, std::function<void(const juce::String&)> = {}) override {
        lastConversation = conversation;
        lastSchema = responseSchema;
        respond(callback);
        return {};
    }
    RequestId sendCapabilityRequest(const juce::String& capability, const juce::var& body,
                                    CompletionCallback callback) override {
        lastCapability = capability;
        lastBody = body;
        respond(callback);
        return {};
    }
    void cancel(RequestId) override {}
    void fetchAvailableModels(std::function<void(const juce::StringArray&, bool)> callback) override {
        callback({}, true);
    }
    void setModel(const juce::String&) override {}
    juce::String getCurrentModel() const override { return {}; }
    void setRequestTimeoutMs(int) override {}
    int getRequestTimeoutMs() const override { return 240000; }
    juce::String getProviderName() const override { return "PlanCapturingProvider"; }
    bool isHosted() const override { return hosted; }

    bool hosted = true;
    juce::String lastCapability;
    juce::var lastBody, lastSchema;
    std::vector<Message> lastConversation;

private:
    static void respond(const CompletionCallback& callback) {
        AIResponse response;
        response.success = true;
        response.content = "{}";
        if (callback)
            callback(response);
    }
};

// One instrument track with a Filter insert (7002), a patch LFO (7003) modulating that insert's
// cutoff by name, and a lane on the insert by nodeId - listed BEFORE the track op that creates it.
constexpr const char* kPlan = R"({"mode": "merge",
    "nodes": [{"id": 7003, "type": "LFO"}], "connections": [],
    "modulations": [{"source": 7003, "dest": 7002, "destParam": "cutoff", "amount": 0.5}],
    "timelineOps": [
        {"op": "writeLane", "nodeId": 7002, "paramId": "cutoff",
         "points": [{"beat": 0, "value": 400, "curve": 1}, {"beat": 8, "value": 3000, "curve": 1}]},
        {"op": "addInstrumentTrack", "name": "Bass", "instrument": "Oscillator", "instrumentId": 7001,
         "inserts": [{"type": "Filter", "id": 7002, "params": {"cutoff": 600}}]}]})";

// The track's own envelope (7010) is reused as a modulation source onto the Filter insert's cutoff
// (7011), and a lane on the envelope is addressed by its id.
constexpr const char* kEnvelopePlan = R"({"mode": "merge", "nodes": [], "connections": [],
    "modulations": [{"source": 7010, "dest": 7011, "destParam": "cutoff", "amount": 0.5}],
    "timelineOps": [
        {"op": "addInstrumentTrack", "name": "Bass", "instrument": "Oscillator",
         "inserts": [{"type": "Filter", "id": 7011}],
         "instrumentParams": {"waveform": "Saw"},
         "envelope": {"id": 7010, "params": {"sustain": 0.0, "release": 0.15}}},
        {"op": "writeLane", "nodeId": 7010, "paramId": "attack",
         "points": [{"beat": 0, "value": 0.5}, {"beat": 4, "value": 1.5}]}]})";

juce::AudioProcessorGraph::Node* firstNodeOfFactoryType(juce::AudioProcessorGraph& graph, const juce::String& type) {
    for (auto* node : graph.getNodes())
        if (AIStateMapper::getFactoryTypeName(node->getProcessor()) == type)
            return node;
    return nullptr;
}

double rawParamValue(juce::AudioProcessor* processor, const juce::String& paramId) {
    for (auto* param : processor->getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(param);
            ranged != nullptr && ranged->paramID == paramId)
            return ranged->convertFrom0to1(ranged->getValue());
    return -1.0;
}

} // namespace

class AIIntegrationServiceProjectEditTest : public AIIntegrationServiceTest {
protected:
    void SetUp() override {
        AIIntegrationServiceTest::SetUp();
        host = std::make_unique<PlanFakeHost>(doc, *graph);
        service->setTimelineContext(&doc, nullptr);
        service->setTimelineOpsHost(host.get());
    }
    juce::String graphDump() { return juce::JSON::toString(AIStateMapper::graphToJSON(*graph)); }
    juce::String docDump() { return juce::JSON::toString(doc.toVar()); }

    TimelineDoc doc;
    std::unique_ptr<PlanFakeHost> host;
};

// A plan that only builds a track carries an empty merge patch; the preview names the track and
// does not lead with "Merges a patch that changes nothing", which read like a failure.
TEST_F(AIIntegrationServiceProjectEditTest, ATrackOnlyPlanPreviewsJustTheTrack) {
    const auto preview = service->previewProjectEdit(parse(
        R"({"mode": "merge", "nodes": [], "connections": [], "timelineOps": [{"op": "addInstrumentTrack",
            "name": "Pluck", "instrument": "Oscillator"}]})"));
    ASSERT_TRUE(preview.ok) << preview.message;
    ASSERT_EQ(preview.previewLines.size(), 1) << preview.previewText;
    EXPECT_TRUE(preview.previewLines[0].startsWith("Adds instrument track \"Pluck\"")) << preview.previewText;
    EXPECT_FALSE(preview.previewText.contains("changes nothing")) << preview.previewText;
}

// The song layout the server expands: a tempo, one marker per section, then the clips. Preview names them
// and touches nothing; apply hands the tempo to the host and puts the markers in the doc.
TEST_F(AIIntegrationServiceProjectEditTest, TempoAndMarkersPreviewAndApply) {
    const auto plan = parse(R"({"mode": "merge", "nodes": [], "connections": [], "timelineOps": [
        {"op": "setTempo", "bpm": 174},
        {"op": "addMarker", "beat": 0, "name": "Intro"}, {"op": "addMarker", "beat": 16, "name": "Drop"}]})");
    const auto docBefore = docDump();

    const auto preview = service->previewProjectEdit(plan);
    ASSERT_TRUE(preview.ok) << preview.message;
    EXPECT_TRUE(preview.previewText.contains("Sets the tempo to 174 BPM")) << preview.previewText;
    EXPECT_TRUE(preview.previewText.contains("adds 2 markers (\"Intro\" at beat 0, \"Drop\" at beat 16)"))
        << preview.previewText;
    EXPECT_EQ(docDump(), docBefore);
    EXPECT_TRUE(host->tempos.empty()) << "a preview never sets the tempo";

    const auto applied = service->applyProjectEdit(plan);
    ASSERT_TRUE(applied.ok) << applied.message;
    ASSERT_EQ(host->tempos.size(), 1u);
    EXPECT_EQ(host->tempos[0], 174.0);
    ASSERT_EQ(doc.getMarkers().size(), 2u);
    EXPECT_EQ(doc.getMarkers()[1].text, "Drop");
    EXPECT_EQ(doc.getMarkers()[1].beat, 16.0);
}

TEST_F(AIIntegrationServiceProjectEditTest, OutOfRangeTempoOrMarkerRejectsThePlanBeforeAnythingChanges) {
    for (const char* ops : {R"([{"op": "setTempo", "bpm": 300}])", R"([{"op": "addMarker", "beat": -1, "name": "A"}])",
                            R"([{"op": "addMarker", "beat": 0, "name": ""}])"}) {
        SCOPED_TRACE(ops);
        const auto plan =
            parse(R"({"mode": "merge", "nodes": [], "connections": [], "timelineOps": )" + juce::String(ops) + "}");
        EXPECT_FALSE(service->previewProjectEdit(plan).ok);
        EXPECT_FALSE(service->applyProjectEdit(plan).ok);
    }
    EXPECT_TRUE(host->tempos.empty());
    EXPECT_TRUE(doc.getMarkers().empty());
}

TEST_F(AIIntegrationServiceProjectEditTest, PreviewDescribesEveryPhaseAndMutatesNothing) {
    graph->addNode(std::make_unique<OscillatorModule>());
    const auto graphBefore = graphDump();
    const auto docBefore = docDump();

    const auto preview = service->previewProjectEdit(parse(kPlan));
    ASSERT_TRUE(preview.ok) << preview.message;
    EXPECT_TRUE(preview.merge);
    EXPECT_TRUE(preview.previewText.contains("Merges a patch that adds 1 module")) << preview.previewText;
    EXPECT_TRUE(preview.previewText.contains("1 modulation")) << preview.previewText;
    EXPECT_TRUE(preview.previewText.contains("Adds instrument track \"Bass\"")) << preview.previewText;
    EXPECT_TRUE(preview.previewText.contains("writes 2 points to Filter cutoff")) << preview.previewText;
    ASSERT_EQ(preview.previewLines.size(), 2) << "one line for the patch phase, one for the timeline ops";
    EXPECT_TRUE(preview.previewLines[0].startsWith("Merges a patch")) << preview.previewLines[0];
    EXPECT_TRUE(preview.previewLines[1].startsWith("Adds instrument track")) << preview.previewLines[1];

    EXPECT_EQ(graphDump(), graphBefore);
    EXPECT_EQ(docDump(), docBefore);
    EXPECT_EQ(host->builds, 0) << "the preview builds stand-ins, never through the app's host";
    EXPECT_EQ(host->batches, 0);
}

TEST_F(AIIntegrationServiceProjectEditTest, DuplicateIdAcrossPatchAndInstrumentIdIsRejected) {
    const auto result = service->previewProjectEdit(parse(R"({"mode": "merge",
        "nodes": [{"id": 7001, "type": "LFO"}], "connections": [],
        "timelineOps": [{"op": "addInstrumentTrack", "name": "Bass", "instrument": "Oscillator", "instrumentId": 7001}]})"));
    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(result.message.contains("Id 7001 is used twice")) << result.message;
    EXPECT_TRUE(result.message.contains("instrumentId")) << result.message;
}

TEST_F(AIIntegrationServiceProjectEditTest, DuplicateIdAcrossPatchAndInsertIdIsRejected) {
    const auto result = service->previewProjectEdit(parse(R"({"mode": "merge",
        "nodes": [{"id": 5, "type": "LFO"}], "connections": [],
        "timelineOps": [{"op": "addInstrumentTrack", "name": "Bass", "instrument": "Oscillator",
                         "inserts": [{"type": "Filter", "id": 5}]}]})"));
    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(result.message.contains("Id 5 is used twice")) << result.message;
    EXPECT_TRUE(result.message.contains("insert 0 id")) << result.message;
}

TEST_F(AIIntegrationServiceProjectEditTest, DuplicateIdAcrossEnvelopeAndInstrumentIdIsRejected) {
    const auto result = service->previewProjectEdit(parse(R"({"timelineOps": [
        {"op": "addInstrumentTrack", "name": "Bass", "instrument": "Oscillator", "instrumentId": 5,
         "envelope": {"id": 5}}]})"));
    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(result.message.contains("Id 5 is used twice")) << result.message;
    EXPECT_TRUE(result.message.contains("instrumentId")) << result.message;
    EXPECT_TRUE(result.message.contains("envelope id")) << result.message;
}

TEST_F(AIIntegrationServiceProjectEditTest, DuplicateIdAcrossEnvelopeAndInsertIdIsRejected) {
    const auto result = service->previewProjectEdit(parse(R"({"timelineOps": [
        {"op": "addInstrumentTrack", "name": "Bass", "instrument": "Oscillator",
         "inserts": [{"type": "Filter", "id": 6}], "envelope": {"id": 6}}]})"));
    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(result.message.contains("Id 6 is used twice")) << result.message;
    EXPECT_TRUE(result.message.contains("insert 0 id")) << result.message;
    EXPECT_TRUE(result.message.contains("envelope id")) << result.message;
}

TEST_F(AIIntegrationServiceProjectEditTest, DuplicateIdAcrossPatchNodeAndEnvelopeIsRejected) {
    const auto result = service->previewProjectEdit(parse(R"({"mode": "merge",
        "nodes": [{"id": 7, "type": "LFO"}], "connections": [],
        "timelineOps": [{"op": "addInstrumentTrack", "name": "Bass", "instrument": "Oscillator",
                         "envelope": {"id": 7}}]})"));
    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(result.message.contains("Id 7 is used twice")) << result.message;
}

TEST_F(AIIntegrationServiceProjectEditTest, EnvelopeIdThatIsALiveUidIsRejected) {
    const auto live = graph->addNode(std::make_unique<OscillatorModule>())->nodeID.uid;
    const auto result = service->previewProjectEdit(
        parse("{\"timelineOps\": [{\"op\": \"addInstrumentTrack\", \"name\": \"Bass\", \"instrument\": "
              "\"Oscillator\", \"envelope\": {\"id\": " +
              juce::String((int)live) + "}}]}"));
    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(result.message.contains("\"envelope id\" " + juce::String((int)live))) << result.message;
    EXPECT_TRUE(result.message.contains("already the id of a node in the current patch")) << result.message;
}

TEST_F(AIIntegrationServiceProjectEditTest, EnvelopeOnASamplerTrackIsRejectedWithItsId) {
    const auto result = service->previewProjectEdit(parse(R"({"timelineOps": [
        {"op": "addInstrumentTrack", "name": "Keys", "instrument": "Sampler", "envelope": {"id": 9}}]})"));
    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(result.message.startsWith("timelineOps[0] (addInstrumentTrack): Sampler tracks have no envelope"))
        << result.message;
}

TEST_F(AIIntegrationServiceProjectEditTest, ReservedIdThatIsALiveUidIsRejected) {
    const auto live = graph->addNode(std::make_unique<OscillatorModule>())->nodeID.uid;
    const auto result = service->previewProjectEdit(
        parse("{\"timelineOps\": [{\"op\": \"addInstrumentTrack\", \"name\": \"Bass\", \"instrument\": \"Oscillator\", "
              "\"instrumentId\": " +
              juce::String((int)live) + "}]}"));
    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(result.message.contains("already the id of a node in the current patch")) << result.message;
}

TEST_F(AIIntegrationServiceProjectEditTest, ReplaceModeWithATrackOpIsRejected) {
    const auto result = service->previewProjectEdit(parse(R"({"mode": "replace",
        "nodes": [{"id": 1, "type": "Oscillator"}, {"id": 2, "type": "Audio Output"}],
        "connections": [{"src": 1, "srcPort": 0, "dst": 2, "dstPort": 0}],
        "timelineOps": [{"op": "addTrack", "kind": "midi", "name": "Keys"}]})"));
    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(result.message.contains("\"mode\": \"merge\"")) << result.message;
}

TEST_F(AIIntegrationServiceProjectEditTest, WriteLaneNeedsExactlyOneAddress) {
    for (const char* address : {R"("nodeUuid": "x", "nodeId": 7002, )", ""}) {
        const auto result =
            service->previewProjectEdit(parse(juce::String(R"({"timelineOps": [
                {"op": "addInstrumentTrack", "name": "Bass", "instrument": "Oscillator",
                 "inserts": [{"type": "Filter", "id": 7002}]},
                {"op": "writeLane", )") + address +
                                              R"("paramId": "cutoff", "points": [{"beat": 0, "value": 400}]}]})"));
        EXPECT_FALSE(result.ok) << address;
        EXPECT_TRUE(result.message.startsWith("timelineOps[1] (writeLane): give exactly one of")) << result.message;
    }
}

TEST_F(AIIntegrationServiceProjectEditTest, WriteLaneNodeIdMustBeANodeTheResponseCreates) {
    const auto result = service->previewProjectEdit(parse(R"({"timelineOps": [
        {"op": "writeLane", "nodeId": 42, "paramId": "cutoff", "points": [{"beat": 0, "value": 400}]}]})"));
    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(result.message.contains("is not a node this response creates")) << result.message;
}

// A TimelineOps rejection names the op by its index in the RESPONSE, not in the phase it ran in.
TEST_F(AIIntegrationServiceProjectEditTest, OpRejectionsUseTheResponsesOwnIndex) {
    const auto result = service->previewProjectEdit(parse(R"({"timelineOps": [
        {"op": "placeClips", "track": "Nowhere", "clips": [{"startBeat": 0, "lengthBeats": 4}]},
        {"op": "addTrack", "kind": "midi", "name": "Keys"}]})"));
    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(result.message.startsWith("timelineOps[0] (placeClips)")) << result.message;
}

TEST_F(AIIntegrationServiceProjectEditTest, ApplyRunsEveryPhaseAsOneUndoStep) {
    const auto graphBefore = graphDump();
    const auto docBefore = docDump();
    const auto applied = service->applyProjectEdit(parse(kPlan));
    ASSERT_TRUE(applied.ok) << applied.message;
    EXPECT_EQ(host->builds, 1);
    EXPECT_EQ(host->batches, 1);

    const auto& tracks = doc.getTracks();
    const Track* bass = nullptr;
    for (const auto& track : tracks)
        bass = track.name == "Bass" ? &track : bass;
    ASSERT_NE(bass, nullptr);

    ASSERT_TRUE(host->undo.undo());
    EXPECT_EQ(graphDump(), graphBefore);
    EXPECT_EQ(docDump(), docBefore);
    EXPECT_FALSE(host->undo.canUndo()) << "the whole plan was one step";
}

// The backstop: a host build that fails after leaving a node behind (impossible for the real host,
// whose contract is to remove what it made) still leaves the graph and doc exactly as they were.
TEST_F(AIIntegrationServiceProjectEditTest, FailureAfterValidationRestoresGraphAndDoc) {
    graph->addNode(std::make_unique<OscillatorModule>());
    const auto graphBefore = graphDump();
    const auto docBefore = docDump();
    host->failOnBuild = 2;

    const auto applied = service->applyProjectEdit(parse(R"({"mode": "merge", "nodes": [], "connections": [],
        "timelineOps": [
            {"op": "addInstrumentTrack", "name": "One", "instrument": "Oscillator"},
            {"op": "addInstrumentTrack", "name": "Two", "instrument": "Sampler"}]})"));
    EXPECT_FALSE(applied.ok);
    EXPECT_TRUE(applied.message.contains("could not build the instrument track")) << applied.message;
    EXPECT_TRUE(applied.message.startsWith("timelineOps[1]")) << applied.message;
    EXPECT_EQ(host->builds, 2);
    EXPECT_EQ(graphDump(), graphBefore);
    EXPECT_EQ(docDump(), docBefore);
}

TEST_F(AIIntegrationServiceProjectEditTest, ApplyWithoutAnEditableDocIsRefused) {
    service->setTimelineOpsHost(nullptr);
    const auto applied = service->applyProjectEdit(parse(kPlan));
    EXPECT_FALSE(applied.ok);
    EXPECT_EQ(applied.message, "Edit plans cannot be applied from here.");
}

// A plugin build or a test has no host; a plan that is only a patch still applies, through
// applyPatch in the mode the preview chose.
TEST_F(AIIntegrationServiceProjectEditTest, ApplyWithoutAHostStillAppliesAPatchOnlyPlan) {
    service->setTimelineOpsHost(nullptr);
    const auto docBefore = docDump();
    const auto applied = service->applyProjectEdit(parse(kMinimalValidPatch));
    ASSERT_TRUE(applied.ok) << applied.message;
    EXPECT_FALSE(applied.merge);
    EXPECT_EQ(graph->getNumNodes(), 2);
    EXPECT_EQ(docDump(), docBefore) << "no timeline write";
}

// -- the track's own envelope -----------------------------------------------------------------------

// The preview resolves the envelope id (a stand-in ADSR with the op's params) without building through
// the host and mutates nothing; the apply binds the same id to the host-built ADSR, so the modulation
// lands on the Filter insert's cutoff and the lane on the envelope.
TEST_F(AIIntegrationServiceProjectEditTest, EnvelopeIdBindsAModulationSourceAndALaneNode) {
    const auto graphBefore = graphDump();
    const auto docBefore = docDump();
    const auto preview = service->previewProjectEdit(parse(kEnvelopePlan));
    ASSERT_TRUE(preview.ok) << preview.message;
    EXPECT_TRUE(
        preview.previewText.contains("instrument: waveform Saw, inserts: Filter, envelope: sustain 0, release 0.15"))
        << preview.previewText;
    EXPECT_EQ(host->builds, 0);
    EXPECT_EQ(graphDump(), graphBefore);
    EXPECT_EQ(docDump(), docBefore);

    const auto applied = service->applyProjectEdit(parse(kEnvelopePlan));
    ASSERT_TRUE(applied.ok) << applied.message;
    EXPECT_EQ(host->builds, 1);

    auto* adsr = firstNodeOfFactoryType(*graph, "ADSR");
    auto* filter = firstNodeOfFactoryType(*graph, "Filter");
    ASSERT_NE(adsr, nullptr);
    ASSERT_NE(filter, nullptr);
    EXPECT_NEAR(rawParamValue(adsr->getProcessor(), "sustain"), 0.0, 1.0e-4) << "the envelope params were applied";
    EXPECT_NEAR(rawParamValue(firstNodeOfFactoryType(*graph, "Oscillator")->getProcessor(), "waveform"), 2.0, 1.0e-3)
        << "the stand-in/host instrument took instrumentParams";
    EXPECT_NEAR(rawParamValue(adsr->getProcessor(), "release"), 0.15, 1.0e-3);

    const int cutoffChannel = dynamic_cast<ModuleBase*>(filter->getProcessor())->modulationChannelForParam("cutoff");
    ASSERT_GE(cutoffChannel, 0);
    bool modulated = false;
    for (auto* node : graph->getNodes())
        modulated = modulated || (dynamic_cast<AttenuverterModule*>(node->getProcessor()) != nullptr &&
                                  graph->isConnected({{adsr->nodeID, 0}, {node->nodeID, 0}}) &&
                                  graph->isConnected({{node->nodeID, 0}, {filter->nodeID, cutoffChannel}}));
    EXPECT_TRUE(modulated) << "envelope -> attenuverter -> Filter cutoff CV";

    EXPECT_NE(doc.getLaneForParam(AIStateMapper::ensureNodeUuid(adsr), "attack"), nullptr)
        << "the lane named the envelope by its id";
}

// "remove" names the envelope by its id too (the bound node is gone afterwards).
TEST_F(AIIntegrationServiceProjectEditTest, EnvelopeIdCanBeRemovedByThePatch) {
    const auto applied = service->applyProjectEdit(parse(R"({"mode": "merge", "nodes": [], "connections": [],
        "remove": [7010],
        "timelineOps": [{"op": "addInstrumentTrack", "name": "Bass", "instrument": "Oscillator",
                         "envelope": {"id": 7010}}]})"));
    ASSERT_TRUE(applied.ok) << applied.message;
    EXPECT_EQ(firstNodeOfFactoryType(*graph, "ADSR"), nullptr);
    EXPECT_NE(firstNodeOfFactoryType(*graph, "Oscillator"), nullptr);
}

// A host whose build reports no envelope (impossible for the real one on an Oscillator/Wavetable)
// leaves an envelope id naming nothing: the plan fails and the graph and doc are restored.
TEST_F(AIIntegrationServiceProjectEditTest, EnvelopeIdOnABuildWithoutAnEnvelopeFailsClearly) {
    graph->addNode(std::make_unique<OscillatorModule>());
    const auto graphBefore = graphDump();
    const auto docBefore = docDump();
    host->withoutEnvelope = true;

    const auto applied = service->applyProjectEdit(parse(kEnvelopePlan));
    EXPECT_FALSE(applied.ok);
    EXPECT_TRUE(applied.message.contains("has no envelope, so the id names nothing")) << applied.message;
    EXPECT_EQ(graphDump(), graphBefore);
    EXPECT_EQ(docDump(), docBefore);
}

// The three worked responses in the local prompt's sound-design section are real plans: each previews
// ok against the project it describes and is scored as what its words ask for.
TEST_F(AIIntegrationServiceProjectEditTest, SoundDesignExamplesInThePromptPreviewAndScore) {
    service->setTimelineToolsEnabled(true);
    graph->addNode(std::make_unique<FilterModule>(), juce::AudioProcessorGraph::NodeID(1003));
    graph->addNode(std::make_unique<ADSRModule>("ADSR"), juce::AudioProcessorGraph::NodeID(1004));
    const juce::var existing = AIStateMapper::graphToJSON(*graph);

    const juce::String prompt = service->getHistory().front().content;
    std::vector<juce::var> examples;
    for (int from = prompt.indexOf("```json\n"); from >= 0; from = prompt.indexOf(from + 1, "```json\n")) {
        const int start = from + 8;
        const juce::var parsed = juce::JSON::parse(prompt.substring(start, prompt.indexOf(start, "```")));
        if (parsed.isObject() &&
            (juce::JSON::toString(parsed).contains("Pluck Lead") ||
             juce::JSON::toString(parsed).contains("Acid Bass") || juce::JSON::toString(parsed).contains("1004")))
            examples.push_back(parsed);
    }
    ASSERT_EQ(examples.size(), 3u) << "pluck, filter envelope and acid examples";

    for (const auto& example : examples) {
        const auto preview = service->previewProjectEdit(example);
        EXPECT_TRUE(preview.ok) << preview.message << "\n" << juce::JSON::toString(example);
    }
    EXPECT_TRUE(soundshape::checkPluck(examples[0]).pass) << soundshape::checkPluck(examples[0]).reason;
    EXPECT_TRUE(soundshape::checkFilterEnvelope(examples[1], existing).pass);
    EXPECT_TRUE(soundshape::checkAcid(examples[2]).pass) << soundshape::checkAcid(examples[2]).reason;
}

// -- asking for a plan ---------------------------------------------------------------------------

TEST_F(AIIntegrationServiceProjectEditTest, HostedRequestIsProjectGenerateWithTheArrangeFieldsAndCurrentPatch) {
    doc.addTrack(TrackKind::Midi, "Melody");
    auto providerPtr = std::make_unique<PlanCapturingProvider>();
    auto* provider = providerPtr.get();
    service->setProvider(std::move(providerPtr));

    service->sendProjectMessage("a wobbly bass", {});
    EXPECT_EQ(provider->lastCapability, "project.generate");
    EXPECT_FALSE(provider->lastBody.hasProperty("currentPatch")) << "an empty graph sends no currentPatch";

    auto osc = graph->addNode(std::make_unique<OscillatorModule>());
    osc->properties.set("uuid", "osc-uuid");
    service->sendProjectMessage("a wobbly bass", {});
    const juce::var body = provider->lastBody;
    ASSERT_TRUE(body.hasProperty("currentPatch"));
    EXPECT_TRUE(body["currentPatch"].isObject()) << "an object, not a JSON string";
    EXPECT_EQ(body["currentPatch"]["nodes"].size(), 1);
    EXPECT_FALSE(body["currentPatch"]["nodes"][0].hasProperty("state"));

    const juce::var arrange = service->buildArrangeRequestBody("a wobbly bass");
    for (const char* key : {"userPrompt", "arrangementContext", "paramTargets", "availableTracks"})
        EXPECT_EQ(juce::JSON::toString(body[key]), juce::JSON::toString(arrange[key])) << key;
    EXPECT_EQ(juce::JSON::toString(service->buildProjectRequestBody("a wobbly bass")), juce::JSON::toString(body));
    EXPECT_EQ((int)body["promptVersion"], 5) << "the server prompt that also returns tempo and markers";

    service->setProjectPromptVersion(3); // the eval harness measuring a newer server prompt
    EXPECT_EQ((int)service->buildProjectRequestBody("a wobbly bass")["promptVersion"], 3);
    service->setProjectPromptVersion(0);
    EXPECT_EQ((int)service->buildProjectRequestBody("a wobbly bass")["promptVersion"], 5) << "0 restores the default";
}

TEST_F(AIIntegrationServiceProjectEditTest, LocalRequestComposesTheSameSectionsAndOffersTheWidenedSchema) {
    doc.addTrack(TrackKind::Midi, "Melody");
    auto providerPtr = std::make_unique<PlanCapturingProvider>();
    providerPtr->hosted = false;
    auto* provider = providerPtr.get();
    service->setProvider(std::move(providerPtr));

    service->sendProjectMessage("a wobbly bass", {});
    ASSERT_FALSE(provider->lastConversation.empty());
    const juce::String content = provider->lastConversation.back().content;
    EXPECT_TRUE(content.startsWith("Current patch is empty.\n\n")) << content;
    const int tracks = content.indexOf("Project tracks:\n```json\n");
    const int targets = content.indexOf("Automation targets:\n```json\n");
    const int prompt = content.indexOf("a wobbly bass");
    EXPECT_GT(tracks, 0);
    EXPECT_GT(targets, tracks);
    EXPECT_GT(prompt, targets);
    EXPECT_TRUE(content.contains("\"timelineOps\"")) << content;
    const auto& history = service->getHistory();
    ASSERT_GE(history.size(), 2u);
    EXPECT_EQ(history[history.size() - 2].role, "user");
    EXPECT_EQ(history[history.size() - 2].content, "a wobbly bass") << "history keeps the raw text";

    graph->addNode(std::make_unique<OscillatorModule>());
    service->sendProjectMessage("again", {});
    EXPECT_TRUE(provider->lastConversation.back().content.startsWith("Current patch state:\n```json\n"));

    auto* properties = provider->lastSchema.getProperty("properties", {}).getDynamicObject();
    ASSERT_NE(properties, nullptr);
    auto* opProperties = properties->getProperty("timelineOps")
                             .getProperty("items", {})
                             .getProperty("properties", {})
                             .getDynamicObject();
    ASSERT_NE(opProperties, nullptr);
    EXPECT_EQ(opProperties->getProperty("nodeId").getProperty("type", {}).toString(), "integer");
    EXPECT_EQ(opProperties->getProperty("instrumentId").getProperty("type", {}).toString(), "integer");
    EXPECT_EQ(opProperties->getProperty("inserts").getProperty("type", {}).toString(), "array");
    EXPECT_EQ(opProperties->getProperty("envelope").getProperty("type", {}).toString(), "object");
    const juce::var modulationItem = properties->getProperty("modulations").getProperty("items", {});
    EXPECT_EQ(
        modulationItem.getProperty("properties", {}).getProperty("destParam", {}).getProperty("type", {}).toString(),
        "string");
    EXPECT_FALSE(modulationItem.getProperty("required", {}).getArray()->contains("destPort"));
    EXPECT_FALSE(modulationItem.getProperty("properties", {}).getProperty("destParam", {}).hasProperty("anyOf"));
}

TEST_F(AIIntegrationServiceProjectEditTest, ProjectRequestBodyCarriesTracksTargetsAndContextAsStructuredFields) {
    auto node = graph->addNode(std::make_unique<OscillatorModule>());
    ASSERT_NE(node, nullptr);
    node->properties.set("uuid", "arrange-uuid-1");
    doc.addTrack(TrackKind::Midi, "Melody");
    doc.addTrack(TrackKind::Automation, "Sweep");
    TransportService transport;
    service->setTimelineContext(&doc, &transport);

    const juce::var body = service->buildProjectRequestBody("build a 16 bar arrangement");
    ASSERT_TRUE(body.isObject());

    // userPrompt is the RAW text: the server composes its own context sections from the fields below.
    EXPECT_EQ(body["userPrompt"].toString(), juce::String("build a 16 bar arrangement"));
    EXPECT_TRUE(body["arrangementContext"].toString().isNotEmpty()) << "a non-empty doc summarises to something";

    ASSERT_TRUE(body["paramTargets"].isArray());
    ASSERT_GT(body["paramTargets"].getArray()->size(), 0) << "the uuid-bearing Oscillator's float params are offered";
    const juce::var target = (*body["paramTargets"].getArray())[0];
    EXPECT_EQ(target["nodeUuid"].toString(), juce::String("arrange-uuid-1"));
    EXPECT_TRUE(target["nodeName"].toString().isNotEmpty());
    EXPECT_TRUE(target["paramId"].toString().isNotEmpty());
    EXPECT_TRUE(target["min"].isDouble() || target["min"].isInt());
    EXPECT_TRUE(target["max"].isDouble() || target["max"].isInt());
    EXPECT_TRUE(target["default"].isDouble() || target["default"].isInt());

    ASSERT_TRUE(body["availableTracks"].isArray());
    ASSERT_EQ(body["availableTracks"].getArray()->size(), 2);
    const juce::var track0 = (*body["availableTracks"].getArray())[0];
    const juce::var track1 = (*body["availableTracks"].getArray())[1];
    EXPECT_EQ(track0["name"].toString(), juce::String("Melody"));
    EXPECT_EQ(track0["kind"].toString(), juce::String("midi"));
    EXPECT_EQ(static_cast<int>(track0["index"]), 0);
    EXPECT_EQ(track1["name"].toString(), juce::String("Sweep"));
    EXPECT_EQ(track1["kind"].toString(), juce::String("automation"));
    EXPECT_EQ(static_cast<int>(track1["index"]), 1);

    // productName is the PROVIDER's field (RemoteProvider adds it).
    EXPECT_FALSE(body.hasProperty("productName"));
}

TEST_F(AIIntegrationServiceProjectEditTest, ProjectRequestParamTargetsAreCappedAtServerMax) {
    // Enough uuid-bearing nodes that the flat float-param count exceeds the cap.
    int paramsPerNode = 0;
    {
        auto probe = graph->addNode(std::make_unique<OscillatorModule>());
        ASSERT_NE(probe, nullptr);
        probe->properties.set("uuid", "probe-uuid");
        for (auto* p : probe->getProcessor()->getParameters())
            if (dynamic_cast<juce::AudioParameterFloat*>(p) != nullptr)
                ++paramsPerNode;
    }
    ASSERT_GT(paramsPerNode, 0);

    const int nodesNeeded = AIIntegrationService::kMaxRemoteParamTargets / paramsPerNode + 1;
    for (int i = 0; i < nodesNeeded; ++i) {
        auto node = graph->addNode(std::make_unique<OscillatorModule>());
        ASSERT_NE(node, nullptr);
        node->properties.set("uuid", "bulk-uuid-" + juce::String(i));
    }

    service->setTimelineToolsEnabled(true);

    const juce::var body = service->buildProjectRequestBody("automate everything");
    ASSERT_TRUE(body["paramTargets"].isArray());
    EXPECT_EQ(body["paramTargets"].getArray()->size(), AIIntegrationService::kMaxRemoteParamTargets)
        << "a longer list would be rejected by the server's input schema before any model saw it";
}

TEST_F(AIIntegrationServiceProjectEditTest, ProjectRequestOnEmptyTimelineSaysSoExplicitly) {
    service->setTimelineToolsEnabled(true);

    const juce::var body = service->buildProjectRequestBody("start an arrangement");

    // The schema requires both keys but allows them empty — "a caller with nothing to say should
    // say so explicitly rather than have the field quietly go missing".
    ASSERT_TRUE(body.hasProperty("arrangementContext"));
    EXPECT_EQ(body["arrangementContext"].toString(), juce::String());
    ASSERT_TRUE(body["availableTracks"].isArray());
    EXPECT_EQ(body["availableTracks"].getArray()->size(), 0);
}

} // namespace synth
