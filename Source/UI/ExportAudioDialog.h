#pragma once

#include "../Transport/BounceExporter.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

namespace synth::ui {

// A juce::Slider whose text box accepts a value past the drag range instead of silently clamping
// it away: typing a tail longer than the slider currently spans grows the range to fit. Dragging
// still tops out at whatever the last-grown maximum is - only the text box can extend it further.
class ExpandingRangeSlider : public juce::Slider {
public:
    double getValueFromText(const juce::String& text) override {
        const double typed = text.retainCharacters("0123456789.-").getDoubleValue();
        if (typed > getMaximum())
            setRange(getMinimum(), typed, getInterval());
        return typed;
    }
};

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
    // Arrangement, since there is no length to bounce. hasLoopRange: a non-degenerate loop region
    // exists (loopEndBeat > loopStartBeat), independent of whether looping is currently armed - the
    // caller (MainComponent::promptExportAudio, P8-17) decides this from the loop LOCATORS alone, so
    // "Current loop range" is offered as a bounce range whether or not the loop is live. Anything else
    // (a collapsed region) disables Selection rather than passing an empty range through to fail
    // BounceOptions validation. If both are disabled the Export button stays disabled too (there is
    // deliberately no third "type a duration" fallback - see
    // docs/architecture.md's export section). bpm: the transport's current tempo, used only to
    // convert the tail control between seconds and bars (4/4 - this app has no time-signature
    // concept anywhere else either, see TimelineDoc). projectIsSaved: true when the caller has a
    // real bundle open - false shows a one-line tip recommending a save first (not enforced, see
    // MainComponent::promptExportAudio). initialDestinationDirectory/initialFileNameBase: the
    // folder and base file name (no extension) Export starts pre-filled with; the caller resolves
    // both (bundle-relative Exports/ folder + the project's name) so this class stays free of
    // ProjectBundle/currentPatchName_ knowledge.
    ExportAudioDialog(double arrangementEndBeat, bool hasLoopRange, double loopStartBeat, double loopEndBeat,
                      double bpm, bool projectIsSaved, const juce::File& initialDestinationDirectory,
                      const juce::String& initialFileNameBase);
    ~ExportAudioDialog() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // Fires once, when Export is pressed with a usable range. The caller is expected to call
    // showProgressPage() (directly or via reportProgress/reportComplete) once the render starts.
    std::function<void(BounceOptions options, juce::File destination)> onExport;
    // Fires when Cancel is pressed on the OPTIONS page, or Close is pressed after a render finishes
    // - either way, the caller should close the window.
    std::function<void()> onRequestClose;
    // Fires when Cancel is pressed on the PROGRESS page, i.e. a render is already running.
    std::function<void()> onCancelRender;

    // Test/automation seam for the destination-exists collision prompt (beginExport) - same idiom
    // MainComponent's unsavedChangesPrompt uses. When set, beginExport() calls this instead of a
    // real juce::AlertWindow::showAsync, so a test can answer synchronously with no message loop.
    // Result codes match AlertWindow's own 3-button convention: 1 = Overwrite, 2 = Save as Copy,
    // 0 = Cancel.
    std::function<void(std::function<void(int)> onChoice)> collisionPromptForTest;

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
    // Bars-unit test seams: switching units first (so the slider's range/value are already in the
    // target unit, exactly like a real click on tailUnitBox_) then setting the value.
    void setTailUnitBarsForTest(bool bars);
    void setTailValueForTest(double value);

private:
    void updateBitDepthChoicesForFormat();
    void chooseDestinationFolder();
    void updateFileNameFromEditor();
    void currentRange(double& startBeat, double& endBeat) const;
    void updateExportButtonEnablement();
    void updateTailReadoutForUnit();
    void beginExport();
    // Appends " 2", " 3", ... before the extension until the result doesn't already exist on disk -
    // "Take 2.wav" style, never "Take 2 2.wav" (strips a trailing " <digits>" from the base first).
    static juce::File uniquifyExistingFile(const juce::File& file);

    double arrangementEndBeat_;
    bool hasLoopRange_;
    double loopStartBeat_;
    double loopEndBeat_;
    double bpm_;
    bool wasBarsUnit_ = false; // updateTailReadoutForUnit's own memory of tailSlider_'s current unit
    juce::File destination_;   // directory + file name + extension, kept in sync by the two rows below

    // ---- Options page ----
    juce::Component optionsPage_;
    juce::Label formatLabel_{"formatLabel", "Format"};
    juce::ComboBox formatBox_;
    juce::Label sampleRateLabel_{"sampleRateLabel", "Sample rate"};
    juce::ComboBox sampleRateBox_;
    juce::Label bitDepthLabel_{"bitDepthLabel", "Bit depth"};
    juce::ComboBox bitDepthBox_;
    juce::Label tailLabel_{"tailLabel", "Tail"};
    ExpandingRangeSlider tailSlider_;
    juce::ComboBox tailUnitBox_; // Seconds / Bars - the slider/text range converts, tailSeconds is what ships
    juce::Label rangeLabel_{"rangeLabel", "Range"};
    juce::ToggleButton wholeArrangementButton_{"Whole arrangement"};
    juce::ToggleButton selectionButton_{"Current loop range"};
    juce::Label fileNameLabel_{"fileNameLabel", "File name"};
    juce::TextEditor fileNameEditor_;
    juce::Label destinationLabel_{"destinationLabel", juce::String()};
    juce::TextButton chooseDestinationButton_{"Choose folder..."};
    juce::Label unsavedProjectTipLabel_{"unsavedProjectTipLabel", juce::String()};
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
