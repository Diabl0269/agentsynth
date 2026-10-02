#pragma once

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

// The question asked before a modulator's removal would also delete its LFO: the removed routing is the last
// thing that LFO moves, and an LFO with nothing to move is deleted with its settings. Switched off for good
// by the dialog's own "Don't ask again" box, or in Preferences > Timeline.

/** The user-settings key behind that preference. DEFAULT TRUE (ask). Read at use time, so the Preferences toggle
 *  and the dialog's box need no live push. */
inline constexpr const char* kAskBeforeRemovingLfoKey = "timelineAskBeforeRemovingLfo";

struct RemoveLfoConfirmText {
    juce::String title;
    juce::String message;
};

/** The dialog's words for `lfoName` losing its last destination `targetName`. */
RemoveLfoConfirmText removeLfoConfirmText(const juce::String& lfoName, const juce::String& targetName);

/** Shows the confirm dialog (a hook answers it in tests). `done(confirmed, dontAskAgain)` runs once, after the
 *  dialog closes; the caller applies the removal and writes the preference. */
void confirmRemoveLfo(const RemoveLfoConfirmText& text, std::function<void(bool confirmed, bool dontAskAgain)> done);

namespace test_hooks {
/** When set, confirmRemoveLfo calls it with the text and the `done` callback instead of opening a window. */
std::function<void(const RemoveLfoConfirmText&, std::function<void(bool, bool)>)>& removeLfoConfirmHookForTest();
} // namespace test_hooks

} // namespace synth::ui
