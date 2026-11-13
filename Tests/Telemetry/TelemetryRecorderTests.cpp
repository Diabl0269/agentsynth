#include "TelemetryTestFixture.h"

using namespace synth::telemetry;
using synth::telemetry::test::TelemetryTestFixture;

using TelemetryRecorderTest = TelemetryTestFixture;

namespace {

int moduleCount(const DaySummary& day, const char* name) {
    return day.modules[static_cast<size_t>(moduleIndexForFactoryName(name))];
}

int featureCount(const DaySummary& day, Feature feature) { return day.features[static_cast<size_t>(feature)]; }

} // namespace

TEST_F(TelemetryRecorderTest, DisabledRecordsNothingAndWritesNoFiles) {
    auto recorder = makeRecorder();
    recorder.noteSessionStart();
    recorder.addActiveMinutes(30);
    recorder.countModuleAdded("Oscillator");
    recorder.countFeature(Feature::AiRequest);
    recorder.flush();
    EXPECT_FALSE(recorder.isEnabled());
    EXPECT_FALSE(recorder.getToday().has_value());
    EXPECT_TRUE(recorder.getQueue().empty());
    EXPECT_TRUE(recorder.getPendingDays().empty());
    EXPECT_FALSE(idFile().existsAsFile());
    EXPECT_FALSE(queueFile().existsAsFile());
}

TEST_F(TelemetryRecorderTest, EnablingCreatesAnIdAndKeepsItAcrossInstances) {
    auto recorder = makeRecorder();
    recorder.setEnabled(true);
    const auto id = recorder.getTelemetryId();
    EXPECT_EQ(id.length(), 36);
    EXPECT_TRUE(idFile().existsAsFile());

    auto second = makeRecorder();
    second.setEnabled(true);
    EXPECT_EQ(second.getTelemetryId(), id);
}

TEST_F(TelemetryRecorderTest, DisablingErasesTheIdTheQueueFileAndTheCounts) {
    auto recorder = makeRecorder();
    recorder.setEnabled(true);
    recorder.noteSessionStart();
    recorder.countFeature(Feature::PresetSaved);
    recorder.flush();
    ASSERT_TRUE(queueFile().existsAsFile());

    recorder.setEnabled(false);
    EXPECT_FALSE(idFile().existsAsFile());
    EXPECT_FALSE(queueFile().existsAsFile());
    EXPECT_FALSE(recorder.getToday().has_value());
    EXPECT_TRUE(recorder.getQueue().empty());
    EXPECT_TRUE(recorder.getTelemetryId().isEmpty());

    recorder.countFeature(Feature::PresetSaved);
    recorder.flush();
    EXPECT_FALSE(recorder.getToday().has_value());
    EXPECT_FALSE(queueFile().existsAsFile());
}

TEST_F(TelemetryRecorderTest, PurgeRemovesLeftoverFilesWithoutEnabling) {
    TelemetryIdStore(idFile()).create();
    queueFile().replaceWithText("{\"days\":[]}");
    auto recorder = makeRecorder();
    recorder.purge();
    EXPECT_FALSE(idFile().existsAsFile());
    EXPECT_FALSE(queueFile().existsAsFile());
    EXPECT_FALSE(recorder.isEnabled());
}

TEST_F(TelemetryRecorderTest, CountsLandOnTodaysLocalDay) {
    auto recorder = makeRecorder();
    recorder.setEnabled(true);
    recorder.noteSessionStart();
    recorder.noteSessionStart();
    recorder.addActiveMinutes(1);
    recorder.addActiveMinutes(1);
    recorder.countModuleAdded("Amp Env");
    recorder.countFeature(Feature::MacroCreated);
    recorder.countFeature(Feature::ProjectOpened);
    ASSERT_TRUE(recorder.getToday().has_value());
    const auto& today = *recorder.getToday();
    EXPECT_EQ(today.day, "2026-10-07");
    EXPECT_EQ(today.sessions, 2);
    EXPECT_EQ(today.activeMinutes, 2);
    EXPECT_EQ(moduleCount(today, "Amp Env"), 1);
    EXPECT_EQ(featureCount(today, Feature::MacroCreated), 1);
    EXPECT_EQ(featureCount(today, Feature::ProjectOpened), 1);
    EXPECT_EQ(featureCount(today, Feature::AiRequest), 0);
}

TEST_F(TelemetryRecorderTest, CountersStopAtTheContractMaximums) {
    auto recorder = makeRecorder();
    recorder.setEnabled(true);
    for (int i = 0; i < kMaxSessions + 20; ++i)
        recorder.noteSessionStart();
    for (int i = 0; i < kMaxModuleCount + 20; ++i)
        recorder.countModuleAdded("Filter");
    for (int i = 0; i < kMaxFeatureCount + 20; ++i)
        recorder.countFeature(Feature::TimelineUsed);
    recorder.addActiveMinutes(100000);
    const auto& today = *recorder.getToday();
    EXPECT_EQ(today.sessions, kMaxSessions);
    EXPECT_EQ(moduleCount(today, "Filter"), kMaxModuleCount);
    EXPECT_EQ(featureCount(today, Feature::TimelineUsed), kMaxFeatureCount);
    EXPECT_EQ(today.activeMinutes, kMaxActiveMinutes);
}

TEST_F(TelemetryRecorderTest, ModuleNamesMapToTheirCounterAndUnknownNamesAreIgnored) {
    auto recorder = makeRecorder();
    recorder.setEnabled(true);
    for (const char* name :
         {"Amp Env", "MIDI Keyboard", "Hosted Plugin", "Midi Input", "Parametric EQ", "Sample & Hold"})
        recorder.countModuleAdded(name);
    // "Gate" is a real module with no counter in the contract; the rest are not module names at all.
    for (const char* name : {"Gate", "Totally Made Up", "", "amp env", "Oscillator 1"})
        recorder.countModuleAdded(name);
    const auto& today = *recorder.getToday();
    EXPECT_EQ(moduleCount(today, "Amp Env"), 1);
    EXPECT_EQ(moduleCount(today, "MIDI Keyboard"), 1);
    EXPECT_EQ(moduleCount(today, "Hosted Plugin"), 1);
    EXPECT_EQ(moduleCount(today, "Midi Input"), 1);
    EXPECT_EQ(moduleCount(today, "Parametric EQ"), 1);
    EXPECT_EQ(moduleCount(today, "Sample & Hold"), 1);
    int total = 0;
    for (int count : today.modules)
        total += count;
    EXPECT_EQ(total, 6);
    EXPECT_EQ(moduleIndexForFactoryName("Gate"), -1);
}

TEST_F(TelemetryRecorderTest, TheLocalDateChangingMovesTodayIntoTheQueue) {
    auto recorder = makeRecorder();
    recorder.setEnabled(true);
    recorder.noteSessionStart();
    recorder.countFeature(Feature::AiRequest);
    setDay(2026, 10, 8);
    recorder.noteSessionStart();

    ASSERT_EQ(recorder.getQueue().size(), 1u);
    EXPECT_EQ(recorder.getQueue()[0].day, "2026-10-07");
    EXPECT_EQ(featureCount(recorder.getQueue()[0], Feature::AiRequest), 1);
    EXPECT_EQ(recorder.getToday()->day, "2026-10-08");
    EXPECT_EQ(recorder.getToday()->sessions, 1);
    EXPECT_EQ(featureCount(*recorder.getToday(), Feature::AiRequest), 0);
    EXPECT_TRUE(queueFile().existsAsFile()); // the rollover is written at once
}

TEST_F(TelemetryRecorderTest, QueueKeepsTheNewestFourteenDays) {
    auto recorder = makeRecorder();
    recorder.setEnabled(true);
    for (int day = 1; day <= 20; ++day) {
        setDay(2026, 9, day);
        recorder.noteSessionStart();
    }
    setDay(2026, 10, 1);
    recorder.noteSessionStart();
    ASSERT_EQ(recorder.getQueue().size(), TelemetryRecorder::kMaxQueuedDays);
    EXPECT_EQ(recorder.getQueue().front().day, "2026-09-07");
    EXPECT_EQ(recorder.getQueue().back().day, "2026-09-20");
}

TEST_F(TelemetryRecorderTest, PendingDaysAreOnlyThoseBeforeToday) {
    auto recorder = makeRecorder();
    recorder.setEnabled(true);
    recorder.noteSessionStart();
    EXPECT_TRUE(recorder.getPendingDays().empty());
    setDay(2026, 10, 9);
    const auto pending = recorder.getPendingDays();
    ASSERT_EQ(pending.size(), 1u);
    EXPECT_EQ(pending[0].day, "2026-10-07");
}

TEST_F(TelemetryRecorderTest, ARelaunchOnTheSameDayResumesTodayAndOnALaterDayQueuesIt) {
    {
        auto recorder = makeRecorder();
        recorder.setEnabled(true);
        recorder.noteSessionStart();
        recorder.countFeature(Feature::PresetLoaded);
        recorder.flush();
    }
    {
        auto recorder = makeRecorder();
        recorder.setEnabled(true);
        recorder.noteSessionStart();
        EXPECT_EQ(recorder.getToday()->sessions, 2);
        EXPECT_EQ(featureCount(*recorder.getToday(), Feature::PresetLoaded), 1);
        EXPECT_TRUE(recorder.getQueue().empty());
        recorder.flush();
    }
    setDay(2026, 10, 10);
    auto recorder = makeRecorder();
    recorder.setEnabled(true);
    ASSERT_EQ(recorder.getQueue().size(), 1u);
    EXPECT_EQ(recorder.getQueue()[0].sessions, 2);
    EXPECT_FALSE(recorder.getToday().has_value());
}

TEST_F(TelemetryRecorderTest, RemovingASentDayNeedsTheSameId) {
    auto recorder = makeRecorder();
    recorder.setEnabled(true);
    recorder.noteSessionStart();
    setDay(2026, 10, 8);
    ASSERT_EQ(recorder.getPendingDays().size(), 1u);
    recorder.removeSentDay("2026-10-07", "00000000-0000-4000-8000-000000000000");
    EXPECT_EQ(recorder.getQueue().size(), 1u);
    recorder.removeSentDay("2026-10-07", recorder.getTelemetryId());
    EXPECT_TRUE(recorder.getQueue().empty());
}

TEST_F(TelemetryRecorderTest, ADeletedIdFileMeansOptedOutAndTheNextFlushDoesNotRewriteTheQueue) {
    auto recorder = makeRecorder();
    recorder.setEnabled(true);
    recorder.noteSessionStart();
    recorder.flush();
    // What the Preferences toggle does the moment it is switched off, before the recorder hears about it.
    idFile().deleteFile();
    queueFile().deleteFile();
    recorder.countFeature(Feature::PresetSaved);
    recorder.flush();
    EXPECT_FALSE(queueFile().existsAsFile());
    EXPECT_FALSE(recorder.isEnabled());
}

TEST_F(TelemetryRecorderTest, ContextCarriesAVersionTheServerAcceptsAndTheBuildPlatform) {
    auto recorder = makeRecorder("development build");
    recorder.setEnabled(true);
    const auto context = recorder.makeContext();
    EXPECT_EQ(context.appVersion, "0.0.0");
    EXPECT_EQ(context.telemetryId, recorder.getTelemetryId());
    EXPECT_EQ(makeRecorder("0.202.0").makeContext().appVersion, "0.202.0");
    EXPECT_EQ(makeRecorder("1.2.3-beta.1").makeContext().appVersion, "1.2.3-beta.1");
#if JUCE_MAC
    EXPECT_EQ(context.os, platform::contracts::TelemetryOs::Macos);
#endif
}
