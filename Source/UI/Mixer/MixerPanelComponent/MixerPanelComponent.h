#pragma once

#include "MacroSet.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "UI/Mixer/MixerDirectColumn.h"
#include "UI/Mixer/MixerMasterColumn.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <vector>

class AppUndoManager;
class GraphEditor;
class AudioEngine;

namespace synth::ui {
class MixerColumnComponent;
}

// MixerPanelComponent.h -- FRO11 (P9-5, docs/mixer.md §5.10): the columns container -- a
// horizontally scrolling row of columns built from synth::buildMixerSnapshot(), rebuilt on every
// graph/timeline/macro change notification. Pure layout + rebuild-on-change; owns nothing
// audio-specific itself.
namespace synth::ui {

class MixerPanelComponent : public juce::Component {
public:
    MixerPanelComponent();
    ~MixerPanelComponent() override;

    void configure(juce::AudioProcessorGraph& graph, synth::TimelineDoc& doc, synth::MacroSet& macros,
                   AppUndoManager& undoManager, GraphEditor& graphEditor, AudioEngine& audioEngine);

    /** Forwarded from MixerDirectColumn -- MixerDockComponent wires this to
     *  MainComponent::makeChannelForNode, the same "Make channel" entry point every other trigger
     *  (header menu, canvas menu, module-card menu) already uses. */
    std::function<void(juce::AudioProcessorGraph::NodeID)> onMakeChannelForNode;

    /** Forwarded from every column's insert list after an add/reorder/remove mutation --
     *  MixerDockComponent wires this to MainComponent::reconcileTimelineAfterGraphChange. */
    std::function<void()> onGraphMutated;

    /** Re-runs buildMixerSnapshot() and rebuilds the column set. Cheap enough to call on every
     *  graph/timeline/macro change (a handful of strips, never per-frame) -- see MixerModel.h. */
    void rebuild();

    int getColumnCount() const noexcept;

    /** The Nth strip column (in the same track order buildMixerSnapshot returns), or null out of
     *  range -- a stable handle for a test to drive real mouse events against without depending on
     *  juce::Viewport's own internal child layout (scrollbars, the viewed-content wrapper). */
    MixerColumnComponent* getStripColumnForTest(int index) const {
        return index >= 0 && index < (int)stripColumns_.size() ? stripColumns_[(size_t)index].get() : nullptr;
    }

    /** FRO11's revealChannelForTrack redirect: scrolls the column for `stripId` into view and
     *  selects it (MixerColumnComponent::setSelected), clearing selection on every other column.
     *  False when no column matches (nothing to reveal -- the caller falls back to the canvas). */
    bool revealColumn(juce::AudioProcessorGraph::NodeID stripId);

    /** One 10 Hz tick while the mixer tab is showing -- ticks every column's meter. */
    void refreshMeters();

    void resized() override;

private:
    void selectOnCanvas(const juce::String& targetId);

    juce::Viewport viewport_;
    juce::Component content_;

    juce::AudioProcessorGraph* graph_ = nullptr;
    synth::TimelineDoc* doc_ = nullptr;
    synth::MacroSet* macros_ = nullptr;
    AppUndoManager* undoManager_ = nullptr;
    GraphEditor* graphEditor_ = nullptr;
    AudioEngine* audioEngine_ = nullptr;

    std::vector<std::unique_ptr<MixerColumnComponent>> stripColumns_;
    std::unique_ptr<MixerDirectColumn> directColumn_;
    std::unique_ptr<MixerMasterColumn> masterColumn_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixerPanelComponent)
};

} // namespace synth::ui
