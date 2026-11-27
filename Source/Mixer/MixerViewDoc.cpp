// Concern: MixerViewDoc's zone, hidden and bus-order bookkeeping and its project-file (de)serialisation.
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
    if (getZone(channelId) != zone)
        pinnedOrder_.erase(std::remove(pinnedOrder_.begin(), pinnedOrder_.end(), channelId), pinnedOrder_.end());
    if (zone == MixerZone::Scrolling)
        zones_.erase(channelId);
    else
        zones_[channelId] = zone;
}

void MixerViewDoc::setZoneOrder(const std::vector<juce::String>& orderedGroupIds) {
    const auto inGroup = [&](const juce::String& id) {
        return std::find(orderedGroupIds.begin(), orderedGroupIds.end(), id) != orderedGroupIds.end();
    };
    pinnedOrder_.erase(std::remove_if(pinnedOrder_.begin(), pinnedOrder_.end(), inGroup), pinnedOrder_.end());
    for (const auto& id : orderedGroupIds)
        if (id.isNotEmpty() && getZone(id) != MixerZone::Scrolling &&
            std::find(pinnedOrder_.begin(), pinnedOrder_.end(), id) == pinnedOrder_.end())
            pinnedOrder_.push_back(id);
}

size_t MixerViewDoc::pinnedRank(const juce::String& channelId) const {
    const auto it = std::find(pinnedOrder_.begin(), pinnedOrder_.end(), channelId);
    return static_cast<size_t>(it - pinnedOrder_.begin()); // end() is size(): after every ranked channel
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

void MixerViewDoc::setBusOrder(std::vector<juce::String> order) {
    busOrder_.clear();
    for (auto& id : order)
        if (id.isNotEmpty() && std::find(busOrder_.begin(), busOrder_.end(), id) == busOrder_.end())
            busOrder_.push_back(std::move(id));
}

// Stable: a bus the saved order has never seen (a new bus, or an old project) keeps its input position
// relative to the other unknown ones and lands after every known one.
std::vector<juce::String> MixerViewDoc::orderBuses(const std::vector<juce::String>& currentIds) const {
    auto rank = [&](const juce::String& id) {
        const auto it = std::find(busOrder_.begin(), busOrder_.end(), id);
        return it == busOrder_.end() ? busOrder_.size() : static_cast<size_t>(it - busOrder_.begin());
    };
    auto ordered = currentIds;
    std::stable_sort(ordered.begin(), ordered.end(),
                     [&](const juce::String& a, const juce::String& b) { return rank(a) < rank(b); });
    return ordered;
}

void MixerViewDoc::retainOnly(const std::vector<juce::String>& aliveIds) {
    auto alive = [&](const juce::String& id) {
        return id == kMasterId || id == kDirectId || std::find(aliveIds.begin(), aliveIds.end(), id) != aliveIds.end();
    };
    for (auto it = zones_.begin(); it != zones_.end();)
        it = alive(it->first) ? std::next(it) : zones_.erase(it);
    for (auto it = hidden_.begin(); it != hidden_.end();)
        it = alive(*it) ? std::next(it) : hidden_.erase(it);
    busOrder_.erase(
        std::remove_if(busOrder_.begin(), busOrder_.end(), [&](const juce::String& id) { return !alive(id); }),
        busOrder_.end());
    pinnedOrder_.erase(
        std::remove_if(pinnedOrder_.begin(), pinnedOrder_.end(), [&](const juce::String& id) { return !alive(id); }),
        pinnedOrder_.end());
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
    if (!busOrder_.empty()) {
        juce::Array<juce::var> order;
        for (const auto& id : busOrder_)
            order.add(id);
        root->setProperty("busOrder", order);
    }
    if (!pinnedOrder_.empty()) {
        juce::Array<juce::var> order;
        for (const auto& id : pinnedOrder_)
            order.add(id);
        root->setProperty("pinnedOrder", order);
    }
    return juce::var(root);
}

// A zone name other than left/right/scrolling, a non-string id or a non-object/array container rejects the
// whole value; a missing "zones", "hidden", "busOrder" or "pinnedOrder" is just empty.
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

    std::vector<juce::String> busOrder;
    if (root->hasProperty("busOrder")) {
        const auto* array = root->getProperty("busOrder").getArray();
        if (array == nullptr)
            return false;
        for (const auto& item : *array)
            if (!item.isString())
                return false;
        for (const auto& item : *array)
            busOrder.push_back(item.toString());
    }

    std::vector<juce::String> pinnedOrder;
    if (root->hasProperty("pinnedOrder")) {
        const auto* array = root->getProperty("pinnedOrder").getArray();
        if (array == nullptr)
            return false;
        for (const auto& item : *array)
            if (!item.isString())
                return false;
        for (const auto& item : *array)
            pinnedOrder.push_back(item.toString());
    }

    zones_ = std::move(zones);
    hidden_ = std::move(hidden);
    setBusOrder(std::move(busOrder));
    pinnedOrder_.clear();
    setZoneOrder(pinnedOrder); // keeps only pinned, distinct, non-empty ids
    return true;
}

} // namespace synth
