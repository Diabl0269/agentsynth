#pragma once

// Private to the MainComponent.cpp / MainComponent*.cpp translation units (FRO63 split of the
// former single MainComponent.cpp). Holds the handful of file-local anonymous-namespace helpers
// that are used by more than one of those units — everything used by only one unit stays in that
// unit's own anonymous namespace instead. Not part of the public API: nothing outside the
// MainComponent units should include this header. Assumes MainComponent.h is included first (for
// the JUCE module headers these declarations depend on).

#include "Modules/ModuleBase.h"
#include "ProjectBundle.h"

namespace detail {

// A clip's assetRef always names the .wav asset — chooseTakeFiles() is what establishes the
// pairing with its .agpk peaks sidecar: same stem, and either a sibling "Peaks/" directory (a saved
// bundle: "Audio/take-n.wav" <-> "Peaks/take-n.agpk") or the SAME "Recordings/" directory (an
// unsaved project, where chooseTakeFiles points audioDir and peaksDir at the same root). Returns
// the peaks SIDECAR'S ref, in the same bundle/root-relative form the streamer's own
// resolveAssetRef() understands — so that one function stays the single place a ref becomes a
// juce::File, for both the audio and the peaks half. Empty in, empty out. Used by
// MainComponentSetupTimeline.cpp (wireTimelineClipLaneCallbacks).
inline juce::String peaksRefForAssetRef(const juce::String& assetRef) {
    if (assetRef.isEmpty())
        return {};

    juce::String ref = assetRef;
    const juce::String audioPrefix = juce::String(synth::ProjectBundle::kAudioSubdirName) + "/";
    if (ref.startsWith(audioPrefix))
        ref = juce::String(synth::ProjectBundle::kPeaksSubdirName) + "/" + ref.substring(audioPrefix.length());

    return ref.upToLastOccurrenceOf(".", false, false) + ".agpk";
}

// Where an UNSAVED project's takes go, under <app data>/<settings folder>. Also the reserved
// prefix such a take's clip assetRef carries — see chooseTakeFiles and ProjectBundle's asset
// policy. Used by both MainComponentFileIO.cpp (saveToFile) and MainComponentTimeline.cpp
// (chooseTakeFiles, promptRelinkClipAsset, relinkClipAsset, importAudioFileToClip).
inline constexpr const char* kRecordingsFolderName = "Recordings";

// The add-track flow's auto-wire target set: MIDI-DRIVEN INSTRUMENTS only.
//
// juce::AudioProcessor::acceptsMidi() cannot be the rule — ModuleBase overrides it to `true` for
// EVERY module in this app, so it would match a Reverb. The module type is the rule instead, and
// the set mirrors AIStateMapper's own midiAcceptingTypes (Oscillator, Sampler, Sequencer, Poly
// Sequencer, Poly MIDI) plus Wavetable, which consumes note-ons exactly the way Oscillator does.
//
// MIDI *sources* are deliberately excluded: Track In itself, External MIDI and MIDI Keyboard
// generate notes, so wiring a new Track In into one of them is never what the user meant. Used by
// both MainComponentTimeline.cpp (createTrackInNode) and MainComponentTrackHeaderHost.cpp
// (getMidiDestinationOptions).
inline bool isMidiInstrumentNode(juce::AudioProcessor* processor) {
    auto* module = dynamic_cast<ModuleBase*>(processor);
    if (module == nullptr)
        return false;

    return isMidiInstrumentType(module->getModuleType());
}

} // namespace detail
