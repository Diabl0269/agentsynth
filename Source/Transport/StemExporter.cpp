#include "StemExporter.h"

#include "../AudioEngine.h"
#include "StemSession.h"
#include <limits>

namespace synth {

bool StemExporter::hasChannelStrips(AudioEngine& engine) { return !collectStemStrips(engine.getGraph()).empty(); }

// exportStems() is StemSession driven to completion in one call, with no chunking - the same
// relationship BounceExporter::bounce() has to BounceSession, and for the same reason: StemRunner
// (Source/Transport/StemRunner.h) is the other driver, for a caller that wants to interleave the
// render with a UI progress tick.
StemResult StemExporter::exportStems(AudioEngine& engine, const juce::File& destinationFolder,
                                     const BounceOptions& options, const ProgressCallback& progress) {
    StemSession session(engine, destinationFolder, options, progress);
    constexpr int kUnbounded = std::numeric_limits<int>::max();
    while (!session.isRangeDone())
        session.stepRange(kUnbounded);
    while (!session.isTailDone())
        session.stepTail(kUnbounded);
    return session.finish();
}

} // namespace synth
