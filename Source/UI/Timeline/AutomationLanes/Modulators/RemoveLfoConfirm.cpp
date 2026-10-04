// Concern: the "Remove LFO?" confirm window and its words.
#include "UI/Timeline/AutomationLanes/Modulators/RemoveLfoConfirm.h"
#include "UI/Chrome/ConfirmDontAskDialog.h"

namespace synth::ui {

namespace {
// The undo key a person presses to bring the LFO back, as their platform writes it.
juce::String undoShortcutText() {
#if JUCE_MAC
    return "Cmd+Z";
#else
    return "Ctrl+Z";
#endif
}
} // namespace

RemoveLfoConfirmText removeLfoConfirmText(const juce::String& lfoName, const juce::String& targetName) {
    return {"Remove " + lfoName + "?", targetName + " is the last thing " + lfoName +
                                           " moves, so removing it also deletes " + lfoName + " and its settings. " +
                                           undoShortcutText() + " brings it back."};
}

void confirmRemoveLfo(const RemoveLfoConfirmText& text, std::function<void(bool, bool)> done) {
    if (auto& hook = test_hooks::removeLfoConfirmHookForTest()) {
        hook(text, std::move(done));
        return;
    }
    showConfirmDontAsk({text.title, text.message, "Remove LFO", "removeLfoDontAskAgain",
                        "Stop asking before removing an LFO's last destination. You can turn it back on in "
                        "Preferences, Timeline."},
                       std::move(done));
}

namespace test_hooks {
std::function<void(const RemoveLfoConfirmText&, std::function<void(bool, bool)>)>& removeLfoConfirmHookForTest() {
    static std::function<void(const RemoveLfoConfirmText&, std::function<void(bool, bool)>)> hook;
    return hook;
}
} // namespace test_hooks

} // namespace synth::ui
