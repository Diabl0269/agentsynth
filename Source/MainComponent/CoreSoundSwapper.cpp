// CoreSoundSwapper.cpp -- swaps pending stand-ins for the core pack's instruments, and gives waiting Drum Kits their
// kit.
#include "MainComponent/CoreSoundSwapper.h"

#include "AI/AIStateMapper/AIStateMapper.h"
#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "Modules/DrumKit/DrumKitModule.h"
#include "Modules/ModuleBase.h"
#include "Modules/ModuleFileStateKeys.h"
#include "Modules/PendingSound.h"
#include "Modules/Sampler/SamplerInstrumentHost.h"
#include "SoundPacks/CoreSounds.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"

namespace synth {

namespace {

using synth::soundpacks::coreInstrumentId;
using synth::soundpacks::coreSoundInstalled;
using synth::soundpacks::findCoreSound;
using synth::soundpacks::kCorePackId;

// The core sound a Drum Kit's saved kit names but has not loaded (the pack was missing when the kit was set).
const synth::soundpacks::CoreSound* waitingKitSound(DrumKitModule& kit) {
    if (kit.hasRecordedKit() || kit.isInstrumentLoadPending())
        return nullptr;
    const auto* sound =
        findCoreSound(kit.getExtraState().getProperty(synth::module_file_keys::kInstrumentPack, {}).toString());
    return sound != nullptr && sound->drumKit && coreSoundInstalled(*sound) ? sound : nullptr;
}

ModuleComponent* componentFor(GraphEditor& editor, const juce::AudioProcessor* processor) {
    for (auto* component : editor.getModuleComponents())
        if (component->getModule() == processor)
            return component;
    return nullptr;
}

void loadPackInstrument(juce::AudioProcessor& processor, const juce::String& instrumentId) {
    if (auto* host = dynamic_cast<synth::sampler::InstrumentHost*>(&processor))
        host->loadInstrumentFromPack(kCorePackId, instrumentId);
}

} // namespace

CoreSoundSwapper::CoreSoundSwapper(AudioEngine& engine, GraphEditor& editor, AppUndoManager& undo)
    : engine_(engine)
    , editor_(editor)
    , undo_(undo) {
    coreWasInstalled_ = coreInstalled();
    installer_->addListener(this);
}

CoreSoundSwapper::~CoreSoundSwapper() { installer_->removeListener(this); }

bool CoreSoundSwapper::coreInstalled() const {
    const auto row = installer_->row(kCorePackId);
    return row && (row->state == synth::soundpacks::SoundPackInstaller::State::Installed ||
                   row->state == synth::soundpacks::SoundPackInstaller::State::UpdateAvailable);
}

// Every progress tick lands here; only the moment the core pack becomes installed does any work.
void CoreSoundSwapper::soundPacksChanged() {
    const bool installed = coreInstalled();
    const bool justInstalled = installed && !coreWasInstalled_;
    coreWasInstalled_ = installed;
    if (justInstalled)
        swapNow();
}

int CoreSoundSwapper::swapNow() {
    auto& graph = engine_.getGraph();
    std::vector<std::pair<juce::AudioProcessorGraph::NodeID, juce::String>> standIns; // node, pack instrument id
    std::vector<std::pair<DrumKitModule*, juce::String>> kits;
    for (auto* node : graph.getNodes()) {
        auto* processor = node->getProcessor();
        if (auto* kit = dynamic_cast<DrumKitModule*>(processor))
            if (const auto* sound = waitingKitSound(*kit))
                kits.emplace_back(kit, coreInstrumentId(*sound));
        if (auto* module = dynamic_cast<ModuleBase*>(processor))
            if (const auto* sound = findCoreSound(synth::pendingSoundOf(*module));
                sound != nullptr && !sound->drumKit && coreSoundInstalled(*sound))
                standIns.emplace_back(node->nodeID, coreInstrumentId(*sound));
    }

    // One undo step for every replacement: each replace records its own change, the group folds them.
    int swapped = 0;
    if (!standIns.empty()) {
        const AppUndoManager::ScopedUndoGroup group(undo_);
        for (const auto& [nodeId, instrumentId] : standIns) {
            auto* node = graph.getNodeForId(nodeId);
            auto* component = node != nullptr ? componentFor(editor_, node->getProcessor()) : nullptr;
            if (component == nullptr)
                continue;
            editor_.replaceModule(component, "Sampler",
                                  [instrumentId](juce::AudioProcessor& p) { loadPackInstrument(p, instrumentId); });
            swapped += graph.getNodeForId(nodeId) == nullptr ? 1 : 0;
        }
    }
    for (const auto& [kit, instrumentId] : kits)
        kit->loadInstrumentFromPack(kCorePackId, instrumentId);

    const int total = swapped + (int)kits.size();
    if (swapped > 0 && onGraphChanged)
        onGraphChanged();
    if (total > 0 && onSwapped)
        onSwapped(total, swapped > 0);
    return total;
}

} // namespace synth
