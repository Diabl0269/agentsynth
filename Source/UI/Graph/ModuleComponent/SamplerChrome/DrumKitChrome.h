#pragma once

// The Drum Kit card's chrome above the body: the instrument picker (drum kits, SFZ files, the user's recordings, or
// "Synth drums" to go back to the synth voices), a caption line, and the "Drum machine" button that opens the step
// grid on the track playing this kit (disabled, with the reason as its tooltip, while none does).

#include "Modules/DrumKit/DrumKitModule.h"
#include "UI/Graph/ModuleComponent/SamplerChrome/InstrumentChrome.h"
#include "UI/Graph/ModuleComponent/SamplerChrome/InstrumentPickerRow.h"

namespace synth::ui {

class DrumKitChrome : public InstrumentChrome {
public:
    /** The height the chrome adds above the body. */
    static int height();

    /** The kit must outlive this. */
    DrumKitChrome(Host host, DrumKitModule& kit);

    int layout(int y, int contentX, int contentW, int narrowX, int narrowW, bool apply) override;
    void poll() override;
    void refreshLoaded() override { row_.refresh(); }

    InstrumentPickerRow& getRow() noexcept { return row_; }
    juce::Button& getDrumMachineButton() noexcept { return drumMachineButton_; }
    /** Re-reads whether a track plays the kit and enables the button to match. */
    void refreshDrumMachineButton();

private:
    juce::String captionText() const;

    DrumKitModule& kit_;
    InstrumentChrome::Host host_;
    InstrumentPickerRow row_;
    juce::TextButton drumMachineButton_{"Drum machine"};
    int pollsSinceRefresh_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DrumKitChrome)
};

} // namespace synth::ui
