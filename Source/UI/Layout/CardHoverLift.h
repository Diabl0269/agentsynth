#pragma once

#include "UI/Layout/FadeVisibility.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/Layout/UIAnimation.h"
#include "UI/Theme/Theme.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

// CardHoverLift (docs/layout/theming.md#depth-levels): at Depth Rich a card rises 1.5px while the pointer is over it.
// Flat and Soft never lift. The rise is a component transform (a translation), so the card's children, its cached
// picture and its bounds stay as they are and only that one card repaints.
//
// Attach it once to a card with `attach`; the helper lives in the card's properties (like ButtonStateEase), listens
// to the card and its children for the pointer, and needs no code from the card's owner. One short eased tween (140 ms
// in, 110 ms out) runs on a VBlank clock only while the lift moves, so a card at rest runs nothing. Under Reduce
// Motion, with Animations Off, or while the card is not on screen, the lift lands at once. A card that something else
// already transforms (the load reveal) is left alone until that is done. Call `refresh` when the look changes: a card
// that is lifted when the level leaves Rich drops at once.
namespace synth::ui {

class CardHoverLift final
    : public juce::ReferenceCountedObject
    , private juce::MouseListener
    , private juce::ComponentListener {
public:
    static constexpr float kLiftPx = 1.5f;
    static constexpr double kInMs = 140.0;
    static constexpr double kOutMs = 110.0;

    /** Gives `card` the lift. A second call is a no-op. */
    static void attach(juce::Component& card);

    /** The lift attached to `card`, or null. */
    static CardHoverLift* find(const juce::Component& card);

    /** Re-reads the Depth level: drops a lifted card at once when it is no longer Rich. */
    static void refresh(juce::Component& card);

    /** How many pixels the card is raised right now (0 to kLiftPx). */
    float lift() const noexcept { return lift_; }
    bool isRunning() const noexcept { return driver_.isRunning(); }

    /** Test seams: the pointer entering and leaving the card (what the mouse listener does), and landing a running
     *  tween on its last frame. */
    void setHoveredForTest(bool hovered) { setHovered(hovered); }
    void landForTest();

    ~CardHoverLift() override;

private:
    explicit CardHoverLift(juce::Component& card);

    void mouseEnter(const juce::MouseEvent&) override { setHovered(true); }
    void mouseExit(const juce::MouseEvent&) override;
    void componentBeingDeleted(juce::Component&) override;

    bool isRich() const;
    void setHovered(bool hovered);
    void moveTo(float target);
    void apply(float lift);

    juce::Component* card_;
    std::unique_ptr<juce::VBlankAnimatorUpdater> updater_;
    AnimationDriver driver_;
    float lift_ = 0.0f;
    float target_ = 0.0f;
    bool hovered_ = false;
    juce::AffineTransform applied_; // what this helper last set; anything else on the card belongs to another motion
};

} // namespace synth::ui
