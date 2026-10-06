#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

/**
 * The dashed area under the card that a control dragged below the card's bottom edge drops into to hide
 * it ("Drop to hide"). It draws only: the editor owns when it fades in and out (during a drag) and
 * whether the pointer is over it, and it never takes the mouse. Hiding without the mouse is Backspace on
 * the focused control. docs/layout/module-card-layout.md#editing-a-layout.
 */
class CardLayoutHideZone final : public juce::Component {
public:
    CardLayoutHideZone();

    /** The pointer is over the zone: the control would be hidden if released now. */
    void setActive(bool active);
    bool isActive() const noexcept { return active_; }

    void paint(juce::Graphics&) override;

private:
    bool active_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CardLayoutHideZone)
};

} // namespace synth::ui
