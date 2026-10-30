#pragma once

// The Drum Kit card's chrome above the body: the instrument picker (drum kits, SFZ files, the user's recordings, or
// "Synth drums" to go back to the synth voices), a caption line, and the "Drum machine" button that opens the step
// grid on the track playing this kit (disabled, with the reason as its tooltip, while none does). Under the body's
// master Level it places the pads area (DrumKitPadsSection).

#include "Modules/DrumKit/DrumKitModule.h"
#include "UI/Graph/ModuleComponent/DrumKitPads/DrumKitPadsSection.h"
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
    int layoutBelowBody(int y, int contentX, int contentW, bool apply) override {
        return pads_.layout(y, contentX, contentW, apply);
    }
    void poll() override;
    void refreshLoaded() override { row_.refresh(); }

    InstrumentPickerRow& getRow() noexcept { return row_; }
    juce::Button& getDrumMachineButton() noexcept { return drumMachineButton_; }
    DrumKitPadsSection& getPads() noexcept { return pads_; }
    /** Re-reads whether a track plays the kit and enables the button to match. */
    void refreshDrumMachineButton();

private:
    juce::String captionText() const;
    const Host& addDrumMachineButton(const Host& host);

    DrumKitModule& kit_;
    InstrumentChrome::Host host_;
    InstrumentPickerRow row_;
    juce::TextButton drumMachineButton_{"Drum machine"};
    DrumKitPadsSection pads_;
    int pollsSinceRefresh_ = 0;
    int captionCount_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DrumKitChrome)
};

} // namespace synth::ui
