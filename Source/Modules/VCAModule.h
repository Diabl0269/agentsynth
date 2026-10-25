#pragma once

#include "ModuleBase.h"
#include "Modules/ParameterText.h"
#include "Modules/SoftLimit.h"
#include <cmath>
#include <optional>

class VCAModule : public ModuleBase {
public:
    // -------------------------------------------------------------------------
    // Channel map
    //
    // Audio L: ch0 in mono, ch0-7 in poly. Gain CV: ch1 (mono) / ch8-15 (poly). Both unchanged.
    //
    // Audio R is a dedicated block at kRightBase, in AND out — a VCA is a processor, so the
    // right leg needs an input jack too. It does NOT go on ch1: that is the gain CV input.
    //
    // Legacy quirk, deliberately preserved: mono still copies ch0 onto ch1 after gating, and poly
    // still writes its voice sum to BOTH ch0 and ch1. That was the module's old "mono to stereo"
    // affordance and three tests pin it, so patches relying on it keep working. It is vestigial now
    // that there is a real right leg — prefer the Audio R jack.
    // -------------------------------------------------------------------------
    static constexpr int kNumVoices = 8;
    static constexpr int kPolyCVBase = kNumVoices;               // per-voice CV fan in poly mode
    static constexpr int kNumInputs = kPolyCVBase + kNumVoices;  // 16
    static constexpr int kRightBase = kNumInputs;                // Audio R block starts here
    static constexpr int kNumChannels = kRightBase + kNumVoices; // 24, in and out

    /** Raw head channel of audio leg `leg` (0 = L, 1 = R). */
    static constexpr int legBaseChannel(int leg) { return leg == 0 ? 0 : kRightBase; }

    VCAModule()
        // 24 in / 24 out: Audio L on 0-7, gain CV on 8-15, Audio R on 16-23. Declaring the outputs
        // above every CV input index also makes JUCE hand this node private copies of shared CV
        // buffers, so the end-of-block clear can only ever zero its own copy.
        // StereoAudio::Declared: 24 outputs, so the Auto shape test cannot see the stereo pair — the
        // right leg is the kRightBase block, and it ships SPLIT.
        : ModuleBase("VCA", kNumChannels, kNumChannels, StereoAudio::Declared) {
        addParameter(gainParam =
                         new juce::AudioParameterFloat("gain", "Gain", juce::NormalisableRange<float>(0.0f, 1.0f), 0.5f,
                                                       synth::fractionPercentAttributes()));
        addParameter(polyParam = new juce::AudioParameterBool("poly", "Poly", false));
        // Dual I/O comes from the ctor's StereoAudio::Declared above (defaults to split: this module
        // gates in stereo). Collapsed, its jack layout is the plain mono one — Audio, CV.
        addMuteParameter();
        enableVisualBuffer(true);
    }

    void prepareToPlay(double sampleRate, int samplesPerBlock) override {
        prepareOutputStage(sampleRate, samplesPerBlock);
        smoothedGain.reset(sampleRate, 0.01);
        smoothedGain.setCurrentAndTargetValue(*gainParam);
        const auto coefficient = [sampleRate](double seconds) {
            return (float)(1.0 - std::exp(-1.0 / (seconds * sampleRate)));
        };
        peakRelease = 1.0f - coefficient(0.02);
        headroomDown = coefficient(0.0015);
        headroomUp = coefficient(0.05);
        resetHeadroom();
    }

    void processModuleBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override {
        // Borrow each voice's Left into its matching Right (kRightBase + voice), sample-
        // exact, while Dual I/O is split and only Left is patched. Before the bypass/mute branches
        // below so both see a filled Right leg exactly as if the user had cabled it.
        applyLeftRightNormalling(buffer, polyParam->get() ? MAX_VOICES : 1);

        juce::ignoreUnused(midiMessages);

        int numSamples = buffer.getNumSamples();
        int numChannels = buffer.getNumChannels();
        if (numChannels == 0 || numSamples == 0)
            return;

        if (isMuted()) {
            buffer.clear();
            return;
        }

        if (isBypassed()) {
            // Dry pass-through: clear only the CV channels so mod CV does not leak downstream as
            // audio. Bounded at kRightBase — the Audio R block sits above the CV inputs and must
            // pass through untouched like Audio L.
            int cvStart = polyParam->get() ? kPolyCVBase : 1;
            for (int ch = cvStart; ch < kRightBase && ch < numChannels; ++ch)
                buffer.clear(ch, 0, numSamples);
            return;
        }

        smoothedGain.setTargetValue(*gainParam);

        if (!polyParam->get()) {
            // --- Mono mode: read CV from ch1 (legacy) or ch8 (poly layout fallback) ---
            auto* audioData = buffer.getWritePointer(0);
            const float* cvData = (numChannels > 1) ? buffer.getReadPointer(1) : nullptr;

            // If ch1 carries nothing at all this block, try ch8 (poly envelope routing). The whole
            // block is probed: an envelope that idles at 0 and fires mid-block would be missed by
            // a head-of-block probe, muting its attack and then stepping in a block later.
            if (cvData != nullptr && numChannels > 8 && !hasAnyNonZero(cvData, numSamples))
                cvData = buffer.getReadPointer(8);

            // The right leg is gated by the SAME gain ramp and the same CV, so both legs stay
            // level-matched; walking the smoother twice would leave R a block behind L. Skipped
            // when nothing is patched into Audio R, so a mono insert costs what it always did.
            const bool hasRight = numChannels > kRightBase && buffer.getRMSLevel(kRightBase, 0, numSamples) >= 1e-9f;
            float* rightData = hasRight ? buffer.getWritePointer(kRightBase) : nullptr;

            for (int s = 0; s < numSamples; ++s) {
                float cv = (cvData != nullptr) ? cvData[s] : 1.0f;
                const float g = smoothedGain.getNextValue() * cv;
                audioData[s] *= g;
                if (rightData != nullptr)
                    rightData[s] *= g;
            }

            if (numChannels > 1) {
                buffer.copyFrom(1, 0, buffer, 0, 0, numSamples);
            }
            if (auto* vb = getVisualBuffer())
                vb->pushBlock(buffer.getReadPointer(0), numSamples);
        } else {
            // --- Poly mode: 8 voices summed to stereo (ch0/ch1) ---
            // Each voice is multiplied by its envelope CV and the master gain,
            // then all voices are accumulated into a single stereo sum at unity: one voice
            // is exactly as loud as the same note through the mono path, and the sum is never
            // divided by the voice count. synth::softLimit rounds only what passes 0.9.

            if (numChannels >= 2) {
                auto* outL = buffer.getWritePointer(0);
                auto* outR = buffer.getWritePointer(1);

                // The Audio R voice block is summed into its own head at kRightBase, keeping a
                // stereo poly chord stereo. ch1 keeps carrying the legacy duplicate of the left sum.
                const bool hasRight = numChannels > kRightBase;
                float* outRightLeg = hasRight ? buffer.getWritePointer(kRightBase) : nullptr;

                for (int s = 0; s < numSamples; ++s) {
                    float gain = smoothedGain.getNextValue();
                    float sum = 0.0f;
                    float sumRight = 0.0f;
                    float cvTotal = 0.0f;
                    for (int v = 0; v < MAX_VOICES && v < numChannels; ++v) {
                        float cv = (v + MAX_VOICES < numChannels) ? buffer.getSample(v + MAX_VOICES, s) : 1.0f;
                        float audio = std::abs(buffer.getSample(v, s));
                        sum += buffer.getSample(v, s) * gain * cv;
                        if (hasRight && kRightBase + v < numChannels) {
                            audio = std::max(audio, std::abs(buffer.getSample(kRightBase + v, s)));
                            sumRight += buffer.getSample(kRightBase + v, s) * gain * cv;
                        }
                        // The voice's recent peak: up at once, down over 20 ms, so it follows the level that is
                        // really there (a silent voice, and an unpatched envelope reading 1, add nothing) whatever
                        // the block size.
                        voicePeak[v] = std::max(audio, voicePeak[v] * peakRelease);
                        cvTotal += voicePeak[v] * std::abs(cv);
                    }
                    // A chord whose voices could add past the knee is brought back by their envelope levels, not by
                    // clipping the sum: one note and quiet chords keep their unity sum.
                    const float headroom = nextHeadroom(synth::polySumHeadroom(std::abs(gain) * cvTotal));
                    float mixed = synth::softLimit(sum * headroom);
                    outL[s] = mixed;
                    outR[s] = mixed;
                    if (outRightLeg != nullptr)
                        outRightLeg[s] = synth::softLimit(sumRight * headroom);
                }

                // Zero out voice channels 2-7 so they don't leak downstream
                for (int v = 2; v < MAX_VOICES && v < numChannels; ++v)
                    buffer.clear(v, 0, numSamples);

                // Same for the right block's follower voices — its sum lives on kRightBase alone.
                for (int v = 1; v < MAX_VOICES && kRightBase + v < numChannels; ++v)
                    buffer.clear(kRightBase + v, 0, numSamples);
            }
            if (auto* vb = getVisualBuffer())
                vb->pushBlock(buffer.getReadPointer(0), numSamples);
        }

        // Clear CV channels to prevent leaking to downstream modules.
        // Exception: in mono mode we copy audio to ch1 for visual feedback, so we only clear from
        // ch2 onwards. Clearing ch1 too would be cleaner but breaks that legacy duplicate.
        // Bounded at kRightBase: running to numChannels would erase the right leg we just gated.
        for (int ch = (polyParam->get() ? kPolyCVBase : 2); ch < kRightBase && ch < numChannels; ++ch)
            buffer.clear(ch, 0, numSamples);
    }

    // The CV jack drives the Gain knob (paramId), so the card rings and knob-drops on Gain.
    std::vector<ModulationTarget> getModulationTargets() const override { return {{"CV", 1, "gain"}}; }
    /** Audio R sits next to Audio L; CV keeps its raw channel, only its visible slot moves. */
    juce::String getInputPortLabel(int i) const override {
        const int audioJacks = splitAudioJackCount();
        if (i >= 0 && i < audioJacks)
            return splitAudioLabel(i);
        return (i == audioJacks) ? "CV" : ModuleBase::getInputPortLabel(i);
    }
    juce::String getOutputPortLabel(int i) const override { return splitAudioLabel(i); }
    int getVisibleInputPortCount() const override { return splitAudioJackCount() + 1; }
    int getVisibleOutputPortCount() const override { return splitAudioJackCount(); }
    int rightAudioLegChannel() const override { return kRightBase; }
    // Audio L/R are read in place at ch0(-7)/kRightBase(+) above -- a genuine stereo
    // pair, eligible for render-time L->R normalling.
    bool hasStereoAudioInputPair() const override { return true; }

    // Pure audio/CV processor — processBlock's midiMessages parameter is unused.
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }

    ModuleType getModuleType() const override { return ModuleType::VCA; }

    LogicalPort mapInputChannel(int raw) const override {
        if (auto audio = mapAudioLeg(raw))
            return *audio;

        LogicalPort p;
        if (polyParam->get()) {
            // Poly mode: raw 8-15 = per-voice ModCV fan
            if (raw >= kPolyCVBase && raw < kPolyCVBase + kNumVoices) {
                p.visibleJackIndex = splitAudioJackCount();
                p.role = PortRole::ModCV;
                p.isPolyGroupHead = (raw == kPolyCVBase);
                p.polyVoiceSpan = (raw == kPolyCVBase) ? kNumVoices : 1;
                return p;
            }
        } else if (raw == 1) {
            p.visibleJackIndex = splitAudioJackCount();
            p.role = PortRole::ModCV;
            p.isPolyGroupHead = true;
            p.polyVoiceSpan = 1;
            return p;
        }

        // Unclaimed channels are addressable but never a jack head. Deliberately not ModuleBase's
        // default: it reports isPolyGroupHead for any raw channel below the VISIBLE jack count, so
        // going from 2 jacks to 3 would make mono raw ch2 a phantom second head on the CV jack.
        p.visibleJackIndex = 0;
        p.role = PortRole::Other;
        p.isPolyGroupHead = false;
        p.polyVoiceSpan = 1;
        return p;
    }

    /** In poly mode the LEFT leg is a voice fan but the OUTPUT is a summed pair, so the output map
        is deliberately not the input map: ch0 and kRightBase are plain mono jacks on the way out. */
    LogicalPort mapOutputChannel(int raw) const override {
        LogicalPort p;
        p.role = PortRole::Audio;
        p.polyVoiceSpan = 1;

        if (raw == 0) {
            p.visibleJackIndex = 0;
            p.isPolyGroupHead = true;
            return p;
        }
        if (isDualIO() && raw == kRightBase) {
            p.visibleJackIndex = 1;
            p.isPolyGroupHead = true;
            return p;
        }

        // Everything else (the CV block, follower voices, and the vestigial ch1 duplicate) is
        // addressable but never a poly-bus head. Not ModuleBase's default, which would clamp a raw
        // channel onto a visible jack index and advertise ch1 as the Audio R head.
        p.visibleJackIndex = 0;
        p.isPolyGroupHead = false;
        return p;
    }

    /** Shared by the input map and the audio half of the output map. */
    std::optional<LogicalPort> mapAudioLeg(int raw) const {
        const int span = polyParam->get() ? kNumVoices : 1;
        const int legs = splitAudioJackCount(); // collapsed: the right block is not exposed
        for (int leg = 0; leg < legs; ++leg) {
            const int base = legBaseChannel(leg);
            if (raw >= base && raw < base + span) {
                LogicalPort p;
                p.visibleJackIndex = leg;
                p.role = PortRole::Audio;
                p.isPolyGroupHead = (raw == base);
                p.polyVoiceSpan = (raw == base) ? span : 1;
                return p;
            }
        }
        return std::nullopt;
    }

    /** The legacy ch1 copy of the output is faded with the legs. Mono bypass clears it (ch1 is the gain CV
        jack there), so it has no dry path; in poly mode bypass leaves voice 1's audio input on it. */
    int duplicateOutputChannel() const override { return 1; }
    int bypassDryChannel(int outputChannel) const override {
        if (outputChannel == 1 && !polyParam->get())
            return -1;
        return ModuleBase::bypassDryChannel(outputChannel);
    }

    bool isAutoPromotableModTarget(int dstChannel) const override {
        if (polyParam->get())
            return false;
        return ModuleBase::isAutoPromotableModTarget(dstChannel);
    }

private:
    static constexpr int MAX_VOICES = 8;

    void resetHeadroom() {
        headroomGain = 1.0f;
        for (auto& peak : voicePeak)
            peak = 0.0f;
    }

    /** The headroom gain moved towards `target`: down in about 1.5 ms, back up in about 50 ms, so it never steps.
        Exactly 1 for as long as the target has been 1. */
    float nextHeadroom(float target) {
        if (target == 1.0f && headroomGain == 1.0f)
            return 1.0f;
        headroomGain += (target - headroomGain) * (target < headroomGain ? headroomDown : headroomUp);
        if (target == 1.0f && headroomGain > 0.99999f)
            headroomGain = 1.0f;
        return headroomGain;
    }

    float voicePeak[MAX_VOICES] = {};
    float peakRelease = 0.0f;
    float headroomGain = 1.0f;
    float headroomDown = 1.0f;
    float headroomUp = 1.0f;
    juce::AudioParameterFloat* gainParam = nullptr;
    juce::AudioParameterBool* polyParam = nullptr;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedGain;
};
