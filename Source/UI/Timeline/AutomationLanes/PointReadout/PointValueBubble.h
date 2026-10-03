#pragma once

#include "UI/Layout/UIAnimation.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>

// PointValueBubble -- the small value label an automation lane editor floats above the point under
// the pointer (and above the point being dragged). It fades in and out, never takes focus and is
// painted by its owner, so it adds no child component.
//
// The owner must outlive it and call paint() last in its own paint(); it repaints the owner itself.
// Message thread only.
namespace synth::ui {

class PointValueBubble {
public:
    static constexpr double kFadeInMs = 160.0;
    static constexpr double kFadeOutMs = 110.0;

    explicit PointValueBubble(juce::Component& owner);
    ~PointValueBubble();

    // Shows `text` above `anchor` (the point's centre, owner coordinates). Fades in when hidden;
    // when already shown it only moves and re-texts at the current opacity.
    void show(juce::Point<float> anchor, const juce::String& text);
    // Fades out, still drawn where it was until it has gone.
    void hide();

    void paint(juce::Graphics& g);

    bool isShown() const noexcept { return wanted_; }
    // 0 hidden .. 1 settled; also the bubble's alpha.
    float getOpacity() const noexcept { return opacity_; }
    juce::String getText() const { return content_ ? content_->text : juce::String(); }
    // Where the bubble is drawn now (owner coordinates); empty before the first show.
    juce::Rectangle<int> getBounds() const;

private:
    struct Content {
        juce::Point<float> anchor;
        juce::String text;
    };

    bool animates() const;
    void fadeIn();
    void fadeOut();
    void setOpacity(float value);

    juce::Component& owner_;
    std::optional<Content> content_; // kept through a fade-out so it keeps drawing
    bool wanted_ = false;
    float opacity_ = 0.0f;
    juce::VBlankAnimatorUpdater vblank_;
    AnimationDriver fade_;
};

} // namespace synth::ui
