#pragma once

#include "VisualBuffer.h"
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
    RingModulator,
    Noise,
    Math,
    Sampler,
    Wavetable,
    MacroControl,
    SampleHold,
    EnvelopeFollower,
    Comparator
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

    // Opt-in stereo I/O jack layout for modules that process a fixed L/R pair on raw ch0/ch1.
    //
    // Bus layout stays 2+ channels for the module's lifetime (JUCE cannot renegotiate without
    // dropping graph connections). Dual I/O only changes *visibility* and logical-port mapping:
    //   Dual on  — separate Left / Right jacks (legacy FX layout)
    //   Dual off — one "Audio" jack that fans both raw channels (polyVoiceSpan == 2)
    // Call before addOutputLevelParameter() / addMuteParameter() so Level stays last among
    // value params. Default is single-jack — the common mono→FX insert case.
    void addDualIOParameter(bool defaultDual = false) {
        if (dualIOParam)
            return;
        addParameter(dualIOParam = new juce::AudioParameterBool("dualIO", "Dual I/O", defaultDual));
    }

    bool hasDualIOParameter() const { return dualIOParam != nullptr; }
    bool isDualIO() const { return dualIOParam != nullptr && dualIOParam->get(); }

    /** Visible input jacks when raw 0/1 are a stereo pair followed by `numCvInputs` ModCV jacks. */
    int stereoVisibleInputCount(int numCvInputs) const { return (isDualIO() ? 2 : 1) + numCvInputs; }
    int stereoVisibleOutputCount() const { return isDualIO() ? 2 : 1; }

    juce::String stereoInputLabel(int visibleJack, int numCvInputs, const juce::String* cvLabels) const {
        if (isDualIO()) {
            if (visibleJack == 0)
                return "Left";
            if (visibleJack == 1)
                return "Right";
            const int cv = visibleJack - 2;
            if (cv >= 0 && cv < numCvInputs)
                return cvLabels[cv];
        } else {
            if (visibleJack == 0)
                return "Audio";
            const int cv = visibleJack - 1;
            if (cv >= 0 && cv < numCvInputs)
                return cvLabels[cv];
        }
        return "In " + juce::String(visibleJack);
    }

    juce::String stereoOutputLabel(int visibleJack) const {
        if (isDualIO())
            return visibleJack == 0 ? "Left" : "Right";
        return "Audio";
    }

    // -------------------------------------------------------------------------
    // Split-block stereo (Audio R on a dedicated kRightBase block)
    //
    // The helpers above assume the stereo pair is raw ch0/ch1, which is true for the FX. The voice
    // modules cannot use that: their ch1 is a CV input (Waveform / Position / Cutoff / gain), so
    // Audio R lives on its own block above the CV inputs. That block is NOT adjacent to ch0, and a
    // collapsed jack can only fan to ADJACENT raw channels, so "Dual I/O off" on these modules
    // means "show the left leg only" rather than "one jack that feeds both legs".
    // -------------------------------------------------------------------------

    /** Raw channel carrying the right audio leg, or -1 when the module has no second leg.
        Defaults to the FX layout (ch1); split-block modules override it with their kRightBase. */
    virtual int rightAudioLegChannel() const { return hasDualIOParameter() ? 1 : -1; }

    /** True when the right leg sits on its own block rather than on ch1. Callers that want to wire
        or unwire a stereo pair must go through rightAudioLegChannel() rather than assuming ch1. */
    bool hasSplitBlockStereo() const { return rightAudioLegChannel() > 1; }

    /** Visible audio jacks for a split-block module: 2 when dual, 1 when collapsed. */
    int splitAudioJackCount() const { return isDualIO() ? 2 : 1; }

    juce::String splitAudioLabel(int visibleJack) const {
        if (!isDualIO())
            return "Audio";
        return visibleJack == 1 ? "Audio R" : "Audio L";
    }

    // -------------------------------------------------------------------------
    // Reserved jack counts
    //
    // How many jacks a side would show in the DUAL state. A collapsed module gains exactly one
    // audio jack per side that already carries audio, which is why this asks the channel map rather
    // than hard-coding per module: an Oscillator's inputs are all CV and gain nothing, while a
    // Filter gains one on each side.
    //
    // NOT currently used for layout. Reserving this much gutter would make flipping Dual I/O
    // height-neutral (nice when the Preferences default re-lays every module at once), but it also
    // makes every collapsed card a jack row TALLER than it is today — all twelve FX default to
    // collapsed, so they would each grow 20px of blank gutter and the factory preset rows would
    // need rebaking again. Left available for a follow-up that wants to make that trade.
    // -------------------------------------------------------------------------
    int getReservedInputPortCount() const {
        int count = getVisibleInputPortCount();
        if (hasDualIOParameter() && !isDualIO() && mapInputChannel(0).role == PortRole::Audio)
            ++count;
        return count;
    }

    int getReservedOutputPortCount() const {
        int count = getVisibleOutputPortCount();
        if (hasDualIOParameter() && !isDualIO() && mapOutputChannel(0).role == PortRole::Audio)
            ++count;
        return count;
    }

    /** Balance pan law shared by every stereo-capable module: centre leaves BOTH legs at unity,
        and panning attenuates only the leg you move away from. -1 is hard left, +1 hard right.

        Deliberately not equal-power. An equal-power centre sits at 1/sqrt(2), which would quieten
        every existing mono patch by 3 dB the moment a module grows a second output jack — Audio L
        has to keep carrying exactly what it carried while the module was mono. */
    static void panGains(float pan, float& gainL, float& gainR) {
        const float p = juce::jlimit(-1.0f, 1.0f, pan);
        gainL = juce::jlimit(0.0f, 1.0f, 1.0f - p);
        gainR = juce::jlimit(0.0f, 1.0f, 1.0f + p);
    }

    /** Map raw ch0/ch1 (+ trailing CV) onto Dual or collapsed Audio jacks. */
    LogicalPort mapStereoPairInput(int raw, int numCvInputs) const {
        LogicalPort p;
        if (isDualIO()) {
            if (raw == 0 || raw == 1) {
                p.visibleJackIndex = raw;
                p.role = PortRole::Audio;
                p.isPolyGroupHead = true;
                p.polyVoiceSpan = 1;
                return p;
            }
            if (raw >= 2 && raw < 2 + numCvInputs) {
                p.visibleJackIndex = raw;
                p.role = PortRole::ModCV;
                p.isPolyGroupHead = true;
                p.polyVoiceSpan = 1;
                return p;
            }
        } else {
            if (raw == 0) {
                p.visibleJackIndex = 0;
                p.role = PortRole::Audio;
                p.isPolyGroupHead = true;
                p.polyVoiceSpan = 2;
                return p;
            }
            if (raw == 1) {
                p.visibleJackIndex = 0;
                p.role = PortRole::Audio;
                p.isPolyGroupHead = false;
                p.polyVoiceSpan = 1;
                return p;
            }
            if (raw >= 2 && raw < 2 + numCvInputs) {
                p.visibleJackIndex = raw - 1;
                p.role = PortRole::ModCV;
                p.isPolyGroupHead = true;
                p.polyVoiceSpan = 1;
                return p;
            }
        }
        LogicalPort fallback;
        const int vis = stereoVisibleInputCount(numCvInputs);
        fallback.visibleJackIndex = (vis > 0) ? juce::jlimit(0, vis - 1, raw) : 0;
        fallback.role = PortRole::Other;
        fallback.isPolyGroupHead = false;
        fallback.polyVoiceSpan = 1;
        return fallback;
    }

    LogicalPort mapStereoPairOutput(int raw) const {
        LogicalPort p;
        if (isDualIO()) {
            if (raw == 0 || raw == 1) {
                p.visibleJackIndex = raw;
                p.role = PortRole::Audio;
                p.isPolyGroupHead = true;
                p.polyVoiceSpan = 1;
                return p;
            }
        } else {
            if (raw == 0) {
                p.visibleJackIndex = 0;
                p.role = PortRole::Audio;
                p.isPolyGroupHead = true;
                p.polyVoiceSpan = 2;
                return p;
            }
            if (raw == 1) {
                p.visibleJackIndex = 0;
                p.role = PortRole::Audio;
                p.isPolyGroupHead = false;
                p.polyVoiceSpan = 1;
                return p;
            }
        }
        LogicalPort fallback;
        const int vis = stereoVisibleOutputCount();
        fallback.visibleJackIndex = (vis > 0) ? juce::jlimit(0, vis - 1, raw) : 0;
        fallback.role = PortRole::Other;
        fallback.isPolyGroupHead = false;
        fallback.polyVoiceSpan = 1;
        return fallback;
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

    // Two-block variant for modules whose Audio R block sits ABOVE the mod-CV inputs on a
    // dedicated `kRightBase` block (Wavetable / Oscillator / Filter layouts) rather than on a
    // contiguous ch0/ch1 pair. Scales [0, span) and [rightBase, rightBase + span) from ONE ramp:
    // calling applyOutputLevel() twice would advance the smoother twice and leave the right leg
    // a block behind the left, which reads as the image drifting while Level moves.
    void applyOutputLevelSplit(juce::AudioBuffer<float>& buffer, int span, int rightBase) {
        if (outputLevelParam == nullptr)
            return;

        const int numSamples = buffer.getNumSamples();
        const int bufChannels = buffer.getNumChannels();
        const int leftCount = juce::jmin(span, bufChannels);
        if (numSamples == 0 || leftCount <= 0)
            return;

        const int rightCount = juce::jlimit(0, span, bufChannels - rightBase);

        smoothedOutputLevel.setTargetValue(outputLevelParam->get());

        if (!smoothedOutputLevel.isSmoothing()) {
            const float gain = smoothedOutputLevel.getCurrentValue();
            if (gain != 1.0f) {
                for (int ch = 0; ch < leftCount; ++ch)
                    buffer.applyGain(ch, 0, numSamples, gain);
                for (int ch = 0; ch < rightCount; ++ch)
                    buffer.applyGain(rightBase + ch, 0, numSamples, gain);
            }
            return;
        }

        auto* const* channels = buffer.getArrayOfWritePointers();
        for (int i = 0; i < numSamples; ++i) {
            const float gain = smoothedOutputLevel.getNextValue();
            for (int ch = 0; ch < leftCount; ++ch)
                channels[ch][i] *= gain;
            for (int ch = 0; ch < rightCount; ++ch)
                channels[rightBase + ch][i] *= gain;
        }
    }

    juce::AudioParameterBool* bypassedParam = nullptr;
    juce::AudioParameterBool* mutedParam = nullptr;
    juce::AudioParameterBool* dualIOParam = nullptr;
    juce::AudioParameterFloat* outputLevelParam = nullptr;

private:
    juce::String moduleName;
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
