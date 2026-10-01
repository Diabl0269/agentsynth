// ModuleCardLayoutStore.cpp -- the per-module-type root of the shared card-layout store.
// See docs/layout/module-card-layout.md#where-a-layout-comes-from.
#include "ModuleCardLayoutStore.h"
#include "UserSettings.h"

namespace synth {

namespace {
constexpr const char* kFolderName = "ModuleCardLayouts";
} // namespace

juce::File ModuleCardLayoutStore::resolveDefaultRootDirectory() {
    // Like ControllerProfileStore: stop at the settings file's PARENT directory, so a layout is a
    // shareable document beside the settings file, never inside the PropertiesFile.
    return userSettingsOptions().getDefaultFile().getParentDirectory().getChildFile(kFolderName);
}

ModuleCardLayoutStore::ModuleCardLayoutStore()
    : CardLayoutStore(resolveDefaultRootDirectory()) {}

ModuleCardLayoutStore::ModuleCardLayoutStore(juce::File rootDir)
    : CardLayoutStore(std::move(rootDir)) {}

// A type name can hold characters a file name cannot ("Sample & Hold" is fine, a future "A/B" is not).
juce::String ModuleCardLayoutStore::directoryKey(const juce::String& moduleType) {
    return juce::File::createLegalFileName(moduleType.trim());
}

// The type name is also stored inside each file, so a file stays self-describing if renamed.
juce::var ModuleCardLayoutStore::ownerOf(const juce::String& moduleType) {
    auto* object = new juce::DynamicObject();
    object->setProperty("moduleType", moduleType);
    return juce::var(object);
}

juce::File ModuleCardLayoutStore::getTypeDirectory(const juce::String& moduleType) const {
    return directoryFor(directoryKey(moduleType));
}

ModuleCardLayoutStore::LoadResult ModuleCardLayoutStore::loadDefault(const juce::String& moduleType) const {
    return CardLayoutStore::loadDefault(directoryKey(moduleType));
}

bool ModuleCardLayoutStore::setDefault(const juce::String& moduleType, const CardLayout& layout) {
    if (directoryKey(moduleType).isEmpty())
        return false;
    const juce::var owner = ownerOf(moduleType);
    if (!writeDefault(directoryKey(moduleType), owner, layout))
        return false;
    listeners_.call([&](Listener& listener) { listener.layoutChangedForModuleType(moduleType); });
    return true;
}

bool ModuleCardLayoutStore::clearDefault(const juce::String& moduleType) {
    bool removed = false;
    if (!removeDefault(directoryKey(moduleType), removed))
        return false;
    if (removed)
        listeners_.call([&](Listener& listener) { listener.layoutChangedForModuleType(moduleType); });
    return true;
}

juce::StringArray ModuleCardLayoutStore::listPresets(const juce::String& moduleType) const {
    return CardLayoutStore::listPresets(directoryKey(moduleType));
}

bool ModuleCardLayoutStore::savePreset(const juce::String& moduleType, const juce::String& name,
                                       const CardLayout& layout) {
    if (directoryKey(moduleType).isEmpty())
        return false;
    const juce::var owner = ownerOf(moduleType);
    return writePreset(directoryKey(moduleType), owner, name, layout);
}

ModuleCardLayoutStore::LoadResult ModuleCardLayoutStore::loadPreset(const juce::String& moduleType,
                                                                    const juce::String& name) const {
    return CardLayoutStore::loadPreset(directoryKey(moduleType), name);
}

bool ModuleCardLayoutStore::deletePreset(const juce::String& moduleType, const juce::String& name) {
    return removePreset(directoryKey(moduleType), name);
}

} // namespace synth
