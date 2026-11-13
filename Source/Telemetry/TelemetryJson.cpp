#include "TelemetryJson.h"
#include <algorithm>

// Concern: turning a day's counts into the request body, and into the queue file and back. The body is
// written by hand so its keys are exactly the contract's: the server rejects any key it does not list.

namespace synth::telemetry {

namespace {

using platform::contracts::TelemetryArch;
using platform::contracts::TelemetryFormat;
using platform::contracts::TelemetryOs;

constexpr const char* kFeatureKeys[kFeatureCount] = {"macrosCreated", "timelineUsed",  "aiRequests",
                                                     "presetsSaved",  "presetsLoaded", "projectsOpened"};

constexpr std::optional<int> platform::contracts::TelemetryFeatureCounters::* kFeatureMembers[kFeatureCount] = {
    &platform::contracts::TelemetryFeatureCounters::macrosCreated,
    &platform::contracts::TelemetryFeatureCounters::timelineUsed,
    &platform::contracts::TelemetryFeatureCounters::aiRequests,
    &platform::contracts::TelemetryFeatureCounters::presetsSaved,
    &platform::contracts::TelemetryFeatureCounters::presetsLoaded,
    &platform::contracts::TelemetryFeatureCounters::projectsOpened};

const char* osName(TelemetryOs os) {
    switch (os) {
    case TelemetryOs::Windows:
        return "windows";
    case TelemetryOs::Macos:
        return "macos";
    case TelemetryOs::Linux:
        return "linux";
    }
    return "linux";
}

const char* archName(TelemetryArch arch) { return arch == TelemetryArch::Arm64 ? "arm64" : "x64"; }

const char* formatName(TelemetryFormat format) {
    switch (format) {
    case TelemetryFormat::Standalone:
        return "standalone";
    case TelemetryFormat::Vst3:
        return "vst3";
    case TelemetryFormat::Au:
        return "au";
    }
    return "standalone";
}

juce::String jsonString(const juce::String& text) { return juce::JSON::toString(juce::var(text)); }

juce::String field(const char* key, const juce::String& rawValue) {
    return jsonString(juce::String(key)) + ":" + rawValue;
}

int clampCount(int value, int maximum) { return std::clamp(value, 0, maximum); }

} // namespace

platform::contracts::TelemetryDaily toDaily(const DaySummary& day, const DailyContext& context) {
    platform::contracts::TelemetryDaily daily;
    daily.telemetryId = context.telemetryId.toStdString();
    daily.day = day.day.toStdString();
    daily.appVersion = context.appVersion.toStdString();
    daily.os = context.os;
    daily.arch = context.arch;
    daily.format = context.format;
    daily.sessions = clampCount(day.sessions, kMaxSessions);
    daily.activeMinutesBucket = activeMinutesBucket(day.activeMinutes).toStdString();

    platform::contracts::TelemetryModuleCounters modules;
    bool anyModule = false;
    const auto& table = moduleTable();
    for (std::size_t i = 0; i < table.size(); ++i) {
        if (day.modules[i] <= 0)
            continue;
        modules.*(table[i].member) = clampCount(day.modules[i], kMaxModuleCount);
        anyModule = true;
    }
    if (anyModule)
        daily.moduleCounters = modules;

    platform::contracts::TelemetryFeatureCounters features;
    bool anyFeature = false;
    for (std::size_t i = 0; i < kFeatureCount; ++i) {
        if (day.features[i] <= 0)
            continue;
        features.*(kFeatureMembers[i]) = clampCount(day.features[i], kMaxFeatureCount);
        anyFeature = true;
    }
    if (anyFeature)
        daily.featureCounters = features;
    return daily;
}

juce::String serializeDaily(const platform::contracts::TelemetryDaily& daily) {
    juce::StringArray parts;
    parts.add(field("telemetryId", jsonString(daily.telemetryId)));
    parts.add(field("day", jsonString(daily.day)));
    parts.add(field("appVersion", jsonString(daily.appVersion)));
    parts.add(field("os", jsonString(osName(daily.os))));
    parts.add(field("arch", jsonString(archName(daily.arch))));
    parts.add(field("format", jsonString(formatName(daily.format))));
    parts.add(field("sessions", juce::String(daily.sessions)));
    parts.add(field("activeMinutesBucket", jsonString(daily.activeMinutesBucket)));

    if (daily.moduleCounters) {
        juce::StringArray set;
        for (const auto& entry : moduleTable())
            if (const auto& value = (*daily.moduleCounters).*(entry.member))
                set.add(field(entry.jsonKey, juce::String(*value)));
        parts.add(field("moduleCounters", "{" + set.joinIntoString(",") + "}"));
    }
    if (daily.featureCounters) {
        juce::StringArray set;
        for (std::size_t i = 0; i < kFeatureCount; ++i)
            if (const auto& value = (*daily.featureCounters).*(kFeatureMembers[i]))
                set.add(field(kFeatureKeys[i], juce::String(*value)));
        parts.add(field("featureCounters", "{" + set.joinIntoString(",") + "}"));
    }
    return "{" + parts.joinIntoString(",") + "}";
}

juce::String serializeQueue(const std::vector<DaySummary>& days) {
    juce::Array<juce::var> list;
    for (const auto& day : days) {
        auto* object = new juce::DynamicObject();
        object->setProperty("day", day.day);
        object->setProperty("sessions", day.sessions);
        object->setProperty("minutes", day.activeMinutes);
        auto* modules = new juce::DynamicObject();
        const auto& table = moduleTable();
        for (std::size_t i = 0; i < table.size(); ++i)
            if (day.modules[i] > 0)
                modules->setProperty(juce::Identifier(table[i].jsonKey), day.modules[i]);
        object->setProperty("modules", juce::var(modules));
        auto* features = new juce::DynamicObject();
        for (std::size_t i = 0; i < kFeatureCount; ++i)
            if (day.features[i] > 0)
                features->setProperty(juce::Identifier(kFeatureKeys[i]), day.features[i]);
        object->setProperty("features", juce::var(features));
        list.add(juce::var(object));
    }
    auto* root = new juce::DynamicObject();
    root->setProperty("days", juce::var(list));
    return juce::JSON::toString(juce::var(root), true);
}

namespace {

bool looksLikeDay(const juce::String& day) {
    if (day.length() != 10)
        return false;
    for (int i = 0; i < 10; ++i) {
        const bool dash = i == 4 || i == 7;
        if (dash ? day[i] != '-' : !juce::CharacterFunctions::isDigit(day[i]))
            return false;
    }
    return true;
}

int readCount(const juce::var& value, int maximum) {
    return value.isInt() || value.isInt64() || value.isDouble() ? clampCount(static_cast<int>(value), maximum) : 0;
}

} // namespace

std::vector<DaySummary> parseQueue(const juce::String& text) {
    std::vector<DaySummary> out;
    const auto root = juce::JSON::parse(text);
    const auto* list = root.getProperty("days", {}).getArray();
    if (list == nullptr)
        return out;
    for (const auto& item : *list) {
        const auto day = item.getProperty("day", {}).toString();
        if (!item.isObject() || !looksLikeDay(day))
            continue;
        DaySummary summary;
        summary.day = day;
        summary.sessions = readCount(item.getProperty("sessions", {}), kMaxSessions);
        summary.activeMinutes = readCount(item.getProperty("minutes", {}), kMaxActiveMinutes);
        const auto modules = item.getProperty("modules", {});
        const auto& table = moduleTable();
        for (std::size_t i = 0; i < table.size(); ++i)
            summary.modules[i] =
                readCount(modules.getProperty(juce::Identifier(table[i].jsonKey), {}), kMaxModuleCount);
        const auto features = item.getProperty("features", {});
        for (std::size_t i = 0; i < kFeatureCount; ++i)
            summary.features[i] =
                readCount(features.getProperty(juce::Identifier(kFeatureKeys[i]), {}), kMaxFeatureCount);
        out.push_back(summary);
    }
    return out;
}

} // namespace synth::telemetry
