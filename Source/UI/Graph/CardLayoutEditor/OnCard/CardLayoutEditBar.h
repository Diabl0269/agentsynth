#pragma once

// The on-card layout editor's bar in the card header: on an ADSR card the "Time and tempo" switch, then Preset
// and Apply to (each opens a menu the editor builds), then Cancel and Done.
// docs/layout/module-card-layout.md#editing-a-layout.

#include "UI/Graph/CardWidgets/CardSegmentedSwitch.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>

namespace synth::ui {

class CardLayoutEditBar final : public juce::Component {
public:
    static constexpr int kHeight = 22;
    static constexpr int kButtonWidth = 56;
    static constexpr int kPresetWidth = 52;
    static constexpr int kApplyToWidth = 60;
    static constexpr int kGap = 6;
    /** The Time and tempo switch's width. */
    static constexpr int kTimeTempoWidth = 120;
    static constexpr int kRowGap = 4;
    /** The bar's width with all four buttons in it: it fits a standard card's header. */
    static constexpr int kMinWidth = kPresetWidth + kApplyToWidth + kButtonWidth * 2 + kGap * 3 + 12;
    /** The width the bar wants: the four buttons, and the switch beside them when `withTimeTempo`. */
    static constexpr int preferredWidth(bool withTimeTempo) {
        return withTimeTempo ? kMinWidth + kGap + kTimeTempoWidth : kMinWidth;
    }
    /** A card too narrow for the switch beside the buttons gets it on a second row under them. */
    static constexpr bool needsTwoRows(int availableWidth) { return availableWidth < preferredWidth(true); }
    static constexpr int heightFor(bool twoRows) { return twoRows ? kHeight * 2 + kRowGap : kHeight; }

    CardLayoutEditBar();

    std::function<void()> onPreset;
    std::function<void()> onApplyTo;
    std::function<void()> onCancel;
    std::function<void()> onDone;
    /** Fired with the segment picked on the Time and tempo switch: 0 Shared, 1 Separate. */
    std::function<void(int index)> onTimeTempo;

    /** Shows the Time and tempo switch with segment `index` selected, or hides it for nullopt. */
    void setTimeTempo(std::optional<int> index);
    bool hasTimeTempo() const noexcept { return timeTempo_.isVisible(); }
    /** Puts the switch on a second row (the bar is then heightFor(true) tall) instead of beside Preset. */
    void setTwoRows(bool twoRows);
    bool isTwoRows() const noexcept { return twoRows_; }

    void paint(juce::Graphics&) override;
    void resized() override;
    void lookAndFeelChanged() override;

    juce::TextButton& getPresetButton() noexcept { return preset_; }
    juce::TextButton& getApplyToButton() noexcept { return applyTo_; }
    juce::TextButton& getCancelButton() noexcept { return cancel_; }
    juce::TextButton& getDoneButton() noexcept { return done_; }
    CardSegmentedSwitch& getTimeTempoSwitch() noexcept { return timeTempo_; }

private:
    CardSegmentedSwitch timeTempo_{"Time and tempo", juce::StringArray{"Shared", "Separate"}};
    juce::TextButton preset_{"Preset"};
    juce::TextButton applyTo_{"Apply to"};
    juce::TextButton cancel_{"Cancel"};
    juce::TextButton done_{"Done"};
    bool twoRows_ = false;
};

} // namespace synth::ui
