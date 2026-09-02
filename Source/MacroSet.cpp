#include "MacroSet.h"
#include <set>
#include <utility>

namespace synth {

Macro* MacroSet::find(const juce::String& macroId) {
    for (auto& m : macros_)
        if (m.id == macroId)
            return &m;
    return nullptr;
}

const Macro* MacroSet::find(const juce::String& macroId) const {
    for (const auto& m : macros_)
        if (m.id == macroId)
            return &m;
    return nullptr;
}

Macro* MacroSet::findByMember(const juce::String& memberUuid) {
    for (auto& m : macros_)
        if (m.hasMember(memberUuid))
            return &m;
    return nullptr;
}

const Macro* MacroSet::findByMember(const juce::String& memberUuid) const {
    for (const auto& m : macros_)
        if (m.hasMember(memberUuid))
            return &m;
    return nullptr;
}

Macro& MacroSet::add(Macro macro) {
    if (macro.id.isEmpty())
        macro.id = juce::Uuid().toDashedString();
    macros_.push_back(std::move(macro));
    return macros_.back();
}

bool MacroSet::remove(const juce::String& macroId) {
    const auto before = macros_.size();
    macros_.erase(std::remove_if(macros_.begin(), macros_.end(), [&](const Macro& m) { return m.id == macroId; }),
                  macros_.end());
    return macros_.size() != before;
}

juce::String MacroSet::removeMemberEverywhere(const juce::String& memberUuid) {
    for (auto it = macros_.begin(); it != macros_.end(); ++it) {
        auto memberIt = std::find(it->members.begin(), it->members.end(), memberUuid);
        if (memberIt == it->members.end())
            continue;

        const juce::String touchedId = it->id;
        it->members.erase(memberIt);
        if (it->members.empty())
            macros_.erase(it);
        return touchedId;
    }
    return {};
}

bool MacroSet::retainOnly(const std::vector<juce::String>& aliveMemberUuids) {
    std::set<juce::String> alive(aliveMemberUuids.begin(), aliveMemberUuids.end());
    bool changed = false;

    for (auto it = macros_.begin(); it != macros_.end();) {
        const auto before = it->members.size();
        it->members.erase(std::remove_if(it->members.begin(), it->members.end(),
                                         [&](const juce::String& uuid) { return alive.find(uuid) == alive.end(); }),
                          it->members.end());
        if (it->members.size() != before)
            changed = true;

        if (it->members.empty()) {
            it = macros_.erase(it);
            changed = true;
        } else {
            ++it;
        }
    }

    return changed;
}

juce::var MacroSet::toVar() const {
    juce::Array<juce::var> arr;
    for (const auto& m : macros_) {
        auto obj = new juce::DynamicObject();
        obj->setProperty("id", m.id);
        obj->setProperty("name", m.name);
        obj->setProperty("colour", m.colour.toString());
        obj->setProperty("collapsed", m.collapsed);

        auto boundsObj = new juce::DynamicObject();
        boundsObj->setProperty("x", m.bounds.getX());
        boundsObj->setProperty("y", m.bounds.getY());
        boundsObj->setProperty("w", m.bounds.getWidth());
        boundsObj->setProperty("h", m.bounds.getHeight());
        obj->setProperty("bounds", juce::var(boundsObj));

        juce::Array<juce::var> memberArr;
        for (const auto& uuid : m.members)
            memberArr.add(uuid);
        obj->setProperty("members", memberArr);

        arr.add(juce::var(obj));
    }
    return arr;
}

bool MacroSet::fromVar(const juce::var& state) {
    // Not an array at all -> reject rather than silently treating it as "no macros"; an absent
    // "macros" key (handled by the caller, same as TimelineDoc's absent "timeline") is the only
    // legitimate way to end up with none.
    auto* arr = state.getArray();
    if (arr == nullptr)
        return false;

    std::vector<Macro> parsed;
    std::set<juce::String> seenIds;
    std::set<juce::String> seenMembers;

    for (const auto& entryVar : *arr) {
        auto* obj = entryVar.getDynamicObject();
        if (obj == nullptr)
            return false;

        Macro m;
        m.id = obj->getProperty("id").toString();
        if (m.id.isEmpty() || !seenIds.insert(m.id).second)
            return false; // empty or duplicate id

        m.name = obj->getProperty("name").toString();
        m.colour = juce::Colour::fromString(obj->getProperty("colour").toString());
        m.collapsed = (bool)obj->getProperty("collapsed");

        if (auto* boundsObj = obj->getProperty("bounds").getDynamicObject()) {
            m.bounds = {(int)boundsObj->getProperty("x"), (int)boundsObj->getProperty("y"),
                        (int)boundsObj->getProperty("w"), (int)boundsObj->getProperty("h")};
        }

        auto* memberArr = obj->getProperty("members").getArray();
        if (memberArr == nullptr || memberArr->isEmpty())
            return false; // an empty macro is not a representable state — see removeMemberEverywhere

        for (const auto& memberVar : *memberArr) {
            const juce::String uuid = memberVar.toString();
            if (uuid.isEmpty() || !seenMembers.insert(uuid).second)
                return false; // empty uuid, or claimed by more than one macro (flat model)
            m.members.push_back(uuid);
        }

        parsed.push_back(std::move(m));
    }

    macros_ = std::move(parsed);
    return true;
}

} // namespace synth
