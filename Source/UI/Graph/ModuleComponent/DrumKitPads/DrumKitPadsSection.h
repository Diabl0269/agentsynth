#pragma once

// The Drum Kit card's pads area, under the master Level: the "Pads" count stepper (4 to 32 in fours), the pad grid,
// a caption line, and the picked pad's controls. docs/modules/drum-kit.md#the-card.

#include "Modules/DrumKit/DrumKitModule.h"
#include "UI/Graph/CardWidgets/CardStepper.h"
#include "UI/Graph/ModuleComponent/DrumKitPads/DrumPadControls.h"
#include "UI/Graph/ModuleComponent/DrumKitPads/DrumPadGrid.h"
#include "UI/Graph/ModuleComponent/SamplerChrome/InstrumentChrome.h"

namespace synth::ui {

class DrumKitPadsSection {
public:
    static constexpr int kGridGap = 6;
    static constexpr int kCaptionGap = 4;
    static constexpr int kCaptionHeight = 16;
    static constexpr int kControlsGap = 8;
    static constexpr int kSectionGap = 8;
    static constexpr int kHintMs = 1800;

    /** The height the area takes for `kit`'s pad count; what layout() returns minus its `y`. */
    static int height(const DrumKitModule& kit);
    /** How many pads beyond the count hold a sound or a setting of their own. */
    static int hiddenPadsWithSettings(const DrumKitModule& kit);

    /** Adds its components to `host.card`; the kit must outlive this. */
    DrumKitPadsSection(const InstrumentChrome::Host& host, DrumKitModule& kit);

    /** Places the area from `y` at `x`, `w` wide, and returns the y below it. */
    int layout(int y, int x, int w, bool apply);
    /** Follows the kit (undo, a project load, the audio thread's last pad) and ends the pick hint. */
    void poll();

    /** Sets the pad count as one undo step and resizes the card. */
    void setPadCountUndoably(int count);

    CardStepper& stepper() noexcept { return stepper_; }
    DrumPadGrid& grid() noexcept { return grid_; }
    DrumPadControls& controls() noexcept { return controls_; }
    juce::Label& caption() noexcept { return caption_; }
    /** The last thing said to a screen reader ("Snare picked"). */
    const juce::String& lastAnnouncement() const noexcept { return lastAnnouncement_; }

private:
    class CountStepper : public CardStepper {
    public:
        using CardStepper::CardStepper;
        bool keyPressed(const juce::KeyPress& key) override;
    };

    void picked(int index, bool played);
    void refreshFromKit();
    void refreshStepper();
    void refreshCaption();

    InstrumentChrome::Host host_;
    DrumKitModule& kit_;
    juce::Label stepperLabel_{"Pads label", "Pads"};
    CountStepper stepper_{"Pads"};
    DrumPadGrid grid_;
    juce::Label caption_{"Pads caption", juce::String()};
    DrumPadControls controls_;
    synth::drums::PadTable shownTable_;
    bool shownRecordedKit_ = false;
    int shownGeneration_ = -1;
    juce::String hint_;
    juce::uint32 hintUntil_ = 0;
    juce::String lastAnnouncement_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DrumKitPadsSection)
};

} // namespace synth::ui
