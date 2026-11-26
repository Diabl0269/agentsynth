#include "ParamNameRepair.h"

#include "../../Modules/ModuleBase.h"
#include "../AIStateMapper/AIStateMapper.h"

#include <map>
#include <memory>

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

// The squashed key without any run of filler a model tacks on after a real id: "attackDiv_placeholder" ->
// "attackdiv", "cutoff_value_1" -> "cutoff". Never strips the key down to nothing.
juce::String stripFiller(const juce::String& key) {
    static const char* const fillers[] = {"placeholder", "parameter", "param", "value", "name", "id"};
    juce::String s = squash(key);
    for (bool changed = true; changed;) {
        changed = false;
        while (s.isNotEmpty() && juce::CharacterFunctions::isDigit(s.getLastCharacter())) {
            s = s.dropLastCharacters(1);
            changed = true;
        }
        for (const char* filler : fillers) {
            const juce::String f(filler);
            if (s.length() > f.length() && s.endsWith(f)) {
                s = s.dropLastCharacters(f.length());
                changed = true;
            }
        }
    }
    return s;
}

// The real parameter ids of a module type, and the ids it still adopts from old saves, remembered for the run of one
// plan.
class ParamIds {
public:
    const juce::StringArray& of(const juce::String& type) { return entry(type).ids; }

    // A key the module drops but still reads (old saves): never an invented name, so never left out.
    bool isLegacy(const juce::String& type, const juce::String& key) {
        return entry(type).legacy != nullptr && entry(type).legacy->isLegacyParamId(key);
    }

private:
    struct Entry {
        juce::StringArray ids;
        std::unique_ptr<juce::AudioProcessor> module;
        const ModuleBase* legacy = nullptr;
    };

    Entry& entry(const juce::String& type) {
        auto found = cache.find(type);
        if (found != cache.end())
            return found->second;
        Entry e;
        if (auto module = AIStateMapper::createModule(type)) {
            for (auto* param : module->getParameters())
                if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(param))
                    e.ids.add(ranged->paramID);
            e.legacy = dynamic_cast<const ModuleBase*>(module.get());
            e.module = std::move(module);
        }
        return cache.emplace(type, std::move(e)).first->second;
    }

    std::map<juce::String, Entry> cache;
};

// How many real ids a key could be a spelling of, and the one it is when that is exactly one.
struct Match {
    int count = 0;
    juce::String id;
};

Match matchFor(const juce::String& key, const juce::StringArray& real) {
    const juce::String squashed = squash(key);
    const juce::String stripped = strip(key);
    const juce::String bare = stripFiller(key);
    Match match;
    for (const auto& id : real) {
        const juce::String candidate = squash(id);
        if (candidate != squashed && candidate != stripped && candidate != bare)
            continue;
        if (match.count == 0 || match.id != id)
            ++match.count;
        match.id = id;
    }
    return match;
}

// The one real id `key` is a spelling of, or empty when none or several are.
juce::String realIdFor(const juce::String& key, const juce::StringArray& real) {
    const Match match = matchFor(key, real);
    return match.count == 1 ? match.id : juce::String();
}

void repairParams(const juce::var& params, const juce::String& type, ParamIds& ids, juce::StringArray& notes) {
    auto* object = params.getDynamicObject();
    if (object == nullptr || type.isEmpty())
        return;
    const juce::StringArray& real = ids.of(type);
    if (real.isEmpty())
        return;
    std::vector<std::pair<juce::String, juce::String>> renames;
    juce::StringArray dropped;
    for (const auto& entry : object->getProperties()) {
        const juce::String key = entry.name.toString();
        if (real.contains(key))
            continue;
        const Match match = matchFor(key, real);
        if (match.count == 1 && !object->hasProperty(match.id))
            renames.emplace_back(key, match.id);
        else if (match.count == 0 && !ids.isLegacy(type, key))
            dropped.add(key); // names nothing on this module: one stray name must not cost the whole plan
    }
    for (const auto& key : dropped) {
        object->removeProperty(key);
        notes.add("the parameter \"" + key + "\" was left out: a " + type + " has no parameter of that name");
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
    std::map<juce::String, juce::String> typeOfNode; // patch node id -> module type, for a modulation's "destParam"
    if (auto* nodes = rootObject->getProperty("nodes").getArray())
        for (const auto& node : *nodes) {
            const juce::String type = node.getProperty("type", {}).toString();
            typeOfNode[node.getProperty("id", {}).toString()] = type;
            repairParams(node.getProperty("params", {}), type, ids, notes);
        }
    if (auto* mods = rootObject->getProperty("modulations").getArray())
        for (const auto& mod : *mods) {
            auto* object = mod.getDynamicObject();
            const auto type = typeOfNode.find(mod.getProperty("dest", {}).toString());
            if (object == nullptr || type == typeOfNode.end() || !mod.getProperty("destParam", {}).isString())
                continue;
            const juce::String key = mod.getProperty("destParam", {}).toString();
            const juce::StringArray& real = ids.of(type->second);
            if (real.isEmpty() || real.contains(key))
                continue;
            if (const juce::String id = realIdFor(key, real); id.isNotEmpty()) {
                object->setProperty("destParam", id);
                notes.add("the modulation target \"" + key + "\" was read as \"" + id + "\" on a " + type->second);
            }
        }
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
