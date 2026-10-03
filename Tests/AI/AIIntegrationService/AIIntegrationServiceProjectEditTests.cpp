// Edit plans through the service, headless: the plan-wide rules (one id namespace, the mode rule,
// how a writeLane names its node), a preview that mutates nothing, the all-or-nothing backstop,
// and how a plan is asked for (project.generate hosted, sendPrompt locally). The apply order end to
// end through the real app host is Tests/Mixer/ChannelFlow/ChannelFlowProjectEditTests.cpp.
#include "AIIntegrationServiceTestFixture.h"
#include "AppUndoManager.h"
#include "MacroSet.h"
#include "Modules/FilterModule.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"

namespace synth {

namespace {

juce::var parse(const juce::String& json) {
    const juce::var value = juce::JSON::parse(json);
    EXPECT_FALSE(value.isVoid()) << "test JSON did not parse: " << json;
    return value;
}

// Builds a track like the app would, minus wiring: the doc track, the instrument and each insert,
// each with a uuid. `failOnBuild` makes that build (1-based) fail AFTER leaving a stray node behind,
// breaking the host contract on purpose so the service's backstop is what restores the graph.
class PlanFakeHost : public TimelineOpsHost {
public:
    PlanFakeHost(TimelineDoc& d, juce::AudioProcessorGraph& g)
        : doc(d)
        , graph(g) {}

    std::optional<InstrumentTrackBuildResult>
    addInstrumentTrack(const juce::String& name, const juce::String& type, bool,
                       const std::vector<InstrumentTrackInsert>& inserts) override {
        ++builds;
        if (builds == failOnBuild) {
            graph.addNode(AIStateMapper::createModule("LFO"));
            return std::nullopt;
        }
        if (!doc.addTrack(TrackKind::Midi, name).isValid())
            return std::nullopt;
        InstrumentTrackBuildResult result;
        result.instrumentUuid = add(type, {});
        for (const auto& insert : inserts)
            result.insertUuids.push_back(add(insert.type, insert.params));
        return result;
    }
    bool recordBatch(const std::function<void()>& mutation) override {
        ++batches;
        return undo.recordGraphTimelineAndMacroChange(graph, doc, macros, mutation);
    }
    TimelineDoc* editableTimelineDoc() override { return &doc; }

    TimelineDoc& doc;
    juce::AudioProcessorGraph& graph;
    AppUndoManager undo;
    MacroSet macros;
    int builds = 0, batches = 0, failOnBuild = 0;

private:
    juce::String add(const juce::String& type, const juce::var& params) {
        auto processor = AIStateMapper::createModule(type);
        if (auto* p = params.getDynamicObject())
            AIStateMapper::applyUntrustedParams(processor.get(), p);
        return AIStateMapper::ensureNodeUuid(graph.addNode(std::move(processor)).get());
    }
};

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
    const juce::var modulationItem = properties->getProperty("modulations").getProperty("items", {});
    EXPECT_EQ(
        modulationItem.getProperty("properties", {}).getProperty("destParam", {}).getProperty("type", {}).toString(),
        "string");
    EXPECT_FALSE(modulationItem.getProperty("required", {}).getArray()->contains("destPort"));
    EXPECT_FALSE(modulationItem.getProperty("properties", {}).getProperty("destParam", {}).hasProperty("anyOf"));
}

} // namespace synth
