#pragma once

// Gives the synth stand-ins a song build left for sound pack instruments their real sound once the pack is installed
// (see docs/modules/sound-pack-downloads.md#song-builds-and-the-core-instruments).

#include "SoundPacks/SoundPackInstaller.h"
#include <functional>
#include <juce_audio_processors/juce_audio_processors.h>
#include <set>

class AudioEngine;
class AppUndoManager;
class GraphEditor;

namespace synth {

class CoreSoundSwapper : private synth::soundpacks::SoundPackInstaller::Listener {
public:
    CoreSoundSwapper(AudioEngine& engine, GraphEditor& editor, AppUndoManager& undo);
    ~CoreSoundSwapper() override;

    /** Runs after a swap changed the graph, so the timeline can re-read what plays each track. */
    std::function<void()> onGraphChanged;
    /** Runs after a swap with how many tracks now play a real instrument and whether one undo step reverts it. */
    std::function<void(int tracks, bool undoable)> onSwapped;

    /** Replaces every stand-in whose pack instrument is installed (one undo step) and loads the kit of every Drum Kit
     *  that waits for one. Returns the number of tracks changed; 0 changes nothing. */
    int swapNow();

private:
    void soundPacksChanged() override;
    std::set<juce::String> installedPacks() const;

    AudioEngine& engine_;
    GraphEditor& editor_;
    AppUndoManager& undo_;
    juce::SharedResourcePointer<synth::soundpacks::SoundPackInstaller> installer_;
    std::set<juce::String> installedPacks_; ///< The packs installed at the last change, to spot a new one.

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CoreSoundSwapper)
};

} // namespace synth
