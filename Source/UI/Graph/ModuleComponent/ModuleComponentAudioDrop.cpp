// ModuleComponentAudioDrop.cpp -- the Sampler's chrome creation (SamplerChrome) plus audio-file drag-and-drop for both
// the Sampler and the Wavetable oscillator's file targets. ModuleComponent is declared in ModuleComponent.h; the rest
// of its implementation lives in the sibling ModuleComponent*.cpp units next to this one.
#include "AudioEngine/AudioEngine.h"
#include "ModuleComponent.h"
#include "Modules/SamplerModule.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"

void ModuleComponent::createSamplerControls() {
    auto* sampler = dynamic_cast<SamplerModule*>(module);
    if (sampler == nullptr)
        return;

    samplerChrome_ = std::make_unique<synth::ui::SamplerChrome>(
        synth::ui::SamplerChrome::Host{this, undoManager, &owner.getAudioEngine().getGraph()}, *sampler);
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

    return samplerChrome_ != nullptr && samplerChrome_->isInterestedInDrop(files);
}

void ModuleComponent::fileDragEnter(const juce::StringArray& files, int, int) {
    if (samplerChrome_ != nullptr)
        samplerChrome_->dragEntered(files);
    if (!fileDragHighlight) {
        fileDragHighlight = true;
        repaint();
    }
}

void ModuleComponent::fileDragExit(const juce::StringArray&) {
    if (samplerChrome_ != nullptr)
        samplerChrome_->dragLeft();
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

    // An .sfz, or two or more recordings, become an instrument (SamplerChrome::dropFiles). One recording is a
    // sample: the first playable file is used. Dropping several onto the canvas (rather than onto a module)
    // creates one Sampler each; that path lives in GraphEditor.
    if (samplerChrome_ != nullptr && samplerChrome_->dropFiles(files)) {
        repaint();
        return;
    }
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
    if (samplerChrome_ != nullptr)
        samplerChrome_->refreshSampleLabel(fallbackMessage);
}

// A project opened on screen decodes its samples and wavetables after the cards exist (DeferredAssetLoads.h): the
// waveform and frame views follow the module on their own ticks, but the file-name labels are only ever set here.
void ModuleComponent::refreshLoadedFileLabels() {
    if (samplerChrome_ != nullptr)
        samplerChrome_->refreshLoaded();
    refreshWavetableLabel();
}
