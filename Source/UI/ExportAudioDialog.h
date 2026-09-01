#pragma once

#include "../Transport/BounceExporter.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

namespace synth::ui {

// The "Export Audio..." dialog's content: gathers a BounceOptions + destination file, then (once
// Export is pressed) switches itself to a progress page while a bounce runs.
//
// Pure UI - it never touches AudioEngine/TimelineDoc/BounceRunner. The two numbers that come from
// the engine (the arrangement's length and the current loop range) are handed in by the caller
// (MainComponent::promptExportAudio), which is what makes this class headless-testable: a test
// constructs it with fixed numbers, drives the controls, and reads back the BounceOptions it would
// have sent, with no audio device and no AudioEngine involved at all.
class ExportAudioDialog : public juce::Component {
public:
    // arrangementEndBeat: TimelineDoc::getArrangementEndBeat() - 0.0 ("no clips") disables Whole
    // Arrangement, since there is no length to bounce. hasLoopRange: the transport is looping AND
    // loopEndBeat > loopStartBeat - anything else disables Selection rather than passing an empty
    // range through to fail BounceOptions validation. If both are disabled the Export button stays
    // disabled too (there is deliberately no third "type a duration" fallback - see
    // docs/architecture.md's export section).
    ExportAudioDialog(double arrangementEndBeat, bool hasLoopRange, double loopStartBeat, double loopEndBeat,
                      const juce::File& initialDestinationDirectory);
    ~ExportAudioDialog() override;

    void resized() override;

    // Fires once, when Export is pressed with a usable range. The caller is expected to call
    // showProgressPage() (directly or via reportProgress/reportComplete) once the render starts.
    std::function<void(BounceOptions options, juce::File destination)> onExport;
    // Fires when Cancel is pressed on the OPTIONS page, or Close is pressed after a render finishes
    // - either way, the caller should close the window.
    std::function<void()> onRequestClose;
    // Fires when Cancel is pressed on the PROGRESS page, i.e. a render is already running.
    std::function<void()> onCancelRender;

    // MESSAGE THREAD. Called by the owner to drive the progress page.
    void showProgressPage();
    void reportProgress(double fraction);
    void reportComplete(const BounceResult& result);

    // Test seams - drive the real controls (not a backdoor into private state) and read back what
    // Export would send.
    BounceOptions getOptionsForTest() const;
    juce::File getDestinationForTest() const { return destination_; }
    void setFormatForTest(BounceFormat format);
    void setBitDepthForTest(int bitDepth);
    void setSampleRateForTest(double sampleRate);
    void setUseSelectionForTest(bool useSelection);
    void setDestinationForTest(const juce::File& file);
    void triggerExportForTest();

private:
    void updateBitDepthChoicesForFormat();
    void chooseDestination();
    void currentRange(double& startBeat, double& endBeat) const;
    void updateExportButtonEnablement();

    double arrangementEndBeat_;
    bool hasLoopRange_;
    double loopStartBeat_;
    double loopEndBeat_;
    juce::File destination_;

    // ---- Options page ----
    juce::Component optionsPage_;
    juce::Label formatLabel_{"formatLabel", "Format"};
    juce::ComboBox formatBox_;
    juce::Label sampleRateLabel_{"sampleRateLabel", "Sample rate"};
    juce::ComboBox sampleRateBox_;
    juce::Label bitDepthLabel_{"bitDepthLabel", "Bit depth"};
    juce::ComboBox bitDepthBox_;
    juce::Label tailLabel_{"tailLabel", "Tail (seconds)"};
    juce::Slider tailSecondsSlider_;
    juce::Label rangeLabel_{"rangeLabel", "Range"};
    juce::ToggleButton wholeArrangementButton_{"Whole arrangement"};
    juce::ToggleButton selectionButton_{"Current loop range"};
    juce::Label destinationLabel_{"destinationLabel", juce::String()};
    juce::TextButton chooseDestinationButton_{"Choose..."};
    juce::TextButton exportButton_{"Export"};
    juce::TextButton cancelButton_{"Cancel"};
    std::unique_ptr<juce::FileChooser> fileChooser_;

    // ---- Progress page ----
    juce::Component progressPage_;
    juce::Label progressStatusLabel_{"progressStatusLabel", "Bouncing..."};
    double progressValue_ = 0.0;
    juce::ProgressBar progressBar_{progressValue_};
    juce::TextButton progressCancelButton_{"Cancel"};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ExportAudioDialog)
};

} // namespace synth::ui
