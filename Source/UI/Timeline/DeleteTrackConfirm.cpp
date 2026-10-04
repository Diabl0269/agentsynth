// Concern: the "Delete track?" confirm window and its words.
#include "UI/Timeline/DeleteTrackConfirm.h"
#include "UI/Chrome/ConfirmDontAskDialog.h"

namespace synth::ui {

namespace {
// The undo key a person presses to bring the track back, as their platform writes it.
juce::String undoShortcutText() {
#if JUCE_MAC
    return "Cmd+Z";
#else
    return "Ctrl+Z";
#endif
}
} // namespace

DeleteTrackConfirmText deleteTrackConfirmText(const juce::String& trackName) {
    return {"Delete " + trackName + "?",
            "The track, its clips and its instrument node go. " + undoShortcutText() + " brings it back."};
}

void confirmDeleteTrack(const DeleteTrackConfirmText& text, std::function<void(bool, bool)> done) {
    if (auto& hook = test_hooks::deleteTrackConfirmHookForTest()) {
        hook(text, std::move(done));
        return;
    }
    showConfirmDontAsk({text.title, text.message, "Delete", "deleteTrackDontAskAgain",
                        "Stop asking before deleting a track with the keyboard. You can turn it back on in "
                        "Preferences, Timeline."},
                       std::move(done));
}

namespace test_hooks {
std::function<void(const DeleteTrackConfirmText&, std::function<void(bool, bool)>)>& deleteTrackConfirmHookForTest() {
    static std::function<void(const DeleteTrackConfirmText&, std::function<void(bool, bool)>)> hook;
    return hook;
}
} // namespace test_hooks

} // namespace synth::ui
