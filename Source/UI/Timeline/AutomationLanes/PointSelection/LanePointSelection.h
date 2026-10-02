#pragma once

#include "Timeline/TimelineDoc/TimelineDoc.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>
#include <set>
#include <vector>

// LanePointSelection -- which points of one automation lane are selected, the keyboard cursor point, and the
// box-select gesture on empty space. Points are keyed by beat (a lane has one point per beat). Runtime view
// state: never saved, never touches the doc.
//
// The owner must outlive it and calls paintMarquee() from its own paint(); it repaints the owner itself.
// Message thread only.
namespace synth::ui {

using LaneBreakpoint = synth::AutomationLane::Breakpoint;

// Where a (beat, value) pair sits in the owner's coordinates.
using LanePointMapper = std::function<juce::Point<float>(double beat, double value)>;

class LanePointSelection {
public:
    explicit LanePointSelection(juce::Component& owner);

    // Fired after any call that changed the set or the cursor (never for a no-op).
    std::function<void()> onChange;

    // ---- The set ----
    void clear();
    bool isEmpty() const noexcept { return beats_.empty(); }
    int size() const noexcept { return (int)beats_.size(); }
    bool contains(double beat) const noexcept { return beats_.count(beat) > 0; }
    bool add(double beat);
    bool remove(double beat);
    // Returns the beat's selected state after the toggle.
    bool toggle(double beat);
    void setSelection(const std::vector<double>& beats);
    // Ascending beat order.
    std::vector<double> getSelected() const { return {beats_.begin(), beats_.end()}; }
    // Drops every selected beat (and the cursor) that is not in `alive`; returns true if anything went.
    bool retainOnly(const std::vector<double>& alive);

    // The selected points of `lane` (beat order) / the box around them in the owner's coordinates (empty when
    // nothing is selected).
    std::vector<LaneBreakpoint> selectedPoints(const std::vector<LaneBreakpoint>& lane) const;
    juce::Rectangle<float> boundingBox(const std::vector<LaneBreakpoint>& lane, const LanePointMapper& map) const;

    // ---- Keyboard cursor: the point arrow navigation stands on ----
    std::optional<double> getCursor() const noexcept { return cursor_; }
    void setCursor(std::optional<double> beat);

    // ---- Box select on empty space ----
    // A plain press is only a pending click until it drags; an additive one (Shift/Cmd/Ctrl) starts the box.
    void pressEmpty(juce::Point<int> pos, bool additive);
    // Follows the pointer; returns true while a box is being drawn.
    bool dragTo(juce::Point<int> pos, const std::vector<LaneBreakpoint>& lane, const LanePointMapper& map);
    // Ends the box, or deselects when the press never moved; returns true when a press was pending or a box was
    // live.
    bool release();
    void cancelGesture();
    bool isBoxActive() const noexcept { return boxActive_; }
    void paintMarquee(juce::Graphics& g) const;

private:
    void notify();
    void beginBox(juce::Point<int> anchor, bool additive);

    juce::Component& owner_;
    std::set<double> beats_;
    std::optional<double> cursor_;
    bool pendingEmptyClick_ = false;
    bool boxActive_ = false;
    juce::Point<int> anchor_;
    juce::Rectangle<int> box_;
    std::set<double> boxBase_; // what an additive box keeps
};

// ---- Moving a selection as one rigid block ----

// `delta` shortened so the earliest of `points` stays at or after beat 0 (the block stops as a unit).
double clampBeatDelta(const std::vector<LaneBreakpoint>& points, double delta);
// `points` shifted by the same beat and value deltas, each value clamped to [minValue, maxValue]; order kept.
std::vector<LaneBreakpoint> movePoints(const std::vector<LaneBreakpoint>& points, double beatDelta, double valueDelta,
                                       double minValue, double maxValue);
// `lane` as the doc would hold it after removing `removeBeats` and inserting `add` (an insert at an existing beat
// replaces it); beat order.
std::vector<LaneBreakpoint> replacePoints(std::vector<LaneBreakpoint> lane, const std::vector<double>& removeBeats,
                                          const std::vector<LaneBreakpoint>& add);
// The beats of the points of `lane` whose centre lies inside `box`.
std::vector<double> beatsInBox(const std::vector<LaneBreakpoint>& lane, const LanePointMapper& map,
                               juce::Rectangle<float> box);

} // namespace synth::ui
