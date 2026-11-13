#pragma once

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

/** The small modal panel shown while a selection renders to audio: what is happening, a progress bar and Cancel.
 *  Hosted by a juce::DialogWindow that MainComponent launches; this class only polls `getProgress` and reports the
 *  user's wish to stop. Message thread only. */
class RenderProgressDialog
    : public juce::Component
    , private juce::Timer {
public:
    /** `getProgress` returns 0..1 and is polled while the dialog is showing. */
    explicit RenderProgressDialog(std::function<double()> getProgress);

    /** Fired by the Cancel button and by Escape; the host stops the render and closes the window. */
    std::function<void()> onCancel;

    /** Puts keyboard focus on Cancel, the dialog's only control. */
    void focusCancelButton();

    juce::TextButton& getCancelButtonForTest() noexcept { return cancelButton_; }

    bool keyPressed(const juce::KeyPress& key) override;
    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;

    std::function<double()> getProgress_;
    juce::Label statusLabel_{"renderStatusLabel", "Rendering to audio..."};
    double progressValue_ = 0.0;
    juce::ProgressBar progressBar_{progressValue_};
    juce::TextButton cancelButton_{"Cancel"};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RenderProgressDialog)
};

} // namespace synth::ui
