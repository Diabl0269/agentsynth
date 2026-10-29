#pragma once

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

// TimelineTopBarKeys.h: keyboard reach for the Timeline's top bar (the tools, snap and follow controls and the
// transport row). The panel root keeps real focus; Up on it puts a ring on the first control and Left/Right/Home/End
// move the ring across the visible, enabled controls in on-screen order, the way the main toolbar's ring does.
// Message thread only.
namespace synth::ui {

class TimelineTopBarKeys {
public:
    using StopsFn = std::function<std::vector<juce::Component*>()>;

    TimelineTopBarKeys(juce::Component& owner, StopsFn candidates);

    /** The ring is on a control and the keys below are the bar's. */
    bool isActive() const noexcept { return active_; }
    /** Puts the ring on the control it last sat on (the first one the first time). False when the bar shows none. */
    bool enter();
    void leave();
    /** Left/Right/Home/End move the ring, Return/Space press the ringed control, Down/Escape return to the panel;
     *  anything else (any chord too) is not ours. Only while active. */
    bool handleKey(const juce::KeyPress& key);
    void paintRing(juce::Graphics& g) const;

    /** The visible, enabled candidates, left to right. */
    std::vector<juce::Component*> getStops() const;
    juce::Component* getRingedStop() const;

private:
    void moveRingTo(juce::Component* stop);
    void activate(juce::Component& stop);

    juce::Component& owner_;
    StopsFn candidates_;
    bool active_ = false;
    juce::Component::SafePointer<juce::Component> ringed_;
};

} // namespace synth::ui
