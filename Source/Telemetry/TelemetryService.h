#pragma once

#include "TelemetryRecorder.h"
#include "TelemetrySender.h"
#include <juce_events/juce_events.h>
#include <memory>

namespace synth::telemetry {

/**
 * @class TelemetryService
 * @brief The owner of usage statistics for one running app: recorder, sender and the minute timer.
 *
 * Created by MainComponent for the standalone app only. The setting `shareUsageStats` is the one source of
 * truth for "on"; the owner calls applySetting() at start and again whenever the settings file changes.
 * Docs: docs/development/usage-statistics.md.
 */
class TelemetryService : private juce::Timer {
public:
    /** Production wiring: real files, clock and network. `appVersion` should look like 1.2.3. */
    TelemetryService(juce::String appVersion, platform::contracts::TelemetryFormat format);

    /** Test wiring: the caller supplies the recorder and sender. */
    TelemetryService(std::unique_ptr<TelemetryRecorder> recorder, std::unique_ptr<TelemetrySender> sender);

    /** Flushes the queue file. */
    ~TelemetryService() override;

    /** Message thread only, once at launch: counts the session and schedules the send when the setting is on;
        when it is off, makes sure no id or queue file is left on disk. */
    void start(bool shareUsageStats);

    /** Message thread only. Brings the recorder in line with the setting; switching on counts the session. */
    void applySetting(bool shareUsageStats);

    /** Message thread only. Sends the days before today (fire and forget); see TelemetrySender. */
    void sendPending();

    /** Message thread only. One foreground minute passed: counts it (when the app is frontmost) and saves. */
    void minuteElapsed(bool appIsForeground);

    /** Message thread only; both are no-ops while usage statistics are off. */
    void countModuleAdded(const juce::String& factoryTypeName);
    void countFeature(Feature feature);

    TelemetryRecorder& getRecorder() noexcept { return *recorder; }

    /** Waits this long after launch before sending, so the send never competes with start-up work. */
    static constexpr int kSendDelayMs = 8000;
    static constexpr int kMinuteMs = 60 * 1000;

private:
    void timerCallback() override;

    class DelayedSend;

    std::unique_ptr<TelemetryRecorder> recorder;
    std::unique_ptr<TelemetrySender> sender; // after recorder: destroyed first, so its in-flight results go inert
    std::unique_ptr<DelayedSend> delayedSend;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TelemetryService)
};

} // namespace synth::telemetry
