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
    return {"Remove " + lfoName + "?",
            targetName + " is the last thing " + lfoName + " moves, so removing it also deletes " + lfoName +
                " and its settings, unless you keep the module. " + undoShortcutText() + " brings it back."};
}

void confirmRemoveLfo(const RemoveLfoConfirmText& text, std::function<void(RemoveLfoChoice, bool)> done) {
    if (auto& hook = test_hooks::removeLfoConfirmHookForTest()) {
        hook(text, std::move(done));
        return;
    }
    ConfirmDontAskSpec spec{text.title,
                            text.message,
                            "Remove LFO",
                            "removeLfoRememberChoice",
                            "Stop asking and do this every time. You can turn the question back on in "
                            "Preferences, Timeline.",
                            "Keep module",
                            "Remember my choice"};
    showConfirmChoice(spec, [done = std::move(done)](ConfirmChoice choice, bool remember) {
        if (!done)
            return;
        done(choice == ConfirmChoice::confirm     ? RemoveLfoChoice::removeModule
             : choice == ConfirmChoice::alternate ? RemoveLfoChoice::keepModule
                                                  : RemoveLfoChoice::cancel,
             remember);
    });
}

void rememberRemoveLfoChoice(juce::PropertiesFile& settings, RemoveLfoChoice choice) {
    if (choice == RemoveLfoChoice::cancel)
        return;
    settings.setValue(kAskBeforeRemovingLfoKey, "0");
    settings.setValue(kRemoveLastSourceChoiceKey, choice == RemoveLfoChoice::keepModule ? "keep" : "remove");
    settings.saveIfNeeded();
}

RemoveLfoChoice rememberedRemoveLfoChoice(const juce::PropertiesFile& settings) {
    return settings.getValue(kRemoveLastSourceChoiceKey) == "keep" ? RemoveLfoChoice::keepModule
                                                                   : RemoveLfoChoice::removeModule;
}

namespace test_hooks {
std::function<void(const RemoveLfoConfirmText&, std::function<void(RemoveLfoChoice, bool)>)>&
removeLfoConfirmHookForTest() {
    static std::function<void(const RemoveLfoConfirmText&, std::function<void(RemoveLfoChoice, bool)>)> hook;
    return hook;
}
} // namespace test_hooks

} // namespace synth::ui
