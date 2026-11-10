#pragma once

#include <juce_core/juce_core.h>
#include <optional>

namespace synth {

/** A plugin binary a crash report points at. */
struct CrashedPlugin {
    juce::String name;       ///< The bundle's file name without its extension, e.g. "Some Synth".
    juce::String bundlePath; ///< The bundle's full path. In memory only: never persisted, never shown.
    bool isAudioUnit = false;

    bool operator==(const CrashedPlugin& other) const noexcept {
        return name == other.name && bundlePath == other.bundlePath && isAudioUnit == other.isAudioUnit;
    }
};

/**
 * Names the plugin a macOS crash report (`Agent Synth-<date>.ips`) blames: a pure unit, report text
 * in, plugin out, no file or settings access.
 *
 * The report is two JSON documents, a one-line header and then the body. The body's `faultingThread`
 * indexes `threads`; each frame's `imageIndex` indexes `usedImages`, whose `path` is the binary the
 * frame ran in. The first frame (the crashing one first, callers last) whose image sits inside a
 * `.vst3` or `.component` bundle is the plugin. When the faulting thread has no such frame (our own
 * bug, a system library) the answer is nullopt, and the caller says nothing.
 */
namespace CrashReportPluginFinder {

/** `reportText` is the whole file. Tolerates a missing header line. */
std::optional<CrashedPlugin> find(const juce::String& reportText);

/** The plugin an image path belongs to: the path up to and including its `.vst3` / `.component`
 *  component. nullopt for any other path. */
std::optional<CrashedPlugin> pluginFromImagePath(const juce::String& imagePath);

} // namespace CrashReportPluginFinder

} // namespace synth
