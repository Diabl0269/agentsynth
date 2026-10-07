#pragma once

// GraphEditorPaintMemo.h
//
// Per-paint memo of the painted macro borders, the canvas memo kept between ticks and paints, and the canvas work
// counters that guard them. Kept out of GraphEditor.h on purpose
// (docs/layout/rendering.md#per-frame-work-does-not-grow-with-the-patch).

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_graphics/juce_graphics.h>
#include <memory>
#include <optional>
#include <unordered_map>

class GraphEditor;
class GraphCanvasHost;
class ModuleComponent;
namespace synth {
class MacroOwnerIndex;
}

namespace graph_editor_paint {

/** Message thread only. While alive, GraphEditor::paintedMacroHullBounds on `editor` computes each border once. */
class HullMemoScope {
public:
    explicit HullMemoScope(const GraphEditor& editor);
    ~HullMemoScope();
    HullMemoScope(const HullMemoScope&) = delete;
    HullMemoScope& operator=(const HullMemoScope&) = delete;

private:
    friend class ::GraphEditor;
    const GraphEditor& editor_;
    HullMemoScope* previous_ = nullptr;
    std::unordered_map<juce::String, juce::Rectangle<int>> hulls_;
};

/** Message thread only. What the canvas keeps between ticks and paints (GraphEditorCanvasTick.cpp): a layout
 *  generation, the macro borders measured at it, and the 30 Hz tick's bookkeeping. */
class CanvasMemo : private juce::ChangeListener {
public:
    /** Listens to the editor's graph until destroyed; the graph must outlive it. */
    explicit CanvasMemo(GraphEditor& editor);
    ~CanvasMemo() override;
    CanvasMemo(const CanvasMemo&) = delete;
    CanvasMemo& operator=(const CanvasMemo&) = delete;

    /** Something that can move a card, a cable end or a macro border changed. */
    void layoutChanged() noexcept;
    /** The border stored for `macroId` since the last layoutChanged(), if any. */
    std::optional<juce::Rectangle<int>> hull(const juce::String& macroId) const;
    void storeHull(const juce::String& macroId, juce::Rectangle<int> hull);
    /** The canvas part of GraphEditor::timerCallback(); call after the cached routings were refreshed. */
    void tick();
    /** What the last tick() asked to repaint, in canvas coordinates. */
    juce::Rectangle<int> lastTickArea() const noexcept { return lastTickArea_; }

private:
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;
    bool routingsMoved();
    void refreshCableActivity();
    juce::Rectangle<int> visibleCableArea() const;

    GraphEditor& editor_;
    juce::uint64 generation_ = 1;
    juce::uint64 fittedGeneration_ = 0; // the generation the canvas frame was last fitted at
    juce::uint64 routingSignature_ = 0;
    juce::Rectangle<int> lastTickArea_;
    std::unordered_map<juce::String, juce::Rectangle<int>> hulls_;
};

/** Message thread only. While alive, MacroGroupController::macroHullBounds for `host` measures every border against
 *  one card map built when the scope opened; no card may be added or removed while it is open. */
class CardMapScope {
public:
    explicit CardMapScope(GraphCanvasHost& host);
    ~CardMapScope();
    CardMapScope(const CardMapScope&) = delete;
    CardMapScope& operator=(const CardMapScope&) = delete;
    /** The innermost open scope's cards by node uid when it was opened for `host`, else null. */
    static const std::unordered_map<uint32_t, ModuleComponent*>* cardsFor(const GraphCanvasHost& host);

private:
    const GraphCanvasHost& host_;
    CardMapScope* previous_ = nullptr;
    std::unordered_map<uint32_t, ModuleComponent*> cards_;
};

/** Message thread only. While alive, MacroGroupController::macroPortOwnerFor for `host` answers from one member map
 *  built when the scope opened; the host's macros must not change while it is open. */
class MacroOwnerScope {
public:
    explicit MacroOwnerScope(GraphCanvasHost& host);
    ~MacroOwnerScope();
    MacroOwnerScope(const MacroOwnerScope&) = delete;
    MacroOwnerScope& operator=(const MacroOwnerScope&) = delete;
    /** The innermost open scope's index when it was opened for `host`, else null. */
    static const synth::MacroOwnerIndex* ownersFor(const GraphCanvasHost& host);

private:
    const GraphCanvasHost& host_;
    MacroOwnerScope* previous_ = nullptr;
    std::unique_ptr<synth::MacroOwnerIndex> owners_;
};

/** Counts since the last reset (test seam): borders computed, whole-graph node scans by uuid or node id,
 *  whole-cable-set scans, the cables and macro borders a paint actually drew, and canvas frame fits. */
struct WorkCounters {
    int hullComputations = 0;
    int nodeScans = 0;
    int cableScans = 0; // a ConnectionIndex built: one pass over every cable (ModMatrixEndpoints.h)
    int cablesPainted = 0;
    int hullsPainted = 0;
    int canvasFrameFits = 0;
};

/** Message thread only. */
WorkCounters& workCounters() noexcept;

} // namespace graph_editor_paint
