#pragma once

#include <atomic>
#include <functional>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

/**
 * Binds ONE hosted-plugin parameter (a juce::HostedAudioProcessorParameter, never a RangedAudioParameter,
 * so juce::SliderParameterAttachment cannot be used) to one widget, live in both directions.
 * Bound by a Slider (0..1), a ToggleButton (on = value >= 0.5) or a ComboBox (item = value string).
 * Message thread only, except parameterValueChanged/parameterGestureChanged, which may arrive on any thread.
 * The widget must outlive the attachment; the parameter must outlive it OR be released with detach() first.
 * See docs/control/plugin-card-layout.md#card-rendering-as-built-fro128.
 */
class HostedParameterAttachment final
    : private juce::AudioProcessorParameter::Listener
    , private juce::AsyncUpdater
    , private juce::Slider::Listener
    , private juce::ComboBox::Listener
    , private juce::Button::Listener {
public:
    HostedParameterAttachment(juce::AudioProcessorParameter& param, juce::Slider& slider);
    HostedParameterAttachment(juce::AudioProcessorParameter& param, juce::ToggleButton& toggle);
    /** Fills `combo` with choiceStrings(param); the parameter must offer at least two. */
    HostedParameterAttachment(juce::AudioProcessorParameter& param, juce::ComboBox& combo);
    ~HostedParameterAttachment() override;

    /** The value strings a Choice widget offers: the parameter's own, else generated from getNumSteps() when small. */
    static juce::StringArray choiceStrings(const juce::AudioProcessorParameter& param);

    /** Removes the listener, drops a queued update and forgets the parameter. Idempotent. Message thread only. */
    void detach();

    /** Cancels a queued update WITHOUT touching the parameter; for an attachment about to be leaked because its
     * parameter is gone. */
    void abandon();

    /** Shows `normalized` on the widget without writing it back; for a value the parameter's own listeners never hear
     * (automation). */
    void reflectValue(float normalized) { showValue(normalized); }

    /** True until detach()/abandon(). */
    bool isBound() const noexcept { return param_ != nullptr; }
    const juce::AudioProcessorParameter* getParameter() const noexcept { return param_; }

    /** Fires for every gesture start/end on the parameter, from whichever thread reports it: act only on the message
     * thread. */
    std::function<void(bool starting)> onGestureChanged;

private:
    enum class Kind { Slider, Toggle, Combo };

    void parameterValueChanged(int, float newValue) override;
    void parameterGestureChanged(int, bool starting) override;
    void handleAsyncUpdate() override;

    void sliderValueChanged(juce::Slider*) override;
    void sliderDragStarted(juce::Slider*) override;
    void sliderDragEnded(juce::Slider*) override;
    void comboBoxChanged(juce::ComboBox*) override;
    void buttonClicked(juce::Button*) override;

    void bind();
    void showValue(float normalized);
    void writeComplete(float normalized);
    void openGesture();
    void closeGesture();

    Kind kind_;
    juce::AudioProcessorParameter* param_;
    juce::Slider* slider_ = nullptr;
    juce::ToggleButton* toggle_ = nullptr;
    juce::ComboBox* combo_ = nullptr;
    int comboCount_ = 0;

    std::atomic<float> pendingValue_{0.0f};
    bool ignoreCallbacks_ = false;
    bool gestureOpen_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HostedParameterAttachment)
};

} // namespace synth::ui
