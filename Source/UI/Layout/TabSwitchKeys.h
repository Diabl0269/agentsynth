#pragma once

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

namespace synth::ui {

// Keeps the tab-switching key (Cmd+<digit>) working while a text field has keyboard focus. A key press walks
// up from the focused component and each component's key listeners run before its own keyPressed, but a
// juce::TextEditor sits at the bottom of that walk and answers first: on a platform that delivers a text
// character with Ctrl+digit it types the digit of Cmd+1, so the window's own handler could miss the key.
// (On macOS the peer drops the text character while Command is held, so there the key already gets
// through.) This listener is attached to such a field and offers the key to the surface's tab handler
// before the field's own handling; every key the handler declines goes on to the field untouched.
class TabSwitchKeys : public juce::KeyListener {
public:
    // Answers true when it used the key.
    using Handler = std::function<bool(const juce::KeyPress&)>;

    explicit TabSwitchKeys(Handler handler);
    ~TabSwitchKeys() override;

    // Listens on `component`, ahead of its own key handling. Safe to call twice for one component.
    void attachTo(juce::Component& component);
    // Attaches to every juce::TextEditor at or under `root`.
    void attachToTextEditorsIn(juce::Component& root);

    bool keyPressed(const juce::KeyPress& key, juce::Component* originatingComponent) override;

    bool isAttachedForTest(const juce::Component* component) const;

    // The zero-based tab Cmd+<digit> picks (Cmd+1 is 0 ... Cmd+9 is 8), or -1 for any other key.
    // The modifier is exactly the platform's command key: no Shift, Option or Ctrl alongside it.
    static int positionalTabIndex(const juce::KeyPress& key);

private:
    Handler handler_;
    std::vector<juce::Component::SafePointer<juce::Component>> attached_;
};

} // namespace synth::ui
