#pragma once

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

/** Runs one job on a background thread behind JUCE's modal progress window. Self-deleting. */
class ProgressTaskWindow : private juce::ThreadWithProgressWindow {
public:
    /** Reports progress 0..1 (negative = indeterminate); returns false once the user cancelled. */
    using ProgressFn = std::function<bool(double)>;
    using Work = std::function<void(const ProgressFn&)>;

    /** Message thread only. `work` runs on a background thread and must not touch the graph or UI;
     *  `done(cancelled)` runs on the message thread afterwards. */
    static void launch(const juce::String& title, bool cancellable, Work work,
                       std::function<void(bool cancelled)> done);

private:
    ProgressTaskWindow(const juce::String& title, bool cancellable, Work work, std::function<void(bool)> done);
    void run() override;
    void threadComplete(bool userPressedCancel) override;

    Work work_;
    std::function<void(bool)> done_;
};

} // namespace synth::ui
