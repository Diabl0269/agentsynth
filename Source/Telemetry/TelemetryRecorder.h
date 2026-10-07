#pragma once

#include "TelemetryDay.h"
#include "TelemetryIdStore.h"
#include "TelemetryJson.h"
#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace synth::telemetry {

/**
 * @class TelemetryRecorder
 * @brief Today's usage counts plus the days not yet sent, and the opt-in switch that guards both.
 *
 * Every counting call is a no-op while disabled, and disabling deletes the id, the queue file and
 * everything held in memory. Counting is message-thread only; nothing here is touched from the audio
 * thread. Docs: docs/development/usage-statistics.md.
 */
class TelemetryRecorder {
public:
    using Clock = std::function<juce::Time()>;

    /** The most unsent days kept; the oldest is dropped past this. */
    static constexpr std::size_t kMaxQueuedDays = 14;

    /** Production: ids and queue under synth::userSettingsRootDirectory(), the real clock, version 0.0.0. */
    TelemetryRecorder();

    /** `clock` null means the system clock. `appVersion` must look like 1.2.3 (anything else becomes 0.0.0). */
    TelemetryRecorder(TelemetryIdStore idStore, juce::File queueFile, Clock clock, juce::String appVersion,
                      platform::contracts::TelemetryFormat format);

    /** Where the queue lives by default: `telemetry_queue.json` under synth::userSettingsRootDirectory(). */
    static juce::File defaultQueueFile();

    /** On: makes sure an id exists and loads any saved days. Off: deletes the id, the queue file and the counts. */
    void setEnabled(bool enabled);
    bool isEnabled() const noexcept { return enabled; }

    /** The id summaries are filed under; empty while disabled. */
    juce::String getTelemetryId() const { return telemetryId; }

    void noteSessionStart();
    void addActiveMinutes(int minutes);
    /** Counts a module added from the library; a type with no counter in the contract is ignored. */
    void countModuleAdded(const juce::String& factoryTypeName);
    void countFeature(Feature feature);

    /** Deletes the id, the queue file and the counts without enabling anything: the off state, made certain. */
    void purge();

    /** Writes the queue file when something changed since the last write. */
    void flush();

    /** Days strictly before today, oldest first, each with its static fields ready to serialize. */
    std::vector<DaySummary> getPendingDays();

    /** A counter that changes whenever the opt-in state does (on, off, or erased elsewhere). A background send
        reads it before each request, so a request never goes out after the user opted out. */
    std::shared_ptr<const std::atomic<int>> getOptInGeneration() const { return optInGeneration; }

    /** The context (id, version, platform, format) a summary is serialized with. */
    DailyContext makeContext() const;

    /** Drops a sent day from the queue, unless the user opted out (or the id changed) since the send began. */
    void removeSentDay(const juce::String& day, const juce::String& idAtSend);

    // Read-only views for the owner's tests.
    const std::optional<DaySummary>& getToday() const noexcept { return today; }
    const std::vector<DaySummary>& getQueue() const noexcept { return queue; }

private:
    TelemetryIdStore idStore;
    juce::File queueFile;
    Clock clock;
    juce::String appVersion;
    platform::contracts::TelemetryFormat format;

    bool enabled = false;
    std::shared_ptr<std::atomic<int>> optInGeneration = std::make_shared<std::atomic<int>>(0);
    bool dirty = false;
    juce::String telemetryId;
    std::optional<DaySummary> today;
    std::vector<DaySummary> queue;

    juce::String todayString() const;
    DaySummary* currentDay();
    void rollOverIfNeeded();
    void trimQueue();
    void loadQueueFile();
    void clearAll();
    bool optedOutElsewhere() const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TelemetryRecorder)
};

} // namespace synth::telemetry
