#pragma once

// The Drum Kit pads' Shown switch: the undoable edits the card and the drum machine share, and the question asked
// before hiding a drum that has hits in a clip. docs/modules/drum-kit.md#hiding-a-pad.

#include "AppUndoManager.h"
#include "Modules/DrumKit/DrumKitModule.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include <functional>
#include <juce_audio_processors/juce_audio_processors.h>

namespace synth::ui {

/** The user-settings key behind "ask before hiding a drum that has hits". DEFAULT TRUE (ask); the question's own box
 *  writes it. Read at use time. */
inline constexpr const char* kAskBeforeHidingDrumKey = "drumKitAskBeforeHidingPad";

/** Sets pad `index`'s Shown switch as one undo step (none when `undo` or `graph` is null, or nothing changes).
 *  False when the switch already read so. */
bool setPadShownUndoably(AppUndoManager* undo, juce::AudioProcessorGraph* graph, DrumKitModule& kit, int index,
                         bool shown);
/** Switches every pad on as one undo step; how many changed. */
int showAllPadsUndoably(AppUndoManager* undo, juce::AudioProcessorGraph* graph, DrumKitModule& kit);

/** The question for hiding `padName`, which has `hits` notes in the clips that play the kit. */
struct HideDrumConfirmText {
    juce::String title;
    juce::String message;
};
HideDrumConfirmText hideDrumConfirmText(const juce::String& padName, int hits);

/** Shows the question (a hook answers it in tests). `done(confirmed, dontAskAgain)` runs once after it closes. */
void confirmHideDrum(const HideDrumConfirmText& text, std::function<void(bool confirmed, bool dontAskAgain)> done);

/** The notes on `note` across the clips of every track `playsKit` says plays the kit. */
int drumHitsOnNote(const TimelineDoc& doc, int note, const std::function<bool(TrackId)>& playsKit);

/** Hiding a drum never mutes it, but a row with hits going away is worth one question: runs `proceed` at once when
 *  `hits` is 0 or the person ticked "Don't ask again", else after they confirm (never once `anchor` is gone). */
void askBeforeHidingDrum(juce::Component& anchor, juce::ApplicationProperties& props, const juce::String& padName,
                         int hits, std::function<void()> proceed);

namespace test_hooks {
/** When set, confirmHideDrum calls it instead of opening a window. */
std::function<void(const HideDrumConfirmText&, std::function<void(bool, bool)>)>& hideDrumConfirmHookForTest();
} // namespace test_hooks

} // namespace synth::ui
