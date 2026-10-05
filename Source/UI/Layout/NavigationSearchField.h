#pragma once

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

/** A single-line search editor that lets its popup claim Up/Down/Return/Escape (the Mod Matrix picker and the mod
 *  dot's Add source list). Its own caret handling would swallow the arrows, and it reports Return and Escape through
 *  a posted command message, which would make a pick land a message-loop turn late. `onNavigationKey` returns true
 *  when it handled the key. */
class NavigationSearchField : public juce::TextEditor {
public:
    std::function<bool(const juce::KeyPress&)> onNavigationKey;

    bool keyPressed(const juce::KeyPress& key) override {
        if (onNavigationKey && onNavigationKey(key))
            return true;
        return juce::TextEditor::keyPressed(key);
    }
};

} // namespace synth::ui
