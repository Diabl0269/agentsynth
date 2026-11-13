#pragma once

#include <juce_data_structures/juce_data_structures.h>

namespace synth::telemetry {

/** Where "What we collect" points: the usage statistics section of the privacy page. */
inline constexpr const char* kUsageStatsPrivacyUrl = "https://agentsynth.app/privacy#telemetry";

/**
 * The ONE place the opt-in is written, used by the Preferences toggle and by the Welcome screen's card.
 * Stores synth::kShareUsageStatsSettingKey = `share` and synth::kUsageStatsAskedSettingKey = true (an answer
 * from either place means the Welcome screen never asks again), saves the file, then does the file work: on,
 * creates the usage statistics id when there is none; off, deletes the id and the unsent queue. The running
 * TelemetryService follows through the settings change the write broadcasts.
 */
void applyShareUsageStatsChoice(juce::PropertiesFile& settings, bool share);

/** True while the Welcome screen should still ask: no answer given yet and not already sharing. */
bool shouldAskAboutUsageStats(const juce::PropertiesFile* settings);

} // namespace synth::telemetry
