// Concern: MixerViewDoc's zone and hidden bookkeeping and its project-file (de)serialisation.
#include "MixerViewDoc.h"

#include <algorithm>

namespace synth {

const char* mixerZoneToString(MixerZone zone) noexcept {
    switch (zone) {
    case MixerZone::Left:
        return "left";
    case MixerZone::Right:
        return "right";
    case MixerZone::Scrolling:
        break;
    }
    return "scrolling";
}

MixerZone mixerZoneFromString(const juce::String& text) noexcept {
    if (text == "left")
        return MixerZone::Left;
    if (text == "right")
        return MixerZone::Right;
    return MixerZone::Scrolling;
}

MixerViewDoc MixerViewDoc::forNewProject() {
    MixerViewDoc doc;
    doc.setZone(kMasterId, MixerZone::Right);
    return doc;
}

MixerZone MixerViewDoc::getZone(const juce::String& channelId) const {
    const auto it = zones_.find(channelId);
    return it == zones_.end() ? MixerZone::Scrolling : it->second;
}

void MixerViewDoc::setZone(const juce::String& channelId, MixerZone zone) {
    if (channelId.isEmpty())
        return;
    if (zone == MixerZone::Scrolling)
        zones_.erase(channelId);
    else
        zones_[channelId] = zone;
}

bool MixerViewDoc::isHidden(const juce::String& channelId) const { return hidden_.count(channelId) != 0; }

void MixerViewDoc::setHidden(const juce::String& channelId, bool hidden) {
    if (channelId.isEmpty() || channelId == kMasterId)
        return;
    if (hidden)
        hidden_.insert(channelId);
    else
        hidden_.erase(channelId);
}

void MixerViewDoc::retainOnly(const std::vector<juce::String>& aliveIds) {
    auto alive = [&](const juce::String& id) {
        return id == kMasterId || id == kDirectId || std::find(aliveIds.begin(), aliveIds.end(), id) != aliveIds.end();
    };
    for (auto it = zones_.begin(); it != zones_.end();)
        it = alive(it->first) ? std::next(it) : zones_.erase(it);
    for (auto it = hidden_.begin(); it != hidden_.end();)
        it = alive(*it) ? std::next(it) : hidden_.erase(it);
}

// Entries are written in id order, so the same document always serialises to the same text.
juce::var MixerViewDoc::toVar() const {
    auto* zones = new juce::DynamicObject();
    for (const auto& [id, zone] : zones_)
        zones->setProperty(id, mixerZoneToString(zone));
    juce::Array<juce::var> hidden;
    for (const auto& id : hidden_)
        hidden.add(id);

    auto* root = new juce::DynamicObject();
    root->setProperty("zones", juce::var(zones));
    root->setProperty("hidden", hidden);
    return juce::var(root);
}

// A zone name other than left/right/scrolling, a non-string id or a non-object/array container rejects the
// whole value; a missing "zones" or "hidden" is just empty.
bool MixerViewDoc::fromVar(const juce::var& v) {
    const auto* root = v.getDynamicObject();
    if (root == nullptr)
        return false;

    std::map<juce::String, MixerZone> zones;
    if (root->hasProperty("zones")) {
        const auto* object = root->getProperty("zones").getDynamicObject();
        if (object == nullptr)
            return false;
        for (const auto& property : object->getProperties()) {
            const auto name = property.value.toString();
            if (!property.value.isString() || (name != "left" && name != "right" && name != "scrolling"))
                return false;
            if (const auto zone = mixerZoneFromString(name); zone != MixerZone::Scrolling)
                zones[property.name.toString()] = zone;
        }
    }

    std::set<juce::String> hidden;
    if (root->hasProperty("hidden")) {
        const auto* array = root->getProperty("hidden").getArray();
        if (array == nullptr)
            return false;
        for (const auto& item : *array) {
            if (!item.isString())
                return false;
            if (item.toString() != kMasterId && item.toString().isNotEmpty())
                hidden.insert(item.toString());
        }
    }

    zones_ = std::move(zones);
    hidden_ = std::move(hidden);
    return true;
}

} // namespace synth
