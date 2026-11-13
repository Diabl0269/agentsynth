#pragma once

// The on-card layout editor's bar in the card header: Preset and Apply to (each opens a menu the editor
// builds), then Cancel and Done. docs/layout/module-card-layout.md#editing-a-layout.

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

class CardLayoutEditBar final : public juce::Component {
public:
    static constexpr int kHeight = 22;
    static constexpr int kButtonWidth = 56;
    static constexpr int kPresetWidth = 52;
    static constexpr int kApplyToWidth = 60;
    static constexpr int kGap = 6;
    /** The bar's width with all four buttons in it: it fits a standard card's header. */
    static constexpr int kMinWidth = kPresetWidth + kApplyToWidth + kButtonWidth * 2 + kGap * 3 + 12;

    CardLayoutEditBar();

    std::function<void()> onPreset;
    std::function<void()> onApplyTo;
    std::function<void()> onCancel;
    std::function<void()> onDone;

    void paint(juce::Graphics&) override;
    void resized() override;
    void lookAndFeelChanged() override;

    juce::TextButton& getPresetButton() noexcept { return preset_; }
    juce::TextButton& getApplyToButton() noexcept { return applyTo_; }
    juce::TextButton& getCancelButton() noexcept { return cancel_; }
    juce::TextButton& getDoneButton() noexcept { return done_; }

private:
    juce::TextButton preset_{"Preset"};
    juce::TextButton applyTo_{"Apply to"};
    juce::TextButton cancel_{"Cancel"};
    juce::TextButton done_{"Done"};
};

} // namespace synth::ui
