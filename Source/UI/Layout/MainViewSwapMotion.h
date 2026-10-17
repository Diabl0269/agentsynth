#pragma once

#include "UI/Layout/UIAnimation.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

// MainViewSwapMotion.h (docs/layout/animation.md "What moves, and how"): the picture show behind a swap of the main
// area's view with the bottom panel's. The owner lays everything out for the final state first; this hides the real
// components under two opaque layers (one over the main area, one over the panel's body) while the leaving view's
// picture shrinks down into the panel body and the arriving view's picture grows from the panel body up to the main
// area, 240 ms easeOutCubic. Under Reduce Motion only the main-area layer shows, the leaving picture fading out over
// the real view in 80 ms. Layers take no mouse, no keyboard focus and are hidden from accessibility. Frames run only
// while the tween does.
namespace synth::ui {

class MainViewSwapMotion {
public:
    static constexpr double kMoveMs = 240.0;
    static constexpr double kFadeMs = 80.0;

    /** `host` is the component both layers are children of (the window's root); coordinates below are its own. */
    explicit MainViewSwapMotion(juce::Component& host);

    /** Called with true when a motion starts and false when it has ended, however it ended. */
    std::function<void(bool running)> onRunningChanged;

    /** Pictures `mainArea` as it is now, before the swap changes anything. Returns false, and takes nothing, when the
     *  swap will not animate: the host is not on screen, Animations is Off, or the rectangle is empty. Finishes a
     *  motion still running first, so its end state is what the next one starts from. */
    bool begin(juce::Rectangle<int> mainArea);
    /** Starts the motion once the swap is laid out: pictures the arriving view in `mainArea` and moves it from
     *  `panelBody`. Returns false, and shows nothing, when begin() took no picture or the panel body is empty. */
    bool play(juce::Rectangle<int> panelBody);
    /** Drops a picture taken by begin() that play() will not use. */
    void discardPending() noexcept;
    /** Ends a running motion on its final frame now; does nothing when none runs. */
    void finishNow();

    bool isRunning() const noexcept { return running_; }
    /** True for the plain fade (Reduce Motion), false for the move. */
    bool isFade() const noexcept { return fade_; }
    /** Eased progress, 0 to 1. */
    float progress() const noexcept { return progress_; }
    double durationMs() const noexcept { return fade_ ? kFadeMs : kMoveMs; }
    /** Where the leaving and arriving pictures are drawn now, in the host's coordinates (the arriving one is empty
     *  under the fade). */
    juce::Rectangle<float> leavingRect() const noexcept;
    juce::Rectangle<float> arrivingRect() const noexcept;
    /** The layers, for tests: the main-area layer and the panel-body layer. */
    juce::Component& mainLayer() noexcept { return mainLayer_; }
    juce::Component& bodyLayer() noexcept { return bodyLayer_; }
    /** Test seam: jumps to eased progress `t` without a clock; 1 ends the motion. */
    void stepForTest(float t);

private:
    class Layer final : public juce::Component {
    public:
        explicit Layer(MainViewSwapMotion& owner);
        void paint(juce::Graphics& g) override;

    private:
        MainViewSwapMotion& owner_;
    };

    void setProgress(float t);
    void end(bool stopDriver);
    void paintLayer(juce::Graphics& g, const Layer& layer) const;
    juce::Image pictureOf(juce::Rectangle<int> area) const;

    juce::Component& host_;
    Layer mainLayer_{*this};
    Layer bodyLayer_{*this};
    juce::VBlankAnimatorUpdater updater_{&mainLayer_};
    AnimationDriver driver_;
    juce::Image leaving_, arriving_;
    juce::Rectangle<int> mainArea_, panelBody_;
    float progress_ = 0.0f;
    bool running_ = false;
    bool fade_ = false;
};

} // namespace synth::ui
