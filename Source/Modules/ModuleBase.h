#pragma once

#include "VisualBuffer.h"
#include <algorithm>
#include <atomic>
#include <cstring>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>
#include <vector>

struct ModulationTarget {
    juce::String name;
    int channelIndex;
};

enum class ModulationCategory { Envelope, LFO, Oscillator, Sequencer, Filter, FX, Other };

enum class PortRole { Audio, ModCV, Pitch, Gate, Midi, Other };

struct LogicalPort {
    int visibleJackIndex =
        0; // which visible jack (0..getVisible*PortCount()-1) a wire to this raw channel should anchor to
    PortRole role = PortRole::Other;
    bool isPolyGroupHead = false; // true only for the lowest raw channel of a poly fan
    int polyVoiceSpan = 1;        // 1 = mono; N = head of an N-voice fan
};

enum class ModuleType {
    Oscillator,
    Filter,
    VCA,
    ADSR,
    LFO,
    Sequencer,
    PolySequencer,
    MidiKeyboard,
    PolyMidi,
    ExternalMidi,
    Attenuverter,
    Delay,
    Distortion,
    Reverb,
    Chorus,
    Phaser,
    Compressor,
    Flanger,
    Limiter,
    ParametricEQ,
    VoiceMixer,
    Bitcrusher,
    PitchShifter,
    Noise,
    Math,
    Sampler,
    Wavetable,
    MacroControl,
    SampleHold,
    EnvelopeFollower,
    // The device-input tap. A singleton in the patch (GraphEditor::isSingletonIOModule),
    // paired with the "Audio Output" node, which is still a juce::AudioGraphIOProcessor.
    AudioInput,
    // Internal-only: the timeline's "Track In" node. Created by the add-track flow, never
    // offered by the library and never authorable by a model.
    TimelineMidiSource,
    // Internal-only: the "Rec Tap" node an audio take records through. Auto-spliced in
    // front of Audio Output by the record flow; same three exclusions as Track In (no library row,
    // no replace-menu entry, never authorable — it names a file path on disk).
    RecordTap,
    // Internal-only: the timeline's "Track Audio" node — the disk-streaming playback end of
    // an audio track. Created by the add-track flow, same three exclusions as Track In and Rec Tap
    // (a model that could author one could point playback at a clip, and therefore a file, it
    // chose).
    TimelineAudioSource,
    // Internal-only: a third-party VST3/AU plugin hosted as a module
    // (synth::HostedPluginModule). Not offered by the library or the replace menu until the scan
    // list and load UX ship, and never authorable by a model — its "state" carries an opaque
    // byte blob handed straight to third-party code, which is the last thing that should arrive
    // from a model.
    HostedPlugin
};

class ModuleBase : public juce::AudioProcessor {
public:
    ModuleBase(const juce::String& name, int numInputs, int numOutputs)
        : AudioProcessor(
              BusesProperties()
                  .withInput("Input", juce::AudioChannelSet::discreteChannels(std::max(1, numInputs)), numInputs > 0)
                  .withOutput("Output", juce::AudioChannelSet::discreteChannels(std::max(1, numOutputs)),
                              numOutputs > 0))
        , moduleName(name) {
        addParameter(bypassedParam = new juce::AudioParameterBool("bypassed", "Bypassed", false));
    }

    void addMuteParameter() {
        if (!mutedParam)
            addParameter(mutedParam = new juce::AudioParameterBool("muted", "Muted", false));
    }

    // Opt-in output-level stage for modules whose output is audio.
    //
    // Deliberately NOT added in the ModuleBase ctor: parameter position is load-bearing
    // for a few positional getParameters()[n] call sites, and "gain" is meaningless (or
    // actively wrong) on pitch/gate CV outputs — scaling a V/oct pitch CV detunes, and
    // scaling a gate drops it under the > 0.5f trigger threshold. Each module opts in,
    // and MUST call this AFTER its own addParameter() calls so the new parameter lands
    // last and existing positional lookups keep resolving to the same parameter.
    void addOutputLevelParameter(float defaultValue = 1.0f) {
        if (outputLevelParam)
            return;
        addParameter(outputLevelParam =
                         new juce::AudioParameterFloat("outputLevel", "Level", 0.0f, 1.0f, defaultValue));
        // Safe default if prepareToPlay (and therefore prepareOutputLevel) never runs:
        // snap-to-target, so applyOutputLevel takes its steady-state path at unity
        // instead of ramping up from a default-constructed 0 and silencing the module.
        smoothedOutputLevel.reset(1);
        smoothedOutputLevel.setCurrentAndTargetValue(defaultValue);
    }

    bool hasOutputLevel() const { return outputLevelParam != nullptr; }
    float getOutputLevel() const { return outputLevelParam != nullptr ? outputLevelParam->get() : 1.0f; }

    ~ModuleBase() override = default;

    const juce::String getName() const override { return moduleName; }

    bool isBypassed() const { return bypassedParam->get(); }
    void setBypassed(bool b) { bypassedParam->setValueNotifyingHost(b ? 1.0f : 0.0f); }

    bool isMuted() const { return mutedParam->get(); }
    void setMuted(bool m) { mutedParam->setValueNotifyingHost(m ? 1.0f : 0.0f); }

    void prepareToPlay(double sampleRate, int samplesPerBlock) override = 0;
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override = 0;

    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; } // To be implemented later

    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    double getTailLengthSeconds() const override { return 0.0; }

    // Boilerplate
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int index) override { juce::ignoreUnused(index); }
    const juce::String getProgramName(int index) override {
        juce::ignoreUnused(index);
        return {};
    }
    void changeProgramName(int index, const juce::String& newName) override { juce::ignoreUnused(index, newName); }
    void getStateInformation(juce::MemoryBlock& destData) override {
        juce::ValueTree state("ModuleState");
        for (auto* param : getParameters()) {
            if (auto* p = dynamic_cast<juce::AudioProcessorParameterWithID*>(param)) {
                state.setProperty(p->paramID, p->getValue(), nullptr);
            }
        }
        copyXmlToBinary(*state.createXml(), destData);
    }

    void setStateInformation(const void* data, int sizeInBytes) override {
        auto xmlState = getXmlFromBinary(data, sizeInBytes);
        if (xmlState != nullptr) {
            if (xmlState->hasTagName("ModuleState")) {
                juce::ValueTree state = juce::ValueTree::fromXml(*xmlState);
                for (auto* param : getParameters()) {
                    if (auto* p = dynamic_cast<juce::AudioProcessorParameterWithID*>(param)) {
                        if (state.hasProperty(p->paramID)) {
                            p->setValue((float)state.getProperty(p->paramID));
                        }
                    }
                }
            }
        }
    }

    void setModuleName(const juce::String& name) { moduleName = name; }

    // -- Node identity, readable from the audio thread ---------------------------------------
    //
    // The graph node's "uuid" property is the app's long-lived node identity (see
    // AIStateMapper::graphToJSON): timeline track bindings and automation lanes key on it. It
    // lives in a juce::NamedValueSet of juce::Strings, neither of which the audio thread may
    // touch, so it is MIRRORED here into a fixed char buffer the moment it is assigned. A module
    // that has to recognise itself in an audio-thread data structure (Track In matching a
    // TimelineSnapshot::TrackInfo::bindingUuid) strcmps against getNodeUuid().
    //
    // Writers: the three places AIStateMapper writes node->properties["uuid"] — adoptUuidIfTrusted
    // (trusted apply), graphToJSON's lazy generation, and applySnapshotPreservingNodes. Each
    // mirrors into the processor immediately after setting the property, so the two never diverge.
    //
    // INVARIANT the lock-free read relies on: the uuid only ever transitions EMPTY -> value, and
    // only while the node is not yet audio-visible (graphToJSON/adopt run on a node the caller
    // just created, or with the graph callback lock held). It is never rewritten to a different
    // value and never cleared, so an audio-thread reader either sees "" (and does nothing) or the
    // final value — there is no torn intermediate. The release/acquire pair on nodeUuidSet_ is
    // what publishes the buffer's bytes alongside the flag.
    void setNodeUuid(const juce::String& uuid) {
        const auto* utf8 = uuid.toRawUTF8();
        const auto length = std::strlen(utf8);
        const auto copied = std::min<std::size_t>(length, sizeof(nodeUuid_) - 1);
        std::memcpy(nodeUuid_, utf8, copied);
        std::memset(nodeUuid_ + copied, 0, sizeof(nodeUuid_) - copied);
        nodeUuidSet_.store(true, std::memory_order_release);
    }

    // Audio-safe: no allocation, no juce::String, never null. Returns "" until a uuid is assigned.
    const char* getNodeUuid() const noexcept { return nodeUuidSet_.load(std::memory_order_acquire) ? nodeUuid_ : ""; }

    virtual std::vector<ModulationTarget> getModulationTargets() const { return {}; }
    virtual juce::String getInputPortLabel(int channelIndex) const { return "In " + juce::String(channelIndex); }
    virtual juce::String getOutputPortLabel(int channelIndex) const { return "Out " + juce::String(channelIndex); }
    virtual int getVisibleInputPortCount() const { return getTotalNumInputChannels(); }
    virtual int getVisibleOutputPortCount() const { return getTotalNumOutputChannels(); }
    virtual ModulationCategory getModulationCategory() const { return ModulationCategory::Other; }
    virtual ModuleType getModuleType() const = 0;

    virtual LogicalPort mapInputChannel(int rawChannel) const {
        LogicalPort p;
        int vis = getVisibleInputPortCount();
        p.visibleJackIndex = (vis > 0) ? juce::jlimit(0, vis - 1, rawChannel) : 0;
        p.role = PortRole::Other;
        p.isPolyGroupHead = (rawChannel < vis);
        p.polyVoiceSpan = 1;
        return p;
    }

    virtual LogicalPort mapOutputChannel(int rawChannel) const {
        LogicalPort p;
        int vis = getVisibleOutputPortCount();
        p.visibleJackIndex = (vis > 0) ? juce::jlimit(0, vis - 1, rawChannel) : 0;
        p.role = PortRole::Other;
        p.isPolyGroupHead = (rawChannel < vis);
        p.polyVoiceSpan = 1;
        return p;
    }

    /** One poly-group head reachable from a visible jack: the raw channel a wire anchored to that
     *  jack should start at, what the channel carries, and how many consecutive raw channels the
     *  fan spans (1 = mono). */
    struct JackTarget {
        int rawHeadChannel = 0;
        PortRole role = PortRole::Other;
        int voiceSpan = 1;
    };

    /** Inverse of mapInput/OutputChannel: every poly-group head that anchors to visibleJackIndex.
     *  Normally one entry.  Poly MIDI's single "Poly Out" jack fronts two fans (Pitch at raw 0 and
     *  Gate at raw 8), so it returns both and the caller disambiguates by role.
     *  Never returns empty — an unmapped jack falls back to the raw==jack identity the editor
     *  assumed before the logical-port API existed, so unmapped modules keep their old wiring. */
    std::vector<JackTarget> getJackTargets(int visibleJackIndex, bool isInput) const {
        std::vector<JackTarget> targets;
        const int numRaw = isInput ? getTotalNumInputChannels() : getTotalNumOutputChannels();

        for (int raw = 0; raw < numRaw; ++raw) {
            const LogicalPort p = isInput ? mapInputChannel(raw) : mapOutputChannel(raw);
            if (p.isPolyGroupHead && p.visibleJackIndex == visibleJackIndex)
                targets.push_back({raw, p.role, std::max(1, p.polyVoiceSpan)});
        }

        if (targets.empty())
            targets.push_back({visibleJackIndex, PortRole::Other, 1});

        return targets;
    }

    // Decouples display-advertising from JSON auto-promotion (used by AIStateMapper in a LATER increment; defined now).
    // Default: a channel is auto-promotable iff it is one of this module's getModulationTargets() channelIndex values.
    virtual bool isAutoPromotableModTarget(int dstChannel) const {
        for (const auto& t : getModulationTargets())
            if (t.channelIndex == dstChannel)
                return true;
        return false;
    }

    // Non-parameter state that must survive a graph rebuild (undo/redo, preset save/load), e.g. the
    // Sampler's loaded file path. AIStateMapper::graphToJSON writes whatever this returns as the
    // node's "state" property and applyJSONToGraph feeds it back through setExtraState().
    //
    // Restored ONLY on the trusted path (our own snapshots and presets). Untrusted model output must
    // never reach setExtraState — a module is free to interpret this as a filename, and letting a
    // remote model pick that filename would turn a patch suggestion into an arbitrary file read.
    // Return a void var when there is nothing to persist, so untouched modules add no JSON noise.
    virtual juce::var getExtraState() const { return {}; }
    virtual void setExtraState(const juce::var& state) { juce::ignoreUnused(state); }

    VisualBuffer* getVisualBuffer() { return visualBuffer.get(); }
    void enableVisualBuffer(bool enable) {
        if (enable && !visualBuffer)
            visualBuffer = std::make_unique<VisualBuffer>();
        else if (!enable)
            visualBuffer = nullptr;
    }

protected:
    // Call from prepareToPlay() when the module uses addOutputLevelParameter().
    void prepareOutputLevel(double sampleRate) {
        smoothedOutputLevel.reset(sampleRate, 0.01); // 10 ms ramp — anti-click on knob moves
        smoothedOutputLevel.setCurrentAndTargetValue(getOutputLevel());
    }

    // Scales the first numAudioChannels channels by the smoothed output level.
    //
    // Call at the END of the normal processBlock path only. Never on the bypass branch
    // (dry pass-through must stay untouched) and never on mute (already cleared) — see
    // the bypass/mute contract in docs/architecture.md. No-op when the module did not
    // call addOutputLevelParameter().
    void applyOutputLevel(juce::AudioBuffer<float>& buffer, int numAudioChannels) {
        if (outputLevelParam == nullptr)
            return;

        const int numSamples = buffer.getNumSamples();
        const int numChannels = juce::jmin(numAudioChannels, buffer.getNumChannels());
        if (numSamples == 0 || numChannels <= 0)
            return;

        smoothedOutputLevel.setTargetValue(outputLevelParam->get());

        if (!smoothedOutputLevel.isSmoothing()) {
            const float gain = smoothedOutputLevel.getCurrentValue();
            if (gain != 1.0f)
                for (int ch = 0; ch < numChannels; ++ch)
                    buffer.applyGain(ch, 0, numSamples, gain);
            return;
        }

        // getArrayOfWritePointers() avoids re-resolving the pointer per sample and
        // allocates nothing — the per-sample walk keeps the ramp exact even when it
        // completes mid-block.
        auto* const* channels = buffer.getArrayOfWritePointers();
        for (int i = 0; i < numSamples; ++i) {
            const float gain = smoothedOutputLevel.getNextValue();
            for (int ch = 0; ch < numChannels; ++ch)
                channels[ch][i] *= gain;
        }
    }

    juce::AudioParameterBool* bypassedParam = nullptr;
    juce::AudioParameterBool* mutedParam = nullptr;
    juce::AudioParameterFloat* outputLevelParam = nullptr;

private:
    juce::String moduleName;
    // 64 bytes including the NUL — matches TimelineSnapshot::kMaxStringBytes, so the strcmp
    // against a snapshot's bindingUuid compares two identically-truncated copies. A uuid is 36.
    char nodeUuid_[64] = {};
    std::atomic<bool> nodeUuidSet_{false};
    std::unique_ptr<VisualBuffer> visualBuffer;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedOutputLevel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ModuleBase)
};

// Look a parameter up by paramID instead of by position. Parameter order is not part of
// a module's contract — adding one (e.g. addOutputLevelParameter) silently repoints any
// getParameters()[n] index. Returns nullptr when the processor has no such parameter.
inline juce::RangedAudioParameter* findParameterByID(juce::AudioProcessor* processor, const juce::String& paramID) {
    if (processor == nullptr)
        return nullptr;
    for (auto* param : processor->getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(param))
            if (ranged->paramID == paramID)
                return ranged;
    return nullptr;
}
