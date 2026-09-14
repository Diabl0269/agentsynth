// ModuleComponentAudioDrop.cpp -- the Sampler's control creation (waveform view, load button,
// file-name label) plus audio-file drag-and-drop for both the Sampler and the Wavetable
// oscillator's file targets. ModuleComponent is declared in ModuleComponent.h; the rest of its
// implementation lives in the sibling ModuleComponent*.cpp units next to this one (FRO65 split of
// the former single ModuleComponent.cpp).
#include "ModuleComponent.h"
#include "Modules/SamplerModule.h"

void ModuleComponent::createSamplerControls() {
    auto* sampler = dynamic_cast<SamplerModule*>(module);
    if (sampler == nullptr)
        return;

    sampleWaveform = std::make_unique<SampleWaveformComponent>(*sampler);
    addAndMakeVisible(*sampleWaveform);

    loadSampleButton = std::make_unique<juce::TextButton>("Load Sample...");
    loadSampleButton->setTooltip("Load an audio file (WAV, AIFF, FLAC, Ogg) into this Sampler");
    loadSampleButton->onClick = [this] {
        auto* mod = dynamic_cast<SamplerModule*>(module);
        if (mod == nullptr)
            return;

        sampleChooser = std::make_unique<juce::FileChooser>(
            "Load Sample", juce::File::getSpecialLocation(juce::File::userMusicDirectory),
            SamplerModule::getSupportedFormatWildcard());
        auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;

        // The chooser outlives this call; SafePointer keeps the callback a no-op if the module
        // component is destroyed (graph rebuild, undo) while the dialog is open.
        juce::Component::SafePointer<ModuleComponent> safeThis(this);
        sampleChooser->launchAsync(flags, [safeThis](const juce::FileChooser& fc) {
            if (safeThis == nullptr)
                return;
            auto file = fc.getResult();
            if (file == juce::File{})
                return;

            auto* target = dynamic_cast<SamplerModule*>(safeThis->getModule());
            if (target == nullptr)
                return;

            if (target->loadSampleFile(file))
                safeThis->refreshSampleLabel();
            else
                safeThis->refreshSampleLabel("Could not read " + file.getFileName());
        });
    };
    addAndMakeVisible(*loadSampleButton);

    sampleNameLabel = std::make_unique<juce::Label>("Sample", juce::String());
    sampleNameLabel->setJustificationType(juce::Justification::centredLeft);
    sampleNameLabel->setMinimumHorizontalScale(0.7f);
    addAndMakeVisible(*sampleNameLabel);

    refreshSampleLabel();
}

bool ModuleComponent::isInterestedInFileDrag(const juce::StringArray& files) {
    // Only a Sampler or a Wavetable accepts a file drop. Returning false for everything else
    // matters: JUCE walks up the hierarchy for an interested target, so a wav dropped on an
    // Oscillator falls through to GraphEditor, which spawns a new Sampler for it instead of
    // doing nothing.
    if (dynamic_cast<WavetableOscillatorModule*>(module) != nullptr) {
        for (const auto& path : files)
            if (WavetableOscillatorModule::isSupportedWavetableFile(juce::File(path)))
                return true;
        return false;
    }

    if (dynamic_cast<SamplerModule*>(module) == nullptr)
        return false;

    for (const auto& path : files)
        if (SamplerModule::isSupportedAudioFile(juce::File(path)))
            return true;
    return false;
}

void ModuleComponent::fileDragEnter(const juce::StringArray& files, int, int) {
    juce::ignoreUnused(files);
    if (!fileDragHighlight) {
        fileDragHighlight = true;
        repaint();
    }
}

void ModuleComponent::fileDragExit(const juce::StringArray& files) {
    juce::ignoreUnused(files);
    if (fileDragHighlight) {
        fileDragHighlight = false;
        repaint();
    }
}

void ModuleComponent::filesDropped(const juce::StringArray& files, int, int) {
    fileDragHighlight = false;

    // A wavetable card takes the first readable file and imports it through exactly the same
    // path as the Load button, so the Import mode applies to drops too.
    if (dynamic_cast<WavetableOscillatorModule*>(module) != nullptr) {
        for (const auto& path : files) {
            const juce::File file(path);
            if (!WavetableOscillatorModule::isSupportedWavetableFile(file))
                continue;

            if (loadWavetableIntoModule(file))
                refreshWavetableLabel();
            else
                refreshWavetableLabel("Could not read " + file.getFileName());
            repaint();
            return;
        }
        repaint();
        return;
    }

    auto* sampler = dynamic_cast<SamplerModule*>(module);
    if (sampler == nullptr) {
        repaint();
        return;
    }

    // Only the first playable file is used — a Sampler holds one sample. Dropping several onto the
    // canvas (rather than onto a module) creates one Sampler each; that path lives in GraphEditor.
    for (const auto& path : files) {
        const juce::File file(path);
        if (!SamplerModule::isSupportedAudioFile(file))
            continue;

        if (sampler->loadSampleFile(file))
            refreshSampleLabel();
        else
            refreshSampleLabel("Could not read " + file.getFileName());
        return;
    }

    repaint();
}

void ModuleComponent::refreshSampleLabel(const juce::String& fallbackMessage) {
    if (sampleNameLabel == nullptr)
        return;

    auto* sampler = dynamic_cast<SamplerModule*>(module);
    juce::String name = (sampler != nullptr) ? sampler->getSampleName() : juce::String();

    if (fallbackMessage.isNotEmpty())
        sampleNameLabel->setText(fallbackMessage, juce::dontSendNotification);
    else
        sampleNameLabel->setText(name.isEmpty() ? juce::String("(no sample)") : name, juce::dontSendNotification);

    sampleNameLabel->setTooltip(name);
    repaint();
}
