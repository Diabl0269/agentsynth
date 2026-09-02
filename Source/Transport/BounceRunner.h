#pragma once

#include "BounceExporter.h"
#include <functional>
#include <juce_events/juce_events.h>
#include <memory>

class AudioEngine;

namespace synth {

class BounceSession;

// Drives a bounce to completion via a repeating juce::Timer instead of blocking the message
// thread for the whole render, so a progress dialog stays responsive and Cancel actually works.
//
// This is NOT a second thread. suspendDeviceCallback/resumeDeviceCallback are message-thread
// only, and OfflineTransportDriver's constructor calls prepareForHost, also message-thread only
// (see AudioEngine.h) — "just run bounce() on a worker" would violate both. Instead this renders
// the SAME BounceSession a synchronous BounceExporter::bounce() would, in small chunks, letting
// the message thread's own event loop (repaint, mouse, other timers) run between chunks. At
// 64 blocks/tick and a 10 ms tick that is roughly 70x realtime, so chunking costs nothing anyone
// would notice.
//
// The UI stays PAINTING but is not free to do anything it likes meanwhile: the caller must still
// hold a modal gate (a genuinely modal progress window — enterModalState, not just a visible one)
// and its own "bounce in progress" flag on every path that can mutate the graph, replace the
// document, or quit — see MainComponent::isBounceInProgress_. This class only solves
// responsiveness, not exclusivity.
class BounceRunner : private juce::Timer {
public:
    using CompletionCallback = std::function<void(BounceResult)>;

    // MESSAGE THREAD. Begins the render's setup synchronously (same cost as bounce()'s own setup —
    // fast, not chunked) and starts ticking. onComplete fires exactly once, always from a timer
    // tick (never synchronously from this constructor, even when setup fails outright) so a caller
    // can safely store `this` in a member before onComplete can possibly run.
    BounceRunner(AudioEngine& engine, const juce::File& outFile, const BounceOptions& options,
                 CompletionCallback onComplete, int chunkBlocks = 64, int tickMs = 10);
    ~BounceRunner() override;

    // MESSAGE THREAD. Requests cancellation; the in-flight chunk finishes, then onComplete fires
    // with a cancelled result on the next tick.
    void cancel();

    // 0..1, monotonically non-decreasing while running.
    double getProgress() const noexcept;

private:
    void timerCallback() override;

    std::unique_ptr<BounceSession> session_;
    CompletionCallback onComplete_;
    int chunkBlocks_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BounceRunner)
};

} // namespace synth
