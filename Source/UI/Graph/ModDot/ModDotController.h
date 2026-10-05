#pragma once

// The mod dot's behaviour: pressing the small dot a knob (or fader) shows where a modulation cable lands
// and dragging up or down changes the amount of the source the user chose last, live, with a tooltip
// beside the knob. One controller per GraphEditor; cards route the dot's mouse and key events here.
//
//  * "Last chosen" is session-only state, per (card, destination channel): the source the next
//    ticket's menu picked, else the first attenuverter routing on the knob.
//  * A press that moves under 3 px is a click (onModDotClicked); past it the gesture takes ONE undo
//    snapshot, lazily, and commits it on release. Esc restores the amount the press started from and
//    pushes no undo step.
//  * A key step (the dot's own Tab stop) is one undo step per press.
//  * A double-click on the dot (or on a visible CV jack bound to the knob) removes the knob's only source, chain
//    and all, as one undo step; with several it opens the panel with every remove button highlighted. It is
//    gated by GraphEditor::getDoubleClickPortDisconnectEnabled(). A global mouse listener also catches the second
//    press of a quick double-click while the panel is open.
//  * A click opens the dot's panel (ModDotPopover) beside the dot, a second click closes it; the panel's edits go
//  through the controller's
//    host hooks (remove and "show in timeline" belong to the app window) with editor-only fallbacks.
//
// docs/modules/modulation.md#drag-to-knob-modulation.

#include "AudioEngine/ModulationRoutingTypes.h"
#include "KnobModSources.h"
#include "ModDotTooltip.h"
#include "UI/Timeline/AutomationLanes/Modulators/ModulatorInfo.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <map>
#include <memory>
#include <optional>
#include <utility>

class GraphEditor;

namespace synth::ui {

class ModDotPopover;

/** The amount a source added from the dot's panel starts at: a quarter way, clearly audible with room both ways. */
inline constexpr float kModDotNewSourceDepth = 0.25f;

class ModDotController {
public:
    static constexpr float kClickThresholdPx = 3.0f;
    static constexpr float kAmountPerPixel = 0.01f;

    /** `canvas` is the component the tooltip paints on (the editor's content component). */
    ModDotController(GraphEditor& editor, juce::Component& canvas);
    ~ModDotController();

    /** A plain click on a dot, or Return/Space on its button; `anchor` is the dot's button. Opens the dot's panel
     *  (openPopover) unless a test replaces it. */
    std::function<void(juce::AudioProcessorGraph::NodeID card, int destChannel, juce::Component& anchor)>
        onModDotClicked;

    // ---- the dot's panel ----
    /** Shows `content` in a window beside `anchor` (ModDotPanelFrame); a headless test captures the content instead
     *  (a real window needs a display). */
    using PopoverLauncher = std::function<void(std::unique_ptr<juce::Component> content, juce::Component& anchor)>;
    PopoverLauncher popoverLauncher;
    /** What the app window does for the panel; each is optional, with an editor-only fallback where there is one. */
    struct Host {
        /** Remove one source as the timeline's "Remove modulator" does (one undo step with its amount lane, asks
         *  before deleting an LFO left with nothing). Fallback: GraphEditor::removeModulationChain. */
        std::function<void(const ModulatorInfo&)> removeModulator;
        /** Open the timeline and bring the source's modulator row into view. No fallback. */
        std::function<void(const ModulatorInfo&)> revealModulator;
        /** Where the "Show info tooltips" preference lives, for the panel window's tooltip (null: always on). */
        juce::ApplicationProperties* appProperties = nullptr;
    } host;

    void openPopover(juce::AudioProcessorGraph::NodeID card, int destChannel, juce::Component& anchor);
    /** The open panel, or null. */
    ModDotPopover* getPopover() const;
    void closePopover();
    /** Stops the open panel's "Pick on canvas" mode, if it is on (the canvas is about to lose its cards). */
    void endCanvasPick();
    /** A double-click on the knob's dot (`anchor` is its button, or the card for a jack without one): with one
     *  source, closes the panel and removes that chain (GraphEditor::removeModulationChain, one undo step); with
     *  several, opens the panel, or reuses it, in remove-highlight mode. Nothing without a source. */
    void dotDoubleClicked(juce::AudioProcessorGraph::NodeID card, int destChannel, juce::Component& anchor);
    /** The editor's 30 Hz tick: the open panel follows the graph. */
    void tickPopover();
    /** Called by the panel as it goes away. */
    void popoverClosed(ModDotPopover* popover);
    void removeSource(juce::AudioProcessorGraph::NodeID card, int destChannel,
                      juce::AudioProcessorGraph::NodeID attenuverterId);
    void revealSource(juce::AudioProcessorGraph::NodeID card, int destChannel,
                      juce::AudioProcessorGraph::NodeID attenuverterId);

    // ---- last-chosen source ----
    /** The attenuverter a drag or key step on (`card`, `destChannel`) changes: the stored choice while
     *  that routing still exists, else the first attenuverter routing. Invalid when there is none. */
    juce::AudioProcessorGraph::NodeID chosenAttenuverter(juce::AudioProcessorGraph::NodeID card, int destChannel) const;
    void setLastChosen(juce::AudioProcessorGraph::NodeID card, int destChannel,
                       juce::AudioProcessorGraph::NodeID attenuverterId);

    // ---- pointer gesture (CardControlGestures::onModDotGesture) ----
    /** `knob` is the control pressed, `anchor` the dot's button (or the knob itself when it has none);
     *  `e` is in `knob`'s space. */
    void pressed(juce::AudioProcessorGraph::NodeID card, int destChannel, juce::Slider& knob, juce::Component& anchor,
                 const juce::MouseEvent& e);
    void dragged(const juce::MouseEvent& e);
    void released(const juce::MouseEvent& e);
    /** Esc during a drag: back to the starting amount, no undo step. */
    void cancel();
    bool isGestureActive() const noexcept { return gesture_.has_value(); }
    bool hasCrossedThreshold() const noexcept { return gesture_.has_value() && gesture_->started; }

    // ---- keyboard ----
    /** One undo step: the chosen source's amount += `delta` (clamped), tooltip and announcement as in a drag. */
    void step(juce::AudioProcessorGraph::NodeID card, int destChannel, juce::Slider& knob, float delta);

    /** True when the routing set differs from the one the last call saw (a cheap signature of every
     *  routing's kind, endpoints and attenuverter), so the 30 Hz tick re-syncs the dot buttons only then. */
    bool routingsChanged(const std::vector<ModulationRouting>& routings);

    // ---- tooltip ----
    ModDotTooltip& getTooltip() noexcept { return tooltip_; }
    void paintTooltip(juce::Graphics& g) { tooltip_.paint(g); }

private:
    struct Gesture {
        juce::AudioProcessorGraph::NodeID card;
        int destChannel = 0;
        juce::AudioProcessorGraph::NodeID attenuverter;
        juce::Component::SafePointer<juce::Slider> knob;
        juce::Component::SafePointer<juce::Component> anchor;
        juce::Point<float> startPos;
        float startAmount = 0.0f;
        bool started = false;
        bool doubleClick = false; // handled on the press; the rest of the press does nothing
    };

    void setAmount(juce::AudioProcessorGraph::NodeID attenuverter, float target);
    void showTooltip(juce::AudioProcessorGraph::NodeID card, int destChannel,
                     juce::AudioProcessorGraph::NodeID attenuverter, juce::Slider& knob);
    void announce(const juce::String& name, float amount);
    void scheduleKeyHide();

    GraphEditor& editor_;
    juce::Component::SafePointer<juce::Component> popover_;
    ModDotTooltip tooltip_;
    std::map<std::pair<juce::uint32, int>, juce::uint32> lastChosen_;
    std::optional<Gesture> gesture_;
    int lastAnnouncedPercent_ = 0;
    juce::uint64 routingSignature_ = 0;
    bool cancelled_ = false;

    // Hides the tooltip a moment after the last key step (a drag hides it on release).
    struct KeyHideTimer final : juce::Timer {
        std::function<void()> fire;
        void timerCallback() override {
            stopTimer();
            if (fire)
                fire();
        }
    } keyHideTimer_;
};

} // namespace synth::ui
