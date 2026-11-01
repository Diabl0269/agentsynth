#pragma once

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <vector>

// KeyRing.h: keyboard reach for a panel's top bar (docs/development/accessibility.md#key-rings). The panel root keeps
// real focus; a key on it puts a painted ring on the first control and Left/Right/Home/End move the ring across the
// visible, enabled controls, the way the main toolbar's ring does. A screen reader hears the ringed control's name as
// the panel's value. Message thread only.
namespace synth::ui {

class KeyRing {
public:
    using StopsFn = std::function<std::vector<juce::Component*>()>;
    /** How the visible stops are ordered: left to right by on-screen position, or as `candidates` lists them. */
    enum class Order { byX, given };

    KeyRing(juce::Component& owner, StopsFn candidates, Order order = Order::byX);

    /** The ring is on a control and the keys below are the ring's. */
    bool isActive() const noexcept { return active_; }
    /** Puts the ring on the control it last sat on (the first one the first time). False when no stop shows. */
    bool enter();
    void leave();
    /** Left/Right/Home/End move the ring, Return/Space press the ringed control, Down/Escape return to the panel;
     *  anything else (any chord too) is not ours. Only while active. */
    bool handleKey(const juce::KeyPress& key);
    void paintRing(juce::Graphics& g) const;

    /** The visible, enabled candidates in ring order. */
    std::vector<juce::Component*> getStops() const;
    juce::Component* getRingedStop() const;

    /** The ringed control's name (its title, else its text) while active; empty otherwise. */
    juce::String getValueText() const;
    /** The owner's handler: role group, the value is getValueText(). The owner returns it from its own
     *  createAccessibilityHandler(); the ring announces every change of the value. */
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler();

private:
    void moveRingTo(juce::Component* stop);
    void activate(juce::Component& stop);
    void announce() const;

    juce::Component& owner_;
    StopsFn candidates_;
    Order order_;
    bool active_ = false;
    juce::Component::SafePointer<juce::Component> ringed_;
};

} // namespace synth::ui
