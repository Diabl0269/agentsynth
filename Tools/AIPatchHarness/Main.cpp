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

        ./build/Tools/AIPatchHarness/AIPatchHarness [--model M] [--runs N] [--host URL] [--json OUT]

    Exit code is 0 whenever the run completed, whatever the pass rate — the histogram is
    the output, not a pass/fail verdict.
*/

#include "AI/AIIntegrationService.h"
#include "AI/AIStateMapper.h"
#include "AI/OllamaProvider.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include <cstdio>
#include <map>
#include <vector>

namespace {

// One replayed scenario: what the user types, and what patch (if any) is already on the
// canvas when they type it. `seedPatch` is applied trusted, exactly as loading a preset
// would, so merge-mode prompts see realistic pre-existing node ids.
struct Scenario {
    const char* name;
    const char* prompt;
    bool mergeMode;
    const char* seedPatch; // nullptr for a from-scratch (replace-mode) request
};

// A small but real starting patch: MIDI -> Oscillator -> Filter -> VCA -> Output.
// Node ids here are what a merge-mode prompt must reference correctly.
constexpr const char* kBasicPatch = R"({
  "nodes": [
    {"id": 1, "type": "Poly MIDI"},
    {"id": 2, "type": "Oscillator", "params": {"waveform": "Saw"}},
    {"id": 3, "type": "Filter", "params": {"cutoff": 2000.0}},
    {"id": 4, "type": "VCA"},
    {"id": 5, "type": "Audio Output"}
  ],
  "connections": [
    {"src": 1, "srcPort": -1, "dst": 2, "dstPort": -1},
    {"src": 2, "srcPort": 0, "dst": 3, "dstPort": 0},
    {"src": 3, "srcPort": 0, "dst": 4, "dstPort": 0},
    {"src": 4, "srcPort": 0, "dst": 5, "dstPort": 0}
  ]
})";

// Prompts a real user would plausibly type. Deliberately spans both modes, because
// merge-mode failures (stale/hallucinated ids) and replace-mode failures (invented
// module types, bad choice strings) are different populations.
const std::vector<Scenario>& scenarios() {
    static const std::vector<Scenario> s = {
        // --- from scratch (replace mode) ---
        {"basic-bass", "Create a fat analog bass patch", false, nullptr},
        {"pluck", "Make a short plucky lead sound", false, nullptr},
        {"pad", "Build a warm evolving pad with a slow filter sweep", false, nullptr},
        {"acid", "Create an acid bassline with a resonant filter and a sequencer", false, nullptr},
        {"bell", "Design a bell-like FM tone", false, nullptr},
        {"drone", "Make a dark drone with reverb", false, nullptr},
        {"stab", "Create a bright synth stab with a fast envelope", false, nullptr},
        {"organ", "Build a simple organ patch with multiple oscillators", false, nullptr},
        {"noise-sweep", "Make a white noise sweep riser", false, nullptr},
        {"poly-keys", "Create a polyphonic keys patch with chorus", false, nullptr},

        // --- modify an existing patch (merge mode) ---
        {"add-reverb", "Add reverb to the end of the chain", true, kBasicPatch},
        {"add-lfo-cutoff", "Add an LFO modulating the filter cutoff", true, kBasicPatch},
        {"brighter", "Make it brighter", true, kBasicPatch},
        {"add-delay", "Add a delay after the filter", true, kBasicPatch},
        {"change-wave", "Change the oscillator to a square wave", true, kBasicPatch},
        {"add-env", "Add an amp envelope controlling the VCA", true, kBasicPatch},
        {"remove-filter", "Remove the filter from the patch", true, kBasicPatch},
        {"add-distortion", "Add some distortion for grit", true, kBasicPatch},
        {"detune", "Detune the oscillator slightly for width", true, kBasicPatch},
        {"add-chorus-delay", "Add chorus and then a delay at the end", true, kBasicPatch},
    };
    return s;
}

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
};

Outcome runScenario(const Scenario& scenario, const juce::String& host, const juce::String& model) {
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
    auto provider = std::make_unique<synth::OllamaProvider>(host);
    provider->setTestMode(true); // deliver callbacks synchronously; no message loop here
    provider->setModel(model);
    service.setProvider(std::move(provider));

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

    return outcome;
}

} // namespace

int main(int argc, char* argv[]) {
    juce::ScopedJuceInitialiser_GUI juceInit;

    juce::StringArray args;
    for (int i = 1; i < argc; ++i)
        args.add(juce::String(argv[i]));

    const juce::String host = argValue(args, "--host", "http://localhost:11434");
    const juce::String model = argValue(args, "--model", "gemma4:12b-it-qat");
    const int runs = juce::jmax(1, argValue(args, "--runs", "1").getIntValue());
    const juce::String jsonOut = argValue(args, "--json", "");

    std::printf("AIPatchHarness  host=%s  model=%s  runs=%d  scenarios=%d\n", host.toRawUTF8(), model.toRawUTF8(), runs,
                (int)scenarios().size());
    std::printf("%-20s %-6s %s\n", "scenario", "run", "outcome");
    std::printf("--------------------------------------------------------------------\n");

    int total = 0, validCount = 0, unparseable = 0, providerErrors = 0;
    int appliedCount = 0, repairedCount = 0, retriedCount = 0;
    std::map<juce::String, int> errorTally;
    juce::Array<juce::var> records;

    for (int run = 1; run <= runs; ++run) {
        for (const auto& scenario : scenarios()) {
            const auto outcome = runScenario(scenario, host, model);
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
        root->setProperty("runs", runs);
        root->setProperty("attempted", attempted);
        root->setProperty("valid", validCount);
        root->setProperty("unparseable", unparseable);
        root->setProperty("providerErrors", providerErrors);
        root->setProperty("records", records);
        juce::File(jsonOut).replaceWithText(juce::JSON::toString(juce::var(root.get()), true));
        std::printf("wrote %s\n", jsonOut.toRawUTF8());
    }

    return 0;
}
