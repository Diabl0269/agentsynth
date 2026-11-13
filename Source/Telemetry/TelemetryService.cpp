#include "TelemetryService.h"

// Concern: when counting runs (a minute timer), when the queue is saved and when the launch-time send fires.

namespace synth::telemetry {

class TelemetryService::DelayedSend : private juce::Timer {
public:
    explicit DelayedSend(std::function<void()> action)
        : fire(std::move(action)) {}
    ~DelayedSend() override { stopTimer(); }
    void schedule(int delayMs) { startTimer(delayMs); }

private:
    void timerCallback() override {
        stopTimer();
        fire();
    }
    std::function<void()> fire;
};

TelemetryService::TelemetryService(juce::String appVersion, platform::contracts::TelemetryFormat format)
    : TelemetryService(std::make_unique<TelemetryRecorder>(TelemetryIdStore(), TelemetryRecorder::defaultQueueFile(),
                                                           nullptr, std::move(appVersion), format),
                       std::make_unique<TelemetrySender>()) {}

TelemetryService::TelemetryService(std::unique_ptr<TelemetryRecorder> recorderIn,
                                   std::unique_ptr<TelemetrySender> senderIn)
    : recorder(std::move(recorderIn))
    , sender(std::move(senderIn))
    , delayedSend(std::make_unique<DelayedSend>([this] { sendPending(); })) {}

TelemetryService::~TelemetryService() {
    delayedSend.reset();
    stopTimer();
    recorder->flush();
}

/** A launch with the setting off removes any id or queue a previous opt-in left behind (a plugin build, which
    never runs this service, can switch the shared setting off), so "off" always means no file on disk. */
void TelemetryService::start(bool shareUsageStats) {
    if (!shareUsageStats) {
        recorder->purge();
        return;
    }
    applySetting(true);
    delayedSend->schedule(kSendDelayMs);
}

void TelemetryService::applySetting(bool shareUsageStats) {
    if (shareUsageStats == recorder->isEnabled())
        return;
    if (!shareUsageStats) {
        stopTimer();
        recorder->setEnabled(false);
        return;
    }
    recorder->setEnabled(true);
    recorder->noteSessionStart();
    recorder->flush();
    startTimer(kMinuteMs);
}

void TelemetryService::sendPending() { sender->sendPending(*recorder); }

void TelemetryService::minuteElapsed(bool appIsForeground) {
    if (appIsForeground)
        recorder->addActiveMinutes(1);
    recorder->flush();
}

void TelemetryService::timerCallback() { minuteElapsed(juce::Process::isForegroundProcess()); }

void TelemetryService::countModuleAdded(const juce::String& factoryTypeName) {
    recorder->countModuleAdded(factoryTypeName);
}

void TelemetryService::countFeature(Feature feature) { recorder->countFeature(feature); }

} // namespace synth::telemetry
