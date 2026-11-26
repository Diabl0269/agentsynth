// GraphEditorCanvasTick.cpp
//
// The canvas memo kept between ticks and paints (graph_editor_paint::CanvasMemo, GraphEditorPaintMemo.h): the layout
// generation that card moves, graph edits and repaintCanvas() bump, the macro borders measured at that generation, and
// what the 30 Hz tick does to the canvas. GraphEditor is declared in GraphEditor.h; sibling GraphEditor*.cpp files in
// this directory hold the rest of the class (docs/layout/rendering.md#per-frame-work-does-not-grow-with-the-patch).

#include "AudioEngine/AudioEngine.h"
#include "CableEndGlide.h"
#include "GraphEditor.h"
#include "GraphEditorPaintMemo.h"
#include "Modules/ModuleBase.h"
#include "UI/Graph/CanvasLayer/CableFlowDots.h"
#include "UI/Graph/CanvasLayer/CanvasStaticLayer.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Graph/ProjectLoad/LoadRevealAnimator.h"
#include "UI/Layout/CableCurve.h"
#include "UI/Layout/ReducedMotion.h"

#include <map>
#include <tuple>

namespace graph_editor_paint {

CanvasMemo::CanvasMemo(GraphEditor& editor)
    : editor_(editor)
    , glide_(std::make_unique<CableEndGlide>(
          editor.vblankUpdater,
          [&editor]() -> const std::vector<GraphEditor::VisibleCable>& { return editor.buildVisibleCables(); },
          [&editor] { editor.repaintCanvas(); }))
    , layer_(std::make_unique<synth::ui::CanvasStaticLayer>(editor, editor.content))
    , dots_(std::make_unique<synth::ui::CableFlowDots>(&GraphEditor::buildCablePath)) {
    editor_.audioEngine.getGraph().addChangeListener(this);
}

CanvasMemo::~CanvasMemo() { editor_.audioEngine.getGraph().removeChangeListener(this); }

CanvasMemo* CanvasMemo::of(GraphEditor& editor) noexcept { return editor.canvasMemo_.get(); }

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
// The Audio Output card's Right label reads "follows L" from the engine's normalling, which the engine refreshes on
// this same broadcast; the repaint is asynchronous, so it paints after both listeners ran, whatever their order.
void CanvasMemo::changeListenerCallback(juce::ChangeBroadcaster*) {
    editor_.repaintCanvas();
    for (auto* card : editor_.getModuleComponents())
        if (card != nullptr && editor_.isOutputDockNode(card->getNodeId()) &&
            dynamic_cast<juce::AudioProcessorGraph::AudioGraphIOProcessor*>(card->getModule()) != nullptr)
            card->repaint();
}

// The tick does not drop the cable memo: rebuilding every cable 30 times a second made an idle canvas cost in
// proportion to the patch (about 10 ms a tick at 80 tracks). The memo is dropped only when the routing set moved (a
// DirectCV or PolyBus cable appears, goes or changes jack); the live values a cable is drawn with (signal activity,
// bypass, the attenuverter amount) are refreshed in place. Card moves, graph edits and every other change reach the
// memo through repaintCanvas() (childBoundsChanged and the graph's broadcast included). The canvas frame is refitted
// only when the layout generation moved, since fitting it measures every macro border.
//
// The tick repaints only the visible cables' area: the signal-flow dots and a cable's activity are all it changes. On
// a canvas zoomed in to a few cards (dragging a knob) the cards outside it are no longer redrawn 30 times a second.
// Anything else on the canvas that changes over time requests its own repaint; the tick no longer covers it. With the
// memo dropped (the routing set moved) the whole canvas is repainted.
//
// A tick that only moves the flow dots, or changes only cables already drawn live, repaints through the static layer
// (CanvasStaticLayer.h): the wires, borders and frame under them are drawn from one picture once the canvas has been
// quiet for a few frames, so an idle project costs about what a new one does instead of every cable and border 30
// times a second. A cable whose look changes (an LFO's activity, a bypass, an amount) leaves the picture for good: the
// first change repaints normally, which drops the picture, and from then on the cable is drawn over it on every frame.
// With audio running, the modulated cables change on nearly every tick, so keeping them in the picture would drop it
// on nearly every tick.
void CanvasMemo::tick() {
    ActivityRefresh refresh{true, true};
    bool dotsMove = false;
    if (routingsMoved())
        editor_.cablesCacheValid = false;
    else if (editor_.cablesCacheValid) {
        refresh = refreshCableActivity();
        dotsMove = synth::ui::loopRate() > 0.0;
    }
    // An idle canvas (nothing changed a cable's look, the flow dots held by Animations Off) is not repainted: on a big
    // project each full paint is milliseconds, 30 times a second.
    if (!editor_.cablesCacheValid)
        lastTickArea_ = editor_.content.getLocalBounds();
    else
        lastTickArea_ = refresh.lookChanged || dotsMove ? visibleCableArea() : juce::Rectangle<int>();
    if (!lastTickArea_.isEmpty()) {
        if (editor_.cablesCacheValid && !refresh.newlyLive)
            layer_->repaintOver(lastTickArea_); // nothing in the picture changed
        else
            editor_.content.repaint(lastTickArea_);
    }
    if (fittedGeneration_ != generation_) {
        fittedGeneration_ = generation_;
        ++workCounters().canvasFrameFits;
        editor_.refreshCanvasFrame(CanvasFrame::Mode::GrowOnly); // live drags only grow
    }
}

// The union of every cable box (synth::ui::cablePaintBounds) that reaches the visible canvas, cut to it.
juce::Rectangle<int> CanvasMemo::visibleCableArea() const {
    const auto visible = editor_.getVisibleCanvasRect();
    juce::Rectangle<float> area;
    for (const auto& cable : editor_.cablesCache)
        if (const auto box = synth::ui::cablePaintBounds(cable.p1, cable.p2); box.intersects(visible))
            area = area.isEmpty() ? box : area.getUnion(box);
    return area.getIntersection(visible).getSmallestIntegerContainer();
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
// A cable whose look changed is marked drawn live (VisibleCable::drawnLive).
CanvasMemo::ActivityRefresh CanvasMemo::refreshCableActivity() {
    ActivityRefresh result;
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
        bool changed = false;
        if (cable.kind == GraphEditor::VisibleCable::Kind::ModRouting) {
            const auto it = routings.find(Key{cable.id.srcUid, cable.id.srcPort, cable.id.dstUid, cable.id.dstPort});
            if (it != routings.end()) {
                changed |= cable.activity != it->second->modSignalPeak || cable.isBypassed != it->second->isBypassed;
                cable.activity = it->second->modSignalPeak;
                cable.isBypassed = it->second->isBypassed;
            }
        } else if (cable.kind == GraphEditor::VisibleCable::Kind::AttenuverterChain) {
            if (const auto it = chainPeaks.find(cable.id.attenUid); it != chainPeaks.end()) {
                changed |= cable.activity != it->second;
                cable.activity = it->second;
            }
            if (auto* node = graph.getNodeForId(juce::AudioProcessorGraph::NodeID{cable.id.attenUid}))
                if (auto* p = findParameterByID(node->getProcessor(), "amount")) {
                    const float amount = p->getValue() * 2.0f - 1.0f; // 0..1 -> -1..1
                    changed |= cable.attenAmount != amount;
                    cable.attenAmount = amount;
                }
        }
        if (changed) {
            result.lookChanged = true;
            result.newlyLive |= !cable.drawnLive;
            cable.drawnLive = true;
        }
    }
    return result;
}

} // namespace graph_editor_paint

juce::Rectangle<int> GraphEditor::getLastTickRepaintAreaForTest() const { return canvasMemo_->lastTickArea(); }

// A card that moves or resizes moves its cable ends and can move a macro border. A card popping in as a project opens
// changes only its transform, never its bounds: nothing on the canvas moved.
void GraphEditor::GraphContentComponent::childBoundsChanged(juce::Component*) {
    if (editor.loadReveal_ == nullptr || !editor.loadReveal_->isApplyingFrame())
        editor.repaintCanvas();
}
