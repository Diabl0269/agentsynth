#pragma once

// CutoffNoteSnap.h -- the Filter cutoff knob's Snap to notes: Free, Notes (every semitone on A 440) or By key (the
// song key's notes). Only while the Filter's type is Custom, where the cutoff is the pitch the shape is tuned around;
// the knob then drags and steps by note and reads "A3 220 Hz". The choice is the card's view state, saved with the
// project.
#include "Modules/CardViewState.h"
#include "Music/MusicalScale.h"
#include <functional>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>

class FilterModule;

namespace synth::ui {

class CardKnobSlider;

namespace cutoffsnap {

enum class Mode { free, notes, key };

Mode modeOf(const CardViewState& state);
void setMode(CardViewState& state, Mode mode);

/** The value `hz` lands on under `mode` (`key` for By key; nullopt or chromatic: every note). */
double snapHz(double hz, Mode mode, const std::optional<MusicalScale>& key);
/** One note up (+1) or down (-1) from `hz` under `mode`; `hz` itself when there is none in range. */
double stepHz(double hz, int direction, Mode mode, const std::optional<MusicalScale>& key);
/** "A3 220 Hz": the note `hz` is nearest and the hertz. */
juce::String readout(double hz);

/** Gives the Filter card's cutoff knob its snap, steps and readout; `songKey` is read whenever the knob needs it. */
void wire(CardKnobSlider& knob, FilterModule& filter, std::function<std::optional<MusicalScale>()> songKey);
/** Adds Snap to notes (Free, Notes, By key) to a knob menu when `param` is a Filter's cutoff and its type is Custom;
 *  else does nothing. `knob` is the cutoff knob (null adds nothing); a pick re-snaps it. */
void appendMenuItems(juce::PopupMenu& menu, juce::AudioProcessor* module, const juce::RangedAudioParameter* param,
                     juce::Slider* knob);

} // namespace cutoffsnap
} // namespace synth::ui
