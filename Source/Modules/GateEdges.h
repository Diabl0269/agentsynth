#pragma once

#include "SchmittTrigger.h"
#include <array>

/** One voice's Gate input over a block: the samples where it rises, where the oscillator restarts the
 *  voice's phase (a new note). Shared by the Oscillator and the Wavetable.
 */
struct GateEdges {
    static constexpr float kThreshold = 0.1f; ///< low, so a soft Vel -> Gate note still restarts
    static constexpr int kMaxEdges = 8;       ///< more notes than this on one voice in one block are dropped

    std::array<int, kMaxEdges> offsets{};
    int count = 0;
    int next = 0;
    SchmittTrigger trigger;

    /** Finds this block's rising edges. `gate` may be null (an unpatched or silent input); a null block
        also drops the trigger's state, so the next real gate starts from low. */
    void scan(const float* gate, int numSamples) noexcept {
        count = 0;
        next = 0;
        if (gate == nullptr) {
            trigger.reset();
            return;
        }
        for (int s = 0; s < numSamples; ++s) {
            if (trigger.process(gate[s], kThreshold) == SchmittTrigger::Edge::Rising && count < kMaxEdges)
                offsets[(size_t)count++] = s;
        }
    }

    /** True once per edge, on the sample it lands; call with ascending `sample` within a block. */
    bool firesAt(int sample) noexcept {
        if (next < count && offsets[(size_t)next] == sample) {
            ++next;
            return true;
        }
        return false;
    }

    void reset() noexcept {
        count = next = 0;
        trigger.reset();
    }
};
