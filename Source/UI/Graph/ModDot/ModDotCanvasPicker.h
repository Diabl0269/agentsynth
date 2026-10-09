#pragma once

// "Pick on canvas": a transparent layer over the graph canvas that outlines the module under the pointer when it can
// be a source for the knob, swallows every press, and reports a press on such a module. Esc stops it. It never
// edits the graph itself. A second mode (the port connections panel's) picks a JACK or a knob instead of a module: the
// jack or knob under the pointer is outlined when the owner calls it eligible, and a press on it is reported.
// docs/modules/modulation.md#the-mod-dot-menu, docs/layout/cables.md#port-connections-panel.

#include <functional>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>

class GraphEditor;

namespace synth::ui {

class ModDotCanvasPicker final : public juce::Component {
public:
    /** A jack (or, with `knob`, the knob whose raw CV channel is `index`) of a module card, as the pick mode that
     *  picks jacks sees it. `area` is in the card's own space. */
    struct JackHit {
        juce::AudioProcessorGraph::NodeID node;
        juce::Rectangle<int> area;
        int index = 0;
        bool isInput = false;
        bool isMidi = false;
        bool knob = false;
    };

    /** `isEligible` says whether a module may be picked (not the knob's own card, not a source already on the knob). */
    ModDotCanvasPicker(GraphEditor& editor, std::function<bool(juce::AudioProcessorGraph::NodeID)> isEligible);
    /** The jack-picking mode: `isEligible` says whether a jack or knob may be picked. */
    ModDotCanvasPicker(GraphEditor& editor, std::function<bool(const JackHit&)> isEligible);
    ~ModDotCanvasPicker() override;

    /** Covers `editor` and starts swallowing presses. */
    void begin();
    /** The topmost eligible module card under `screenPoint`, or an invalid id. */
    juce::AudioProcessorGraph::NodeID eligibleNodeAt(juce::Point<int> screenPoint) const;

    /** The eligible jack or knob under `screenPoint`, if any (the jack-picking mode). */
    std::optional<JackHit> eligibleJackAt(juce::Point<int> screenPoint) const;
    /** A press landed on an eligible jack or knob (the jack-picking mode). The layer is still up: the owner ends it. */
    std::function<void(const JackHit&)> onPickedJack;
    /** A press landed on an eligible module. The layer is still up: the owner ends it. */
    std::function<void(juce::AudioProcessorGraph::NodeID)> onPicked;
    /** Esc was pressed with the layer holding the keyboard. */
    std::function<void()> onEscape;

    void paint(juce::Graphics& g) override;
    void parentSizeChanged() override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    bool keyPressed(const juce::KeyPress& key) override;

private:
    void setHovered(juce::AudioProcessorGraph::NodeID node);
    void setHoveredJack(std::optional<JackHit> hit);

    GraphEditor& editor_;
    std::function<bool(juce::AudioProcessorGraph::NodeID)> isEligible_;
    juce::AudioProcessorGraph::NodeID hovered_;
    std::function<bool(const JackHit&)> isEligibleJack_;
    std::optional<JackHit> hoveredJack_;
};

} // namespace synth::ui
