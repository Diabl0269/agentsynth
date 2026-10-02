// Concern: the "Remove LFO?" confirm window and its words.
#include "UI/Timeline/AutomationLanes/Modulators/RemoveLfoConfirm.h"

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
    auto* window = new juce::AlertWindow(text.title, text.message, juce::MessageBoxIconType::WarningIcon);
    auto* dontAsk = new juce::ToggleButton("Don't ask again");
    dontAsk->setComponentID("removeLfoDontAskAgain");
    dontAsk->setTitle("Don't ask again");
    dontAsk->setTooltip("Stop asking before removing an LFO's last destination. You can turn it back on in "
                        "Preferences, Timeline.");
    dontAsk->setSize(220, 24);
    window->addCustomComponent(dontAsk);
    window->addButton("Remove LFO", 1, juce::KeyPress(juce::KeyPress::returnKey));
    window->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    window->enterModalState(
        true, juce::ModalCallbackFunction::create([window, dontAsk, done = std::move(done)](int result) {
            const bool tick = dontAsk->getToggleState(); // read before the window (and the box) goes
            std::unique_ptr<juce::AlertWindow> owned(window);
            if (done)
                done(result == 1, tick);
        }),
        false);
}

namespace test_hooks {
std::function<void(const RemoveLfoConfirmText&, std::function<void(bool, bool)>)>& removeLfoConfirmHookForTest() {
    static std::function<void(const RemoveLfoConfirmText&, std::function<void(bool, bool)>)> hook;
    return hook;
}
} // namespace test_hooks

} // namespace synth::ui
