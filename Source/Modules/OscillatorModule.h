#pragma once

#include "FrequencyText.h"
#include "GateEdges.h"
#include "ModuleBase.h"
#include "Oscillator/HeldNotes.h"
#include "Oscillator/PitchGlide.h"
#include "ParameterText.h"
#include "UnisonVoicing.h"
#include <array>
#include <cmath>

class OscillatorModule : public ModuleBase {
public:
    // -------------------------------------------------------------------------
    // Channel map
    //
    // Inputs (28): mono mode puts jack j on raw ch j for j 0-6 (Pitch, Waveform, Octave, Coarse,
    // Fine, Level, Pan); poly mode fans Pitch across ch0-7 and puts the shared mod-CV block at
    // kPolyModCVBase, so jack j (1 <= j <= 6) lands on kPolyModCVBase + j - 1. Pan took the
    // channels that were already declared and unused — ch6 in mono, ch13 in poly — so every
    // older CV routing keeps its raw channel.
    //
    // Unison, Detune, Pulse Width, Glide, Blend and Width are appended AFTER the poly shared-CV block, at the
    // SAME raw channel in both voice modes (ch14-19) — they are global params, not per-voice, so
    // there is no separate poly variant to place. ch14-19 alias the Audio R output block's first
    // channels (kRightBase..kRightBase+5): exactly the same shared-channel pattern ch0 already
    // uses for Pitch CV in vs. Audio L out, so it is safe on the same condition — the CV must be
    // cached before this module writes any output to that channel (see readGlobalParamCV).
    //
    // Gate is appended last, at the SAME raw channel in both voice modes: mono reads one channel at
    // kGateBase, poly an eight-wide fan at kGateBase..kGateBase+7, one per voice. A rising edge restarts
    // that voice's phase at Phase/Rand on its own sample, so a held pitch CV (what Poly MIDI leaves after a
    // note-off) still restarts on the next note. The fan's first two channels alias Audio R's last two
    // (voices 6 and 7), so it is scanned before any write, like ch14-19.
    //
    // Outputs: Audio L on the voice block (ch0, or ch0-7 in poly) and Audio R on a dedicated
    // block at kRightBase. R deliberately does NOT live on ch1: that is the Waveform CV input,
    // and relabelling it would break both the CV routing and every saved patch using it.
    // -------------------------------------------------------------------------
    static constexpr int kNumVoices = 8;
    static constexpr int kJackPan = 6; // last of the original jacks
    static constexpr int kJackUnison = 7;
    static constexpr int kJackDetune = 8;
    static constexpr int kJackPulseWidth = 9;
    static constexpr int kJackGlide = 10;
    static constexpr int kJackBlend = 11;
    static constexpr int kJackWidth = 12;
    static constexpr int kJackGate = 13;
    static constexpr int kNumJacks = kJackGate + 1;           // Pitch..Gate
    static constexpr int kPolyModCVBase = kNumVoices;         // poly shared-CV block start
    static constexpr int kGateBase = 20;                      // mono: one channel; poly: 8-wide fan
    static constexpr int kNumInputs = kGateBase + kNumVoices; // 8 pitch + 6 shared mod CV + 6 global CV + 8 gate
    static constexpr int kUnisonCVChannel = 14;               // same raw channel, mono and poly
    static constexpr int kDetuneCVChannel = 15;
    static constexpr int kPulseWidthCVChannel = 16;
    static constexpr int kGlideCVChannel = 17;
    static constexpr int kBlendCVChannel = 18;
    static constexpr int kWidthCVChannel = 19;
    // Audio R's OUTPUT channel index. Deliberately a LITERAL, not derived from kNumInputs: it is a
    // different JUCE channel-index space (output channels vs. input channels), and growing
    // kNumInputs must never move it — every patch already routed FROM Audio R depends on raw
    // output channel 14 staying 14.
    static constexpr int kRightBase = 14;
    // Outputs must stay at or above every input channel (the Gate fan ends at kGateBase + 7): JUCE only
    // copies a shared input buffer for channels below the output count, and the bypass/mute clear would
    // otherwise wipe a Poly MIDI gate that also feeds the ADSR.
    static constexpr int kNumOutputs = kGateBase + kNumVoices; // 28
    static_assert(kNumOutputs >= kNumInputs, "outputs must cover every input channel");

    /** Raw channel carrying jack `jack`'s CV, for the current voice mode. Only valid for the
        original Pitch..Pan jacks (1 <= jack <= kJackPan) — Unison/Detune use the fixed
        kUnisonCVChannel/kDetuneCVChannel constants instead, the same in both voice modes. */
    static constexpr int modCVChannelFor(int jack, bool poly) { return poly ? (kPolyModCVBase + jack - 1) : jack; }

    OscillatorModule()
        // 28 in: 8 per-voice pitch CV (0-7) + 6 shared mod CV (8-13) + Unison/Detune/Pulse Width/Glide/
        // Blend/Width CV (14-19) + Gate (20-27).
        // 28 out: Audio L on 0-7, silent pass-throughs on 8-13, Audio R on 14-21, silent on 22-27. Declaring outputs
        // ABOVE every CV input channel below kRightBase is what makes JUCE copy shared-mod-CV input
        // buffers when they fan out to several downstream nodes, so our post-render clear cannot
        // corrupt them — see the processPolyMode clear note. ch14-19 are the one exception (they
        // alias Audio R, see the channel-map comment above): those are cached before any write,
        // exactly like ch0's Pitch-CV/Audio-L sharing. Do NOT reduce kNumOutputs below kNumInputs.
        //
        // StereoAudio::Declared: with 28 outputs the Auto shape test cannot see the stereo pair —
        // Audio R is the kRightBase block, not ch1. Ships SPLIT.
        : ModuleBase("Oscillator", kNumInputs, kNumOutputs, StereoAudio::Declared) {
        addParameter(waveformParam = new juce::AudioParameterChoice("waveform", "Waveform",
                                                                    {"Sine", "Square", "Saw", "Triangle"}, 0));
        addParameter(octaveParam = new juce::AudioParameterInt(juce::ParameterID("octave", 1), "Octave", -4, 4, 0));
        addParameter(coarseParam = new juce::AudioParameterInt(juce::ParameterID("coarse", 1), "Coarse", -12, 12, 0));
        addParameter(fineParam = new juce::AudioParameterFloat("fine", "Fine", -100.0f, 100.0f, 0.0f));
        addParameter(levelParam = new juce::AudioParameterFloat("level", "Level", 0.0f, 1.0f, 1.0f));
        addParameter(polyParam = new juce::AudioParameterBool("poly", "Poly", false));
        addParameter(unisonParam = new juce::AudioParameterInt(juce::ParameterID("unison", 1), "Unison", 1, 8, 1));
        // Detune is stored in cents; the skew only gives the small, musical amounts more knob travel.
        juce::NormalisableRange<float> detuneRange(0.0f, 100.0f);
        detuneRange.setSkewForCentre(25.0f);
        addParameter(detuneParam = new juce::AudioParameterFloat("detune", "Detune", detuneRange, 0.0f,
                                                                 synth::centsAttributes()));
        addParameter(panParam = new juce::AudioParameterFloat("pan", "Pan", -1.0f, 1.0f, 0.0f));
        // Pulse Width is the Square's duty cycle in percent (50 = today's square); Glide is the
        // portamento time in ms (0 = off, today's behaviour).
        addParameter(pulseWidthParam = new juce::AudioParameterFloat("pulseWidth", "Pulse Width",
                                                                     juce::NormalisableRange<float>(5.0f, 95.0f), 50.0f,
                                                                     synth::percentAttributes()));
        addParameter(glideParam =
                         new juce::AudioParameterFloat("glide", "Glide", juce::NormalisableRange<float>(0.0f, 2000.0f),
                                                       0.0f, synth::millisecondsAttributes()));
        // Dual I/O comes from the ctor's StereoAudio::Declared above, defaulting to split: this
        // module is stereo, so showing both legs is the honest out-of-the-box state. The Preferences
        // default overrides it for newly dropped modules.
        addMuteParameter();
        // Appended after the mute parameter so no earlier parameter moves. Phase and Rand are the
        // start phase of each unison voice on a new note; Blend and Width shape the unison stack.
        addParameter(phaseParam =
                         new juce::AudioParameterFloat("phase", "Phase", juce::NormalisableRange<float>(0.0f, 360.0f),
                                                       0.0f, synth::degreesAttributes()));
        addParameter(randomPhaseParam = new juce::AudioParameterFloat("randomPhase", "Rand",
                                                                      juce::NormalisableRange<float>(0.0f, 100.0f),
                                                                      0.0f, synth::percentAttributes()));
        addParameter(blendParam = new juce::AudioParameterFloat("blend", "Blend", 0.0f, 1.0f, 1.0f));
        addParameter(widthParam = new juce::AudioParameterFloat("width", "Width", 0.0f, 1.0f, 0.0f));
        // Mono voice feel. Glide Mode "Scaled" makes Glide the time per octave; Glide Curve "Exponential" is
        // fast at the start and slow at the end; Legato (mono only) glides and keeps the phase only for
        // overlapping notes.
        addParameter(glideModeParam =
                         new juce::AudioParameterChoice("glideMode", "Glide Mode", {"Always", "Scaled"}, 0));
        addParameter(glideCurveParam =
                         new juce::AudioParameterChoice("glideCurve", "Glide Curve", {"Linear", "Exponential"}, 0));
        addParameter(legatoParam = new juce::AudioParameterBool("legato", "Legato", false));
        // Rate mode: "Fixed" runs the oscillator at Fixed rate (Hz) whatever note is played, so it can serve as an
        // audio-rate modulation source.
        addParameter(rateModeParam = new juce::AudioParameterChoice("rateMode", "Rate", {"Follow notes", "Fixed"}, 0));
        juce::NormalisableRange<float> fixedRateRange(0.1f, 2000.0f);
        fixedRateRange.setSkewForCentre(20.0f);
        addParameter(fixedRateParam = new juce::AudioParameterFloat("fixedRate", "Fixed rate", fixedRateRange, 50.0f,
                                                                    synth::frequencyAttributes()));
        enableVisualBuffer(true);
    }

    void prepareToPlay(double sampleRate, int samplesPerBlock) override {
        juce::ignoreUnused(samplesPerBlock);
        currentSampleRate = sampleRate;

        const float initFreq = baseFrequencyHz();

        for (int v = 0; v < MAX_VOICES; ++v) {
            voices[v].smoothedFreq.reset(sampleRate, 0.005);
            voices[v].smoothedFreq.setCurrentAndTargetValue(initFreq);
        }

        // Level is a straight output gain, so an automated step is a click. 10 ms is the same
        // anti-click ramp ModuleBase::prepareOutputLevel uses. Snapped to the current knob value
        // so a static render is bit-identical to the un-smoothed version.
        smoothedLevel.reset(sampleRate, 0.01);
        smoothedLevel.setCurrentAndTargetValue(levelParam->get());
        smoothedPan.reset(sampleRate, 0.01);
        smoothedPan.setCurrentAndTargetValue(panParam->get());
        smoothedPulseWidth.reset(sampleRate, 0.01);
        smoothedPulseWidth.setCurrentAndTargetValue(pulseWidthParam->get() * 0.01f);
        smoothedBlend.reset(sampleRate, 0.01);
        smoothedBlend.setCurrentAndTargetValue(blendParam->get());
        smoothedWidth.reset(sampleRate, 0.01);
        smoothedWidth.setCurrentAndTargetValue(widthParam->get());
        for (auto& v : voices) {
            v.glide.reset();
            v.active = false;
            v.restartFadeRemaining = 0;
        }
        resetNoteState();
    }

    void processModuleBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override {
        // Held notes are tracked whatever the state, so a note released while muted is not left held.
        scanMidi(midiMessages, buffer.getNumSamples(), /*audible*/ !isBypassed() && !isMuted());

        // Two separate branches, per docs/architecture/module-base.md#bypassmute-contract. A pure
        // source has no dry audio path to pass through, so bypass clears here rather than
        // returning early — but it stays its own branch so the two cases can never be conflated.
        if (isBypassed()) {
            buffer.clear();
            return;
        }

        if (isMuted()) {
            buffer.clear();
            return;
        }

        if (polyParam->get())
            processPolyMode(buffer, buffer.getNumSamples());
        else
            processMonoMode(buffer, midiMessages);
        clearPendingRestarts();
    }

    /** The held note's pitch with the Octave/Coarse/Fine knobs, whatever the Rate mode. */
    float getTargetFrequency() const;
    /** The frequency the knobs set before any CV: the note's pitch, or in Fixed rate mode the Fixed rate. */
    float baseFrequencyHz() const;
    /** True while the Rate mode is Fixed. */
    bool isFixedRate() const { return rateModeParam->getIndex() == 1; }
    /** The lowest frequency a voice renders at in the current Rate mode. */
    float minRenderHz() const;

    std::vector<ModulationTarget> getModulationTargets() const override {
        if (polyParam->get())
            return {{"Waveform", 8, "waveform"},
                    {"Octave", 9, "octave"},
                    {"Coarse", 10, "coarse"},
                    {"Fine", 11, "fine"},
                    {"Level", 12, "level"},
                    {"Pan", 13, "pan"},
                    {"Unison", kUnisonCVChannel, "unison"},
                    {"Detune", kDetuneCVChannel, "detune"},
                    {"Pulse Width", kPulseWidthCVChannel, "pulseWidth"},
                    {"Glide", kGlideCVChannel, "glide"},
                    {"Blend", kBlendCVChannel, "blend"},
                    {"Width", kWidthCVChannel, "width"}};
        return {{"Pitch", 0},
                {"Waveform", 1, "waveform"},
                {"Octave", 2, "octave"},
                {"Coarse", 3, "coarse"},
                {"Fine", 4, "fine"},
                {"Level", 5, "level"},
                {"Pan", 6, "pan"},
                {"Unison", kUnisonCVChannel, "unison"},
                {"Detune", kDetuneCVChannel, "detune"},
                {"Pulse Width", kPulseWidthCVChannel, "pulseWidth"},
                {"Glide", kGlideCVChannel, "glide"},
                {"Blend", kBlendCVChannel, "blend"},
                {"Width", kWidthCVChannel, "width"}};
    }
    juce::String getInputPortLabel(int i) const override {
        const juce::String labels[] = {"Pitch",  "Waveform", "Octave",      "Coarse", "Fine",  "Level", "Pan",
                                       "Unison", "Detune",   "Pulse Width", "Glide",  "Blend", "Width", "Gate"};
        return (i >= 0 && i < kNumJacks) ? labels[i] : ModuleBase::getInputPortLabel(i);
    }
    juce::String getOutputPortLabel(int i) const override { return splitAudioLabel(i); }
    int getVisibleInputPortCount() const override { return kNumJacks; }
    int getVisibleOutputPortCount() const override { return splitAudioJackCount(); }
    int rightAudioLegChannel() const override { return kRightBase; }
    int polyGateInputChannel() const override { return kGateBase; }

    // processBlock consumes note-on for the mono-mode MIDI pitch fallback (see the loop at the
    // top of processBlock) but never writes to the MIDI buffer.
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }

    ModulationCategory getModulationCategory() const override { return ModulationCategory::Oscillator; }
    ModuleType getModuleType() const override { return ModuleType::Oscillator; }

    LogicalPort mapInputChannel(int raw) const override {
        LogicalPort p;
        if (polyParam->get()) {
            // Poly mode: raw 0-7 = per-voice Pitch fan; raw 8-12 = shared ModCV
            if (raw >= 0 && raw <= 7) {
                p.visibleJackIndex = 0;
                p.role = PortRole::Pitch;
                p.isPolyGroupHead = (raw == 0);
                p.polyVoiceSpan = (raw == 0) ? 8 : 1;
                return p;
            }
            if (raw == 8) {
                p.visibleJackIndex = 1;
                p.role = PortRole::ModCV;
                p.isPolyGroupHead = true;
                p.polyVoiceSpan = 1;
                return p;
            }
            if (raw == 9) {
                p.visibleJackIndex = 2;
                p.role = PortRole::ModCV;
                p.isPolyGroupHead = true;
                p.polyVoiceSpan = 1;
                return p;
            }
            if (raw == 10) {
                p.visibleJackIndex = 3;
                p.role = PortRole::ModCV;
                p.isPolyGroupHead = true;
                p.polyVoiceSpan = 1;
                return p;
            }
            if (raw == 11) {
                p.visibleJackIndex = 4;
                p.role = PortRole::ModCV;
                p.isPolyGroupHead = true;
                p.polyVoiceSpan = 1;
                return p;
            }
            if (raw == 12) {
                p.visibleJackIndex = 5;
                p.role = PortRole::ModCV;
                p.isPolyGroupHead = true;
                p.polyVoiceSpan = 1;
                return p;
            }
            if (raw == modCVChannelFor(kJackPan, /*poly*/ true)) { // ch13
                p.visibleJackIndex = kJackPan;
                p.role = PortRole::ModCV;
                p.isPolyGroupHead = true;
                p.polyVoiceSpan = 1;
                return p;
            }
            if (raw >= kGateBase && raw < kGateBase + kNumVoices) { // per-voice Gate fan
                p.visibleJackIndex = kJackGate;
                p.role = PortRole::Gate;
                p.isPolyGroupHead = (raw == kGateBase);
                p.polyVoiceSpan = (raw == kGateBase) ? kNumVoices : 1;
                return p;
            }
        } else {
            // Mono mode: raw 0-6 = ModCV jacks 0-6 (Pitch..Pan). Deliberately kJackPan + 1, not
            // kNumJacks: Unison/Detune (jacks 7-8) do NOT live at raw 7-8 in mono, they share
            // kUnisonCVChannel/kDetuneCVChannel with poly (handled below).
            if (raw >= 0 && raw < kJackPan + 1) {
                p.visibleJackIndex = raw;
                p.role = PortRole::ModCV;
                p.isPolyGroupHead = true;
                p.polyVoiceSpan = 1;
                return p;
            }
            if (raw == modCVChannelFor(kJackPan, /*poly*/ true)) {
                // Mono raw 13 is unused; left to the default it would head the Gate jack as an audio input.
                p.visibleJackIndex = kJackGate;
                p.role = PortRole::Gate;
                p.isPolyGroupHead = false;
                p.polyVoiceSpan = 1;
                return p;
            }
            if (raw == kGateBase) { // mono Gate is one channel; raw 21-27 fall through
                p.visibleJackIndex = kJackGate;
                p.role = PortRole::Gate;
                p.isPolyGroupHead = true;
                p.polyVoiceSpan = 1;
                return p;
            }
        }
        // Unison/Detune/Pulse Width/Glide/Blend/Width CV: same raw channel in both voice modes (see the
        // class-level channel-map comment) — checked after the mode-specific blocks so neither can
        // shadow it.
        struct GlobalJack {
            int channel;
            int jack;
        };
        static constexpr GlobalJack kGlobalJacks[] = {
            {kUnisonCVChannel, kJackUnison}, {kDetuneCVChannel, kJackDetune}, {kPulseWidthCVChannel, kJackPulseWidth},
            {kGlideCVChannel, kJackGlide},   {kBlendCVChannel, kJackBlend},   {kWidthCVChannel, kJackWidth}};
        for (const auto& jack : kGlobalJacks) {
            if (raw == jack.channel) {
                p.visibleJackIndex = jack.jack;
                p.role = PortRole::ModCV;
                p.isPolyGroupHead = true;
                p.polyVoiceSpan = 1;
                return p;
            }
        }
        return ModuleBase::mapInputChannel(raw);
    }

    /** Audio L lives on the voice block (ch0, or ch0-7 in poly) and Audio R on a dedicated block
        starting at kRightBase, so neither output ever collides with a mod-CV input channel. Poly
        fans both blocks eight wide, which is what carries a panned poly chord into the Voice Mixer
        as a stereo pair. */
    LogicalPort mapOutputChannel(int raw) const override {
        const bool poly = polyParam->get();
        const int span = poly ? kNumVoices : 1;

        LogicalPort p;
        p.role = PortRole::Audio;
        p.polyVoiceSpan = 1;

        if (raw >= 0 && raw < span) {
            p.visibleJackIndex = 0;
            p.isPolyGroupHead = (raw == 0);
            p.polyVoiceSpan = (raw == 0) ? span : 1;
            return p;
        }
        // Collapsed (Dual I/O off): the right block still renders, it is simply not exposed as a
        // jack, so nothing can be patched to it. Falls through to the non-head case below.
        if (isDualIO() && raw >= kRightBase && raw < kRightBase + span) {
            p.visibleJackIndex = 1;
            p.isPolyGroupHead = (raw == kRightBase);
            p.polyVoiceSpan = (raw == kRightBase) ? span : 1;
            return p;
        }

        // Silent pass-through channels (the mod-CV block, and voices 1-7 in mono): addressable but
        // never a poly-bus head. Falling through to ModuleBase here would be wrong — its default
        // clamps the raw channel onto a visible jack index, so mono ch1 would advertise itself as
        // the head of the Audio R jack and a wire could be drawn off the Waveform CV channel.
        p.visibleJackIndex = 0;
        p.isPolyGroupHead = false;
        return p;
    }

    bool isAutoPromotableModTarget(int dstChannel) const override {
        if (polyParam->get())
            return false;
        return ModuleBase::isAutoPromotableModTarget(dstChannel);
    }

private:
    // -------------------------------------------------------------------------
    // Constants
    // -------------------------------------------------------------------------
    static constexpr int MAX_VOICES = 8;
    static constexpr int MAX_UNISON = 8;
    static constexpr int CROSSFADE_SAMPLES = 64;

    // -------------------------------------------------------------------------
    // Per-voice state
    // -------------------------------------------------------------------------
    struct UnisonOsc {
        float phase = 0.0f;
    };

    struct VoiceState {
        UnisonOsc unisonOscs[MAX_UNISON];
        UnisonOsc fadeOscs[MAX_UNISON]; ///< the phases before the last restart, running out
        int restartFadeRemaining = 0;
        bool active = false; ///< poly: was this voice sounding last block?
        juce::SmoothedValue<float> smoothedFreq;
        synth::PitchGlide glide;
        GateEdges gateEdges; ///< this block's rising Gate edges, each a note that restarts the phases
        float lastMidiNote = 69.0f;
        int previousWaveform = 0;
        int fadingFromWaveform = 0;
        int crossfadeSamplesRemaining = 0;
    };

    VoiceState voices[MAX_VOICES];

    /** Per-block smoothed ramps, materialised ONCE before the voice loop: poly mode renders one
        voice after another, so pulling a smoother inside that loop would advance it eight times
        per block. Level and Pulse Width are read per sample, Pan by placeVoiceInStereo. */
    void fillLevelRamp(int len) {
        smoothedLevel.setTargetValue(levelParam->get());
        for (int i = 0; i < len; ++i)
            levelRamp[(size_t)i] = smoothedLevel.getNextValue();
    }

    void fillPanRamp(int len) {
        len = std::min(len, (int)panRamp.size()); // same clamp contract as the level ramp's caller
        smoothedPan.setTargetValue(panParam->get());
        for (int i = 0; i < len; ++i)
            panRamp[(size_t)i] = smoothedPan.getNextValue();
    }

    /** `pulseWidthPercent` is the CV-modulated value from readGlobalParamCV; the ramp holds the
        duty cycle as a 0..1 fraction. */
    void fillPulseWidthRamp(int len, float pulseWidthPercent) {
        smoothedPulseWidth.setTargetValue(pulseWidthPercent * 0.01f);
        for (int i = 0; i < len; ++i)
            pulseWidthRamp[(size_t)i] = smoothedPulseWidth.getNextValue();
    }

    /** Whole-block "is anything patched here" guard shared by both voice modes. */
    static bool channelHasSignal(const juce::AudioBuffer<float>& buffer, int ch, int numSamples) {
        return ch < buffer.getNumChannels() && hasAnyNonZero(buffer.getReadPointer(ch), numSamples);
    }

    /** The six global (not per-voice) params after their CV, read once per block. Their CV
        channels (ch14-19) alias the Audio R output block, so a caller MUST read this before it
        clears or writes those channels. */
    struct GlobalParamCV {
        int unisonCount;
        float detuneCents;
        float pulseWidthPercent;
        float glideSeconds;
        float blend;
        float width;
    };
    GlobalParamCV readGlobalParamCV(const juce::AudioBuffer<float>& buffer, int numSamples) const;

    // ---- Unison mix (OscillatorModuleVoicing.cpp) ----
    /** Per-voice weights of the unison stack for one block, ramped linearly from the block's start
        to its end so an automated Blend or Width glides. At Blend 1 / Width 0 every left weight is
        exactly 1 and the right side is unused. */
    struct UnisonMix {
        float leftStart[MAX_UNISON]{};
        float leftStep[MAX_UNISON]{};
        float rightStart[MAX_UNISON]{};
        float rightStep[MAX_UNISON]{};
        float sumStart = 1.0f; ///< sum of the squared Blend gains: the stack is divided by its square root
        float sumStep = 0.0f;
        bool stereo = false; ///< Width above 0: the stack renders to both legs
    } unisonMix;
    void prepareUnisonMix(int unisonCount, int numSamples, float blend, float width);
    void fillUnisonRatios(float* ratios, int unisonCount, float detuneCents) const;

    // ---- Notes and restart (OscillatorModuleNotes.cpp) ----
    /** Reads the block's MIDI: tracks held notes and, when `audible`, the pitch and the sample offsets of
        the note-ons that restart the voice. Never allocates. */
    void scanMidi(const juce::MidiBuffer& midi, int numSamples, bool audible);
    /** The single gate for restarting phase on a note-on; `overlapping` is true while another note is held. */
    bool shouldRestartOnNoteOn(bool overlapping) const;
    /** Restarts every unison oscillator of `voice`; with `fadeFromOld` the previous phases run out over
     * CROSSFADE_SAMPLES. */
    void restartVoice(VoiceState& voice, bool fadeFromOld);
    void resetNoteState();
    synth::GlideTime glideTime() const {
        return glideModeParam->getIndex() == 1 ? synth::GlideTime::Scaled : synth::GlideTime::Always;
    }
    synth::GlideCurve glideCurve() const {
        return glideCurveParam->getIndex() == 1 ? synth::GlideCurve::Exponential : synth::GlideCurve::Linear;
    }
    void clearPendingRestarts();
    /** Adds a restart at sample `offset`, keeping `restartOffsets` sorted and free of duplicates; past
        kMaxRestartsPerBlock a further offset is dropped. */
    void addRestartOffset(int offset);

    void processMonoMode(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages);
    void processPolyMode(juce::AudioBuffer<float>& buffer, int numSamples);
    /** `rightRendered`: Audio R already holds the stack's right leg, so Pan scales both legs in place. */
    void placeVoiceInStereo(juce::AudioBuffer<float>& buffer, int voiceIndex, int numSamples, const float* panCV,
                            int panCVLength, bool rightRendered);

    /** Poly helpers (OscillatorModulePoly.cpp). */
    struct PolyModCVPresence {
        bool waveform = false, octave = false, coarse = false, fine = false, level = false;
    } polyCV;
    void cachePolyModCV(const juce::AudioBuffer<float>& buffer, int numSamples);
    struct PolyVoiceSettings {
        int unisonCount;
        float detuneCents;
        float glideSeconds;
    };
    /** `outputR` is the voice's Audio R channel, or null when the stack renders to the left leg only. */
    void renderPolyVoice(int voice, float* output, float* outputR, int numSamples, float basePitchHz,
                         const PolyVoiceSettings& set);
    void renderPolyVoiceSteady(int voice, float* output, float* outputR, int numSamples, float basePitchHz,
                               const PolyVoiceSettings& set);
    void renderPolyVoiceModulated(int voice, float* output, float* outputR, int numSamples, float basePitchHz,
                                  const PolyVoiceSettings& set, bool gliding);

    // -------------------------------------------------------------------------
    // Unison stack
    // -------------------------------------------------------------------------
    struct StackSample {
        float left = 0.0f;
        float right = 0.0f;
    };

    /** Everything one sample of a stack depends on besides its own phases. */
    struct StackContext {
        const float* ratio; ///< per-voice frequency ratio (detune)
        int count;
        float baseDt; ///< phase increment before detune
        int waveform;
        int fadingFromWaveform;
        float waveformAlpha; ///< above 0 while crossfading from fadingFromWaveform
        float pulseWidth;
    };

    /** Weighted sum of one stack at sample `i` of the block, advancing every phase. Not yet normalised. */
    StackSample sumStack(UnisonOsc* oscs, const StackContext& c, int i) const {
        StackSample s;
        for (int u = 0; u < c.count; ++u) {
            const float uniDt = c.baseDt * c.ratio[u];
            float g;
            if (c.waveformAlpha > 0.0f) {
                g = generateSample(c.fadingFromWaveform, oscs[u].phase, uniDt, c.pulseWidth) * c.waveformAlpha +
                    generateSample(c.waveform, oscs[u].phase, uniDt, c.pulseWidth) * (1.0f - c.waveformAlpha);
            } else {
                g = generateSample(c.waveform, oscs[u].phase, uniDt, c.pulseWidth);
            }
            s.left += (unisonMix.leftStart[u] + unisonMix.leftStep[u] * (float)i) * g;
            if (unisonMix.stereo)
                s.right += (unisonMix.rightStart[u] + unisonMix.rightStep[u] * (float)i) * g;
            oscs[u].phase += uniDt;
            if (oscs[u].phase >= 1.0f)
                oscs[u].phase -= 1.0f;
        }
        return s;
    }

    /** One normalised sample of `voice`'s stack, blending in the run-out phases after a restart. */
    StackSample renderStack(VoiceState& voice, const StackContext& c, int i) const {
        StackSample s = sumStack(voice.unisonOscs, c, i);
        if (voice.restartFadeRemaining > 0) {
            const StackSample old = sumStack(voice.fadeOscs, c, i);
            const float alpha = (float)voice.restartFadeRemaining / (float)CROSSFADE_SAMPLES;
            s.left = old.left * alpha + s.left * (1.0f - alpha);
            s.right = old.right * alpha + s.right * (1.0f - alpha);
            --voice.restartFadeRemaining;
        }
        const float divisor = std::sqrt(unisonMix.sumStart + unisonMix.sumStep * (float)i);
        s.left /= divisor;
        s.right /= divisor;
        return s;
    }

    // -------------------------------------------------------------------------
    // Waveform generators
    // -------------------------------------------------------------------------
    /** `pulseWidth` (0..1 duty cycle) shapes the Square only. */
    float generateSample(int waveform, float phase, float dt, float pulseWidth) const {
        switch (waveform) {
        case 0:
            return generateSine(phase);
        case 1:
            return generateSquare(phase, dt, pulseWidth);
        case 2:
            return generateSaw(phase, dt);
        case 3:
            return generateTriangle(phase, dt);
        default:
            return 0.0f;
        }
    }

    static float generateSine(float phase) { return std::sin(phase * juce::MathConstants<float>::twoPi); }

    // At pulseWidth 0.5 both edges are exactly where the plain square put them.
    static float generateSquare(float phase, float dt, float pulseWidth) {
        float sample = phase < pulseWidth ? 1.0f : -1.0f;
        sample += polyBlep(phase, dt);
        sample -= polyBlep(std::fmod(phase + (1.0f - pulseWidth), 1.0f), dt);
        return sample;
    }

    static float generateSaw(float phase, float dt) {
        float sample = 2.0f * phase - 1.0f;
        sample -= polyBlep(phase, dt);
        return sample;
    }

    static float generateTriangle(float phase, float dt) {
        float sample = 4.0f * std::abs(phase - 0.5f) - 1.0f;
        // PolyBLAMP: integrated polyBLEP applied at discontinuities (phase=0 and phase=0.5)
        sample += polyBlamp(phase, dt);
        sample -= polyBlamp(std::fmod(phase + 0.5f, 1.0f), dt);
        return sample;
    }

    static float polyBlamp(float t, float dt) {
        if (t < dt) {
            float n = t / dt;
            // Third-order polynomial: integrated polyBLEP
            return -dt * (n * n * n / 3.0f - n * n / 2.0f + 1.0f / 6.0f) * 4.0f;
        }
        if (t > 1.0f - dt) {
            float n = (t - 1.0f) / dt;
            return dt * (n * n * n / 3.0f + n * n / 2.0f + 1.0f / 6.0f) * 4.0f;
        }
        return 0.0f;
    }

    static float polyBlep(float t, float dt) {
        if (t < dt) {
            float n = t / dt;
            return n + n - n * n - 1.0f;
        }
        if (t > 1.0f - dt) {
            float n = (t - 1.0f) / dt;
            return n * n + n + n + 1.0f;
        }
        return 0.0f;
    }

    // -------------------------------------------------------------------------
    // Member variables
    // -------------------------------------------------------------------------
    double currentSampleRate = 44100.0;

    // Pre-allocated buffers to avoid heap allocation in audio thread
    std::array<std::array<float, 4096>, MAX_VOICES> pitchCVCache{};
    std::array<float, 4096> waveformCVCache{};
    std::array<float, 4096> octaveCVCache{};
    std::array<float, 4096> coarseCVCache{};
    std::array<float, 4096> fineCVCache{};
    std::array<float, 4096> levelCVCache{};
    std::array<float, 4096> levelRamp{};
    std::array<float, 4096> panRamp{};
    std::array<float, 4096> pulseWidthRamp{};
    std::array<float, 4096> panCVCache{};

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedLevel;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedPan;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedPulseWidth;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedBlend;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedWidth;

    // Note tracking. Fixed-size so the audio thread never allocates.
    static constexpr int kMaxRestartsPerBlock = 16;
    synth::HeldNotes heldNotes;
    std::array<int, kMaxRestartsPerBlock> restartOffsets{}; ///< sample offsets of this block's restarting note-ons
    int numRestarts = 0;
    bool noteJumps = false;            ///< mono legato: the newest note-on was not overlapping, so its pitch jumps
    bool voice0RestartPending = false; ///< poly: a MIDI note-on restarts voice 0 at the next block
    juce::Random phaseRandom{0x5EED1234};

    juce::AudioParameterChoice* waveformParam = nullptr;
    juce::AudioParameterInt* octaveParam = nullptr;
    juce::AudioParameterInt* coarseParam = nullptr;
    juce::AudioParameterFloat* fineParam = nullptr;
    juce::AudioParameterFloat* levelParam = nullptr;
    juce::AudioParameterBool* polyParam = nullptr;
    juce::AudioParameterInt* unisonParam = nullptr;
    juce::AudioParameterFloat* detuneParam = nullptr;
    juce::AudioParameterFloat* panParam = nullptr;
    juce::AudioParameterFloat* pulseWidthParam = nullptr;
    juce::AudioParameterFloat* glideParam = nullptr;
    juce::AudioParameterFloat* phaseParam = nullptr;
    juce::AudioParameterFloat* randomPhaseParam = nullptr;
    juce::AudioParameterFloat* blendParam = nullptr;
    juce::AudioParameterFloat* widthParam = nullptr;
    juce::AudioParameterChoice* glideModeParam = nullptr;
    juce::AudioParameterChoice* glideCurveParam = nullptr;
    juce::AudioParameterBool* legatoParam = nullptr;
    juce::AudioParameterChoice* rateModeParam = nullptr;
    juce::AudioParameterFloat* fixedRateParam = nullptr;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OscillatorModule)
};
