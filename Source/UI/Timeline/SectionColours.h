#pragma once

#include "Timeline/TimelineDoc/SectionPalette.h"

// The section palette lives in the timeline model (Timeline/TimelineDoc/SectionPalette.h) so the AI's section ops
// and the strip share it; the UI namespace re-exports it.
namespace synth::ui {

using synth::kSectionColours;
using synth::sectionColourFor;

} // namespace synth::ui
