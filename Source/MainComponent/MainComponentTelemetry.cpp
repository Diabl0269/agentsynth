// MainComponentTelemetry.cpp -- opt-in anonymous usage statistics: the service's creation and the
// settings-driven on/off switch. The counters themselves are called from the user actions that feed them.
// Docs: docs/development/usage-statistics.md.
#include "AudioEngine/AudioEngine.h"
#include "MainComponent.h"
#include "Telemetry/TelemetryService.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"

namespace {

bool shareUsageStatsSettingIsOn(const juce::PropertiesFile* settings) {
    return settings != nullptr && settings->getBoolValue(synth::kShareUsageStatsSettingKey, false);
}

// The running application's version when there is one (a test host has none): the server accepts only a dotted
// version, so anything else is replaced by the recorder.
juce::String runningAppVersion() {
    if (auto* app = juce::JUCEApplicationBase::getInstance())
        return app->getApplicationVersion();
    return "0.0.0";
}

} // namespace

// Standalone only. A plugin editor is opened and closed at the host's whim and several instances can share one
// queue file, so counting there would give one "session" per editor open; the plugin build records nothing and
// the setting simply has no effect there.
void MainComponent::startTelemetry() {
    if (ownedAudioEngine == nullptr)
        return;
    telemetry_ = std::make_unique<synth::telemetry::TelemetryService>(runningAppVersion(),
                                                                      platform::contracts::TelemetryFormat::Standalone);
    auto* service = telemetry_.get();
    graphEditor.onModuleAdded = [service](const juce::String& typeName) { service->countModuleAdded(typeName); };
    graphEditor.getMacroController().onMacroCreated = [service] {
        service->countFeature(synth::telemetry::Feature::MacroCreated);
    };
    aiChatComponent.onMessageSent = [service] { service->countFeature(synth::telemetry::Feature::AiRequest); };
    telemetry_->start(shareUsageStatsSettingIsOn(appProperties.getUserSettings()));
}

void MainComponent::applyTelemetryPreference() {
    if (telemetry_ != nullptr)
        telemetry_->applySetting(shareUsageStatsSettingIsOn(appProperties.getUserSettings()));
}

void MainComponent::countUsage(synth::telemetry::Feature feature) {
    if (telemetry_ != nullptr)
        telemetry_->countFeature(feature);
}
