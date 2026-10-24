// WavetableOscillatorModuleRender.cpp -- the per-voice render loop (one block of one voice).
// The class, channel map and mode processing: WavetableOscillatorModule.h / .cpp.

#include "WavetableOscillatorModule.h"

void WavetableOscillatorModule::renderVoice(const Wavetable& wt, VoiceState& voice, float* outL, float* outR,
                                            int numSamples, int cacheLen, const BlockSettings& bs) {
    const float invSampleRate = 1.0f / (float)currentSampleRate;
    const int oversample = bs.oversample ? kOversample : 1;
    const float subStep = 1.0f / (float)oversample;

    // The ramp is monotone, so its highest-frequency end bounds the harmonic content for
    // the whole block — pick the mip from that end to stay alias-free.
    const float mipFreq = std::max(freqRamp[0], freqRamp[(size_t)(cacheLen - 1)]);

    // Worst-case warp rate over the block, so the mip is chosen for the fastest read the
    // warp will ever perform rather than for its value at sample 0.
    const float blockDt = juce::jlimit(20.0f, 20000.0f, mipFreq) * invSampleRate;
    const float warpRate = warpRateFactor(bs.warp, clampWarpAmount(bs.warp, blockPeakWarpAmount, blockDt));

    // Dividing by the oversample factor targets the OVERSAMPLED Nyquist: those extra
    // harmonics are representable while we render at kOversample x, and the decimator
    // takes them back out on the way down. Band-limiting to the base Nyquist here instead
    // would collapse a hard-synced table to a sine before the warp ever saw it.
    const float mipRateScale = warpRate / (float)oversample;

    int uniMip[MAX_UNISON];
    for (int u = 0; u < bs.unisonCount; ++u)
        uniMip[u] = selectMip(blockDt * bs.uniRatio[u] * mipRateScale);

    const auto& subTable = *builtInTables()[0];
    const float subPosFrames = bs.subPosition * (float)(subTable.numFrames - 1);
    const float lastFrame = (float)(wt.numFrames - 1);
    const bool hermite = bs.interpolation == Interpolation::Hermite;

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
        const float dt = juce::jlimit(20.0f, 20000.0f, freq) * invSampleRate;

        const float position = juce::jlimit(0.0f, 1.0f, positionRamp[(size_t)idx] + cvAt(kJackPosition, idx));
        const float posFrames = position * lastFrame;
        const float level = juce::jlimit(0.0f, 1.0f, levelRamp[(size_t)idx] + cvAt(kJackLevel, idx));
        const float warpAmount =
            clampWarpAmount(bs.warp, juce::jlimit(0.0f, 1.0f, warpRamp[(size_t)idx] + cvAt(kJackWarp, idx)), dt);
        const float subLevel = juce::jlimit(0.0f, 1.0f, subRamp[(size_t)idx] + cvAt(kJackSub, idx));

        float panL, panR;
        panGains(juce::jlimit(-1.0f, 1.0f, panRamp[(size_t)idx] + cvAt(kJackPan, idx)), panL, panR);
        if (panRestsOnLeft)
            panL = 1.0f;

        // A rising Gate edge is a new note on this voice: it restarts the phases at Phase/Rand/Spread
        // on its own sample, before the sync reset and the read.
        if (voice.gateEdges.firesAt(s))
            retriggerVoice(voice, bs.unisonCount);

        // Hard sync resets every sub-oscillator on the master's rising zero crossing.
        if (bs.syncMode == SyncMode::HardSync && syncResetCache[(size_t)idx]) {
            for (int u = 0; u < MAX_UNISON; ++u) {
                voice.phase[u] = 0.0f;
                voice.masterPhase[u] = 0.0f;
            }
        }

        float accL = 0.0f, accR = 0.0f;
        for (int k = 0; k < oversample; ++k) {
            float subL = 0.0f, subR = 0.0f;

            for (int u = 0; u < bs.unisonCount; ++u) {
                const float uniDt = dt * bs.uniRatio[u] * subStep;
                const int mip = bs.pitchModulated ? selectMip(dt * bs.uniRatio[u] * mipRateScale) : uniMip[u];

                const float v = readWarped(wt, mip, posFrames, voice.masterPhase[u], bs.warp, warpAmount, hermite);
                subL += v * bs.uniGainL[u];
                subR += v * bs.uniGainR[u];

                voice.masterPhase[u] += uniDt;
                // `while`, not `if`: at very low sample rates a single note can advance more
                // than a full cycle per sample, and one subtraction would leave phase >= 1.
                while (voice.masterPhase[u] >= 1.0f)
                    voice.masterPhase[u] -= 1.0f;
                voice.phase[u] = voice.masterPhase[u];
            }

            subL *= bs.uniNormalise;
            subR *= bs.uniNormalise;

            // Sub-oscillator: read out of the band-limited Basic Shapes table rather than
            // generated naively, so it inherits the same mip anti-aliasing as everything else.
            if (subLevel > 0.0f) {
                const float subDt = dt * bs.subOctaveRatio * subStep;
                const int subMip = selectMip(dt * bs.subOctaveRatio);
                const float sv = sampleTable(subTable, subMip, subPosFrames, voice.subPhase, hermite) * subLevel;
                subL += sv;
                subR += sv;
                voice.subPhase += subDt;
                while (voice.subPhase >= 1.0f)
                    voice.subPhase -= 1.0f;
            }

            if (bs.oversample) {
                voice.decimator[0].push(subL);
                voice.decimator[1].push(subR);
            } else {
                accL = subL;
                accR = subR;
            }
        }

        if (bs.oversample) {
            accL = voice.decimator[0].read();
            accR = voice.decimator[1].read();
        }

        // Ring mod / AM multiply the finished voice, so they apply to the whole stack.
        if (bs.syncMode == SyncMode::RingMod || bs.syncMode == SyncMode::AM) {
            const float sync = cvAt(kJackSync, idx);
            const float gain = (bs.syncMode == SyncMode::RingMod) ? sync : (0.5f + 0.5f * sync);
            accL *= gain;
            accR *= gain;
        }

        outL[s] = accL * level * panL;
        outR[s] = accR * level * panR;
    }
}
