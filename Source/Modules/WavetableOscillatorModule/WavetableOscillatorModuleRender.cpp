// WavetableOscillatorModuleRender.cpp -- the per-voice render loop (one block of one voice).
// The class, channel map and mode processing: WavetableOscillatorModule.h / .cpp; restarts and the unison mix:
// WavetableOscillatorModuleNotes.cpp / WavetableOscillatorModuleVoicing.cpp.

#include "WavetableOscillatorModule.h"

// One sample of one stack (the live one, or a ghost running out): every unison sub-oscillator, the sub and, when
// the warp oversamples, the decimation back to the base rate. Advances the stack's phases.
WavetableOscillatorModule::StackOut
WavetableOscillatorModule::renderStackSample(StackState& st, const BlockSettings& bs, const SampleParams& p) const {
    const int oversample = bs.oversample ? kOversample : 1;
    const float subStep = 1.0f / (float)oversample;
    float accL = 0.0f, accR = 0.0f;

    for (int k = 0; k < oversample; ++k) {
        float subL = 0.0f, subR = 0.0f;

        for (int u = 0; u < bs.renderCount; ++u) {
            const float uniDt = p.dt * bs.uniRatio[u] * subStep;
            const int mip = bs.pitchModulated ? selectMip(p.dt * bs.uniRatio[u] * p.mipRateScale) : p.uniMip[u];

            const float v = readWarped(*p.wt, mip, p.posFrames, st.masterPhase[u], bs.warp, p.warpAmount, p.hermite);
            subL += v * p.gainL[u];
            subR += v * p.gainR[u];

            st.masterPhase[u] += uniDt;
            // `while`, not `if`: at very low sample rates a single note can advance
            // more than a full cycle per sample, and one subtraction would leave phase >= 1.
            while (st.masterPhase[u] >= 1.0f)
                st.masterPhase[u] -= 1.0f;
            st.phase[u] = st.masterPhase[u];
        }

        subL *= p.norm;
        subR *= p.norm;

        // Sub-oscillator: read out of the band-limited Basic Shapes table rather than
        // generated naively, so it inherits the same mip anti-aliasing as everything else.
        if (p.subLevel > 0.0f) {
            const float subDt = p.dt * bs.subOctaveRatio * subStep;
            const int subMip = selectMip(p.dt * bs.subOctaveRatio);
            const float sv = sampleTable(*p.subTable, subMip, p.subPosFrames, st.subPhase, p.hermite) * p.subLevel;
            subL += sv;
            subR += sv;
            st.subPhase += subDt;
            while (st.subPhase >= 1.0f)
                st.subPhase -= 1.0f;
        }

        if (bs.oversample) {
            st.decimator[0].push(subL);
            st.decimator[1].push(subR);
        } else {
            accL = subL;
            accR = subR;
        }
    }

    if (bs.oversample) {
        accL = st.decimator[0].read();
        accR = st.decimator[1].read();
    }
    return {accL, accR};
}

// The live stack plus every ghost still running out: each ghost leaves along its own raised cosine and the live
// stack takes whatever share they give up, so the sum is continuous through a restart.
WavetableOscillatorModule::StackOut WavetableOscillatorModule::renderWithGhosts(VoiceState& voice, StackOut live,
                                                                                const BlockSettings& bs,
                                                                                const SampleParams& p) const {
    StackOut mixed;
    float ghostTotal = 0.0f;
    for (int g = 0; g < voice.ghosts.count; ++g) {
        auto& ghost = voice.ghosts.items[g];
        const StackOut old = renderStackSample(ghost.state, bs, p);
        const float w = synth::RestartGhosts<StackState>::share(ghost);
        mixed.left += old.left * w;
        mixed.right += old.right * w;
        ghostTotal += w;
        --ghost.remaining;
    }
    voice.ghosts.retireFinished();
    return {mixed.left + live.left * (1.0f - ghostTotal), mixed.right + live.right * (1.0f - ghostTotal)};
}

void WavetableOscillatorModule::renderVoice(const Wavetable& wt, VoiceState& voice, float* outL, float* outR,
                                            int numSamples, int cacheLen, const BlockSettings& bs) {
    const float invSampleRate = 1.0f / (float)currentSampleRate;
    const int oversample = bs.oversample ? kOversample : 1;

    // The ramp is monotone, so its highest-frequency end bounds the harmonic content for
    // the whole block -- pick the mip from that end to stay alias-free.
    const float mipFreq = std::max(freqRamp[0], freqRamp[(size_t)(cacheLen - 1)]);

    // Worst-case warp rate over the block, so the mip is chosen for the fastest read the
    // warp will ever perform rather than for its value at sample 0.
    const float blockDt = juce::jlimit(20.0f, 20000.0f, mipFreq) * invSampleRate;
    const float warpRate = warpRateFactor(bs.warp, clampWarpAmount(bs.warp, blockPeakWarpAmount, blockDt));

    // Dividing by the oversample factor targets the OVERSAMPLED Nyquist: those extra
    // harmonics are representable while we render at kOversample x, and the decimator
    // takes them back out on the way down. Band-limiting to the base Nyquist here instead
    // would collapse a hard-synced table to a sine before the warp ever saw it.
    SampleParams p;
    p.wt = &wt;
    p.subTable = builtInTables()[0].get();
    p.mipRateScale = warpRate / (float)oversample;
    p.hermite = bs.interpolation == Interpolation::Hermite;
    p.subPosFrames = bs.subPosition * (float)(p.subTable->numFrames - 1);
    int uniMip[MAX_UNISON];
    for (int u = 0; u < bs.renderCount; ++u)
        uniMip[u] = selectMip(blockDt * bs.uniRatio[u] * p.mipRateScale);
    p.uniMip = uniMip;
    const float lastFrame = (float)(wt.numFrames - 1);

    // Collapsed to one jack only the left leg is heard, so a balance would just fade it out (silent at
    // full right): the pan rests there and the hidden right leg alone follows it.
    const bool panRestsOnLeft = !isDualIO();
    for (int s = 0; s < numSamples; ++s) {
        const int idx = std::min(s, cacheLen - 1);

        float freq = freqRamp[(size_t)idx];
        if (bs.pitchModulated) {
            float semis = 0.0f;
            if (hasCV(kJackOctave))
                semis += std::round(cvAt(kJackOctave, idx) * 4.0f) * 12.0f;
            if (hasCV(kJackCoarse))
                semis += std::round(cvAt(kJackCoarse, idx) * 12.0f);
            if (hasCV(kJackFine))
                semis += cvAt(kJackFine, idx);
            if (semis != 0.0f)
                freq *= std::pow(2.0f, semis / 12.0f);
        }
        p.dt = juce::jlimit(20.0f, 20000.0f, freq) * invSampleRate;

        const float position = juce::jlimit(0.0f, 1.0f, positionRamp[(size_t)idx] + cvAt(kJackPosition, idx));
        p.posFrames = position * lastFrame;
        const float level = juce::jlimit(0.0f, 1.0f, levelRamp[(size_t)idx] + cvAt(kJackLevel, idx));
        p.warpAmount =
            clampWarpAmount(bs.warp, juce::jlimit(0.0f, 1.0f, warpRamp[(size_t)idx] + cvAt(kJackWarp, idx)), p.dt);
        p.subLevel = juce::jlimit(0.0f, 1.0f, subRamp[(size_t)idx] + cvAt(kJackSub, idx));

        float panL, panR;
        panGains(juce::jlimit(-1.0f, 1.0f, panRamp[(size_t)idx] + cvAt(kJackPan, idx)), panL, panR);
        if (panRestsOnLeft)
            panL = 1.0f;

        // A note (a MIDI note-on, or a rising Gate edge) restarts the phases at Phase/Rand/Spread on its own
        // sample, before the sync reset and the read; the old sound runs out as a ghost.
        float lead = 0.0f;
        if (voice.restartFiresAt(s, lead))
            restartVoice(voice, bs.unisonCount, /*fadeFromOld*/ true, lead);
        advanceSyncLead(voice, bs, p.dt);

        // Hard sync resets every sub-oscillator on the master's rising zero crossing.
        if (bs.syncMode == SyncMode::HardSync && syncResetCache[(size_t)idx]) {
            voice.stack.resetSyncPhases();
            for (int g = 0; g < voice.ghosts.count; ++g)
                voice.ghosts.items[g].state.resetSyncPhases();
        }

        // The voice gains ramp across the block, and the stack divides by the root of their summed power as it
        // stands at this sample.
        const float fs = (float)s;
        for (int u = 0; u < bs.renderCount; ++u) {
            p.gainL[u] = bs.mix.leftStart[u] + bs.mix.leftStep[u] * fs;
            p.gainR[u] = bs.mix.rightStart[u] + bs.mix.rightStep[u] * fs;
        }
        p.norm = 1.0f / std::sqrt(bs.mix.sumStart + bs.mix.sumStep * fs);

        StackOut acc = renderStackSample(voice.stack, bs, p);
        if (voice.ghosts.count > 0)
            acc = renderWithGhosts(voice, acc, bs, p);

        // Ring mod / AM multiply the finished voice, so they apply to the whole stack.
        if (bs.syncMode == SyncMode::RingMod || bs.syncMode == SyncMode::AM) {
            const float sync = cvAt(kJackSync, idx);
            const float gain = (bs.syncMode == SyncMode::RingMod) ? sync : (0.5f + 0.5f * sync);
            acc.left *= gain;
            acc.right *= gain;
        }

        outL[s] = acc.left * level * panL;
        outR[s] = acc.right * level * panR;
    }
}
