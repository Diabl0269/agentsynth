// Behaviour of AIIntegrationService::applyPatchWithRetry — the validate-and-retry loop that
// feeds a rejection reason back to the model instead of surfacing a dead Apply button.
//
// The provider double answers synchronously, so each applyPatchWithRetry() call has fully
// resolved by the time it returns and the assertions need no message loop.

#include "AI/AIIntegrationService.h"
#include "AI/AIStateMapper.h"
#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>

namespace synth {
namespace {

// A patch naming a module type the factory does not know — rejected as UnknownNodeType.
constexpr const char* kInvalidPatch = R"({"nodes":[{"id":1,"type":"HyperResonator"}],"connections":[]})";

// A second, differently-invalid patch, so a test can tell "model repeated itself" from
// "model changed its answer but is still wrong".
constexpr const char* kOtherInvalidPatch = R"({"nodes":[{"id":1,"type":"Oscillator"},
                                                       {"id":1,"type":"Filter"}],"connections":[]})";

constexpr const char* kValidPatch = R"({"nodes":[{"id":1,"type":"Oscillator"},{"id":2,"type":"Audio Output"}],
                                        "connections":[{"src":1,"srcPort":0,"dst":2,"dstPort":0}]})";

// Replays a fixed script of responses and records every conversation it was sent, so a test can
// assert what the model was actually told. Once the script runs out the last entry repeats,
// which is what makes the unbounded-loop case expressible.
class ScriptedProvider : public AIProvider {
public:
    explicit ScriptedProvider(std::vector<juce::String> scriptedResponses)
        : script(std::move(scriptedResponses)) {}

    RequestId sendPrompt(const std::vector<Message>& conversation, CompletionCallback callback, const juce::var&,
                         std::function<void(const juce::String&)>) override {
        conversations.push_back(conversation);

        AIResponse response;
        response.success = true;
        response.content = script[juce::jmin((size_t)callCount, script.size() - 1)];
        ++callCount;

        if (callback)
            callback(response);
        return {};
    }

    void cancel(RequestId) override {}
    void setModel(const juce::String& name) override { model = name; }
    juce::String getCurrentModel() const override { return model; }
    juce::String getProviderName() const override { return "ScriptedProvider"; }
    void fetchAvailableModels(std::function<void(const juce::StringArray&, bool)> callback) override {
        callback({"scripted"}, true);
    }

    /** Content of the most recent user turn — what the retry actually said to the model. */
    juce::String lastUserMessage() const {
        if (conversations.empty())
            return {};
        const auto& last = conversations.back();
        for (auto it = last.rbegin(); it != last.rend(); ++it)
            if (it->role == "user")
                return it->content;
        return {};
    }

    std::vector<juce::String> script;
    std::vector<std::vector<Message>> conversations;
    int callCount = 0;
    juce::String model;
};

} // namespace

TEST(AIPatchRetryTest, RetryOnValidationFailureFeedsErrorBackToModel) {
    juce::AudioProcessorGraph graph;
    AIIntegrationService service(graph);

    auto provider = std::make_unique<ScriptedProvider>(std::vector<juce::String>{kValidPatch});
    auto* providerPtr = provider.get();
    service.setProvider(std::move(provider));

    bool completed = false;
    bool succeeded = false;
    juce::String reportedError = "unset";
    std::vector<AIIntegrationService::PatchRetryInfo> retries;

    service.applyPatchWithRetry(
        kInvalidPatch, /*mergeMode=*/false,
        [&](bool ok, const juce::String& error) {
            completed = true;
            succeeded = ok;
            reportedError = error;
        },
        [&](const AIIntegrationService::PatchRetryInfo& info) { retries.push_back(info); });

    ASSERT_TRUE(completed) << "onComplete must be invoked exactly once";
    EXPECT_TRUE(succeeded) << "the corrected patch should have been applied, error: " << reportedError;

    // The second, valid patch is the one that reached the graph.
    EXPECT_EQ(graph.getNumNodes(), 2);

    // Exactly one correction round-trip: the first patch was ours, the second came from the model.
    EXPECT_EQ(providerPtr->callCount, 1);
    ASSERT_EQ(retries.size(), 1u);
    EXPECT_EQ(retries[0].failedAttempt, 1);
    EXPECT_EQ(retries[0].totalAttempts, AIIntegrationService::kMaxPatchRetries + 1);

    // The specific reason — not a generic "try again" — is what the model was sent.
    const juce::String sent = providerPtr->lastUserMessage();
    EXPECT_TRUE(sent.contains("HyperResonator"))
        << "the validation message must reach the model, but it was sent: " << sent;
    EXPECT_TRUE(sent.containsIgnoreCase("rejected")) << "sent: " << sent;
    EXPECT_TRUE(retries[0].error.contains("HyperResonator"));
}

// RemoteProvider (Source/AI/RemoteProvider.h) sends only the LAST conversation message, never
// full history — so unlike OllamaProvider, a correction turn that doesn't itself restate the
// original request reaches the model as a bare "fix this JSON" with no idea what the patch was
// even supposed to be. Verified live against the real service: it invents an unrelated patch
// referencing node ids that don't exist anywhere. buildCorrectionPrompt() must restate it.
TEST(AIPatchRetryTest, CorrectionPromptRestatesOriginalRequest) {
    juce::AudioProcessorGraph graph;
    AIIntegrationService service(graph);

    // First answer is schema-valid but structurally empty (no Audio Output) — trips the
    // structural gate, not schema validation, so this also covers the gate feeding the retry loop.
    // Second answer is valid, so the retry cycle completes.
    auto provider =
        std::make_unique<ScriptedProvider>(std::vector<juce::String>{R"({"nodes":[],"connections":[]})", kValidPatch});
    auto* providerPtr = provider.get();
    service.setProvider(std::move(provider));

    juce::String firstResponseContent;
    service.sendMessage(
        "Design a bell-like FM tone",
        [&](const AIProvider::AIResponse& response) { firstResponseContent = response.content; },
        /*useStructuredOutput=*/true);

    bool succeeded = false;
    service.applyPatchWithRetry(firstResponseContent, /*mergeMode=*/false,
                                [&](bool ok, const juce::String&) { succeeded = ok; });

    EXPECT_TRUE(succeeded);
    ASSERT_EQ(providerPtr->callCount, 2) << "the initial sendMessage plus one correction round-trip";

    const juce::String correctionSent = providerPtr->lastUserMessage();
    EXPECT_TRUE(correctionSent.contains("Design a bell-like FM tone"))
        << "the correction prompt must restate the original request on its own, since RemoteProvider "
        << "never sees conversation history — but it was sent: " << correctionSent;
}

TEST(AIPatchRetryTest, RetryIsBounded) {
    juce::AudioProcessorGraph graph;
    AIIntegrationService service(graph);

    // Always invalid, however many times it is asked.
    auto provider = std::make_unique<ScriptedProvider>(std::vector<juce::String>{kOtherInvalidPatch});
    auto* providerPtr = provider.get();
    service.setProvider(std::move(provider));

    bool completed = false;
    bool succeeded = true;
    juce::String reportedError;
    int retryCount = 0;

    service.applyPatchWithRetry(
        kInvalidPatch, /*mergeMode=*/false,
        [&](bool ok, const juce::String& error) {
            completed = true;
            succeeded = ok;
            reportedError = error;
        },
        [&](const AIIntegrationService::PatchRetryInfo&) { ++retryCount; });

    ASSERT_TRUE(completed) << "it must give up and report, not loop";
    EXPECT_FALSE(succeeded);

    // It stops after the bound rather than retrying indefinitely.
    EXPECT_EQ(providerPtr->callCount, AIIntegrationService::kMaxPatchRetries);
    EXPECT_EQ(retryCount, AIIntegrationService::kMaxPatchRetries);

    // The failure is surfaced with a reason, not swallowed.
    EXPECT_TRUE(reportedError.isNotEmpty());
    EXPECT_EQ(reportedError, service.getLastPatchError());

    // Nothing partially applied along the way.
    EXPECT_EQ(graph.getNumNodes(), 0);
}

TEST(AIPatchRetryTest, ValidPatchNeverContactsTheModel) {
    juce::AudioProcessorGraph graph;
    AIIntegrationService service(graph);

    auto provider = std::make_unique<ScriptedProvider>(std::vector<juce::String>{kInvalidPatch});
    auto* providerPtr = provider.get();
    service.setProvider(std::move(provider));

    bool succeeded = false;
    int retryCount = 0;
    service.applyPatchWithRetry(
        kValidPatch, /*mergeMode=*/false, [&](bool ok, const juce::String&) { succeeded = ok; },
        [&](const AIIntegrationService::PatchRetryInfo&) { ++retryCount; });

    EXPECT_TRUE(succeeded);
    EXPECT_EQ(providerPtr->callCount, 0) << "a patch that already validates must not cost a round-trip";
    EXPECT_EQ(retryCount, 0);
    EXPECT_EQ(graph.getNumNodes(), 2);
}

TEST(AIPatchRetryTest, WithoutProviderTheRejectionIsReportedNotRetried) {
    juce::AudioProcessorGraph graph;
    AIIntegrationService service(graph); // no provider installed

    bool completed = false;
    bool succeeded = true;
    juce::String reportedError;

    service.applyPatchWithRetry(kInvalidPatch, /*mergeMode=*/false, [&](bool ok, const juce::String& error) {
        completed = true;
        succeeded = ok;
        reportedError = error;
    });

    EXPECT_TRUE(completed);
    EXPECT_FALSE(succeeded);
    EXPECT_TRUE(reportedError.isNotEmpty()) << "the user must still learn why the patch was refused";
}

// The retry must not smuggle in a different failure: a patch rejected for a reason the model
// then "fixes" into a *new* violation still ends in a reported failure, not a silent apply.
TEST(AIPatchRetryTest, StillFailsWhenEveryCorrectionIsInvalid) {
    juce::AudioProcessorGraph graph;
    AIIntegrationService service(graph);

    auto provider = std::make_unique<ScriptedProvider>(
        std::vector<juce::String>{kOtherInvalidPatch, R"({"nodes":[{"id":1,"type":"Oscillator"}],
                                                          "connections":[{"src":1,"srcPort":0,"dst":77,"dstPort":0}]})"});
    service.setProvider(std::move(provider));

    bool succeeded = true;
    juce::String reportedError;
    service.applyPatchWithRetry(kInvalidPatch, /*mergeMode=*/false, [&](bool ok, const juce::String& error) {
        succeeded = ok;
        reportedError = error;
    });

    EXPECT_FALSE(succeeded);
    EXPECT_TRUE(reportedError.isNotEmpty());
    EXPECT_EQ(graph.getNumNodes(), 0);
    // The last failure reported is the last one that actually happened, not the original.
    EXPECT_EQ(service.getLastPatchErrorCode(), PatchValidationError::ConnectionUnknownNode);
}

// ---------------------------------------------------------------------------------------------
// The mode repair: a rejected, mode-less patch is retried as a merge before the model is bothered.
// These pin down that it fires ONLY in that case — a repair that fired more widely would be the
// kind of silent mutation the strict gate exists to prevent.
// ---------------------------------------------------------------------------------------------

namespace {

// Seeds a graph and returns the id of a node a merge patch can legitimately reference.
int seedGraphWithOutput(juce::AudioProcessorGraph& graph) {
    auto node = graph.addNode(std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor>(
        juce::AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode));
    return node != nullptr ? (int)node->nodeID.uid : -1;
}

// A delta that only makes sense as a merge: it wires a new node to an id it does not itself create.
juce::String deltaReferencing(int existingId, const juce::String& mode = {}) {
    juce::String modeField = mode.isNotEmpty() ? ("\"mode\":\"" + mode + "\",") : juce::String();
    return "{" + modeField +
           "\"nodes\":[{\"id\":9001,\"type\":\"Reverb\"}],"
           "\"connections\":[{\"src\":9001,\"srcPort\":0,\"dst\":" +
           juce::String(existingId) + ",\"dstPort\":0}]}";
}

} // namespace

TEST(AIPatchRetryTest, ModelessMergeDeltaAppliedAsReplaceIsRepairedToMerge) {
    juce::AudioProcessorGraph graph;
    const int existingId = seedGraphWithOutput(graph);
    ASSERT_GT(existingId, 0);
    const int nodesBefore = graph.getNumNodes();

    AIIntegrationService service(graph);

    // The caller guessed "replace" (mergeMode=false) and the model stated no mode.
    EXPECT_TRUE(service.applyPatch(deltaReferencing(existingId), /*mergeMode=*/false));
    EXPECT_TRUE(service.didLastPatchRepairMode());

    // Repaired to a merge, so the pre-existing node survived and the new one was added.
    EXPECT_EQ(graph.getNumNodes(), nodesBefore + 1);
    EXPECT_NE(graph.getNodeForId(juce::AudioProcessorGraph::NodeID((juce::uint32)existingId)), nullptr)
        << "a merge must never destroy the node the patch referenced";
}

TEST(AIPatchRetryTest, RepairDoesNotFireWhenTheModelStatedReplace) {
    juce::AudioProcessorGraph graph;
    const int existingId = seedGraphWithOutput(graph);
    ASSERT_GT(existingId, 0);

    AIIntegrationService service(graph);

    // An explicit "replace" is an intent the repair must not override, so this stays rejected.
    EXPECT_FALSE(service.applyPatch(deltaReferencing(existingId, "replace"), /*mergeMode=*/false));
    EXPECT_FALSE(service.didLastPatchRepairMode());
    EXPECT_EQ(service.getLastPatchErrorCode(), PatchValidationError::ConnectionUnknownNode);
}

TEST(AIPatchRetryTest, RepairNeverTurnsAMergeIntoAReplace) {
    juce::AudioProcessorGraph graph;
    const int existingId = seedGraphWithOutput(graph);
    ASSERT_GT(existingId, 0);
    const int nodesBefore = graph.getNumNodes();

    AIIntegrationService service(graph);

    // Invalid whichever way it is read. The repair is one-directional, so nothing here may cause
    // the existing graph to be cleared.
    EXPECT_FALSE(service.applyPatch(R"({"nodes":[{"id":1,"type":"HyperResonator"}],"connections":[]})",
                                    /*mergeMode=*/true));
    EXPECT_FALSE(service.didLastPatchRepairMode());
    EXPECT_EQ(graph.getNumNodes(), nodesBefore) << "a rejected patch must leave the graph untouched";
}

TEST(AIPatchRetryTest, RepairDoesNotFireForPatchesInvalidInEitherMode) {
    juce::AudioProcessorGraph graph;
    seedGraphWithOutput(graph);

    AIIntegrationService service(graph);

    // Unknown module type is unknown in both modes; the repair must not mask a real error.
    EXPECT_FALSE(service.applyPatch(R"({"nodes":[{"id":1,"type":"HyperResonator"}],"connections":[]})",
                                    /*mergeMode=*/false));
    EXPECT_FALSE(service.didLastPatchRepairMode());
    EXPECT_EQ(service.getLastPatchErrorCode(), PatchValidationError::UnknownNodeType);
}

TEST(AIPatchRetryTest, RepairIsNotNeededForAlreadyValidReplacePatches) {
    juce::AudioProcessorGraph graph;
    seedGraphWithOutput(graph);

    AIIntegrationService service(graph);

    EXPECT_TRUE(service.applyPatch(kValidPatch, /*mergeMode=*/false));
    EXPECT_FALSE(service.didLastPatchRepairMode()) << "a patch that validates as-is must not be reinterpreted";
}

} // namespace synth
