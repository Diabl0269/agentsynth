// Patch apply: JSON extraction, the structural gate, merge-regression gating, listener-callback
// ordering, and error-passthrough from the provider.
#include "AIIntegrationServiceTestFixture.h"

namespace synth {

TEST_F(AIIntegrationServiceTest, ExtractJsonFromResponse) {
    // This tests a private method if we use friend or just test through applyPatch
    // Since it's private and we don't have friend access here easily,
    // we test it indirectly via applyPatch which is public.

    // Test extraction from backticks
    juce::String withBackticks =
        "Here is the patch: ```json\n" + juce::String(kMinimalValidPatch) + "\n``` and some more text.";
    // applyPatch calls extractJsonFromResponse internally
    // We expect it to try and parse the JSON. If it fails, it returns false.
    // If it succeeds, it returns true.
    EXPECT_TRUE(service->applyPatch(withBackticks));
}

TEST_F(AIIntegrationServiceTest, ExtractJsonRaw) { EXPECT_TRUE(service->applyPatch(kMinimalValidPatch)); }

TEST_F(AIIntegrationServiceTest, GetPatchContext) {
    juce::String context = service->getPatchContext();
    EXPECT_TRUE(context.contains("nodes"));
    EXPECT_TRUE(context.contains("connections"));
}

// P3-3 (anonymous trial): a mocked 402 TRIAL_EXHAUSTED response from the provider must reach the
// caller as a distinct AIErrorKind::TrialExhausted with the server's message intact — not
// collapsed into a generic failure. The actual HTTP-status-to-AIErrorKind mapping is RemoteProvider's
// job (see Tests/AI/RemoteProviderTests.cpp's TrialExhaustedMapsToDistinctKindWithServerMessageIntact
// for that); this test locks down that AIIntegrationService::sendMessage() is a transparent
// pass-through and never rewrites error.kind/error.message on the way to the UI-facing callback.
TEST_F(AIIntegrationServiceTest, TrialExhaustedErrorPassesThroughWithServerMessageIntact) {
    auto provider = std::make_unique<MockAIProvider>();
    provider->shouldFail = true;
    provider->mockErrorKind = AIProvider::AIErrorKind::TrialExhausted;
    provider->mockErrorMessage = "Your free trial has been used up. Sign in with Google to continue.";
    service->setProvider(std::move(provider));

    AIProvider::AIResponse received;
    service->sendMessage("make a bass patch",
                         [&received](const AIProvider::AIResponse& response) { received = response; });

    EXPECT_FALSE(received.success);
    EXPECT_EQ(received.error.kind, AIProvider::AIErrorKind::TrialExhausted);
    EXPECT_EQ(received.error.message,
              juce::String("Your free trial has been used up. Sign in with Google to continue."));
}

// Same pass-through guarantee for the other new distinct kind (a service-wide capacity cap,
// unrelated to the caller's own trial/quota).
TEST_F(AIIntegrationServiceTest, ServiceCapacityExceededErrorPassesThroughWithServerMessageIntact) {
    auto provider = std::make_unique<MockAIProvider>();
    provider->shouldFail = true;
    provider->mockErrorKind = AIProvider::AIErrorKind::ServiceCapacityExceeded;
    provider->mockErrorMessage = "The service is at its daily capacity. Please try again later.";
    service->setProvider(std::move(provider));

    AIProvider::AIResponse received;
    service->sendMessage("make a bass patch",
                         [&received](const AIProvider::AIResponse& response) { received = response; });

    EXPECT_FALSE(received.success);
    EXPECT_EQ(received.error.kind, AIProvider::AIErrorKind::ServiceCapacityExceeded);
    EXPECT_EQ(received.error.message, juce::String("The service is at its daily capacity. Please try again later."));
}

TEST_F(AIIntegrationServiceTest, ApplyPatch_MergeMode_PreservesExisting) {
    // Add an existing node to the graph
    graph->addNode(std::make_unique<OscillatorModule>());
    ASSERT_EQ(graph->getNumNodes(), 1);

    // Apply a delta patch in merge mode
    juce::String deltaJson = "{\"nodes\":[{\"id\":100,\"type\":\"Filter\",\"params\":{}}],\"connections\":[]}";
    bool success = service->applyPatch(deltaJson, true);
    ASSERT_TRUE(success);
    ASSERT_EQ(graph->getNumNodes(), 2); // Original Oscillator + new Filter
}

// --- Structural gate: applyPatch() now rejects a schema-valid patch that doesn't produce a
// usable signal path (Source/AI/PatchEval.h), the same bar Tools/AIEvalHarness measures models
// against. See the "Structural gate" comment in AIIntegrationService::applyPatch() for the
// regression-vs-absolute distinction between replace and merge mode. ---

TEST_F(AIIntegrationServiceTest, ReplaceModeWithNoAudioOutputIsRejectedStructurally) {
    bool success = service->applyPatch(R"({"nodes":[],"connections":[]})");

    EXPECT_FALSE(success);
    EXPECT_EQ(service->getLastPatchError(), "no Audio Output node in the patch");
    EXPECT_EQ(graph->getNumNodes(), 0) << "a structurally rejected patch must not touch the live graph";
}

TEST_F(AIIntegrationServiceTest, ReplaceModeNotReachingOutputIsRejectedStructurally) {
    // Audio Output exists but nothing feeds it — no Oscillator anywhere in the patch.
    bool success = service->applyPatch(R"({"nodes":[{"id":1,"type":"Audio Output"}],"connections":[]})");

    EXPECT_FALSE(success);
    // Prefix match, not the exact string: the tail enumerates every module type that counts
    // as a signal source, so it changes whenever one is added. That already broke this
    // assertion once (#165 added Noise, fixed in #164) and again when Wavetable was added.
    // The prefix is the stable part and still pins the failure mode.
    EXPECT_TRUE(service->getLastPatchError().startsWith("Audio Output is not reachable from any"))
        << "actual: " << service->getLastPatchError().toStdString();
}

TEST_F(AIIntegrationServiceTest, StructuralRejectionFiresNoListenerCallbacks) {
    int callCounter = 0;
    CountingListener listener;
    listener.sharedCallCounter = &callCounter;
    service->addListener(&listener);

    bool success = service->applyPatch(R"({"nodes":[],"connections":[]})");

    EXPECT_FALSE(success);
    EXPECT_EQ(listener.aboutToApplyCount, 0);
    EXPECT_EQ(listener.appliedCount, 0);

    service->removeListener(&listener);
}

TEST_F(AIIntegrationServiceTest, MergeRegressionGate_PreExistingGapIsNotBlamedOnTheDelta) {
    // The live graph already has no Oscillator (just a bare Audio Output) — a merge delta that
    // doesn't fix that, but doesn't make it worse either, must not be rejected for a gap it never
    // caused (e.g. the user's own half-built canvas).
    graph->addNode(std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor>(
        juce::AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode));
    ASSERT_EQ(graph->getNumNodes(), 1);

    juce::String delta = "{\"nodes\":[{\"id\":100,\"type\":\"Filter\",\"params\":{}}],\"connections\":[]}";
    bool success = service->applyPatch(delta, /*mergeMode=*/true);

    EXPECT_TRUE(success) << service->getLastPatchError();
    EXPECT_EQ(graph->getNumNodes(), 2);
}

TEST_F(AIIntegrationServiceTest, MergeRegressionGate_RejectsADeltaThatBreaksAWorkingChain) {
    // Live graph: a complete Oscillator(1) -> Audio Output(2) chain. Ids are preserved by the
    // trusted+clearExisting apply path (mirrors undo/redo's own snapshot replay — see the
    // "Preserve node identity" comment in AIStateMapper::applyJSONToGraph), so "remove":[1] below
    // reliably targets the Oscillator.
    //
    // prepareGraphForPatchEval() matters here beyond evaluation: without it the "Audio Output"
    // node reports zero channels and the seed connection below silently no-ops, so the "before"
    // state would already read as unreachable regardless of this test's fixture. A real live
    // graph is always configured this way by AudioEngine before AIIntegrationService ever runs.
    prepareGraphForPatchEval(*graph);
    juce::var seed = juce::JSON::parse(juce::String(kMinimalValidPatch));
    ASSERT_TRUE(AIStateMapper::applyJSONToGraph(seed, *graph, /*clearExisting=*/true, /*trusted=*/true));
    ASSERT_EQ(graph->getNumNodes(), 2);

    // "Remove the Oscillator" without reconnecting anything: the chain regresses.
    bool success = service->applyPatch(R"({"mode":"merge","remove":[1],"nodes":[],"connections":[]})",
                                       /*mergeMode=*/true);

    EXPECT_FALSE(success);
    // Prefix match, not the exact string: the tail enumerates every module type that counts
    // as a signal source, so it changes whenever one is added. That already broke this
    // assertion once (#165 added Noise, fixed in #164) and again when Wavetable was added.
    // The prefix is the stable part and still pins the failure mode.
    EXPECT_TRUE(service->getLastPatchError().startsWith("Audio Output is not reachable from any"))
        << "actual: " << service->getLastPatchError().toStdString();
    EXPECT_EQ(graph->getNumNodes(), 2) << "a structurally rejected merge must not touch the live graph";
}

TEST_F(AIIntegrationServiceTest, ApplyPatch_DefaultReplace_ClearsGraph) {
    // Add an existing node to the graph
    graph->addNode(std::make_unique<OscillatorModule>());
    ASSERT_EQ(graph->getNumNodes(), 1);

    // Apply a full patch without merge mode (default)
    bool success = service->applyPatch(kMinimalValidPatch);
    ASSERT_TRUE(success);
    ASSERT_EQ(graph->getNumNodes(), 2); // Only kMinimalValidPatch's 2 nodes; the old Oscillator is gone
}

TEST_F(AIIntegrationServiceTest, OutgoingRequestStillIncludesPatchContext) {
    graph->addNode(std::make_unique<OscillatorModule>());

    auto provider = std::make_unique<MockAIProvider>();
    auto* rawProvider = provider.get();
    service->setProvider(std::move(provider));

    service->sendMessage("Add a filter", nullptr, true);

    ASSERT_FALSE(rawProvider->lastConversation.empty());
    EXPECT_TRUE(rawProvider->lastConversation.back().content.contains("Current patch state"));
    EXPECT_TRUE(rawProvider->lastConversation.back().content.contains("Add a filter"));
}

// Regression: buildPatchAugmentedContent() used to silently return bare `text` when the live
// graph had zero nodes, giving the model no signal either way about whether a patch already
// exists. A fresh-session "create a bass patch" request needs an explicit "empty" marker instead,
// or the model has no way to distinguish "there is a patch but you weren't told about it" from
// "there is genuinely nothing yet".
TEST_F(AIIntegrationServiceTest, OutgoingRequestOnEmptyGraphStatesPatchIsEmpty) {
    // No nodes added to `graph` — deliberately left empty.
    auto provider = std::make_unique<MockAIProvider>();
    auto* rawProvider = provider.get();
    service->setProvider(std::move(provider));

    service->sendMessage("Create a fat bass patch", nullptr, true);

    ASSERT_FALSE(rawProvider->lastConversation.empty());
    const auto& content = rawProvider->lastConversation.back().content;
    EXPECT_TRUE(content.contains("Current patch is empty"));
    EXPECT_TRUE(content.contains("Create a fat bass patch"));
    // Must not claim a patch state block that doesn't exist.
    EXPECT_FALSE(content.contains("Current patch state"));
}

// Regression: buildPatchAugmentedContent() used to embed graphToJSON() verbatim, including each
// node's "state" object. For a Sampler that is an absolute disk path; for a Hosted Plugin it would
// be the third-party plugin's opaque state blob. Neither is something the model can author (the
// trusted-only rule in AIStateMapper), so it must never leave the machine in the request payload.
TEST_F(AIIntegrationServiceTest, OutgoingRequestStripsNodeStateFromPatchContext) {
    auto file = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("ai-context-146.wav");
    file.deleteFile();
    {
        juce::AudioBuffer<float> buffer(1, 512);
        buffer.clear();
        juce::WavAudioFormat wavFormat;
        std::unique_ptr<juce::FileOutputStream> stream(file.createOutputStream());
        ASSERT_NE(stream, nullptr);
        std::unique_ptr<juce::AudioFormatWriter> writer(wavFormat.createWriterFor(stream.get(), 44100.0, 1, 32, {}, 0));
        ASSERT_NE(writer, nullptr);
        stream.release(); // writer owns the stream now
        writer->writeFromAudioSampleBuffer(buffer, 0, 512);
    }

    auto* sampler = graph->addNode(std::make_unique<SamplerModule>())->getProcessor();
    ASSERT_TRUE(dynamic_cast<SamplerModule*>(sampler)->loadSampleFile(file));

    auto provider = std::make_unique<MockAIProvider>();
    auto* rawProvider = provider.get();
    service->setProvider(std::move(provider));

    service->sendMessage("Add a filter", nullptr, true);

    ASSERT_FALSE(rawProvider->lastConversation.empty());
    const auto& content = rawProvider->lastConversation.back().content;
    EXPECT_TRUE(content.contains("Current patch state"));
    EXPECT_TRUE(content.contains("\"Sampler\""));
    EXPECT_FALSE(content.contains("\"state\""));
    EXPECT_FALSE(content.contains(file.getFullPathName()));
    EXPECT_FALSE(content.contains("ai-context-146.wav"));

    file.deleteFile();
}

TEST_F(AIIntegrationServiceTest, InvalidJsonFiresNoListenerCallbacks) {
    int callCounter = 0;
    CountingListener listener;
    listener.sharedCallCounter = &callCounter;
    service->addListener(&listener);

    bool success = service->applyPatch("not json at all");

    EXPECT_FALSE(success);
    EXPECT_EQ(listener.aboutToApplyCount, 0);
    EXPECT_EQ(listener.appliedCount, 0);

    service->removeListener(&listener);
}

TEST_F(AIIntegrationServiceTest, StructurallyInvalidPatchFiresNoListenerCallbacks) {
    int callCounter = 0;
    CountingListener listener;
    listener.sharedCallCounter = &callCounter;
    service->addListener(&listener);

    bool success = service->applyPatch("{\"nodes\": \"not-an-array\"}");

    EXPECT_FALSE(success);
    EXPECT_EQ(listener.aboutToApplyCount, 0);
    EXPECT_EQ(listener.appliedCount, 0);

    service->removeListener(&listener);
}

TEST_F(AIIntegrationServiceTest, ValidPatchFiresBothCallbacksInOrder) {
    int callCounter = 0;
    CountingListener listener;
    listener.sharedCallCounter = &callCounter;
    service->addListener(&listener);

    bool success = service->applyPatch(kMinimalValidPatch);

    EXPECT_TRUE(success);
    EXPECT_EQ(listener.aboutToApplyCount, 1);
    EXPECT_EQ(listener.appliedCount, 1);
    EXPECT_LT(listener.aboutToApplyOrder, listener.appliedOrder);

    service->removeListener(&listener);
}

} // namespace synth
