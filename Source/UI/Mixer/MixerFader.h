#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

class AppUndoManager;

// MixerFader.h -- FRO11 (P9-5, docs/mixer.md §5.10): a dB-scale vertical fader bound to a
// ChannelStripModule/MasterModule's own `gain` AudioParameterFloat*, reused by both (their ranges
// are identical -- ChannelStripModule::kMinGainDb/kMaxGainDb).
//
// Undo: a juce::AudioProcessorParameter::Listener on the bound param brackets ONE
// captureBeforeState()/pushSnapshotFromCapture() pair per drag gesture -- the exact mechanism
// ModuleComponent::parameterGestureChanged already uses for every module's own knobs
// (Source/UI/Graph/ModuleComponent/ModuleComponentInteraction.cpp), just triggered from the
// mixer's own slider instead of a module card's.

namespace synth::ui {

class MixerFader
    : public juce::Component
    , private juce::AudioProcessorParameter::Listener {
public:
    MixerFader();
    ~MixerFader() override;

    /** Binds to `param` (already added to a live processor). `graph`/`undoManager` must outlive
     *  every gesture made while bound. Call unbind() (or bind() again) before the param's owning
     *  node can be destroyed. */
    void bind(juce::AudioProcessorGraph& graph, AppUndoManager& undoManager, juce::AudioParameterFloat& param);
    void unbind();

    juce::Slider& getSlider() noexcept { return slider_; }

    void resized() override;

private:
    void parameterValueChanged(int parameterIndex, float newValue) override;
    void parameterGestureChanged(int parameterIndex, bool gestureIsStarting) override;

    juce::Slider slider_;
    juce::Label readout_;
    std::unique_ptr<juce::SliderParameterAttachment> attachment_;

    juce::AudioProcessorGraph* graph_ = nullptr;
    AppUndoManager* undoManager_ = nullptr;
    juce::AudioParameterFloat* param_ = nullptr;
    bool gestureActive_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixerFader)
};

} // namespace synth::ui
