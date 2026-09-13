#pragma once

#include "StemExporter.h"
#include <functional>
#include <juce_events/juce_events.h>
#include <memory>

class AudioEngine;

namespace synth {

class StemSession;

// The chunked, message-thread-timer-driven twin of StemExporter::exportStems() — StemSession's own
// counterpart to BounceRunner (Source/Transport/BounceRunner.h). See that class's header comment for
// why chunking exists and why this is safe only from the message thread; everything there applies
// here unchanged, with StemSession standing in for BounceSession.
class StemRunner : private juce::Timer {
public:
    using CompletionCallback = std::function<void(StemResult)>;

    // MESSAGE THREAD. Begins the render's setup synchronously and starts ticking. onComplete fires
    // exactly once, always from a timer tick.
    StemRunner(AudioEngine& engine, const juce::File& destinationFolder, const BounceOptions& options,
               CompletionCallback onComplete, int chunkBlocks = 64, int tickMs = 10);
    ~StemRunner() override;

    // MESSAGE THREAD. Requests cancellation; the in-flight chunk finishes, then onComplete fires
    // with a cancelled result on the next tick.
    void cancel();

    // 0..1, monotonically non-decreasing while running.
    double getProgress() const noexcept;

private:
    void timerCallback() override;

    std::unique_ptr<StemSession> session_;
    CompletionCallback onComplete_;
    int chunkBlocks_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StemRunner)
};

} // namespace synth
