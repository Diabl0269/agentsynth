#pragma once

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

/** The user-settings key behind "ask before deleting a track or a track's macro". DEFAULT TRUE (ask); the dialogs'
 *  own boxes and Preferences > Timeline both write it. Read at use time. */
inline constexpr const char* kAskBeforeDeletingTrackKey = "timelineAskBeforeDeletingTrack";

/** The question's words for the track called `trackName`. */
struct DeleteTrackConfirmText {
    juce::String title;
    juce::String message;
};
DeleteTrackConfirmText deleteTrackConfirmText(const juce::String& trackName);
/** The one question for deleting `count` selected tracks at once ("Delete 3 tracks?"). */
DeleteTrackConfirmText deleteTrackConfirmText(int count);

/** Shows the dialog (a hook answers it in tests). `done(confirmed, dontAskAgain)` runs once after it closes. */
void confirmDeleteTrack(const DeleteTrackConfirmText& text,
                        std::function<void(bool confirmed, bool dontAskAgain)> done);

/** The user-settings key holding the answer "Remember my choice" stored for a track's macro ("keep" or "remove"),
 *  read only while the question is off. No stored answer reads as "remove", which is what a macro delete did before
 *  the question offered Keep track. Preferences > Timeline turning the question back on forgets it. */
inline constexpr const char* kDeleteMacroChoiceKey = "timelineDeleteMacroChoice";

/** What the person chose when the macro they are deleting is played by a track. */
enum class DeleteMacroChoice { cancel, keepTrack, removeTrack };

/** The question for deleting a macro that `trackNames` play: Remove track also deletes the track(s), Keep track
 *  deletes the macro and its modules only. */
DeleteTrackConfirmText deleteMacroConfirmText(const juce::StringArray& trackNames);

/** Shows the three-way dialog (a hook answers it in tests). `done(choice, remember)` runs once after it closes; the
 *  caller applies the delete and, when `remember` and the choice is not a cancel, stores it
 * (rememberDeleteMacroChoice). Reuses the shared showConfirmChoice window, as the LFO last-destination question does.
 */
void confirmDeleteMacro(const DeleteTrackConfirmText& text,
                        std::function<void(DeleteMacroChoice choice, bool remember)> done);

/** Stores `choice` as the standing answer: the question is no longer asked and `choice` is applied. */
void rememberDeleteMacroChoice(juce::PropertiesFile& settings, DeleteMacroChoice choice);

/** The standing answer while the question is off. */
DeleteMacroChoice rememberedDeleteMacroChoice(const juce::PropertiesFile& settings);

namespace test_hooks {
/** When set, confirmDeleteMacro calls it instead of opening a window. */
std::function<void(const DeleteTrackConfirmText&, std::function<void(DeleteMacroChoice, bool)>)>&
deleteMacroConfirmHookForTest();
/** When set, confirmDeleteTrack calls it instead of opening a window. */
std::function<void(const DeleteTrackConfirmText&, std::function<void(bool, bool)>)>& deleteTrackConfirmHookForTest();
} // namespace test_hooks

} // namespace synth::ui
