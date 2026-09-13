#include "StemRunner.h"

#include "StemSession.h"

namespace synth {

StemRunner::StemRunner(AudioEngine& engine, const juce::File& destinationFolder, const BounceOptions& options,
                       CompletionCallback onComplete, int chunkBlocks, int tickMs)
    : session_(std::make_unique<StemSession>(engine, destinationFolder, options))
    , onComplete_(std::move(onComplete))
    , chunkBlocks_(juce::jmax(1, chunkBlocks)) {
    startTimer(juce::jmax(1, tickMs));
}

StemRunner::~StemRunner() { stopTimer(); }

void StemRunner::cancel() {
    if (session_ != nullptr)
        session_->requestCancel();
}

double StemRunner::getProgress() const noexcept { return session_ != nullptr ? session_->getProgress() : 0.0; }

void StemRunner::timerCallback() {
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
    // Moved out first: onComplete may destroy `this` - see BounceRunner::timerCallback's own
    // comment.
    const auto onComplete = std::move(onComplete_);
    onComplete(result);
}

} // namespace synth
