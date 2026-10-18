#pragma once

// The instrument picker and the caption line under it, shared by every card that loads an Instrument: the choosers,
// the one-step undo around a load, and the notes of the last load. The owning chrome places the two components.

#include "Modules/ModuleBase.h"
#include "Modules/Sampler/SamplerInstrumentHost.h"
#include "UI/Graph/ModuleComponent/SamplerChrome/InstrumentChrome.h"
#include "UI/Graph/ModuleComponent/SamplerChrome/SamplerInstrumentChoices.h"
#include "UI/Graph/ModuleComponent/SamplerChrome/SamplerInstrumentPicker.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

namespace synth::ui {

class ModMatrixPicker;

class InstrumentPickerRow {
public:
    static constexpr int kCaptionGap = 4;
    static constexpr int kCaptionHeight = 16;

    /** `module` and `instruments` are one object; both must outlive this. The components join `host.card` (hidden)
     *  when the owner calls the two add functions, so it sets their order among its own children. */
    InstrumentPickerRow(const InstrumentChrome::Host& host, ModuleBase& module,
                        synth::sampler::InstrumentHost& instruments, InstrumentChoiceOptions options);
    ~InstrumentPickerRow();

    void addPickerToCard();
    void addCaptionToCard();

    SamplerInstrumentPicker& picker() noexcept { return picker_; }
    juce::Label& caption() noexcept { return caption_; }

    /** Places the picker (`pickerW` wide) and the caption (`captionW`) from `y` at `x`; returns the y below the
     *  caption. */
    int layout(int y, int x, int pickerW, int captionW, bool apply);

    /** Runs `load` as ONE undo step and reports its messages. */
    bool loadUndoably(const std::function<bool(synth::sampler::InstrumentHost&)>& load);
    void openPicker();
    void loadFromFiles(const juce::StringArray& paths);
    void loadFromSfz(const juce::File& sfz);
    /** Re-reads the instrument's name and the caption now. */
    void refresh();
    /** Refreshes when the instrument changed behind the card's back (undo, a project load, a failed restore). */
    void poll();
    juce::StringArray getLoadMessages() const { return loadMessages_; }

    /** Called on every refresh; its text is the caption when the last load left no note. */
    std::function<juce::String()> defaultCaption;

    /** When set, openPicker() hands its list here instead of opening a call-out. */
    static std::function<void(std::unique_ptr<ModMatrixPicker>)>& pickerHookForTest();

private:
    void chooseInstrument(const InstrumentChoice& choice);
    void launchSfzChooser();
    void launchRecordingsChooser();
    void reportLoad(bool ok);
    juce::String currentPackKey() const;

    InstrumentChrome::Host host_;
    ModuleBase& module_;
    synth::sampler::InstrumentHost& instruments_;
    InstrumentChoiceOptions options_;
    SamplerInstrumentPicker picker_;
    juce::Label caption_{"Instrument caption", juce::String()};
    std::unique_ptr<juce::FileChooser> chooser_;
    juce::StringArray loadMessages_;
    int shownGeneration_ = -1;
    int shownLoadSerial_ = -1;

    JUCE_DECLARE_WEAK_REFERENCEABLE(InstrumentPickerRow)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(InstrumentPickerRow)
};

} // namespace synth::ui
