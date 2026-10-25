#pragma once

#include "UI/Chrome/ToolbarButton/ToolbarButton.h"
#include "UI/Layout/DetachablePanelHost/DockBackSlot.h"
#include "UI/Layout/SidePane/SidePaneToggleButton.h"
#include "UI/Layout/SpaceClaimingButton.h"
#include "UI/Mixer/MixerSections/MixerSectionLayout.h"
#include <array>
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

// MixerPanelToolbar.h (docs/mixer/panel.md#the-toolbar): the slim row along the top of the mixer
// panel -- the side-pane button at the very left, then the Inserts / Sends / EQ show-hide toggles, "Reset Meters" and
// "Add Bus" on the right, each an icon chip with its name beside it. It belongs to the panel, so it travels with it
// into a detached window and the Own-panel strip.
namespace synth::ui {

class MixerPanelToolbar : public juce::Component {
public:
    static constexpr int kHeight = 24;

    MixerPanelToolbar();

    /** `layout` must outlive this toolbar; the toggles mirror its hidden flags. */
    void setLayout(MixerSectionLayout& layout);
    /** Re-reads the hidden flags into the toggles' on-state and names. */
    void refresh();

    /** Fired by the "Add Bus" button and the "Reset Meters" button. */
    std::function<void()> onAddBus;
    std::function<void()> onResetMeters;
    /** The display text of a section toggle's shortcut ("Ctrl+S"), appended to its tooltip; empty for none. */
    std::function<juce::String(MixerSection)> shortcutTextFor;

    /** The button that shows and hides the panel's side pane; follows `pane` (which must outlive it). */
    void bindSidePane(SidePane& pane);
    SidePaneToggleButton& getSidePaneButton() noexcept { return paneButton_; }

    /** The "Dock back" button at the toolbar's right end while the panel is in its own window (see DockBackSlot). */
    void setDockBackButton(juce::Button* button) {
        DockBackSlot::adopt(*this, dockBack_, button);
        resized();
    }

    juce::Button& getSectionToggle(MixerSection section) noexcept { return toggles_[(size_t)section]; }
    juce::Button& getSectionToggleForTest(MixerSection section) noexcept { return getSectionToggle(section); }
    ToolbarButton& getAddBusButtonForTest() noexcept { return addBusButton_; }
    ToolbarButton& getResetMetersButtonForTest() noexcept { return resetMetersButton_; }

    /** Shows each button's name beside its icon (true) or only the icon, the name in its tooltip (false). */
    void setLabelsShown(bool shown);
    bool areLabelsShown() const noexcept { return labelsShown_; }

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    MixerSectionLayout* layout_ = nullptr;
    SidePaneToggleButton paneButton_;
    juce::Button* dockBack_ = nullptr; // borrowed; a child while set
    bool labelsShown_ = true;
    std::array<SpaceClaimingButton<ToolbarButton>, MixerSectionLayout::kSectionCount> toggles_{
        SpaceClaimingButton<ToolbarButton>("Inserts"), SpaceClaimingButton<ToolbarButton>("Sends"),
        SpaceClaimingButton<ToolbarButton>("EQ")};
    SpaceClaimingButton<ToolbarButton> resetMetersButton_{"Reset Meters"};
    SpaceClaimingButton<ToolbarButton> addBusButton_{"Add Bus"};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixerPanelToolbar)
};

} // namespace synth::ui
