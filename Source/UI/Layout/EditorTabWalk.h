#pragma once

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>
#include <vector>

class ShortcutManager;

// EditorTabWalk.h: the Tab walk of a clip editor (the drum machine, the piano roll). The app's Tab is the region cycle
// (FocusRegion.h), so an editor whose grid holds focus claims it first: Tab from the grid walks the editor's header
// controls in order and comes back to the grid, and the next Tab from the grid leaves through the region cycle
// (Shift+Tab there walks the header back). Shift+Tab walks the other way. The header is also a bar of controls, so
// Left/Right on a focused stop step to the neighbour as well; a stop that keeps the plain arrows for its own
// value (a slider, a number field, a combo box, a segmented switch) is stepped by Alt+Left/Right instead. Past either
// end the arrows hand focus back to the grid. docs/timeline/focus.md has the orders. Message thread only.
namespace synth::ui {

class EditorTabWalk : public juce::KeyListener {
public:
    /** Listens on `home`, the editor's grid; `home` owns this walk or outlives it. */
    explicit EditorTabWalk(juce::Component& home);
    ~EditorTabWalk() override;

    /** The stops in walk order; a hidden, disabled or empty one is skipped when the walk runs. */
    void setStops(std::function<std::vector<juce::Component*>()> stops) { stops_ = std::move(stops); }
    /** The user's bindings for the region cycle keys; null (or no manager) means Tab and Shift+Tab. */
    void setShortcuts(std::function<const ShortcutManager*()> shortcuts) { shortcuts_ = std::move(shortcuts); }

    bool keyPressed(const juce::KeyPress& key, juce::Component* originatingComponent) override;
    /** Also answers Alt+Left/Right from `stop`, a stop that would otherwise swallow them (a text box moves its caret by
     *  word): the walk listens on it too, and claims nothing else there. */
    void watch(juce::Component& stop);
    /** Whether `stop` was given to watch(): the walk hears its keys before the stop does. */
    bool isWatching(const juce::Component& stop) const;
    /** The next Tab from the grid enters the header again (focus left the grid, or the editor opened a clip). */
    void reset() noexcept { walkedRound_.reset(); }

    /** The stops a Tab can land on now, in order. */
    std::vector<juce::Component*> getStops() const;
    /** The stop that holds keyboard focus, or null when focus is on the grid or elsewhere. */
    juce::Component* focusedStop() const;

    /** A headless test has no native window to hold focus, so it supplies where focus is and records each move. */
    struct FocusHooks {
        std::function<juce::Component*()> focused;
        std::function<void(juce::Component&)> moveFocusTo;
    };
    void setFocusHooksForTest(FocusHooks hooks) { hooks_ = std::move(hooks); }

private:
    std::optional<bool> cycleDirection(const juce::KeyPress& key) const;
    bool stepAlongStops(const juce::KeyPress& key, juce::Component* originatingComponent);
    juce::Component* focused() const;
    void land(juce::Component& target, bool forward);

    juce::Component& home_;
    std::function<std::vector<juce::Component*>()> stops_;
    std::function<const ShortcutManager*()> shortcuts_;
    FocusHooks hooks_;
    std::vector<juce::Component::SafePointer<juce::Component>> watched_;
    std::optional<bool> walkedRound_; // the direction of a full walk that just came back to the grid
};

} // namespace synth::ui
