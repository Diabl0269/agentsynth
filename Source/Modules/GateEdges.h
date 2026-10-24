#pragma once

#include "SchmittTrigger.h"
#include <algorithm>
#include <array>
#include <cstddef>

/** One voice's Gate input over a block: the samples where it rises, where the oscillator restarts the
 *  voice's phase (a new note). Shared by the Oscillator and the Wavetable.
 */
struct GateEdges {
    static constexpr float kThreshold = 0.1f; ///< low, so a soft Vel -> Gate note still restarts
    static constexpr int kMinSpacing = 16;    ///< edges closer than this many samples to the last kept one are ignored
    static constexpr int kMaxEdges = 256;     ///< room for one edge per kMinSpacing samples of a 4096-sample block

    std::array<int, kMaxEdges> offsets{};
    int count = 0;
    int next = 0;
    SchmittTrigger trigger;
    int sinceLast = kMinSpacing; ///< samples since the last kept edge, carried across blocks

    /** Finds this block's rising edges, keeping those at least kMinSpacing samples after the last kept one (also
        across a block boundary), so an audio-rate gate restarts at a steady rate whatever the block size. `gate`
        may be null (an unpatched or silent input); a null block also drops the trigger's state, so the next real
        gate starts from low. */
    void scan(const float* gate, int numSamples) noexcept {
        count = 0;
        next = 0;
        if (gate == nullptr) {
            trigger.reset();
            sinceLast = kMinSpacing;
            return;
        }
        for (int s = 0; s < numSamples; ++s) {
            sinceLast = std::min(sinceLast + 1, kMinSpacing);
            if (trigger.process(gate[s], kThreshold) == SchmittTrigger::Edge::Rising && sinceLast >= kMinSpacing &&
                count < kMaxEdges) {
                offsets[(std::size_t)count++] = s;
                sinceLast = 0;
            }
        }
    }

    /** True once per edge, on the sample it lands; call with ascending `sample` within a block. */
    bool firesAt(int sample) noexcept {
        if (next < count && offsets[(std::size_t)next] == sample) {
            ++next;
            return true;
        }
        return false;
    }

    void reset() noexcept {
        count = next = 0;
        trigger.reset();
        sinceLast = kMinSpacing;
    }
};
