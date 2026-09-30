#pragma once

#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "UI/Layout/PanelResizeHandle.h"
#include "UI/Layout/UIAnimation.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <map>
#include <optional>
#include <utility>
#include <vector>

// PianoRollVelocityLane — the velocity strip under the piano roll's note grid: one stick per note
// at the note's start x, its height the note's velocity. It owns the strip's painting, hit-testing,
// hover readout and the per-gesture drag state (stick / pen / ramp) and preview values, and knows
// nothing about PianoRollComponent: everything it reads and every edit it makes goes through the
// Host callbacks. Painting is in PianoRollVelocityLanePainting.cpp, gestures in
// PianoRollVelocityLaneGestures.cpp. See docs/timeline/piano-roll.md#velocity-strip.
namespace synth::ui {

class PianoRollVelocityLane : public juce::Component {
public:
    // The strip's height while shown.
    static constexpr int kDefaultHeight = 64;
    // The least a drag on the top-edge handle can shrink the strip to.
    static constexpr int kMinHeight = 32;
    // Readout fade, ms (docs/layout/animation.md "Motion rules").
    static constexpr double kReadoutFadeInMs = 160.0;
    static constexpr double kReadoutFadeOutMs = 110.0;
    // How close (px, horizontally) a press must land to a stick to grab it.
    static constexpr int kStickHitPx = 5;

    // One note as the strip draws it. `x` is in this component's coordinates.
    struct Stick {
        synth::NoteId id;
        int x = 0;
        int velocity = 100;
        bool selected = false;
        juce::Colour colour;
    };

    // Every member may be left empty; an empty provider reads as "nothing", an empty callback as
    // "nobody listening". All are called on the message thread only.
    struct Host {
        std::function<std::vector<Stick>()> sticks;
        std::function<int()> gutterWidth; // px at the left edge that hold the scale, not sticks
        std::function<bool()> isDrawToolActive;
        std::function<void(const std::map<synth::NoteId, int>&)> onPreview;
        std::function<void(const std::vector<std::pair<synth::NoteId, int>>&)> onCommit;
        std::function<void()> onCancel;
        // The top-edge handle's drag: the height asked for, UNCLAMPED (the owner clamps, re-lays
        // out and persists); the commit fires once on release, only if the drag moved.
        std::function<void(int desiredHeight)> onResizeRequest;
        std::function<void(int desiredHeight)> onResizeCommitted;
    };

    enum class Gesture { None, Stick, Pen, Ramp };

    PianoRollVelocityLane();
    ~PianoRollVelocityLane() override;

    void setHost(Host host);

    // Abandons an in-flight gesture (nothing committed, onCancel fired); false when idle.
    bool cancelGesture();
    bool isGestureActive() const noexcept;
    Gesture getGesture() const noexcept;

    // The y of `velocity`'s stick head, in this component's coordinates.
    float yForVelocity(int velocity) const noexcept;
    int velocityForY(float y) const noexcept;

    // The note whose value is shown next to its stick (hovered or being dragged), or invalid.
    synth::NoteId getReadoutNote() const noexcept;
    // The readout's fade (0 hidden .. 1 settled); it is also the readout's alpha.
    float getReadoutOpacity() const noexcept;
    PanelResizeHandle& getResizeHandle() noexcept;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;
    // Escape cancels a gesture; every other key falls through to the parent.
    bool keyPressed(const juce::KeyPress& key) override;

private:
    float plotTop() const noexcept;
    float plotBottom() const noexcept;
    int gutterWidth() const;
    std::vector<Stick> currentSticks() const;

    void beginGesture(juce::Point<float> pos, bool shift);
    void updateGesture(juce::Point<float> pos);
    void applyStickDrag(juce::Point<float> pos);
    void applyLine(juce::Point<float> from, juce::Point<float> to, bool accumulate);
    void publishPreview();
    void finishGesture(bool commit);
    synth::NoteId stickUnder(juce::Point<float> pos) const;

    void paintScale(juce::Graphics& g, int gutter);
    void paintSticks(juce::Graphics& g, int gutter);
    void paintReadout(juce::Graphics& g, const Stick& stick, int velocity);
    juce::Rectangle<int> readoutBox(const Stick& stick, int velocity, float t);

    // The one place the readout note changes: fades on appear / disappear only (see .cpp).
    void setReadout(synth::NoteId next);
    void fadeReadoutIn();
    void fadeReadoutOut();
    void setReadoutOpacity(float value);

    Host host_;
    Gesture gesture_ = Gesture::None;
    // What the notes looked like when the gesture began, and which of them it may touch.
    std::vector<Stick> origins_;
    std::vector<Stick> eligible_;
    std::map<synth::NoteId, int> preview_;
    // Stick gesture: the grabbed note, and whether it drags the whole selection by a delta.
    synth::NoteId grabbed_;
    bool grabbedRelative_ = false;
    juce::Point<float> anchor_;
    juce::Point<float> lastPos_;
    synth::NoteId hovered_;
    synth::NoteId readout_;
    // The readout as last painted, kept so a fade-out keeps drawing it after readout_ is cleared.
    struct ShownReadout {
        Stick stick;
        int velocity = 0;
    };
    std::optional<ShownReadout> shown_;
    float readoutOpacity_ = 0.0f;
    PanelResizeHandle resizeHandle_{*this};
    juce::VBlankAnimatorUpdater vblank_{this};
    AnimationDriver fade_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PianoRollVelocityLane)
};

} // namespace synth::ui
