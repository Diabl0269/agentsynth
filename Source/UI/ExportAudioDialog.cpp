#include "ExportAudioDialog.h"

namespace synth::ui {

namespace {
constexpr double kSampleRates[] = {44100.0, 48000.0, 96000.0};
constexpr int kBitDepths[] = {16, 24, 32};
// This app has no time-signature concept anywhere else (see TimelineDoc) - "a bar" for the tail
// control's Bars unit is always 4 beats, the same assumption the metronome's downbeat detection
// falls back to.
constexpr double kBeatsPerBar = 4.0;
} // namespace

ExportAudioDialog::ExportAudioDialog(double arrangementEndBeat, bool hasLoopRange, double loopStartBeat,
                                     double loopEndBeat, double bpm, bool projectIsSaved,
                                     const juce::File& initialDestinationDirectory,
                                     const juce::String& initialFileNameBase)
    : arrangementEndBeat_(arrangementEndBeat)
    , hasLoopRange_(hasLoopRange)
    , loopStartBeat_(loopStartBeat)
    , loopEndBeat_(loopEndBeat)
    , bpm_(bpm > 0.0 ? bpm : 120.0)
    , destination_(initialDestinationDirectory.getChildFile(initialFileNameBase + ".wav")) {
    addAndMakeVisible(optionsPage_);
    optionsPage_.addAndMakeVisible(formatLabel_);
    optionsPage_.addAndMakeVisible(formatBox_);
    optionsPage_.addAndMakeVisible(sampleRateLabel_);
    optionsPage_.addAndMakeVisible(sampleRateBox_);
    optionsPage_.addAndMakeVisible(bitDepthLabel_);
    optionsPage_.addAndMakeVisible(bitDepthBox_);
    optionsPage_.addAndMakeVisible(tailLabel_);
    optionsPage_.addAndMakeVisible(tailSlider_);
    optionsPage_.addAndMakeVisible(tailUnitBox_);
    optionsPage_.addAndMakeVisible(rangeLabel_);
    optionsPage_.addAndMakeVisible(wholeArrangementButton_);
    optionsPage_.addAndMakeVisible(selectionButton_);
    optionsPage_.addAndMakeVisible(fileNameLabel_);
    optionsPage_.addAndMakeVisible(fileNameEditor_);
    optionsPage_.addAndMakeVisible(destinationLabel_);
    optionsPage_.addAndMakeVisible(chooseDestinationButton_);
    optionsPage_.addAndMakeVisible(unsavedProjectTipLabel_);
    unsavedProjectTipLabel_.setVisible(!projectIsSaved);
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
    };

    sampleRateBox_.addItem("44100 Hz", 1);
    sampleRateBox_.addItem("48000 Hz", 2);
    sampleRateBox_.addItem("96000 Hz", 3);
    sampleRateBox_.setSelectedId(2, juce::dontSendNotification); // 48 kHz default

    updateBitDepthChoicesForFormat(); // populates bitDepthBox_ for WAV, selects 24-bit

    tailSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
    tailSlider_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 20);
    tailSlider_.setRange(0.0, 30.0, 0.1);
    tailSlider_.setValue(0.0, juce::dontSendNotification);
    tailLabel_.setTooltip("Extra render time after the range ends, so reverb/delay tails ring out. "
                          "Type a value past the slider's end to extend it.");

    tailUnitBox_.addItem("Seconds", 1);
    tailUnitBox_.addItem("Bars", 2);
    tailUnitBox_.setSelectedId(1, juce::dontSendNotification);
    tailUnitBox_.onChange = [this] { updateTailReadoutForUnit(); };

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

    fileNameEditor_.setText(initialFileNameBase, juce::dontSendNotification);
    fileNameEditor_.onTextChange = [this] { updateFileNameFromEditor(); };
    destinationLabel_.setText(destination_.getParentDirectory().getFullPathName(), juce::dontSendNotification);
    chooseDestinationButton_.onClick = [this] { chooseDestinationFolder(); };

    if (!projectIsSaved) {
        unsavedProjectTipLabel_.setText("Tip: save the project first to export straight into its own folder.",
                                        juce::dontSendNotification);
        unsavedProjectTipLabel_.setFont(unsavedProjectTipLabel_.getFont().withHeight(13.0f));
        unsavedProjectTipLabel_.setColour(juce::Label::textColourId,
                                          findColour(juce::Label::textColourId).withAlpha(0.6f));
    }

    exportButton_.onClick = [this] { beginExport(); };
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

    setSize(440, 380);
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

void ExportAudioDialog::chooseDestinationFolder() {
    // Directory-only: the file NAME is the fileNameEditor_ row's job now (item 3) - mixing a
    // full-file save chooser back in here would let the two rows disagree about what the export
    // is actually called.
    fileChooser_ = std::make_unique<juce::FileChooser>("Choose export folder", destination_.getParentDirectory());
    const auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories;
    fileChooser_->launchAsync(flags, [this](const juce::FileChooser& fc) {
        const auto dir = fc.getResult();
        if (dir != juce::File()) {
            destination_ = dir.getChildFile(destination_.getFileName());
            destinationLabel_.setText(dir.getFullPathName(), juce::dontSendNotification);
        }
    });
}

void ExportAudioDialog::updateFileNameFromEditor() {
    // A bare base name, sanitised for the characters a filename can't hold - the extension always
    // comes from the format picker, never from what the user types here.
    auto base = fileNameEditor_.getText().trim();
    if (base.isEmpty())
        base = "Untitled";
    destination_ = destination_.getParentDirectory().getChildFile(juce::File::createLegalFileName(base) + "." +
                                                                  destination_.getFileExtension().substring(1));
}

void ExportAudioDialog::updateTailReadoutForUnit() {
    const double secondsPerBar = (60.0 / bpm_) * kBeatsPerBar;
    const bool toBars = tailUnitBox_.getSelectedId() == 2;

    // The slider always holds a value in whichever unit was selected BEFORE this call - convert
    // it to seconds first (the one currency getOptionsForTest ships), then into the new unit. This
    // is what keeps "30 seconds -> switch to Bars -> switch back to Seconds" a no-op.
    const double currentSeconds = wasBarsUnit_ ? tailSlider_.getValue() * secondsPerBar : tailSlider_.getValue();
    const double newValue = toBars ? currentSeconds / secondsPerBar : currentSeconds;
    const double defaultMax = toBars ? 30.0 / secondsPerBar : 30.0;

    tailSlider_.setRange(0.0, juce::jmax(defaultMax, newValue), toBars ? 0.01 : 0.1);
    tailSlider_.setValue(newValue, juce::dontSendNotification);
    wasBarsUnit_ = toBars;
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
    // BounceOptions only ever speaks seconds - Bars is purely a display convenience for the slider.
    options.tailSeconds = wasBarsUnit_ ? tailSlider_.getValue() * (60.0 / bpm_) * kBeatsPerBar : tailSlider_.getValue();
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
    destinationLabel_.setText(destination_.getParentDirectory().getFullPathName(), juce::dontSendNotification);
    fileNameEditor_.setText(destination_.getFileNameWithoutExtension(), juce::dontSendNotification);
}

void ExportAudioDialog::triggerExportForTest() {
    if (exportButton_.onClick)
        exportButton_.onClick();
}

void ExportAudioDialog::setTailUnitBarsForTest(bool bars) {
    tailUnitBox_.setSelectedId(bars ? 2 : 1, juce::sendNotificationSync);
}

void ExportAudioDialog::setTailValueForTest(double value) { tailSlider_.setValue(value, juce::dontSendNotification); }

juce::File ExportAudioDialog::uniquifyExistingFile(const juce::File& file) {
    // Strip a trailing " <digits>" from the base name first, so a second collision produces
    // "Take 3.wav", never "Take 2 2.wav".
    const auto ext = file.getFileExtension();
    auto base = file.getFileNameWithoutExtension();
    const auto digitsStart = base.trimCharactersAtEnd("0123456789");
    if (digitsStart != base && digitsStart.endsWithChar(' '))
        base = digitsStart.dropLastCharacters(1);

    juce::File candidate = file;
    for (int n = 2; candidate.exists(); ++n)
        candidate = file.getSiblingFile(base + " " + juce::String(n) + ext);
    return candidate;
}

void ExportAudioDialog::beginExport() {
    if (!destination_.existsAsFile()) {
        if (onExport)
            onExport(getOptionsForTest(), destination_);
        return;
    }

    // Three-way, not the OS's own overwrite-or-cancel: Save as Copy is the one arm neither the
    // format's own save panel nor a plain overwrite prompt offers. Button order is load-bearing -
    // LookAndFeel_V2::createAlertWindow maps a 3-button MessageBoxOptions as
    // button1->1, button2->2, button3(Escape)->0 - collisionPromptForTest uses the same codes.
    juce::Component::SafePointer<ExportAudioDialog> safeThis(this);
    auto onChoice = [safeThis](int result) {
        auto* self = safeThis.getComponent();
        if (self == nullptr || result == 0)
            return;
        const auto target = result == 2 ? uniquifyExistingFile(self->destination_) : self->destination_;
        if (self->onExport)
            self->onExport(self->getOptionsForTest(), target);
    };

    if (collisionPromptForTest) {
        collisionPromptForTest(onChoice);
        return;
    }

    auto options = juce::MessageBoxOptions()
                       .withIconType(juce::MessageBoxIconType::WarningIcon)
                       .withTitle("File Already Exists")
                       .withMessage(destination_.getFileName() + " already exists.")
                       .withButton("Overwrite")
                       .withButton("Save as Copy")
                       .withButton("Cancel")
                       .withAssociatedComponent(this);
    juce::AlertWindow::showAsync(options, onChoice);
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

void ExportAudioDialog::paint(juce::Graphics& g) { g.fillAll(findColour(juce::ResizableWindow::backgroundColourId)); }

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

        auto tailRow = area.removeFromTop(28);
        tailLabel_.setBounds(tailRow.removeFromLeft(110));
        tailUnitBox_.setBounds(tailRow.removeFromRight(80));
        tailRow.removeFromRight(6);
        tailSlider_.setBounds(tailRow);
        area.removeFromTop(6);

        area.removeFromTop(6);
        rangeLabel_.setBounds(area.removeFromTop(20));
        wholeArrangementButton_.setBounds(area.removeFromTop(24));
        selectionButton_.setBounds(area.removeFromTop(24));

        area.removeFromTop(10);
        row(fileNameLabel_, fileNameEditor_);

        auto destRow = area.removeFromTop(24);
        chooseDestinationButton_.setBounds(destRow.removeFromRight(110));
        destRow.removeFromRight(6);
        destinationLabel_.setBounds(destRow);

        if (unsavedProjectTipLabel_.isVisible()) {
            area.removeFromTop(6);
            unsavedProjectTipLabel_.setBounds(area.removeFromTop(18));
        }

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
