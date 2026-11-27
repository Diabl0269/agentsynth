#pragma once

#include <algorithm>

namespace synth {

/** The Render cores setting (Settings > Audio): 0 is Auto, n >= 1 renders on n cores (n - 1 workers beside the audio
 *  thread). Auto takes every core but one, at most kAutoRenderCores. */
inline constexpr int kAutoRenderCores = 4;
inline constexpr int kMaxRenderCores = 8;
inline constexpr int kDefaultRenderCores = 1; // off: the plan follows a graph edit a message-loop turn late
inline int trackWorkersFor(int renderCores, int machineCores) noexcept {
    const int cores = std::max(1, machineCores);
    const int wanted = renderCores <= 0 ? std::min(kAutoRenderCores, cores - 1) : std::min(renderCores, cores);
    return std::max(0, std::min(wanted, kMaxRenderCores) - 1);
}

} // namespace synth
