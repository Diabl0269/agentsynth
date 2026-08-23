// Characterisation tests for the corpus recorded under Tests/fixtures/ai-patches/: real model
// output captured once by Tools/AIPatchHarness (see its README's "Recording the fixture corpus"
// section), replayed forever through the EXACT production path with no model and no network:
//
//     extractJsonFromResponse -> JSON::parse -> validatePatch -> applyPatchWithRetry
//
// against a provider double that hands back the recorded retry answers instead of calling a real
// model. Each record's recorded outcome (accept/reject decision, PatchValidationError, whether
// applyPatchWithRetry ultimately applied the patch) is asserted exactly. This is DELIBERATELY
// intolerant: a behaviour change in the retry/validation path SHOULD fail this test, same as it
// should fail any other characterisation test. A failure means one of two things, and only a
// human can tell which: a real regression (fix the code), or an intended behaviour change (fix
// the fixture — see "Re-recording the corpus" below). Never loosen an assertion here to make the
// suite green again.
//
// Re-recording the corpus (one command per model):
//
//     cmake -S . -B build -DENABLE_AI_HARNESS=ON
//     cmake --build build --target AIPatchHarness
//     ./build/Tools/AIPatchHarness/AIPatchHarness --model gpt-oss:20b --runs 2 \
//         --json Tests/fixtures/ai-patches/gpt-oss-20b.json
//
// See Tools/AIPatchHarness/README.md for the full corpus-recording recipe and the --json record
// format these tests parse.

#include "../Source/AI/AIIntegrationService.h"
#include "../Source/AI/AIStateMapper.h"
#include "../Tools/AIPatchHarness/Scenarios.h"
#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <map>

namespace {

// A fixed script of raw model answers, replayed in call order — the offline stand-in for the
// retry correction round-trips applyPatchWithRetry() makes through a real provider. Once the
// script runs out the last entry repeats (same behaviour as Tests/AIPatchRetryTests.cpp's
// ScriptedProvider), so a fixture recorded with fewer retry answers than the replay needs still
// produces a deterministic (if not necessarily matching) result rather than crashing.
class ReplayProvider : public synth::AIProvider {
public:
    explicit ReplayProvider(std::vector<juce::String> scriptedResponses)
        : script(std::move(scriptedResponses)) {}

    RequestId sendPrompt(const std::vector<Message>&, CompletionCallback callback, const juce::var&,
                         std::function<void(const juce::String&)>) override {
        AIResponse response;
        if (script.empty()) {
            response.success = false;
            response.error.message = "ReplayProvider has no recorded retryResponses for this fixture";
        } else {
            response.success = true;
            response.content = script[juce::jmin((size_t)callCount, script.size() - 1)];
        }
        ++callCount;
        if (callback)
            callback(response);
        return {};
    }

    void cancel(RequestId) override {}
    juce::String getProviderName() const override { return "ReplayProvider"; }
    void setModel(const juce::String&) override {}
    juce::String getCurrentModel() const override { return {}; }
    void setRequestTimeoutMs(int) override {}
    int getRequestTimeoutMs() const override { return 0; }
    void fetchAvailableModels(std::function<void(const juce::StringArray&, bool)> callback) override {
        if (callback)
            callback({}, true);
    }

    std::vector<juce::String> script;
    int callCount = 0;
};

const std::map<juce::String, const synth::harness::Scenario*>& scenariosByName() {
    static const std::map<juce::String, const synth::harness::Scenario*> m = [] {
        std::map<juce::String, const synth::harness::Scenario*> result;
        for (const auto& s : synth::harness::scenarios())
            result[s.name] = &s;
        return result;
    }();
    return m;
}

// Every *.json corpus file committed under Tests/fixtures/ai-patches/ — one file per recorded
// harness run (see the compile definition AI_PATCH_FIXTURES_DIR in Tests/CMakeLists.txt).
juce::Array<juce::File> corpusFiles() {
    juce::Array<juce::File> files;
#ifdef AI_PATCH_FIXTURES_DIR
    juce::File dir(AI_PATCH_FIXTURES_DIR);
    dir.findChildFiles(files, juce::File::findFiles, false, "*.json");
    files.sort();
#endif
    return files;
}

void buildSeedGraph(const synth::harness::Scenario& scenario, juce::AudioProcessorGraph& graph) {
    if (scenario.seedPatch == nullptr)
        return;
    const juce::var seed = juce::JSON::parse(juce::String(scenario.seedPatch));
    // trusted=true: this is the harness's own fixture seed, not model output — see Scenarios.h.
    ASSERT_TRUE(synth::AIStateMapper::applyJSONToGraph(seed, graph, /*clearExisting=*/true, /*trusted=*/true));
}

} // namespace

// A broken build configuration (the compile definition missing, or the directory renamed/emptied
// out from under it) must fail loudly as a broken test, not as zero cases silently passing — and
// the corpus must actually contain a rejection, or this suite is only exercising the happy path.
TEST(AIPatchFixtureReplayTest, CorpusIsPopulatedAndContainsARejection) {
#ifndef AI_PATCH_FIXTURES_DIR
    FAIL() << "AI_PATCH_FIXTURES_DIR was not defined by CMake";
#endif
    const auto files = corpusFiles();
    ASSERT_FALSE(files.isEmpty()) << "no *.json corpus files under Tests/fixtures/ai-patches/ — see "
                                     "Tools/AIPatchHarness/README.md to record one";

    int respondedRecords = 0;
    bool sawRejection = false;
    for (const auto& file : files) {
        const juce::var root = juce::JSON::parse(file);
        const auto* rootObj = root.getDynamicObject();
        ASSERT_NE(rootObj, nullptr) << file.getFileName();
        const juce::var records = rootObj->getProperty("records");
        for (int i = 0; i < records.size(); ++i) {
            const auto* rec = records[i].getDynamicObject();
            ASSERT_NE(rec, nullptr) << file.getFileName();
            if (!static_cast<bool>(rec->getProperty("responded")))
                continue;
            ++respondedRecords;
            if (!static_cast<bool>(rec->getProperty("valid")))
                sawRejection = true;
        }
    }

    EXPECT_GE(respondedRecords, 60) << "expected at least ~60 recorded model responses across the corpus "
                                       "(P1-12 aim: 60-100)";
    EXPECT_TRUE(sawRejection) << "corpus must include at least one recorded rejection, not only successes — "
                                 "a corpus of only-valid patches can't characterise the retry path";
}

TEST(AIPatchFixtureReplayTest, EveryRecordedResponseReplaysItsRecordedOutcome) {
    const auto files = corpusFiles();
    ASSERT_FALSE(files.isEmpty()) << "no *.json corpus files under Tests/fixtures/ai-patches/ — see "
                                     "Tools/AIPatchHarness/README.md to record one";

    const auto& byName = scenariosByName();

    for (const auto& file : files) {
        const juce::var root = juce::JSON::parse(file);
        const auto* rootObj = root.getDynamicObject();
        ASSERT_NE(rootObj, nullptr) << file.getFileName();
        const juce::String model = rootObj->getProperty("model").toString();
        const juce::var records = rootObj->getProperty("records");

        for (int i = 0; i < records.size(); ++i) {
            const auto* rec = records[i].getDynamicObject();
            ASSERT_NE(rec, nullptr) << file.getFileName();

            const juce::String scenarioName = rec->getProperty("scenario").toString();
            const int run = static_cast<int>(rec->getProperty("run"));
            SCOPED_TRACE(("file=" + file.getFileName() + " model=" + model + " scenario=" + scenarioName +
                          " run=" + juce::String(run))
                             .toStdString());

            // A provider error (network/timeout) leaves no raw model text — nothing to replay.
            if (!static_cast<bool>(rec->getProperty("responded")))
                continue;

            const auto it = byName.find(scenarioName);
            ASSERT_NE(it, byName.end())
                << "fixture references scenario \"" << scenarioName
                << "\" which no longer exists in Tools/AIPatchHarness/Scenarios.h — the corpus has drifted "
                   "from the prompt set that produced it and must be re-recorded";
            const auto& scenario = *it->second;
            ASSERT_EQ(scenario.mergeMode, static_cast<bool>(rec->getProperty("merge")))
                << "scenario \"" << scenarioName << "\" changed mode since this fixture was recorded";

            const juce::String rawResponse = rec->getProperty("rawResponse").toString();
            const bool expectedParsed = static_cast<bool>(rec->getProperty("parsed"));
            const bool expectedValid = static_cast<bool>(rec->getProperty("valid"));
            const juce::String expectedErrorName = rec->getProperty("error").toString();
            const bool expectedAppliedAfterRetry = static_cast<bool>(rec->getProperty("appliedAfterRetry"));
            const int expectedRetriesUsed = static_cast<int>(rec->getProperty("retriesUsed"));

            // Step 1: extractJsonFromResponse -> JSON::parse -> validatePatch, exactly the check
            // that produced the recorded `parsed`/`valid`/`error` fields.
            {
                juce::AudioProcessorGraph graph;
                buildSeedGraph(scenario, graph);

                const juce::var parsed =
                    juce::JSON::parse(synth::AIIntegrationService::extractJsonFromResponse(rawResponse));
                EXPECT_EQ(parsed.isObject(), expectedParsed) << "extraction/parse result changed";

                const auto validation = synth::AIStateMapper::validatePatch(
                    parsed, graph, /*clearExisting=*/!scenario.mergeMode, /*trusted=*/false);
                EXPECT_EQ(validation.ok, expectedValid)
                    << "recorded message: " << rec->getProperty("message").toString()
                    << " / replayed message: " << validation.message;
                EXPECT_EQ(synth::patchValidationErrorName(validation.error), expectedErrorName);
            }

            // Step 2: applyPatchWithRetry against a double that replays the recorded retry
            // answers — the number the user actually experiences, per-record.
            {
                juce::AudioProcessorGraph graph;
                buildSeedGraph(scenario, graph);
                synth::AIIntegrationService service(graph);

                std::vector<juce::String> retryScript;
                if (const auto* retryArr = rec->getProperty("retryResponses").getArray())
                    for (const auto& v : *retryArr)
                        retryScript.push_back(v.toString());
                service.setProvider(std::make_unique<ReplayProvider>(std::move(retryScript)));

                bool completed = false;
                bool applied = false;
                int retriesUsed = 0;
                juce::String replayedFinalError;
                service.applyPatchWithRetry(
                    rawResponse, scenario.mergeMode,
                    [&](bool ok, const juce::String& error) {
                        completed = true;
                        applied = ok;
                        replayedFinalError = error;
                    },
                    [&](const synth::AIIntegrationService::PatchRetryInfo&) { ++retriesUsed; });

                ASSERT_TRUE(completed) << "applyPatchWithRetry's onComplete must fire synchronously against a "
                                          "synchronous provider double";
                // The number of correction round-trips is a production invariant worth pinning on
                // its own, independent of the final applied/rejected outcome: a regression that
                // makes the retry loop need one MORE (or fewer) round-trip than the original run
                // is a real behaviour change even when the two happen to end the same way. This
                // also guards against ReplayProvider's repeat-last fallback (mirroring
                // ScriptedProvider in AIPatchRetryTests.cpp) masking an extra call — a recorded
                // fixture's `retryResponses` is evidence, not an authored script, so needing more
                // calls than were recorded would otherwise silently re-serve the last entry instead
                // of surfacing the discrepancy.
                EXPECT_EQ(retriesUsed, expectedRetriesUsed) << "retry count changed — the retry loop itself may "
                                                               "have regressed, independent of the final outcome";
                EXPECT_EQ(applied, expectedAppliedAfterRetry)
                    << "recorded finalError: " << rec->getProperty("finalError").toString()
                    << " / replayed finalError: " << replayedFinalError;
            }
        }
    }
}
