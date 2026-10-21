#pragma once

#include "Modules/ModuleBase.h"
#include "Modules/Shapes/LockFreeHandoff.h"
#include "SequencerClock.h"
#include "SequencerPattern.h"
#include "SequencerPlayer.h"
#include "SequencerPreview.h"
#include "SequencerScaleSnap.h"
#include <atomic>

namespace synth {

/** Parameter ids shared by the card, the tools and the tests. */
namespace sequencer_ids {
inline constexpr const char* kRun = "run";
inline constexpr const char* kBpm = "bpm";
inline constexpr const char* kSync = "syncToTransport";
inline constexpr const char* kSteps = "steps";
inline constexpr const char* kRate = "rate";
inline constexpr const char* kTranspose = "transpose";
inline constexpr const char* kGate = "gate";
inline constexpr const char* kVelocity = "velocity";
inline constexpr const char* kChance = "chance";
inline constexpr const char* kScaleRoot = "scaleRoot";
inline constexpr const char* kScale = "scale";
} // namespace sequencer_ids

/** What Sequencer and Poly Sequencer share: the whole-pattern parameters, the pattern as module state, and the
 *  playback. The subclasses only choose mono or poly and their name. */
class SequencerModuleBase : public ModuleBase {
public:
    /** The step the playhead last reached (UI). */
    std::atomic<int> currentActiveStep{0};

    bool isPoly() const noexcept { return poly_; }

    /** Message thread: the authoritative pattern. */
    const SequencerPattern& getPattern() const noexcept { return pattern_; }
    /** Message thread: replaces the pattern and hands a copy to the audio thread. */
    void setPattern(const SequencerPattern& pattern);
    /** Bumps on every setPattern(); the card polls it to know when to redraw. */
    int patternGeneration() const noexcept { return generation_.load(std::memory_order_acquire); }

    /** Message thread: plays these notes (at most 8) through the output for about 300 ms, whether Run is on or off,
     *  replacing any earlier preview. Does nothing while the module is bypassed. */
    void previewNotes(const int* notes, int count, float velocity) noexcept {
        preview_.request(notes, count, velocity);
    }
    /** Message thread: ends the preview now. */
    void stopPreview() noexcept { preview_.request(nullptr, 0, 0.0f); }

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void processModuleBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;

    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return true; }
    ModulationCategory getModulationCategory() const override { return ModulationCategory::Sequencer; }

    juce::var getExtraState() const override;
    void setExtraState(const juce::var& state) override;
    void setUntrustedExtraState(const juce::var& state) override;
    void adoptLegacyParams(const juce::NamedValueSet& params) override;
    bool isLegacyParamId(const juce::String& id) const override;

protected:
    SequencerModuleBase(const char* name, bool poly);

private:
    SequencerSettings readSettings() const;
    void renderPattern(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi);
    void stopOutput(juce::MidiBuffer& midi);

    const bool poly_;
    SequencerPattern pattern_;
    std::atomic<int> generation_{0};
    LockFreeHandoff<SequencerPattern> handoff_;

    SequencerClock clock_;
    SequencerPlayer player_;
    SequencerScaleSnap snap_;
    SequencerPreview preview_;
    double sampleRate_ = 44100.0;

    juce::AudioParameterBool* runParam_ = nullptr;
    juce::AudioParameterFloat* bpmParam_ = nullptr;
    juce::AudioParameterBool* syncParam_ = nullptr;
    juce::AudioParameterInt* stepsParam_ = nullptr;
    juce::AudioParameterChoice* rateParam_ = nullptr;
    juce::AudioParameterInt* transposeParam_ = nullptr;
    juce::AudioParameterFloat* gateParam_ = nullptr;
    juce::AudioParameterFloat* velocityParam_ = nullptr;
    juce::AudioParameterFloat* chanceParam_ = nullptr;
    juce::AudioParameterChoice* scaleRootParam_ = nullptr;
    juce::AudioParameterChoice* scaleParam_ = nullptr;
};

} // namespace synth
