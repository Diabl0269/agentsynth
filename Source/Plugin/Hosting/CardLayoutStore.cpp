// CardLayoutStore.cpp -- file layout and name mapping shared by every card-layout store.
// See docs/control/plugin-card-layout.md#persistence.
#include "CardLayoutStore.h"

namespace synth {

namespace {
constexpr const char* kDefaultStem = "default";

// The reserved stem is compared case-insensitively: macOS and Windows volumes are, so a preset
// named "Default" would otherwise silently overwrite default.json.
bool isReservedStem(const juce::String& stem) { return stem.equalsIgnoreCase(kDefaultStem); }
} // namespace

CardLayoutStore::CardLayoutStore(juce::File rootDir)
    : rootDir_(std::move(rootDir)) {}

juce::File CardLayoutStore::directoryFor(const juce::String& key) const { return rootDir_.getChildFile(key); }

juce::File CardLayoutStore::defaultFile(const juce::String& key) const {
    return directoryFor(key).getChildFile(juce::String(kDefaultStem) + ".json");
}

juce::File CardLayoutStore::presetFile(const juce::String& key, const juce::String& name) const {
    const juce::String stem = juce::File::createLegalFileName(name.trim());
    if (stem.isEmpty() || isReservedStem(stem))
        return {};
    return directoryFor(key).getChildFile(stem + ".json");
}

// The file is the layout's own JSON plus the owner's identity beside it. CardLayout::fromVar
// ignores the extra properties, so the file stays readable as a bare layout, and the name inside is
// what a picker shows for an owner that is not currently loaded.
bool CardLayoutStore::writeLayout(const juce::File& file, const juce::var& owner, const CardLayout& layout) {
    if (file == juce::File())
        return false;

    juce::var json = layout.toVar();
    auto* object = json.getDynamicObject();
    if (auto* ownerObject = owner.getDynamicObject())
        for (const auto& property : ownerObject->getProperties())
            object->setProperty(property.name, property.value);

    if (!file.getParentDirectory().exists() && !file.getParentDirectory().createDirectory())
        return false;
    return file.replaceWithText(juce::JSON::toString(json));
}

CardLayoutLoadResult CardLayoutStore::readLayout(const juce::File& file) {
    CardLayoutLoadResult result;
    if (file == juce::File() || !file.existsAsFile())
        return result;

    const juce::var json = juce::JSON::parse(file);
    auto parsed = CardLayout::fromVar(json);
    switch (parsed.status) {
    case CardLayout::ParseStatus::Ok:
        result.status = CardLayoutLoadStatus::Ok;
        result.layout = std::move(parsed.layout);
        result.file = json;
        break;
    case CardLayout::ParseStatus::UnsupportedVersion:
        result.status = CardLayoutLoadStatus::UnsupportedVersion;
        break;
    case CardLayout::ParseStatus::Malformed:
        result.status = CardLayoutLoadStatus::Malformed;
        break;
    }
    return result;
}

CardLayoutLoadResult CardLayoutStore::loadDefault(const juce::String& key) const {
    return readLayout(defaultFile(key));
}

bool CardLayoutStore::writeDefault(const juce::String& key, const juce::var& owner, const CardLayout& layout) {
    return writeLayout(defaultFile(key), owner, layout);
}

bool CardLayoutStore::removeDefault(const juce::String& key, bool& removed) {
    removed = false;
    const auto file = defaultFile(key);
    if (!file.existsAsFile())
        return true; // idempotent
    removed = file.deleteFile();
    return removed;
}

juce::StringArray CardLayoutStore::listPresets(const juce::String& key) const {
    juce::StringArray names;
    for (const auto& file : directoryFor(key).findChildFiles(juce::File::findFiles, /*recursive=*/false, "*.json"))
        if (!isReservedStem(file.getFileNameWithoutExtension()))
            names.add(file.getFileNameWithoutExtension());
    names.sortNatural();
    return names;
}

bool CardLayoutStore::writePreset(const juce::String& key, const juce::var& owner, const juce::String& name,
                                  const CardLayout& layout) {
    return writeLayout(presetFile(key, name), owner, layout);
}

CardLayoutLoadResult CardLayoutStore::loadPreset(const juce::String& key, const juce::String& name) const {
    return readLayout(presetFile(key, name));
}

bool CardLayoutStore::removePreset(const juce::String& key, const juce::String& name) {
    const auto file = presetFile(key, name);
    return file != juce::File() && file.existsAsFile() && file.deleteFile();
}

} // namespace synth
