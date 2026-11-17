#pragma once

// The chevron at the left of a sound pack row: Return, Space or a click opens and closes that pack's details. A
// juce::Button, so it is a Tab stop with the accent focus ring; its accessible name carries the state.

#include "UI/Layout/ChevronTurn.h"
#include "UI/Layout/SpaceClaimingButton.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

class SoundPackDisclosure : public SpaceClaimingButton<juce::Button> {
public:
    SoundPackDisclosure();

    /** Names the pack: the accessible name, the tooltip and the state words follow it. */
    void setPackName(const juce::String& name);

    /** Turns the chevron to open or closed; `animate` false lands at once. */
    void setOpen(bool open, bool animate = true);
    bool isOpen() const noexcept { return open_; }

    /** Called when a click or key asks for the other state; the owner decides and calls setOpen. */
    std::function<void(bool open)> onOpenRequested;

    float getChevronOpenness() const noexcept { return turn_.openness(); }

    /** Lights the chevron as if hovered while the pointer is over the pack's title, which toggles it too. */
    void setTitleHovered(bool hovered);
    bool isLit() const { return isOver() || titleHovered_ || hasKeyboardFocus(false); }

    void paintButton(juce::Graphics& g, bool hot, bool down) override;

    // ---- Test seam ----
    /** Sends a key the way the focused button would receive it (Return and Space both toggle). */
    bool pressKeyForTest(const juce::KeyPress& key) { return keyPressed(key); }

private:
    void refreshText();

    juce::String name_;
    bool open_ = false;
    bool titleHovered_ = false;
    ChevronTurn turn_{*this, false};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SoundPackDisclosure)
};

} // namespace synth::ui
