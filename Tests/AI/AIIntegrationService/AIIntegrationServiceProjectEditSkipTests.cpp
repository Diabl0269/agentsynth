// An edit plan with a few bad details is repaired, not refused
// (docs/ai/timeline-ops.md#one-bad-detail-does-not-sink-the-plan): a writeLane on a node the response never creates, on
// a setting its module lacks, or on a repeated id is skipped; a value outside the range is clamped; and the plan is
// still refused for too many bad details, for a bad detail that leaves no notes, and with the response's own op index
// in the message.
#include "AIIntegrationServiceTestFixture.h"
#include "PlanFakeHost.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"

namespace synth {

namespace {

juce::var parse(const juce::String& json) {
    const juce::var value = juce::JSON::parse(json);
    EXPECT_FALSE(value.isVoid()) << "test JSON did not parse: " << json;
    return value;
}

// A Bass track with a Filter insert (7002), one clip of notes, then `extraOps` (comma first, or empty).
juce::var planWith(const juce::String& extraOps, const juce::String& patch = "\"nodes\": [], \"connections\": []",
                   const juce::String& insertId = "7002") {
    return parse("{\"mode\": \"merge\", " + patch + R"(, "timelineOps": [
        {"op": "addInstrumentTrack", "name": "Bass", "instrument": "Oscillator", "instrumentId": 7001,
         "inserts": [{"type": "Filter", "id": )" +
                 insertId + R"(}]},
        {"op": "placeClips", "track": "Bass", "clips": [{"startBeat": 0, "lengthBeats": 4, "notes": [
            {"startBeat": 0, "lengthBeats": 1, "pitch": 36, "velocity": 100}]}]})" +
                 extraOps + "]}");
}

juce::String lane(const juce::String& nodeRef, const juce::String& paramId, double value) {
    return R"(, {"op": "writeLane", )" + nodeRef + R"(, "paramId": ")" + paramId +
           R"(", "points": [{"beat": 0, "value": 400}, {"beat": 8, "value": )" + juce::String(value) + "}]}";
}

const AutomationLane* firstLane(const TimelineDoc& doc) {
    for (const auto& track : doc.getTracks())
        if (!track.lanes.empty())
            return &track.lanes.front();
    return nullptr;
}

int laneCount(const TimelineDoc& doc) {
    int count = 0;
    for (const auto& track : doc.getTracks())
        count += static_cast<int>(track.lanes.size());
    return count;
}

} // namespace

class AIIntegrationServiceProjectEditSkipTest : public AIIntegrationServiceTest {
protected:
    void SetUp() override {
        AIIntegrationServiceTest::SetUp();
        host = std::make_unique<PlanFakeHost>(doc, *graph);
        service->setTimelineContext(&doc, nullptr);
        service->setTimelineOpsHost(host.get());
    }

    TimelineDoc doc;
    std::unique_ptr<PlanFakeHost> host;
};

TEST_F(AIIntegrationServiceProjectEditSkipTest, APlanWithNothingToRepairCarriesNoNotes) {
    const auto plan = planWith(lane("\"nodeId\": 7002", "cutoff", 3000));
    const auto preview = service->previewProjectEdit(plan);
    ASSERT_TRUE(preview.ok) << preview.message;
    EXPECT_TRUE(preview.notes.isEmpty());
    EXPECT_EQ(preview.skippedCount + preview.adjustedCount, 0);
}

TEST_F(AIIntegrationServiceProjectEditSkipTest, ALaneOnANodeTheResponseNeverCreatesIsSkipped) {
    const auto plan = planWith(lane("\"nodeId\": 42", "cutoff", 3000) + lane("\"nodeId\": 7002", "cutoff", 3000));
    const auto preview = service->previewProjectEdit(plan);
    ASSERT_TRUE(preview.ok) << preview.message;
    EXPECT_EQ(preview.skippedCount, 1);
    ASSERT_EQ(preview.notes.size(), 1);
    EXPECT_TRUE(preview.notes[0].contains("never created")) << preview.notes[0];

    const auto applied = service->applyProjectEdit(plan);
    ASSERT_TRUE(applied.ok) << applied.message;
    EXPECT_EQ(applied.skippedCount, 1);
    EXPECT_EQ(laneCount(doc), 1) << "the good lane landed; the bad one did not";
}

TEST_F(AIIntegrationServiceProjectEditSkipTest, ALaneOnASettingTheModuleLacksIsSkipped) {
    const auto plan = planWith(lane("\"nodeId\": 7002", "wobble", 3) + lane("\"nodeId\": 7002", "cutoff", 3000));
    const auto preview = service->previewProjectEdit(plan);
    ASSERT_TRUE(preview.ok) << preview.message;
    EXPECT_EQ(preview.skippedCount, 1);
    ASSERT_EQ(preview.notes.size(), 1);
    EXPECT_TRUE(preview.notes[0].contains("setting the Filter doesn't have")) << preview.notes[0];

    ASSERT_TRUE(service->applyProjectEdit(plan).ok);
    ASSERT_EQ(laneCount(doc), 1);
    EXPECT_EQ(firstLane(doc)->paramId, "cutoff");
}

TEST_F(AIIntegrationServiceProjectEditSkipTest, ALaneValueOutsideTheRangeIsClampedNotSkipped) {
    const auto plan = planWith(lane("\"nodeId\": 7002", "cutoff", 1.0e9));
    const auto preview = service->previewProjectEdit(plan);
    ASSERT_TRUE(preview.ok) << preview.message;
    EXPECT_EQ(preview.skippedCount, 0);
    EXPECT_EQ(preview.adjustedCount, 1);
    ASSERT_EQ(preview.notes.size(), 1);
    EXPECT_TRUE(preview.notes[0].contains("brought back into its")) << preview.notes[0];

    ASSERT_TRUE(service->applyProjectEdit(plan).ok);
    const auto* written = firstLane(doc);
    ASSERT_NE(written, nullptr);
    ASSERT_EQ(written->points.size(), 2u);
    EXPECT_DOUBLE_EQ(written->points.back().value, static_cast<double>(written->range.maxValue));
    EXPECT_LT(written->points.back().value, 1.0e9);
}

TEST_F(AIIntegrationServiceProjectEditSkipTest, ARepeatedInsertIdIsLeftOutAndLanesNamingItAreSkipped) {
    // Insert id 5 repeats a patch node id: the track is still built, the id binds nothing, and a lane naming 5
    // could mean either module, so it is skipped too.
    const auto plan = planWith(lane("\"nodeId\": 5", "cutoff", 3000),
                               "\"nodes\": [{\"id\": 5, \"type\": \"LFO\"}], \"connections\": []", "5");
    const auto preview = service->previewProjectEdit(plan);
    ASSERT_TRUE(preview.ok) << preview.message;
    EXPECT_EQ(preview.skippedCount, 1);
    EXPECT_EQ(preview.adjustedCount, 1);
    ASSERT_EQ(preview.notes.size(), 2);
    EXPECT_TRUE(preview.notes[0].contains("left out")) << preview.notes[0];
    EXPECT_TRUE(preview.notes[1].contains("used twice")) << preview.notes[1];
}

TEST_F(AIIntegrationServiceProjectEditSkipTest, ThreeBadDetailsAreRepairedAndFourRefuseThePlan) {
    juce::String three;
    for (int i = 0; i < 3; ++i)
        three += lane("\"nodeId\": 42", "cutoff", 3000);
    const auto four = three + lane("\"nodeId\": 43", "cutoff", 3000);
    const auto ok = service->previewProjectEdit(planWith(three));
    ASSERT_TRUE(ok.ok) << ok.message;
    EXPECT_EQ(ok.skippedCount, 3);

    const auto refused = service->previewProjectEdit(planWith(four));
    EXPECT_FALSE(refused.ok);
    EXPECT_TRUE(refused.message.contains("is not a node this response creates")) << refused.message;
    EXPECT_TRUE(refused.message.startsWith("timelineOps[2] (writeLane)")) << refused.message;
}

TEST_F(AIIntegrationServiceProjectEditSkipTest, AnUnknownSettingCountsTowardTheLimitToo) {
    const auto plan = planWith(lane("\"nodeId\": 7002", "a", 1) + lane("\"nodeId\": 7002", "b", 1) +
                               lane("\"nodeId\": 7002", "c", 1) + lane("\"nodeId\": 7002", "d", 1));
    const auto refused = service->previewProjectEdit(plan);
    EXPECT_FALSE(refused.ok);
    EXPECT_TRUE(refused.message.contains("which module \"Filter\" does not have")) << refused.message;
    EXPECT_EQ(laneCount(doc), 0);
}

TEST_F(AIIntegrationServiceProjectEditSkipTest, ABadDetailThatLeavesNoNotesRefusesThePlan) {
    const auto plan = parse(R"({"mode": "merge", "nodes": [], "connections": [], "timelineOps": [
        {"op": "addInstrumentTrack", "name": "Bass", "instrument": "Oscillator",
         "inserts": [{"type": "Filter", "id": 7002}]},
        {"op": "writeLane", "nodeId": 42, "paramId": "cutoff", "points": [{"beat": 0, "value": 400}]}]})");
    const auto refused = service->previewProjectEdit(plan);
    EXPECT_FALSE(refused.ok);
    EXPECT_TRUE(refused.message.contains("is not a node this response creates")) << refused.message;
}

TEST_F(AIIntegrationServiceProjectEditSkipTest, ABadDetailOnAPlanWithOnlyThatOpRefusesIt) {
    const auto refused = service->previewProjectEdit(parse(R"({"timelineOps": [
        {"op": "writeLane", "nodeId": 42, "paramId": "cutoff", "points": [{"beat": 0, "value": 400}]}]})"));
    EXPECT_FALSE(refused.ok);
    EXPECT_TRUE(refused.message.contains("is not a node this response creates")) << refused.message;
}

TEST_F(AIIntegrationServiceProjectEditSkipTest, ABadInstrumentIdStillRefusesThePlan) {
    const auto refused = service->previewProjectEdit(
        parse(R"({"mode": "merge", "nodes": [{"id": 7001, "type": "LFO"}], "connections": [], "timelineOps": [
        {"op": "addInstrumentTrack", "name": "Bass", "instrument": "Oscillator", "instrumentId": 7001},
        {"op": "placeClips", "track": "Bass", "clips": [{"startBeat": 0, "lengthBeats": 4, "notes": [
            {"startBeat": 0, "lengthBeats": 1, "pitch": 36, "velocity": 100}]}]}]})"));
    EXPECT_FALSE(refused.ok);
    EXPECT_TRUE(refused.message.contains("Id 7001 is used twice")) << refused.message;
}

// Messages keep the response's own index even after an earlier op left the list.
TEST_F(AIIntegrationServiceProjectEditSkipTest, RenumberingFollowsTheOpsThatStay) {
    const auto plan =
        planWith(lane("\"nodeId\": 42", "cutoff", 3000) +
                 R"(, {"op": "placeClips", "track": "Nowhere", "clips": [{"startBeat": 0, "lengthBeats": 4}]})");
    const auto refused = service->previewProjectEdit(plan);
    EXPECT_FALSE(refused.ok);
    EXPECT_TRUE(refused.message.startsWith("timelineOps[3] (placeClips)")) << refused.message;
}

TEST_F(AIIntegrationServiceProjectEditSkipTest, ARepairedPlanStillUndoesAsOneStep) {
    const auto plan = planWith(lane("\"nodeId\": 42", "cutoff", 3000) + lane("\"nodeId\": 7002", "cutoff", 1.0e9));
    const auto applied = service->applyProjectEdit(plan);
    ASSERT_TRUE(applied.ok) << applied.message;
    EXPECT_EQ(host->batches, 1);
    ASSERT_TRUE(host->undo.undo());
    EXPECT_FALSE(host->undo.canUndo());
    EXPECT_EQ(laneCount(doc), 0);
}

} // namespace synth
