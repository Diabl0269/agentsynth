/*
    AIPatchHarness — offline measurement of how often AI-generated patches actually validate.

    This is a measurement instrument, not a test. It talks to a real Ollama instance and
    replays a fixed set of realistic user prompts through the exact production path an
    Apply/Merge click takes:

        AIIntegrationService::sendMessage(useStructuredOutput=true)
          -> OllamaProvider (schema passed as `format`)
          -> AIIntegrationService::extractJsonFromResponse
          -> juce::JSON::parse
          -> AIStateMapper::validatePatch(..., trusted=false)

    and tallies the PatchValidationError of every rejection. Because it needs a live model
    it is excluded from the test target and from CI; build it explicitly with
    -DENABLE_AI_HARNESS=ON.

        ./build/Tools/AIPatchHarness/AIPatchHarness [--provider ollama|remote] [--model M] [--runs N]
            [--host URL] [--limit N] [--json OUT]

    Exit code is 0 whenever the run completed, whatever the pass rate — the histogram is
    the output, not a pass/fail verdict.
*/

#include "AI/AIIntegrationService.h"
#include "AI/AIProvider.h"
#include "AI/AIStateMapper.h"
#include "AI/OllamaProvider.h"
#include "AI/RemoteProvider.h"
#include "Scenarios.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include <cstdio>
#include <map>
#include <vector>

namespace {

using synth::harness::Scenario;
using synth::harness::scenarios;

// Wraps the real provider (Ollama or remote) and records the raw content of every completion
// that passes through sendPrompt(), in call order. For a scenario with N applyPatchWithRetry
// correction round-trips, capturedResponses() ends up [initial answer, retry 1 answer, retry 2
// answer, ...] — exactly the sequence Tests/AIPatchFixtureReplayTests.cpp needs to feed a
// provider double that replays this scenario offline. Everything else is a pure pass-through;
// this must not change provider behaviour, only observe it.
class RecordingProvider : public synth::AIProvider {
public:
    explicit RecordingProvider(std::unique_ptr<synth::AIProvider> providerToWrap)
        : inner(std::move(providerToWrap)) {}

    RequestId sendPrompt(const std::vector<Message>& conversation, CompletionCallback callback,
                         const juce::var& responseSchema, std::function<void(const juce::String&)> onDelta) override {
        return inner->sendPrompt(
            conversation,
            [this, callback](const AIResponse& response) {
                // Only a successful completion has a raw model answer worth replaying; a provider
                // error (network, timeout) leaves nothing to record and the scenario already
                // reports it separately.
                if (response.success)
                    responses.push_back(response.content);
                if (callback)
                    callback(response);
            },
            responseSchema, std::move(onDelta));
    }

    void cancel(RequestId requestId) override { inner->cancel(requestId); }
    juce::String getProviderName() const override { return inner->getProviderName(); }
    void setModel(const juce::String& name) override { inner->setModel(name); }
    juce::String getCurrentModel() const override { return inner->getCurrentModel(); }
    void setRequestTimeoutMs(int timeoutMs) override { inner->setRequestTimeoutMs(timeoutMs); }
    int getRequestTimeoutMs() const override { return inner->getRequestTimeoutMs(); }
    void fetchAvailableModels(std::function<void(const juce::StringArray&, bool)> callback) override {
        inner->fetchAvailableModels(std::move(callback));
    }
    void setAuthToken(const juce::String& token) override { inner->setAuthToken(token); }
    void setConversationId(const juce::String& id) override { inner->setConversationId(id); }
    bool isHosted() const override { return inner->isHosted(); }

    std::vector<juce::String> responses;

private:
    std::unique_ptr<synth::AIProvider> inner;
};

juce::String argValue(const juce::StringArray& args, const juce::String& flag, const juce::String& fallback) {
    int i = args.indexOf(flag);
    if (i >= 0 && i + 1 < args.size())
        return args[i + 1];
    return fallback;
}

// Result of replaying one scenario once.
struct Outcome {
    bool responded = false; // the provider returned a successful completion at all
    bool parsed = false;    // the response contained parseable JSON
    bool valid = false;     // validatePatch accepted the model's FIRST answer
    synth::PatchValidationError error = synth::PatchValidationError::None;
    juce::String message;

    // What the user actually ends up with: the same patch put through applyPatchWithRetry, so a
    // first answer that was rejected still counts as a success if a bounded correction fixed it.
    bool appliedAfterRetry = false;
    int retriesUsed = 0;
    bool modeRepaired = false;
    juce::String finalError;

    // The raw model text behind `valid`/`error` above (before extractJsonFromResponse/JSON::parse)
    // — empty whenever `responded` is false. Recorded so a fixture-replay test can walk the exact
    // same bytes through the production path offline, with no model in the loop.
    juce::String rawResponse;

    // The raw answer to each applyPatchWithRetry() correction round-trip, in order — what a
    // replay's provider double must hand back when the retry loop asks for one. Length equals
    // `retriesUsed`, unless a retry itself hit a provider error (rare), in which case it is
    // shorter than retriesUsed; a replay's double should repeat its last scripted entry rather
    // than require an exact length match, mirroring Tests/AIPatchRetryTests.cpp's ScriptedProvider.
    std::vector<juce::String> retryResponses;
};

Outcome runScenario(const Scenario& scenario, const juce::String& host, const juce::String& model,
                    const juce::String& providerKind) {
    Outcome outcome;

    juce::AudioProcessorGraph graph;
    if (scenario.seedPatch != nullptr) {
        juce::var seed = juce::JSON::parse(juce::String(scenario.seedPatch));
        // trusted=true: this is our own fixture, not model output. It must not be subject to
        // the gate we are measuring, or a seed failure would masquerade as a model failure.
        if (!synth::AIStateMapper::applyJSONToGraph(seed, graph, /*clearExisting=*/true, /*trusted=*/true)) {
            outcome.message = "harness error: seed patch failed to apply";
            return outcome;
        }
    }

    synth::AIIntegrationService service(graph);
    std::unique_ptr<synth::AIProvider> provider;
    if (providerKind == "remote") {
        auto remoteProvider = std::make_unique<synth::RemoteProvider>(host);
        remoteProvider->setTestMode(true); // deliver callbacks synchronously; no message loop here
        provider = std::move(remoteProvider);
    } else {
        auto ollamaProvider = std::make_unique<synth::OllamaProvider>(host);
        ollamaProvider->setTestMode(true); // deliver callbacks synchronously; no message loop here
        ollamaProvider->setModel(model);
        provider = std::move(ollamaProvider);
    }
    auto recordingProvider = std::make_unique<RecordingProvider>(std::move(provider));
    RecordingProvider* recording = recordingProvider.get();
    service.setProvider(std::move(recordingProvider));

    juce::WaitableEvent done;
    juce::String responseText;
    bool success = false;

    service.sendMessage(
        scenario.prompt,
        [&](const synth::AIProvider::AIResponse& response) {
            // On failure `content` is empty and the reason lives in error.message; report whichever
            // is actually populated so a provider error is never logged as a blank line.
            responseText = response.success ? response.content : response.error.message;
            success = response.success;
            done.signal();
        },
        /*useStructuredOutput=*/true);

    // Must exceed OllamaProvider's own kChatRequestTimeoutMs (currently 240s) with margin, or this
    // outer wait fires first and reports a vague "timed out" instead of OllamaProvider's real
    // provider-error message.
    if (!done.wait(270000)) {
        outcome.message = "timed out waiting for model";
        return outcome;
    }
    if (!success) {
        outcome.message = "provider error: " + responseText;
        return outcome;
    }
    outcome.responded = true;

    juce::var json = juce::JSON::parse(synth::AIIntegrationService::extractJsonFromResponse(responseText));
    outcome.parsed = json.isObject();

    const auto validation =
        synth::AIStateMapper::validatePatch(json, graph, /*clearExisting=*/!scenario.mergeMode, /*trusted=*/false);
    outcome.valid = validation.ok;
    outcome.error = validation.error;
    if (!validation.ok)
        outcome.message = validation.message;

    // Now replay the same answer through the production apply path, which may ask the model to
    // correct itself. This is the number a user experiences, as opposed to the raw first-shot rate.
    juce::WaitableEvent applied;
    service.applyPatchWithRetry(
        responseText, scenario.mergeMode,
        [&](bool ok, const juce::String& error) {
            outcome.appliedAfterRetry = ok;
            outcome.finalError = error;
            applied.signal();
        },
        [&](const synth::AIIntegrationService::PatchRetryInfo&) { ++outcome.retriesUsed; });

    if (!applied.wait(600000))
        outcome.finalError = "timed out during retry";
    outcome.modeRepaired = service.didLastPatchRepairMode();

    // recording->responses[0] is the initial sendMessage() answer above (== responseText);
    // everything after it is one raw answer per applyPatchWithRetry() correction round-trip, in
    // the order the model was asked for them.
    if (!recording->responses.empty()) {
        outcome.rawResponse = recording->responses.front();
        outcome.retryResponses.assign(recording->responses.begin() + 1, recording->responses.end());
    }

    return outcome;
}

} // namespace

int main(int argc, char* argv[]) {
    juce::ScopedJuceInitialiser_GUI juceInit;

    juce::StringArray args;
    for (int i = 1; i < argc; ++i)
        args.add(juce::String(argv[i]));

    const juce::String providerKind = argValue(args, "--provider", "ollama");
    const juce::String defaultHost = providerKind == "remote" ? "http://localhost:8787" : "http://localhost:11434";
    const juce::String host = argValue(args, "--host", defaultHost);
    const juce::String model = argValue(args, "--model", "gemma4:12b-it-qat");
    const int runs = juce::jmax(1, argValue(args, "--runs", "1").getIntValue());
    const int limit = argValue(args, "--limit", "0").getIntValue();
    const juce::String jsonOut = argValue(args, "--json", "");

    std::printf("AIPatchHarness  provider=%s  host=%s  model=%s  runs=%d  scenarios=%d\n", providerKind.toRawUTF8(),
                host.toRawUTF8(), model.toRawUTF8(), runs, (int)scenarios().size());
    std::printf("%-20s %-6s %s\n", "scenario", "run", "outcome");
    std::printf("--------------------------------------------------------------------\n");

    int total = 0, validCount = 0, unparseable = 0, providerErrors = 0;
    int appliedCount = 0, repairedCount = 0, retriedCount = 0;
    std::map<juce::String, int> errorTally;
    juce::Array<juce::var> records;

    const auto& allScenarios = scenarios();
    const int scenarioCount = limit > 0 ? juce::jmin(limit, (int)allScenarios.size()) : (int)allScenarios.size();

    for (int run = 1; run <= runs; ++run) {
        for (int i = 0; i < scenarioCount; ++i) {
            const auto& scenario = allScenarios[i];
            const auto outcome = runScenario(scenario, host, model, providerKind);
            ++total;

            juce::String label;
            if (!outcome.responded) {
                ++providerErrors;
                label = "PROVIDER-ERROR " + outcome.message;
            } else if (!outcome.parsed) {
                ++unparseable;
                errorTally["<unparseable>"]++;
                label = "UNPARSEABLE";
            } else if (outcome.valid) {
                ++validCount;
                label = "ok";
            } else {
                const auto name = synth::patchValidationErrorName(outcome.error);
                errorTally[name]++;
                label = "REJECTED " + name + " — " + outcome.message;
            }

            if (outcome.responded) {
                if (outcome.appliedAfterRetry)
                    ++appliedCount;
                if (outcome.retriesUsed > 0)
                    ++retriedCount;
                if (outcome.modeRepaired)
                    ++repairedCount;

                if (!outcome.valid)
                    label += outcome.appliedAfterRetry
                                 ? ("  -> RECOVERED after " + juce::String(outcome.retriesUsed) + " retry(s)")
                                 : ("  -> still failed: " + outcome.finalError);
                else if (outcome.modeRepaired)
                    label += "  (mode repaired)";
            }

            std::printf("%-20s %-6d %s\n", scenario.name, run, label.toRawUTF8());
            std::fflush(stdout);

            juce::DynamicObject::Ptr rec = new juce::DynamicObject();
            rec->setProperty("scenario", scenario.name);
            rec->setProperty("run", run);
            rec->setProperty("merge", scenario.mergeMode);
            rec->setProperty("responded", outcome.responded);
            rec->setProperty("parsed", outcome.parsed);
            rec->setProperty("valid", outcome.valid);
            rec->setProperty("error", synth::patchValidationErrorName(outcome.error));
            rec->setProperty("message", outcome.message);
            rec->setProperty("appliedAfterRetry", outcome.appliedAfterRetry);
            rec->setProperty("retriesUsed", outcome.retriesUsed);
            rec->setProperty("modeRepaired", outcome.modeRepaired);
            rec->setProperty("finalError", outcome.finalError);
            // Raw model text, for offline fixture replay — see Tools/AIPatchHarness/README.md
            // "--json record format". Empty when `responded` is false (nothing to replay).
            rec->setProperty("rawResponse", outcome.rawResponse);
            juce::Array<juce::var> retryResponsesJson;
            for (const auto& r : outcome.retryResponses)
                retryResponsesJson.add(r);
            rec->setProperty("retryResponses", retryResponsesJson);
            records.add(juce::var(rec.get()));
        }
    }

    // Attempts that actually produced a model response — the denominator the acceptance
    // rate is quoted against, so an unreachable Ollama can't flatter the number.
    const int attempted = total - providerErrors;
    const double rate = attempted > 0 ? (100.0 * validCount / attempted) : 0.0;

    const double appliedRate = attempted > 0 ? (100.0 * appliedCount / attempted) : 0.0;

    std::printf("\n=================== SUMMARY ===================\n");
    std::printf("attempts (model responded): %d / %d\n", attempted, total);
    std::printf("valid on first answer:      %d  (%.1f%%)\n", validCount, rate);
    std::printf("rejected on first answer:   %d  (%.1f%%)\n", attempted - validCount,
                attempted > 0 ? 100.0 - rate : 0.0);
    std::printf("APPLIED after retry/repair: %d  (%.1f%%)   <- what the user gets\n", appliedCount, appliedRate);
    std::printf("  needed a retry:           %d\n", retriedCount);
    std::printf("  mode repaired:            %d\n", repairedCount);
    if (providerErrors > 0)
        std::printf("provider errors (excluded): %d\n", providerErrors);
    std::printf("\nrejections by PatchValidationError:\n");
    if (errorTally.empty()) {
        std::printf("  (none)\n");
    } else {
        for (const auto& [name, count] : errorTally)
            std::printf("  %-28s %3d  (%.1f%% of attempts)\n", name.toRawUTF8(), count,
                        attempted > 0 ? 100.0 * count / attempted : 0.0);
    }
    std::printf("===============================================\n");

    if (jsonOut.isNotEmpty()) {
        juce::DynamicObject::Ptr root = new juce::DynamicObject();
        root->setProperty("model", model);
        // ISO 8601 UTC, so a committed corpus file self-documents when it was recorded without
        // relying on filesystem/VCS timestamps that don't survive a checkout.
        root->setProperty("capturedAt", juce::Time::getCurrentTime().toISO8601(true));
        root->setProperty("runs", runs);
        root->setProperty("attempted", attempted);
        root->setProperty("valid", validCount);
        root->setProperty("unparseable", unparseable);
        root->setProperty("providerErrors", providerErrors);
        root->setProperty("records", records);
        // Pretty-printed (not allOnOneLine): committed fixtures under Tests/fixtures/ai-patches/
        // are re-recorded and diffed by a human deciding regression-vs-intended-change — a 50+ KB
        // single line would make that diff unreadable.
        juce::File(jsonOut).replaceWithText(juce::JSON::toString(juce::var(root.get()), false));
        std::printf("wrote %s\n", jsonOut.toRawUTF8());
    }

    return 0;
}
