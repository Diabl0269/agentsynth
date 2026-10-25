#include "SequencerModuleBase.h"
#include "Modules/Envelope/EnvelopeTempoSync.h"
#include "Modules/ParameterText.h"
#include "SequencerPatternLegacy.h"
#include "Transport/TransportService.h"

namespace synth {

SequencerModuleBase::SequencerModuleBase(const char* name, bool poly)
    : ModuleBase(name, 0, 0)
    , poly_(poly) {
    namespace ids = sequencer_ids;
    addParameter(runParam_ = new juce::AudioParameterBool(ids::kRun, "Run", false));
    addParameter(bpmParam_ = new juce::AudioParameterFloat(ids::kBpm, "BPM", 30.0f, 300.0f, 120.0f));
    addParameter(syncParam_ = new juce::AudioParameterBool(ids::kSync, "Sync to Transport", false));
    addParameter(stepsParam_ = new juce::AudioParameterInt(ids::kSteps, "Steps", 1, kMaxSequencerSteps, 8));
    addParameter(rateParam_ = new juce::AudioParameterChoice(ids::kRate, "Rate", envelopeNoteDivisions(),
                                                             envelopeNoteDivisionIndex("1/4")));
    addParameter(transposeParam_ = new juce::AudioParameterInt(ids::kTranspose, "Transpose", -24, 24, 0));
    addParameter(gateParam_ = new juce::AudioParameterFloat(ids::kGate, "Gate", 0.0f, 2.0f, 1.0f));
    addParameter(velocityParam_ = new juce::AudioParameterFloat(ids::kVelocity, "Velocity", 0.0f, 2.0f, 1.0f));
    addParameter(chanceParam_ =
                     new juce::AudioParameterFloat(ids::kChance, "Chance", juce::NormalisableRange<float>(0.0f, 1.0f),
                                                   1.0f, synth::fractionPercentAttributes()));
    addParameter(scaleRootParam_ = new juce::AudioParameterChoice(
                     ids::kScaleRoot, "Key", {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"}, 0));
    addParameter(scaleParam_ = new juce::AudioParameterChoice(ids::kScale, "Scale", sequencerScaleChoices(), 0));
    // A "follows the song key" switch will be appended after these, never inserted among them.

    auto pattern = SequencerPattern::makeDefault(poly_);
    pattern.seed = (std::uint32_t)juce::Random::getSystemRandom().nextInt();
    setPattern(pattern);
}

void SequencerModuleBase::setPattern(const SequencerPattern& pattern) {
    pattern_ = pattern;
    handoff_.publish(std::make_unique<SequencerPattern>(pattern));
    generation_.fetch_add(1, std::memory_order_release);
}

juce::var SequencerModuleBase::getExtraState() const { return pattern_.toVar(poly_); }

// No state at all (a node saved by an older build, or an undo to one) keeps the pattern as it is, so steps
// adopted from old parameter keys survive.
void SequencerModuleBase::setExtraState(const juce::var& state) {
    if (state.isVoid())
        return;
    setPattern(SequencerPattern::fromVar(state, poly_, pattern_.seed));
}

// The same clamped parse as a trusted load: the pattern is pure data and cannot name a file.
void SequencerModuleBase::setUntrustedExtraState(const juce::var& state) { setExtraState(state); }

void SequencerModuleBase::adoptLegacyParams(const juce::NamedValueSet& params) {
    auto adopted = pattern_;
    if (adoptLegacyStepParams(adopted, params, poly_))
        setPattern(adopted);
}

bool SequencerModuleBase::isLegacyParamId(const juce::String& id) const { return isLegacyStepParamId(id, poly_); }

void SequencerModuleBase::prepareToPlay(double sampleRate, int samplesPerBlock) {
    juce::ignoreUnused(samplesPerBlock);
    sampleRate_ = sampleRate;
    clock_.reset();
    player_.reset();
    preview_.prepare(sampleRate);
    currentActiveStep = 0;
}

SequencerSettings SequencerModuleBase::readSettings() const {
    SequencerSettings settings;
    settings.poly = poly_;
    settings.steps = stepsParam_->get();
    // The list stores beats as floats; every entry is a multiple of 1/384 beat, so snap back to the exact value or a
    // triplet drifts by a sample within a few dozen steps.
    settings.rateBeats = std::round((double)envelopeNoteDivisionBeats(rateParam_->getIndex()) * 384.0) / 384.0;
    settings.transpose = transposeParam_->get();
    settings.gateAmount = gateParam_->get();
    settings.velocityAmount = velocityParam_->get();
    settings.chanceAmount = chanceParam_->get();
    return settings;
}

void SequencerModuleBase::stopOutput(juce::MidiBuffer& midi) {
    player_.flush(midi);
    clock_.reset();
}

void SequencerModuleBase::processModuleBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) {
    handoff_.adopt();
    if (isBypassed()) {
        stopOutput(midiMessages);
        preview_.silence(midiMessages, player_);
        buffer.clear();
        return;
    }
    renderPattern(buffer, midiMessages);
    // This module has no mute parameter today; one added later silences the audition like bypass does.
    if (hasMuteParameter() && isMuted())
        preview_.silence(midiMessages, player_);
    else
        preview_.render(midiMessages, buffer.getNumSamples(), player_);
}

// The audition is rendered after the pattern, so the held-note list it consults already holds this block's notes.
void SequencerModuleBase::renderPattern(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) {
    const auto* pattern = handoff_.current();
    if (pattern == nullptr || !runParam_->get()) {
        stopOutput(midiMessages);
        return;
    }

    const auto settings = readSettings();
    snap_.update(scaleRootParam_->getIndex(), scaleParam_->getIndex(), pattern->customMask);

    // Locked to the transport only when the playhead is our own TransportService; a foreign host's playhead
    // falls back to the free-running clock rather than going silent.
    SequencerClockBlock block;
    TransportService* transport = syncParam_->get() ? dynamic_cast<TransportService*>(getPlayHead()) : nullptr;
    if (transport != nullptr) {
        const auto& info = transport->getCurrentBlockInfo();
        if (!info.playing) {
            stopOutput(midiMessages);
            return;
        }
        block = clock_.synced(info, settings.rateBeats);
    } else {
        block = clock_.freeRunning(buffer.getNumSamples(), (double)bpmParam_->get(), sampleRate_, settings.rateBeats);
    }

    player_.renderBlock(block, *pattern, snap_, settings, midiMessages, buffer.getNumSamples());
    currentActiveStep.store(player_.activeStep());
}

} // namespace synth
