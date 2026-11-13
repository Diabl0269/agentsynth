// PluginCardLayoutStore.cpp -- file layout, name mapping and change broadcast for per-plugin-type
// card layouts. See docs/control/plugin-card-layout.md#persistence.
#include "PluginCardLayoutStore.h"
#include "UserSettings.h"

namespace synth {

namespace {
constexpr const char* kFolderName = "PluginCardLayouts";
} // namespace

juce::File PluginCardLayoutStore::resolveDefaultRootDirectory() {
    // Like ControllerProfileStore: stop at the settings file's PARENT directory. A layout is a
    // shareable document that lives beside the settings file, never inside the PropertiesFile.
    return userSettingsOptions().getDefaultFile().getParentDirectory().getChildFile(kFolderName);
}

PluginCardLayoutStore::PluginCardLayoutStore()
    : CardLayoutStore(resolveDefaultRootDirectory()) {}

PluginCardLayoutStore::PluginCardLayoutStore(juce::File rootDir)
    : CardLayoutStore(std::move(rootDir)) {}

// The directory key is `<format>-<uid>`. A uid of 0 means "unknown" (PluginIdentity matches those
// by name), and keying every such plugin on "-0" would make them share one layout, so they fall
// back to `<format>-name-<legal name>` instead.
juce::String PluginCardLayoutStore::directoryKey(const PluginIdentity& identity) {
    const juce::String format = juce::File::createLegalFileName(identity.format);
    const juce::String key =
        identity.uid != 0 ? juce::String(identity.uid) : "name-" + juce::File::createLegalFileName(identity.name);
    return format + "-" + key;
}

juce::File PluginCardLayoutStore::getPluginDirectory(const PluginIdentity& identity) const {
    return directoryFor(directoryKey(identity));
}

juce::var PluginCardLayoutStore::ownerOf(const PluginIdentity& identity) { return identity.toVar(); }

PluginCardLayoutStore::LoadResult PluginCardLayoutStore::toLoadResult(CardLayoutLoadResult&& base) {
    LoadResult result;
    result.status = base.status;
    result.layout = std::move(base.layout);
    if (auto* object = base.file.getDynamicObject())
        result.pluginName = object->getProperty("pluginName").toString();
    return result;
}

PluginCardLayoutStore::LoadResult PluginCardLayoutStore::loadDefault(const PluginIdentity& identity) const {
    return toLoadResult(CardLayoutStore::loadDefault(directoryKey(identity)));
}

bool PluginCardLayoutStore::setDefault(const PluginIdentity& identity, const CardLayout& layout) {
    if (!identity.isValid())
        return false;
    const juce::var owner = ownerOf(identity); // named: the object dies with the temporary
    if (!writeDefault(directoryKey(identity), owner, layout))
        return false;
    listeners_.call([&](Listener& listener) { listener.layoutChangedForPlugin(identity); });
    return true;
}

bool PluginCardLayoutStore::clearDefault(const PluginIdentity& identity) {
    bool removed = false;
    if (!removeDefault(directoryKey(identity), removed))
        return false;
    if (removed) // nothing changed otherwise, so nothing to broadcast
        listeners_.call([&](Listener& listener) { listener.layoutChangedForPlugin(identity); });
    return true;
}

juce::StringArray PluginCardLayoutStore::listPresets(const PluginIdentity& identity) const {
    return CardLayoutStore::listPresets(directoryKey(identity));
}

bool PluginCardLayoutStore::savePreset(const PluginIdentity& identity, const juce::String& name,
                                       const CardLayout& layout) {
    if (!identity.isValid())
        return false;
    const juce::var owner = ownerOf(identity);
    return writePreset(directoryKey(identity), owner, name, layout);
}

PluginCardLayoutStore::LoadResult PluginCardLayoutStore::loadPreset(const PluginIdentity& identity,
                                                                    const juce::String& name) const {
    return toLoadResult(CardLayoutStore::loadPreset(directoryKey(identity), name));
}

bool PluginCardLayoutStore::deletePreset(const PluginIdentity& identity, const juce::String& name) {
    return removePreset(directoryKey(identity), name);
}

} // namespace synth
