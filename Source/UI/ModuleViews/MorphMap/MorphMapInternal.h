#pragma once

// MorphMapInternal.h -- the keyboard stop that sits over each dot and over the cursor (shared by the MorphMap units).
#include "MorphMap.h"
#include "UI/Layout/FocusRing.h"

namespace synth::ui {

/** A transparent, mouse-inert Tab stop over one dot (`index` >= 0), the cursor (-1) or a slider (-2 for X, -3 for Y).
 * It carries the screen-reader name, the tooltip and the focus ring; the map keeps handling the pointer. */
class MorphMap::Stop
    : public juce::Component
    , public juce::SettableTooltipClient {
public:
    static constexpr int kSize = 22;

    Stop(MorphMap& owner, int stopIndex)
        : owner_(owner)
        , index_(stopIndex) {
        setWantsKeyboardFocus(true);
        setInterceptsMouseClicks(false, false);
        setSize(kSize, kSize);
        setPaintingIsUnclipped(true); // the ring's sub-pixel shift moves its edge past the bounds: never clip it flat
    }

    int getIndex() const noexcept { return index_; }
    bool isCursor() const noexcept { return index_ == -1; }
    bool isStrip() const noexcept { return index_ <= -2; }
    Axis axis() const noexcept { return index_ == -2 ? Axis::X : index_ == -3 ? Axis::Y : Axis::None; }

    void paint(juce::Graphics& g) override {
        paintFocusRing(g, getLocalBounds().toFloat().translated(ringOffset_.x, ringOffset_.y), *this,
                       isStrip() ? 3.0f : (float)kSize * 0.5f);
    }
    /** Puts the stop's whole-pixel bounds over the exact `centre` (this component's coordinates): the bounds stay
     *  integer for focus and the screen reader, and the ring shifts by the sub-pixel remainder so it sits evenly
     *  round the dot drawn at `centre`. */
    void placeAt(juce::Point<float> centre) {
        setCentrePosition(centre.roundToInt());
        setRingOffset(centre - getBounds().toFloat().getCentre());
    }
    /** Same for a stop whose bounds are `exact` rounded to whole pixels. */
    void placeOver(juce::Rectangle<float> exact) {
        setBounds(exact.toNearestInt());
        setRingOffset(exact.getCentre() - getBounds().toFloat().getCentre());
    }
    bool keyPressed(const juce::KeyPress& key) override { return owner_.handleStopKey(*this, key); }
    void focusGained(FocusChangeType) override {
        repaint();
        if (index_ >= 0)
            owner_.select(index_);
        else if (index_ == -1)
            owner_.setCursorSelected(true);
        owner_.repaint();
    }
    void focusLost(FocusChangeType) override { repaint(); }

private:
    void setRingOffset(juce::Point<float> offset) {
        if (offset == ringOffset_)
            return;
        ringOffset_ = offset;
        repaint();
    }

    juce::Point<float> ringOffset_;
    MorphMap& owner_;
    int index_;
};

} // namespace synth::ui
