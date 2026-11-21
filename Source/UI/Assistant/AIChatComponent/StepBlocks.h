#pragma once

#include "AI/SongSteps/SongSteps.h"
#include "StepIcons.h"
#include "UI/Layout/UIAnimation.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <vector>

// The steps of a song build as one block each in a sunken box (docs/ai/song-steps.md#the-song-card). A block is the
// step's kind art in the kind's own colour (stepKindColour), with a small check once the step is done. The box wraps
// the blocks onto further rows; past kMaxHeight it scrolls vertically, so 30 steps never run off the side or clip. A
// highlight ring follows the step that is running, gliding from block to block through the shared motion rules.
namespace synth {

class StepBlockBox;

class StepBlock
    : public juce::Component
    , public juce::SettableTooltipClient {
public:
    StepBlock();

    void update(const songsteps::StepState& state);
    const songsteps::StepState& state() const { return step_; }
    songsteps::StepStatus status() const { return step_.status; }
    synth::ui::StepIconView& iconView() { return icon_; }
    juce::Colour kindColour() const;

    void resized() override;
    void paint(juce::Graphics& g) override;
    void focusGained(FocusChangeType) override;
    void focusLost(FocusChangeType) override { repaint(); }
    bool keyPressed(const juce::KeyPress& key) override;

private:
    StepBlockBox* box() const;

    songsteps::StepState step_;
    synth::ui::StepIconView icon_;
};

class StepBlockBox : public juce::Component {
public:
    static constexpr int kBlock = 28;
    static constexpr int kGap = 4;
    static constexpr int kInset = 6;
    static constexpr int kMaxHeight = 200; // about six rows; more steps scroll inside the box
    static constexpr int kScrollBar = 8;

    StepBlockBox();
    ~StepBlockBox() override;

    /** Adds blocks for new steps and updates every block; the highlight follows the running (or fixing) step. */
    void setSteps(const std::vector<songsteps::StepState>& steps);
    /** The blocks are Tab stops while the fold is open; folded away, none is. */
    void setBlocksFocusable(bool focusable);

    /** The box's height for `width`: every row of blocks, capped at kMaxHeight (then it scrolls). */
    int heightFor(int width) const;
    int contentHeightFor(int width) const;
    int columnsFor(int width) const;
    bool scrolls(int width) const { return contentHeightFor(width) > kMaxHeight; }

    int blockCount() const { return (int)blocks_.size(); }
    StepBlock* block(int i) const { return blocks_[(size_t)i].get(); }
    /** A block's bounds in the box's own coordinates (where it is drawn now, scroll included). */
    juce::Rectangle<int> blockBoundsInBox(int i) const;
    /** The block's bounds in the scrolled content (where it sits whatever the scroll). */
    juce::Rectangle<int> blockBoundsInContent(int i) const;
    int highlightedIndexForTesting() const { return highlight_; }
    juce::Rectangle<float> highlightRectForTesting() const { return hlRect_; }
    juce::Viewport& viewportForTesting() { return viewport_; }

    void scrollToShow(const juce::Component& block);
    void moveFocus(const StepBlock& from, int dx, int dy);

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    class Content;
    int columnsIn(int viewportWidth) const;
    int contentHeightIn(int viewportWidth) const;
    void layoutContent(int width);
    void setHighlight(int index);
    juce::Rectangle<float> highlightTarget() const;

    std::unique_ptr<Content> content_;
    juce::Viewport viewport_;
    std::vector<std::unique_ptr<StepBlock>> blocks_;
    int highlight_ = -1;
    juce::Rectangle<float> hlRect_, hlFrom_, hlTo_;
    float hlAlpha_ = 0.0f, hlAlphaFrom_ = 0.0f, hlAlphaTo_ = 0.0f;
    int shownWidth_ = 0;
    synth::ui::AnimationDriver hlAnim_;
    juce::VBlankAnimatorUpdater vblank_{this};
};

} // namespace synth
