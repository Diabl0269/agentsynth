#include "PatchDocument.h"

namespace synth {

const juce::StringArray& PatchDocument::knownKeys() {
    static const juce::StringArray keys{"nodes",  "connections",       "modulations",  "mode",
                                        "remove", "removeModulations", "schemaVersion"};
    return keys;
}

void PatchDocument::loadFromVar(const juce::var& root) {
    stashed.clear();
    if (!root.isObject())
        return;

    auto* obj = root.getDynamicObject();
    if (obj == nullptr)
        return;

    const auto& known = knownKeys();
    for (const auto& prop : obj->getProperties()) {
        if (!known.contains(prop.name.toString()))
            stashed.set(prop.name, prop.value);
    }
}

juce::var PatchDocument::toVar(const juce::var& freshGraphJson) const {
    if (!freshGraphJson.isObject())
        return freshGraphJson;

    auto* fresh = freshGraphJson.getDynamicObject();
    if (fresh == nullptr)
        return freshGraphJson;

    for (const auto& prop : stashed)
        fresh->setProperty(prop.name, prop.value);

    return freshGraphJson;
}

void PatchDocument::clear() { stashed.clear(); }

} // namespace synth
