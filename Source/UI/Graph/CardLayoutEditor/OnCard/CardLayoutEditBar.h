#pragma once

// The on-card layout editor's bar in the card header: Cancel and Done on the right, with room to its
// left for the Preset and Apply to controls a later step adds. docs/layout/module-card-layout.md#editing-a-layout.

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

class CardLayoutEditBar final : public juce::Component {
public:
    static constexpr int kHeight = 22;
    static constexpr int kButtonWidth = 56;
    static constexpr int kGap = 6;
    /** The bar's width with only Cancel and Done in it. */
    static constexpr int kMinWidth = kButtonWidth * 2 + kGap + 12;

    CardLayoutEditBar();

    std::function<void()> onCancel;
    std::function<void()> onDone;

    void paint(juce::Graphics&) override;
    void resized() override;
    void lookAndFeelChanged() override;

    juce::TextButton& getCancelButton() noexcept { return cancel_; }
    juce::TextButton& getDoneButton() noexcept { return done_; }

private:
    juce::TextButton cancel_{"Cancel"};
    juce::TextButton done_{"Done"};
};

} // namespace synth::ui
