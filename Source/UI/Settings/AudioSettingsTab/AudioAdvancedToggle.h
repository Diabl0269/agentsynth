#pragma once

#include "UI/Layout/ChevronTurn.h"
#include "UI/Layout/SpaceClaimingButton.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

// The "Advanced" row of the Device section: a chevron and a word. Return, Space or a click asks for the other state;
// the owner decides and calls setOpen. A juce::Button, so it is a Tab stop with the accent ring, and its accessible
// name carries the state.
class AudioAdvancedToggle : public synth::ui::SpaceClaimingButton<juce::Button> {
public:
    AudioAdvancedToggle();

    /** Turns the chevron to open or closed; `animate` false lands at once. */
    void setOpen(bool open, bool animate = true);
    bool isOpen() const noexcept { return open_; }

    std::function<void(bool open)> onOpenRequested;

    void paintButton(juce::Graphics& g, bool hot, bool down) override;

    /** Sends a key the way the focused button receives it (Return and Space both toggle). */
    bool pressKeyForTest(const juce::KeyPress& key) { return keyPressed(key); }

private:
    void refreshText();

    bool open_ = false;
    synth::ui::ChevronTurn turn_{*this, false};
};
