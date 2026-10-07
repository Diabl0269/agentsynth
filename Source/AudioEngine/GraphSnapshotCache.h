#pragma once

#include <functional>
#include <juce_audio_processors/juce_audio_processors.h>
#include <unordered_map>
#include <vector>

namespace synth {

/** True when `a` and `b` write out as the same JSON text (juce::JSON::toString), decided without writing either; an
 *  object or array both hold is equal at once. */
bool sameJson(const juce::var& a, const juce::var& b);

/** About the length of `v` written out as JSON on one line, counted without writing it. `known` answers for an object
 *  or array (by getDynamicObject() / getArray()) whose size is already known, or returns -1. */
int estimateJsonSize(const juce::var& v, const std::function<int(const void*)>& known = {});

/** Builds AIStateMapper::graphToJSON snapshots for the undo history, re-serialising only the nodes whose JSON changed
 *  since the last capture and reusing the rest: two snapshots share every unchanged node object, and a capture's text
 *  is always graphToJSON's. Snapshots are shared, so never modify one in place. Message thread only. */
class GraphSnapshotCache {
public:
    GraphSnapshotCache();
    ~GraphSnapshotCache();

    /** The graph as graphToJSON writes it (assigning missing uuids the same way). */
    juce::var capture(juce::AudioProcessorGraph& graph);
    /** The size estimateJsonSize counts for a node object or cable list of the latest capture, else -1. */
    int knownSize(const void* objectOrArray) const;
    /** How many node objects the latest capture built afresh (test seam). */
    int lastRebuiltNodes() const noexcept { return lastRebuilt_; }

private:
    // Everything nodeToJSON writes for one node, read off the live node, plus the object it wrote from them.
    struct Entry {
        const juce::AudioProcessor* processor = nullptr;
        juce::uint32 id = 0;
        juce::String type, displayName;
        juce::var cardLayout, extraState, cardView, x, y;
        std::vector<std::pair<const juce::AudioProcessorParameter*, float>> params;
        juce::var json;
        int size = 0;
        bool sameAs(const Entry& other) const;
    };
    using Entries = std::unordered_map<juce::String, Entry>;
    static Entry readNode(juce::AudioProcessorGraph::Node& node);
    void captureCables(juce::AudioProcessorGraph& graph, bool attenuvertersChanged);

    const juce::AudioProcessorGraph* graph_ = nullptr;
    Entries entries_; // by uuid
    std::vector<juce::uint32> attenuverters_;
    std::vector<juce::AudioProcessorGraph::Connection> cables_;
    juce::var connections_, modulations_;
    std::unordered_map<const void*, int> sizes_;
    int lastRebuilt_ = 0;
};

} // namespace synth
