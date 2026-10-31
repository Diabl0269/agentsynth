// Envelope-shape renders for the black-box A/B harness: a sustained sine through each synth's envelope at a sweep of
// attack, decay and release times, so the attack/decay/release curves can be fitted from the demodulated amplitude.
// Gated like the rest of the harness (AGENTSYNTH_REFERENCE_COMPARE_DIR / AGENTSYNTH_REFERENCE_PLUGIN); it only writes
// WAV files for Tools/ReferenceCompare/envelope.py.

#include "ReferenceCompareRig.h"

using namespace referencecompare;

namespace {

// The reference synth's Env1 times read 32 s * x^5 and its sustain is dB; its Sub oscillator is the only exposed sine.
void referenceSineEnvelope(ReferenceRig& r, double attack, double decay, double release, double sustainDb) {
    r.setNorm("Osc A On", 0.0f);
    r.setNorm("Osc S On", 1.0f);
    r.setText("SubOscShape", "Sine");
    r.setNumeric("SubOscOctave", 0.0);
    r.setNumeric("Sub Osc Level", 100.0);
    auto time = [](double seconds) { return static_cast<float>(std::pow(seconds / 32.0, 0.2)); };
    r.setNorm("Env1 Atk", time(attack), "time = 32 s * x^5");
    r.setNorm("Env1 Hold", 0.0f);
    r.setNorm("Env1 Dec", time(decay), "time = 32 s * x^5");
    // The -inf end of the sustain range reads as "0" to the numeric text search, so full scale is set by position.
    if (sustainDb > -0.01)
        r.setNorm("Env1 Sus", 1.0f, "0 dB");
    else
        r.setNumeric("Env1 Sus", sustainDb);
    r.setNorm("Env1 Rel", time(release), "time = 32 s * x^5");
}

AgentSetup agentSineEnvelope(double attack, double decay, double sustain, double release) {
    AgentSetup a;
    a.osc["waveform"] = 0;
    a.osc["phase"] = 0.0;
    a.osc["randomPhase"] = 0.0;
    a.adsr = {{"attack", attack}, {"decay", decay}, {"sustain", sustain}, {"release", release}};
    return a;
}

} // namespace

// One note per case at MIDI 96 (2.09 kHz) so a sub-millisecond ramp is still resolved by the demodulation. Case names
// carry the swept time in tenths of a millisecond: envatk_5 is a 0.5 ms attack.
TEST(ReferenceCompareRender, RenderEnvelopeShapes) {
    const auto dir = outDir();
    if (dir == juce::File())
        GTEST_SKIP() << "AGENTSYNTH_REFERENCE_COMPARE_DIR / AGENTSYNTH_REFERENCE_PLUGIN not set";
    dir.createDirectory();
    synth::DefaultHostedPluginBackend backend;
    constexpr int kPitch = 96;
    auto run = [&](const juce::String& name, double on, double off, double length, double attack, double decay,
                   double sustain, double release) {
        const double sustainDb = 20.0 * std::log10(std::max(sustain, 1.0e-6));
        Case c{name,
               {{samples(on), samples(off), kPitch}},
               samples(length),
               agentSineEnvelope(attack, decay, sustain, release),
               [=](ReferenceRig& r) { referenceSineEnvelope(r, attack, decay, release, sustainDb); },
               false,
               {}};
        juce::var entry;
        ASSERT_TRUE(renderCase(dir, backend, c, entry)) << name;
    };
    for (const double ms : {0.5, 1.0, 2.0, 4.0, 10.0, 50.0, 200.0})
        run("envatk_" + juce::String(juce::roundToInt(ms * 10.0)), 0.3, 1.3, 1.7, ms / 1000.0, 0.0, 1.0, 0.015);
    for (const double ms : {5.0, 15.0, 50.0, 200.0, 800.0, 2000.0, 5000.0})
        run("envrel_" + juce::String(juce::roundToInt(ms * 10.0)), 0.3, 1.0, 1.0 + ms / 1000.0 + 0.4, 0.002, 0.0, 1.0,
            ms / 1000.0);
    for (const double ms : {20.0, 50.0, 100.0, 200.0, 400.0, 800.0, 1600.0})
        run("envdec_" + juce::String(juce::roundToInt(ms * 10.0)), 0.3, 2.3, 2.7, 0.002, ms / 1000.0, 0.5, 0.015);
    // The same 200 ms decay to a lower sustain, to see whether the fade shape depends on how far it travels.
    run("envdsl_2000", 0.3, 2.3, 2.7, 0.002, 0.2, 0.1, 0.015);
}
