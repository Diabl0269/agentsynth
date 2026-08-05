/*
    AIEvalHarness — offline quality evaluation of AI-generated patches against golden prompts.

    AIPatchHarness (Tools/AIPatchHarness) answers "did the model's answer pass validatePatch and
    survive the retry path" — a syntactic question. This tool answers a different one: of the
    patches that DID make it into the graph, are they actually usable synthesizer patches? A
    patch can be perfectly schema-valid and still be silent (nothing wired to Audio Output) or
    dangling (an oscillator nobody connected to anything).

    It replays a fixed set of 30-50 realistic prompts through the exact production path an
    Apply/Merge click takes, then runs synth::evaluatePatch() (Source/AI/PatchEval.h) against the
    resulting graph for every prompt that actually applied:

        AIIntegrationService::sendMessage(useStructuredOutput=true)
          -> applyPatchWithRetry(...)             # what the user actually ends up with
          -> evaluatePatch(graph)                 # has an output? connects? params in range?

    This is what lets a cheaper/local candidate model be compared against the current default
    without guessing — see Tools/AIEvalHarness/README.md for how to use the numbers to decide.
    Because it needs a live model it is excluded from the test target and from CI; build it
    explicitly with -DENABLE_AI_HARNESS=ON.

        ./build/Tools/AIEvalHarness/AIEvalHarness [--provider ollama|remote] [--model M]
                                                   [--runs N] [--host URL] [--json OUT]

    --provider ollama (default) talks to Ollama's own /api/chat directly, same as before.
    --provider remote talks to a local synth-platform inference service instead
    (RemoteProvider, Source/AI/RemoteProvider.h) — the service picks its own model server-side
    per its own INFERENCE_PROVIDER/INFERENCE_MODEL_ID config, so --model is a label for the
    report only in this mode, not something sent over the wire. This is what lets a model be
    scored through the exact stack a user hits (client -> service -> Ollama/Groq) instead of an
    approximation of it.

    Exit code is 0 whenever the run completed, whatever the pass rate — the scorecard is the
    output, not a pass/fail verdict.
*/

#include "AI/AIIntegrationService.h"
#include "AI/AIProvider.h"
#include "AI/AIStateMapper.h"
#include "AI/OllamaProvider.h"
#include "AI/PatchEval.h"
#include "AI/RemoteProvider.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include <cstdio>
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

// 40 prompts a real user would plausibly type, spanning both modes and every module family
// (oscillators, filters, envelopes, every FX module, poly/sequencing, external MIDI, and
// parameter-only tweaks). Kept as one flat golden set rather than per-prompt bespoke
// assertions: every scenario is scored on the same three structural checks in PatchEval.h, so
// what varies across scenarios is coverage of the module surface, not the pass criteria.
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
        {"mastering-chain", "Build a patch with compression and limiting on the output", false, nullptr},
        {"flanger-sweep", "Create a swirling flanger effect on a saw wave", false, nullptr},
        {"poly-pad", "Build a polyphonic pad using Poly MIDI and a voice mixer", false, nullptr},
        {"sequenced-arp", "Make a sequenced arpeggio with a poly sequencer", false, nullptr},
        {"sidechain-pump", "Create a pumping bass sound with heavy compression", false, nullptr},
        {"ext-midi-lead", "Build a lead patch driven by external MIDI input", false, nullptr},
        {"double-osc-unison", "Create a thick unison lead with two oscillators", false, nullptr},
        {"ambient-wash", "Design an ambient wash with chorus, phaser, and reverb", false, nullptr},
        {"percussive-pluck", "Make a percussive pluck with a fast filter envelope", false, nullptr},
        {"vintage-organ", "Build a vintage organ patch with a slow chorus and a flanger", false, nullptr},

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
        {"slower-attack", "Make the attack slower", true, kBasicPatch},
        {"more-resonance", "Increase the filter resonance", true, kBasicPatch},
        {"add-compressor", "Add a compressor for punch", true, kBasicPatch},
        {"add-limiter", "Add a limiter at the very end", true, kBasicPatch},
        {"add-flanger", "Add a flanger for movement", true, kBasicPatch},
        {"add-lfo-pitch", "Add an LFO for subtle pitch vibrato", true, kBasicPatch},
        {"add-filter-env", "Add a filter envelope controlling the cutoff", true, kBasicPatch},
        {"quieter", "Make it quieter", true, kBasicPatch},
        {"remove-vca", "Remove the VCA", true, kBasicPatch},
        {"attenuverted-lfo", "Add an LFO modulating the cutoff at reduced depth", true, kBasicPatch},
    };
    return s;
}

juce::String argValue(const juce::StringArray& args, const juce::String& flag, const juce::String& fallback) {
    int i = args.indexOf(flag);
    if (i >= 0 && i + 1 < args.size())
        return args[i + 1];
    return fallback;
}

enum class ProviderKind { ollama, remote };

// Builds the exact AIProvider a live Apply/Merge click would use, so this harness measures the
// stack a user actually hits rather than an approximation of it. Both branches setTestMode(true)
// on the concrete type (not virtual on AIProvider) before it's upcast, so the callback below
// fires synchronously with no message loop running here.
std::unique_ptr<synth::AIProvider> makeProvider(ProviderKind kind, const juce::String& host,
                                                const juce::String& model) {
    if (kind == ProviderKind::remote) {
        auto provider = std::make_unique<synth::RemoteProvider>(host);
        provider->setTestMode(true);
        provider->setModel(model);
        return provider;
    }

    auto provider = std::make_unique<synth::OllamaProvider>(host);
    provider->setTestMode(true);
    provider->setModel(model);
    return provider;
}

// Result of replaying one scenario once.
struct Outcome {
    bool responded = false;         // the provider returned a successful completion at all
    bool appliedAfterRetry = false; // what the user actually ends up with
    juce::String applyError;

    // Only meaningful when appliedAfterRetry is true — there is no graph worth scoring otherwise.
    bool evaluated = false;
    synth::PatchEvalResult eval;
};

Outcome runScenario(const Scenario& scenario, ProviderKind providerKind, const juce::String& host,
                    const juce::String& model) {
    Outcome outcome;

    juce::AudioProcessorGraph graph;
    // "Audio Output" mirrors the graph's own bus channel count (AudioGraphIOProcessor); without
    // configuring it the way AudioEngine does before device audio starts, it reports zero
    // channels and every connection into it silently no-ops, making every scenario look
    // unconnected regardless of what the model actually produced.
    graph.setPlayConfigDetails(0, 2, 44100.0, 512);
    graph.prepareToPlay(44100.0, 512);

    if (scenario.seedPatch != nullptr) {
        juce::var seed = juce::JSON::parse(juce::String(scenario.seedPatch));
        // trusted=true: this is our own fixture, not model output.
        if (!synth::AIStateMapper::applyJSONToGraph(seed, graph, /*clearExisting=*/true, /*trusted=*/true)) {
            outcome.applyError = "harness error: seed patch failed to apply";
            return outcome;
        }
    }

    synth::AIIntegrationService service(graph);
    service.setProvider(makeProvider(providerKind, host, model));

    juce::WaitableEvent done;
    juce::String responseText;
    bool success = false;

    service.sendMessage(
        scenario.prompt,
        [&](const synth::AIProvider::AIResponse& response) {
            responseText = response.success ? response.content : response.error.message;
            success = response.success;
            done.signal();
        },
        /*useStructuredOutput=*/true);

    // Must exceed both OllamaProvider's kChatRequestTimeoutMs and RemoteProvider's
    // kRequestTimeoutMs (currently 240s each) with margin, or this outer wait fires first and
    // reports a vague "timed out" instead of the provider's own error message.
    if (!done.wait(270000)) {
        outcome.applyError = "timed out waiting for model";
        return outcome;
    }
    if (!success) {
        outcome.applyError = "provider error: " + responseText;
        return outcome;
    }
    outcome.responded = true;

    juce::WaitableEvent applied;
    service.applyPatchWithRetry(responseText, scenario.mergeMode, [&](bool ok, const juce::String& error) {
        outcome.appliedAfterRetry = ok;
        outcome.applyError = error;
        applied.signal();
    });

    if (!applied.wait(600000)) {
        outcome.applyError = "timed out during retry";
        return outcome;
    }

    if (outcome.appliedAfterRetry) {
        outcome.eval = synth::evaluatePatch(graph);
        outcome.evaluated = true;
    }

    return outcome;
}

} // namespace

int main(int argc, char* argv[]) {
    juce::ScopedJuceInitialiser_GUI juceInit;

    juce::StringArray args;
    for (int i = 1; i < argc; ++i)
        args.add(juce::String(argv[i]));

    const juce::String providerFlag = argValue(args, "--provider", "ollama");
    if (providerFlag != "ollama" && providerFlag != "remote") {
        std::fprintf(stderr, "unknown --provider \"%s\" (expected \"ollama\" or \"remote\")\n",
                     providerFlag.toRawUTF8());
        return 1;
    }
    const ProviderKind providerKind = providerFlag == "remote" ? ProviderKind::remote : ProviderKind::ollama;

    const juce::String defaultHost =
        providerKind == ProviderKind::remote ? "http://localhost:8787" : "http://localhost:11434";
    const juce::String host = argValue(args, "--host", defaultHost);
    const juce::String model = argValue(args, "--model", "gemma4:12b-it-qat");
    const int runs = juce::jmax(1, argValue(args, "--runs", "1").getIntValue());
    const juce::String jsonOut = argValue(args, "--json", "");

    std::printf("AIEvalHarness  provider=%s  host=%s  model=%s  runs=%d  scenarios=%d\n", providerFlag.toRawUTF8(),
                host.toRawUTF8(), model.toRawUTF8(), runs, (int)scenarios().size());
    std::printf("%-20s %-6s %s\n", "scenario", "run", "outcome");
    std::printf("--------------------------------------------------------------------\n");

    int total = 0, providerErrors = 0, notApplied = 0, applied = 0;
    int passOutput = 0, passConnects = 0, passParams = 0, passAll = 0;
    juce::Array<juce::var> records;

    for (int run = 1; run <= runs; ++run) {
        for (const auto& scenario : scenarios()) {
            const auto outcome = runScenario(scenario, providerKind, host, model);
            ++total;

            juce::String label;
            if (!outcome.responded) {
                ++providerErrors;
                label = "PROVIDER-ERROR " + outcome.applyError;
            } else if (!outcome.appliedAfterRetry) {
                ++notApplied;
                label = "NOT-APPLIED " + outcome.applyError;
            } else {
                ++applied;
                const auto& e = outcome.eval;
                if (e.hasAudioOutput)
                    ++passOutput;
                if (e.sourceReachesOutput)
                    ++passConnects;
                if (e.allParamsInRange)
                    ++passParams;
                if (e.passed())
                    ++passAll;
                label = e.passed() ? "PASS" : ("FAIL " + e.detail);
            }

            std::printf("%-20s %-6d %s\n", scenario.name, run, label.toRawUTF8());
            std::fflush(stdout);

            juce::DynamicObject::Ptr rec = new juce::DynamicObject();
            rec->setProperty("scenario", scenario.name);
            rec->setProperty("run", run);
            rec->setProperty("merge", scenario.mergeMode);
            rec->setProperty("responded", outcome.responded);
            rec->setProperty("appliedAfterRetry", outcome.appliedAfterRetry);
            rec->setProperty("applyError", outcome.applyError);
            if (outcome.evaluated) {
                rec->setProperty("hasAudioOutput", outcome.eval.hasAudioOutput);
                rec->setProperty("sourceReachesOutput", outcome.eval.sourceReachesOutput);
                rec->setProperty("allParamsInRange", outcome.eval.allParamsInRange);
                rec->setProperty("detail", outcome.eval.detail);
            }
            records.add(juce::var(rec.get()));
        }
    }

    // Quality is only meaningful for patches that actually applied — a provider error or a
    // rejected patch can't be scored against PatchEval, so `applied` (not `total`) is the
    // denominator for every rate below.
    const double rate = applied > 0 ? (100.0 * passAll / applied) : 0.0;

    std::printf("\n=================== SUMMARY ===================\n");
    std::printf("attempts (model responded): %d / %d\n", total - providerErrors, total);
    std::printf("applied (what the user gets): %d\n", applied);
    if (providerErrors > 0)
        std::printf("provider errors (excluded): %d\n", providerErrors);
    if (notApplied > 0)
        std::printf("never applied (excluded from quality rate): %d\n", notApplied);
    std::printf("\nof the %d applied patches:\n", applied);
    std::printf("  has Audio Output:            %3d  (%.1f%%)\n", passOutput,
                applied > 0 ? 100.0 * passOutput / applied : 0.0);
    std::printf("  source reaches Audio Output: %3d  (%.1f%%)\n", passConnects,
                applied > 0 ? 100.0 * passConnects / applied : 0.0);
    std::printf("  all params in range:         %3d  (%.1f%%)\n", passParams,
                applied > 0 ? 100.0 * passParams / applied : 0.0);
    std::printf("  ALL THREE (overall pass):    %3d  (%.1f%%)\n", passAll, rate);
    std::printf("===============================================\n");

    if (jsonOut.isNotEmpty()) {
        juce::DynamicObject::Ptr root = new juce::DynamicObject();
        root->setProperty("provider", providerFlag);
        root->setProperty("model", model);
        root->setProperty("runs", runs);
        root->setProperty("applied", applied);
        root->setProperty("passAll", passAll);
        root->setProperty("providerErrors", providerErrors);
        root->setProperty("notApplied", notApplied);
        root->setProperty("records", records);
        juce::File(jsonOut).replaceWithText(juce::JSON::toString(juce::var(root.get()), true));
        std::printf("wrote %s\n", jsonOut.toRawUTF8());
    }

    return 0;
}
