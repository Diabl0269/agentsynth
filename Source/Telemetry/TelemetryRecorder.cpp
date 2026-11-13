#include "TelemetryRecorder.h"
#include "UserSettings.h"
#include <algorithm>
#include <regex>

// Concern: the counters, the day rollover, the 14-day queue and its file. The sender (TelemetrySender.cpp)
// drains the queue; the service (TelemetryService.cpp) decides when to flush.

namespace synth::telemetry {

namespace {

platform::contracts::TelemetryOs currentOs() {
#if JUCE_MAC
    return platform::contracts::TelemetryOs::Macos;
#elif JUCE_WINDOWS
    return platform::contracts::TelemetryOs::Windows;
#else
    return platform::contracts::TelemetryOs::Linux;
#endif
}

platform::contracts::TelemetryArch currentArch() {
#if JUCE_ARM || defined(__aarch64__) || defined(_M_ARM64)
    return platform::contracts::TelemetryArch::Arm64;
#else
    return platform::contracts::TelemetryArch::X64;
#endif
}

// The server accepts only a dotted version (1.2.3, optionally -beta.1); a build label must never leave the app.
juce::String sanitisedVersion(const juce::String& version) {
    static const std::regex pattern(R"(^\d{1,4}\.\d{1,4}\.\d{1,4}(-[a-zA-Z0-9.]{1,32})?$)");
    return std::regex_match(version.toStdString(), pattern) ? version : juce::String("0.0.0");
}

int addCapped(int current, int amount, int maximum) { return std::min(maximum, current + std::max(0, amount)); }

} // namespace

TelemetryRecorder::TelemetryRecorder()
    : TelemetryRecorder(TelemetryIdStore(), defaultQueueFile(), nullptr, "0.0.0",
                        platform::contracts::TelemetryFormat::Standalone) {}

TelemetryRecorder::TelemetryRecorder(TelemetryIdStore idStoreIn, juce::File queueFileIn, Clock clockIn,
                                     juce::String appVersionIn, platform::contracts::TelemetryFormat formatIn)
    : idStore(std::move(idStoreIn))
    , queueFile(std::move(queueFileIn))
    , clock(clockIn ? std::move(clockIn) : Clock([] { return juce::Time::getCurrentTime(); }))
    , appVersion(sanitisedVersion(appVersionIn))
    , format(formatIn) {}

juce::File TelemetryRecorder::defaultQueueFile() {
    return synth::userSettingsRootDirectory().getChildFile("telemetry_queue.json");
}

/** Switching on creates the id only when none exists, so a relaunch keeps filing under the same id. Switching
    off is the opt-out promise: the id, the queue file and the in-memory counts are all gone before it returns. */
void TelemetryRecorder::setEnabled(bool wantEnabled) {
    if (wantEnabled == enabled)
        return;
    enabled = wantEnabled;
    optInGeneration->fetch_add(1);
    if (enabled) {
        telemetryId = idStore.load();
        if (telemetryId.isEmpty())
            telemetryId = idStore.create();
        loadQueueFile();
        return;
    }
    purge();
}

void TelemetryRecorder::purge() {
    optInGeneration->fetch_add(1);
    enabled = false;
    clearAll();
    idStore.erase();
    queueFile.deleteFile();
}

juce::String TelemetryRecorder::todayString() const {
    const auto now = clock();
    return juce::String::formatted("%04d-%02d-%02d", now.getYear(), now.getMonth() + 1, now.getDayOfMonth());
}

void TelemetryRecorder::trimQueue() {
    while (queue.size() > kMaxQueuedDays)
        queue.erase(queue.begin());
}

/** The local date changed since the last count: yesterday's record joins the queue and is written at once, so
    a crash right after midnight cannot lose it. */
void TelemetryRecorder::rollOverIfNeeded() {
    if (!today || today->day == todayString())
        return;
    queue.push_back(*today);
    today.reset();
    trimQueue();
    dirty = true;
    flush();
}

DaySummary* TelemetryRecorder::currentDay() {
    rollOverIfNeeded();
    if (!enabled) // the rollover's write found the user had opted out
        return nullptr;
    if (!today) {
        today = DaySummary{};
        today->day = todayString();
    }
    dirty = true;
    return &*today;
}

void TelemetryRecorder::noteSessionStart() {
    if (!enabled)
        return;
    JUCE_ASSERT_MESSAGE_THREAD
    if (auto* day = currentDay())
        day->sessions = addCapped(day->sessions, 1, kMaxSessions);
}

void TelemetryRecorder::addActiveMinutes(int minutes) {
    if (!enabled)
        return;
    JUCE_ASSERT_MESSAGE_THREAD
    if (auto* day = currentDay())
        day->activeMinutes = addCapped(day->activeMinutes, minutes, kMaxActiveMinutes);
}

void TelemetryRecorder::countModuleAdded(const juce::String& factoryTypeName) {
    if (!enabled)
        return;
    JUCE_ASSERT_MESSAGE_THREAD
    const int index = moduleIndexForFactoryName(factoryTypeName);
    if (index < 0)
        return;
    if (auto* day = currentDay()) {
        auto& count = day->modules[static_cast<std::size_t>(index)];
        count = addCapped(count, 1, kMaxModuleCount);
    }
}

void TelemetryRecorder::countFeature(Feature feature) {
    if (!enabled)
        return;
    JUCE_ASSERT_MESSAGE_THREAD
    if (auto* day = currentDay()) {
        auto& count = day->features[static_cast<std::size_t>(feature)];
        count = addCapped(count, 1, kMaxFeatureCount);
    }
}

/** The Preferences tab deletes the id and the queue file itself the moment the toggle goes off, which can be a
    message-loop pass before this recorder hears about it; a write in that gap would bring the queue back. */
bool TelemetryRecorder::optedOutElsewhere() const { return idStore.load() != telemetryId; }

void TelemetryRecorder::flush() {
    if (!enabled || !dirty)
        return;
    if (optedOutElsewhere()) {
        optInGeneration->fetch_add(1);
        clearAll();
        enabled = false;
        queueFile.deleteFile();
        return;
    }
    std::vector<DaySummary> all = queue;
    if (today)
        all.push_back(*today);
    const auto parent = queueFile.getParentDirectory();
    if (!parent.exists())
        parent.createDirectory();
    queueFile.replaceWithText(serializeQueue(all));
    dirty = false;
}

void TelemetryRecorder::clearAll() {
    telemetryId = {};
    today.reset();
    queue.clear();
    dirty = false;
}

/** A saved day equal to today resumes as today's record (one summary per day, however many launches); every other
    saved day is unsent history. */
void TelemetryRecorder::loadQueueFile() {
    today.reset();
    queue.clear();
    if (!queueFile.existsAsFile())
        return;
    const auto todayText = todayString();
    for (auto& day : parseQueue(queueFile.loadFileAsString())) {
        if (day.day == todayText)
            today = day;
        else
            queue.push_back(day);
    }
    std::stable_sort(queue.begin(), queue.end(),
                     [](const DaySummary& a, const DaySummary& b) { return a.day < b.day; });
    trimQueue();
}

std::vector<DaySummary> TelemetryRecorder::getPendingDays() {
    if (!enabled)
        return {};
    rollOverIfNeeded();
    const auto todayText = todayString();
    std::vector<DaySummary> pending;
    for (const auto& day : queue)
        if (day.day < todayText)
            pending.push_back(day);
    return pending;
}

DailyContext TelemetryRecorder::makeContext() const {
    DailyContext context;
    context.telemetryId = telemetryId;
    context.appVersion = appVersion;
    context.os = currentOs();
    context.arch = currentArch();
    context.format = format;
    return context;
}

void TelemetryRecorder::removeSentDay(const juce::String& day, const juce::String& idAtSend) {
    if (!enabled || telemetryId != idAtSend)
        return;
    queue.erase(std::remove_if(queue.begin(), queue.end(), [&](const DaySummary& d) { return d.day == day; }),
                queue.end());
    dirty = true;
    flush();
}

} // namespace synth::telemetry
