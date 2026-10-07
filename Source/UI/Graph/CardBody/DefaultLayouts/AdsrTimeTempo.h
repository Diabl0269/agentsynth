#pragma once

// The ADSR card's two ways to lay out its stages (docs/layout/module-card-layout.md#default-layouts):
// Shared, each stage once with its time or its note division in the same cell by the card's Sync switch
// (Time/Tempo; the default), or Separate, a Time look and a Tempo look, each its own section with its own
// controls and positions, only the one the Sync switch is on shown, both in the same area of the card. Pure
// conversions on a CardLayout; the on-card editor's "Controls" switch writes them.

#include "Modules/CardLayout.h"
#include <optional>

namespace synth {

enum class AdsrTimeTempo { Shared, Separate };

/** True for the module types whose card has the stages (ADSR, Amp Env, Filter Env). */
bool hasAdsrTimeTempo(const juce::String& moduleType);

/** Shared when a section holds the stage swap pairs, Separate when the "stages-time" or "stages-tempo"
 *  section exists, nullopt for any other layout (the stages were moved out or hidden). */
std::optional<AdsrTimeTempo> adsrTimeTempoOf(const CardLayout& layout);

/** `layout` read in the current Separate form: a layout saved when Separate showed both groups at once (each
 *  dimmed while the other mode was on) gets each group's look condition, loses the dims and gives the Tempo
 *  group the Sustain it lacked. Any other layout, and one already in the current form, comes back unchanged. */
CardLayout withAdsrSeparateLooks(CardLayout layout);

/** `layout` with its stages in `mode`, in place: the group keeps its position in the section list, each item
 *  its label, span and range (a hidden control stays hidden), and loses its `at` position, as the
 *  new groups flow. Separate draws a division as a fader. A Separate layout in the older form is read in the
 *  current one first. Every other section is untouched; a layout in no mode is returned unchanged. */
CardLayout withAdsrTimeTempo(CardLayout layout, AdsrTimeTempo mode);

} // namespace synth
