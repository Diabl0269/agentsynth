// SweepPlatform.h -- which platform a known gap of a sweep (layout gaps, key standards) belongs to. Fonts, audio
// devices and menus differ per platform, so each platform lists the gaps it really has; a gap is never judged on
// another one.
#pragma once

#include <juce_core/juce_core.h>

namespace synth::test {

enum class GapPlatform { All, Mac, Windows };

inline bool gapAppliesHere(GapPlatform platform) {
#if JUCE_MAC
    constexpr bool onMac = true, onWindows = false;
#elif JUCE_WINDOWS
    constexpr bool onMac = false, onWindows = true;
#else
    constexpr bool onMac = false, onWindows = false;
#endif
    switch (platform) {
    case GapPlatform::Mac:
        return onMac;
    case GapPlatform::Windows:
        return onWindows;
    case GapPlatform::All:
        break;
    }
    return true;
}

} // namespace synth::test
