#include "RenderProgressDialog.h"

namespace synth::ui {

namespace {
constexpr int kPollMs = 40;
} // namespace

RenderProgressDialog::RenderProgressDialog(std::function<double()> getProgress)
    : getProgress_(std::move(getProgress)) {
    setTitle("Rendering to audio");
    setDescription("Rendering to a new audio track. Press Escape or choose Cancel to stop.");
    setWantsKeyboardFocus(false);

    addAndMakeVisible(statusLabel_);
    addAndMakeVisible(progressBar_);
    addAndMakeVisible(cancelButton_);

    statusLabel_.setTitle("Render status");
    progressBar_.setTitle("Render progress");
    cancelButton_.setTitle("Cancel render");
    cancelButton_.setTooltip("Stop rendering and keep the project as it was (Esc)");
    cancelButton_.setWantsKeyboardFocus(true);
    cancelButton_.onClick = [this] {
        if (onCancel)
            onCancel();
    };

    setSize(360, 120);
    startTimer(kPollMs);
}

void RenderProgressDialog::focusCancelButton() { cancelButton_.grabKeyboardFocus(); }

bool RenderProgressDialog::keyPressed(const juce::KeyPress& key) {
    if (key == juce::KeyPress::escapeKey) {
        if (onCancel)
            onCancel();
        return true;
    }
    return false;
}

void RenderProgressDialog::timerCallback() {
    if (getProgress_)
        progressValue_ = juce::jlimit(0.0, 1.0, getProgress_());
}

void RenderProgressDialog::paint(juce::Graphics& g) {
    g.fillAll(findColour(juce::ResizableWindow::backgroundColourId));
}

void RenderProgressDialog::resized() {
    auto area = getLocalBounds().reduced(12);
    statusLabel_.setBounds(area.removeFromTop(28));
    progressBar_.setBounds(area.removeFromTop(24));
    area.removeFromTop(8);
    cancelButton_.setBounds(area.removeFromTop(28).removeFromRight(90));
}

} // namespace synth::ui
