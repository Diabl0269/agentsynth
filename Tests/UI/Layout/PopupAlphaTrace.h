// PopupAlphaTrace.h -- reading a popup's opacity frame by frame. A popup that opens and closes shows one rise to full
// opacity and one fall to nothing; a rise after a fall (or a fall before the first rise) is a flicker: the popup
// closed and was opened again, or showed whole for a frame before it faded.
#pragma once

#include <vector>

namespace synth::test {

/** True when `alphas` rise (or hold), then fall (or hold), and never rise again after the first fall. The whole
 *  open-close sequence of a popup must satisfy it; `tolerance` absorbs float noise. */
inline bool isUnimodal(const std::vector<float>& alphas, float tolerance = 1e-4f) {
    bool fell = false;
    for (size_t i = 1; i < alphas.size(); ++i) {
        const float step = alphas[i] - alphas[i - 1];
        if (step < -tolerance)
            fell = true;
        else if (step > tolerance && fell)
            return false;
    }
    return true;
}

/** True when every frame is at most `tolerance` above the one before it. */
inline bool isNonIncreasing(const std::vector<float>& alphas, float tolerance = 1e-4f) {
    for (size_t i = 1; i < alphas.size(); ++i)
        if (alphas[i] > alphas[i - 1] + tolerance)
            return false;
    return true;
}

/** True when no frame is fainter than the one before it by more than `tolerance`. */
inline bool isNonDecreasing(const std::vector<float>& alphas, float tolerance = 1e-4f) {
    for (size_t i = 1; i < alphas.size(); ++i)
        if (alphas[i] < alphas[i - 1] - tolerance)
            return false;
    return true;
}

} // namespace synth::test
