// TimelineDoc track monitor tests: the Monitor mode's setter rules, one undo step, duplication and the project-file
// round trip; plus the pure rules every monitoring path shares (when a track hears its input, whether an input is
// connected) and the guard's report slots.

#include "AppUndoManager.h"
#include "Timeline/TrackAudioInput.h"
#include "TimelineDocTestHelpers.h"
#include "Transport/MonitorGuardReports.h"
#include <vector>

using synth::MonitorMode;

TEST_F(TimelineDocTest, NewAudioTrackMonitorsOnAuto) {
    const auto id = doc.addTrack(TrackKind::Audio, "Vox");
    EXPECT_EQ(doc.getTrack(id)->monitorMode, MonitorMode::Auto);
}

TEST_F(TimelineDocTest, SetTrackMonitorModeRefusesTracksThatHaveNoAudioInput) {
    const auto midi = doc.addTrack(TrackKind::Midi, "Keys");
    EXPECT_FALSE(doc.setTrackMonitorMode(midi, MonitorMode::On));
    EXPECT_EQ(doc.getTrack(midi)->monitorMode, MonitorMode::Auto);
    EXPECT_FALSE(doc.setTrackMonitorMode(synth::TrackId{999}, MonitorMode::On));
}

TEST_F(TimelineDocTest, SetTrackMonitorModeNotifiesOnlyOnAChange) {
    const auto id = doc.addTrack(TrackKind::Audio, "Vox");
    const auto before = doc.getRevision();
    EXPECT_TRUE(doc.setTrackMonitorMode(id, MonitorMode::Auto));
    EXPECT_EQ(doc.getRevision(), before);
    EXPECT_TRUE(doc.setTrackMonitorMode(id, MonitorMode::Off));
    EXPECT_GT(doc.getRevision(), before);
}

TEST_F(TimelineDocTest, SetTrackMonitorModeIsOneUndoStep) {
    AppUndoManager undo;
    const auto id = doc.addTrack(TrackKind::Audio, "Vox");
    ASSERT_TRUE(undo.recordTimelineChange(doc, [&] { doc.setTrackMonitorMode(id, MonitorMode::On); }));
    EXPECT_EQ(doc.getTrack(id)->monitorMode, MonitorMode::On);
    ASSERT_TRUE(undo.undo());
    EXPECT_EQ(doc.getTrack(id)->monitorMode, MonitorMode::Auto);
    ASSERT_TRUE(undo.redo());
    EXPECT_EQ(doc.getTrack(id)->monitorMode, MonitorMode::On);
}

TEST_F(TimelineDocTest, DuplicateTrackKeepsTheMonitorMode) {
    const auto id = doc.addTrack(TrackKind::Audio, "Vox");
    ASSERT_TRUE(doc.setTrackMonitorMode(id, MonitorMode::Off));
    const auto copy = doc.duplicateTrack(id, "Vox 2", 0xff00ff00, {});
    ASSERT_TRUE(copy.isValid());
    EXPECT_EQ(doc.getTrack(copy)->monitorMode, MonitorMode::Off);
}

TEST_F(TimelineDocTest, MonitorModeSurvivesTheRoundTripAndAutoWritesNoKey) {
    const auto on = doc.addTrack(TrackKind::Audio, "On");
    const auto off = doc.addTrack(TrackKind::Audio, "Off");
    const auto autoTrack = doc.addTrack(TrackKind::Audio, "Auto");
    ASSERT_TRUE(doc.setTrackMonitorMode(on, MonitorMode::On));
    ASSERT_TRUE(doc.setTrackMonitorMode(off, MonitorMode::Off));

    const auto text = juce::JSON::toString(doc.toVar());
    EXPECT_TRUE(text.contains("\"monitor\": \"on\"")) << text;
    EXPECT_TRUE(text.contains("\"monitor\": \"off\"")) << text;
    EXPECT_FALSE(text.contains("\"monitor\": \"auto\"")) << "the default is left out";

    TimelineDoc loaded;
    ASSERT_TRUE(loaded.fromVar(juce::JSON::parse(text)));
    EXPECT_EQ(loaded.getTrack(on)->monitorMode, MonitorMode::On);
    EXPECT_EQ(loaded.getTrack(off)->monitorMode, MonitorMode::Off);
    EXPECT_EQ(loaded.getTrack(autoTrack)->monitorMode, MonitorMode::Auto);
    EXPECT_EQ(dump(loaded), dump(doc));
}

TEST_F(TimelineDocTest, FromVarRejectsAnUnknownMonitorValue) {
    for (const juce::String value : {R"("loud")", R"(1)", R"(true)", R"("On")"}) {
        TimelineDoc loaded;
        EXPECT_FALSE(
            loaded.fromVar(juce::JSON::parse(R"({"version":1,"tracks":[{"id":1,"kind":1,"monitor":)" + value + "}]}")))
            << value;
    }
    TimelineDoc loaded;
    ASSERT_TRUE(loaded.fromVar(juce::JSON::parse(R"({"version":1,"tracks":[{"id":1,"kind":1,"monitor":"auto"}]})")));
    EXPECT_EQ(loaded.getTracks().front().monitorMode, MonitorMode::Auto);
}

// Auto hears the input while armed and either stopped or recording; never during plain playback, never unarmed.
TEST(MonitorRuleTest, MonitorHearsInputTable) {
    for (const bool armed : {false, true})
        for (const bool playing : {false, true})
            for (const bool recording : {false, true}) {
                EXPECT_TRUE(synth::monitorHearsInput(MonitorMode::On, armed, playing, recording));
                EXPECT_FALSE(synth::monitorHearsInput(MonitorMode::Off, armed, playing, recording));
                const bool expected = armed && (!playing || recording);
                EXPECT_EQ(synth::monitorHearsInput(MonitorMode::Auto, armed, playing, recording), expected)
                    << "armed " << armed << " playing " << playing << " recording " << recording;
            }
}

TEST(MonitorRuleTest, AudioInputConnectedNeedsTheSameDeviceAndEveryChannelOn) {
    synth::AudioInputStatus status;
    status.deviceName = "Scarlett";
    status.deviceInputs = 4;
    status.activeInputs = 3;
    status.activeMask = 0b0111;

    synth::AudioInputChoice choice;
    EXPECT_TRUE(synth::isAudioInputConnected(choice, status)) << "No input is never cut off";
    choice.kind = synth::AudioInputChoice::Kind::WholeMix;
    EXPECT_TRUE(synth::isAudioInputConnected(choice, status));

    choice.kind = synth::AudioInputChoice::Kind::Device;
    choice.firstChannel = 0;
    choice.numChannels = 2;
    choice.deviceName = "Scarlett";
    EXPECT_TRUE(synth::isAudioInputConnected(choice, status));
    choice.firstChannel = 2;
    EXPECT_FALSE(synth::isAudioInputConnected(choice, status)) << "In 4 is off";
    choice.numChannels = 1;
    EXPECT_TRUE(synth::isAudioInputConnected(choice, status));
    choice.deviceName = "MOTU";
    EXPECT_FALSE(synth::isAudioInputConnected(choice, status)) << "another device";
    choice.deviceName = {};
    EXPECT_TRUE(synth::isAudioInputConnected(choice, status)) << "an empty name means whatever is open";

    status.hosted = true;
    status.deviceName = {};
    choice.deviceName = "MOTU";
    EXPECT_TRUE(synth::isAudioInputConnected(choice, status)) << "the host's inputs carry no device";
}

TEST(MonitorGuardReportsTest, ReportsEachTrackOnceAndForgetsOnConsume) {
    synth::MonitorGuardReports reports;
    reports.report(7);
    reports.report(7);
    reports.report(9);
    std::vector<std::int64_t> seen;
    reports.consume([&](std::int64_t id) { seen.push_back(id); });
    EXPECT_EQ(seen, (std::vector<std::int64_t>{7, 9}));
    seen.clear();
    reports.consume([&](std::int64_t id) { seen.push_back(id); });
    EXPECT_TRUE(seen.empty());
}

TEST(MonitorGuardReportsTest, AFullSetDropsTheExtraReport) {
    synth::MonitorGuardReports reports;
    for (int id = 1; id <= synth::MonitorGuardReports::kSlots + 3; ++id)
        reports.report(id);
    int count = 0;
    reports.consume([&](std::int64_t) { ++count; });
    EXPECT_EQ(count, synth::MonitorGuardReports::kSlots);
}
