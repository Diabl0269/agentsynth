#pragma once

#include "TelemetryDay.h"
#include <juce_core/juce_core.h>
#include <optional>
#include <vector>

namespace synth::telemetry {

/** What goes into the static fields of every summary. */
struct DailyContext {
    juce::String telemetryId;
    juce::String appVersion;
    platform::contracts::TelemetryOs os = platform::contracts::TelemetryOs::Macos;
    platform::contracts::TelemetryArch arch = platform::contracts::TelemetryArch::Arm64;
    platform::contracts::TelemetryFormat format = platform::contracts::TelemetryFormat::Standalone;
};

/** The contract struct for one day: only counters above zero are set, and an empty group is left unset. */
platform::contracts::TelemetryDaily toDaily(const DaySummary& day, const DailyContext& context);

/** The request body: compact JSON holding exactly the keys the server's strict schema allows. */
juce::String serializeDaily(const platform::contracts::TelemetryDaily& daily);

/** The unsent days as the queue file stores them. */
juce::String serializeQueue(const std::vector<DaySummary>& days);

/** Reads a queue file's text. Days that are malformed, or whose counters are out of range, are skipped. */
std::vector<DaySummary> parseQueue(const juce::String& text);

} // namespace synth::telemetry
