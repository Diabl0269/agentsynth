// PluginCardLayoutStore.cpp -- file layout, name mapping and change broadcast for per-plugin-type
// card layouts. See docs/control/plugin-card-layout.md#persistence.
#include "PluginCardLayoutStore.h"
#include "UserSettings.h"

namespace synth {

namespace {
constexpr const char* kFolderName = "PluginCardLayouts";
constexpr const char* kDefaultStem = "default";

// The reserved stem is compared case-insensitively: macOS and Windows volumes are, so a preset
// named "Default" would otherwise silently overwrite default.json.
bool isReservedStem(const juce::String& stem) { return stem.equalsIgnoreCase(kDefaultStem); }
} // namespace

juce::File PluginCardLayoutStore::resolveDefaultRootDirectory() {
    // Like ControllerProfileStore: stop at the settings file's PARENT directory. A layout is a
    // shareable document that lives beside the settings file, never inside the PropertiesFile.
    return userSettingsOptions().getDefaultFile().getParentDirectory().getChildFile(kFolderName);
}

PluginCardLayoutStore::PluginCardLayoutStore()
    : rootDir_(resolveDefaultRootDirectory()) {}

PluginCardLayoutStore::PluginCardLayoutStore(juce::File rootDir)
    : rootDir_(std::move(rootDir)) {}

// The directory key is `<format>-<uid>`. A uid of 0 means "unknown" (PluginIdentity matches those
// by name), and keying every such plugin on "-0" would make them share one layout, so they fall
// back to `<format>-name-<legal name>` instead.
juce::File PluginCardLayoutStore::getPluginDirectory(const PluginIdentity& identity) const {
    const juce::String format = juce::File::createLegalFileName(identity.format);
    const juce::String key =
        identity.uid != 0 ? juce::String(identity.uid) : "name-" + juce::File::createLegalFileName(identity.name);
    return rootDir_.getChildFile(format + "-" + key);
}

juce::File PluginCardLayoutStore::presetFile(const PluginIdentity& identity, const juce::String& name) const {
    const juce::String stem = juce::File::createLegalFileName(name.trim());
    if (stem.isEmpty() || isReservedStem(stem))
        return {};
    return getPluginDirectory(identity).getChildFile(stem + ".json");
}

// The file is the layout's own JSON plus the plugin's identity beside it. CardLayout::fromVar
// ignores the extra properties, so the file stays readable as a bare layout, and the name inside is
// what a picker shows for a plugin that is not currently loaded.
bool PluginCardLayoutStore::writeLayout(const juce::File& file, const PluginIdentity& identity,
                                        const CardLayout& layout) const {
    if (!identity.isValid() || file == juce::File())
        return false;

    juce::var json = layout.toVar();
    auto* object = json.getDynamicObject();
    const juce::var identityJson = identity.toVar(); // named: the object dies with the temporary
    if (auto* identityObject = identityJson.getDynamicObject())
        for (const auto& property : identityObject->getProperties())
            object->setProperty(property.name, property.value);

    if (!file.getParentDirectory().exists() && !file.getParentDirectory().createDirectory())
        return false;
    return file.replaceWithText(juce::JSON::toString(json));
}

PluginCardLayoutStore::LoadResult PluginCardLayoutStore::readLayout(const juce::File& file) {
    LoadResult result;
    if (file == juce::File() || !file.existsAsFile())
        return result;

    const juce::var json = juce::JSON::parse(file);
    auto parsed = CardLayout::fromVar(json);
    switch (parsed.status) {
    case CardLayout::ParseStatus::Ok:
        result.status = LoadStatus::Ok;
        result.layout = std::move(parsed.layout);
        if (auto* object = json.getDynamicObject())
            result.pluginName = object->getProperty("pluginName").toString();
        break;
    case CardLayout::ParseStatus::UnsupportedVersion:
        result.status = LoadStatus::UnsupportedVersion;
        break;
    case CardLayout::ParseStatus::Malformed:
        result.status = LoadStatus::Malformed;
        break;
    }
    return result;
}

PluginCardLayoutStore::LoadResult PluginCardLayoutStore::loadDefault(const PluginIdentity& identity) const {
    return readLayout(getPluginDirectory(identity).getChildFile(juce::String(kDefaultStem) + ".json"));
}

bool PluginCardLayoutStore::setDefault(const PluginIdentity& identity, const CardLayout& layout) {
    const auto file = getPluginDirectory(identity).getChildFile(juce::String(kDefaultStem) + ".json");
    if (!writeLayout(file, identity, layout))
        return false;
    listeners_.call([&](Listener& listener) { listener.layoutChangedForPlugin(identity); });
    return true;
}

bool PluginCardLayoutStore::clearDefault(const PluginIdentity& identity) {
    const auto file = getPluginDirectory(identity).getChildFile(juce::String(kDefaultStem) + ".json");
    if (!file.existsAsFile())
        return true; // idempotent, and nothing changed so nothing to broadcast
    if (!file.deleteFile())
        return false;
    listeners_.call([&](Listener& listener) { listener.layoutChangedForPlugin(identity); });
    return true;
}

juce::StringArray PluginCardLayoutStore::listPresets(const PluginIdentity& identity) const {
    juce::StringArray names;
    for (const auto& file :
         getPluginDirectory(identity).findChildFiles(juce::File::findFiles, /*recursive=*/false, "*.json"))
        if (!isReservedStem(file.getFileNameWithoutExtension()))
            names.add(file.getFileNameWithoutExtension());
    names.sortNatural();
    return names;
}

bool PluginCardLayoutStore::savePreset(const PluginIdentity& identity, const juce::String& name,
                                       const CardLayout& layout) {
    return writeLayout(presetFile(identity, name), identity, layout);
}

PluginCardLayoutStore::LoadResult PluginCardLayoutStore::loadPreset(const PluginIdentity& identity,
                                                                    const juce::String& name) const {
    return readLayout(presetFile(identity, name));
}

bool PluginCardLayoutStore::deletePreset(const PluginIdentity& identity, const juce::String& name) {
    const auto file = presetFile(identity, name);
    return file != juce::File() && file.existsAsFile() && file.deleteFile();
}

} // namespace synth
