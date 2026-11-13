// ProgressTaskWindow.cpp — a self-deleting juce::ThreadWithProgressWindow for one-shot file jobs
// (Collect & Archive's copy and zip steps).
#include "ProgressTaskWindow.h"

namespace synth::ui {

void ProgressTaskWindow::launch(const juce::String& title, bool cancellable, Work work,
                                std::function<void(bool cancelled)> done) {
    // Owned by itself from here on: threadComplete deletes it.
    auto* window = new ProgressTaskWindow(title, cancellable, std::move(work), std::move(done));
    window->launchThread();
}

// A non-cancellable job gets no Cancel button at all: JUCE kills a thread that ignores the stop
// request after its timeout, which must never happen halfway through a file write.
ProgressTaskWindow::ProgressTaskWindow(const juce::String& title, bool cancellable, Work work,
                                       std::function<void(bool)> done)
    : juce::ThreadWithProgressWindow(title, true, cancellable)
    , work_(std::move(work))
    , done_(std::move(done)) {}

void ProgressTaskWindow::run() {
    work_([this](double progress) {
        setProgress(progress);
        return !threadShouldExit();
    });
}

void ProgressTaskWindow::threadComplete(bool userPressedCancel) {
    auto done = std::move(done_);
    delete this;
    if (done)
        done(userPressedCancel);
}

} // namespace synth::ui
