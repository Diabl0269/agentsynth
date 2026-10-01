#pragma once

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth {

/**
 * The folded More row's button, titled "More controls (N)". A Tab stop with the accent focus ring;
 * a click, Return or Space calls `onToggle` straight away. docs/layout/module-card.md#the-more-row.
 */
class CardMoreButton : public juce::TextButton {
public:
    explicit CardMoreButton(int hiddenCount);

    std::function<void()> onToggle;

    /** Reflects the fold state in the tooltip. */
    void setUnfolded(bool unfolded);

    bool keyPressed(const juce::KeyPress& key) override;
    void paintButton(juce::Graphics& g, bool highlighted, bool down) override;
    void focusGained(FocusChangeType cause) override;
    void focusLost(FocusChangeType cause) override;
    void clicked() override;
};

} // namespace synth
