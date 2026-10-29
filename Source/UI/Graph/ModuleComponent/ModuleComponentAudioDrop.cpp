// ModuleComponentAudioDrop.cpp -- the Sampler's and Drum Kit's chrome creation (SamplerChrome, DrumKitChrome) plus
// audio-file drag-and-drop for both the Sampler and the Wavetable oscillator's file targets. ModuleComponent is
// declared in ModuleComponent.h; the rest of its implementation lives in the sibling ModuleComponent*.cpp units next to
// this one.
#include "AudioEngine/AudioEngine.h"
#include "ModuleComponent.h"
#include "Modules/SamplerModule.h"
#include "UI/Graph/CardBody/CardBody.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/SamplerChrome/DrumKitChrome.h"

void ModuleComponent::createSamplerControls() {
    synth::ui::InstrumentChrome::Host host{this, undoManager, &owner.getAudioEngine().getGraph(), {}};
    if (auto* sampler = dynamic_cast<SamplerModule*>(module)) {
        instrumentChrome_ = std::make_unique<synth::ui::SamplerChrome>(host, *sampler);
    } else if (auto* kit = dynamic_cast<DrumKitModule*>(module)) {
        // The body re-reads its conditions when the instrument changes.
        host.kitChanged = [this] {
            if (cardBody_ != nullptr)
                cardBody_->refreshConditions();
        };
        host.drumMachineAvailable = [this] { return owner.drumKitHasTrack && owner.drumKitHasTrack(nodeId); };
        host.openDrumMachine = [this] {
            if (owner.onOpenDrumMachineRequested)
                owner.onOpenDrumMachineRequested(nodeId);
        };
        host.relayout = [this] {
            updateLayout();
            owner.handleModuleResized(this);
            repaint();
        };
        instrumentChrome_ = std::make_unique<synth::ui::DrumKitChrome>(host, *kit);
    }
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

    return instrumentChrome_ != nullptr && instrumentChrome_->isInterestedInDrop(files);
}

void ModuleComponent::fileDragEnter(const juce::StringArray& files, int, int) {
    if (instrumentChrome_ != nullptr)
        instrumentChrome_->dragEntered(files);
    if (!fileDragHighlight) {
        fileDragHighlight = true;
        repaint();
    }
}

void ModuleComponent::fileDragExit(const juce::StringArray&) {
    if (instrumentChrome_ != nullptr)
        instrumentChrome_->dragLeft();
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
    if (instrumentChrome_ != nullptr && instrumentChrome_->dropFiles(files)) {
        repaint();
        return;
    }
    for (const auto& path : files) {
        const juce::File file(path);
        if (!SamplerModule::isSupportedAudioFile(file))
            continue;

        // One undo step, so a Cmd+Z puts the previous sample back; a drop from the OS and a dragged Sound both land
        // here.
        if (auto* chrome = dynamic_cast<synth::ui::SamplerChrome*>(instrumentChrome_.get()))
            chrome->loadSampleUndoably(file);
        else if (sampler->loadSampleFile(file))
            refreshSampleLabel();
        else
            refreshSampleLabel("Could not read " + file.getFileName());
        return;
    }

    repaint();
}

void ModuleComponent::refreshSampleLabel(const juce::String& fallbackMessage) {
    if (instrumentChrome_ != nullptr)
        instrumentChrome_->refreshSampleLabel(fallbackMessage);
}

// A project opened on screen decodes its samples and wavetables after the cards exist (DeferredAssetLoads.h): the
// waveform and frame views follow the module on their own ticks, but the file-name labels are only ever set here.
void ModuleComponent::refreshLoadedFileLabels() {
    if (instrumentChrome_ != nullptr)
        instrumentChrome_->refreshLoaded();
    refreshWavetableLabel();
}
