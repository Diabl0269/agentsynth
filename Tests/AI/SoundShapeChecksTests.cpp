// SoundShapeChecks: the shape checks the eval harness scores a response with, on hand-written
// responses (positive and negative). The worked examples in the local prompt are scored against the
// same checks in AIIntegrationServiceProjectEditTests.cpp.
#include "AI/SoundShapeChecks.h"
#include <gtest/gtest.h>

using synth::soundshape::checkAcid;
using synth::soundshape::checkFilterEnvelope;
using synth::soundshape::checkPluck;

namespace {

juce::var parse(const juce::String& json) {
    const juce::var value = juce::JSON::parse(json);
    EXPECT_FALSE(value.isVoid()) << "test JSON did not parse: " << json;
    return value;
}

// A response with one new track whose envelope params are `envelopeParams`.
juce::var trackWithEnvelope(const juce::String& envelopeParams) {
    return parse(R"({"nodes": [], "connections": [], "timelineOps": [{"op": "addInstrumentTrack",
        "name": "Lead", "instrument": "Oscillator", "envelope": {"params": )" +
                 envelopeParams + "}}]}");
}

// Filter insert 7011 and envelope 7010 on a new track, joined by `modulation`.
juce::var acidResponse(const juce::String& filterParams, const juce::String& envelopeParams,
                       const juce::String& modulation = R"({"source": 7010, "dest": 7011, "destParam": "cutoff"})",
                       const juce::String& instrumentParams = R"({"waveform": "Saw"})") {
    return parse(
        R"({"mode": "merge", "nodes": [], "connections": [], "modulations": [)" + modulation +
        R"(], "timelineOps": [{"op": "addInstrumentTrack", "name": "Acid", "instrument": "Oscillator", "instrumentParams": )" +
        instrumentParams + R"(,
        "inserts": [{"type": "Filter", "id": 7011, "params": )" +
        filterParams + R"(}], "envelope": {"id": 7010, "params": )" + envelopeParams + "}}]}");
}

const char* kExistingPatch = R"({"nodes": [{"id": 1003, "type": "Filter", "params": {"cutoff": 2000}},
    {"id": 1004, "type": "ADSR", "params": {"attack": 0.01}}, {"id": 1005, "type": "LFO"}], "connections": []})";

} // namespace

// -- checkPluck -----------------------------------------------------------------------------------

TEST(SoundShapeChecksTest, PluckPassesForAShortSustainlessTrackEnvelope) {
    const auto check =
        checkPluck(trackWithEnvelope(R"({"attack": 0.005, "decay": 0.2, "sustain": 0, "release": 0.15})"));
    EXPECT_TRUE(check.pass) << check.reason;
}

TEST(SoundShapeChecksTest, PluckFailsForASustainingDecayingOrSlowEnvelope) {
    EXPECT_FALSE(checkPluck(trackWithEnvelope(R"({"attack": 0.005, "decay": 0.2, "sustain": 0.6})")).pass);
    EXPECT_FALSE(checkPluck(trackWithEnvelope(R"({"decay": 1.2, "sustain": 0})")).pass) << "decay too long";
    EXPECT_FALSE(checkPluck(trackWithEnvelope(R"({"attack": 0.4, "decay": 0.2, "sustain": 0})")).pass)
        << "attack too slow";
    const auto check = checkPluck(trackWithEnvelope(R"({"decay": 0.2})"));
    EXPECT_FALSE(check.pass) << "a left-out sustain stands at the module default (1), which is not plucky";
    EXPECT_TRUE(check.reason.contains("not plucky")) << check.reason;
}

TEST(SoundShapeChecksTest, PluckFailsWithNoEnvelopeAtAll) {
    EXPECT_FALSE(checkPluck(parse(R"({"nodes": [{"id": 1, "type": "Oscillator"}], "connections": []})")).pass);
    const auto sampler = checkPluck(parse(R"({"timelineOps": [{"op": "addInstrumentTrack", "name": "Keys",
        "instrument": "Sampler"}]})"));
    EXPECT_FALSE(sampler.pass) << "a Sampler track has no envelope to shape";
}

TEST(SoundShapeChecksTest, PluckAcceptsAPatchAdsrNodeToo) {
    EXPECT_TRUE(checkPluck(parse(R"({"nodes": [{"id": 5, "type": "Amp Env",
        "params": {"attack": 0.01, "decay": 0.15, "sustain": 0.0, "release": 0.1}}], "connections": []})"))
                    .pass);
    EXPECT_FALSE(checkPluck(parse(R"({"nodes": [{"id": 5, "type": "ADSR", "params": {"sustain": 0.8}}],
        "connections": []})"))
                     .pass);
}

// -- checkFilterEnvelope ----------------------------------------------------------------------------

TEST(SoundShapeChecksTest, FilterEnvelopePassesForATrackEnvelopeOntoAnInsertFilterCutoff) {
    const auto check = checkFilterEnvelope(acidResponse(R"({"cutoff": 400})", R"({"decay": 0.2})"));
    EXPECT_TRUE(check.pass) << check.reason;
}

TEST(SoundShapeChecksTest, FilterEnvelopePassesForAnExistingAdsrOntoAnExistingFilterByNameOrPort) {
    const auto existing = parse(kExistingPatch);
    EXPECT_TRUE(checkFilterEnvelope(parse(R"({"mode": "merge", "nodes": [], "connections": [],
        "modulations": [{"source": 1004, "dest": 1003, "destParam": "cutoff", "amount": 0.6}]})"),
                                    existing)
                    .pass);
    // Port 1 is the Filter's cutoff input; any other port is some other parameter.
    EXPECT_TRUE(
        checkFilterEnvelope(parse(R"({"modulations": [{"source": 1004, "dest": 1003, "destPort": 1}]})"), existing)
            .pass);
    EXPECT_FALSE(
        checkFilterEnvelope(parse(R"({"modulations": [{"source": 1004, "dest": 1003, "destPort": 2}]})"), existing)
            .pass);
}

TEST(SoundShapeChecksTest, FilterEnvelopePassesForAnAdsrNodeAndFilterNodeOfTheResponse) {
    EXPECT_TRUE(checkFilterEnvelope(parse(R"({"nodes": [{"id": 1, "type": "Filter Env"}, {"id": 2, "type": "Filter"}],
        "connections": [], "modulations": [{"source": 1, "dest": 2, "destParam": "cutoff"}]})"))
                    .pass);
}

// Asked for "a bass track with a filter envelope" in a project that
// already had a track, the model routed the OTHER track's ADSR onto the new track's Filter. That
// envelope is triggered by the other track's notes, so the bass filter never moves on its own notes.
TEST(SoundShapeChecksTest, FilterEnvelopeFailsWhenANewTracksFilterIsMovedByAnotherTracksEnvelope) {
    const juce::var otherTrack = parse(R"({"nodes": [{"id": 55, "type": "ADSR", "params": {"sustain": 0}}]})");
    const auto check = checkFilterEnvelope(acidResponse(R"({"cutoff": 400})", R"({"decay": 0.2})",
                                                        R"({"source": 55, "dest": 7011, "destParam": "cutoff"})"),
                                           otherTrack);
    EXPECT_FALSE(check.pass);
    EXPECT_TRUE(check.reason.contains("track's own")) << check.reason;
}

TEST(SoundShapeChecksTest, FilterEnvelopeFailsWhenTheModulationIsNotEnvelopeToFilterCutoff) {
    const auto existing = parse(kExistingPatch);
    // No modulation at all.
    EXPECT_FALSE(checkFilterEnvelope(parse(R"({"nodes": [], "connections": []})"), existing).pass);
    // An LFO is not an envelope.
    const auto lfo = checkFilterEnvelope(parse(R"({"modulations": [{"source": 1005, "dest": 1003,
        "destParam": "cutoff"}]})"),
                                         existing);
    EXPECT_FALSE(lfo.pass);
    EXPECT_TRUE(lfo.reason.contains("not an envelope")) << lfo.reason;
    // An envelope onto something other than a Filter.
    EXPECT_FALSE(checkFilterEnvelope(parse(R"({"nodes": [{"id": 9, "type": "VCA"}],
        "modulations": [{"source": 1004, "dest": 9, "destParam": "gain"}]})"),
                                     existing)
                     .pass);
    // An envelope onto a Filter parameter other than cutoff.
    const auto resonance = checkFilterEnvelope(
        parse(R"({"modulations": [{"source": 1004, "dest": 1003, "destParam": "resonance"}]})"), existing);
    EXPECT_FALSE(resonance.pass);
    EXPECT_TRUE(resonance.reason.contains("other than cutoff")) << resonance.reason;
    // An id nothing in the response or the patch names.
    EXPECT_FALSE(
        checkFilterEnvelope(parse(R"({"modulations": [{"source": 8, "dest": 1003, "destParam": "cutoff"}]})"), existing)
            .pass);
}

// -- checkAcid --------------------------------------------------------------------------------------

TEST(SoundShapeChecksTest, AcidPassesForTheLowPassWithHighResonanceAndAShortSustainlessEnvelope) {
    const auto check = checkAcid(acidResponse(R"({"filterType": "LPF24", "cutoff": 400, "resonance": 0.8})",
                                              R"({"decay": 0.25, "sustain": 0.1})"));
    EXPECT_TRUE(check.pass) << check.reason;
    // The Filter's default type IS the 24 dB low-pass, so leaving filterType out is fine.
    EXPECT_TRUE(checkAcid(acidResponse(R"({"resonance": 0.7})", R"({"sustain": 0})")).pass);
}

TEST(SoundShapeChecksTest, AcidFailsOnTheWrongFilterTypeLowResonanceOrASustainingEnvelope) {
    const auto wrongType =
        checkAcid(acidResponse(R"({"filterType": "HPF24", "resonance": 0.8})", R"({"sustain": 0.1})"));
    EXPECT_FALSE(wrongType.pass);
    EXPECT_TRUE(wrongType.reason.contains("24 dB low-pass")) << wrongType.reason;
    EXPECT_FALSE(checkAcid(acidResponse(R"({"filterType": "LPF12", "resonance": 0.8})", R"({"sustain": 0.1})")).pass);

    const auto lowResonance = checkAcid(acidResponse(R"({"resonance": 0.3})", R"({"sustain": 0.1})"));
    EXPECT_FALSE(lowResonance.pass);
    EXPECT_TRUE(lowResonance.reason.contains("resonance")) << lowResonance.reason;
    EXPECT_FALSE(checkAcid(acidResponse(R"({})", R"({"sustain": 0.1})")).pass) << "the default resonance (0.1) is low";

    const auto sustaining = checkAcid(acidResponse(R"({"resonance": 0.8})", R"({"sustain": 0.7})"));
    EXPECT_FALSE(sustaining.pass);
    EXPECT_TRUE(sustaining.reason.contains("sustain")) << sustaining.reason;
    EXPECT_FALSE(checkAcid(acidResponse(R"({"resonance": 0.8})", R"({})")).pass) << "the default sustain is 1";
}

TEST(SoundShapeChecksTest, AcidFailsWithoutAnEnvelopeOnTheCutoff) {
    const auto onResonance = checkAcid(acidResponse(R"({"resonance": 0.8})", R"({"sustain": 0})",
                                                    R"({"source": 7010, "dest": 7011, "destParam": "resonance"})"));
    EXPECT_FALSE(onResonance.pass);
    EXPECT_FALSE(checkAcid(parse(R"({"nodes": [], "connections": []})")).pass);
}

TEST(SoundShapeChecksTest, AcidNeedsASawOrSquareOscillator) {
    const juce::String filter = R"({"resonance": 0.8})", envelope = R"({"sustain": 0.1})";
    const juce::String mod = R"({"source": 7010, "dest": 7011, "destParam": "cutoff"})";
    EXPECT_TRUE(checkAcid(acidResponse(filter, envelope, mod, R"({"waveform": "Square"})")).pass);
    EXPECT_TRUE(checkAcid(acidResponse(filter, envelope, mod, R"({"waveform": 2})")).pass) << "index 2 is Saw";

    const auto sine = checkAcid(acidResponse(filter, envelope, mod, R"({"waveform": "Sine"})"));
    EXPECT_FALSE(sine.pass);
    EXPECT_TRUE(sine.reason.contains("Saw or Square")) << sine.reason;
    EXPECT_FALSE(checkAcid(acidResponse(filter, envelope, mod, R"({})")).pass) << "the default waveform is a Sine";
}

TEST(SoundShapeChecksTest, AcidAcceptsAnExistingOscillatorNodeWithASawWaveform) {
    const auto existing = parse(R"({"nodes": [{"id": 1, "type": "Oscillator", "params": {"waveform": "Saw"}},
        {"id": 1003, "type": "Filter", "params": {"resonance": 0.8}}, {"id": 1004, "type": "ADSR",
        "params": {"sustain": 0.1}}], "connections": []})");
    const auto response = parse(R"({"modulations": [{"source": 1004, "dest": 1003, "destParam": "cutoff"}]})");
    EXPECT_TRUE(checkAcid(response, existing).pass);
    const auto sineExisting = parse(R"({"nodes": [{"id": 1, "type": "Oscillator"},
        {"id": 1003, "type": "Filter", "params": {"resonance": 0.8}}, {"id": 1004, "type": "ADSR",
        "params": {"sustain": 0.1}}], "connections": []})");
    EXPECT_FALSE(checkAcid(response, sineExisting).pass);
}
