/*
    ProjectMode.h -- AIEvalHarness `--mode project`: sound-design words asked of the edit-plan path.

    The other modes ask whether a patch applied and is wired to the output. This one asks whether the
    response does what the WORDS asked for, on the exact path a chat message takes:

        AIIntegrationService::sendProjectMessage    # hosted project.generate, or the local model
          -> previewProjectEdit(plan)               # valid? (the plan validator, stand-in track builds)
          -> synth::soundshape::check*(plan)        # right shape? (Source/AI/SoundShapeChecks.h)

    "valid" and "shape" are scored separately: a plan can preview fine and still leave the envelope at
    its drone-friendly default, which is the failure this mode exists to count.

    `--mode track` runs the same path on whole-track requests ("an upbeat dark techno track with
    sparkling sounds"). Its shape check only asks whether a track came back at all (several
    instrument tracks, most with notes); whether it sounds good is judged by listening, so every
    raw answer goes to --json.
*/
#pragma once

#include "AI/AIIntegrationService/AIIntegrationService.h"
#include "AI/AIProvider.h"
#include "AI/AIStateMapper/AIStateMapper.h"
#include "AI/PatchEval.h"
#include "AI/SoundShapeChecks.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>

#include <cstdio>
#include <functional>
#include <memory>
#include <vector>

namespace project_mode {

enum class Shape { pluck, filterEnvelope, acid, fullTrack };

struct Scenario {
    const char* name;
    const char* prompt;
    Shape shape;
    const char* seedPatch; // nullptr = an empty project
};

// An existing patch with an envelope already modulating the VCA: Poly MIDI -> Oscillator -> Filter ->
// VCA -> Audio Output, with an ADSR on the VCA's CV. The Filter has no envelope on its cutoff.
constexpr const char* kEnvelopedPatch = R"({
  "nodes": [
    {"id": 1, "type": "Poly MIDI"},
    {"id": 2, "type": "Oscillator", "params": {"waveform": "Saw"}},
    {"id": 3, "type": "Filter", "params": {"cutoff": 2000.0}},
    {"id": 4, "type": "VCA"},
    {"id": 5, "type": "ADSR", "params": {"attack": 0.01, "decay": 0.3, "sustain": 0.7, "release": 0.3}},
    {"id": 6, "type": "Audio Output"}
  ],
  "connections": [
    {"src": 1, "srcPort": -1, "dst": 2, "dstPort": -1},
    {"src": 2, "srcPort": 0, "dst": 3, "dstPort": 0},
    {"src": 3, "srcPort": 0, "dst": 4, "dstPort": 0},
    {"src": 4, "srcPort": 0, "dst": 6, "dstPort": 0}
  ],
  "modulations": [
    {"source": 5, "dest": 4, "destPort": 1, "amount": 1.0}
  ]
})";

inline const std::vector<Scenario>& scenarios() {
    static const std::vector<Scenario> s = {
        {"plucky", "make a plucky lead track", Shape::pluck, nullptr},
        {"filter-env-new", "add a bass track with a filter envelope", Shape::filterEnvelope, nullptr},
        {"filter-env-existing", "add a filter envelope", Shape::filterEnvelope, kEnvelopedPatch},
        // A new track beside one that already has an ADSR: the new Filter must use the NEW track's envelope.
        {"filter-env-beside", "add a bass track with a filter envelope", Shape::filterEnvelope, kEnvelopedPatch},
        {"acid", "make an acid bassline track", Shape::acid, nullptr},
    };
    return s;
}

// Whole-track requests, from an empty project. Genre, tempo and sound words vary so a model can't pass
// on one template.
inline const std::vector<Scenario>& fullTrackScenarios() {
    static const std::vector<Scenario> s = {
        {"dark-techno", "make an upbeat dark techno track with sparkling sounds", Shape::fullTrack, nullptr},
        {"lofi", "make a melancholic lo-fi hip hop beat with dusty keys and a lazy bassline", Shape::fullTrack,
         nullptr},
        {"prog-house", "make a driving 128 bpm progressive house track with a big pluck lead and a rolling bassline",
         Shape::fullTrack, nullptr},
        {"ambient", "make an ambient track with slow evolving pads, a soft pulse and shimmering high bells",
         Shape::fullTrack, nullptr},
        {"dnb", "make a dark drum and bass track at 174 bpm with a reese bass and sparse atmospheric pads",
         Shape::fullTrack, nullptr},
        {"synthwave", "make a retro synthwave track with gated chords, an arpeggiated bass and a bright lead",
         Shape::fullTrack, nullptr},
        {"acid-techno", "make a minimal acid techno track with a squelchy 303-style line and tight hats",
         Shape::fullTrack, nullptr},
        {"chillwave", "make a dreamy chillwave track with washed-out chords, a simple melody and a laid-back groove",
         Shape::fullTrack, nullptr},
        {"trap", "make a hard-hitting trap beat with booming 808 bass, rolling hi-hats and a dark bell melody",
         Shape::fullTrack, nullptr},
        {"trance", "make a euphoric trance build with a supersaw lead, an offbeat bass and a rising filter sweep",
         Shape::fullTrack, nullptr},
    };
    return s;
}

// A whole track came back: at least kMinTracks new tracks, and at least two of the tracks that got clips
// have notes. The reason line carries the counts either way, so a thin answer is visible next to a pass.
inline synth::soundshape::ShapeCheck checkFullTrack(const juce::var& plan) {
    constexpr int kMinTracks = 3;
    int tracks = 0, clipOps = 0, clipOpsWithNotes = 0, notes = 0, lanes = 0;
    if (const auto* ops = plan.getProperty("timelineOps", juce::var()).getArray()) {
        for (const auto& op : *ops) {
            const auto kind = op.getProperty("op", juce::var()).toString();
            if (kind == "addInstrumentTrack" || kind == "addTrack")
                ++tracks;
            else if (kind == "writeLane")
                ++lanes;
            else if (kind == "placeClips") {
                ++clipOps;
                int opNotes = 0;
                if (const auto* clips = op.getProperty("clips", juce::var()).getArray())
                    for (const auto& clip : *clips)
                        if (const auto* clipNotes = clip.getProperty("notes", juce::var()).getArray())
                            opNotes += clipNotes->size();
                notes += opNotes;
                clipOpsWithNotes += opNotes > 0 ? 1 : 0;
            }
        }
    }
    synth::soundshape::ShapeCheck result;
    result.pass = tracks >= kMinTracks && clipOpsWithNotes >= 2;
    result.reason = juce::String(tracks) + " tracks, " + juce::String(clipOpsWithNotes) + "/" + juce::String(clipOps) +
                    " clip ops with notes, " + juce::String(notes) + " notes, " + juce::String(lanes) +
                    " automation lanes";
    return result;
}

// A host that only has to EXIST: previewProjectEdit refuses a plan that builds tracks without one, but
// builds through its own stand-ins and never calls it.
struct PreviewOnlyHost : synth::TimelineOpsHost {
    std::optional<synth::InstrumentTrackBuildResult>
    addInstrumentTrack(const juce::String&, const juce::String&, bool, const std::vector<synth::InstrumentTrackInsert>&,
                       const juce::var&, const juce::var&) override {
        return std::nullopt;
    }
    bool recordBatch(const std::function<void()>&) override { return false; }
};

// Called for each plan that previews valid (--save-projects): applies and saves it, returns the line to print
// and sets `savedPath` to the bundle written (left empty when nothing was).
using SaveHook =
    std::function<juce::String(const Scenario&, int run, const juce::String& response, juce::String& savedPath)>;

struct RunOptions {
    int outerWaitMs = 270000;     // how long the harness waits for one answer (--timeout-ms + 30 s)
    SaveHook saveHook;            // empty = do not save
    int projectPromptVersion = 0; // --project-prompt-version; 0 = the app's own pin
};

struct Outcome {
    bool responded = false;
    bool valid = false;
    juce::String error; // provider error, or why the plan was not valid
    bool shapeOk = false;
    juce::String shapeReason;
    juce::String response; // the model's raw answer, so a failure can be read back from --json
};

inline synth::soundshape::ShapeCheck scoreShape(Shape shape, const juce::var& plan, const juce::var& existingPatch) {
    switch (shape) {
    case Shape::pluck:
        return synth::soundshape::checkPluck(plan);
    case Shape::filterEnvelope:
        return synth::soundshape::checkFilterEnvelope(plan, existingPatch);
    case Shape::acid:
        return synth::soundshape::checkAcid(plan, existingPatch);
    case Shape::fullTrack:
        return checkFullTrack(plan);
    }
    return {};
}

inline Outcome runScenario(const Scenario& scenario,
                           const std::function<std::unique_ptr<synth::AIProvider>()>& makeProvider,
                           int outerWaitMs = 270000, int projectPromptVersion = 0) {
    Outcome outcome;
    juce::AudioProcessorGraph graph;
    synth::prepareGraphForPatchEval(graph);
    if (scenario.seedPatch != nullptr) {
        const juce::var seed = juce::JSON::parse(juce::String(scenario.seedPatch));
        // trusted=true: this is our own fixture, not model output.
        if (!synth::AIStateMapper::applyJSONToGraph(seed, graph, /*clearExisting=*/true, /*trusted=*/true)) {
            outcome.error = "harness error: seed patch failed to apply";
            return outcome;
        }
    }
    // The patch as the model is shown it (live uids), which the shape check resolves ids against.
    const juce::var existingPatch = synth::AIStateMapper::graphToJSON(graph);

    synth::AIIntegrationService service(graph);
    service.setProvider(makeProvider());
    synth::TimelineDoc timelineDoc;
    synth::TransportService transport;
    PreviewOnlyHost host;
    service.setTimelineContext(&timelineDoc, &transport);
    service.setTimelineToolsEnabled(true);
    service.setTimelineOpsHost(&host);
    service.setProjectPromptVersion(projectPromptVersion);

    juce::WaitableEvent done;
    juce::String responseText;
    bool success = false;
    service.sendProjectMessage(scenario.prompt, [&](const synth::AIProvider::AIResponse& response) {
        responseText = response.success ? response.content : response.error.message;
        success = response.success;
        done.signal();
    });
    // Longer than the provider's own request timeout, so its message is the one reported.
    if (!done.wait(outerWaitMs)) {
        outcome.error = "timed out waiting for model";
        return outcome;
    }
    if (!success) {
        outcome.error = "provider error: " + responseText;
        return outcome;
    }
    outcome.responded = true;
    outcome.response = responseText;

    const juce::var plan = juce::JSON::parse(synth::AIIntegrationService::extractJsonFromResponse(responseText));
    if (!plan.isObject()) {
        outcome.error = "the response is not a JSON object";
        outcome.shapeReason = outcome.error;
        return outcome;
    }
    const auto preview = service.previewProjectEdit(plan);
    outcome.valid = preview.ok;
    outcome.error = preview.ok ? juce::String() : preview.message;
    const auto shape = scoreShape(scenario.shape, plan, existingPatch);
    outcome.shapeOk = shape.pass;
    outcome.shapeReason = shape.reason;
    return outcome;
}

// Replays every scenario in `list` `runs` times and prints the scorecard under `label`; returns the process
// exit code (0 whenever the run completed, like the other modes).
inline int runAll(const std::vector<Scenario>& list, const char* label, int runs,
                  const std::function<std::unique_ptr<synth::AIProvider>()>& makeProvider, const juce::String& jsonOut,
                  const juce::DynamicObject::Ptr& header, const RunOptions& options = {}) {
    std::printf("%-22s %-5s %-6s %-12s %s\n", "scenario", "run", "valid", "shape", "reason");
    std::printf("--------------------------------------------------------------------\n");
    int total = 0, responded = 0, valid = 0, shapeOk = 0;
    juce::Array<juce::var> records;
    for (int run = 1; run <= runs; ++run) {
        for (const auto& scenario : list) {
            const auto startMs = juce::Time::currentTimeMillis();
            const auto outcome = runScenario(scenario, makeProvider, options.outerWaitMs, options.projectPromptVersion);
            const auto endMs = juce::Time::currentTimeMillis();
            ++total;
            responded += outcome.responded ? 1 : 0;
            valid += outcome.valid ? 1 : 0;
            shapeOk += outcome.shapeOk ? 1 : 0;
            const juce::String shape = !outcome.responded ? "no-response"
                                       : outcome.shapeOk  ? "shape-ok"
                                                          : "shape-wrong";
            juce::String reason = outcome.shapeReason.isNotEmpty() ? outcome.shapeReason : outcome.error;
            if (outcome.responded && !outcome.valid && outcome.error.isNotEmpty() && outcome.error != reason)
                reason += " | invalid: " + outcome.error;
            std::printf("%-22s %-5d %-6s %-12s %s\n", scenario.name, run, outcome.valid ? "valid" : "INVALID",
                        shape.toRawUTF8(), reason.toRawUTF8());
            std::fflush(stdout);

            juce::String savedPath;
            if (options.saveHook && outcome.valid) {
                const auto line = options.saveHook(scenario, run, outcome.response, savedPath);
                std::printf("  %s\n", line.toRawUTF8());
                std::fflush(stdout);
            }

            juce::DynamicObject::Ptr rec = new juce::DynamicObject();
            rec->setProperty("scenario", scenario.name);
            rec->setProperty("run", run);
            rec->setProperty("prompt", scenario.prompt);
            rec->setProperty("startMs", startMs);
            rec->setProperty("endMs", endMs);
            rec->setProperty("responded", outcome.responded);
            rec->setProperty("valid", outcome.valid);
            rec->setProperty("shapeOk", outcome.shapeOk);
            rec->setProperty("shapeReason", outcome.shapeReason);
            rec->setProperty("error", outcome.error);
            rec->setProperty("response", outcome.response);
            if (savedPath.isNotEmpty())
                rec->setProperty("savedProject", savedPath);
            records.add(juce::var(rec.get()));
        }
    }
    std::printf("\n=================== SUMMARY (%s) ===================\n", label);
    std::printf("attempts (model responded): %d / %d\n", responded, total);
    std::printf("valid (plan previews ok):   %3d  (%.1f%% of responses)\n", valid,
                responded > 0 ? 100.0 * valid / responded : 0.0);
    std::printf("shape-ok:                   %3d  (%.1f%% of responses)\n", shapeOk,
                responded > 0 ? 100.0 * shapeOk / responded : 0.0);
    std::printf("shape-wrong:                %3d\n", responded - shapeOk);
    std::printf("=========================================================\n");

    if (jsonOut.isNotEmpty()) {
        header->setProperty("attempts", total);
        header->setProperty("responded", responded);
        header->setProperty("valid", valid);
        header->setProperty("shapeOk", shapeOk);
        header->setProperty("records", records);
        juce::File(jsonOut).replaceWithText(juce::JSON::toString(juce::var(header.get()), true));
        std::printf("wrote %s\n", jsonOut.toRawUTF8());
    }
    return 0;
}

} // namespace project_mode
