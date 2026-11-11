#pragma once

#include "Modules/FX/ParametricEQ/ParametricEQBandTypes.h"
#include "Modules/ModuleBase.h"
#include <array>
#include <atomic>
#include <juce_dsp/juce_dsp.h>

/** Parametric EQ with 24 fixed band slots, each with its own type and (for cuts) slope.
 *
 *  Slots 1-4 are the bands the card and curve show (Low Shelf, Bell, Bell, High Shelf by default);
 *  slots 5-24 exist in the engine and the saved model but are not shown yet (isParameterExposed).
 *  Every band starts off, so a fresh EQ is a straight wire. Saved ids, the append order of the
 *  parameters and the slope set: docs/modules/fx-modules.md#parametric-eq-module.
 */
class ParametricEQModule : public ModuleBase {
public:
    using BandType = synth::eq::BandType;

    static constexpr int kMaxBands = 24;
    /** The slots the card, the curve's points and its keys show today. */
    static constexpr int kShownBands = 4;
    static constexpr int kMaxStages = synth::eq::kMaxStages;
    static constexpr float kMaxGainDb = 24.0f;
    static constexpr float kMinFreq = 20.0f;
    static constexpr float kMaxFreq = 20000.0f;
    static constexpr float kMinQ = 0.1f;
    static constexpr float kMaxQ = 10.0f;
    /** Q = 1/sqrt(2): maximally flat. */
    static constexpr float kDefaultQ = 0.70710678f;
    static constexpr int kMixCvChannel = 15;
    static constexpr int kPanCvChannel = 16;

    /** Resolved (post-CV) settings of one band: what the visualiser draws. */
    struct BandSnapshot {
        BandType type = BandType::Peak;
        bool enabled = false;
        float freqHz = 1000.0f;
        float gainDb = 0.0f;
        float q = kDefaultQ;
        int slopeDbPerOct = synth::eq::kSlopesDbPerOct[synth::eq::kDefaultSlopeIndex];
    };
    using BandSnapshots = std::array<BandSnapshot, kMaxBands>;

    ParametricEQModule();

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void processModuleBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;

    juce::String getInputPortLabel(int i) const override;
    juce::String getOutputPortLabel(int i) const override { return stereoOutputLabel(i); }
    int getVisibleInputPortCount() const override { return stereoVisibleInputCount(kNumCvInputs); }
    int getVisibleOutputPortCount() const override { return stereoVisibleOutputCount(); }
    LogicalPort mapInputChannel(int raw) const override { return mapStereoPairInput(raw, kNumCvInputs); }
    bool hasStereoAudioInputPair() const override { return true; }
    LogicalPort mapOutputChannel(int raw) const override { return mapStereoPairOutput(raw); }
    std::vector<ModulationTarget> getModulationTargets() const override;
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    ModulationCategory getModulationCategory() const override { return ModulationCategory::Filter; }
    ModuleType getModuleType() const override { return ModuleType::ParametricEQ; }
    bool isParameterExposed(const juce::String& paramId) const override { return !isHiddenParamId(paramId); }

    /** True for a parameter no surface shows yet: bands 5-24 and every band's Type and Slope. */
    static bool isHiddenParamId(const juce::String& paramId);

    // ---------- band model (message thread) ----------

    /** Parameter values, with a jack's CV-resolved value overlaid while that jack drives it. */
    BandSnapshots getBandSnapshots() const;
    bool isBandEnabled(int band) const { return isValidBand(band) && bands[(size_t)band].on->get(); }
    int getEnabledBandCount() const;
    BandType getBandType(int band) const;
    int getBandSlope(int band) const;

    // Setters take real-world units and notify the host; an out-of-range band is ignored.
    void setBandEnabled(int band, bool enabled);
    void setBandFreq(int band, float freqHz);
    void setBandGain(int band, float gainDb);
    void setBandQ(int band, float q);
    void setBandType(int band, BandType type);
    /** `slopeDbPerOct` snaps to the nearest listed slope. */
    void setBandSlope(int band, int slopeDbPerOct);

    /** The shown slot (1-4) a new curve point at `freqHz` takes, or -1 when all four are on. */
    int findBandForNewPoint(float freqHz) const;
    /** The lowest slot of all 24 that is off, or -1 when every slot is on. */
    int findFreeBand() const;
    /** Turns the lowest free slot on at `freqHz` as `type` (gain 0, default Q); returns it, or -1. */
    int addBand(float freqHz, BandType type);

    // ---------- solo audition (any thread; not saved) ----------

    /** Plays only band `band`'s region while it is on; an invalid band clears the solo. */
    void setSoloBand(int band) { soloBand.store(isValidBand(band) ? band : -1, std::memory_order_relaxed); }
    void clearSolo() { soloBand.store(-1, std::memory_order_relaxed); }
    int getSoloBand() const { return soloBand.load(std::memory_order_relaxed); }

    float getOutputGainDb() const { return outputGainParam->get(); }
    double getLastSampleRate() const { return lastSampleRate; }

    /** The type slot `index` starts as: 0 Low Shelf, 3 High Shelf, every other slot Bell. */
    static BandType bandTypeFor(int index) noexcept;
    /** Row label / on-off toggle text for slot `index`, e.g. "1 Low Shelf". */
    static juce::String rowLabelFor(int index);
    /** Home frequency of slot `index`: its default, and what findBandForNewPoint ranks against. */
    static float defaultFreqFor(int index) noexcept;

    // ---------- pure static helpers ----------

    /** Analog magnitude in dB of one band at `freq`, every stage included. */
    static float
    bandMagnitudeDb(BandType type, float centreHz, float gainDb, float q, float freq,
                    int slopeDbPerOct = synth::eq::kSlopesDbPerOct[synth::eq::kDefaultSlopeIndex]) noexcept;
    /** Enabled bands plus the output trim, in dB, at `freq`. */
    static float responseDb(const BandSnapshots& bands, float outputGainDb, float freq) noexcept;
    /** One band's first stage into `dest[0..4]`, normalised as juce::dsp::IIR::Coefficients stores it. */
    static void writeBiquad(BandType type, float centreHz, float gainDb, float q, double sampleRate,
                            float* dest) noexcept;
    /** cv +1 sweeps to 20 kHz, -1 to 20 Hz, exponentially. */
    static float applyFreqCV(float base, float cv) noexcept;
    /** cv maps linearly onto +/-24 dB, added to the knob. */
    static float applyGainCV(float baseDb, float cv) noexcept;

private:
    static constexpr int kNumAudioChannels = 2;
    static constexpr int kNumCvInputs = 15;
    static constexpr int kNumBellBands = 2;
    static constexpr int kBellBandIndex[kNumBellBands] = {1, 2};

    using Coefs = juce::dsp::IIR::Coefficients<float>;
    using Filter = juce::dsp::IIR::Filter<float>;
    using FreqSmoother = juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>;
    using GainSmoother = juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>;

    struct BandParams {
        juce::AudioParameterBool* on = nullptr;
        juce::AudioParameterFloat* freq = nullptr;
        juce::AudioParameterFloat* gain = nullptr;
        juce::AudioParameterFloat* q = nullptr;
        juce::AudioParameterChoice* type = nullptr;
        juce::AudioParameterChoice* slope = nullptr;
    };

    static bool isValidBand(int band) noexcept { return band >= 0 && band < kMaxBands; }
    void addBandParameters(int band);
    void addTypeAndSlopeParameters(int band);
    BandType typeOf(int band) const noexcept;
    int slopeOf(int band) const noexcept;
    static float readCV(const juce::AudioBuffer<float>& buffer, int channel, int numSamples) noexcept;
    void readTargetsAndCV(const juce::AudioBuffer<float>& buffer, int numSamples);
    void updateCoefficients();
    void updateSoloCoefficients(const float* freq, const float* q);
    void filterRange(juce::AudioBuffer<float>& buffer, int numAudioCh, int start, int numSamples);
    void renderSoloed(juce::AudioBuffer<float>& buffer, int numAudioCh, int numSamples);

    // [channel][band][stage]; coefficients are shared by both channels (state lives in the Filter).
    std::array<std::array<std::array<Filter, kMaxStages>, kMaxBands>, kNumAudioChannels> filters;
    std::array<std::array<Coefs::Ptr, kMaxStages>, kMaxBands> coefficients;
    std::array<int, kMaxBands> activeStages{};
    std::array<Filter, kNumAudioChannels> soloFilters;
    Coefs::Ptr soloCoefficients;
    juce::AudioBuffer<float> soloScratch;
    std::atomic<int> soloBand{-1};
    int appliedSolo = -1; // audio thread only
    bool soloAudible = false;

    double lastSampleRate = 44100.0;

    std::array<FreqSmoother, kMaxBands> smoothedFreq{};
    std::array<GainSmoother, kMaxBands> smoothedGain{};
    std::array<FreqSmoother, kMaxBands> smoothedQ{};
    GainSmoother smoothedOutputGain{1.0f};

    std::array<BandParams, kMaxBands> bands{};
    juce::AudioParameterFloat* outputGainParam = nullptr;

    // Written by the audio thread each block, read by getBandSnapshots on the message thread.
    std::array<std::atomic<float>, kMaxBands> resolvedFreq{};
    std::array<std::atomic<float>, kMaxBands> resolvedGain{};
    std::array<std::atomic<float>, kMaxBands> resolvedQ{};
    std::array<std::atomic<bool>, kMaxBands> freqCVActive{};
    std::array<std::atomic<bool>, kMaxBands> gainCVActive{};
    std::array<std::atomic<bool>, kMaxBands> qCVActive{};
};
