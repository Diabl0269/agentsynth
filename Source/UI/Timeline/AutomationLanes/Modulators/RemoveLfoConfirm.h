#pragma once

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

// The question asked before a modulator's removal would also delete its LFO: the removed routing is the last
// thing that LFO moves, and an LFO with nothing to move is deleted with its settings. The person can remove the
// LFO too or keep it; the dialog's "Remember my choice" box makes that answer stand without asking again, and
// Preferences > Timeline brings the question back.

/** The user-settings key behind that preference. DEFAULT TRUE (ask). Read at use time, so the Preferences toggle
 *  and the dialog's box need no live push. */
inline constexpr const char* kAskBeforeRemovingLfoKey = "timelineAskBeforeRemovingLfo";

/** The user-settings key holding a remembered answer ("keep" or "remove"), read only while the question is off.
 *  No stored answer reads as "remove". */
inline constexpr const char* kRemoveLastSourceChoiceKey = "timelineRemoveLastSourceChoice";

/** What the person chose when a removal would also delete the LFO. */
enum class RemoveLfoChoice { cancel, keepModule, removeModule };

struct RemoveLfoConfirmText {
    juce::String title;
    juce::String message;
};

/** The dialog's words for `lfoName` losing its last destination `targetName`. */
RemoveLfoConfirmText removeLfoConfirmText(const juce::String& lfoName, const juce::String& targetName);

/** Shows the confirm dialog (a hook answers it in tests). `done(choice, remember)` runs once, after the dialog
 *  closes; the caller applies the removal and, when `remember` and the choice is not a cancel, stores the answer
 *  (rememberRemoveLfoChoice). */
void confirmRemoveLfo(const RemoveLfoConfirmText& text,
                      std::function<void(RemoveLfoChoice choice, bool remember)> done);

/** Stores `choice` as the standing answer: the question is no longer asked and `choice` is applied. */
void rememberRemoveLfoChoice(juce::PropertiesFile& settings, RemoveLfoChoice choice);

/** The standing answer while the question is off; the dialog's answer is never asked for here. */
RemoveLfoChoice rememberedRemoveLfoChoice(const juce::PropertiesFile& settings);

namespace test_hooks {
/** When set, confirmRemoveLfo calls it with the text and the `done` callback instead of opening a window. */
std::function<void(const RemoveLfoConfirmText&, std::function<void(RemoveLfoChoice, bool)>)>&
removeLfoConfirmHookForTest();
} // namespace test_hooks

} // namespace synth::ui
