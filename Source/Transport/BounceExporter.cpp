#include "BounceExporter.h"

#include "BounceSession.h"
#include <limits>

namespace synth {

// bounce() is BounceSession driven to completion in one call, with no chunking - see
// BounceSession.h for why the choreography lives there instead of here. BounceRunner
// (Source/Transport/BounceRunner.h) is the other driver, for a caller that wants to interleave
// the render with a UI progress tick instead of blocking for the whole take.
BounceResult BounceExporter::bounce(AudioEngine& engine, const juce::File& outFile, const BounceOptions& options,
                                    const ProgressCallback& progress) {
    BounceSession session(engine, outFile, options, progress);
    constexpr int kUnbounded = std::numeric_limits<int>::max();
    while (!session.isRangeDone())
        session.stepRange(kUnbounded);
    while (!session.isTailDone())
        session.stepTail(kUnbounded);
    return session.finish();
}

} // namespace synth
