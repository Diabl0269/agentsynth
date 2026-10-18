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
        {fk::kSamplerType, fk::kInstrumentFolder, true, ProjectBundle::kInstrumentsSubdirName, false, true},
        {fk::kSamplerType, fk::kInstrumentFiles, false, ProjectBundle::kSamplesSubdirName, true},
        {fk::kDrumKitType, fk::kInstrumentFolder, true, ProjectBundle::kInstrumentsSubdirName, false, true},
        {fk::kDrumKitType, fk::kInstrumentFiles, false, ProjectBundle::kSamplesSubdirName, true},
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
        if (!key.isList) {
            const juce::String value = obj->getProperty(key.stateKey).toString();
            if (value.isNotEmpty())
                fn(key, value);
        } else if (auto* list = obj->getProperty(key.stateKey).getArray()) {
            for (const auto& element : *list)
                if (element.toString().isNotEmpty())
                    fn(key, element.toString());
        }
    }
}

void ModuleFileRefs::rewriteFileValues(
    const juce::String& moduleType, juce::DynamicObject& state,
    const std::function<std::optional<juce::String>(const ModuleFileKey&, const juce::String&)>& fn) {
    for (const auto& key : keys()) {
        if (moduleType != key.moduleType)
            continue;
        if (!key.isList) {
            const juce::String value = state.getProperty(key.stateKey).toString();
            const auto replacement = value.isNotEmpty() ? fn(key, value) : std::nullopt;
            if (!replacement)
                continue;
            if (replacement->isEmpty())
                state.removeProperty(key.stateKey);
            else
                state.setProperty(key.stateKey, *replacement);
        } else if (auto* list = state.getProperty(key.stateKey).getArray()) {
            for (auto& element : *list)
                if (element.toString().isNotEmpty())
                    if (const auto replacement = fn(key, element.toString()))
                        element = *replacement;
        }
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
        rewriteFileValues(type, state, [&](const ModuleFileKey&, const juce::String& value) {
            std::optional<juce::String> replacement;
            if (juce::File::isAbsolutePath(value))
                if (const auto ref = toBundleRef(juce::File(value), bundleDir); ref.isNotEmpty())
                    replacement = ref;
            return replacement;
        });
    });
}

// An absolute value is a legacy project or a genuinely external file and passes through untouched.
// A relative one that does not resolve inside the bundle is removed rather than left for the
// module to interpret: juce::File asserts on a relative path, and a dropped key reads as "no file"
// exactly like a missing one. A list element is blanked instead, so the others keep their indices.
void ModuleFileRefs::resolveOnLoad(juce::var& patchJson, const juce::File& bundleDir) {
    forEachNodeState(patchJson, [&bundleDir](const juce::String& type, juce::DynamicObject& state) {
        rewriteFileValues(type, state, [&](const ModuleFileKey&, const juce::String& value) {
            std::optional<juce::String> replacement;
            if (!juce::File::isAbsolutePath(value))
                replacement = resolveBundleRef(value, bundleDir).getFullPathName(); // empty when it does not resolve
            return replacement;
        });
    });
}

} // namespace synth
