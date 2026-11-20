#include "ParamNameRepair.h"

#include "../AIStateMapper/AIStateMapper.h"

#include <map>

namespace synth::projectgenerate {

namespace {

// Lower case, no underscores or dashes: "filter_type" and "filterType" compare equal.
juce::String squash(const juce::String& key) { return key.toLowerCase().removeCharacters("_- "); }

// The squashed key without a trailing "id" and without trailing digits ("cutoff_1" -> "cutoff", "filterType_id" ->
// "filtertype"); empty when nothing is left.
juce::String strip(const juce::String& key) {
    juce::String s = squash(key);
    while (s.isNotEmpty() && juce::CharacterFunctions::isDigit(s.getLastCharacter()))
        s = s.dropLastCharacters(1);
    if (s.length() > 2 && s.endsWith("id"))
        s = s.dropLastCharacters(2);
    return s;
}

// The real parameter ids of a module type, remembered for the run of one plan.
class ParamIds {
public:
    const juce::StringArray& of(const juce::String& type) {
        auto found = cache.find(type);
        if (found != cache.end())
            return found->second;
        juce::StringArray ids;
        if (auto module = AIStateMapper::createModule(type))
            for (auto* param : module->getParameters())
                if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(param))
                    ids.add(ranged->paramID);
        return cache.emplace(type, ids).first->second;
    }

private:
    std::map<juce::String, juce::StringArray> cache;
};

// The one real id `key` is a spelling of, or empty when none or several are.
juce::String realIdFor(const juce::String& key, const juce::StringArray& real) {
    const juce::String squashed = squash(key);
    const juce::String stripped = strip(key);
    juce::String found;
    for (const auto& id : real) {
        const juce::String candidate = squash(id);
        if (candidate != squashed && candidate != stripped)
            continue;
        if (found.isNotEmpty() && found != id)
            return {};
        found = id;
    }
    return found;
}

void repairParams(const juce::var& params, const juce::String& type, ParamIds& ids, juce::StringArray& notes) {
    auto* object = params.getDynamicObject();
    if (object == nullptr || type.isEmpty())
        return;
    const juce::StringArray& real = ids.of(type);
    if (real.isEmpty())
        return;
    std::vector<std::pair<juce::String, juce::String>> renames;
    for (const auto& entry : object->getProperties()) {
        const juce::String key = entry.name.toString();
        if (real.contains(key))
            continue;
        const juce::String id = realIdFor(key, real);
        if (id.isNotEmpty() && !object->hasProperty(id))
            renames.emplace_back(key, id);
    }
    for (const auto& [key, id] : renames) {
        if (object->hasProperty(id))
            continue; // two invented spellings of one parameter: the first wins, the validator names the other
        object->setProperty(id, object->getProperty(key));
        object->removeProperty(key);
        notes.add("the parameter \"" + key + "\" was read as \"" + id + "\" on a " + type);
    }
}

} // namespace

void repairInventedParamNames(juce::var& root, juce::StringArray& notes) {
    auto* rootObject = root.getDynamicObject();
    if (rootObject == nullptr)
        return;
    ParamIds ids;
    if (auto* nodes = rootObject->getProperty("nodes").getArray())
        for (const auto& node : *nodes)
            repairParams(node.getProperty("params", {}), node.getProperty("type", {}).toString(), ids, notes);
    if (auto* ops = rootObject->getProperty("timelineOps").getArray())
        for (const auto& op : *ops) {
            if (op.getProperty("op", {}).toString() != "addInstrumentTrack")
                continue;
            repairParams(op.getProperty("instrumentParams", {}), op.getProperty("instrument", {}).toString(), ids,
                         notes);
            if (auto* inserts = op.getProperty("inserts", {}).getArray())
                for (const auto& insert : *inserts)
                    repairParams(insert.getProperty("params", {}), insert.getProperty("type", {}).toString(), ids,
                                 notes);
        }
}

void repairTrackOpSlips(juce::var& root, juce::StringArray& notes) {
    auto* rootObject = root.getDynamicObject();
    auto* ops = rootObject != nullptr ? rootObject->getProperty("timelineOps").getArray() : nullptr;
    if (ops == nullptr)
        return;
    juce::StringArray names;
    for (const auto& opVar : *ops) {
        auto* op = opVar.getDynamicObject();
        if (op == nullptr || op->getProperty("op").toString() != "addInstrumentTrack")
            continue;
        if (auto* params = op->getProperty("instrumentParams").getDynamicObject();
            params != nullptr && params->hasProperty("poly")) {
            if (!op->hasProperty("poly"))
                op->setProperty("poly", (bool)params->getProperty("poly") || (double)params->getProperty("poly") > 0.5);
            params->removeProperty("poly");
            notes.add("\"poly\" inside the instrument parameters was moved to the track");
        }
        const juce::String name = op->getProperty("name").toString();
        if (name.isEmpty())
            continue;
        juce::String unique = name;
        for (int n = 2; names.contains(unique, true); ++n)
            unique = name + " " + juce::String(n);
        names.add(unique);
        if (unique != name) {
            op->setProperty("name", unique);
            notes.add("a second new track named \"" + name + "\" was called \"" + unique + "\"");
        }
    }
}

} // namespace synth::projectgenerate
