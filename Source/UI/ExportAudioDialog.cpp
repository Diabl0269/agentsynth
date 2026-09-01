#include "ExportAudioDialog.h"

namespace synth::ui {

namespace {
constexpr double kSampleRates[] = {44100.0, 48000.0, 96000.0};
constexpr int kBitDepths[] = {16, 24, 32};
} // namespace

ExportAudioDialog::ExportAudioDialog(double arrangementEndBeat, bool hasLoopRange, double loopStartBeat,
                                     double loopEndBeat, const juce::File& initialDestinationDirectory)
    : arrangementEndBeat_(arrangementEndBeat)
    , hasLoopRange_(hasLoopRange)
    , loopStartBeat_(loopStartBeat)
    , loopEndBeat_(loopEndBeat)
    , destination_(initialDestinationDirectory.getChildFile("Bounce.wav")) {
    addAndMakeVisible(optionsPage_);
    optionsPage_.addAndMakeVisible(formatLabel_);
    optionsPage_.addAndMakeVisible(formatBox_);
    optionsPage_.addAndMakeVisible(sampleRateLabel_);
    optionsPage_.addAndMakeVisible(sampleRateBox_);
    optionsPage_.addAndMakeVisible(bitDepthLabel_);
    optionsPage_.addAndMakeVisible(bitDepthBox_);
    optionsPage_.addAndMakeVisible(tailLabel_);
    optionsPage_.addAndMakeVisible(tailSecondsSlider_);
    optionsPage_.addAndMakeVisible(rangeLabel_);
    optionsPage_.addAndMakeVisible(wholeArrangementButton_);
    optionsPage_.addAndMakeVisible(selectionButton_);
    optionsPage_.addAndMakeVisible(destinationLabel_);
    optionsPage_.addAndMakeVisible(chooseDestinationButton_);
    optionsPage_.addAndMakeVisible(exportButton_);
    optionsPage_.addAndMakeVisible(cancelButton_);

    formatBox_.addItem("WAV", 1);
    formatBox_.addItem("AIFF", 2);
    formatBox_.setSelectedId(1, juce::dontSendNotification);
    formatBox_.onChange = [this] {
        updateBitDepthChoicesForFormat();
        // Keep the destination's extension in step with the chosen format so the file that lands
        // on disk matches what the format picker says, without the user having to retype it.
        const bool isAiff = formatBox_.getSelectedId() == 2;
        destination_ = destination_.withFileExtension(isAiff ? "aiff" : "wav");
        destinationLabel_.setText(destination_.getFullPathName(), juce::dontSendNotification);
    };

    sampleRateBox_.addItem("44100 Hz", 1);
    sampleRateBox_.addItem("48000 Hz", 2);
    sampleRateBox_.addItem("96000 Hz", 3);
    sampleRateBox_.setSelectedId(2, juce::dontSendNotification); // 48 kHz default

    updateBitDepthChoicesForFormat(); // populates bitDepthBox_ for WAV, selects 24-bit

    tailSecondsSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
    tailSecondsSlider_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 20);
    tailSecondsSlider_.setRange(0.0, 30.0, 0.1);
    tailSecondsSlider_.setValue(0.0, juce::dontSendNotification);

    // A group id ties these together as mutually exclusive radio buttons - JUCE handles the
    // exclusivity, nothing here has to.
    wholeArrangementButton_.setRadioGroupId(1);
    selectionButton_.setRadioGroupId(1);
    wholeArrangementButton_.setEnabled(arrangementEndBeat_ > 0.0);
    selectionButton_.setEnabled(hasLoopRange_);
    wholeArrangementButton_.onClick = [this] { updateExportButtonEnablement(); };
    selectionButton_.onClick = [this] { updateExportButtonEnablement(); };
    if (arrangementEndBeat_ > 0.0)
        wholeArrangementButton_.setToggleState(true, juce::dontSendNotification);
    else if (hasLoopRange_)
        selectionButton_.setToggleState(true, juce::dontSendNotification);

    destinationLabel_.setText(destination_.getFullPathName(), juce::dontSendNotification);
    chooseDestinationButton_.onClick = [this] { chooseDestination(); };

    exportButton_.onClick = [this] {
        if (onExport)
            onExport(getOptionsForTest(), destination_);
    };
    updateExportButtonEnablement();
    cancelButton_.onClick = [this] {
        if (onRequestClose)
            onRequestClose();
    };

    addAndMakeVisible(progressPage_);
    progressPage_.setVisible(false);
    progressPage_.addAndMakeVisible(progressStatusLabel_);
    progressPage_.addAndMakeVisible(progressBar_);
    progressPage_.addAndMakeVisible(progressCancelButton_);
    progressCancelButton_.onClick = [this] {
        if (onCancelRender)
            onCancelRender();
    };

    setSize(420, 300);
}

ExportAudioDialog::~ExportAudioDialog() = default;

void ExportAudioDialog::updateExportButtonEnablement() {
    exportButton_.setEnabled(wholeArrangementButton_.isEnabled() || selectionButton_.isEnabled());
}

void ExportAudioDialog::updateBitDepthChoicesForFormat() {
    const bool isAiff = formatBox_.getSelectedId() == 2;
    const int previousId = bitDepthBox_.getSelectedId() == 0 ? 2 : bitDepthBox_.getSelectedId();
    bitDepthBox_.clear(juce::dontSendNotification);
    bitDepthBox_.addItem("16-bit", 1);
    bitDepthBox_.addItem("24-bit", 2);
    // AIFF has no 32-bit float variant (see BounceOptions::format) - offering it here would only
    // let a user pick a combination validate() rejects at Export time.
    if (!isAiff)
        bitDepthBox_.addItem("32-bit float", 3);
    bitDepthBox_.setSelectedId(isAiff && previousId == 3 ? 2 : previousId, juce::dontSendNotification);
}

void ExportAudioDialog::chooseDestination() {
    const bool isAiff = formatBox_.getSelectedId() == 2;
    fileChooser_ = std::make_unique<juce::FileChooser>("Export Audio", destination_, isAiff ? "*.aiff" : "*.wav");
    const auto flags = juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles;
    fileChooser_->launchAsync(flags, [this](const juce::FileChooser& fc) {
        const auto file = fc.getResult();
        if (file != juce::File()) {
            destination_ = file;
            destinationLabel_.setText(destination_.getFullPathName(), juce::dontSendNotification);
        }
    });
}

void ExportAudioDialog::currentRange(double& startBeat, double& endBeat) const {
    if (selectionButton_.getToggleState() && hasLoopRange_) {
        startBeat = loopStartBeat_;
        endBeat = loopEndBeat_;
        return;
    }
    startBeat = 0.0;
    endBeat = arrangementEndBeat_;
}

BounceOptions ExportAudioDialog::getOptionsForTest() const {
    BounceOptions options;
    options.format = formatBox_.getSelectedId() == 2 ? BounceFormat::Aiff : BounceFormat::Wav;
    options.sampleRate = kSampleRates[juce::jlimit(1, 3, sampleRateBox_.getSelectedId()) - 1];
    options.bitDepth = kBitDepths[juce::jlimit(1, 3, bitDepthBox_.getSelectedId()) - 1];
    options.tailSeconds = tailSecondsSlider_.getValue();
    options.blockSize = 512;
    options.numChannels = 2;
    currentRange(options.startBeat, options.endBeat);
    return options;
}

void ExportAudioDialog::setFormatForTest(BounceFormat format) {
    formatBox_.setSelectedId(format == BounceFormat::Aiff ? 2 : 1, juce::sendNotificationSync);
}

void ExportAudioDialog::setBitDepthForTest(int bitDepth) {
    const int id = bitDepth == 16 ? 1 : bitDepth == 32 ? 3 : 2;
    bitDepthBox_.setSelectedId(id, juce::dontSendNotification);
}

void ExportAudioDialog::setSampleRateForTest(double sampleRate) {
    const int id = sampleRate <= 44100.0 ? 1 : sampleRate >= 96000.0 ? 3 : 2;
    sampleRateBox_.setSelectedId(id, juce::dontSendNotification);
}

void ExportAudioDialog::setUseSelectionForTest(bool useSelection) {
    if (useSelection)
        selectionButton_.setToggleState(true, juce::sendNotificationSync);
    else
        wholeArrangementButton_.setToggleState(true, juce::sendNotificationSync);
}

void ExportAudioDialog::setDestinationForTest(const juce::File& file) {
    destination_ = file;
    destinationLabel_.setText(destination_.getFullPathName(), juce::dontSendNotification);
}

void ExportAudioDialog::triggerExportForTest() {
    if (exportButton_.onClick)
        exportButton_.onClick();
}

void ExportAudioDialog::showProgressPage() {
    optionsPage_.setVisible(false);
    progressPage_.setVisible(true);
    progressStatusLabel_.setText("Bouncing...", juce::dontSendNotification);
    progressValue_ = 0.0;
    progressCancelButton_.setButtonText("Cancel");
    progressCancelButton_.onClick = [this] {
        if (onCancelRender)
            onCancelRender();
    };
}

void ExportAudioDialog::reportProgress(double fraction) { progressValue_ = fraction; }

void ExportAudioDialog::reportComplete(const BounceResult& result) {
    progressValue_ = 1.0;
    progressStatusLabel_.setText(result.message, juce::dontSendNotification);
    progressCancelButton_.setButtonText("Close");
    progressCancelButton_.onClick = [this] {
        if (onRequestClose)
            onRequestClose();
    };
}

void ExportAudioDialog::resized() {
    optionsPage_.setBounds(getLocalBounds());
    progressPage_.setBounds(getLocalBounds());

    {
        auto area = optionsPage_.getLocalBounds().reduced(12);
        auto row = [&](juce::Component& label, juce::Component& control) {
            auto r = area.removeFromTop(28);
            label.setBounds(r.removeFromLeft(110));
            control.setBounds(r);
            area.removeFromTop(6);
        };
        row(formatLabel_, formatBox_);
        row(sampleRateLabel_, sampleRateBox_);
        row(bitDepthLabel_, bitDepthBox_);
        row(tailLabel_, tailSecondsSlider_);

        area.removeFromTop(6);
        rangeLabel_.setBounds(area.removeFromTop(20));
        wholeArrangementButton_.setBounds(area.removeFromTop(24));
        selectionButton_.setBounds(area.removeFromTop(24));

        area.removeFromTop(10);
        auto destRow = area.removeFromTop(24);
        chooseDestinationButton_.setBounds(destRow.removeFromRight(90));
        destRow.removeFromRight(6);
        destinationLabel_.setBounds(destRow);

        auto buttonRow = area.removeFromBottom(28);
        exportButton_.setBounds(buttonRow.removeFromRight(90));
        buttonRow.removeFromRight(8);
        cancelButton_.setBounds(buttonRow.removeFromRight(90));
    }
    {
        auto area = progressPage_.getLocalBounds().reduced(12);
        progressStatusLabel_.setBounds(area.removeFromTop(40));
        progressBar_.setBounds(area.removeFromTop(24));
        progressCancelButton_.setBounds(area.removeFromBottom(28).removeFromRight(90));
    }
}

} // namespace synth::ui
