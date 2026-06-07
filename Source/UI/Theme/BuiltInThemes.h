#pragma once

#include "Theme.h"
#include <vector>

namespace gsynth::theme {
Theme makeObsidian();               // id "obsidian"
Theme makeNeon();                   // id "neon"
Theme makeWarm();                   // id "warm"
std::vector<Theme> builtInThemes(); // {Obsidian, Neon, Warm} in this order
} // namespace gsynth::theme
