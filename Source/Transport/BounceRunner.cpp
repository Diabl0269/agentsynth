#include "BounceRunner.h"

#include "BounceSession.h"

namespace synth {

BounceRunner::BounceRunner(AudioEngine& engine, const juce::File& outFile, const BounceOptions& options,
                           CompletionCallback onComplete, int chunkBlocks, int tickMs)
    : session_(std::make_unique<BounceSession>(engine, outFile, options))
    , onComplete_(std::move(onComplete))
    , chunkBlocks_(juce::jmax(1, chunkBlocks)) {
    startTimer(juce::jmax(1, tickMs));
}

BounceRunner::~BounceRunner() { stopTimer(); }

void BounceRunner::cancel() {
    if (session_ != nullptr)
        session_->requestCancel();
}

double BounceRunner::getProgress() const noexcept { return session_ != nullptr ? session_->getProgress() : 0.0; }

void BounceRunner::timerCallback() {
    if (!session_->isRangeDone()) {
        session_->stepRange(chunkBlocks_);
        return;
    }
    if (!session_->isTailDone()) {
        session_->stepTail(chunkBlocks_);
        return;
    }

    stopTimer();
    const auto result = session_->finish();
    // Moved out first: onComplete may destroy `this` (it owns the runner, closing the dialog on
    // completion is the expected shape), so nothing below this line may touch a member.
    const auto onComplete = std::move(onComplete_);
    onComplete(result);
}

} // namespace synth
