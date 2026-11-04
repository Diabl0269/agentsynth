#include "UI/Layout/DialogMenuKeys.h"

namespace synth::ui::dialog_menu_keys {

Actions defaultActions() {
    return {
        [] {
            if (auto* app = juce::JUCEApplicationBase::getInstance())
                app->systemRequestedQuit();
        },
        [] { juce::Process::hide(); },
    };
}

Actions& actions() {
    static Actions current = defaultActions();
    return current;
}

bool handle([[maybe_unused]] const juce::KeyPress& key, [[maybe_unused]] juce::DocumentWindow& window) {
#if JUCE_MAC
    static const juce::KeyPress quitKey('q', juce::ModifierKeys::commandModifier, 0);
    static const juce::KeyPress hideKey('h', juce::ModifierKeys::commandModifier, 0);
    auto& run = actions();
    if (key == quitKey && run.quit) {
        run.quit();
        return true;
    }
    if (key == hideKey && run.hide) {
        run.hide();
        return true;
    }
#endif
    return false;
}

} // namespace synth::ui::dialog_menu_keys
