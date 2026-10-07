#pragma once

#include "generated/Telemetry.g.h"
#include <array>
#include <cstddef>
#include <juce_core/juce_core.h>

namespace synth::telemetry {

/** The features usage statistics count (one counter each in the daily summary). */
enum class Feature { MacroCreated, TimelineUsed, AiRequest, PresetSaved, PresetLoaded, ProjectOpened };
inline constexpr std::size_t kFeatureCount = 6;

/** One counter per module type in the contract's module counters. */
inline constexpr std::size_t kModuleCount = 48;

/** Counter ceilings, equal to the bounds the server enforces on a summary. */
inline constexpr int kMaxSessions = 500;
inline constexpr int kMaxModuleCount = 2000;
inline constexpr int kMaxFeatureCount = 5000;

/** Longest stretch of foreground time one day can hold. */
inline constexpr int kMaxActiveMinutes = 24 * 60;

/** Everything recorded for one local day: counts only, nothing a person typed or chose. */
struct DaySummary {
    juce::String day; // local calendar day, YYYY-MM-DD
    int sessions = 0;
    int activeMinutes = 0;
    std::array<int, kModuleCount> modules{};
    std::array<int, kFeatureCount> features{};
};

/** One row of the module mapping: the factory type name the module library uses, the contract's key for
    it, and the contract struct member it fills. */
struct ModuleEntry {
    const char* factoryName;
    const char* jsonKey;
    std::optional<int> platform::contracts::TelemetryModuleCounters::* member;
};

/** The mapping table, kModuleCount rows. Add a row here (and in the contract) when a module type is added. */
const std::array<ModuleEntry, kModuleCount>& moduleTable();

/** Index of the module counter for a factory type name such as "Amp Env", or -1 when the name has none. */
int moduleIndexForFactoryName(const juce::String& factoryTypeName);

/** One of "<15", "15-60", "60-180" or "180+". */
juce::String activeMinutesBucket(int minutes);

} // namespace synth::telemetry
