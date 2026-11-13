#pragma once

#include "Timeline/TimelineDoc/TimelineDoc.h"
#include <algorithm>
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

// The Range tool's selection on ONE automation lane: a beat span on that lane only. At most one exists
// across all lanes (TimelineAutomationLanes owns it and hands every lane editor a pointer). Independent
// of the clip lanes' RangeSelectionModel, which spans track rows. It also records the editor that began it:
// a modulator band's editor works on a private doc whose lane ids repeat the real ones', so a lane id alone
// does not say which surface the span belongs to. Message thread only.
class LaneRangeSelection {
public:
    /** Starts a range on `lane` with both edges at `beat`, owned by the editor `owner` that began it. */
    void begin(synth::LaneId lane, double beat, juce::Component* owner) {
        lane_ = lane;
        owner_ = owner;
        anchor_ = extent_ = beat;
        changed();
    }
    /** Moves the far edge; a no-op while inactive or when the edge does not move. */
    void extendTo(double beat) {
        if (!isActive() || extent_ == beat)
            return;
        extent_ = beat;
        changed();
    }
    void clear() {
        if (!isActive())
            return;
        lane_ = {};
        owner_ = nullptr;
        anchor_ = extent_ = 0.0;
        changed();
    }

    bool isActive() const noexcept { return lane_.isValid(); }
    bool hasWidth() const noexcept { return isActive() && getEndBeat() > getStartBeat(); }
    synth::LaneId getLane() const noexcept { return lane_; }
    /** The editor the range sits on; null once that editor is gone. */
    juce::Component* getOwner() const noexcept { return owner_.getComponent(); }
    double getStartBeat() const noexcept { return std::min(anchor_, extent_); }
    double getEndBeat() const noexcept { return std::max(anchor_, extent_); }

    /** Fired after every change (begin, a moved edge, clear); may be empty. */
    std::function<void()> onChanged;

private:
    void changed() {
        if (onChanged)
            onChanged();
    }

    synth::LaneId lane_;
    juce::Component::SafePointer<juce::Component> owner_;
    double anchor_ = 0.0;
    double extent_ = 0.0;
};

} // namespace synth::ui
