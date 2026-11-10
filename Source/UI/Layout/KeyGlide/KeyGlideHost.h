// KeyGlideHost.h -- what a key hold reads from the outside world: the clock and the keys. A test replaces it to
// supply both.
#pragma once

#include <functional>
#include <optional>
#include <set>

namespace synth::ui {

struct KeyGlideHost {
    std::function<double()> nowMs;
    /** Whether the arrow with this juce::KeyPress code is physically down right now. */
    std::function<bool(int)> isArrowDown;
    std::function<bool()> isShiftDown;
    std::function<bool()> isAltDown;
};

/** The hi-res clock, the OS's own view of the arrows where there is one (JUCE's otherwise) and the live modifiers;
 *  while held keys are set (setHeldKeys) it reads those instead. */
KeyGlideHost makeDefaultKeyGlideHost();

/** Keys a script holds down instead of a hand: the arrows (juce::KeyPress codes) and the modifiers. */
struct HeldKeys {
    std::set<int> arrows;
    bool shift = false;
    bool alt = false;
};

/** Test mode's keyDown/keyUp: every default host reads `held` instead of the keyboard until it is cleared. Message
 *  thread only. */
void setHeldKeys(std::optional<HeldKeys> held);
const std::optional<HeldKeys>& heldKeys();

} // namespace synth::ui
