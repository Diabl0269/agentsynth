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
    addParameter(followParam_ = new juce::AudioParameterBool(ids::kFollowSongKey, "Follow Song Key", true));

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

// A node saved before the follow switch existed keeps what it played: a scale it had becomes its own key, and one
// with No scale follows the song key, which an old song does not have, so it still plays every note.
void SequencerModuleBase::applyStoredParamDefaults(const juce::NamedValueSet& storedParams, bool trusted) {
    juce::ignoreUnused(trusted); // the follow rule applies to a model-authored node as well
    if (storedParams.contains(sequencer_ids::kFollowSongKey))
        return;
    *followParam_ = scaleParam_->getIndex() == 0;
}

std::optional<MusicalScale> SequencerModuleBase::getOwnKey() const {
    const int index = scaleParam_->getIndex();
    if (index == 0)
        return std::nullopt;
    auto key = sequencerScale(scaleRootParam_->getIndex(), index, pattern_.customMask);
    key.name = sequencerScaleChoices()[index];
    return key;
}

MusicalScale SequencerModuleBase::resolvedScale(const std::optional<MusicalScale>& songKey) const {
    if (!followsSongKey())
        return sequencerScale(scaleRootParam_->getIndex(), scaleParam_->getIndex(), pattern_.customMask);
    if (songKey.has_value())
        return *songKey;
    return sequencerScale(0, 0, 0);
}

// A preset's own index when the mask is one; anything else (a user scale, a hand-made mask) is stored as Custom.
SequencerModuleBase::OwnKeyValues SequencerModuleBase::ownKeyValuesFor(const std::optional<MusicalScale>& key) {
    OwnKeyValues values;
    if (!key.has_value())
        return values;
    values.root = ((key->rootPitchClass % 12) + 12) % 12;
    const auto mask = (std::uint16_t)(key->mask & 0xFFF);
    const auto& presets = builtInScalePresets();
    for (size_t i = 0; i < presets.size(); ++i)
        if (presets[i].mask == mask) {
            values.scaleIndex = (int)i + 1;
            return values;
        }
    values.scaleIndex = sequencerCustomScaleIndex();
    values.customMask = mask;
    return values;
}

bool SequencerModuleBase::isLegacyParamId(const juce::String& id) const { return isLegacyStepParamId(id, poly_); }

void SequencerModuleBase::prepareToPlay(double sampleRate, int samplesPerBlock) {
    juce::ignoreUnused(samplesPerBlock);
    sampleRate_ = sampleRate;
    clock_.reset();
    flushOnNextBlock_ = true; // a note-off is still owed for what the player holds: a hosted synth keeps its voices
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
    if (flushOnNextBlock_) {
        flushOnNextBlock_ = false;
        player_.flush(midiMessages);
        player_.reset();
    }
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

// A follower reads the song key from our own TransportService whatever Sync says; a foreign host's playhead (or none)
// has no song key, which plays every note. The key goes in as a Custom mask, so any scale the picker offers snaps.
void SequencerModuleBase::updateSnap(const SequencerPattern& pattern) {
    if (!followParam_->get()) {
        snap_.update(scaleRootParam_->getIndex(), scaleParam_->getIndex(), pattern.customMask);
        return;
    }
    const auto* transport = dynamic_cast<TransportService*>(getPlayHead());
    const std::uint32_t packed = transport != nullptr ? transport->getSongKeyPacked() : 0u;
    if ((packed & (1u << 16)) == 0) {
        snap_.update(0, 0, 0);
        return;
    }
    snap_.update((int)((packed >> 12) & 0xF), sequencerCustomScaleIndex(), (std::uint16_t)(packed & 0xFFF));
}

// The audition is rendered after the pattern, so the held-note list it consults already holds this block's notes.
void SequencerModuleBase::renderPattern(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) {
    const auto* pattern = handoff_.current();
    if (pattern == nullptr || !runParam_->get()) {
        stopOutput(midiMessages);
        return;
    }

    const auto settings = readSettings();
    updateSnap(*pattern);

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
