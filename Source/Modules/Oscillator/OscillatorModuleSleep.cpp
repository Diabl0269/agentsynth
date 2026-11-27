// OscillatorModuleSleep.cpp -- a mono Oscillator nobody can hear this block skips its waves and only moves its
// phases. The proof of who can hear it is built on the message thread (Source/AudioEngine/OscillatorSleepProof.cpp);
// the rules: docs/architecture/audio-engine.md#oscillator-sleep and Modules/CLAUDE.md.
#include "Modules/ADSRModule.h"
#include "Modules/AttenuverterModule.h"
#include "Modules/OscillatorModule.h"
#include "Modules/RenderShortcuts.h"
#include "Modules/VCAModule.h"

// Every VCA reads its gain CV from the cone alone, so when every envelope in it rested and nothing on the way can
// turn a zero into something else, the VCA multiplies this Oscillator by exactly zero. An envelope that rested last
// block and gets no note or gate (its notes come only from this Oscillator's sources, whose MIDI is empty, and its
// gate is unpatched) still outputs +0 this block, so the answer holds whether it renders before or after us. A VCA
// renders after its source, so its output stage is read before its own render writes it.
bool synth::OscillatorSleepCone::atRest() const noexcept {
    for (const auto* vca : vcas)
        if (vca->isPolyMode() || !vca->rendersWetOnly())
            return false;
    for (const auto* envelope : envelopes)
        if (!envelope->restedLastBlock() || envelope->hasNoteInbox())
            return false;
    for (const auto* attenuverter : attenuverters)
        if (!attenuverter->curveIsLinear())
            return false;
    return true;
}

// Publishes only a changed cone: refreshNormalling runs on every graph broadcast and a new table per call would only
// churn the handoff. A node's role is its type, so the node list is the whole cone. An empty cone is still published
// once a real one was, so a cable that makes the Oscillator heard revokes it; before any, no cone means no sleep.
void OscillatorModule::publishSleepCone(std::unique_ptr<synth::OscillatorSleepCone> cone) {
    if (cone == nullptr)
        cone = std::make_unique<synth::OscillatorSleepCone>();
    std::vector<const juce::AudioProcessorGraph::Node*> nodes;
    for (const auto& node : cone->keepAlive)
        nodes.push_back(node.get());
    if (nodes == publishedCone_) {
        sleepCone_.collectRetired();
        return;
    }
    publishedCone_ = std::move(nodes);
    sleepCone_.publish(std::move(cone));
}

// The low-band trim only acts while the stack's in-phase start is coherent. With no coherence weight (Rand or Spread
// apart) it stays at gain 1 until the next restart resets it, and a single voice keeps no trim state at all
// (renderStack), so skipping its filter leaves the full render's state. Predicts renderCount before advance() runs:
// a leaving voice still fading keeps the stack wider than the Unison knob.
bool OscillatorModule::lowTrimAtRest(int unisonCount) const noexcept {
    if (voices[0].coherenceWeight <= 0.0f)
        return true;
    if (unisonCount >= 2)
        return false;
    if (!unisonMix.primed)
        return true;
    for (int u = 1; u < MAX_UNISON; ++u)
        if (unisonMix.reachedL[u] != 0.0f || unisonMix.reachedR[u] != 0.0f)
            return false;
    return true;
}

// Read-only: nothing here changes state, so a refusal leaves processModuleBlock to render the block as if this had
// never run. No audio cable enters the Oscillator (the proof), so its CV and Gate inputs are zeros, and with empty MIDI
// the block holds no note-on, pitch event or Gate edge. What is left is a voice running at a steady wave: no restart
// fade, no waveform crossfade, no sync lead, and the trim at rest.
bool OscillatorModule::canSleep(const juce::AudioBuffer<float>& buffer) noexcept {
    sleepCone_.adopt();
    const auto* cone = sleepCone_.current();
    if (cone == nullptr || cone->vcas.empty() || polyParam->get() || buffer.getNumChannels() <= kGateBase)
        return false;
    const auto& voice = voices[0];
    if (voice.ghosts.count > 0 || voice.crossfadeSamplesRemaining > 0 || voice.sync.pendingLead != 0.0f ||
        voice.previousWaveform != waveformParam->getIndex() || numRestarts != 0 || numPitchEvents != 0)
        return false;
    if (auto* vb = getVisualBuffer(); vb != nullptr && vb->watchedRecently())
        return false;
    if (!lowTrimAtRest(readGlobalParamCV(buffer, buffer.getNumSamples()).unisonCount))
        return false;
    return cone->atRest();
}

// processModuleBlock and processMonoMode with everything that only shapes the wave left out: the same calls in the same
// order for every piece of state (held notes, pitch smoother and glide, level / pulse width / pan smoothers, unison
// gains, Gate and sync counters), then per sample the same phase arithmetic as renderStack / sumStack, written as the
// same separate statements so the compiler rounds it the same way. The view is not pushed, so the card's LED keeps
// the last level it showed; a shown scope keeps the Oscillator awake (canSleep).
void OscillatorModule::advanceAsleep(juce::AudioBuffer<float>& buffer) noexcept {
    const int numSamples = buffer.getNumSamples();
    const juce::MidiBuffer noMidi;
    scanMidi(noMidi, numSamples, /*audible*/ true);

    const GlobalParamCV global = readGlobalParamCV(buffer, numSamples);
    fixedRateCv_ = global.fixedRateCv;
    cacheRestartCV(buffer, numSamples);
    auto& voice = voices[0];
    voice.gateEdges.scan(buffer.getReadPointer(kGateBase), numSamples);

    const float targetFreq = baseFrequencyHz(noteAtBlockStart);
    const float minHz = minRenderHz();
    voice.smoothedFreq.setTargetValue(targetFreq);
    voice.glide.retarget(targetFreq, global.glideSeconds, currentSampleRate, glideTime(), glideCurve());
    const bool gliding = global.glideSeconds > 0.0f;
    float lastBaseFreq = targetFreq;

    const int levelLen = std::max(1, std::min(numSamples, 4096));
    fillLevelRamp(levelLen);
    fillPulseWidthRamp(levelLen, global.pulseWidthPercent);
    prepareUnisonMix(global.unisonCount, numSamples, global.blend, global.width);
    float detuneRatios[MAX_UNISON];
    fillUnisonRatios(detuneRatios, global.unisonCount, unisonMix.renderCount, global.detuneCents);
    const int count = unisonMix.renderCount;

    for (int i = 0; i < numSamples; ++i) {
        const float baseFreq = gliding ? voice.glide.nextFrequency() : voice.smoothedFreq.getNextValue();
        lastBaseFreq = baseFreq;
        const float freq = juce::jlimit(minHz, 20000.0f, baseFreq);
        const float dt = static_cast<float>(freq / currentSampleRate);
        voice.sync.tick();
        for (int u = 0; u < count; ++u) {
            const float uniDt = dt * detuneRatios[u];
            voice.unisonOscs[u].phase += uniDt;
            if (voice.unisonOscs[u].phase >= 1.0f)
                voice.unisonOscs[u].phase -= 1.0f;
        }
        voice.samplesSinceRestart += 1.0f;
        voice.lastBaseDt = dt;
    }
    if (count < 2)
        voice.lowTrim = {};
    else
        voice.lowTrim.gain = 1.0f;

    if (gliding)
        voice.smoothedFreq.setCurrentAndTargetValue(lastBaseFreq);
    fillPanRamp(numSamples);
    buffer.clear();
    clearPendingRestarts();
    voiceCountLatch.publish(blockPeakHeld > 0 ? 1 : 0, numSamples, currentSampleRate);
}

bool OscillatorModule::renderSilentBlock(juce::AudioBuffer<float>& buffer) noexcept {
    if (!canSleep(buffer))
        return false;
    advanceAsleep(buffer);
    synth::RenderShortcuts::noteSleep();
    return true;
}
