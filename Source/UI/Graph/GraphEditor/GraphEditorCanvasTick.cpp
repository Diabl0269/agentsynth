// GraphEditorCanvasTick.cpp
//
// The canvas memo kept between ticks and paints (graph_editor_paint::CanvasMemo, GraphEditorPaintMemo.h): the layout
// generation that card moves, graph edits and repaintCanvas() bump, the macro borders measured at that generation, and
// what the 30 Hz tick does to the canvas. GraphEditor is declared in GraphEditor.h; sibling GraphEditor*.cpp files in
// this directory hold the rest of the class (docs/layout/rendering.md#per-frame-work-does-not-grow-with-the-patch).

#include "AudioEngine/AudioEngine.h"
#include "GraphEditor.h"
#include "GraphEditorPaintMemo.h"
#include "Modules/ModuleBase.h"

#include <map>
#include <tuple>

namespace graph_editor_paint {

CanvasMemo::CanvasMemo(GraphEditor& editor)
    : editor_(editor) {
    editor_.audioEngine.getGraph().addChangeListener(this);
}

CanvasMemo::~CanvasMemo() { editor_.audioEngine.getGraph().removeChangeListener(this); }

void CanvasMemo::layoutChanged() noexcept {
    ++generation_;
    hulls_.clear();
}

std::optional<juce::Rectangle<int>> CanvasMemo::hull(const juce::String& macroId) const {
    if (const auto it = hulls_.find(macroId); it != hulls_.end())
        return it->second;
    return std::nullopt;
}

void CanvasMemo::storeHull(const juce::String& macroId, juce::Rectangle<int> hull) { hulls_[macroId] = hull; }

// The graph broadcasts every node and cable change, asynchronously, wherever it came from: an edit that changes the
// cables without going through repaintCanvas() still reaches the memo here, now that the tick no longer rebuilds
// every cable on every frame.
void CanvasMemo::changeListenerCallback(juce::ChangeBroadcaster*) { editor_.repaintCanvas(); }

// The tick does not drop the cable memo: rebuilding every cable 30 times a second made an idle canvas cost in
// proportion to the patch (about 10 ms a tick at 80 tracks). The memo is dropped only when the routing set moved (a
// DirectCV or PolyBus cable appears, goes or changes jack); the live values a cable is drawn with (signal activity,
// bypass, the attenuverter amount) are refreshed in place. Card moves, graph edits and every other change reach the
// memo through repaintCanvas() (childBoundsChanged and the graph's broadcast included). The canvas frame is refitted
// only when the layout generation moved, since fitting it measures every macro border.
void CanvasMemo::tick() {
    if (routingsMoved())
        editor_.cablesCacheValid = false;
    else if (editor_.cablesCacheValid)
        refreshCableActivity();
    editor_.content.repaint(); // the signal-flow dots move along every cable
    if (fittedGeneration_ != generation_) {
        fittedGeneration_ = generation_;
        ++workCounters().canvasFrameFits;
        editor_.refreshCanvasFrame(CanvasFrame::Mode::GrowOnly); // live drags only grow
    }
}

// FNV-1a over what decides a routing cable's existence and ends; values that only change how it is drawn are left
// out, so they never cost a rebuild.
bool CanvasMemo::routingsMoved() {
    juce::uint64 hash = 1469598103934665603ull;
    auto mix = [&hash](juce::uint64 v) { hash = (hash ^ v) * 1099511628211ull; };
    for (const auto& r : editor_.cachedModRoutings) {
        mix((juce::uint64)r.kind);
        mix(r.sourceNodeID.uid);
        mix((juce::uint64)r.sourceChannelIndex);
        mix((juce::uint64)r.sourceVisibleJack);
        mix(r.destNodeID.uid);
        mix((juce::uint64)r.destChannelIndex);
        mix((juce::uint64)r.destVisibleJack);
        mix(r.attenuverterNodeID.uid);
        mix((juce::uint64)r.voiceCount);
        mix((juce::uint64)r.role);
    }
    mix(editor_.cachedModRoutings.size());
    const bool moved = hash != routingSignature_;
    routingSignature_ = hash;
    return moved;
}

// The same values rebuildVisibleCables() reads, written onto the memo's cables: a routing cable's activity and bypass
// from its routing, an attenuverter chain's activity from the display info and its amount from the attenuverter.
void CanvasMemo::refreshCableActivity() {
    using Key = std::tuple<juce::uint32, int, juce::uint32, int>;
    std::map<Key, const ModulationRouting*> routings;
    for (const auto& r : editor_.cachedModRoutings)
        if (r.kind != ModulationRoutingKind::AttenuverterChain)
            routings.emplace(Key{r.sourceNodeID.uid, r.sourceChannelIndex, r.destNodeID.uid, r.destChannelIndex}, &r);
    std::map<juce::uint32, float> chainPeaks;
    for (const auto& info : editor_.cachedModDisplayInfo)
        chainPeaks.emplace(info.attenuverterNodeID.uid, info.modSignalPeak);

    auto& graph = editor_.audioEngine.getGraph();
    for (auto& cable : editor_.cablesCache) {
        if (cable.kind == GraphEditor::VisibleCable::Kind::ModRouting) {
            const auto it = routings.find(Key{cable.id.srcUid, cable.id.srcPort, cable.id.dstUid, cable.id.dstPort});
            if (it != routings.end()) {
                cable.activity = it->second->modSignalPeak;
                cable.isBypassed = it->second->isBypassed;
            }
        } else if (cable.kind == GraphEditor::VisibleCable::Kind::AttenuverterChain) {
            if (const auto it = chainPeaks.find(cable.id.attenUid); it != chainPeaks.end())
                cable.activity = it->second;
            if (auto* node = graph.getNodeForId(juce::AudioProcessorGraph::NodeID{cable.id.attenUid}))
                if (auto* p = findParameterByID(node->getProcessor(), "amount"))
                    cable.attenAmount = p->getValue() * 2.0f - 1.0f; // 0..1 -> -1..1
        }
    }
}

} // namespace graph_editor_paint

// A card that moves or resizes moves its cable ends and can move a macro border.
void GraphEditor::GraphContentComponent::childBoundsChanged(juce::Component*) { editor.repaintCanvas(); }
