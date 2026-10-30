#pragma once

// The Drum Kit card's pads area, under the master Level: the "Pads" count stepper (4 to 32 in fours), the pad grid,
// a caption line, and the picked pad's controls. docs/modules/drum-kit.md#the-card.

#include "Modules/DrumKit/DrumKitModule.h"
#include "UI/Graph/CardWidgets/CardStepper.h"
#include "UI/Graph/ModuleComponent/DrumKitPads/DrumPadControls.h"
#include "UI/Graph/ModuleComponent/DrumKitPads/DrumPadGrid.h"
#include "UI/Graph/ModuleComponent/DrumKitPads/DrumPadSoundMenu.h"
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

    // ---- A pad's sound (DrumKitPadsSectionSound.cpp); each edit is one undo step ----
    /** Opens pad `index`'s Sound menu (from the Sound row) or its pad menu (right-click, Shift+F10). */
    void openMenu(int index, bool padMenu, bool keyboard);
    /** Does what item `result` of the last menu built for `index` says. */
    void applyMenuResult(int index, int result);
    void applyChoice(int index, const DrumPadSoundMenu::Choice& choice);
    /** Pad `index` plays `file`; false (no undo step) when it cannot be read. */
    bool setPadFileUndoably(int index, const juce::File& file);
    void setInstrumentPitchUndoably(int index, int pitch);
    /** Shows the module instrument pad `index` plays on the canvas. */
    void showInstrument(int index);
    /** What pad `index` plays, as {name, detail}: "Pluck 1", "instrument"; a removed module says so. */
    std::pair<juce::String, juce::String> describeSound(int index) const;
    DrumPadSoundMenu& soundMenu() noexcept { return soundMenu_; }
    /** Set by tests: called with the menu instead of showing it. */
    std::function<void(juce::PopupMenu& menu, int index, bool padMenu)> showMenuForTest;

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
    bool editPadUndoably(const std::function<bool()>& change);
    void relinkInstruments();
    void wireSoundControls();
    juce::AudioProcessorGraph::Node* nodeForUuid(const juce::String& uuid) const;
    void chooseFileFor(int index);
    juce::String dropHint() const;

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
    DrumPadSoundMenu soundMenu_;
    std::unique_ptr<juce::FileChooser> chooser_;
    std::uint32_t shownLinks_ = 0;
    bool shownOutline_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DrumKitPadsSection)
};

} // namespace synth::ui
