#include "SoundPacks/SoundPackIndex.h"

namespace synth::soundpacks {

namespace {

constexpr int kSupportedFormat = 1;
constexpr int kMaxIdLength = 64;
constexpr int kSha256HexLength = 64;

bool isLowerHex(const juce::String& text) {
    for (auto c : text)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
            return false;
    return true;
}

// Only https downloads are accepted: the checksum guards the content, but the transport still hides what is fetched.
bool isHttpsUrl(const juce::String& url) {
    return url.startsWith("https://") && url.length() > 9 && !url.containsAnyOf(" \t\r\n");
}

bool readInstruments(const juce::var& packVar, IndexPack& pack) {
    const auto* list = packVar.getProperty("instruments", {}).getArray();
    if (list == nullptr)
        return true; // a pack may omit the preview list
    for (const auto& entry : *list) {
        IndexInstrument inst;
        inst.id = entry.getProperty("id", {}).toString();
        inst.name = entry.getProperty("name", {}).toString().trim();
        inst.category = entry.getProperty("category", {}).toString();
        if (inst.id.isEmpty())
            return false;
        if (inst.name.isEmpty())
            inst.name = inst.id;
        pack.instruments.push_back(std::move(inst));
    }
    return true;
}

bool readPack(const juce::var& packVar, IndexPack& pack) {
    pack.id = packVar.getProperty("id", {}).toString();
    pack.name = packVar.getProperty("name", {}).toString().trim();
    pack.version = packVar.getProperty("version", {}).toString().trim();
    pack.url = packVar.getProperty("url", {}).toString().trim();
    pack.sha256 = packVar.getProperty("sha256", {}).toString().trim().toLowerCase();
    pack.licence = packVar.getProperty("licence", {}).toString().trim();
    pack.licenceUrl = packVar.getProperty("licenceUrl", {}).toString().trim();
    pack.credit = packVar.getProperty("credit", {}).toString().trim();
    pack.downloadBytes = (juce::int64)packVar.getProperty("downloadBytes", 0);
    pack.installedBytes = (juce::int64)packVar.getProperty("installedBytes", 0);
    if (pack.name.isEmpty())
        pack.name = pack.id;
    // A licence link that is not https is dropped rather than shown: it is opened in the browser on a click.
    if (!isHttpsUrl(pack.licenceUrl))
        pack.licenceUrl = {};
    return SoundPackIndex::isValidId(pack.id) && pack.version.isNotEmpty() && isHttpsUrl(pack.url) &&
           pack.sha256.length() == kSha256HexLength && isLowerHex(pack.sha256) && pack.downloadBytes > 0 &&
           pack.installedBytes > 0 && readInstruments(packVar, pack);
}

} // namespace

bool SoundPackIndex::isValidId(const juce::String& id) {
    if (id.isEmpty() || id.length() > kMaxIdLength)
        return false;
    for (auto c : id)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_'))
            return false;
    return true;
}

const IndexPack* SoundPackIndex::find(const juce::String& packId) const {
    for (const auto& pack : packs)
        if (pack.id == packId)
            return &pack;
    return nullptr;
}

ParsedIndex SoundPackIndex::parse(const juce::String& json) {
    ParsedIndex result;
    const juce::var root = juce::JSON::parse(json);
    if (!root.isObject() || !root.hasProperty("format")) {
        result.error = "The sound pack list could not be read.";
        return result;
    }
    if ((int)root.getProperty("format", 0) != kSupportedFormat) {
        result.error = "The sound pack list is from a different version of the app. Update the app and try again.";
        return result;
    }
    const auto* list = root.getProperty("packs", {}).getArray();
    if (list == nullptr) {
        result.error = "The sound pack list could not be read.";
        return result;
    }
    for (const auto& packVar : *list) {
        IndexPack pack;
        if (!readPack(packVar, pack) || result.index.find(pack.id) != nullptr) {
            ++result.index.skippedPacks;
            continue;
        }
        result.index.packs.push_back(std::move(pack));
    }
    result.ok = true;
    return result;
}

juce::String formatPackSize(juce::int64 bytes) {
    constexpr double kMegabyte = 1000.0 * 1000.0;
    constexpr double kGigabyte = 1000.0 * kMegabyte;
    if ((double)bytes >= kGigabyte)
        return juce::String((double)bytes / kGigabyte, 1) + " GB";
    return juce::String(juce::jmax(1, juce::roundToInt((double)bytes / kMegabyte))) + " MB";
}

} // namespace synth::soundpacks
