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
    }

    int getIndex() const noexcept { return index_; }
    bool isCursor() const noexcept { return index_ == -1; }
    bool isStrip() const noexcept { return index_ <= -2; }
    Axis axis() const noexcept { return index_ == -2 ? Axis::X : index_ == -3 ? Axis::Y : Axis::None; }

    void paint(juce::Graphics& g) override {
        paintFocusRing(g, getLocalBounds().toFloat(), *this, isStrip() ? 3.0f : (float)kSize * 0.5f);
    }
    bool keyPressed(const juce::KeyPress& key) override { return owner_.handleStopKey(*this, key); }
    void focusGained(FocusChangeType) override {
        repaint();
        if (index_ >= 0)
            owner_.select(index_);
        owner_.repaint();
    }
    void focusLost(FocusChangeType) override { repaint(); }

private:
    MorphMap& owner_;
    int index_;
};

} // namespace synth::ui
