#pragma once

// ShapeStackPanel.h -- the small popover Ring helpers > Stack harmonics opens: the root note, how many harmonics, how
// much quieter each one is and how wide the peaks are, then Stack (or Cancel). It lives in a juce::CallOutBox grown
// out of the Ring helpers button and holds no curve: Stack reports the options and the shape window applies them as
// one undo step.
#include "ShapeRingHelpers.h"
#include "UI/Graph/CardWidgets/CardSegmentedSwitch.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

class ShapeStackPanel : public juce::Component {
public:
    static constexpr int kWidth = 270;
    static constexpr int kHeight = 192;

    /** `initial.rootHz` is shown as the nearest note of the Root box. */
    explicit ShapeStackPanel(const ringhelpers::StackOptions& initial);
    ~ShapeStackPanel() override;

    /** The options as the controls show them now. */
    ringhelpers::StackOptions getOptions() const;
    /** Stack was pressed (click or Return), with the options as shown; the panel closes itself afterwards. */
    std::function<void(const ringhelpers::StackOptions&)> onStack;

    juce::ComboBox& getRootBox() noexcept { return root_; }
    juce::ComboBox& getCountBox() noexcept { return count_; }
    juce::ComboBox& getStepBox() noexcept { return step_; }
    CardSegmentedSwitch& getWidthSwitch() noexcept { return width_; }
    juce::TextButton& getStackButton() noexcept { return stack_; }
    juce::TextButton& getCancelButton() noexcept { return cancel_; }

    void resized() override;
    void paint(juce::Graphics& g) override;
    bool keyPressed(const juce::KeyPress& key) override;
    void lookAndFeelChanged() override;

private:
    void applyColours();

    juce::Label title_{"title", "Stack harmonics"};
    juce::Label rootLabel_, countLabel_, stepLabel_, widthLabel_;
    juce::ComboBox root_, count_, step_;
    CardSegmentedSwitch width_;
    juce::TextButton cancel_{"Cancel"}, stack_{"Stack"};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ShapeStackPanel)
};

} // namespace synth::ui
