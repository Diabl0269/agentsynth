#pragma once

#include "UI/Layout/SidePane/SidePaneToggleButton.h"
#include "UI/Mixer/MixerSections/MixerSectionLayout.h"
#include <array>
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

// MixerPanelToolbar.h (docs/mixer/panel.md#the-toolbar): the slim row along the top of the mixer
// panel -- the side-pane button at the very left, then the Inserts / Sends / EQ show-hide toggles, "Reset Meters" and
// "+ Bus" on the right. It belongs to the panel, so it travels with it into a detached window and the Own-panel strip.
namespace synth::ui {

class MixerPanelToolbar : public juce::Component {
public:
    static constexpr int kHeight = 24;

    MixerPanelToolbar();

    /** `layout` must outlive this toolbar; the toggles mirror its hidden flags. */
    void setLayout(MixerSectionLayout& layout);
    /** Re-reads the hidden flags into the toggles' on-state and names. */
    void refresh();

    /** Fired by the "+ Bus" button and the "Reset Meters" button. */
    std::function<void()> onAddBus;
    std::function<void()> onResetMeters;
    /** The display text of a section toggle's shortcut ("Ctrl+S"), appended to its tooltip; empty for none. */
    std::function<juce::String(MixerSection)> shortcutTextFor;

    /** The button that shows and hides the panel's side pane; follows `pane` (which must outlive it). */
    void bindSidePane(SidePane& pane);
    SidePaneToggleButton& getSidePaneButton() noexcept { return paneButton_; }

    juce::Button& getSectionToggle(MixerSection section) noexcept { return toggles_[(size_t)section]; }
    juce::Button& getSectionToggleForTest(MixerSection section) noexcept { return getSectionToggle(section); }
    juce::TextButton& getAddBusButtonForTest() noexcept { return addBusButton_; }
    juce::TextButton& getResetMetersButtonForTest() noexcept { return resetMetersButton_; }

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    MixerSectionLayout* layout_ = nullptr;
    SidePaneToggleButton paneButton_;
    std::array<juce::DrawableButton, MixerSectionLayout::kSectionCount> toggles_{
        juce::DrawableButton("Inserts", juce::DrawableButton::ImageAboveTextLabel),
        juce::DrawableButton("Sends", juce::DrawableButton::ImageAboveTextLabel),
        juce::DrawableButton("EQ", juce::DrawableButton::ImageAboveTextLabel)};
    juce::TextButton resetMetersButton_{"Reset Meters"};
    juce::TextButton addBusButton_{"+ Bus"};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixerPanelToolbar)
};

} // namespace synth::ui
