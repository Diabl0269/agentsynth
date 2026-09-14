#pragma once

// Shared between the split AppLookAndFeel*.cpp units: loadEmbeddedTypeface is called both by the
// constructor (AppLookAndFeelLifecycle.cpp, which pre-loads every built-in family at startup) and
// by the font-resolution unit (AppLookAndFeelFonts.cpp, where it is defined).

#include "AppLookAndFeel.h"

namespace synth::theme {

extern juce::Typeface::Ptr loadEmbeddedTypeface(const juce::String& family, bool bold, bool medium);

} // namespace synth::theme
