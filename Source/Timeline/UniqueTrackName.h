#pragma once

#include <juce_core/juce_core.h>
#include <vector>

namespace synth {

/** `base` if no entry of `existing` equals it, else the lowest "<base> <n>" (n >= 2) that is free.
 *  Exact, case-sensitive comparison, like the track names themselves. Pure; message thread. */
inline juce::String uniqueNameAmong(const juce::String& base, const std::vector<juce::String>& existing) {
    const auto taken = [&existing](const juce::String& candidate) {
        for (const auto& name : existing)
            if (name == candidate)
                return true;
        return false;
    };
    if (!taken(base))
        return base;
    for (int n = 2;; ++n)
        if (const auto candidate = base + " " + juce::String(n); !taken(candidate))
            return candidate;
}

} // namespace synth
