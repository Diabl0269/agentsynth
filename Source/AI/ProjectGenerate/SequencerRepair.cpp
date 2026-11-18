#include "AI/ProjectGenerate/SequencerRepair.h"
#include "AI/ProjectGenerate/SongLayout.h"
#include <set>

namespace synth::projectgenerate {

namespace {

bool isSequencerType(const juce::var& node) {
    const juce::String type = node["type"].toString();
    return type == "Sequencer" || type == "Poly Sequencer";
}

bool isNumber(const juce::var& v) { return v.isInt() || v.isInt64() || v.isDouble(); }

// Ids compare as numbers; a non-numeric id never matches.
bool readId(const juce::var& v, double& id) {
    if (!isNumber(v))
        return false;
    id = static_cast<double>(v);
    return true;
}

std::set<double> ids(const juce::var& nodes, bool (*keep)(const juce::var&)) {
    std::set<double> out;
    if (const juce::Array<juce::var>* list = nodes.getArray())
        for (const juce::var& node : *list) {
            double id = 0;
            if (node.isObject() && keep(node) && readId(node["id"], id))
                out.insert(id);
        }
    return out;
}

juce::var withProperty(const juce::var& object, const juce::Identifier& name, const juce::var& value) {
    auto* copy = new juce::DynamicObject();
    if (object.isObject())
        for (const auto& property : object.getDynamicObject()->getProperties())
            copy->setProperty(property.name, property.value);
    copy->setProperty(name, value);
    return juce::var(copy);
}

juce::var withTimelineOps(const juce::var& envelope, const juce::Array<juce::var>& ops) {
    return withProperty(envelope, "timelineOps", ops);
}

} // namespace

bool isPatternSequencer(const juce::var& node) {
    return node.isObject() && isSequencerType(node) && node.hasProperty("state");
}

juce::var dropNotesOnSequencerTracks(const juce::var& envelope) {
    const juce::Array<juce::var>* ops = envelope["timelineOps"].getArray();
    if (ops == nullptr || ops->isEmpty())
        return envelope;
    const std::set<double> sequencerIds =
        ids(envelope["nodes"], [](const juce::var& n) { return isPatternSequencer(n); });
    if (sequencerIds.empty())
        return envelope;

    std::set<double> fed;
    if (const juce::Array<juce::var>* connections = envelope["connections"].getArray())
        for (const juce::var& c : *connections) {
            double src = 0;
            double dst = 0;
            if (readId(c["src"], src) && readId(c["dst"], dst) && sequencerIds.count(src) > 0)
                fed.insert(dst);
        }
    std::set<juce::String> driven;
    for (const juce::var& op : *ops) {
        double instrument = 0;
        if (op["op"].toString() == "addInstrumentTrack" && readId(op["instrumentId"], instrument) &&
            fed.count(instrument) > 0)
            driven.insert(op["name"].toString());
    }
    if (driven.empty())
        return envelope;

    juce::Array<juce::var> repaired;
    bool changed = false;
    for (const juce::var& op : *ops) {
        if (op["op"].toString() == "placeClips" && op["track"].isString() && driven.count(op["track"].toString()) > 0)
            changed = true;
        else
            repaired.add(op);
    }
    return changed ? withTimelineOps(envelope, repaired) : envelope;
}

juce::var fixSequencerInstrumentWiring(const juce::var& envelope) {
    std::set<double> instrumentIds;
    if (const juce::Array<juce::var>* ops = envelope["timelineOps"].getArray())
        for (const juce::var& op : *ops) {
            double id = 0;
            if (op["op"].toString() == "addInstrumentTrack" && readId(op["instrumentId"], id))
                instrumentIds.insert(id);
        }
    if (instrumentIds.empty())
        return envelope;
    const std::set<double> sequencerIds = ids(envelope["nodes"], [](const juce::var& n) { return isSequencerType(n); });

    std::set<double> wired;
    bool changed = false;
    juce::Array<juce::var> connections;
    if (const juce::Array<juce::var>* list = envelope["connections"].getArray())
        for (const juce::var& c : *list) {
            double src = 0;
            double dst = 0;
            const bool feeds = readId(c["src"], src) && readId(c["dst"], dst) && sequencerIds.count(src) > 0 &&
                               instrumentIds.count(dst) > 0;
            if (!feeds) {
                connections.add(c);
                continue;
            }
            wired.insert(src);
            const bool sentinel = isNumber(c["srcPort"]) && isNumber(c["dstPort"]) &&
                                  static_cast<int>(c["srcPort"]) == -1 && static_cast<int>(c["dstPort"]) == -1;
            if (sentinel) {
                connections.add(c);
                continue;
            }
            changed = true;
            connections.add(withProperty(withProperty(c, "srcPort", -1), "dstPort", -1));
        }
    if (wired.empty())
        return envelope;

    juce::Array<juce::var> nodes;
    if (const juce::Array<juce::var>* list = envelope["nodes"].getArray())
        for (const juce::var& n : *list) {
            double id = 0;
            if (!readId(n["id"], id) || wired.count(id) == 0 || n["params"].hasProperty("run")) {
                nodes.add(n);
                continue;
            }
            changed = true;
            nodes.add(withProperty(n, "params", withProperty(n["params"], "run", 1)));
        }
    return changed ? withProperty(withProperty(envelope, "nodes", nodes), "connections", connections) : envelope;
}

juce::var applyServerRepairs(const juce::var& envelope, juce::StringArray* warnings) {
    return fixSequencerInstrumentWiring(dropNotesOnSequencerTracks(expandSong(envelope, warnings)));
}

} // namespace synth::projectgenerate
