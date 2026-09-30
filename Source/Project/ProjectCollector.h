#pragma once

#include <functional>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>
#include <map>
#include <vector>

namespace synth {

/** One file to copy into the bundle: the external source and the bundle directory it lands in. */
struct CollectCopyJob {
    juce::File source;
    juce::File destDir;
};

/** What Collect will copy, worked out on the message thread from the live graph. */
struct CollectPlan {
    std::vector<CollectCopyJob> copies;
    /** Wavetable folders to point at their copy once their files are in: source -> dest. */
    std::vector<std::pair<juce::File, juce::File>> folders;
    /** Paths a module refers to that are not on disk; left untouched. */
    juce::StringArray missing;
};

/** What the copy step did: every old absolute path mapped to its copy, and what failed. */
struct CollectResult {
    std::map<juce::String, juce::String> rewrites;
    juce::StringArray failures;
    /** The plan's missing paths, carried along so one result describes the whole run. */
    juce::StringArray missing;
    int filesCopied = 0;
    bool cancelled = false;
};

/**
 * @class ProjectCollector
 * @brief Collect & Archive: copies every file a module uses from outside the project into the
 *        project bundle, rewrites the modules to the copies, and zips a bundle for sending.
 *
 * Non-destructive: it only adds files to the bundle and points modules at them. It never moves,
 * deletes or overwrites a source, and a module whose file is missing is left exactly as it was.
 * Timeline clips are not its concern; their refs are already bundle-relative.
 */
class ProjectCollector {
public:
    /** Returns false to cancel. Called with 0..1. */
    using ProgressFn = std::function<bool(double)>;

    /** Every external module file to copy into `bundleDir`. Message thread only (reads the graph). */
    static CollectPlan plan(juce::AudioProcessorGraph& graph, const juce::File& bundleDir);

    /** Performs `plan`'s copies. Pure file I/O, safe on a background thread; `progress` may be null. */
    static CollectResult copy(const CollectPlan& plan, const ProgressFn& progress);

    /** Points every module at its copy. Message thread only. Returns the number of modules changed. */
    static int apply(juce::AudioProcessorGraph& graph, const CollectResult& result);

    /** plan + copy + apply in one synchronous call. Message thread only. */
    static CollectResult collect(juce::AudioProcessorGraph& graph, const juce::File& bundleDir);

    /** Zips `bundleDir` to `zipFile` with the bundle folder as the archive root, skipping autosave
     *  sidecars, hidden files and the top-level `excludedDirs`. Safe on a background thread;
     *  `progress` may be null. Returns an empty string on success, else why it failed. */
    static juce::String writeArchive(const juce::File& bundleDir, const juce::File& zipFile,
                                     const juce::StringArray& excludedDirs, double* progress);

private:
    ProjectCollector() = delete;
};

} // namespace synth
