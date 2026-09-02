#include "SnippetManager.h"
#include "AI/AIStateMapper.h"
#include "Branding.h"
#include "Modules/AttenuverterModule.h"
#include "Modules/AudioInputModule.h"
#include <limits>
#include <map>
#include <set>
#include <utility>

namespace synth {

namespace {

using IOProcessor = juce::AudioProcessorGraph::AudioGraphIOProcessor;

/** Nodes that must never be captured into a snippet:
 *   - graph I/O (Audio In/Out, MIDI In) — singletons that already exist in the target patch,
 *     so copying them in would duplicate the output bus;
 *   - Attenuverters — stored as `modulations` entries instead (see SnippetManager docs).
 */
bool isSnippetEligible(juce::AudioProcessor* processor) {
    if (processor == nullptr)
        return false;
    if (dynamic_cast<IOProcessor*>(processor) != nullptr)
        return false;
    // Audio Input is a ModuleBase now, so the IO-processor test above no longer catches it —
    // but it is still a singleton, and a snippet carrying one would insert a second Audio Input
    // node into any patch it lands in.
    if (dynamic_cast<AudioInputModule*>(processor) != nullptr)
        return false;
    if (dynamic_cast<AttenuverterModule*>(processor) != nullptr)
        return false;
    return true;
}

int intProperty(const juce::DynamicObject* obj, const char* key, int fallback = 0) {
    if (obj == nullptr || !obj->hasProperty(key))
        return fallback;
    auto value = obj->getProperty(key);
    if (value.isVoid())
        return fallback;
    return (int)value;
}

/** Reads a node entry's stored position. graphToJSON copies node->properties["x"/"y"] straight
 *  through, and those are void for a node that has never been placed — treat that as (0,0). */
juce::Point<int> readPosition(const juce::DynamicObject* nodeObj) {
    if (nodeObj == nullptr || !nodeObj->hasProperty("position"))
        return {};
    if (auto* posObj = nodeObj->getProperty("position").getDynamicObject())
        return {intProperty(posObj, "x"), intProperty(posObj, "y")};
    return {};
}

juce::var makePosition(juce::Point<int> p) {
    juce::DynamicObject::Ptr obj = new juce::DynamicObject();
    obj->setProperty("x", p.x);
    obj->setProperty("y", p.y);
    return juce::var(obj.get());
}

const juce::Array<juce::var>* arrayProperty(const juce::DynamicObject* obj, const char* key) {
    if (obj == nullptr || !obj->hasProperty(key))
        return nullptr;
    return obj->getProperty(key).getArray();
}

struct SnippetNameComparator {
    static int compareElements(const SnippetInfo& a, const SnippetInfo& b) { return a.name.compareIgnoreCase(b.name); }
};

} // namespace

// ---------------------------------------------------------------------------------------
// Storage location
// ---------------------------------------------------------------------------------------

juce::File SnippetManager::getDefaultSnippetsDirectory() {
    juce::File folder = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                            .getChildFile(synth::branding::kSettingsFolderName)
                            .getChildFile("Snippets");

    if (!folder.exists())
        folder.createDirectory();

    return folder;
}

// ---------------------------------------------------------------------------------------
// Extraction
// ---------------------------------------------------------------------------------------

juce::Point<int> SnippetManager::selectionOrigin(juce::AudioProcessorGraph& graph,
                                                 const std::vector<NodeID>& selection) {
    int minX = std::numeric_limits<int>::max();
    int minY = std::numeric_limits<int>::max();

    for (auto id : selection) {
        auto* node = graph.getNodeForId(id);
        if (node == nullptr || !isSnippetEligible(node->getProcessor()))
            continue;

        // graphToJSON copies these straight through, and they are void for a node that has never
        // been placed — the same "treat that as 0" rule readPosition applies.
        const int x = node->properties["x"].isVoid() ? 0 : (int)node->properties["x"];
        const int y = node->properties["y"].isVoid() ? 0 : (int)node->properties["y"];
        minX = juce::jmin(minX, x);
        minY = juce::jmin(minY, y);
    }

    if (minX == std::numeric_limits<int>::max())
        return {};
    return {minX, minY};
}

juce::var SnippetManager::extractSnippet(juce::AudioProcessorGraph& graph, const std::vector<NodeID>& selection,
                                         const juce::String& name, bool includeExtraState, const MacroSet& macros) {
    // Resolve the selection to the uids actually eligible for capture.
    std::set<int> keep;
    for (auto id : selection) {
        auto* node = graph.getNodeForId(id);
        if (node == nullptr)
            continue;
        if (isSnippetEligible(node->getProcessor()))
            keep.insert((int)id.uid);
    }

    // Serialise the whole graph once and filter it, rather than re-deriving the per-node JSON
    // shape here — graphToJSON is the single source of truth for that shape (params encoding,
    // MIDI port sentinel, attenuverter → modulations folding) and must not be duplicated.
    auto full = AIStateMapper::graphToJSON(graph);
    auto* fullObj = full.getDynamicObject();

    juce::DynamicObject::Ptr root = new juce::DynamicObject();
    root->setProperty("name", name);

    juce::Array<juce::var> nodes;

    // The selection's top-left corner, so positions can be made origin-relative. Shared with the
    // clipboard, which needs the same corner to place a paste relative to its source.
    const auto origin = selectionOrigin(graph, selection);

    if (auto* nodeList = arrayProperty(fullObj, "nodes")) {
        for (const auto& nVar : *nodeList) {
            auto* nObj = nVar.getDynamicObject();
            if (nObj == nullptr || keep.count(intProperty(nObj, "id", -1)) == 0)
                continue;

            juce::DynamicObject::Ptr n = new juce::DynamicObject();
            n->setProperty("id", intProperty(nObj, "id"));
            n->setProperty("type", nObj->getProperty("type"));
            if (nObj->hasProperty("params"))
                n->setProperty("params", nObj->getProperty("params"));
            if (includeExtraState && nObj->hasProperty("state"))
                n->setProperty("state", nObj->getProperty("state"));
            auto pos = readPosition(nObj);
            n->setProperty("position", makePosition({pos.x - origin.x, pos.y - origin.y}));
            nodes.add(juce::var(n.get()));
        }
    }
    root->setProperty("nodes", nodes);

    // Only wires wholly inside the selection. Hops through an Attenuverter drop out for free
    // here, because attenuverters are never in `keep` — the routing is re-expressed below.
    juce::Array<juce::var> connections;
    if (auto* connList = arrayProperty(fullObj, "connections")) {
        for (const auto& cVar : *connList) {
            auto* cObj = cVar.getDynamicObject();
            if (cObj == nullptr)
                continue;
            if (keep.count(intProperty(cObj, "src", -1)) == 0 || keep.count(intProperty(cObj, "dst", -1)) == 0)
                continue;

            juce::DynamicObject::Ptr c = new juce::DynamicObject();
            c->setProperty("src", intProperty(cObj, "src"));
            c->setProperty("srcPort", intProperty(cObj, "srcPort"));
            c->setProperty("dst", intProperty(cObj, "dst"));
            c->setProperty("dstPort", intProperty(cObj, "dstPort"));
            c->setProperty("isMidi", cObj->getProperty("isMidi"));
            connections.add(juce::var(c.get()));
        }
    }
    root->setProperty("connections", connections);

    juce::Array<juce::var> modulations;
    if (auto* modList = arrayProperty(fullObj, "modulations")) {
        for (const auto& mVar : *modList) {
            auto* mObj = mVar.getDynamicObject();
            if (mObj == nullptr)
                continue;
            if (keep.count(intProperty(mObj, "source", -1)) == 0 || keep.count(intProperty(mObj, "dest", -1)) == 0)
                continue;

            juce::DynamicObject::Ptr m = new juce::DynamicObject();
            m->setProperty("source", intProperty(mObj, "source"));
            m->setProperty("sourcePort", intProperty(mObj, "sourcePort"));
            m->setProperty("dest", intProperty(mObj, "dest"));
            m->setProperty("destPort", intProperty(mObj, "destPort"));
            if (mObj->hasProperty("amount"))
                m->setProperty("amount", mObj->getProperty("amount"));
            if (mObj->hasProperty("bypass"))
                m->setProperty("bypass", mObj->getProperty("bypass"));
            modulations.add(juce::var(m.get()));
        }
    }
    root->setProperty("modulations", modulations);

    // Macros (P8-12): capture only a macro whose FULL membership is inside `keep` — the same
    // self-contained rule connections/modulations already follow. Membership is emitted using
    // this snippet's own node ids (matching "nodes" above), not uuids: those ids get renumbered
    // on insert, uuids don't exist yet for a macro captured into a still-unsaved snippet var.
    if (!macros.empty()) {
        // uuid -> original node id, so a macro's uuid-keyed membership can be tested against `keep`.
        std::map<juce::String, int> idForUuid;
        for (auto* node : graph.getNodes()) {
            const juce::String uuid = node->properties["uuid"].toString();
            if (uuid.isNotEmpty() && keep.count((int)node->nodeID.uid) > 0)
                idForUuid[uuid] = (int)node->nodeID.uid;
        }

        juce::Array<juce::var> macrosArr;
        for (const auto& macro : macros.getAll()) {
            std::vector<int> memberIds;
            bool fullyContained = true;
            for (const auto& uuid : macro.members) {
                auto it = idForUuid.find(uuid);
                if (it == idForUuid.end()) {
                    fullyContained = false;
                    break;
                }
                memberIds.push_back(it->second);
            }
            if (!fullyContained || memberIds.empty())
                continue;

            juce::DynamicObject::Ptr m = new juce::DynamicObject();
            m->setProperty("name", macro.name);
            m->setProperty("colour", macro.colour.toString());
            m->setProperty("collapsed", macro.collapsed);
            const auto relPos = macro.bounds.getPosition() - origin;
            juce::DynamicObject::Ptr boundsObj = new juce::DynamicObject();
            boundsObj->setProperty("x", relPos.x);
            boundsObj->setProperty("y", relPos.y);
            boundsObj->setProperty("w", macro.bounds.getWidth());
            boundsObj->setProperty("h", macro.bounds.getHeight());
            m->setProperty("bounds", juce::var(boundsObj.get()));
            juce::Array<juce::var> memberArr;
            for (int id : memberIds)
                memberArr.add(id);
            m->setProperty("members", memberArr);
            macrosArr.add(juce::var(m.get()));
        }
        if (!macrosArr.isEmpty())
            root->setProperty("macros", macrosArr);
    }

    return juce::var(root.get());
}

// ---------------------------------------------------------------------------------------
// Insert preparation
// ---------------------------------------------------------------------------------------

juce::uint32 SnippetManager::nextFreeIdBase(const juce::AudioProcessorGraph& graph) {
    juce::uint32 maxUid = 0;
    for (auto* node : graph.getNodes())
        maxUid = juce::jmax(maxUid, node->nodeID.uid);
    return maxUid + 1;
}

juce::var SnippetManager::prepareForInsert(const juce::var& snippet, juce::Point<int> dropPos, juce::uint32 idBase,
                                           bool includeExtraState) {
    juce::DynamicObject::Ptr root = new juce::DynamicObject();
    auto* src = snippet.getDynamicObject();

    // NodeID{0} is the graph's invalid-node sentinel, so ids must start at 1.
    juce::uint32 next = juce::jmax<juce::uint32>(1, idBase);
    std::map<int, int> idMap;

    juce::Array<juce::var> nodes;
    if (auto* nodeList = arrayProperty(src, "nodes")) {
        for (const auto& nVar : *nodeList) {
            auto* nObj = nVar.getDynamicObject();
            if (nObj == nullptr || !nObj->hasProperty("id") || !nObj->hasProperty("type"))
                continue;

            const int oldId = intProperty(nObj, "id", -1);
            if (oldId < 0 || idMap.count(oldId) > 0)
                continue; // malformed or duplicate id — drop rather than alias two nodes together

            const int newId = (int)next++;
            idMap[oldId] = newId;

            juce::DynamicObject::Ptr n = new juce::DynamicObject();
            n->setProperty("id", newId);
            n->setProperty("type", nObj->getProperty("type"));
            if (nObj->hasProperty("params"))
                n->setProperty("params", nObj->getProperty("params"));
            // Dropped unless explicitly asked for: applyJSONToGraph honours `state` on the trusted
            // path, and a snippet read off disk must not be able to carry one.
            if (includeExtraState && nObj->hasProperty("state"))
                n->setProperty("state", nObj->getProperty("state"));
            auto pos = readPosition(nObj);
            n->setProperty("position", makePosition({pos.x + dropPos.x, pos.y + dropPos.y}));
            nodes.add(juce::var(n.get()));
        }
    }
    root->setProperty("nodes", nodes);

    juce::Array<juce::var> connections;
    if (auto* connList = arrayProperty(src, "connections")) {
        for (const auto& cVar : *connList) {
            auto* cObj = cVar.getDynamicObject();
            if (cObj == nullptr)
                continue;
            auto srcIt = idMap.find(intProperty(cObj, "src", -1));
            auto dstIt = idMap.find(intProperty(cObj, "dst", -1));
            if (srcIt == idMap.end() || dstIt == idMap.end())
                continue; // references a node this snippet doesn't carry

            juce::DynamicObject::Ptr c = new juce::DynamicObject();
            c->setProperty("src", srcIt->second);
            c->setProperty("srcPort", intProperty(cObj, "srcPort"));
            c->setProperty("dst", dstIt->second);
            c->setProperty("dstPort", intProperty(cObj, "dstPort"));
            c->setProperty("isMidi", cObj->getProperty("isMidi"));
            connections.add(juce::var(c.get()));
        }
    }
    root->setProperty("connections", connections);

    juce::Array<juce::var> modulations;
    if (auto* modList = arrayProperty(src, "modulations")) {
        for (const auto& mVar : *modList) {
            auto* mObj = mVar.getDynamicObject();
            if (mObj == nullptr)
                continue;
            auto srcIt = idMap.find(intProperty(mObj, "source", -1));
            auto dstIt = idMap.find(intProperty(mObj, "dest", -1));
            if (srcIt == idMap.end() || dstIt == idMap.end())
                continue;

            juce::DynamicObject::Ptr m = new juce::DynamicObject();
            m->setProperty("source", srcIt->second);
            m->setProperty("sourcePort", intProperty(mObj, "sourcePort"));
            m->setProperty("dest", dstIt->second);
            m->setProperty("destPort", intProperty(mObj, "destPort"));
            if (mObj->hasProperty("amount"))
                m->setProperty("amount", mObj->getProperty("amount"));
            if (mObj->hasProperty("bypass"))
                m->setProperty("bypass", mObj->getProperty("bypass"));
            modulations.add(juce::var(m.get()));
        }
    }
    root->setProperty("modulations", modulations);

    // Macros (P8-12): remap membership through the same idMap node ids were just renumbered
    // through. A member id this snippet doesn't carry (shouldn't happen — extractSnippet only
    // ever captures a fully-contained macro — but a hand-edited .agsnip could claim one) drops
    // that member; a macro left with fewer than two members this way is dropped outright, since
    // a stray single-member "group" is meaningless.
    if (auto* macrosList = arrayProperty(src, "macros")) {
        juce::Array<juce::var> macrosArr;
        for (const auto& macroVar : *macrosList) {
            auto* macroObj = macroVar.getDynamicObject();
            if (macroObj == nullptr)
                continue;

            auto* memberList = arrayProperty(macroObj, "members");
            if (memberList == nullptr)
                continue;

            juce::Array<juce::var> newMembers;
            for (const auto& memberVar : *memberList) {
                auto it = idMap.find((int)memberVar);
                if (it != idMap.end())
                    newMembers.add(it->second);
            }
            if (newMembers.size() < 2)
                continue;

            juce::DynamicObject::Ptr m = new juce::DynamicObject();
            m->setProperty("name", macroObj->getProperty("name"));
            m->setProperty("colour", macroObj->getProperty("colour"));
            m->setProperty("collapsed", macroObj->getProperty("collapsed"));
            if (auto* boundsObj = macroObj->getProperty("bounds").getDynamicObject()) {
                juce::DynamicObject::Ptr newBounds = new juce::DynamicObject();
                newBounds->setProperty("x", intProperty(boundsObj, "x") + dropPos.x);
                newBounds->setProperty("y", intProperty(boundsObj, "y") + dropPos.y);
                newBounds->setProperty("w", boundsObj->getProperty("w"));
                newBounds->setProperty("h", boundsObj->getProperty("h"));
                m->setProperty("bounds", juce::var(newBounds.get()));
            }
            m->setProperty("members", newMembers);
            macrosArr.add(juce::var(m.get()));
        }
        if (!macrosArr.isEmpty())
            root->setProperty("macros", macrosArr);
    }

    return juce::var(root.get());
}

// ---------------------------------------------------------------------------------------
// Insertion
// ---------------------------------------------------------------------------------------

std::vector<SnippetManager::NodeID> SnippetManager::insertSnippet(const juce::var& snippet,
                                                                  juce::AudioProcessorGraph& graph,
                                                                  juce::Point<int> dropPos, bool includeExtraState,
                                                                  std::vector<Macro>* outMacros) {
    auto prepared = prepareForInsert(snippet, dropPos, nextFreeIdBase(graph), includeExtraState);

    auto* preparedObj = prepared.getDynamicObject();
    auto* preparedNodes = arrayProperty(preparedObj, "nodes");
    if (preparedNodes == nullptr || preparedNodes->isEmpty())
        return {};

    // A snippet is a file on disk, so it can be hand-edited, truncated or copied in from
    // elsewhere: run the STRICT (untrusted) validation first so a malformed snippet is rejected
    // whole rather than applied halfway.
    //
    // The apply itself then runs on the TRUSTED path, and that pairing is deliberate. The
    // untrusted apply path carries a heuristic for AI-authored patches: when a parameter's range
    // extends beyond [0,1] but the incoming value sits inside it, the value is treated as
    // normalised and rescaled. Snippet values come from graphToJSON and are already denormalised,
    // so that heuristic would silently corrupt any legitimate small value (a 0.5 Hz LFO rate
    // would land as several Hz). Validate strictly, apply faithfully.
    //
    // allowInternalModuleTypes: a snippet is our own graphToJSON output, so it may name internal
    // module types the AI is barred from authoring. That restriction guards against a MODEL
    // reaching for them; it is not a property of app-authored files, and applying it here would
    // reject legitimate snippets.
    //
    // validatePatch's untrusted path refuses ANY payload carrying a "macros" key (see
    // AIStateMapper.cpp) - including our own snippet's, since that refusal can't distinguish a
    // patch suggestion smuggling one in from a snippet that legitimately carries one. Strip it for
    // validation only; applyJSONToGraph never reads "macros" and the resolution below reads
    // `preparedObj` directly, so restoring it afterwards is all that's needed.
    juce::var strippedMacros;
    const bool hadMacros = preparedObj->hasProperty("macros");
    if (hadMacros) {
        strippedMacros = preparedObj->getProperty("macros");
        preparedObj->removeProperty("macros");
    }
    auto validation = AIStateMapper::validatePatch(prepared, graph, /*clearExisting=*/false, /*trusted=*/false,
                                                   /*allowInternalModuleTypes=*/true);
    if (hadMacros)
        preparedObj->setProperty("macros", strippedMacros);
    if (!validation.ok) {
        juce::Logger::writeToLog("SnippetManager::insertSnippet: snippet rejected - " + validation.message);
        return {};
    }

    std::set<juce::uint32> before;
    for (auto* node : graph.getNodes())
        before.insert(node->nodeID.uid);

    // autoConnectNewNodes=false is essential, not incidental. Merge mode otherwise wires every new
    // audio node with no outgoing wire straight to Audio Output (and every MIDI-accepting node to an
    // existing MIDI source) as a convenience for AI-authored patches. A snippet is an exact
    // sub-graph: the wires it does NOT have are as deliberate as the ones it does, and those
    // convenience connections would splice the inserted group into the surrounding patch.
    //
    // idMap: this snippet's own node ids (the "id" prepareForInsert assigned, also what a
    // "macros" entry's members reference) -> the real NodeID applyJSONToGraph actually created.
    // Needed because merge-mode apply does not honour the requested id (see below).
    std::map<int, juce::AudioProcessorGraph::NodeID> idMap;
    if (!AIStateMapper::applyJSONToGraph(prepared, graph, /*clearExisting=*/false, /*trusted=*/true,
                                         /*autoConnectNewNodes=*/false, &idMap))
        return {};

    // Diff rather than trusting the requested ids: merge-mode apply assigns its own ids.
    std::vector<NodeID> added;
    for (auto* node : graph.getNodes()) {
        if (before.count(node->nodeID.uid) > 0)
            continue;
        if (!isSnippetEligible(node->getProcessor()))
            continue; // attenuverters rebuilt for modulations aren't user-selectable
        added.push_back(node->nodeID);
    }

    if (outMacros != nullptr) {
        if (auto* macrosList = arrayProperty(preparedObj, "macros")) {
            for (const auto& macroVar : *macrosList) {
                auto* macroObj = macroVar.getDynamicObject();
                if (macroObj == nullptr)
                    continue;
                auto* memberList = arrayProperty(macroObj, "members");
                if (memberList == nullptr)
                    continue;

                Macro macro;
                macro.name = macroObj->getProperty("name").toString();
                macro.colour = juce::Colour::fromString(macroObj->getProperty("colour").toString());
                macro.collapsed = (bool)macroObj->getProperty("collapsed");
                if (auto* boundsObj = macroObj->getProperty("bounds").getDynamicObject()) {
                    macro.bounds = {intProperty(boundsObj, "x"), intProperty(boundsObj, "y"),
                                    intProperty(boundsObj, "w"), intProperty(boundsObj, "h")};
                }
                for (const auto& memberVar : *memberList) {
                    auto it = idMap.find((int)memberVar);
                    if (it == idMap.end())
                        continue;
                    auto* node = graph.getNodeForId(it->second);
                    if (node == nullptr)
                        continue;
                    // A freshly pasted node has no "uuid" yet — applyJSONToGraph only adopts one
                    // from the incoming JSON when trusted AND the JSON carried one (it never does
                    // for a snippet node, see extractSnippet), so generate it here rather than
                    // reading an empty property. ensureNodeUuid is the same lazy-generation
                    // graphToJSON itself uses, exposed for exactly this "just created it" case.
                    const juce::String uuid = AIStateMapper::ensureNodeUuid(node);
                    if (uuid.isNotEmpty())
                        macro.members.push_back(uuid);
                }
                if (macro.members.size() >= 2)
                    outMacros->push_back(std::move(macro));
            }
        }
    }

    return added;
}

// ---------------------------------------------------------------------------------------
// Metadata
// ---------------------------------------------------------------------------------------

int SnippetManager::getModuleCount(const juce::var& snippet) {
    if (auto* nodeList = arrayProperty(snippet.getDynamicObject(), "nodes"))
        return nodeList->size();
    return 0;
}

juce::String SnippetManager::getSnippetName(const juce::var& snippet) {
    if (auto* obj = snippet.getDynamicObject()) {
        if (obj->hasProperty("name"))
            return obj->getProperty("name").toString();
    }
    return {};
}

// ---------------------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------------------

juce::String SnippetManager::sanitiseName(const juce::String& raw) {
    // createLegalFileName strips path separators and other characters the filesystem rejects;
    // the name doubles as the on-disk filename, so a user-typed string never reaches the
    // filesystem unfiltered.
    auto cleaned = juce::File::createLegalFileName(raw.trim());

    // Leading dots would produce a hidden file whose snippet is invisible in the sidebar.
    while (cleaned.startsWithChar('.'))
        cleaned = cleaned.substring(1);

    cleaned = cleaned.trim();
    if (cleaned.length() > kMaxNameLength)
        cleaned = cleaned.substring(0, kMaxNameLength).trim();

    return cleaned;
}

juce::File SnippetManager::fileForName(const juce::File& dir, const juce::String& name) {
    auto safe = sanitiseName(name);
    if (safe.isEmpty())
        return {};
    return dir.getChildFile(safe + kFileExtension);
}

bool SnippetManager::saveSnippet(const juce::File& dir, const juce::String& name, const juce::var& snippet) {
    auto safe = sanitiseName(name);
    if (safe.isEmpty())
        return false;
    if (getModuleCount(snippet) <= 0)
        return false;

    if (!dir.exists() && !dir.createDirectory())
        return false;

    // Persist the sanitised name so the sidebar label and the filename can never disagree.
    juce::var stored = snippet;
    if (auto* obj = stored.getDynamicObject())
        obj->setProperty("name", safe);

    auto file = dir.getChildFile(safe + kFileExtension);
    return file.replaceWithText(juce::JSON::toString(stored));
}

juce::var SnippetManager::loadSnippet(const juce::File& file) {
    if (!file.existsAsFile())
        return {};
    auto json = juce::JSON::parse(file.loadFileAsString());
    if (!json.isObject())
        return {};
    return json;
}

juce::Array<SnippetInfo> SnippetManager::listSnippets(const juce::File& dir) {
    juce::Array<SnippetInfo> result;
    if (!dir.isDirectory())
        return result;

    juce::Array<juce::File> files;
    dir.findChildFiles(files, juce::File::findFiles, false, juce::String("*") + kFileExtension);

    for (const auto& file : files) {
        auto json = loadSnippet(file);
        if (!json.isObject())
            continue; // unreadable/corrupt file: skip it rather than surface a broken row

        SnippetInfo info;
        info.file = file;
        info.name = getSnippetName(json);
        if (info.name.isEmpty())
            info.name = file.getFileNameWithoutExtension();
        info.moduleCount = getModuleCount(json);
        result.add(info);
    }

    SnippetNameComparator comparator;
    result.sort(comparator);

    return result;
}

bool SnippetManager::deleteSnippet(const juce::File& dir, const juce::String& name) {
    auto file = fileForName(dir, name);
    if (file == juce::File() || !file.existsAsFile())
        return false;
    return file.deleteFile();
}

juce::String SnippetManager::nameFromPayload(const juce::String& payload) {
    if (!isSnippetPayload(payload))
        return {};
    return payload.substring((int)juce::String(kPayloadPrefix).length());
}

} // namespace synth
