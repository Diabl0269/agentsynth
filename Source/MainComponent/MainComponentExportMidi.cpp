// MainComponentExportMidi.cpp — the "Export MIDI..." menu item: asks whole arrangement vs loop range
// (only when a loop range exists), then writes a Standard MIDI File via MidiClipFile::exportArrangement.
#include "AudioEngine/AudioEngine.h"
#include "MainComponent.h"
#include "MainComponentInternal.h"
#include "Timeline/MidiClipFile.h"
#include <memory>
#include <utility>

namespace {

// What the range prompt decided, carried into the (possibly later) file chooser.
struct MidiExportRequest {
    synth::MidiClipFile::ArrangementExportOptions options;
    juce::File suggested;
};

} // namespace

// The export reads the document, not the offline render path, so it never takes isBounceInProgress_.
// The loop range is offered whenever the loop locators describe a non-degenerate span, independent
// of whether looping is armed (same rule as promptExportAudio); ppq positions are beats here.
void MainComponent::promptExportMidi() {
    const auto position = audioEngine.getTransport().getPositionSnapshot();

    auto request = std::make_shared<MidiExportRequest>();
    request->options.bpm = position.bpm;
    request->options.timeSigNumerator = position.timeSigNumerator;
    request->options.timeSigDenominator = position.timeSigDenominator;
    request->suggested = detail::resolveExportSubdirectory(currentBundleDir_, detail::kExportsFolderName)
                             .getChildFile(currentPatchName_ + ".mid");

    juce::Component::SafePointer<MainComponent> safeThis(this);
    auto chooseFile = [safeThis, request] {
        auto* self = safeThis.getComponent();
        if (self == nullptr)
            return;
        auto onFile = [safeThis, request](const juce::File& picked) {
            auto* target = safeThis.getComponent();
            if (target == nullptr || picked == juce::File{})
                return;
            const auto file = picked.hasFileExtension("mid;midi") ? picked : picked.withFileExtension("mid");
            const bool ok = synth::MidiClipFile::exportArrangementToFile(target->timelineDoc, request->options, file);
            target->statusBar.showMessage(ok ? "Exported MIDI to " + file.getFileName()
                                             : "Could not export MIDI to " + file.getFileName());
        };
        if (self->midiExportSeams.filePrompt) {
            self->midiExportSeams.filePrompt(onFile);
            return;
        }
        self->fileChooser = std::make_unique<juce::FileChooser>("Export MIDI", request->suggested, "*.mid");
        auto flags = juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles |
                     juce::FileBrowserComponent::warnAboutOverwriting;
        self->fileChooser->launchAsync(flags, [onFile](const juce::FileChooser& fc) { onFile(fc.getResult()); });
    };

    const bool hasLoopRange = position.loopEndPpq > position.loopStartPpq;
    if (!hasLoopRange) {
        chooseFile();
        return;
    }

    const auto loop = std::make_pair(position.loopStartPpq, position.loopEndPpq);
    auto onRange = [request, loop, chooseFile](synth::MidiExportRange choice) {
        if (choice == synth::MidiExportRange::Cancel)
            return;
        if (choice == synth::MidiExportRange::LoopRange)
            request->options.rangeBeats = loop;
        chooseFile();
    };

    if (midiExportSeams.rangePrompt) {
        midiExportSeams.rangePrompt(onRange);
        return;
    }

    auto options = juce::MessageBoxOptions()
                       .withIconType(juce::MessageBoxIconType::QuestionIcon)
                       .withTitle("Export MIDI")
                       .withMessage("Export the whole arrangement, or only the current loop range?")
                       .withButton("Whole arrangement")
                       .withButton("Loop range")
                       .withButton("Cancel");
    // Async like every other prompt here; the first button returns 1, the second 2, and Cancel or a
    // dismissed window return 0, which lands on the non-exporting arm.
    juce::AlertWindow::showAsync(options, [safeThis, onRange](int result) {
        if (safeThis.getComponent() == nullptr)
            return;
        onRange(result == 1   ? synth::MidiExportRange::WholeArrangement
                : result == 2 ? synth::MidiExportRange::LoopRange
                              : synth::MidiExportRange::Cancel);
    });
}
