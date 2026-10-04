#pragma once

// The ADSR card's two ways to lay out its stages (docs/layout/module-card-layout.md#default-layouts):
// Shared, each stage once with its time or its note division in the same cell by the card's Time/Tempo
// switch (the default), or Separate, a "Time" group of faders and a "Tempo" group of divisions, both
// always visible. Pure conversions on a CardLayout; the on-card editor's "Time and tempo" switch writes them.

#include "Modules/CardLayout.h"
#include <optional>

namespace synth {

enum class AdsrTimeTempo { Shared, Separate };

/** True for the module types whose card has the stages (ADSR, Amp Env, Filter Env). */
bool hasAdsrTimeTempo(const juce::String& moduleType);

/** Shared when a section holds the stage swap pairs, Separate when the "stages-time" or "stages-tempo"
 *  section exists, nullopt for any other layout (the stages were moved out or hidden). */
std::optional<AdsrTimeTempo> adsrTimeTempoOf(const CardLayout& layout);

/** `layout` with its stages in `mode`, in place: the group keeps its position in the section list, each item
 *  its label, widget, span and range (a hidden control stays hidden), and loses its `at` position, as the
 *  new groups flow. Every other section is untouched; a layout in no mode is returned unchanged. */
CardLayout withAdsrTimeTempo(CardLayout layout, AdsrTimeTempo mode);

} // namespace synth
