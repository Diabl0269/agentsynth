#include "Modules/Sampler/SoundPackLibrary.h"
#include "Branding.h"
#include <set>

namespace synth::sampler {

namespace {

std::optional<std::vector<juce::File>>& rootsOverride() {
    static std::optional<std::vector<juce::File>> roots;
    return roots;
}

juce::File bundledRoot() {
#if JUCE_MAC
    return juce::File::getSpecialLocation(juce::File::currentApplicationFile)
        .getChildFile("Contents/Resources/SoundPacks");
#else
    return juce::File::getSpecialLocation(juce::File::currentExecutableFile).getSiblingFile("SoundPacks");
#endif
}

juce::File userRoot() {
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile(branding::kSettingsFolderName)
        .getChildFile("SoundPacks");
}

// A pack-relative path that stays inside the pack folder, or an invalid File.
juce::File resolveInsidePack(const juce::File& packDir, const juce::String& relative) {
    if (relative.isEmpty() || juce::File::isAbsolutePath(relative))
        return {};
    const juce::File file = packDir.getChildFile(relative.replaceCharacter('\\', '/'));
    return file.isAChildOf(packDir) ? file : juce::File();
}

void readInstruments(const juce::File& packDir, const juce::var& manifest, const PackInstrument& packInfo,
                     std::vector<PackInstrument>& out) {
    const auto* entries = manifest.getProperty("instruments", {}).getArray();
    if (entries == nullptr) {
        juce::Logger::writeToLog("Sound pack " + packDir.getFileName() + ": pack.json has no instruments list.");
        return;
    }
    for (const auto& entry : *entries) {
        PackInstrument inst = packInfo;
        inst.instrumentId = entry.getProperty("id", {}).toString();
        inst.sfz = resolveInsidePack(packDir, entry.getProperty("sfz", {}).toString());
        if (inst.instrumentId.isEmpty() || !inst.sfz.existsAsFile()) {
            juce::Logger::writeToLog("Sound pack " + packDir.getFileName() +
                                     ": skipped an instrument entry (needs an id "
                                     "and an sfz inside the pack).");
            continue;
        }
        inst.name = entry.getProperty("name", {}).toString().trim();
        if (inst.name.isEmpty())
            inst.name = inst.instrumentId;
        inst.category = entry.getProperty("category", {}).toString();
        out.push_back(std::move(inst));
    }
}

// False when the folder is not a readable pack (no pack.json, bad JSON, no id).
bool readPack(const juce::File& packDir, std::set<juce::String>& seenIds, std::vector<PackInstrument>& out) {
    const juce::File manifestFile = packDir.getChildFile("pack.json");
    if (!manifestFile.existsAsFile())
        return true; // an ordinary folder, not a pack
    const juce::var manifest = juce::JSON::parse(manifestFile);
    PackInstrument info;
    info.packId = manifest.getProperty("id", {}).toString();
    if (!manifest.isObject() || info.packId.isEmpty()) {
        juce::Logger::writeToLog("Sound pack " + packDir.getFileName() + ": pack.json is malformed.");
        return false;
    }
    if (!seenIds.insert(info.packId).second)
        return true;
    info.packName = manifest.getProperty("name", {}).toString();
    if (info.packName.isEmpty())
        info.packName = info.packId;
    info.licence = manifest.getProperty("licence", {}).toString();
    readInstruments(packDir, manifest, info, out);
    return true;
}

} // namespace

std::vector<juce::File> SoundPackLibrary::roots() {
    if (rootsOverride().has_value())
        return *rootsOverride();
    return {bundledRoot(), userRoot()};
}

void SoundPackLibrary::setRootsForTests(std::optional<std::vector<juce::File>> roots) {
    rootsOverride() = std::move(roots);
}

std::vector<PackInstrument> SoundPackLibrary::listInstruments() {
    std::vector<PackInstrument> out;
    std::set<juce::String> seenIds;
    for (const auto& root : roots()) {
        auto folders = root.findChildFiles(juce::File::findDirectories, false);
        folders.sort();
        for (const auto& folder : folders)
            readPack(folder, seenIds, out);
    }
    return out;
}

std::optional<PackInstrument> SoundPackLibrary::find(const juce::String& packId, const juce::String& instrumentId) {
    for (auto& inst : listInstruments())
        if (inst.packId == packId && inst.instrumentId == instrumentId)
            return std::move(inst);
    return std::nullopt;
}

} // namespace synth::sampler
