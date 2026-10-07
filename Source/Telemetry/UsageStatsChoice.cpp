#include "UsageStatsChoice.h"
#include "TelemetryIdStore.h"
#include "TelemetryRecorder.h"
#include "UserSettings.h"

namespace synth::telemetry {

void applyShareUsageStatsChoice(juce::PropertiesFile& settings, bool share) {
    settings.setValue(synth::kShareUsageStatsSettingKey, share);
    settings.setValue(synth::kUsageStatsAskedSettingKey, true);
    settings.saveIfNeeded();
    TelemetryIdStore idStore;
    if (share) {
        if (idStore.load().isEmpty())
            idStore.create();
    } else {
        idStore.erase();
        TelemetryRecorder::defaultQueueFile().deleteFile();
    }
}

bool shouldAskAboutUsageStats(const juce::PropertiesFile* settings) {
    return settings != nullptr && !settings->getBoolValue(synth::kUsageStatsAskedSettingKey, false) &&
           !settings->getBoolValue(synth::kShareUsageStatsSettingKey, false);
}

} // namespace synth::telemetry
