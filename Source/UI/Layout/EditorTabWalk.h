#pragma once

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>
#include <vector>

class ShortcutManager;

// EditorTabWalk.h: the Tab walk of a clip editor (the drum machine, the piano roll). The app's Tab is the region cycle
// (FocusRegion.h), so an editor whose grid holds focus claims it first: Tab from the grid walks the editor's header
// controls in order and comes back to the grid, and the next Tab from the grid leaves through the region cycle
// (Shift+Tab there walks the header back). Shift+Tab walks the other way. docs/timeline/focus.md has the orders.
// Message thread only.
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
    juce::Component* focused() const;
    void land(juce::Component& target, bool forward);

    juce::Component& home_;
    std::function<std::vector<juce::Component*>()> stops_;
    std::function<const ShortcutManager*()> shortcuts_;
    FocusHooks hooks_;
    std::optional<bool> walkedRound_; // the direction of a full walk that just came back to the grid
};

} // namespace synth::ui
