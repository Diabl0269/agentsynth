#pragma once

#include "UI/Layout/FadeVisibility.h"
#include <algorithm>
#include <juce_gui_basics/juce_gui_basics.h>
#include <utility>

// FadeOutPicture: a still picture of something that has just gone, faded away over whatever replaced it, so a thing
// that empties itself as it closes (the piano roll going back to the clip lanes) still leaves the way it came in.
// It takes no mouse, no keyboard focus and is hidden from accessibility; the fade is a FadeVisibility, so it is 110 ms,
// a plain 80 ms fade under Reduce Motion, and it is never shown at all when the parent is not on screen or
// Animations is Off (the change lands at once). docs/layout/animation.md "Fading things in and out".
namespace synth::ui {

class FadeOutPicture : public juce::Component {
public:
    FadeOutPicture() {
        setInterceptsMouseClicks(false, false);
        setWantsKeyboardFocus(false);
        setAccessible(false);
        setOpaque(false);
    }

    /** Pictures `source` as it is now, for a play() that follows. Replaces any earlier capture; takes nothing when the
     *  parent is not animating or `source` is not on show. */
    void capture(juce::Component& source) {
        pending_ = {};
        pendingBounds_ = source.getBounds();
        if (!source.isVisible() || !FadeVisibility::canAnimateIn(getParentComponent()))
            return;
        const auto& displays = juce::Desktop::getInstance().getDisplays();
        const float scale = displays.getPrimaryDisplay() != nullptr
                                ? std::clamp(static_cast<float>(displays.getPrimaryDisplay()->scale), 1.0f, 2.0f)
                                : 1.0f;
        pending_ = source.createComponentSnapshot(source.getLocalBounds(), true, scale);
    }

    /** Puts the captured picture over where the source stood (parent coordinates) and fades it out. Returns false, and
     * shows nothing, when nothing was captured or the parent is not animating. */
    bool play() {
        const auto bounds = pendingBounds_;
        auto picture = std::exchange(pending_, juce::Image{});
        if (picture.isNull() || bounds.isEmpty() || !FadeVisibility::canAnimateIn(getParentComponent()))
            return false;
        picture_ = std::move(picture);
        setBounds(bounds);
        fade_.snapTo(true);
        fade_.onHidden = [this] { picture_ = {}; };
        repaint();
        fade_.setShown(false);
        return true;
    }

    /** True while the picture is on screen. */
    bool isPlaying() const { return isVisible() && picture_.isValid(); }

    void paint(juce::Graphics& g) override {
        if (picture_.isValid())
            g.drawImage(picture_, getLocalBounds().toFloat());
    }

private:
    juce::Rectangle<int> pendingBounds_;
    juce::Image pending_; // pictured by capture(), shown by play()
    juce::Image picture_;
    FadeVisibility fade_{this};
};

} // namespace synth::ui
