#pragma once

#include "Modules/Sequencer/SequencerPatternOps.h"
#include "Music/MusicalScale.h"
#include "UI/Layout/CalloutReveal.h"
#include "UI/Layout/NoteRangePicker/NoteRangePicker.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

/**
 * The Randomize popover: a note range, how many steps play, whether to keep the rhythm, and a Roll button. It lives in
 * a juce::CallOutBox grown out of the Randomize button and stays open between rolls. It holds no pattern: Roll reports
 * the settings and the grid applies them. Esc closes it. docs/modules/sequencer-design.md#pattern-tools.
 */
class StepGridRandomizePanel : public juce::Component {
public:
    static constexpr int kWidth = 250;

    StepGridRandomizePanel(const sequencer_ops::RandomizeSettings& initial, const MusicalScale& scale,
                           int middleCOctave);
    ~StepGridRandomizePanel() override;

    const sequencer_ops::RandomizeSettings& getSettings() const noexcept { return settings_; }
    NoteRangePicker& getRangePicker() noexcept { return range_; }
    juce::Slider& getDensitySlider() noexcept { return density_; }
    juce::ToggleButton& getKeepRhythm() noexcept { return keepRhythm_; }
    juce::TextButton& getRollButton() noexcept { return roll_; }

    /** Roll was pressed (click, Space or Enter), with the settings as shown. */
    std::function<void(const sequencer_ops::RandomizeSettings&)> onRoll;
    /** The range, density or rhythm choice changed, so the owner can remember it for the session. */
    std::function<void(const sequencer_ops::RandomizeSettings&)> onSettingsChanged;

    void resized() override;
    void paint(juce::Graphics& g) override;
    bool keyPressed(const juce::KeyPress& key) override;
    void parentHierarchyChanged() override;
    void lookAndFeelChanged() override;

private:
    void applyColours();
    void settingsChanged();

    sequencer_ops::RandomizeSettings settings_;
    juce::Label title_{"title", "Randomize in scale"};
    juce::Label rangeLabel_{"rangeLabel", "Note range"};
    juce::Label densityLabel_{"densityLabel", "Steps that play"};
    juce::Label densityValue_{"densityValue"};
    NoteRangePicker range_;
    juce::Slider density_{juce::Slider::LinearHorizontal, juce::Slider::NoTextBox};
    juce::ToggleButton keepRhythm_{"Keep my rhythm"};
    juce::TextButton roll_{"Roll"};
    CalloutReveal reveal_{*this};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StepGridRandomizePanel)
};

} // namespace synth::ui
