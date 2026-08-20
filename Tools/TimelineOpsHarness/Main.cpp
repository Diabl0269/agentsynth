/*
    TimelineOpsHarness — offline measurement of timeline-ops / .mid-blob envelope validity
    against a fixed set of RECORDED fixtures (TL8-5).

    AIPatchHarness and AIEvalHarness (Tools/) both measure a LIVE model's output, so what they
    replay is a fixed set of prompts sent to a real Ollama instance. A timeline-ops envelope's
    validity is instead a deterministic function of TimelineOps::validate (or, for the two-door
    pin, AIStateMapper::validatePatch) against a fixed graph — there is no model in this loop — so
    what this harness replays is a fixed set of RECORDED fixture files instead of prompts. Each
    fixture pins an envelope (or, for the "envelope smuggled in a patch" pin, a patch) and the
    outcome it must produce; the same fixtures are asserted as fast gtest cases in
    Tests/TimelineOpsFixtureTests.cpp, which is what CI actually gates on. This tool exists to
    print the same kind of per-case expected-vs-actual table and summary rate its siblings do, so
    a change to TimelineOps/TimelineValidator/MidiClipFile can be eyeballed against every fixture
    at once without needing a live model to do it.

    Exit code is 0 whenever every fixture loaded and ran, whatever the match rate — like its
    siblings, the histogram is the output, not a pass/fail verdict.

        ./build/Tools/TimelineOpsHarness/TimelineOpsHarness [--fixtures DIR] [--json OUT]

    --fixtures defaults to the Fixtures/ directory checked into this tool's own source tree
    (TIMELINE_OPS_FIXTURES_DIR, set by CMake), so no flag is needed for the common case.
*/

#include "AI/AIStateMapper.h"
#include "Modules/FilterModule.h"
#include "Modules/OscillatorModule.h"
#include "Timeline/TimelineDoc.h"
#include "Timeline/TimelineOps.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include <cstdio>
#include <memory>

namespace {

juce::String argValue(const juce::StringArray& args, const juce::String& flag, const juce::String& fallback) {
    int i = args.indexOf(flag);
    if (i >= 0 && i + 1 < args.size())
        return args[i + 1];
    return fallback;
}

// The same fixed graph every fixture is checked against: a Filter and an Oscillator carrying the
// stable "filter-uuid"/"osc-uuid" properties Tests/TimelineOpsTests.cpp's own fixture graph uses,
// so a writeLane fixture targeting them resolves exactly the way the unit tests' does.
void buildFixtureGraph(juce::AudioProcessorGraph& graph) {
    graph.clear();
    auto osc = graph.addNode(std::make_unique<OscillatorModule>());
    osc->properties.set("uuid", "osc-uuid");
    auto filter = graph.addNode(std::make_unique<FilterModule>());
    filter->properties.set("uuid", "filter-uuid");
}

// Outcome of replaying one recorded fixture.
struct FixtureOutcome {
    bool loaded = false; // the fixture file parsed as a JSON object
    bool ranOk = false;  // its "kind" named a path this harness knows how to run
    bool actualValid = false;
    juce::String actualMessage;
    juce::String actualErrorName; // only populated for kind == "patchSmuggle"
    juce::String actualPreview;   // only populated for kind == "timelineOps"
    bool matchesExpectedValidity = false;
    bool matchesExpectedMessage = false;
    bool matchesExpectedErrorName = true; // vacuously true when the fixture names none
};

FixtureOutcome runFixture(const juce::var& fixture) {
    FixtureOutcome outcome;
    auto* root = fixture.getDynamicObject();
    if (root == nullptr) {
        outcome.actualMessage = "fixture is not a JSON object";
        return outcome;
    }
    outcome.loaded = true;

    const juce::String kind = root->getProperty("kind").toString();
    const juce::var payload = root->getProperty("payload");
    const bool expectedValid = static_cast<bool>(root->getProperty("expectedValid"));
    const juce::String expectedMessageContains = root->getProperty("expectedMessageContains").toString();
    const juce::String expectedErrorName = root->getProperty("expectedErrorName").toString();

    juce::AudioProcessorGraph graph;
    buildFixtureGraph(graph);
    synth::TimelineDoc doc;

    if (kind == "timelineOps") {
        const auto result = synth::TimelineOps::validate(payload, doc, graph);
        outcome.ranOk = true;
        outcome.actualValid = result.ok;
        outcome.actualMessage = result.message;
        outcome.actualPreview = result.previewText;
    } else if (kind == "patchSmuggle") {
        const auto result =
            synth::AIStateMapper::validatePatch(payload, graph, /*clearExisting=*/true, /*trusted=*/false);
        outcome.ranOk = true;
        outcome.actualValid = result.ok;
        outcome.actualMessage = result.message;
        outcome.actualErrorName = synth::patchValidationErrorName(result.error);
    } else {
        outcome.actualMessage = "unknown fixture kind \"" + kind + "\"";
        return outcome;
    }

    outcome.matchesExpectedValidity = (outcome.actualValid == expectedValid);
    outcome.matchesExpectedMessage =
        expectedMessageContains.isEmpty() || outcome.actualMessage.contains(expectedMessageContains);
    if (expectedErrorName.isNotEmpty())
        outcome.matchesExpectedErrorName = (outcome.actualErrorName == expectedErrorName);

    return outcome;
}

} // namespace

int main(int argc, char* argv[]) {
    juce::ScopedJuceInitialiser_GUI juceInit;

    juce::StringArray args;
    for (int i = 1; i < argc; ++i)
        args.add(juce::String(argv[i]));

#ifdef TIMELINE_OPS_FIXTURES_DIR
    const juce::String defaultFixturesDir = TIMELINE_OPS_FIXTURES_DIR;
#else
    const juce::String defaultFixturesDir;
#endif
    const juce::String fixturesDirArg = argValue(args, "--fixtures", defaultFixturesDir);
    const juce::String jsonOut = argValue(args, "--json", "");

    if (fixturesDirArg.isEmpty()) {
        std::fprintf(stderr,
                     "no fixtures directory: pass --fixtures DIR, or build with TIMELINE_OPS_FIXTURES_DIR set\n");
        return 1;
    }

    const juce::File fixturesDir(fixturesDirArg);
    juce::Array<juce::File> files;
    fixturesDir.findChildFiles(files, juce::File::findFiles, false, "*.json");
    files.sort();

    std::printf("TimelineOpsHarness  fixtures=%s  count=%d\n", fixturesDirArg.toRawUTF8(), files.size());
    std::printf("%-32s %-9s %-9s %s\n", "fixture", "expected", "actual", "outcome");
    std::printf("----------------------------------------------------------------------------------\n");

    int total = 0, matched = 0, malformed = 0;
    juce::Array<juce::var> records;

    for (const auto& file : files) {
        ++total;
        const juce::var fixture = juce::JSON::parse(file);
        const auto outcome = runFixture(fixture);

        auto* root = fixture.getDynamicObject();
        const juce::String name =
            root != nullptr ? root->getProperty("name").toString() : file.getFileNameWithoutExtension();
        const bool expectedValid = root != nullptr && static_cast<bool>(root->getProperty("expectedValid"));

        if (!outcome.loaded || !outcome.ranOk) {
            ++malformed;
            std::printf("%-32s %-9s %-9s MALFORMED: %s\n", name.toRawUTF8(), "?", "?",
                        outcome.actualMessage.toRawUTF8());
        } else {
            const bool pass =
                outcome.matchesExpectedValidity && outcome.matchesExpectedMessage && outcome.matchesExpectedErrorName;
            if (pass)
                ++matched;
            const juce::String label = pass ? juce::String("PASS") : ("MISMATCH: " + outcome.actualMessage);
            std::printf("%-32s %-9s %-9s %s\n", name.toRawUTF8(), expectedValid ? "valid" : "invalid",
                        outcome.actualValid ? "valid" : "invalid", label.toRawUTF8());
        }

        juce::DynamicObject::Ptr rec = new juce::DynamicObject();
        rec->setProperty("name", name);
        rec->setProperty("expectedValid", expectedValid);
        rec->setProperty("actualValid", outcome.actualValid);
        rec->setProperty("actualMessage", outcome.actualMessage);
        rec->setProperty("actualErrorName", outcome.actualErrorName);
        rec->setProperty("actualPreview", outcome.actualPreview);
        rec->setProperty("matches", outcome.matchesExpectedValidity && outcome.matchesExpectedMessage &&
                                        outcome.matchesExpectedErrorName);
        records.add(juce::var(rec.get()));
    }

    const double rate = total > 0 ? (100.0 * matched / total) : 0.0;
    std::printf("\n=================== SUMMARY ===================\n");
    std::printf("fixtures:            %d\n", total);
    std::printf("matched expectation: %d  (%.1f%%)\n", matched, rate);
    if (malformed > 0)
        std::printf("malformed/unreadable: %d\n", malformed);
    std::printf("===============================================\n");

    if (jsonOut.isNotEmpty()) {
        juce::DynamicObject::Ptr root = new juce::DynamicObject();
        root->setProperty("total", total);
        root->setProperty("matched", matched);
        root->setProperty("records", records);
        juce::File(jsonOut).replaceWithText(juce::JSON::toString(juce::var(root.get()), true));
        std::printf("wrote %s\n", jsonOut.toRawUTF8());
    }

    return 0;
}
