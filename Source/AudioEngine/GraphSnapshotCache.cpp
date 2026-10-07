// GraphSnapshotCache.cpp -- the undo history's graph snapshots (GraphSnapshotCache.h), built from the last capture's
// node objects wherever a node's JSON has not changed, and the JSON comparison and size count that go with them.
#include "GraphSnapshotCache.h"

#include "AI/AIStateMapper/AIStateMapper.h"
#include "AudioEngine/ConnectionIndex.h"
#include "Modules/AttenuverterModule.h"
#include "Modules/CardLayout.h"
#include "Modules/ModuleBase.h"

#include <algorithm>
#include <cmath>

namespace synth {

namespace {

// What JSON writes as "null": a void, and a double that is not finite.
bool writesNull(const juce::var& v) { return v.isVoid() || (v.isDouble() && !std::isfinite((double)v)); }

bool sameObject(const juce::DynamicObject& a, const juce::DynamicObject& b) {
    const auto& pa = a.getProperties();
    const auto& pb = b.getProperties();
    if (pa.size() != pb.size())
        return false;
    for (int i = 0; i < pa.size(); ++i)
        if (pa.getName(i) != pb.getName(i) || !sameJson(pa.getValueAt(i), pb.getValueAt(i)))
            return false;
    return true;
}

int stringSize(const juce::String& s) { return (int)s.getNumBytesAsUTF8() + 2; }

} // namespace

// Mirrors juce::JSON::writeToStream case by case, so two values compare equal exactly when their text would: the
// property order counts, an int and an int64 of one value write the same digits, a double never writes like an int,
// and a void and a non-finite double both write "null". Doubles compare by value, which is at least as strict as
// their printed digits. Anything else (binary data) is compared as the text JSON writes for it.
bool sameJson(const juce::var& a, const juce::var& b) {
    if (writesNull(a) || writesNull(b))
        return writesNull(a) && writesNull(b);
    if (a.isString() || b.isString())
        return a.isString() && b.isString() && a.toString() == b.toString();
    if (a.isUndefined() || b.isUndefined())
        return a.isUndefined() && b.isUndefined();
    if (a.isBool() || b.isBool())
        return a.isBool() && b.isBool() && (bool)a == (bool)b;
    if (a.isDouble() || b.isDouble())
        return a.isDouble() && b.isDouble() && (double)a == (double)b;
    if (a.isArray() || b.isArray()) {
        const auto* x = a.getArray();
        const auto* y = b.getArray();
        if (x == nullptr || y == nullptr)
            return false;
        if (x == y)
            return true;
        if (x->size() != y->size())
            return false;
        for (int i = 0; i < x->size(); ++i)
            if (!sameJson(x->getReference(i), y->getReference(i)))
                return false;
        return true;
    }
    if (a.isObject() || b.isObject()) {
        const auto* x = a.getDynamicObject();
        const auto* y = b.getDynamicObject();
        if (x == nullptr || y == nullptr)
            return x == y && a.isObject() && b.isObject();
        return x == y || sameObject(*x, *y);
    }
    if ((a.isInt() || a.isInt64()) && (b.isInt() || b.isInt64()))
        return (juce::int64)a == (juce::int64)b;
    return a.toString() == b.toString();
}

// Counts the way one-line JSON is laid out (quotes, ": ", ", ", brackets) with every number taken as a short fixed
// width: an undo budget, not a byte count, so it only has to grow with what the state holds.
int estimateJsonSize(const juce::var& v, const std::function<int(const void*)>& known) {
    if (v.isString())
        return stringSize(v.toString());
    if (const auto* array = v.getArray()) {
        if (known)
            if (const int size = known(array); size >= 0)
                return size;
        int size = 2;
        for (const auto& item : *array)
            size += estimateJsonSize(item, known) + 2;
        return size;
    }
    if (auto* object = v.getDynamicObject()) {
        if (known)
            if (const int size = known(object); size >= 0)
                return size;
        int size = 2;
        const auto& props = object->getProperties();
        for (int i = 0; i < props.size(); ++i)
            size += stringSize(props.getName(i).toString()) + 4 + estimateJsonSize(props.getValueAt(i), known);
        return size;
    }
    return v.isBool() || writesNull(v) ? 5 : 8;
}

GraphSnapshotCache::GraphSnapshotCache() = default;
GraphSnapshotCache::~GraphSnapshotCache() = default;

// Every field AIStateMapper::nodeToJSON writes, read cheaply: parameters by their raw values (each written field is a
// function of the parameter object and its value), the rest as the vars nodeToJSON copies in.
GraphSnapshotCache::Entry GraphSnapshotCache::readNode(juce::AudioProcessorGraph::Node& node) {
    Entry e;
    auto* processor = node.getProcessor();
    e.processor = processor;
    e.id = node.nodeID.uid;
    e.type = AIStateMapper::getFactoryTypeName(processor);
    e.displayName = node.properties["displayName"].toString();
    e.cardLayout = node.properties[kCardLayoutNodeProperty];
    e.x = node.properties["x"];
    e.y = node.properties["y"];
    const auto& params = processor->getParameters();
    e.params.reserve((size_t)params.size());
    for (auto* param : params)
        e.params.emplace_back(param, param->getValue());
    if (auto* mb = dynamic_cast<ModuleBase*>(processor)) {
        e.extraState = mb->getExtraState();
        e.cardView = mb->getCardViewState().toVar();
    }
    return e;
}

bool GraphSnapshotCache::Entry::sameAs(const Entry& o) const {
    return processor == o.processor && id == o.id && type == o.type && displayName == o.displayName &&
           params == o.params && sameJson(x, o.x) && sameJson(y, o.y) && sameJson(cardLayout, o.cardLayout) &&
           sameJson(extraState, o.extraState) && sameJson(cardView, o.cardView);
}

// A node's object is reused only when every field nodeToJSON would write for it reads the same as when that object
// was written; anything else is written afresh by nodeToJSON itself, so the text can never drift from graphToJSON's.
juce::var GraphSnapshotCache::capture(juce::AudioProcessorGraph& graph) {
    if (graph_ != &graph) { // another graph: nothing from the last one applies
        entries_.clear();
        attenuverters_.clear();
        cables_.clear();
        connections_ = modulations_ = juce::var();
        graph_ = &graph;
    }
    lastRebuilt_ = 0;
    Entries next;
    next.reserve(entries_.size() + 16);
    juce::Array<juce::var> nodes;
    std::vector<juce::uint32> attenuverters;
    bool attenuvertersChanged = false;
    for (auto* node : graph.getNodes()) {
        if (node->getProcessor() == nullptr)
            continue;
        const auto uuid = AIStateMapper::ensureNodeUuid(node);
        auto entry = readNode(*node);
        const auto old = entries_.find(uuid);
        const bool reuse = old != entries_.end() && old->second.sameAs(entry);
        const bool isAttenuverter = dynamic_cast<AttenuverterModule*>(node->getProcessor()) != nullptr;
        if (reuse) {
            entry = std::move(old->second);
        } else {
            entry.json = AIStateMapper::nodeToJSON(*node);
            entry.cardLayout = entry.cardLayout.clone(); // compared against later, so never the live object
            entry.size = estimateJsonSize(entry.json);
            ++lastRebuilt_;
            attenuvertersChanged = attenuvertersChanged || isAttenuverter;
        }
        if (isAttenuverter)
            attenuverters.push_back(entry.id);
        nodes.add(entry.json);
        next[uuid] = std::move(entry);
    }
    entries_ = std::move(next);
    attenuvertersChanged = attenuvertersChanged || attenuverters != attenuverters_;
    attenuverters_ = std::move(attenuverters);
    captureCables(graph, attenuvertersChanged);

    sizes_.clear();
    for (const auto& [uuid, entry] : entries_)
        sizes_[entry.json.getDynamicObject()] = entry.size;
    sizes_[connections_.getArray()] = estimateJsonSize(connections_);

    juce::DynamicObject::Ptr root = new juce::DynamicObject();
    root->setProperty("schemaVersion", AIStateMapper::kSchemaVersion);
    root->setProperty("nodes", nodes);
    root->setProperty("connections", connections_);
    root->setProperty("modulations", modulations_);
    return juce::var(root.get());
}

// The cable lists are kept while the graph's cables are the same; the modulations also need every attenuverter
// unchanged (their amount and bypass are written into each entry).
void GraphSnapshotCache::captureCables(juce::AudioProcessorGraph& graph, bool attenuvertersChanged) {
    auto cables = graph.getConnections();
    const bool cablesChanged = connections_.isVoid() || cables != cables_;
    if (!cablesChanged && !attenuvertersChanged)
        return;
    const ConnectionIndex index(graph);
    if (cablesChanged)
        connections_ = AIStateMapper::connectionsToJSON(index);
    modulations_ = AIStateMapper::modulationsToJSON(graph, index);
    cables_ = std::move(cables);
}

int GraphSnapshotCache::knownSize(const void* objectOrArray) const {
    const auto it = sizes_.find(objectOrArray);
    return it != sizes_.end() ? it->second : -1;
}

} // namespace synth
