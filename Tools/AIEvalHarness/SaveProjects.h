/*
    SaveProjects.h -- AIEvalHarness `--save-projects`, `--replay` and `--check-project`: turn a plan into
    a project you can open in the app and listen to.

    A plan is applied the way the chat's Apply button applies it: a real MainComponent (built headless,
    the way the app's own tests build one) is loaded with the scenario's starting patch, then
    AIIntegrationService::applyProjectEdit runs on that component's own service with its own
    MainComponentTimelineOpsHost, so every track is built by MainComponent::buildInstrumentTrackBody
    (Track In node, instrument, envelope, inserts, wiring, channel, binding). The result is written by
    MainComponent::saveProjectForTest, the same code the Save dialog runs. No part of the apply is
    re-implemented here.

    Every saved bundle is read back with ProjectBundle::load and compared with the plan's timelineOps
    (tracks, clips, notes) and checked for a Track In binding on every track.

    The MainComponent is pointed at a throw-away settings folder, so a run never reads or writes the
    user's real app settings.
*/
#pragma once

#include "AI/AIIntegrationService/AIIntegrationService.h"
#include "AI/AIProvider.h"
#include "AI/AIStateMapper/AIStateMapper.h"
#include "AudioEngine/AudioEngine.h"
#include "Auth/KeychainTokenStore.h"
#include "MacroSet.h"
#include "MainComponent/MainComponent.h"
#include "MidiRemote/RemoteModel.h"
#include "Modules/ModuleBase.h"
#include "PatchDocument.h"
#include "ProjectBundle.h"
#include "ProjectMode.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "UserSettings.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include <cstdio>
#include <functional>
#include <memory>
#include <optional>

namespace save_projects {

// What a plan asks the timeline for, or what a bundle holds.
struct Counts {
    int tracks = 0;
    int clips = 0;
    int notes = 0;
    int lanes = 0;
    int boundTracks = 0; // bundle only: tracks whose binding resolves to a Track In node
    juce::String text() const {
        return juce::String(tracks) + " tracks, " + juce::String(clips) + " clips, " + juce::String(notes) +
               " notes, " + juce::String(lanes) + " lanes";
    }
};

// "claude-haiku-5-5", "gpt-oss:20b" -> a file-name-safe label.
inline juce::String sanitizeLabel(const juce::String& label) {
    juce::String out;
    for (auto c : label.trim())
        out += (juce::CharacterFunctions::isLetterOrDigit(c) || c == '.' || c == '_' || c == '-')
                   ? juce::String::charToString(c)
                   : juce::String("-");
    return out.isEmpty() ? juce::String("model") : out;
}

inline juce::File bundleFile(const juce::File& dir, const juce::String& modelLabel, const juce::String& scenario,
                             int run) {
    return dir.getChildFile(sanitizeLabel(modelLabel) + "-" + sanitizeLabel(scenario) + "-run" + juce::String(run) +
                            synth::ProjectBundle::kBundleExtension);
}

// The tracks, clips, notes and automation lanes a plan's timelineOps create.
inline Counts planCounts(const juce::var& plan) {
    Counts c;
    if (const auto* ops = plan.getProperty("timelineOps", juce::var()).getArray()) {
        for (const auto& op : *ops) {
            const auto kind = op.getProperty("op", juce::var()).toString();
            if (kind == "addInstrumentTrack" || kind == "addTrack")
                ++c.tracks;
            else if (kind == "writeLane")
                ++c.lanes;
            else if (kind == "placeClips")
                if (const auto* clips = op.getProperty("clips", juce::var()).getArray())
                    for (const auto& clip : *clips) {
                        ++c.clips;
                        if (const auto* notes = clip.getProperty("notes", juce::var()).getArray())
                            c.notes += notes->size();
                    }
        }
    }
    return c;
}

// Loads a bundle with ProjectBundle::load into scratch objects and counts what is in it. A track counts as
// bound when its binding names a live Track In node.
inline std::optional<Counts> readBundle(const juce::File& bundle, juce::String& error) {
    juce::AudioProcessorGraph graph;
    synth::TimelineDoc timeline;
    synth::PatchDocument patchDocument;
    synth::MacroSet macros;
    synth::MidiRemoteProjectDoc midiRemote;
    const auto loaded = synth::ProjectBundle::load(bundle, graph, timeline, patchDocument, macros, midiRemote);
    if (!loaded.ok) {
        error = "load failed: " + loaded.message;
        return std::nullopt;
    }
    Counts c;
    for (const auto& track : timeline.getTracks()) {
        ++c.tracks;
        c.lanes += (int)track.lanes.size();
        for (const auto& clip : track.clips) {
            ++c.clips;
            c.notes += (int)clip.notes.size();
        }
        if (track.bindingUuid.isNotEmpty())
            for (auto* node : graph.getNodes())
                if (node->properties["uuid"].toString() == track.bindingUuid &&
                    synth::AIStateMapper::getFactoryTypeName(node->getProcessor()) == "Track In") {
                    ++c.boundTracks;
                    break;
                }
    }
    return c;
}

// `--check-project <bundle>`: print what the bundle holds. Exit code 1 when it does not load or a track is
// not bound to a Track In.
inline int checkProject(const juce::File& bundle) {
    juce::String error;
    const auto counts = readBundle(bundle, error);
    if (!counts) {
        std::printf("%s: %s\n", bundle.getFullPathName().toRawUTF8(), error.toRawUTF8());
        return 1;
    }
    std::printf("%s: %s, %d/%d tracks bound to a Track In\n", bundle.getFileName().toRawUTF8(),
                counts->text().toRawUTF8(), counts->boundTracks, counts->tracks);
    return counts->boundTracks == counts->tracks ? 0 : 1;
}

// Never answers: applying a plan needs a provider object to exist, not to be asked anything.
class NullProvider : public synth::AIProvider {
public:
    juce::String getProviderName() const override { return "none"; }
    void fetchAvailableModels(std::function<void(const juce::StringArray&, bool)> callback) override {
        callback({}, true);
    }
    RequestId sendPrompt(const std::vector<Message>&, CompletionCallback, const juce::var&,
                         std::function<void(const juce::String&)> = {}) override {
        return {};
    }
    void cancel(RequestId) override {}
    void setModel(const juce::String& name) override { model = name; }
    juce::String getCurrentModel() const override { return model; }
    void setRequestTimeoutMs(int ms) override { timeoutMs = ms; }
    int getRequestTimeoutMs() const override { return timeoutMs; }

private:
    juce::String model;
    int timeoutMs = 240000;
};

// Process-wide set-up for building MainComponents outside the app: its own settings folder (the real
// one is never touched), an in-memory token store (no Keychain prompt) and a private MIDI Remote
// profile folder. Create one before the first save; it removes the folder when destroyed.
class Environment {
public:
    Environment() {
        dir_ = juce::File::getSpecialLocation(juce::File::tempDirectory)
                   .getChildFile("aieval-save-projects-" + juce::Uuid().toDashedString());
        dir_.createDirectory();
        synth::setSettingsDirOverrideForTests(dir_.getFullPathName());
        MainComponent::setControllerProfileTestDirectory(dir_.getChildFile("controller-profiles"));
        synth::KeychainTokenStore::useInMemoryStoreForProcess(true);
    }
    ~Environment() {
        synth::setSettingsDirOverrideForTests({});
        dir_.deleteRecursively();
    }
    Environment(const Environment&) = delete;
    Environment& operator=(const Environment&) = delete;
    const juce::File& scratchDir() const { return dir_; }

private:
    juce::File dir_;
};

// What "an empty project" is when the model's ids were chosen against a bare graph; see applyAndSave.
constexpr const char* kEmptyProject = R"({"nodes": [{"id": 900000, "type": "Audio Output"}], "connections": []})";

struct SaveOutcome {
    bool saved = false;
    juce::File file;
    juce::String message; // why not, or the verification line
};

// Applies the plan in `response` on top of `seedPatch` (nullptr = empty project) and saves it to `dest`.
inline SaveOutcome applyAndSave(const Environment& env, const char* seedPatch, const juce::String& response,
                                const juce::File& dest) {
    SaveOutcome out;
    out.file = dest;
    const juce::var plan = juce::JSON::parse(synth::AIIntegrationService::extractJsonFromResponse(response));
    if (!plan.isObject()) {
        out.message = "invalid plan: the response is not a JSON object";
        return out;
    }

    MainComponent mc(std::make_unique<NullProvider>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();

    // A new project already holds an Audio Output with a small node id, while the model was shown an empty graph
    // and picks small ids of its own ("instrumentId": 1). Reloading the output under a large id keeps the plan's
    // ids free, so the plan applies exactly as it validated. A seeded scenario already carries its own ids.
    const char* start = seedPatch != nullptr ? seedPatch : kEmptyProject;
    const auto seedFile = env.scratchDir().getChildFile("seed.json");
    seedFile.replaceWithText(juce::String(start));
    if (!mc.openPatchForTest(seedFile, /*append=*/false)) {
        out.message = "harness error: the starting patch did not load";
        return out;
    }

    const auto applied = mc.getAiServiceForTest().applyProjectEdit(plan);
    if (!applied.ok) {
        out.message = "invalid plan: " + applied.message;
        return out;
    }

    if (dest.exists())
        dest.deleteRecursively();
    dest.getParentDirectory().createDirectory();
    if (!mc.saveProjectForTest(dest) || !synth::ProjectBundle::isBundle(dest)) {
        out.message = "the project did not save";
        return out;
    }

    // Read it back and compare with what the plan asked for.
    juce::String error;
    const auto want = planCounts(plan);
    const auto got = readBundle(dest, error);
    out.saved = true;
    if (!got) {
        out.message = "saved, but " + error;
        return out;
    }
    const bool match = got->tracks == want.tracks && got->clips == want.clips && got->notes == want.notes &&
                       got->lanes == want.lanes && got->boundTracks == got->tracks;
    out.message = (match ? "verified: " : "MISMATCH, plan " + want.text() + ", bundle ") + got->text() + ", " +
                  juce::String(got->boundTracks) + "/" + juce::String(got->tracks) + " bound to a Track In";
    return out;
}

// The starting patch for a scenario name from either project-mode list. False when the name is unknown.
inline bool seedForScenario(const juce::String& name, const char*& seed) {
    for (const auto* list : {&project_mode::scenarios(), &project_mode::fullTrackScenarios()})
        for (const auto& scenario : *list)
            if (name == scenario.name) {
                seed = scenario.seedPatch;
                return true;
            }
    return false;
}

// Called by project_mode::runAll for each plan that previews valid; returns the line to print and, in
// `savedPath`, the bundle written (empty when nothing was).
inline project_mode::SaveHook makeSaveHook(const Environment& env, const juce::File& dir, const juce::String& model) {
    return [&env, dir, model](const project_mode::Scenario& scenario, int run, const juce::String& response,
                              juce::String& savedPath) {
        const auto out = applyAndSave(env, scenario.seedPatch, response, bundleFile(dir, model, scenario.name, run));
        if (out.saved)
            savedPath = out.file.getFullPathName();
        return out.saved ? "saved " + out.file.getFullPathName() + " (" + out.message + ")"
                         : "not saved: " + out.message;
    };
}

// `--replay <results.json> --save-projects <dir>`: no model calls. Every record that has a response is
// rebuilt from its scenario's starting state, applied and saved; one line per record.
inline int replay(const Environment& env, const juce::File& results, const juce::File& dir) {
    const juce::var root = juce::JSON::parse(results);
    if (!root.isObject()) {
        std::fprintf(stderr, "cannot read %s as an AIEvalHarness --json file\n", results.getFullPathName().toRawUTF8());
        return 1;
    }
    const auto mode = root.getProperty("mode", juce::var()).toString();
    if (mode != "project" && mode != "track") {
        std::fprintf(stderr, "--replay needs a --mode project or --mode track file (this one is \"%s\")\n",
                     mode.toRawUTF8());
        return 1;
    }
    const auto model = root.getProperty("model", juce::var()).toString();
    const auto* records = root.getProperty("records", juce::var()).getArray();
    if (records == nullptr) {
        std::fprintf(stderr, "%s has no records\n", results.getFullPathName().toRawUTF8());
        return 1;
    }
    int saved = 0, total = 0;
    for (const auto& rec : *records) {
        ++total;
        const auto scenario = rec.getProperty("scenario", juce::var()).toString();
        const int run = (int)rec.getProperty("run", 1);
        const auto response = rec.getProperty("response", juce::var()).toString();
        juce::String line;
        const char* seed = nullptr;
        if (!(bool)rec.getProperty("responded", false) || response.isEmpty())
            line = "skipped: the model gave no response";
        else if (!seedForScenario(scenario, seed))
            line = "skipped: unknown scenario, so its starting state is not known";
        else {
            const auto out = applyAndSave(env, seed, response, bundleFile(dir, model, scenario, run));
            saved += out.saved ? 1 : 0;
            line = out.saved ? "saved " + out.file.getFullPathName() + " (" + out.message + ")"
                             : "not saved: " + out.message;
        }
        std::printf("%-22s run%-3d %s\n", scenario.toRawUTF8(), run, line.toRawUTF8());
        std::fflush(stdout);
    }
    std::printf("\nsaved %d of %d records to %s\n", saved, total, dir.getFullPathName().toRawUTF8());
    return 0;
}

} // namespace save_projects
