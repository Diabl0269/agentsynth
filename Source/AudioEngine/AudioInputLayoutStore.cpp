// Concern: the per-interface input layout (pairs and names) -- the data, its lookups and its JSON form.
#include "AudioEngine/AudioInputLayoutStore.h"

#include <algorithm>

namespace synth {

namespace {
constexpr int kFormatVersion = 1;
}

void AudioInputLayoutStore::addListener(Listener* listener) { listeners_.add(listener); }

void AudioInputLayoutStore::removeListener(Listener* listener) { listeners_.remove(listener); }

void AudioInputLayoutStore::changed() {
    listeners_.call([](Listener& listener) { listener.inputLayoutChanged(); });
    if (onChange)
        onChange();
}

AudioInputLayoutStore::Entry& AudioInputLayoutStore::entryFor(const juce::String& deviceType,
                                                              const juce::String& deviceName) {
    auto& entry = entries_[{deviceType, deviceName}];
    entry.deviceType = deviceType;
    entry.deviceName = deviceName;
    return entry;
}

const AudioInputLayoutStore::Entry* AudioInputLayoutStore::find(const juce::String& deviceType,
                                                                const juce::String& deviceName) const {
    const auto found = entries_.find({deviceType, deviceName});
    return found != entries_.end() ? &found->second : nullptr;
}

void AudioInputLayoutStore::touch(const juce::String& deviceType, const juce::String& deviceName, juce::int64 nowMs) {
    if (deviceName.isEmpty())
        return;
    entryFor(deviceType, deviceName).lastUsedMs = nowMs;
    changed();
}

bool AudioInputLayoutStore::has(const juce::String& deviceType, const juce::String& deviceName) const {
    return find(deviceType, deviceName) != nullptr;
}

bool AudioInputLayoutStore::isPaired(const juce::String& deviceType, const juce::String& deviceName, int pair) const {
    const auto* entry = find(deviceType, deviceName);
    return entry != nullptr && entry->pairedPairs.count(pair) != 0;
}

// Names are left alone: unpairing hands each jack back the name it had, and pairing again finds the pair's own.
void AudioInputLayoutStore::setPaired(const juce::String& deviceType, const juce::String& deviceName, int pair,
                                      bool paired) {
    if (deviceName.isEmpty() || pair < 0 || pair >= kMaxPairs || isPaired(deviceType, deviceName, pair) == paired)
        return;
    auto& entry = entryFor(deviceType, deviceName);
    if (paired)
        entry.pairedPairs.insert(pair);
    else
        entry.pairedPairs.erase(pair);
    changed();
}

juce::String AudioInputLayoutStore::getName(const juce::String& deviceType, const juce::String& deviceName,
                                            int firstJack) const {
    const auto* entry = find(deviceType, deviceName);
    if (entry == nullptr)
        return {};
    const auto found = entry->names.find(firstJack);
    return found != entry->names.end() ? found->second : juce::String();
}

void AudioInputLayoutStore::setName(const juce::String& deviceType, const juce::String& deviceName, int firstJack,
                                    const juce::String& name) {
    const auto trimmed = name.trim();
    if (deviceName.isEmpty() || firstJack < 0 || firstJack >= kMaxJacks ||
        getName(deviceType, deviceName, firstJack) == trimmed)
        return;
    auto& entry = entryFor(deviceType, deviceName);
    if (trimmed.isEmpty())
        entry.names.erase(firstJack);
    else
        entry.names[firstJack] = trimmed;
    changed();
}

bool AudioInputLayoutStore::forget(const juce::String& deviceType, const juce::String& deviceName) {
    if (entries_.erase({deviceType, deviceName}) == 0)
        return false;
    changed();
    return true;
}

std::vector<InputSlot> AudioInputLayoutStore::slotsFor(const juce::String& deviceType, const juce::String& deviceName,
                                                       int numJacks) const {
    std::vector<InputSlot> slots;
    const int jacks = juce::jlimit(0, kMaxJacks, numJacks);
    for (int first = 0; first < jacks; first += 2) {
        const bool pairable = first + 1 < jacks;
        if (pairable && isPaired(deviceType, deviceName, first / 2)) {
            slots.push_back({first, 2, getName(deviceType, deviceName, first)});
            continue;
        }
        slots.push_back({first, 1, getName(deviceType, deviceName, first)});
        if (pairable)
            slots.push_back({first + 1, 1, getName(deviceType, deviceName, first + 1)});
    }
    return slots;
}

int AudioInputLayoutStore::countNamedInputs(const juce::String& deviceType, const juce::String& deviceName,
                                            int numJacks) const {
    const auto slots = slotsFor(deviceType, deviceName, numJacks);
    return (int)std::count_if(slots.begin(), slots.end(), [](const InputSlot& s) { return s.name.isNotEmpty(); });
}

std::vector<AudioInputLayoutStore::Entry> AudioInputLayoutStore::entries() const {
    std::vector<Entry> all;
    all.reserve(entries_.size());
    for (const auto& [key, entry] : entries_)
        all.push_back(entry);
    std::stable_sort(all.begin(), all.end(),
                     [](const Entry& a, const Entry& b) { return a.lastUsedMs > b.lastUsedMs; });
    return all;
}

juce::String AudioInputLayoutStore::toJson() const {
    juce::Array<juce::var> interfaces;
    for (const auto& entry : entries()) {
        auto* item = new juce::DynamicObject();
        item->setProperty("type", entry.deviceType);
        item->setProperty("name", entry.deviceName);
        item->setProperty("lastUsed", entry.lastUsedMs);
        juce::Array<juce::var> pairs;
        for (const int pair : entry.pairedPairs)
            pairs.add(pair);
        item->setProperty("paired", pairs);
        auto* names = new juce::DynamicObject();
        for (const auto& [jack, name] : entry.names)
            names->setProperty(juce::Identifier(juce::String(jack)), name);
        item->setProperty("names", juce::var(names));
        interfaces.add(juce::var(item));
    }
    auto* root = new juce::DynamicObject();
    root->setProperty("version", kFormatVersion);
    root->setProperty("interfaces", interfaces);
    return juce::JSON::toString(juce::var(root), true);
}

void AudioInputLayoutStore::loadJson(const juce::String& json) {
    entries_.clear();
    if (json.isEmpty())
        return;
    juce::var root;
    if (!juce::JSON::parse(json, root).wasOk())
        return;
    const auto* interfaces = root.getProperty("interfaces", {}).getArray();
    if (interfaces == nullptr)
        return;
    for (const auto& item : *interfaces) {
        const auto* object = item.getDynamicObject();
        if (object == nullptr || !object->getProperty("name").isString() || !object->getProperty("type").isString() ||
            object->getProperty("name").toString().isEmpty())
            continue;
        auto& entry = entryFor(object->getProperty("type").toString(), object->getProperty("name").toString());
        entry.lastUsedMs = (juce::int64)object->getProperty("lastUsed");
        if (const auto* pairs = object->getProperty("paired").getArray())
            for (const auto& pair : *pairs)
                if (pair.isInt() && (int)pair >= 0 && (int)pair < kMaxPairs)
                    entry.pairedPairs.insert((int)pair);
        if (const auto* names = object->getProperty("names").getDynamicObject())
            for (const auto& property : names->getProperties()) {
                const int jack = property.name.toString().getIntValue();
                if (property.value.isString() && jack >= 0 && jack < kMaxJacks &&
                    property.value.toString().isNotEmpty())
                    entry.names[jack] = property.value.toString();
            }
    }
}

} // namespace synth
