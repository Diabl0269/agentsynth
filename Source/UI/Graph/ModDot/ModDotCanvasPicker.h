#pragma once

// "Pick on canvas": a transparent layer over the graph canvas that outlines the module under the pointer when it can
// be a source for the knob, swallows every press, and reports a press on such a module. Esc stops it. It never
// edits the graph itself. docs/modules/modulation.md#the-mod-dot-menu.

#include <functional>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

class GraphEditor;

namespace synth::ui {

class ModDotCanvasPicker final : public juce::Component {
public:
    /** `isEligible` says whether a module may be picked (not the knob's own card, not a source already on the knob). */
    ModDotCanvasPicker(GraphEditor& editor, std::function<bool(juce::AudioProcessorGraph::NodeID)> isEligible);
    ~ModDotCanvasPicker() override;

    /** Covers `editor` and starts swallowing presses. */
    void begin();
    /** The topmost eligible module card under `screenPoint`, or an invalid id. */
    juce::AudioProcessorGraph::NodeID eligibleNodeAt(juce::Point<int> screenPoint) const;

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

    GraphEditor& editor_;
    std::function<bool(juce::AudioProcessorGraph::NodeID)> isEligible_;
    juce::AudioProcessorGraph::NodeID hovered_;
};

} // namespace synth::ui
