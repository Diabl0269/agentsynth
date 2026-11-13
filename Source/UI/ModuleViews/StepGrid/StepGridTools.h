#pragma once

#include <array>
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

namespace synth::ui {

/**
 * The pattern tools row above the step grid: Randomize, shift left and right, Reverse, Double, move up and down a scale
 * note, Fit to scale, right-aligned in 24 px chips. Each button is a Tab stop with a focus ring; Space or Enter acts.
 * The row only reports which tool was pressed (onTool) and shows which are enabled; the grid applies them.
 * docs/modules/sequencer-design.md#pattern-tools.
 */
class StepGridTools : public juce::Component {
public:
    static constexpr int kRowHeight = 28;
    static constexpr int kButtonHeight = 24;

    enum class Tool { Randomize, ShiftLeft, ShiftRight, Reverse, Double, Up, Down, FitToScale };
    static constexpr int kToolCount = 8;

    StepGridTools();
    ~StepGridTools() override;

    juce::Button& getButton(Tool tool) noexcept;
    const juce::Button& getButton(Tool tool) const noexcept;
    /** The words a button's tooltip and accessibility name say. */
    static juce::String tooltipFor(Tool tool);

    /** Double is off when the pattern cannot double; Fit to scale is off when it would change nothing. */
    void setAvailability(bool canDouble, bool canFit);

    /** A tool button was pressed (click, Space or Enter). */
    std::function<void(Tool tool)> onTool;

    void resized() override;

private:
    class ToolButton;
    std::array<std::unique_ptr<ToolButton>, kToolCount> buttons_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StepGridTools)
};

} // namespace synth::ui
