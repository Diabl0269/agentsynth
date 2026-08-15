#include "PatchDiff.h"
#include "../Modules/ModuleBase.h"
#include "AIStateMapper.h"
#include <algorithm>
#include <cmath>
#include <juce_audio_processors/juce_audio_processors.h>
#include <map>
#include <memory>
#include <tuple>
#include <unordered_map>

namespace synth {

namespace {

constexpr double kNumericTolerance = 1.0e-4;

struct NodeEntry {
    juce::String uuid;
    juce::String type;
    const juce::DynamicObject* params = nullptr;
};

// Per-snapshot lookup: graphToJSON's integer node "id" is only meaningful within the snapshot it
// came from (a fresh scratch-graph rebuild assigns unrelated ids to brand-new nodes), so
// connection/modulation endpoints must be resolved to uuid through THIS map before comparing
// across before/after.
struct SnapshotIndex {
    std::unordered_map<int, juce::String> idToUuid;
    std::map<juce::String, NodeEntry> byUuid;
};

SnapshotIndex indexSnapshot(const juce::var& snapshot) {
    SnapshotIndex idx;
    auto* root = snapshot.getDynamicObject();
    if (root == nullptr)
        return idx;
    auto* nodes = root->getProperty("nodes").getArray();
    if (nodes == nullptr)
        return idx;

    for (const auto& nVar : *nodes) {
        auto* nObj = nVar.getDynamicObject();
        if (nObj == nullptr)
            continue;
        juce::String uuid = nObj->getProperty("uuid").toString();
        if (uuid.isEmpty())
            continue;

        int id = (int)nObj->getProperty("id");
        idx.idToUuid[id] = uuid;

        NodeEntry entry;
        entry.uuid = uuid;
        entry.type = nObj->getProperty("type").toString();
        entry.params = nObj->getProperty("params").getDynamicObject();
        idx.byUuid[uuid] = entry;
    }
    return idx;
}

juce::String resolveUuid(const SnapshotIndex& idx, int id) {
    auto it = idx.idToUuid.find(id);
    return it == idx.idToUuid.end() ? juce::String() : it->second;
}

bool isAttenuverterType(const juce::String& type) { return type == "Attenuverter"; }

// ~3 significant figures, no unit-formatting table exists in this codebase (confirmed against
// ParametricEQModule's getText()) so raw numeric values are shown without units.
juce::String formatSigFigs(double v, int sig = 3) {
    if (!std::isfinite(v))
        return juce::String(v);
    if (v == 0.0)
        return "0";

    double av = std::abs(v);
    int magnitude = (int)std::floor(std::log10(av));
    int decimals = juce::jlimit(0, 6, sig - 1 - magnitude);

    juce::String s(v, decimals);
    if (s.containsChar('.')) {
        while (s.endsWithChar('0'))
            s = s.dropLastCharacters(1);
        if (s.endsWithChar('.'))
            s = s.dropLastCharacters(1);
    }
    return s;
}

juce::String describeVar(const juce::var& v) {
    if (v.isBool())
        return (bool)v ? "true" : "false";
    if (v.isString())
        return v.toString();
    if (v.isDouble() || v.isInt() || v.isInt64())
        return formatSigFigs((double)v);
    return v.toString();
}

bool varsEqual(const juce::var& a, const juce::var& b) {
    if (a.isBool() || b.isBool())
        return (bool)a == (bool)b;
    if (a.isString() || b.isString())
        return a.toString() == b.toString();
    if ((a.isDouble() || a.isInt() || a.isInt64()) && (b.isDouble() || b.isInt() || b.isInt64()))
        return std::abs((double)a - (double)b) <= kNumericTolerance;
    return a == b;
}

// Lazily-instantiated, never-processed processors, one per node type, purely to read parameter
// display names and modulation-target names. Headless (no GUI, no audio device), same pattern as
// AIStateMapper::validateNodeParams's "probe" processors.
class TypeMetadataCache {
public:
    juce::String paramDisplayName(const juce::String& type, const juce::String& paramId) {
        if (auto* proc = get(type))
            if (auto* p = findParameterByID(proc, paramId))
                return p->name;
        return paramId;
    }

    juce::String modTargetName(const juce::String& type, int channel) {
        if (auto* proc = get(type))
            if (auto* mb = dynamic_cast<ModuleBase*>(proc))
                for (const auto& t : mb->getModulationTargets())
                    if (t.channelIndex == channel)
                        return t.name;
        return {};
    }

private:
    juce::AudioProcessor* get(const juce::String& type) {
        auto it = cache.find(type);
        if (it != cache.end())
            return it->second.get();
        auto processor = AIStateMapper::createModule(type);
        auto* raw = processor.get();
        cache[type] = std::move(processor);
        return raw;
    }

    std::map<juce::String, std::unique_ptr<juce::AudioProcessor>> cache;
};

// src uuid, srcPort, dst uuid, dstPort, isMidi
using ConnKey = std::tuple<juce::String, int, juce::String, int, bool>;

struct ConnRecord {
    ConnKey key;
    juce::String srcType, dstType;
};

std::vector<ConnRecord> collectConnections(const juce::var& snapshot, const SnapshotIndex& idx) {
    std::vector<ConnRecord> out;
    auto* root = snapshot.getDynamicObject();
    if (root == nullptr)
        return out;
    auto* conns = root->getProperty("connections").getArray();
    if (conns == nullptr)
        return out;

    for (const auto& cVar : *conns) {
        auto* cObj = cVar.getDynamicObject();
        if (cObj == nullptr)
            continue;

        juce::String srcUuid = resolveUuid(idx, (int)cObj->getProperty("src"));
        juce::String dstUuid = resolveUuid(idx, (int)cObj->getProperty("dst"));
        if (srcUuid.isEmpty() || dstUuid.isEmpty())
            continue;

        juce::String srcType, dstType;
        if (auto it = idx.byUuid.find(srcUuid); it != idx.byUuid.end())
            srcType = it->second.type;
        if (auto it = idx.byUuid.find(dstUuid); it != idx.byUuid.end())
            dstType = it->second.type;

        // Attenuverter plumbing is reported exclusively via modulations[]; suppress it here so a
        // modulation doesn't ALSO show up as a raw node + two connections.
        if (isAttenuverterType(srcType) || isAttenuverterType(dstType))
            continue;

        ConnRecord rec;
        rec.key = {srcUuid, (int)cObj->getProperty("srcPort"), dstUuid, (int)cObj->getProperty("dstPort"),
                   (bool)cObj->getProperty("isMidi")};
        rec.srcType = srcType;
        rec.dstType = dstType;
        out.push_back(rec);
    }
    return out;
}

// src uuid, srcPort, dst uuid, dstPort
using ModKey = std::tuple<juce::String, int, juce::String, int>;

struct ModRecord {
    ModKey key;
    juce::String srcType, dstType;
    double amount = 1.0;
    bool bypass = false;
};

std::vector<ModRecord> collectModulations(const juce::var& snapshot, const SnapshotIndex& idx) {
    std::vector<ModRecord> out;
    auto* root = snapshot.getDynamicObject();
    if (root == nullptr)
        return out;
    auto* mods = root->getProperty("modulations").getArray();
    if (mods == nullptr)
        return out;

    for (const auto& mVar : *mods) {
        auto* mObj = mVar.getDynamicObject();
        if (mObj == nullptr)
            continue;

        int srcPort = mObj->hasProperty("sourcePort") ? (int)mObj->getProperty("sourcePort") : 0;
        int dstPort = (int)mObj->getProperty("destPort");
        juce::String srcUuid = resolveUuid(idx, (int)mObj->getProperty("source"));
        juce::String dstUuid = resolveUuid(idx, (int)mObj->getProperty("dest"));
        if (srcUuid.isEmpty() || dstUuid.isEmpty())
            continue;

        ModRecord rec;
        rec.key = {srcUuid, srcPort, dstUuid, dstPort};
        if (auto it = idx.byUuid.find(srcUuid); it != idx.byUuid.end())
            rec.srcType = it->second.type;
        if (auto it = idx.byUuid.find(dstUuid); it != idx.byUuid.end())
            rec.dstType = it->second.type;
        rec.amount = mObj->hasProperty("amount") ? (double)mObj->getProperty("amount") : 1.0;
        rec.bypass = mObj->hasProperty("bypass") ? (bool)mObj->getProperty("bypass") : false;
        out.push_back(rec);
    }
    return out;
}

} // namespace

juce::String PatchChange::describe() const {
    switch (kind) {
    case Kind::NodeAdded:
        return "+ " + nodeType;
    case Kind::NodeRemoved:
        return "- " + nodeType;
    case Kind::ParamChanged:
        return nodeType + ": " + paramName + " " + beforeText + " -> " + afterText;
    case Kind::ConnectionAdded:
        return "+ connect " + srcType + (isMidi ? " (MIDI) -> " : " -> ") + dstType;
    case Kind::ConnectionRemoved:
        return "- connect " + srcType + (isMidi ? " (MIDI) -> " : " -> ") + dstType;
    case Kind::ModulationAdded:
        return "+ mod " + srcType + " -> " + dstType +
               (destParamName.isNotEmpty() ? (" " + destParamName) : juce::String());
    case Kind::ModulationRemoved:
        return "- mod " + srcType + " -> " + dstType +
               (destParamName.isNotEmpty() ? (" " + destParamName) : juce::String());
    }
    return {};
}

std::vector<PatchChange> computeDiff(const juce::var& before, const juce::var& after) {
    std::vector<PatchChange> changes;

    SnapshotIndex beforeIdx = indexSnapshot(before);
    SnapshotIndex afterIdx = indexSnapshot(after);
    TypeMetadataCache meta;

    // --- Nodes + params (matched by uuid; see PatchDiff.h for why not "id") ---
    for (const auto& [uuid, entry] : beforeIdx.byUuid) {
        if (isAttenuverterType(entry.type))
            continue;

        auto afterIt = afterIdx.byUuid.find(uuid);
        if (afterIt == afterIdx.byUuid.end()) {
            PatchChange c;
            c.kind = PatchChange::Kind::NodeRemoved;
            c.nodeType = entry.type;
            c.nodeUuid = uuid;
            changes.push_back(c);
            continue;
        }

        // Matched node: diff "params" contents only. Deliberately NOT "position" (merge-mode
        // patches routinely carry positions for unrelated existing nodes — that would read as
        // "moved" noise) or "state"/"id"/"uuid" (never meaningfully diffable here).
        const auto& afterEntry = afterIt->second;
        std::vector<juce::String> keys;
        if (entry.params != nullptr)
            for (const auto& p : entry.params->getProperties())
                keys.push_back(p.name.toString());
        if (afterEntry.params != nullptr)
            for (const auto& p : afterEntry.params->getProperties()) {
                juce::String k = p.name.toString();
                if (std::find(keys.begin(), keys.end(), k) == keys.end())
                    keys.push_back(k);
            }

        for (const auto& key : keys) {
            juce::var beforeVal = entry.params != nullptr ? entry.params->getProperty(key) : juce::var();
            juce::var afterVal = afterEntry.params != nullptr ? afterEntry.params->getProperty(key) : juce::var();
            if (varsEqual(beforeVal, afterVal))
                continue;

            PatchChange c;
            c.kind = PatchChange::Kind::ParamChanged;
            c.nodeType = entry.type;
            c.nodeUuid = uuid;
            c.paramId = key;
            c.paramName = meta.paramDisplayName(entry.type, key);
            c.beforeValue = beforeVal;
            c.afterValue = afterVal;
            c.beforeText = describeVar(beforeVal);
            c.afterText = describeVar(afterVal);
            changes.push_back(c);
        }
    }

    for (const auto& [uuid, entry] : afterIdx.byUuid) {
        if (isAttenuverterType(entry.type))
            continue;
        if (beforeIdx.byUuid.count(uuid) != 0)
            continue; // matched above

        PatchChange c;
        c.kind = PatchChange::Kind::NodeAdded;
        c.nodeType = entry.type;
        c.nodeUuid = uuid;
        changes.push_back(c);
    }

    // --- Connections ---
    std::map<ConnKey, ConnRecord> beforeConnMap;
    for (auto& c : collectConnections(before, beforeIdx))
        beforeConnMap[c.key] = c;
    std::map<ConnKey, ConnRecord> afterConnMap;
    for (auto& c : collectConnections(after, afterIdx))
        afterConnMap[c.key] = c;

    for (const auto& [key, rec] : beforeConnMap) {
        if (afterConnMap.count(key) != 0)
            continue;
        PatchChange c;
        c.kind = PatchChange::Kind::ConnectionRemoved;
        c.srcType = rec.srcType;
        c.dstType = rec.dstType;
        c.srcPort = std::get<1>(key);
        c.dstPort = std::get<3>(key);
        c.isMidi = std::get<4>(key);
        changes.push_back(c);
    }
    for (const auto& [key, rec] : afterConnMap) {
        if (beforeConnMap.count(key) != 0)
            continue;
        PatchChange c;
        c.kind = PatchChange::Kind::ConnectionAdded;
        c.srcType = rec.srcType;
        c.dstType = rec.dstType;
        c.srcPort = std::get<1>(key);
        c.dstPort = std::get<3>(key);
        c.isMidi = std::get<4>(key);
        changes.push_back(c);
    }

    // --- Modulations ---
    std::map<ModKey, ModRecord> beforeModMap;
    for (auto& m : collectModulations(before, beforeIdx))
        beforeModMap[m.key] = m;
    std::map<ModKey, ModRecord> afterModMap;
    for (auto& m : collectModulations(after, afterIdx))
        afterModMap[m.key] = m;

    auto makeModChange = [&](PatchChange::Kind kind, const ModKey& key, const ModRecord& rec) {
        PatchChange c;
        c.kind = kind;
        c.srcType = rec.srcType;
        c.dstType = rec.dstType;
        c.srcPort = std::get<1>(key);
        c.dstPort = std::get<3>(key);
        c.amount = rec.amount;
        c.bypass = rec.bypass;
        c.destParamName = meta.modTargetName(rec.dstType, std::get<3>(key));
        changes.push_back(c);
    };

    for (const auto& [key, rec] : beforeModMap) {
        auto afterIt = afterModMap.find(key);
        if (afterIt == afterModMap.end()) {
            makeModChange(PatchChange::Kind::ModulationRemoved, key, rec);
        } else if (std::abs(afterIt->second.amount - rec.amount) > kNumericTolerance ||
                   afterIt->second.bypass != rec.bypass) {
            // Same route, different amount/bypass. There is no ModulationChanged kind, so a
            // changed route is reported as its old value removed and its new value added — both
            // amounts stay visible rather than picking one.
            makeModChange(PatchChange::Kind::ModulationRemoved, key, rec);
            makeModChange(PatchChange::Kind::ModulationAdded, key, afterIt->second);
        }
    }
    for (const auto& [key, rec] : afterModMap) {
        if (beforeModMap.count(key) == 0)
            makeModChange(PatchChange::Kind::ModulationAdded, key, rec);
    }

    return changes;
}

} // namespace synth
