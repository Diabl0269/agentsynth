#include "Telemetry/TelemetryJson.h"
#include <gtest/gtest.h>

using namespace synth::telemetry;
namespace contracts = platform::contracts;

namespace {

DailyContext context() {
    DailyContext c;
    c.telemetryId = "3f2b8c1e-5a4d-4c6b-9e7f-0a1b2c3d4e5f";
    c.appVersion = "0.202.0";
    c.os = contracts::TelemetryOs::Macos;
    c.arch = contracts::TelemetryArch::Arm64;
    c.format = contracts::TelemetryFormat::Standalone;
    return c;
}

DaySummary minimalDay() {
    DaySummary day;
    day.day = "2026-10-06";
    day.sessions = 1;
    return day;
}

} // namespace

TEST(TelemetryJsonTest, MinimalRecordHasOnlyTheRequiredKeys) {
    EXPECT_EQ(serializeDaily(toDaily(minimalDay(), context())),
              "{\"telemetryId\":\"3f2b8c1e-5a4d-4c6b-9e7f-0a1b2c3d4e5f\",\"day\":\"2026-10-06\","
              "\"appVersion\":\"0.202.0\",\"os\":\"macos\",\"arch\":\"arm64\",\"format\":\"standalone\","
              "\"sessions\":1,\"activeMinutesBucket\":\"<15\"}");
}

TEST(TelemetryJsonTest, FullRecordListsEverySetCounterInContractOrder) {
    auto day = minimalDay();
    day.sessions = 3;
    day.activeMinutes = 75;
    day.modules[static_cast<size_t>(moduleIndexForFactoryName("Oscillator"))] = 4;
    day.modules[static_cast<size_t>(moduleIndexForFactoryName("Amp Env"))] = 2;
    day.modules[static_cast<size_t>(moduleIndexForFactoryName("Sample & Hold"))] = 1;
    day.features[static_cast<size_t>(Feature::MacroCreated)] = 5;
    day.features[static_cast<size_t>(Feature::AiRequest)] = 7;
    day.features[static_cast<size_t>(Feature::ProjectOpened)] = 1;
    auto ctx = context();
    ctx.os = contracts::TelemetryOs::Windows;
    ctx.arch = contracts::TelemetryArch::X64;
    ctx.format = contracts::TelemetryFormat::Vst3;

    EXPECT_EQ(serializeDaily(toDaily(day, ctx)),
              "{\"telemetryId\":\"3f2b8c1e-5a4d-4c6b-9e7f-0a1b2c3d4e5f\",\"day\":\"2026-10-06\","
              "\"appVersion\":\"0.202.0\",\"os\":\"windows\",\"arch\":\"x64\",\"format\":\"vst3\","
              "\"sessions\":3,\"activeMinutesBucket\":\"60-180\","
              "\"moduleCounters\":{\"oscillator\":4,\"ampEnv\":2,\"sampleHold\":1},"
              "\"featureCounters\":{\"macrosCreated\":5,\"aiRequests\":7,\"projectsOpened\":1}}");
}

TEST(TelemetryJsonTest, NeverEmitsNullsOrUnsetGroups) {
    const auto json = serializeDaily(toDaily(minimalDay(), context()));
    EXPECT_FALSE(json.contains("null"));
    EXPECT_FALSE(json.contains("moduleCounters"));
    EXPECT_FALSE(json.contains("featureCounters"));
}

TEST(TelemetryJsonTest, FormatNamesAreLowerCase) {
    auto ctx = context();
    ctx.format = contracts::TelemetryFormat::Au;
    ctx.os = contracts::TelemetryOs::Linux;
    const auto json = serializeDaily(toDaily(minimalDay(), ctx));
    EXPECT_TRUE(json.contains("\"os\":\"linux\""));
    EXPECT_TRUE(json.contains("\"format\":\"au\""));
}

TEST(TelemetryJsonTest, ActiveMinutesBucketEdges) {
    EXPECT_EQ(activeMinutesBucket(0), "<15");
    EXPECT_EQ(activeMinutesBucket(14), "<15");
    EXPECT_EQ(activeMinutesBucket(15), "15-60");
    EXPECT_EQ(activeMinutesBucket(59), "15-60");
    EXPECT_EQ(activeMinutesBucket(60), "60-180");
    EXPECT_EQ(activeMinutesBucket(179), "60-180");
    EXPECT_EQ(activeMinutesBucket(180), "180+");
    EXPECT_EQ(activeMinutesBucket(1440), "180+");
}

TEST(TelemetryJsonTest, CountsAreClampedToTheContractMaximums) {
    auto day = minimalDay();
    day.sessions = 9999;
    day.modules[0] = 99999;
    day.features[0] = 99999;
    const auto daily = toDaily(day, context());
    EXPECT_EQ(daily.sessions, kMaxSessions);
    EXPECT_EQ(daily.moduleCounters->audioInput, kMaxModuleCount);
    EXPECT_EQ(daily.featureCounters->macrosCreated, kMaxFeatureCount);
}

TEST(TelemetryJsonTest, EveryModuleRowMapsToADistinctMember) {
    const auto& table = moduleTable();
    DaySummary day = minimalDay();
    for (auto& count : day.modules)
        count = 1;
    const auto daily = toDaily(day, context());
    for (const auto& entry : table)
        EXPECT_EQ((*daily.moduleCounters).*(entry.member), 1) << entry.factoryName;
    const auto json = serializeDaily(daily);
    for (const auto& entry : table)
        EXPECT_TRUE(json.contains(juce::String("\"") + entry.jsonKey + "\":1")) << entry.jsonKey;
}

TEST(TelemetryJsonTest, QueueRoundTrips) {
    auto day = minimalDay();
    day.activeMinutes = 42;
    day.modules[static_cast<size_t>(moduleIndexForFactoryName("Reverb"))] = 3;
    day.features[static_cast<size_t>(Feature::PresetLoaded)] = 9;
    auto other = minimalDay();
    other.day = "2026-10-05";

    const auto parsed = parseQueue(serializeQueue({day, other}));
    ASSERT_EQ(parsed.size(), 2u);
    EXPECT_EQ(parsed[0].day, "2026-10-06");
    EXPECT_EQ(parsed[0].activeMinutes, 42);
    EXPECT_EQ(parsed[0].modules, day.modules);
    EXPECT_EQ(parsed[0].features, day.features);
    EXPECT_EQ(parsed[1].day, "2026-10-05");
}

TEST(TelemetryJsonTest, ParseSkipsMalformedDaysAndGarbage) {
    EXPECT_TRUE(parseQueue("").empty());
    EXPECT_TRUE(parseQueue("not json").empty());
    EXPECT_TRUE(parseQueue("{\"days\":5}").empty());
    const auto parsed = parseQueue("{\"days\":[{\"day\":\"bad\"},{\"day\":\"2026-10-01\",\"sessions\":99999},3]}");
    ASSERT_EQ(parsed.size(), 1u);
    EXPECT_EQ(parsed[0].sessions, kMaxSessions);
}
