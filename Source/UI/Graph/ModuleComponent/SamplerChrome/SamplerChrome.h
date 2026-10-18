#pragma once

// The Sampler card's chrome above the body: in Sample and Granular mode the waveform and the Load Sample row; in
// Instrument mode the instrument picker, a caption and the key strip. Both sets sit in the same area and cross-fade
// when the mode changes, so the card never changes height. Also the card's file drop, which turns several dropped
// recordings (or an .sfz) into an instrument.

#include "Modules/SamplerModule.h"
#include "UI/Graph/ModuleComponent/SamplerChrome/InstrumentChrome.h"
#include "UI/Graph/ModuleComponent/SamplerChrome/InstrumentPickerRow.h"
#include "UI/Graph/ModuleComponent/SamplerChrome/SamplerInstrumentPicker.h"
#include "UI/Graph/ModuleComponent/SamplerChrome/SamplerKeyStrip.h"
#include "UI/Graph/ModuleComponent/SamplerChrome/SamplerMapView.h"
#include "UI/Layout/FadeVisibility.h"
#include "UI/ModuleViews/SampleWaveformComponent.h"
#include <functional>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

class AppUndoManager;

namespace synth::ui {

class ModMatrixPicker;

class SamplerChrome : public InstrumentChrome {
public:
    /** The sampler must outlive this. Message thread only for every member. */
    SamplerChrome(Host host, SamplerModule& sampler);
    ~SamplerChrome() override;

    /** Places both sets of components from `y` (the card's content origin) and returns the y below the chrome. */
    int layout(int y, int contentX, int contentW, int narrowX, int narrowW, bool apply) override;
    /** The card's gated tick: follows the instrument and lights the sounding notes. */
    void poll() override;
    /** Re-reads the sample name and the instrument (after a project's deferred loads landed). */
    void refreshLoaded() override;
    void refreshSampleLabel(const juce::String& fallbackMessage = {}) override;

    bool isInstrumentMode() const;
    /** The loaded sample's file-name label, for the card's accessibility pass. */
    juce::Label& getSampleNameLabel() noexcept { return nameLabel_; }

    // ---- File drop ----
    bool isInterestedInDrop(const juce::StringArray& files) const override;
    void dragEntered(const juce::StringArray& files) override;
    void dragLeft() override;
    /** Loads an instrument when the drop holds an .sfz or two or more recordings; false leaves a single recording to
     *  the card's Sample-mode drop. */
    bool dropFiles(const juce::StringArray& files) override;
    /** The hint over the drop highlight while two or more recordings are dragged over the card. */
    void paintDropHint(juce::Graphics& g, juce::Rectangle<int> cardBounds) const override;
    int getDragFileCount() const noexcept { return dragFileCount_; }

    // ---- Instrument loading ----
    /** Runs `load` as ONE undo step (the instrument and the mode come back together) and reports its messages. */
    bool loadUndoably(const std::function<bool(synth::sampler::InstrumentHost&)>& load);
    /** Runs an edit of the instrument's regions as ONE undo step (see SamplerModule::editInstrumentRegions). */
    bool editRegionsUndoably(const std::vector<synth::sampler::RegionSpec>& specs);
    void openPicker() { row_.openPicker(); }
    /** Opens the instrument map window out of the card, or brings the open one forward. */
    void openMap();
    /** The open map's content; null while the map is closed (or when a test hook took it). */
    SamplerMapView* getMapView() const noexcept { return mapView_.getComponent(); }
    /** The notes on the caption line after the last load (errors and warnings); empty when it was clean. */
    juce::StringArray getLoadMessages() const { return row_.getLoadMessages(); }

    // ---- Parts, for tests ----
    SamplerInstrumentPicker& getPicker() noexcept { return row_.picker(); }
    SamplerKeyStrip& getKeyStrip() noexcept { return strip_; }
    juce::TextButton& getEditMapButton() noexcept { return editMapButton_; }
    juce::Label& getCaption() noexcept { return row_.caption(); }
    juce::TextButton& getLoadSampleButton() noexcept { return loadButton_; }
    SampleWaveformComponent& getWaveform() noexcept { return waveform_; }
    /** When set, openPicker() hands its list here instead of opening a call-out. */
    static std::function<void(std::unique_ptr<ModMatrixPicker>)>& pickerHookForTest() {
        return InstrumentPickerRow::pickerHookForTest();
    }

private:
    void applyMode(bool instrument, bool animate);
    void closeMap();
    void launchSampleChooser();

    Host host_;
    SamplerModule& sampler_;
    SampleWaveformComponent waveform_;
    juce::TextButton loadButton_{"Load Sample..."};
    juce::Label nameLabel_{"Sample", juce::String()};
    InstrumentPickerRow row_;
    juce::TextButton editMapButton_{"Edit map"};
    SamplerKeyStrip strip_;
    FadeVisibility sampleFade_{&waveform_, &loadButton_, &nameLabel_};
    FadeVisibility instrumentFade_{&row_.picker(), &editMapButton_, &row_.caption(), &strip_};
    juce::Component::SafePointer<juce::DialogWindow> mapWindow_;
    juce::Component::SafePointer<SamplerMapView> mapView_;
    std::unique_ptr<juce::ParameterAttachment> modeAttachment_;
    std::unique_ptr<juce::FileChooser> chooser_;
    int dragFileCount_ = 0;

    JUCE_DECLARE_WEAK_REFERENCEABLE(SamplerChrome)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SamplerChrome)
};

} // namespace synth::ui
