#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace synth {

/**
 * One audio-graph rebuild for a whole batch of edits. Everything that rebuilds the render sequence after a topology
 * change (applyJSONToGraph, a track duplicate's boundary cables) goes through rebuild(): on its own it rebuilds at
 * once, inside a GraphRebuildBatch it only notes that one is due and the outermost batch does it when it ends. So
 * duplicating three tracks pays for one rebuild, not three. Message
 * thread only; the graph's structure (nodes, cables) is current throughout, only the render sequence lags.
 */
class GraphRebuildBatch {
public:
    explicit GraphRebuildBatch(juce::AudioProcessorGraph& graph)
        : graph_(graph) {
        ++depth();
    }
    ~GraphRebuildBatch() {
        if (--depth() == 0 && pending()) {
            pending() = false;
            rebuild(graph_);
        }
    }
    GraphRebuildBatch(const GraphRebuildBatch&) = delete;
    GraphRebuildBatch& operator=(const GraphRebuildBatch&) = delete;

    /** Rebuilds `graph` now, or once when the outermost batch ends. */
    static void rebuild(juce::AudioProcessorGraph& graph) {
        if (depth() > 0) {
            pending() = true;
            return;
        }
        ++rebuildCount();
        graph.rebuild();
    }

    /** How many rebuilds have actually run through rebuild() (test seam; monotonic). */
    static int rebuildCountForTest() noexcept { return rebuildCount(); }

private:
    static int& depth() noexcept {
        static int value = 0;
        return value;
    }
    static bool& pending() noexcept {
        static bool value = false;
        return value;
    }
    static int& rebuildCount() noexcept {
        static int value = 0;
        return value;
    }

    juce::AudioProcessorGraph& graph_;
};

/**
 * A GraphRebuildBatch whose end also marks a rebuild due. Every topology op inside the scope passes
 * UpdateKind::none (no per-op render-sequence rebuild); the scope rebuilds once when it ends, or once at the end of the
 * outermost enclosing scope/batch, so a nested call costs nothing extra. Message thread only.
 */
class GraphRebuildScope {
public:
    explicit GraphRebuildScope(juce::AudioProcessorGraph& graph)
        : batch_(graph)
        , graph_(graph) {}
    ~GraphRebuildScope() { GraphRebuildBatch::rebuild(graph_); } // batch_ (declared first) then runs it if outermost
    GraphRebuildScope(const GraphRebuildScope&) = delete;
    GraphRebuildScope& operator=(const GraphRebuildScope&) = delete;

private:
    GraphRebuildBatch batch_;
    juce::AudioProcessorGraph& graph_;
};

} // namespace synth
