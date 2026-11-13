#pragma once

#include "ExitEnterListPlan.h"
#include "UI/Layout/UIAnimation.h"
#include <memory>
#include <optional>
#include <vector>

// ExitEnterListMotion.h (docs/layout/animation.md "Delete and undo animation"): plays an ExitEnterListPlan over a
// list that is already in its final state. A picture of the list is drawn by an overlay child of `host` that covers
// the real rows or columns for the length of the timeline (the model change is final and synchronous underneath);
// when it ends the overlay goes and the real ones show. The overlay is paint-only: it takes no mouse, no keyboard
// focus and is hidden from accessibility, so the real, restored row or column keeps its focus and title.
//
// Owned by the list's panel. The owner decides whether to animate at all (on screen, an Animations setting that
// allows it) and only calls start() then; headless runs never get here, tests force it and step applyAtMs() by hand.
namespace synth::ui {

class ExitEnterListOverlay;

class ExitEnterListMotion {
public:
    explicit ExitEnterListMotion(juce::Component& host);
    ~ExitEnterListMotion();
    ExitEnterListMotion(const ExitEnterListMotion&) = delete;
    ExitEnterListMotion& operator=(const ExitEnterListMotion&) = delete;

    struct Job {
        ListAxis axis = ListAxis::Vertical;
        juce::Image picture;       // of `area`, taken at `pictureScale` pixels per point
        juce::Rectangle<int> area; // in the host's coordinates
        float pictureScale = 1.0f;
        juce::Colour background; // fills the overlay under the slices
        juce::Colour accent;     // the 1 px outline of an item that came back
        std::vector<ExitEnterListItem> items;
    };

    /** Starts the motion; false (and nothing drawn) when the plan has nothing to show. Replaces a running one.
     *  Reduce Motion is read here: the shrink and grow become a fade. */
    bool start(Job job);
    /** Lands now: the overlay goes, the real list shows. */
    void finishNow();
    bool isRunning() const noexcept { return overlay_ != nullptr; }

    /** Puts the motion at `elapsedMs` after it began (what each frame does; tests call it by hand). */
    void applyAtMs(double elapsedMs);
    const ExitEnterTimeline& timeline() const noexcept { return timeline_; }

    /** The picture of `area` of `source` that start() wants (`area` in source's coordinates). */
    static juce::Image pictureOf(juce::Component& source, juce::Rectangle<int> area, float& scaleOut);

    // Test seams and inspection.
    int exitGhostCount() const;
    int enterGhostCount() const;
    /** Where `key`'s slice is drawn now, in the host's coordinates; empty when it is not drawn. */
    std::optional<juce::Rectangle<float>> drawnRectFor(const juce::String& key) const;
    /** The outline alpha of `key` now (0 when none). */
    float outlineAlphaFor(const juce::String& key) const;
    bool isReducedMotion() const noexcept { return reduced_; }
    juce::Component* overlayComponent() const noexcept;

private:
    juce::Component& host_;
    std::unique_ptr<ExitEnterListOverlay> overlay_;
    juce::VBlankAnimatorUpdater updater_;
    AnimationDriver driver_;
    ExitEnterTimeline timeline_;
    bool reduced_ = false;
};

} // namespace synth::ui
