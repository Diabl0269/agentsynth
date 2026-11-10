#include "CrashReportPluginFinder.h"

namespace synth::CrashReportPluginFinder {

namespace {

/** The body JSON: the first object that has a "threads" list. The header line comes before it, and a report with
 *  no header is the body alone. */
juce::var parseReport(const juce::String& text) {
    const auto trimmed = text.trimStart();
    for (int from = 0; from >= 0; from = trimmed.indexOf(from + 1, "\n{")) {
        const auto start = from == 0 ? 0 : from + 1;
        auto candidate = juce::JSON::parse(trimmed.substring(start));
        if (candidate.isObject() && candidate.hasProperty("threads"))
            return candidate;
    }
    return {};
}

/** End index (exclusive) of the first path component ending in `extension` (".vst3"), or -1. */
int bundleEnd(const juce::String& path, const juce::String& extension) {
    for (int from = 0;;) {
        const int at = path.indexOfIgnoreCase(from, extension);
        if (at < 0)
            return -1;
        const int end = at + extension.length();
        if (end == path.length() || path[end] == '/' || path[end] == '\\')
            return end;
        from = at + 1;
    }
}

bool isWholeNumber(const juce::var& value) { return value.isInt() || value.isInt64(); }

} // namespace

std::optional<CrashedPlugin> pluginFromImagePath(const juce::String& imagePath) {
    for (const bool audioUnit : {false, true}) {
        const int end = bundleEnd(imagePath, audioUnit ? ".component" : ".vst3");
        if (end < 0)
            continue;

        CrashedPlugin plugin;
        plugin.bundlePath = imagePath.substring(0, end);
        plugin.isAudioUnit = audioUnit;
        const auto fileName = plugin.bundlePath.fromLastOccurrenceOf("/", false, false);
        plugin.name = fileName.upToLastOccurrenceOf(".", false, false);
        if (plugin.name.isEmpty())
            return std::nullopt;
        return plugin;
    }
    return std::nullopt;
}

std::optional<CrashedPlugin> find(const juce::String& reportText) {
    const auto report = parseReport(reportText);
    const auto* threads = report.getProperty("threads", juce::var()).getArray();
    const auto* images = report.getProperty("usedImages", juce::var()).getArray();
    const auto& faulting = report.getProperty("faultingThread", juce::var());
    if (threads == nullptr || images == nullptr || !isWholeNumber(faulting) || static_cast<int>(faulting) < 0 ||
        static_cast<int>(faulting) >= threads->size())
        return std::nullopt;

    const auto* frames = (*threads)[static_cast<int>(faulting)].getProperty("frames", juce::var()).getArray();
    if (frames == nullptr)
        return std::nullopt;

    for (const auto& frame : *frames) {
        const auto& index = frame.getProperty("imageIndex", juce::var());
        if (!isWholeNumber(index) || static_cast<int>(index) < 0 || static_cast<int>(index) >= images->size())
            continue;
        const auto path = (*images)[static_cast<int>(index)].getProperty("path", juce::var()).toString();
        if (auto plugin = pluginFromImagePath(path))
            return plugin;
    }
    return std::nullopt;
}

} // namespace synth::CrashReportPluginFinder
