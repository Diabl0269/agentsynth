#pragma once

#include <juce_core/juce_core.h>

// MixerPanLaw.h -- FRO325 (docs/mixer/mixer.md#pan-law): the mixer's pan law, chosen PER PROJECT,
// for a MONO-shaped ChannelStripModule's own "pan" parameter and its sends' "sendNPan" parameters
// only -- never a Stereo-shaped strip's balance control, and never any other
// ModuleBase::panGains caller (a module card's own pan knob). Balance is ModuleBase::panGains
// (unchanged); Compensated is ModuleBase::panGainsCompensated (new). Persisted in the project file
// as a top-level "mixerPanLaw" string ("balance"/"compensated"); absent means Balance (an existing
// project's mix is never changed underfoot).

namespace synth {

enum class MixerPanLaw { Balance, Compensated };

/** The exact strings ProjectBundle's "mixerPanLaw" field and the mixer's own pan-law control use. */
inline juce::String mixerPanLawToString(MixerPanLaw law) {
    return law == MixerPanLaw::Compensated ? "compensated" : "balance";
}

/** Anything other than the exact "compensated" string (including absent/malformed) reads as
 *  Balance -- callers that must distinguish "absent" from "present but garbled" check
 *  hasProperty() themselves before calling this, the same split "shape"/"sends" already use. */
inline MixerPanLaw mixerPanLawFromString(const juce::String& text) {
    return text == "compensated" ? MixerPanLaw::Compensated : MixerPanLaw::Balance;
}

} // namespace synth
