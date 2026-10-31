#pragma once

// The one curve every curve view edits: the response curve of one attenuverter, found by its node id on every read
// and write so a view never holds a pointer into a processor that undo or a delete may free.

#include "Modules/ModCurve.h"
#include <functional>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <optional>

class GraphEditor;

namespace synth::ui {

class ModCurveEditTarget {
public:
    ModCurveEditTarget(GraphEditor& editor, juce::AudioProcessorGraph::NodeID attenuverter);

    juce::AudioProcessorGraph::NodeID attenuverterId() const noexcept { return atten_; }
    /** False once the attenuverter or the editor is gone. */
    bool exists() const;
    /** Empty when the attenuverter is gone. */
    std::optional<synth::ModCurve> read() const;

    /** Opens one undo step; every `apply` until `commitGesture` belongs to it. */
    void beginGesture();
    void commitGesture();
    /** Sets the curve and repaints the canvas. A vanished node or an equal curve changes nothing. */
    void apply(const synth::ModCurve& curve);
    /** begin + apply + commit: a preset click or Reset. Adds no undo step when the curve is already equal. */
    void setOnce(const synth::ModCurve& curve);

    /** "Cutoff" style names behind any macro ports; empty when unknown. */
    juce::String sourceName() const;
    juce::String destinationName() const;
    /** "Curve: <source> to <destination>". */
    juce::String title() const;

    /** Runs `hook` after any view changed this attenuverter's curve, for as long as the returned handle lives: how the
     *  box, the window and the chips stay in step. Message thread only. */
    std::shared_ptr<void> watchChanges(std::function<void()> hook) const;

    /** Runs `hook` after every undo or redo for as long as the returned handle lives (null with no undo manager). */
    std::shared_ptr<void> watchRestore(std::function<void()> hook) const;

private:
    juce::Component::SafePointer<GraphEditor> editor_;
    juce::AudioProcessorGraph::NodeID atten_;
};

} // namespace synth::ui
