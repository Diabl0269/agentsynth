#pragma once

#include "Telemetry/TelemetryRecorder.h"
#include <gtest/gtest.h>
#include <juce_core/juce_core.h>

namespace synth::telemetry::test {

// A temp directory per test with explicit id and queue files, and a clock the test moves by hand, so
// nothing here touches the real settings folder or depends on today's date.
class TelemetryTestFixture : public ::testing::Test {
protected:
    void SetUp() override {
        dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                  .getChildFile("TelemetryTest")
                  .getNonexistentChildFile("run", "", false);
        dir.createDirectory();
        setDay(2026, 10, 7);
    }

    void TearDown() override { dir.deleteRecursively(); }

    juce::File idFile() const { return dir.getChildFile("telemetry_id"); }
    juce::File queueFile() const { return dir.getChildFile("telemetry_queue.json"); }

    // Months are 1-based here.
    void setDay(int year, int month, int day) { now = juce::Time(year, month - 1, day, 12, 0, 0, 0, true); }

    TelemetryRecorder makeRecorder(juce::String version = "1.2.3") {
        return TelemetryRecorder(
            TelemetryIdStore(idFile()), queueFile(), [this] { return now; }, std::move(version),
            platform::contracts::TelemetryFormat::Standalone);
    }

    std::unique_ptr<TelemetryRecorder> makeRecorderPtr(juce::String version = "1.2.3") {
        return std::make_unique<TelemetryRecorder>(
            TelemetryIdStore(idFile()), queueFile(), [this] { return now; }, std::move(version),
            platform::contracts::TelemetryFormat::Standalone);
    }

    juce::File dir;
    juce::Time now;
};

} // namespace synth::telemetry::test
