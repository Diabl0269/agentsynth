#pragma once

#include <functional>
#include <juce_core/juce_core.h>

namespace synth {

/** What the user picked in the Collect & Archive prompt. Cancel does nothing. */
enum class CollectArchiveChoice { CollectOnly, CollectAndZip, Cancel };

/** Test/automation seams for Collect & Archive. When set, each REPLACES the real async dialog,
 *  progress window or message box (same idiom as MidiExportSeams). */
struct CollectArchiveSeams {
    /** Reports progress 0..1 (negative = indeterminate); returns false once the user cancelled. */
    using ProgressFn = std::function<bool(double)>;
    using Work = std::function<void(const ProgressFn&)>;

    std::function<void(std::function<void(CollectArchiveChoice)> onChoice)> choicePrompt;
    std::function<void(std::function<void(const juce::File&)> onFile)> zipFilePrompt;
    /** Must run `work`, then call `done(cancelled)` on the message thread. */
    std::function<void(const juce::String& title, bool cancellable, Work work, std::function<void(bool)> done)> runTask;
    std::function<void(const juce::String& title, const juce::String& message)> report;
};

} // namespace synth
