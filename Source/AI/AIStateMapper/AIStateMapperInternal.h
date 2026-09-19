#pragma once

// Shared internal helpers for the AIStateMapper translation units (AIStateMapper.cpp,
// AIStateMapperValidation.cpp, AIStateMapperSnapshots.cpp, AIStateMapperSchema.cpp). Everything
// here is an implementation detail of the split, not part of the public AIStateMapper API — never
// included from outside this directory. The class itself is declared in AIStateMapper.h.

#include "AIStateMapper.h"

#include "../../Modules/ADSRModule.h"
#include "../../Modules/AttenuverterModule.h"
#include "../../Modules/AudioInputModule.h"
#include "../../Modules/ChannelStripModule.h"
#include "../../Modules/ExternalMidiModule.h"
#include "../../Modules/FX/ChorusModule.h"

#include "../../Modules/ComparatorModule.h"
#include "../../Modules/EnvelopeFollowerModule.h"
#include "../../Modules/FX/BitcrusherModule.h"
#include "../../Modules/FX/CompressorModule.h"
#include "../../Modules/FX/DelayModule.h"
#include "../../Modules/FX/DistortionModule.h"
#include "../../Modules/FX/FlangerModule.h"
#include "../../Modules/FX/GateModule.h"
#include "../../Modules/FX/LimiterModule.h"
#include "../../Modules/FX/ParametricEQModule.h"
#include "../../Modules/FX/PhaserModule.h"
#include "../../Modules/FX/PitchShifterModule.h"
#include "../../Modules/FX/ReverbModule.h"
#include "../../Modules/FX/RingModulatorModule.h"
#include "../../Modules/FilterModule.h"
#include "../../Modules/LFOModule.h"
#include "../../Modules/MacroControlModule.h"
#include "../../Modules/MacroInletModule.h"
#include "../../Modules/MacroMidiInletModule.h"
#include "../../Modules/MacroMidiOutletModule.h"
#include "../../Modules/MacroOutletModule.h"
#include "../../Modules/MasterModule.h"
#include "../../Modules/MathModule.h"
#include "../../Modules/MidiKeyboardModule.h"
#include "../../Modules/ModuleBase.h"
#include "../../Modules/NoiseModule.h"
#include "../../Modules/OscillatorModule.h"
#include "../../Modules/PolyMidiModule.h"
#include "../../Modules/PolySequencerModule.h"
#include "../../Modules/RecordTapModule.h"
#include "../../Modules/SampleHoldModule.h"
#include "../../Modules/SamplerModule.h"
#include "../../Modules/SequencerModule.h"
#include "../../Modules/TimelineAudioSourceModule.h"
#include "../../Modules/TimelineMidiSourceModule.h"
#include "../../Modules/VCAModule.h"
#include "../../Modules/VoiceMixerModule.h"
#include "../../Modules/WavetableOscillatorModule/WavetableOscillatorModule.h"
#include "../../Plugin/Hosting/HostedPluginModule.h"
#include <cmath>
#include <functional> // For std::function
#include <limits>
#include <set>
#include <unordered_map> // For the factory map

namespace synth {
namespace detail {

typedef juce::AudioProcessorGraph::AudioGraphIOProcessor AudioGraphIOProcessor;

using ModuleFactoryFunc = std::function<std::unique_ptr<juce::AudioProcessor>()>;

// Factory map for module creation
inline const std::unordered_map<juce::String, ModuleFactoryFunc>& moduleFactory() {
    static const std::unordered_map<juce::String, ModuleFactoryFunc> factory = {
        // A real module, not the graph's audioInputNode — same factory key and same display name, so
        // every patch already saved with an "Audio Input" node loads onto the module unchanged.
        {"Audio Input", []() { return std::make_unique<AudioInputModule>(); }},
        {"Audio Output",
         []() { return std::make_unique<AudioGraphIOProcessor>(AudioGraphIOProcessor::audioOutputNode); }},
        {"Midi Input", []() { return std::make_unique<AudioGraphIOProcessor>(AudioGraphIOProcessor::midiInputNode); }},
        {"Oscillator", []() { return std::make_unique<OscillatorModule>(); }},
        {"Filter", []() { return std::make_unique<FilterModule>(); }},
        {"VCA", []() { return std::make_unique<VCAModule>(); }},
        {"ADSR", []() { return std::make_unique<ADSRModule>("ADSR"); }}, // ADSR constructor for generic case
        {"Sequencer", []() { return std::make_unique<SequencerModule>(); }},
        {"LFO", []() { return std::make_unique<LFOModule>(); }},
        {"Distortion", []() { return std::make_unique<DistortionModule>(); }},
        {"Delay", []() { return std::make_unique<DelayModule>(); }},
        {"Reverb", []() { return std::make_unique<ReverbModule>(); }},
        {"MIDI Keyboard", []() { return std::make_unique<MidiKeyboardModule>(); }},
        {"Amp Env", []() { return std::make_unique<ADSRModule>("Amp Env"); }},
        {"Filter Env", []() { return std::make_unique<ADSRModule>("Filter Env"); }},
        {"Poly MIDI", []() { return std::make_unique<PolyMidiModule>(); }},
        {"Poly Sequencer", []() { return std::make_unique<PolySequencerModule>(); }},
        {"Attenuverter", []() { return std::make_unique<AttenuverterModule>(); }},
        {"Mod Slot", []() { return std::make_unique<AttenuverterModule>(); }},
        {"Chorus", []() { return std::make_unique<ChorusModule>(); }},
        {"Phaser", []() { return std::make_unique<PhaserModule>(); }},
        {"Compressor", []() { return std::make_unique<CompressorModule>(); }},
        {"Flanger", []() { return std::make_unique<FlangerModule>(); }},
        {"Limiter", []() { return std::make_unique<LimiterModule>(); }},
        {"Gate", []() { return std::make_unique<GateModule>(); }},
        {"Parametric EQ", []() { return std::make_unique<ParametricEQModule>(); }},
        {"Voice Mixer", []() { return std::make_unique<VoiceMixerModule>(); }},
        {"Bitcrusher", []() { return std::make_unique<BitcrusherModule>(); }},
        {"Pitch Shifter", []() { return std::make_unique<PitchShifterModule>(); }},
        {"Ring Modulator", []() { return std::make_unique<RingModulatorModule>(); }},
        {"Noise", []() { return std::make_unique<NoiseModule>(); }},
        {"Envelope Follower", []() { return std::make_unique<EnvelopeFollowerModule>(); }},
        {"Math", []() { return std::make_unique<MathModule>(); }},
        {"Macros", []() { return std::make_unique<MacroControlModule>(); }},
        {"Sample & Hold", []() { return std::make_unique<SampleHoldModule>(); }},
        {"Comparator", []() { return std::make_unique<ComparatorModule>(); }},
        {"Sampler", []() { return std::make_unique<SamplerModule>(); }},
        {"Wavetable", []() { return std::make_unique<WavetableOscillatorModule>(); }},
        {"External MIDI", []() { return std::make_unique<ExternalMidiModule>(); }},
        // A third-party VST3/AU plugin as a module. In the factory so our own saves reload it (the
        // node comes back as a placeholder and re-loads its plugin asynchronously);
        // kNonAuthorableModuleTypes below keeps it away from the model.
        {"Hosted Plugin", []() { return std::make_unique<HostedPluginModule>(); }},
        // In the factory (rather than constructed ad hoc by the add-track flow) purely so our own
        // saves round-trip it; kNonAuthorableModuleTypes below keeps it away from the model.
        {"Track In", []() { return std::make_unique<TimelineMidiSourceModule>(); }},
        // In the factory so our own saves round-trip a patch that has one; kNonAuthorableModuleTypes
        // below keeps it away from the model.
        {"Rec Tap", []() { return std::make_unique<RecordTapModule>(); }},
        // In the factory so our own saves round-trip a patch that has one; kNonAuthorableModuleTypes
        // below keeps it away from the model.
        {"Track Audio", []() { return std::make_unique<TimelineAudioSourceModule>(); }},
        // A Macro's audio/CV inlet/outlet jacks (P8-15 Macro I/O). In the factory so our own saves
        // round-trip a patch that has one; kNonAuthorableModuleTypes below keeps them away from the
        // model — see docs/macros/macros.md#ai-authorability.
        {"Macro In", []() { return std::make_unique<MacroInletModule>(); }},
        {"Macro Out", []() { return std::make_unique<MacroOutletModule>(); }},
        // A Macro's MIDI inlet/outlet jacks — a separate type from the audio/CV pair above (see
        // MacroMidiInletModule's class comment for why), same reason for being in the factory.
        {"Macro MIDI In", []() { return std::make_unique<MacroMidiInletModule>(); }},
        {"Macro MIDI Out", []() { return std::make_unique<MacroMidiOutletModule>(); }},
        // The mixer's channel strip and mix bus (P9-2; docs/mixer/mixer.md#node-types). In the factory so our own
        // saves round-trip a patch that has them; kNonAuthorableModuleTypes below keeps them away from
        // the model — see docs/mixer/mixer.md#ai-authorability.
        {"Channel Strip", []() { return std::make_unique<ChannelStripModule>(); }},
        {"Master", []() { return std::make_unique<MasterModule>(); }},
    };
    return factory;
}

// The module types a model may never author, as an explicit set with a reason recorded against
// each entry — not a chain of equality tests, which said nothing about why a name was on it.
//
// Registering a module in moduleFactory above makes it model-authorable BY DEFAULT. That default
// is right for an ordinary DSP module and wrong for anything that names an external resource or
// carries privileged state (a hosted plugin binary, a timeline feed, a file path): such a module
// belongs in this set at the moment it is registered. The exact resulting allowlist is pinned by
// AIStateMapperTest.AuthorableModuleTypesGolden, so either kind of addition fails the build until
// the choice is made deliberately.
inline const std::set<juce::String> kNonAuthorableModuleTypes = {
    // Attenuverters are an implementation detail of the `modulations` array — applyJSONToGraph
    // creates them itself — so exposing them would invite the model to hand-build modulation
    // chains that the mod matrix then can't read back.
    "Attenuverter",
    // The same AttenuverterModule, registered under the name the modulation UI uses for it.
    "Mod Slot",
    // The timeline feed. A Track In node's only meaningful state is the identity of the timeline
    // track bound to it, which lives OUTSIDE the patch — so a model authoring one either creates a
    // node that plays nothing, or (worse) one that latches onto a track the user owns. The
    // timeline's own add-track flow is the only thing that may create these.
    "Track In",
    // The audio-take tap. It names a FILE PATH on disk — a model that could author one could aim
    // a recording anywhere the app can write, which is the same class of authority the Sampler's
    // `"state"` file path is denied on the untrusted path. The record flow is the only thing that
    // may create these.
    "Rec Tap",
    // The audio-track player. Same authority argument as Rec Tap from the other direction: a Track
    // Audio node plays whatever clips the track bound to it names, so a model authoring one and
    // latching it onto a track would be choosing what gets read off disk and rendered. The
    // timeline's own add-track flow is the only thing that may create these.
    "Track Audio",
    // A hosted third-party plugin. The strongest case on this list: the node's `"state"` carries
    // the plugin's own opaque byte blob, which is handed verbatim to
    // AudioPluginInstance::setStateInformation — i.e. straight into third-party code that will
    // parse it however it likes. Its identity also selects WHICH binary the host loads. Neither may
    // ever be chosen by a model, so the type is refused outright on the untrusted path
    // (PatchValidationError::InternalModuleNotAllowed) rather than sanitised. Only the app's own
    // load UX may create one.
    "Hosted Plugin",
    // A Macro's audio/CV inlet/outlet jack (P8-15 Macro I/O; docs/macros/macros.md#ai-authorability). Membership of
    // the macro it belongs to is keyed by node uuid, and a provider-supplied uuid is ignored
    // (adoptUuidIfTrusted) — so a model-authored one could never resolve to a real macro's port
    // list even if it were let through. The macro's own port-creation flow is the only thing that
    // may create these.
    "Macro In",
    "Macro Out",
    // A Macro's MIDI inlet/outlet jack. Same reasoning as "Macro In"/"Macro Out" — see
    // MacroMidiInletModule's class comment for why it is a separate type at all.
    "Macro MIDI In",
    "Macro MIDI Out",
    // The mixer's channel strip (docs/mixer/mixer.md#ai-authorability). A strip's meaning is the channel the app built
    // around it — its shape, its place at the end of a chain, the track it may be linked to — and
    // its solo flag rides in trusted extra state. "The AI can build a channel" is an app-side
    // action the model invokes, never a patch node it writes directly.
    "Channel Strip",
    // The mix bus. A singleton spliced in front of the output by the app (synth::ensureMasterNode);
    // a model-authored second Master would split the mix and defeat the solo gate on Direct.
    "Master",
};

inline bool isInternalOnlyModule(const juce::String& typeName) { return kNonAuthorableModuleTypes.count(typeName) > 0; }

// Whether a merge patch's "type" designates the module that already lives under that node id.
// Two namespaces meet here: the factory key graphToJSON writes, and the processor's display name.
// They differ for the ADSR aliases — an "Amp Env" node serializes as type "ADSR" — so comparing
// against only one of them makes an update-in-place silently fall through to "create a second
// node", which is the aliasing this predicate exists to prevent.
inline bool patchTypeMatchesProcessor(juce::AudioProcessor* processor, const juce::String& type) {
    return processor->getName() == type || AIStateMapper::getFactoryTypeName(processor) == type;
}

// Accepts only whole numbers representable as a uint32 (matches juce::AudioProcessorGraph::NodeID's
// underlying type). Rejects negatives, fractional values, and non-numeric JSON values outright —
// there is no legitimate node id that isn't one of these.
inline bool extractUnsignedInt(const juce::var& v, juce::uint32& out) {
    if (v.isInt() || v.isInt64()) {
        auto i = static_cast<juce::int64>(v);
        if (i < 0 || i > static_cast<juce::int64>(std::numeric_limits<juce::uint32>::max()))
            return false;
        out = static_cast<juce::uint32>(i);
        return true;
    }
    if (v.isDouble()) {
        double d = static_cast<double>(v);
        if (!std::isfinite(d) || d < 0.0 || d > static_cast<double>(std::numeric_limits<juce::uint32>::max()) ||
            d != std::floor(d))
            return false;
        out = static_cast<juce::uint32>(d);
        return true;
    }
    return false;
}

// Mirrors a node's "uuid" property into the processor itself (ModuleBase::setNodeUuid), so the
// AUDIO thread can read it without touching a juce::NamedValueSet or a juce::String. Called at
// every one of the three sites that writes the property — keep them paired, or a Track In node
// silently stops matching its timeline track. See the invariant on ModuleBase::setNodeUuid.
inline void mirrorUuidIntoProcessor(juce::AudioProcessorGraph::Node* node, const juce::String& uuid) {
    if (node == nullptr)
        return;
    if (auto* mb = dynamic_cast<ModuleBase*>(node->getProcessor()))
        mb->setNodeUuid(uuid);
}

// Copies a patch node's custom card title ("displayName") onto the live node.
//
// Applied on BOTH the trusted and untrusted paths, unlike "uuid" and "state" above, because it is
// display-only: it is never consulted for module-type resolution (that is "type"), for node
// identity ("uuid"), for parameter values, or for anything else semantic. The worst an untrusted
// patch can do with it is mislabel a card, which the user can see and rename. It IS length-capped,
// so a hostile patch cannot stuff a megabyte of text into a title and wedge the canvas paint.
//
// Blank or whitespace-only means "no custom title" — the card falls back to the auto-numbered name
// AudioEngine::updateModuleNames() maintains on the processor.
inline void applyDisplayNameToNode(juce::AudioProcessorGraph::Node* node, const juce::DynamicObject* nObj) {
    if (node == nullptr || nObj == nullptr || !nObj->hasProperty("displayName"))
        return;
    const auto name = nObj->getProperty("displayName").toString().trim();
    if (name.isEmpty()) {
        node->properties.remove("displayName");
        return;
    }
    node->properties.set("displayName", name.substring(0, synth::kMaxModuleDisplayNameChars));
}

} // namespace detail
} // namespace synth
