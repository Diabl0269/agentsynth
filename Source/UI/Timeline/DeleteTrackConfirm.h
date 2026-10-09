#pragma once

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

/** The user-settings key behind "ask before deleting a track with the keyboard". DEFAULT TRUE (ask); the dialog's
 *  own "Don't ask again" box and Preferences > Timeline both write it. Read at use time. */
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

namespace test_hooks {
/** When set, confirmDeleteTrack calls it instead of opening a window. */
std::function<void(const DeleteTrackConfirmText&, std::function<void(bool, bool)>)>& deleteTrackConfirmHookForTest();
} // namespace test_hooks

} // namespace synth::ui
