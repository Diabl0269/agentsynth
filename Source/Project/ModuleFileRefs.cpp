// ModuleFileRefs.cpp — the key registry and the save/load translation between absolute module file
// paths (in memory) and bundle-relative refs (in a saved project.json).
#include "ModuleFileRefs.h"
#include "Modules/ModuleFileStateKeys.h"
#include "ProjectBundle.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"

namespace synth {

namespace {

namespace fk = module_file_keys;

// Visits every node object in a graphToJSON-shaped root that carries a "state" object.
void forEachNodeState(juce::var& patchJson,
                      const std::function<void(const juce::String& type, juce::DynamicObject& state)>& fn) {
    auto* nodes = patchJson.getProperty("nodes", {}).getArray();
    if (nodes == nullptr)
        return;
    for (auto& node : *nodes) {
        auto* nodeObj = node.getDynamicObject();
        if (nodeObj == nullptr)
            continue;
        if (auto* state = nodeObj->getProperty("state").getDynamicObject())
            fn(nodeObj->getProperty("type").toString(), *state);
    }
}

} // namespace

const std::vector<ModuleFileKey>& ModuleFileRefs::keys() {
    static const std::vector<ModuleFileKey> registry = {
        {fk::kSamplerType, fk::kSampleFile, false, ProjectBundle::kSamplesSubdirName},
        {fk::kWavetableType, fk::kWavetableFile, false, ProjectBundle::kWavetablesSubdirName},
        {fk::kWavetableType, fk::kWavetableFolder, true, ProjectBundle::kWavetablesSubdirName},
    };
    return registry;
}

void ModuleFileRefs::forEachFileValue(const juce::String& moduleType, const juce::var& state,
                                      const std::function<void(const ModuleFileKey&, const juce::String&)>& fn) {
    auto* obj = state.getDynamicObject();
    if (obj == nullptr)
        return;
    for (const auto& key : keys()) {
        if (moduleType != key.moduleType)
            continue;
        const juce::String value = obj->getProperty(key.stateKey).toString();
        if (value.isNotEmpty())
            fn(key, value);
    }
}

juce::String ModuleFileRefs::toBundleRef(const juce::File& file, const juce::File& bundleDir) {
    if (bundleDir == juce::File() || !file.isAChildOf(bundleDir))
        return {};
    return file.getRelativePathFrom(bundleDir).replaceCharacter('\\', '/');
}

// The same two checks AudioClipStreamer::resolveAssetRef makes for clip refs: the string rule, and
// then that the resolved file is still inside the bundle. A hand-edited project.json must never be
// a way to point a module at an arbitrary file.
juce::File ModuleFileRefs::resolveBundleRef(const juce::String& ref, const juce::File& bundleDir) {
    if (ref.isEmpty() || bundleDir == juce::File() || !TimelineDoc::isValidAssetRef(ref))
        return {};
    const auto resolved = bundleDir.getChildFile(ref.replaceCharacter('\\', '/'));
    return resolved.isAChildOf(bundleDir) ? resolved : juce::File();
}

void ModuleFileRefs::relativizeForSave(juce::var& patchJson, const juce::File& bundleDir) {
    forEachNodeState(patchJson, [&bundleDir](const juce::String& type, juce::DynamicObject& state) {
        forEachFileValue(type, juce::var(&state), [&](const ModuleFileKey& key, const juce::String& value) {
            if (!juce::File::isAbsolutePath(value))
                return;
            const auto ref = toBundleRef(juce::File(value), bundleDir);
            if (ref.isNotEmpty())
                state.setProperty(key.stateKey, ref);
        });
    });
}

// An absolute value is a legacy project or a genuinely external file and passes through untouched.
// A relative one that does not resolve inside the bundle is removed rather than left for the
// module to interpret: juce::File asserts on a relative path, and a dropped key reads as "no file"
// exactly like a missing one.
void ModuleFileRefs::resolveOnLoad(juce::var& patchJson, const juce::File& bundleDir) {
    forEachNodeState(patchJson, [&bundleDir](const juce::String& type, juce::DynamicObject& state) {
        forEachFileValue(type, juce::var(&state), [&](const ModuleFileKey& key, const juce::String& value) {
            if (juce::File::isAbsolutePath(value))
                return;
            const auto resolved = resolveBundleRef(value, bundleDir);
            if (resolved == juce::File())
                state.removeProperty(key.stateKey);
            else
                state.setProperty(key.stateKey, resolved.getFullPathName());
        });
    });
}

} // namespace synth
