#pragma once

// Headless "auto-profile" binder (docs/control/midi-remote.md#learn-what-does-the-first-message-mean,
// "Auto-profile"): turns a settled synth::midi::LearnResult into the ControllerProfile/Control/
// Assignment values the app layer needs to persist. Lives in Core beside RemoteModel.h -- pure
// data transformation over LearnResult/ControllerProfile, no ControllerProfileStore, no
// RemoteEngine, no juce::ApplicationProperties (Source/CLAUDE.md's Core-layering rule).

#include "MidiRemote/RemoteEngine/RemoteEngine.h"
#include "MidiRemote/RemoteModel.h"

#include <vector>

namespace synth::midi {

/** What bindLearnResult() produces. `profile` is the FULL profile the touched control now lives
 *  on (new, or the caller's existing one with the control added/refreshed) -- the caller saves it
 *  via ControllerProfileStore and republishes RemoteEngine::setProfiles() only when
 *  `profileIsNew || controlIsNew` (an existing control's spec never changes on a plain re-learn of
 *  the same message key, so nothing to persist). `assignment` is always new (a fresh id) and is
 *  the caller's to upsert into MidiRemoteProjectDoc::assignments -- upserting BY TARGET (replacing
 *  any assignment already pointing at the same Target, "Learn again") is the caller's job, not
 *  this function's: a pure binder over one LearnResult has no view of the rest of the project
 *  doc. */
struct LearnBindOutcome {
    ControllerProfile profile;
    bool profileIsNew = false;
    bool controlIsNew = false;
    Assignment assignment;
};

/** `deviceName` is the caller's resolved name for `result.sourceKey` (identifier -> name is an
 *  app-layer juce::MidiInput::getAvailableDevices() lookup / "Host MIDI" in Hosted mode -- done
 *  BEFORE calling this, since Core must not depend on juce_audio_devices). `profiles` is searched
 *  by `input.identifier == result.sourceKey` but never mutated -- this function is a pure query. */
LearnBindOutcome bindLearnResult(const LearnResult& result, const juce::String& deviceName,
                                 const std::vector<ControllerProfile>& profiles);

} // namespace synth::midi
