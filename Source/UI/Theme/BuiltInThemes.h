#pragma once

#include "Theme.h"
#include <vector>

namespace synth::theme {
Theme makeObsidian();               // id "obsidian"
Theme makeNeon();                   // id "neon"
Theme makeWarm();                   // id "warm"
std::vector<Theme> builtInThemes(); // {Obsidian, Neon, Warm} in this order
} // namespace synth::theme
