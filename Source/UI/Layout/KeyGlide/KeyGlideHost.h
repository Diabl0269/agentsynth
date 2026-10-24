// KeyGlideHost.h -- what a key hold reads from the outside world: the clock and the keys. A test replaces it to
// supply both.
#pragma once

#include <functional>

namespace synth::ui {

struct KeyGlideHost {
    std::function<double()> nowMs;
    /** Whether the arrow with this juce::KeyPress code is physically down right now. */
    std::function<bool(int)> isArrowDown;
    std::function<bool()> isShiftDown;
    std::function<bool()> isAltDown;
};

/** The hi-res clock, the OS's own view of the arrows where there is one (JUCE's otherwise) and the live modifiers. */
KeyGlideHost makeDefaultKeyGlideHost();

} // namespace synth::ui
