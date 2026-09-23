#pragma once

#include "UI/Graph/PickTargetOverlay/PickCandidate.h"
#include <functional>
#include <vector>

// PickTargetOverlay.h -- FRO135 (docs/control/midi-remote-ui.md#assign-from-the-panel-control-first-learn):
// the "Pick a module control" mode. A transparent layer over its parent (MainComponent, so it
// covers the canvas, the bottom dock and the transport bar alike) that outlines every candidate a
// surface reported, swallows the next click, and resolves it against the candidates itself -- no
// card or mixer column knows the mode exists. Entered only from the MIDI Remote panel, never a
// global key. The outlines are static (painted once into a cached image, redone only when the
// parent resizes or a click re-measures), so it adds no timer and no animation.
namespace synth::ui {

class PickTargetOverlay
    : public juce::Component
    , private juce::ComponentListener {
public:
    PickTargetOverlay();
    ~PickTargetOverlay() override;

    /** Shows the overlay over its parent with `candidates` outlined. Candidates that are not visible,
     *  not inside the parent, or fully clipped by an ancestor are skipped. */
    void begin(std::vector<PickCandidate> candidates);
    /** Hides the overlay and drops the candidates. Safe to call from the callbacks below. */
    void end();
    bool isActive() const noexcept { return active_; }

    /** A left click landed on a candidate. The overlay has already ended. */
    std::function<void(const synth::midi::PickTarget&)> onPicked;
    /** Esc, or a click on anything that is not a candidate. The overlay has already ended. */
    std::function<void()> onCancelled;

    /** The candidate under `point` (this overlay's coordinates), re-measured from the live
     *  components; the topmost one when several overlap. Null if none. */
    const PickCandidate* findCandidateAt(juce::Point<int> point);
    int getOutlineCountForTest() const noexcept { return static_cast<int>(outlines_.size()); }
    juce::Rectangle<int> getOutlineBoundsForTest(int index) const {
        return outlines_[static_cast<size_t>(index)].bounds;
    }

    void paint(juce::Graphics& g) override;
    bool hitTest(int x, int y) override;
    void mouseDown(const juce::MouseEvent& e) override;
    bool keyPressed(const juce::KeyPress& key) override;

private:
    struct Outline {
        size_t candidate = 0;
        juce::Rectangle<int> bounds;
    };

    void refreshOutlines();
    void componentMovedOrResized(juce::Component& component, bool wasMoved, bool wasResized) override;
    void finish(bool picked, const synth::midi::PickTarget& target);

    std::vector<PickCandidate> candidates_;
    std::vector<Outline> outlines_;
    bool active_ = false;
    bool probing_ = false;
    juce::Component::SafePointer<juce::Component> watchedParent_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PickTargetOverlay)
};

} // namespace synth::ui
