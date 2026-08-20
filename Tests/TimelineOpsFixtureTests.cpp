// The recorded fixtures under Tools/TimelineOpsHarness/Fixtures/, asserted here as fast
// gtest cases so CI covers them without building TimelineOpsHarness (which needs no live model,
// but is gated behind -DENABLE_AI_HARNESS=ON like its siblings and so is never part of the
// default build). This file and the harness read the EXACT SAME fixture directory — one recorded
// set, two ways of running it: a table+summary-rate for a human, and gtest assertions for CI.
//
// Each fixture pins an envelope (or, for the two-door pin, a patch) and the outcome it must
// produce. See Tools/TimelineOpsHarness/README.md for the fixture schema.

#include "../Source/AI/AIStateMapper.h"
#include "../Source/Modules/FilterModule.h"
#include "../Source/Modules/OscillatorModule.h"
#include "../Source/Timeline/TimelineDoc.h"
#include "../Source/Timeline/TimelineOps.h"
#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>

namespace {

// Same fixed graph TimelineOpsHarness builds and Tests/TimelineOpsTests.cpp's own fixture uses:
// a Filter and an Oscillator carrying the "filter-uuid"/"osc-uuid" properties a writeLane fixture
// targets.
void buildFixtureGraph(juce::AudioProcessorGraph& graph) {
    graph.clear();
    auto osc = graph.addNode(std::make_unique<OscillatorModule>());
    osc->properties.set("uuid", "osc-uuid");
    auto filter = graph.addNode(std::make_unique<FilterModule>());
    filter->properties.set("uuid", "filter-uuid");
}

juce::Array<juce::File> fixtureFiles() {
    juce::Array<juce::File> files;
#ifdef TIMELINE_OPS_FIXTURES_DIR
    juce::File dir(TIMELINE_OPS_FIXTURES_DIR);
    dir.findChildFiles(files, juce::File::findFiles, false, "*.json");
    files.sort();
#endif
    return files;
}

} // namespace

// A broken build configuration (the compile definition missing, or the directory renamed/emptied
// out from under it) must fail loudly as a broken test, not as zero cases silently passing.
TEST(TimelineOpsFixtureTest, FixtureDirectoryIsPopulated) {
#ifndef TIMELINE_OPS_FIXTURES_DIR
    FAIL() << "TIMELINE_OPS_FIXTURES_DIR was not defined by CMake";
#endif
    const auto files = fixtureFiles();
    ASSERT_GE(files.size(), 6) << "expected at least 6 recorded timeline-ops fixtures";
}

TEST(TimelineOpsFixtureTest, EveryFixtureMatchesItsRecordedExpectation) {
    const auto files = fixtureFiles();
    ASSERT_FALSE(files.isEmpty()) << "TIMELINE_OPS_FIXTURES_DIR has no *.json fixtures";

    for (const auto& file : files) {
        SCOPED_TRACE(file.getFileName());

        const juce::var fixture = juce::JSON::parse(file);
        auto* root = fixture.getDynamicObject();
        ASSERT_NE(root, nullptr) << "fixture is not a JSON object";

        const juce::String kind = root->getProperty("kind").toString();
        const juce::var payload = root->getProperty("payload");
        const bool expectedValid = static_cast<bool>(root->getProperty("expectedValid"));
        const juce::String expectedMessageContains = root->getProperty("expectedMessageContains").toString();
        const juce::String expectedPreviewContains = root->getProperty("expectedPreviewContains").toString();
        const juce::String expectedErrorName = root->getProperty("expectedErrorName").toString();

        juce::AudioProcessorGraph graph;
        buildFixtureGraph(graph);
        synth::TimelineDoc doc;

        if (kind == "timelineOps") {
            const auto result = synth::TimelineOps::validate(payload, doc, graph);
            EXPECT_EQ(result.ok, expectedValid) << "message: " << result.message;
            if (!expectedValid && expectedMessageContains.isNotEmpty())
                EXPECT_TRUE(result.message.contains(expectedMessageContains)) << "actual message: " << result.message;
            if (expectedValid && expectedPreviewContains.isNotEmpty())
                EXPECT_TRUE(result.previewText.contains(expectedPreviewContains))
                    << "actual preview: " << result.previewText;
            // validate() must never mutate the doc — every fixture proves this incidentally.
            EXPECT_TRUE(doc.isEmpty());
        } else if (kind == "patchSmuggle") {
            const auto result =
                synth::AIStateMapper::validatePatch(payload, graph, /*clearExisting=*/true, /*trusted=*/false);
            EXPECT_EQ(result.ok, expectedValid) << "message: " << result.message;
            if (!expectedValid && expectedMessageContains.isNotEmpty())
                EXPECT_TRUE(result.message.contains(expectedMessageContains)) << "actual message: " << result.message;
            if (expectedErrorName.isNotEmpty())
                EXPECT_EQ(synth::patchValidationErrorName(result.error), expectedErrorName);
        } else {
            FAIL() << "unknown fixture kind \"" << kind << "\"";
        }
    }
}
