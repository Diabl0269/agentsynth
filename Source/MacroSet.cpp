#include "MacroSet.h"
#include <map>
#include <set>
#include <utility>

namespace synth {

juce::var MacroPort::toVar() const {
    auto* obj = new juce::DynamicObject();
    obj->setProperty("nodeUuid", nodeUuid);
    obj->setProperty("isInput", isInput);
    obj->setProperty("name", name);
    obj->setProperty("order", order);
    obj->setProperty("kind", kind == MacroPortKind::Midi ? "midi" : "audioCV");
    // Omitted entirely when unset, matching how "ports" itself is omitted on an older macro —
    // absence, not a sentinel value, is what "no custom colour" looks like on disk.
    if (colour.has_value())
        obj->setProperty("colour", colour->toString()); // same encoding as Macro::colour above
    return juce::var(obj);
}

bool MacroPort::fromVar(const juce::var& v, MacroPort& out) {
    auto* obj = v.getDynamicObject();
    if (obj == nullptr)
        return false;

    MacroPort parsed;
    parsed.nodeUuid = obj->getProperty("nodeUuid").toString();
    if (parsed.nodeUuid.isEmpty())
        return false;

    parsed.isInput = (bool)obj->getProperty("isInput");
    parsed.name = obj->getProperty("name").toString();
    parsed.order = (int)obj->getProperty("order");

    const juce::String kindStr = obj->getProperty("kind").toString();
    if (kindStr == "midi")
        parsed.kind = MacroPortKind::Midi;
    else if (kindStr == "audioCV")
        parsed.kind = MacroPortKind::AudioCV;
    else
        return false; // missing or unrecognised "kind" — reject rather than silently default

    // "colour" is decorative-only (unlike kind/nodeUuid above), so it is the one field here that
    // does NOT follow the all-or-nothing rule: absent (every older save) or present-but-not-a-
    // string both just leave it unset rather than rejecting the whole port over a cosmetic field.
    const juce::var colourVar = obj->getProperty("colour");
    if (colourVar.isString() && colourVar.toString().isNotEmpty())
        parsed.colour = juce::Colour::fromString(colourVar.toString());

    out = std::move(parsed);
    return true;
}

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

juce::String MacroSet::add(Macro macro) {
    if (macro.id.isEmpty())
        macro.id = juce::Uuid().toDashedString();
    const juce::String id = macro.id;
    macros_.push_back(std::move(macro));
    return id;
}

/** Id of `macroId`'s parent; empty if top level or unknown. */
juce::String MacroSet::parentOf(const juce::String& macroId) const {
    const auto* m = find(macroId);
    return m != nullptr ? m->parentId : juce::String();
}

/** Ids of the macros whose parent is `macroId`, in stored order. */
std::vector<juce::String> MacroSet::childrenOf(const juce::String& macroId) const {
    std::vector<juce::String> out;
    if (macroId.isEmpty())
        return out;
    for (const auto& m : macros_)
        if (m.parentId == macroId)
            out.push_back(m.id);
    return out;
}

/** Ancestors of `macroId`, parent first, outermost last; excludes `macroId`. */
std::vector<juce::String> MacroSet::ancestorChain(const juce::String& macroId) const {
    std::vector<juce::String> chain;
    juce::String cur = parentOf(macroId);
    // The size bound is belt and braces: setParent/fromVar never admit a cycle.
    while (cur.isNotEmpty() && find(cur) != nullptr && chain.size() <= macros_.size()) {
        chain.push_back(cur);
        cur = parentOf(cur);
    }
    return chain;
}

/** 0 for a top-level (or unknown) macro, 1 for its child, and so on. */
int MacroSet::depth(const juce::String& macroId) const { return (int)ancestorChain(macroId).size(); }

/** Every node uuid directly in `macroId` or in any macro nested below it. */
std::set<juce::String> MacroSet::descendantMembers(const juce::String& macroId) const {
    std::set<juce::String> out;
    std::vector<juce::String> pending{macroId};
    while (!pending.empty()) {
        const juce::String id = pending.back();
        pending.pop_back();
        if (const auto* m = find(id))
            out.insert(m->members.begin(), m->members.end());
        for (const auto& child : childrenOf(id))
            pending.push_back(child);
    }
    return out;
}

/** Outermost macro containing node `uuid` (top of its owner's chain); empty if in none. */
juce::String MacroSet::outermostOf(const juce::String& uuid) const {
    const auto* owner = findByMember(uuid);
    if (owner == nullptr)
        return {};
    const auto chain = ancestorChain(owner->id);
    return chain.empty() ? owner->id : chain.back();
}

/** Outermost COLLAPSED macro in `uuid`'s owner chain (its card stands in for the node);
 *  empty if the node is in no macro or none in the chain is collapsed. */
juce::String MacroSet::outermostCollapsedAncestorOf(const juce::String& uuid) const {
    const auto* owner = findByMember(uuid);
    if (owner == nullptr)
        return {};
    juce::String result;
    if (owner->collapsed)
        result = owner->id;
    for (const auto& id : ancestorChain(owner->id))
        if (const auto* a = find(id); a != nullptr && a->collapsed)
            result = id; // chain runs inner -> outer, so the last hit is the outermost
    return result;
}

/** `macroId` or any ancestor is collapsed. */
bool MacroSet::isEffectivelyCollapsed(const juce::String& macroId) const {
    const auto* m = find(macroId);
    return m != nullptr && (m->collapsed || !isVisible(macroId));
}

/** No ANCESTOR is collapsed (the macro itself may be: it is then drawn as a card). */
bool MacroSet::isVisible(const juce::String& macroId) const {
    for (const auto& id : ancestorChain(macroId))
        if (const auto* a = find(id); a != nullptr && a->collapsed)
            return false;
    return true;
}

/** Parent `macroId` under `parentId` (empty = top level). False, no change, if either is unknown
 *  or the link would form a cycle. Moves no members. */
bool MacroSet::setParent(const juce::String& macroId, const juce::String& parentId) {
    auto* m = find(macroId);
    if (m == nullptr)
        return false;
    if (parentId.isNotEmpty()) {
        if (parentId == macroId || find(parentId) == nullptr)
            return false;
        for (const auto& ancestor : ancestorChain(parentId))
            if (ancestor == macroId)
                return false; // parentId is already below macroId: would form a cycle
    }
    m->parentId = parentId;
    return true;
}

bool MacroSet::dissolveEmpty() {
    bool changed = false;
    for (bool again = true; again;) {
        again = false;
        for (const auto& m : macros_) {
            if (m.members.empty() && childrenOf(m.id).empty()) {
                remove(m.id);
                again = changed = true;
                break; // macros_ changed; rescan (a parent may now be empty too)
            }
        }
    }
    return changed;
}

bool MacroSet::remove(const juce::String& macroId) {
    const auto* target = find(macroId);
    if (target == nullptr)
        return false;
    const juce::String grandparent = target->parentId;
    for (auto& m : macros_)
        if (m.parentId == macroId)
            m.parentId = grandparent;
    const auto before = macros_.size();
    macros_.erase(std::remove_if(macros_.begin(), macros_.end(), [&](const Macro& m) { return m.id == macroId; }),
                  macros_.end());
    return macros_.size() != before;
}

bool MacroSet::addMember(const juce::String& macroId, const juce::String& memberUuid) {
    auto* macro = find(macroId);
    if (macro == nullptr || macro->hasMember(memberUuid))
        return false;
    macro->members.push_back(memberUuid);
    return true;
}

juce::String MacroSet::removeMemberEverywhere(const juce::String& memberUuid) {
    for (auto it = macros_.begin(); it != macros_.end(); ++it) {
        auto memberIt = std::find(it->members.begin(), it->members.end(), memberUuid);
        if (memberIt == it->members.end())
            continue;

        const juce::String touchedId = it->id;
        it->members.erase(memberIt);
        // A port's nodeUuid is always a member (see Macro::ports) — drop the port descriptor
        // along with the member it fronted, or the invariant no longer holds.
        it->ports.erase(std::remove_if(it->ports.begin(), it->ports.end(),
                                       [&](const MacroPort& p) { return p.nodeUuid == memberUuid; }),
                        it->ports.end());
        dissolveEmpty();
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

        // Same liveness set: a port whose node died is dropped like any other member.
        const auto portsBefore = it->ports.size();
        it->ports.erase(std::remove_if(it->ports.begin(), it->ports.end(),
                                       [&](const MacroPort& p) { return alive.find(p.nodeUuid) == alive.end(); }),
                        it->ports.end());
        if (it->ports.size() != portsBefore)
            changed = true;

        ++it;
    }

    // Now that members are pruned, dissolve bottom-up (a parent emptied of its last child too).
    if (dissolveEmpty())
        changed = true;
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
        // Written only when nested, so a flat set serialises exactly as it did before nesting.
        if (m.parentId.isNotEmpty())
            obj->setProperty("parent", m.parentId);

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

        juce::Array<juce::var> portArr;
        for (const auto& p : m.ports)
            portArr.add(p.toVar());
        obj->setProperty("ports", portArr);

        arr.add(juce::var(obj));
    }
    return arr;
}

namespace {

/** Every parent exists, no chain loops, and each macro has direct members or children. */
bool validateHierarchy(const std::vector<Macro>& macros) {
    std::map<juce::String, const Macro*> byId;
    for (const auto& m : macros)
        byId[m.id] = &m;

    std::set<juce::String> hasChild;
    for (const auto& m : macros) {
        if (m.parentId.isEmpty())
            continue;
        if (byId.find(m.parentId) == byId.end())
            return false; // dangling parent
        hasChild.insert(m.parentId);
    }

    for (const auto& m : macros) {
        if (m.members.empty() && hasChild.count(m.id) == 0)
            return false; // an empty macro is not a representable state — see dissolveEmpty
        const Macro* cur = &m;
        for (size_t steps = 0; !cur->parentId.isEmpty(); ++steps) {
            if (steps > macros.size())
                return false; // cycle
            cur = byId[cur->parentId];
        }
    }
    return true;
}

} // namespace

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
        if (memberArr == nullptr)
            return false;
        // Emptiness (no members AND no children) is checked once every macro is parsed, below.

        if (obj->hasProperty("parent")) {
            const juce::var parentVar = obj->getProperty("parent");
            if (!parentVar.isString() || parentVar.toString().isEmpty())
                return false;
            m.parentId = parentVar.toString();
        }

        for (const auto& memberVar : *memberArr) {
            const juce::String uuid = memberVar.toString();
            if (uuid.isEmpty() || !seenMembers.insert(uuid).second)
                return false; // empty uuid, or claimed by more than one macro (one direct owner each)
            m.members.push_back(uuid);
        }

        // "ports" is optional: every macro saved before this key existed has none, and
        // that must parse as an empty list rather than reject the whole load.
        if (obj->hasProperty("ports")) {
            auto* portsArr = obj->getProperty("ports").getArray();
            if (portsArr == nullptr)
                return false;

            for (const auto& portVar : *portsArr) {
                MacroPort port;
                if (!MacroPort::fromVar(portVar, port))
                    return false;
                m.ports.push_back(std::move(port));
            }

            // A port's nodeUuid must be one of THIS macro's own members (Macro::ports'
            // invariant) — checked after both lists are fully parsed so order in the JSON
            // ("ports" before "members", say) can never matter.
            for (const auto& port : m.ports)
                if (std::find(m.members.begin(), m.members.end(), port.nodeUuid) == m.members.end())
                    return false;
        }

        parsed.push_back(std::move(m));
    }

    if (!validateHierarchy(parsed))
        return false;

    macros_ = std::move(parsed);
    return true;
}

} // namespace synth
