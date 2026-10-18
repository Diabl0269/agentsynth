#pragma once

// The Sampler card's chrome above the body: in Sample and Granular mode the waveform and the Load Sample row; in
// Instrument mode the instrument picker, a caption and the key strip. Both sets sit in the same area and cross-fade
// when the mode changes, so the card never changes height. Also the card's file drop, which turns several dropped
// recordings (or an .sfz) into an instrument.

#include "Modules/SamplerModule.h"
#include "UI/Graph/ModuleComponent/SamplerChrome/SamplerInstrumentPicker.h"
#include "UI/Graph/ModuleComponent/SamplerChrome/SamplerKeyStrip.h"
#include "UI/Layout/FadeVisibility.h"
#include "UI/ModuleViews/SampleWaveformComponent.h"
#include <functional>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

class AppUndoManager;

namespace synth::ui {

class ModMatrixPicker;
struct InstrumentChoice;

class SamplerChrome {
public:
    struct Host {
        juce::Component* card = nullptr; ///< Parent of every component here; outlives this.
        AppUndoManager* undo = nullptr;  ///< Null: instrument loads record no undo step.
        juce::AudioProcessorGraph* graph = nullptr;
    };

    /** The sampler must outlive this. Message thread only for every member. */
    SamplerChrome(Host host, SamplerModule& sampler);
    ~SamplerChrome();

    /** Places both sets of components from `y` (the card's content origin) and returns the y below the chrome. */
    int layout(int y, int contentX, int contentW, int narrowX, int narrowW, bool apply);
    /** The card's gated tick: follows the instrument and lights the sounding notes. */
    void poll();
    /** Re-reads the sample name and the instrument (after a project's deferred loads landed). */
    void refreshLoaded();
    void refreshSampleLabel(const juce::String& fallbackMessage = {});

    bool isInstrumentMode() const;
    /** The loaded sample's file-name label, for the card's accessibility pass. */
    juce::Label& getSampleNameLabel() noexcept { return nameLabel_; }

    // ---- File drop ----
    bool isInterestedInDrop(const juce::StringArray& files) const;
    void dragEntered(const juce::StringArray& files);
    void dragLeft();
    /** Loads an instrument when the drop holds an .sfz or two or more recordings; false leaves a single recording to
     *  the card's Sample-mode drop. */
    bool dropFiles(const juce::StringArray& files);
    /** The hint over the drop highlight while two or more recordings are dragged over the card. */
    void paintDropHint(juce::Graphics& g, juce::Rectangle<int> cardBounds) const;
    int getDragFileCount() const noexcept { return dragFileCount_; }

    // ---- Instrument loading ----
    /** Runs `load` as ONE undo step (the instrument and the mode come back together) and reports its messages. */
    bool loadUndoably(const std::function<bool(SamplerModule&)>& load);
    void openPicker();
    /** The notes on the caption line after the last load (errors and warnings); empty when it was clean. */
    juce::StringArray getLoadMessages() const { return loadMessages_; }

    // ---- Parts, for tests ----
    SamplerInstrumentPicker& getPicker() noexcept { return picker_; }
    SamplerKeyStrip& getKeyStrip() noexcept { return strip_; }
    juce::Label& getCaption() noexcept { return caption_; }
    juce::TextButton& getLoadSampleButton() noexcept { return loadButton_; }
    SampleWaveformComponent& getWaveform() noexcept { return waveform_; }
    /** When set, openPicker() hands its list here instead of opening a call-out. */
    static std::function<void(std::unique_ptr<ModMatrixPicker>)>& pickerHookForTest();

private:
    void applyMode(bool instrument, bool animate);
    void refreshInstrument();
    void reportLoad(bool ok);
    void chooseInstrument(const InstrumentChoice& choice);
    void loadFromFiles(const juce::StringArray& paths);
    void launchSampleChooser();
    void launchSfzChooser();
    void launchRecordingsChooser();
    juce::String currentPackKey() const;

    Host host_;
    SamplerModule& sampler_;
    SampleWaveformComponent waveform_;
    juce::TextButton loadButton_{"Load Sample..."};
    juce::Label nameLabel_{"Sample", juce::String()};
    SamplerInstrumentPicker picker_;
    juce::Label caption_{"Instrument caption", juce::String()};
    SamplerKeyStrip strip_;
    FadeVisibility sampleFade_{&waveform_, &loadButton_, &nameLabel_};
    FadeVisibility instrumentFade_{&picker_, &caption_, &strip_};
    std::unique_ptr<juce::ParameterAttachment> modeAttachment_;
    std::unique_ptr<juce::FileChooser> chooser_;
    juce::StringArray loadMessages_;
    int shownGeneration_ = -1;
    int shownLoadSerial_ = -1;
    int dragFileCount_ = 0;

    JUCE_DECLARE_WEAK_REFERENCEABLE(SamplerChrome)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SamplerChrome)
};

} // namespace synth::ui
