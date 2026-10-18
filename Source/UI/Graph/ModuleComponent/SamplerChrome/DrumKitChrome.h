#pragma once

// The Drum Kit card's chrome above the body: the instrument picker (drum kits, SFZ files, the user's recordings, or
// "Synth drums" to go back to the synth voices) and a caption line.

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
    void poll() override { row_.poll(); }
    void refreshLoaded() override { row_.refresh(); }

    InstrumentPickerRow& getRow() noexcept { return row_; }

private:
    juce::String captionText() const;

    DrumKitModule& kit_;
    InstrumentPickerRow row_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DrumKitChrome)
};

} // namespace synth::ui
