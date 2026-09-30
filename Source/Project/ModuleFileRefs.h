#pragma once

#include <functional>
#include <juce_core/juce_core.h>
#include <vector>

namespace synth {

/** One node "state" key that holds a path on disk: which module type writes it, whether it names a
 *  folder rather than a file, and the bundle subdirectory Collect copies it into. */
struct ModuleFileKey {
    const char* moduleType;
    const char* stateKey;
    bool isFolder;
    const char* bundleSubdir;
};

/**
 * @class ModuleFileRefs
 * @brief The bundle-relative path convention for module file state (Sampler sample, Wavetable
 *        file and folder) inside a saved `.agsproj`.
 *
 * Modules always hold ABSOLUTE paths in memory; this class translates at the bundle boundary only.
 * On save, a path inside the bundle is written as a relative ref with '/' separators
 * ("Samples/kick.wav"); a path outside it stays absolute. On load, a relative ref is resolved
 * against the bundle, and one that is malformed or escapes the bundle is dropped. Absolute values
 * load exactly as they always have. Headless and stateless; message thread only.
 */
class ModuleFileRefs {
public:
    /** Every registered file-state key. */
    static const std::vector<ModuleFileKey>& keys();

    /** Calls `fn` once per registered key present (non-empty) in `state` for `moduleType`. `state`
     *  may be void or a non-object, in which case nothing is called. */
    static void forEachFileValue(const juce::String& moduleType, const juce::var& state,
                                 const std::function<void(const ModuleFileKey&, const juce::String&)>& fn);

    /** `file` as a '/'-separated bundle-relative ref, or empty when it is not inside `bundleDir`. */
    static juce::String toBundleRef(const juce::File& file, const juce::File& bundleDir);

    /** The file `ref` names inside `bundleDir`, or an invalid File when `ref` is malformed (see
     *  TimelineDoc::isValidAssetRef) or resolves outside `bundleDir`. Never touches the disk. */
    static juce::File resolveBundleRef(const juce::String& ref, const juce::File& bundleDir);

    /** Rewrites, in place, every registered path inside `bundleDir` in `patchJson`'s node states
     *  to a bundle-relative ref. `patchJson` is `AIStateMapper::graphToJSON` output. */
    static void relativizeForSave(juce::var& patchJson, const juce::File& bundleDir);

    /** Resolves, in place, every relative ref in `patchJson`'s node states to an absolute path in
     *  `bundleDir`, removing any ref that is malformed or escapes the bundle. Call before the
     *  patch is applied. */
    static void resolveOnLoad(juce::var& patchJson, const juce::File& bundleDir);

private:
    ModuleFileRefs() = delete;
};

} // namespace synth
