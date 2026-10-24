// WavetableOscillatorModuleVoicing.cpp -- the settings every voice shares in a block: the unison stack's
// ratios and gain mix (Blend, Width, voice count) and the warp, sync and sub choices. The class and channel
// map: WavetableOscillatorModule.h; the render loop: WavetableOscillatorModuleRender.cpp. The unison maths is
// shared with the Oscillator (UnisonVoicing.h).

#include "WavetableOscillatorModule.h"

// Every voice has a target gain on each leg: its Blend level times its Width pan gain, or 0 once Unison no
// longer includes it. UnisonMix moves the gain a voice plays towards it (at most 1.0 per 10 ms, ramped inside
// the block), so Blend, Width and the voice count glide instead of stepping, and the stack divides by the root
// of the summed power as it stands at each sample. A voice that is leaving keeps the detune and stack interval
// of the outermost voice that stays, so it fades out in tune. `numSamples` is the block length.
WavetableOscillatorModule::BlockSettings WavetableOscillatorModule::gatherBlockSettings(int numSamples) {
    BlockSettings bs;
    bs.unisonCount = juce::jlimit(1, MAX_UNISON, unisonParam->get());
    bs.warp = (Warp)juce::jlimit(0, (int)Warp::Count - 1, warpParam->getIndex());
    bs.stack = (Stack)juce::jlimit(0, (int)Stack::Count - 1, stackParam->getIndex());
    bs.syncMode = (SyncMode)juce::jlimit(0, (int)SyncMode::Count - 1, syncModeParam->getIndex());
    bs.interpolation = (Interpolation)juce::jlimit(0, (int)Interpolation::Count - 1, interpolationParam->getIndex());
    bs.pitchModulated = hasCV(kJackOctave) || hasCV(kJackCoarse) || hasCV(kJackFine);
    bs.oversample = warpNeedsOversampling(bs.warp, blockPeakWarpAmount);
    bs.subOctaveRatio = (subOctaveParam->getIndex() == 1) ? 0.25f : 0.5f;
    bs.subPosition = (subShapeParam->getIndex() == 1) ? 1.0f : 0.0f; // Basic Shapes: sine .. square

    bs.detuneCents = juce::jlimit(0.0f, 100.0f, detuneParam->get() + cvAt(kJackDetune, 0) * 100.0f);
    const float width = juce::jlimit(0.0f, 1.0f, widthParam->get() + cvAt(kJackWidth, 0));
    const float blend = juce::jlimit(0.0f, 1.0f, blendParam->get() + cvAt(kJackBlend, 0));

    float targetL[MAX_UNISON]{};
    float targetR[MAX_UNISON]{};
    for (int u = 0; u < bs.unisonCount; ++u) {
        // Blend fades the other voices against an always-present root, so turning it down thins the
        // chorus without changing the fundamental's level. Detune spreads the voices symmetrically, so
        // the root is the middle voice(s) (the shared helper); a Stack mode keeps voice 0 at the root pitch.
        const bool isRoot = bs.stack == Stack::Detune ? synth::isUnisonCentreVoice(u, bs.unisonCount) : u == 0;
        const float voiceGain = isRoot ? 1.0f : synth::unisonOuterGain(blend);
        float gl, gr;
        synth::unisonPanGains(u, bs.unisonCount, width, gl, gr);
        targetL[u] = gl * voiceGain;
        targetR[u] = gr * voiceGain;
    }
    unisonMix.advance(targetL, targetR, bs.unisonCount, numSamples, currentSampleRate);
    bs.mix = unisonMix;
    bs.renderCount = unisonMix.renderCount;

    for (int u = 0; u < bs.renderCount; ++u) {
        const int shape = std::min(u, bs.unisonCount - 1);
        const float cents = bs.detuneCents * synth::unisonOffsetFraction(shape, bs.unisonCount);
        const float semis = stackSemitones(bs.stack, shape, bs.unisonCount);
        bs.uniRatio[u] = std::pow(2.0f, semis / 12.0f + cents / 1200.0f);
    }
    return bs;
}
