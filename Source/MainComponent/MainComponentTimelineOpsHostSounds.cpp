// MainComponentTimelineOpsHostSounds.cpp -- an addInstrumentTrack op that names a core pack sound: the pack's
// instrument when it is installed, else the stand-in synth marked to be swapped once the pack arrives
// (docs/ai/song-steps.md#real-instruments).
#include "AI/AIStateMapper/AIStateMapper.h"
#include "MainComponent.h"
#include "MainComponentTimelineOpsHost.h"
#include "Modules/ModuleBase.h"
#include "Modules/ModuleFileStateKeys.h"
#include "Modules/PendingSound.h"
#include "Modules/Sampler/SamplerInstrumentHost.h"
#include "Modules/Sampler/SamplerInstrumentSlot.h"
#include "SoundPacks/CoreSounds.h"

namespace {

using synth::soundpacks::CoreSound;

// The Sampler's Level is the one stand-in param a Sampler also has; the rest (a Pluck's damping, a Wavetable's
// position) tune a synth and mean nothing to a recorded instrument.
juce::var genericParams(const juce::var& params) {
    auto* source = params.getDynamicObject();
    if (source == nullptr || !source->hasProperty("level"))
        return {};
    juce::DynamicObject::Ptr generic = new juce::DynamicObject();
    generic->setProperty("level", source->getProperty("level"));
    return juce::var(generic.get());
}

void applyParams(juce::AudioProcessor& processor, const juce::var& params) {
    if (auto* paramsObj = params.getDynamicObject())
        synth::AIStateMapper::applyUntrustedParams(&processor, paramsObj);
}

// A Sampler holding the pack's instrument, or null when it will not load.
std::unique_ptr<juce::AudioProcessor> makePackSampler(const CoreSound& core, const juce::var& instrumentParams) {
    auto sampler = synth::AIStateMapper::createModule("Sampler");
    auto* host = dynamic_cast<synth::sampler::InstrumentHost*>(sampler.get());
    if (host == nullptr ||
        !host->loadInstrumentFromPack(synth::soundpacks::kCorePackId, synth::soundpacks::coreInstrumentId(core)))
        return nullptr;
    applyParams(*sampler, genericParams(instrumentParams));
    return sampler;
}

// The stand-in synth. A Drum Kit names the core kit instead, which it plays as synth drums until the pack exists.
std::unique_ptr<juce::AudioProcessor> makeStandIn(const CoreSound& core, const juce::String& type,
                                                  const juce::var& instrumentParams) {
    auto instrument = synth::AIStateMapper::createModule(type);
    if (instrument == nullptr)
        return nullptr;
    applyParams(*instrument, instrumentParams);
    auto* module = dynamic_cast<ModuleBase*>(instrument.get());
    if (module == nullptr)
        return instrument;
    if (core.drumKit) {
        juce::DynamicObject::Ptr state = new juce::DynamicObject();
        synth::sampler::writeInstrumentState(
            synth::sampler::InstrumentSource::fromPack(synth::soundpacks::kCorePackId,
                                                       synth::soundpacks::coreInstrumentId(core)),
            *state);
        module->setExtraState(juce::var(state.get()));
    } else {
        synth::setPendingSound(*module, core.id);
    }
    return instrument;
}

// The Drum Kit with the core kit loaded.
std::unique_ptr<juce::AudioProcessor> makeKit(const CoreSound& core, const juce::String& type,
                                              const juce::var& instrumentParams) {
    auto kit = synth::AIStateMapper::createModule(type);
    if (kit == nullptr)
        return nullptr;
    applyParams(*kit, instrumentParams);
    if (auto* host = dynamic_cast<synth::sampler::InstrumentHost*>(kit.get()))
        host->loadInstrumentFromPack(synth::soundpacks::kCorePackId, synth::soundpacks::coreInstrumentId(core));
    return kit;
}

} // namespace

std::optional<synth::InstrumentTrackBuildResult> MainComponentTimelineOpsHost::addInstrumentTrackWithSound(
    const juce::String& name, const juce::String& instrumentType, bool poly,
    const std::vector<synth::InstrumentTrackInsert>& inserts, const juce::var& envelopeParams,
    const juce::var& instrumentParams, const juce::String& sound) {
    const auto* core = synth::soundpacks::findCoreSound(sound);
    if (core == nullptr || instrumentType != core->standIn)
        return addInstrumentTrack(name, instrumentType, poly, inserts, envelopeParams, instrumentParams);

    const bool installed = synth::soundpacks::coreSoundInstalled(*core);
    std::unique_ptr<juce::AudioProcessor> instrument;
    bool isSampler = false;
    if (installed && core->drumKit) {
        instrument = makeKit(*core, instrumentType, instrumentParams);
    } else if (installed) {
        instrument = makePackSampler(*core, instrumentParams);
        isSampler = instrument != nullptr;
    }
    if (instrument == nullptr)
        instrument = makeStandIn(*core, instrumentType, instrumentParams);
    if (instrument == nullptr)
        return std::nullopt;

    auto staged = std::make_shared<std::unique_ptr<juce::AudioProcessor>>(std::move(instrument));
    // A Sampler has no poly mode and owns its envelope, so the stand-in's poly flag and envelope params do not apply.
    return owner_.buildInstrumentTrackBody(staged, name, isSampler ? false : poly, inserts,
                                           isSampler ? juce::var() : envelopeParams);
}
