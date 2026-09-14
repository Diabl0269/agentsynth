// SmartConnectionEngine.h
//
// Proximity-based cable suggestions shown while placing a module (drag preview ghost near
// existing cards) and applied on drop. Reaches its owning canvas only through GraphCanvasHost —
// see that header for the narrow seam this depends on. GraphEditor holds one instance
// (`smartConnections_`) and forwards its own (unchanged) public smart-connection API to it; the
// mode/suggestion state that used to live on GraphEditor now lives here instead.
//
// Self-contained header: never includes GraphEditor.h (GraphEditor.h includes THIS header to
// declare its `smartConnections_` member, so the reverse would cycle). Implementation split
// across sibling SmartConnectionEngine.cpp (naming/eligibility/jack helpers) and
// SmartConnectionEngineApply.cpp (refreshSmartSuggestions/applySmartSuggestions).
#pragma once

#include "UI/Graph/CableColour.h"
#include "UI/Graph/GraphCanvasHost.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>
#include <vector>

class SmartConnectionEngine {
public:
    // Proximity-based cable suggestions while placing a module. One setting covers Off /
    // library-only / free-main-I/O moves / all moves.
    enum class SmartConnectionMode { Off, NewOnly, NewAndUnwired, AllMoves };

    /** One suggested cable shown as a frosted preview during drag; applied on drop.
     *
     *  `isInsert` turns the same record into an insert-in-series: the ghost is spliced into cabling
     *  that already exists rather than given a jack of its own. Two cable SETS then come with it —
     *  `doomedLinks` (upstream -> sink, to be removed, drawn dashed) and `upstreamCables`
     *  (upstream -> ghost, replacing them) — plus this record's own ghostJack -> neighborJack.
     *
     *  Both sets describe the WHOLE insert group, not just this record's leg, and both are already
     *  deduped, so applying them once per suggestion is idempotent. They are deliberately not
     *  per-leg: a jack pair dropped by the fan dedupe must NOT take its doomed link with it, or the
     *  cable it represented survives and sums into the sink alongside the ghost's output.
     *
     *  Only offered for the graph's terminal audio sink; see refreshSmartSuggestions. */
    struct SmartSuggestion {
        /** One cable of an insert, at visible-jack level. Endpoints are for preview paint only. */
        struct InsertLink {
            int fromJack = 0; // upstream visible OUTPUT jack
            int toJack = 0;   // sink visible input jack (doomed), or ghost visible input jack (new)
            juce::Point<float> p1{}, p2{};

            bool operator==(const InsertLink& o) const noexcept { return fromJack == o.fromJack && toJack == o.toJack; }
        };

        /** When true the dragged module is the cable source; when false it is the destination. */
        bool ghostIsSource = true;
        juce::AudioProcessorGraph::NodeID neighborId{};
        int ghostJack = 0;
        int neighborJack = 0;
        bool isMidi = false;
        juce::Point<float> p1{}, p2{}; // head endpoints; mainPreviewLegs is what actually gets drawn
        synth::ui::CableSignal signal = synth::ui::CableSignal::Audio;
        synth::ui::ModuleCategory sourceCategory = synth::ui::ModuleCategory::Utility;

        /** Every frosted segment the preview must draw, so it shows exactly what the drop will wire.
         *  ONE suggestion is not one drawn cable: connectPorts fans a collapsed jack across a whole
         *  raw pair, and when the far end fronts those raws as two separate visible jacks (the
         *  terminal sink does — it has no ModuleBase to group them) that is two cables on screen.
         *  Resolved from the same PolyLink connectPorts uses and deduped to distinct visible jack
         *  pairs, because N graph edges through one jack pair are still one cable. */
        std::vector<InsertLink> mainPreviewLegs;     // ghostJack -> neighborJack
        std::vector<InsertLink> upstreamPreviewLegs; // upstream -> ghost (insert only)

        // ---- Insert-in-series (audio only; ghostIsSource is always true) ----
        bool isInsert = false;
        juce::AudioProcessorGraph::NodeID upstreamId{}; // node whose cabling gets rerouted
        std::vector<InsertLink> doomedLinks;            // upstream -> sink, every one to remove
        std::vector<InsertLink> upstreamCables;         // upstream -> ghost, replacing them
        synth::ui::ModuleCategory upstreamCategory = synth::ui::ModuleCategory::Utility;

        bool operator==(const SmartSuggestion& o) const noexcept {
            return ghostIsSource == o.ghostIsSource && neighborId == o.neighborId && ghostJack == o.ghostJack &&
                   neighborJack == o.neighborJack && isMidi == o.isMidi && isInsert == o.isInsert &&
                   upstreamId == o.upstreamId && doomedLinks == o.doomedLinks && upstreamCables == o.upstreamCables &&
                   mainPreviewLegs == o.mainPreviewLegs && upstreamPreviewLegs == o.upstreamPreviewLegs;
        }
        bool operator!=(const SmartSuggestion& o) const noexcept { return !(*this == o); }
    };

    /** The drag-preview state refreshSmartSuggestions needs, read from GraphEditor's own fields
     *  and handed in rather than reached for — GraphEditor keeps owning the actual drag-preview
     *  members until FRO77 PR3 (GraphDragDropController). `probe` is BORROWED (the owning
     *  unique_ptr stays on GraphEditor); `selectionDragBlocksSuggestions` is GraphEditor's own
     *  `selectionDragActive && selection.size() > 1` — selection itself is not on GraphCanvasHost
     *  in this PR, so the caller precomputes the one bit the engine needs from it. */
    struct DragPreviewState {
        bool active = false;
        juce::Rectangle<int> ghost;
        juce::Rectangle<int> aim;
        juce::AudioProcessorGraph::NodeID selfId{};
        bool isSnippet = false;
        juce::AudioProcessor* probe = nullptr;
        bool selectionDragBlocksSuggestions = false;
    };

    explicit SmartConnectionEngine(GraphCanvasHost& host)
        : host_(host) {}

    void refreshSmartSuggestions(const DragPreviewState& drag);
    void applySmartSuggestions(juce::AudioProcessorGraph::NodeID ghostNodeId, bool recordUndo);
    void clearSmartSuggestions() { smartSuggestions_.clear(); }

    /** Re-evaluates the suggestions when the insert modifier changed since the last drag tick. A
     *  modifier press/release is not a mouse move, so nothing else would notice it. */
    void refreshSuggestionsIfInsertModifierChanged(const DragPreviewState& drag);

    void setSmartConnectionMode(SmartConnectionMode mode) noexcept { smartConnectionMode_ = mode; }
    SmartConnectionMode getSmartConnectionMode() const noexcept { return smartConnectionMode_; }

    void setInsertModifierOverrideForTests(std::optional<bool> down) { insertModifierOverride_ = down; }

    /** CTRL turns a proximity suggestion into an insert-in-series. Ctrl on every platform (it is
     *  the literal Control key on macOS too, NOT Cmd) — Cmd was tried first and lost, because
     *  Cmd-click is the additive-selection modifier and the two gestures are indistinguishable at
     *  mouse-down.
     *
     *  Sampled LIVE on every drag tick rather than latched at mouse-down, so BOTH orderings work:
     *  press-then-Ctrl (the modifier is picked up on the next tick) and Ctrl-then-press (the
     *  deferred classification in `ModuleComponent::mouseDown` arms a drag as well as a selection
     *  toggle, and this read simply sees Ctrl already down).
     *
     *  Tests set the override (setInsertModifierOverrideForTests above); production leaves it
     *  empty and reads the real keyboard. */
    bool isInsertModifierDown() const;

    /** Seeds the drag-tick comparison from the modifier state right now, so a drag started WITH
     *  the modifier already held is not reported as a change on its very first tick. Called once,
     *  from GraphEditor::beginDragPreview. */
    void seedInsertModifierSample() { lastSampledInsertModifier_ = isInsertModifierDown(); }

    /** Persist / restore helpers (Preferences tab + MainComponent launch restore). */
    static SmartConnectionMode smartConnectionModeFromString(const juce::String& s);
    static juce::String smartConnectionModeToString(SmartConnectionMode mode);

    int getSmartSuggestionCount() const noexcept { return (int)smartSuggestions_.size(); }
    const std::vector<SmartSuggestion>& getSmartSuggestions() const noexcept { return smartSuggestions_; }

    /** Audio-jack occupancy / eligibility helpers, shared by refresh/apply and exposed for the
     *  *ForTest(s) seams GraphEditor forwards to them. */
    bool isInputJackFree(juce::AudioProcessorGraph::NodeID nodeId, int jack, bool isMidi) const;
    bool isOutputJackFree(juce::AudioProcessorGraph::NodeID nodeId, int jack, bool isMidi) const;
    bool areJacksAlreadyConnected(juce::AudioProcessorGraph::NodeID srcId, int srcJack,
                                  juce::AudioProcessorGraph::NodeID dstId, int dstJack, bool isMidi) const;

    /** The cabling feeding an audio input jack, at CABLE level (visible output jacks of the
     *  feeding node, never raw graph edges). */
    struct UpstreamLink {
        juce::AudioProcessorGraph::NodeID nodeId{};
        /** Every distinct visible OUTPUT jack of that ONE node feeding the destination jack — see
         *  GraphEditor's original header comment on this type for why more than one is normal. */
        std::vector<int> jacks;
    };

    /** Resolves the cabling currently feeding `dstJack`, or nullopt when the jack is free, is fed
     *  from more than one NODE (a hand-built mix), or is fed through a mod routing / attenuverter
     *  chain (neither is ever silently rerouted). Insert-in-series needs this to succeed. */
    std::optional<UpstreamLink> findSingleUpstreamAudioLink(juce::AudioProcessorGraph::NodeID dstId, int dstJack) const;

    /** Removes the audio cable between two visible jacks — the exact inverse of connectPorts, so a
     *  collapsed stereo wire drops both raw legs. Caller owns the undo transaction. */
    void disconnectAudioLink(juce::AudioProcessorGraph::NodeID srcId, int srcJack,
                             juce::AudioProcessorGraph::NodeID dstId, int dstJack);

    static constexpr float kSmartConnectionProximityPx = 96.0f;

    /** Whether a proximity suggestion should be offered at all for the CURRENT drag — mode, the
     *  multi-selection-drag guard, and the snippet guard. refreshSmartSuggestions is the main
     *  caller; public because GraphEditor::finalizeModuleDrag (GraphEditorDragDrop.cpp, FRO77 PR3)
     *  also re-checks it before applying, exactly as it did when this lived on GraphEditor. */
    bool shouldOfferSmartConnections(const DragPreviewState& drag) const;

private:
    GraphCanvasHost& host_;

    SmartConnectionMode smartConnectionMode_ = SmartConnectionMode::NewAndUnwired;
    std::optional<bool> insertModifierOverride_; // tests only; empty means read the real keyboard
    // Last modifier state the drag tick saw, so a press/release that happens WITHOUT a mouse move
    // still re-evaluates the suggestions exactly once (see refreshSuggestionsIfInsertModifierChanged).
    bool lastSampledInsertModifier_ = false;
    std::vector<SmartSuggestion> smartSuggestions_;
};
