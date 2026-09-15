#pragma once

#include "MixerColumnHeader.h"
#include "MixerFader.h"
#include "MixerMeter.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

class AppUndoManager;

// MixerMasterColumn.h -- FRO11 (P9-5, docs/mixer.md §5.1/§5.10): Master's column -- fader/mute/
// meter/dB readout on MasterModule's own gain/mute params, no pan, no solo, no insert list.
namespace synth::ui {

class MixerMasterColumn : public juce::Component {
public:
    MixerMasterColumn();

    void configure(juce::AudioProcessorGraph& graph, AppUndoManager& undoManager);
    void setNodeId(juce::AudioProcessorGraph::NodeID nodeId);

    /** One 10 Hz tick, same driving chain as MixerColumnComponent::refreshMeter(). */
    void refreshMeter();

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    juce::AudioProcessorGraph* graph_ = nullptr;
    AppUndoManager* undoManager_ = nullptr;
    juce::AudioProcessorGraph::NodeID nodeId_;

    MixerColumnHeader header_;
    MixerFader fader_;
    MixerMeter meter_;
    juce::TextButton muteButton_{"M"};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixerMasterColumn)
};

} // namespace synth::ui
