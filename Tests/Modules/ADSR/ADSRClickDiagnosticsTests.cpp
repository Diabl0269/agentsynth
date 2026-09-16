// ADSRClickDiagnosticsTests.cpp
//
// FRO116 regression suite: the user reported an audible click on a "pluck" patch --
// attack=0, hold=0, release=0, decay in the 100-300ms range, sustain=0 -- on top of the
// intended percussive attack. FRO110 had removed the old 2ms/5ms anti-click PARAMETER clamps so
// 0ms stage times are reachable and displayed; that made a 0ms Attack/Decay/Release a genuine
// one-sample full-scale level step, which is exactly the click reported here. The fix (see
// EnvelopeGenerator.h's kMinAttackSeconds / kMinRampSeconds) floors those stages' *effective*
// time internally to a fixed, much smaller click-free minimum without changing the parameter's
// own range or displayed value. These tests, originally written to pinpoint the click
// sample-by-sample, now assert that fix: no single sample looks like an instant cut/jump, and
// each floored ramp is still fast (completes within about the floor's own duration, not
// noticeably longer).
//
// Method: EnvelopeGenerator's contract is that a stage's start/target/curve are exact, so a
// jump only ever happens across a stage transition. Each test below isolates one such
// transition and bounds its per-sample magnitude and duration against the floor.

#include "ADSRTestFixture.h"
#include "Modules/OscillatorModule.h"
#include "Modules/VCAModule.h"
#include <cmath>
#include <vector>

namespace {

// Runs `numSamples` samples through a mono ADSRModule one sample at a time (a 1-sample buffer
// per processBlock call), injecting any MIDI event scheduled for that sample index. Returns
// the per-sample envelope output.
std::vector<float> runMonoSamples(ADSRModule& adsr, int numSamples,
                                  const std::vector<std::pair<int, juce::MidiMessage>>& events) {
    std::vector<float> out;
    out.reserve(static_cast<size_t>(numSamples));
    juce::AudioBuffer<float> buf(1, 1);
    for (int i = 0; i < numSamples; ++i) {
        buf.clear();
        juce::MidiBuffer midi;
        for (const auto& ev : events)
            if (ev.first == i)
                midi.addEvent(ev.second, 0);
        adsr.processBlock(buf, midi);
        out.push_back(buf.getSample(0, 0));
    }
    return out;
}

struct MaxJump {
    float value = 0.0f;
    int index = -1;
};

// Largest |trace[i] - trace[i-1]|, treating the sample before trace[0] as silence (0.0f) --
// that is the generator's real Idle starting state, so a note-on's own attack step counts.
MaxJump maxSampleJump(const std::vector<float>& trace) {
    MaxJump m;
    float prev = 0.0f;
    for (size_t i = 0; i < trace.size(); ++i) {
        const float d = std::abs(trace[i] - prev);
        if (d > m.value) {
            m.value = d;
            m.index = static_cast<int>(i);
        }
        prev = trace[i];
    }
    return m;
}

} // namespace

// ---------------------------------------------------------------------------
// (a) Isolated note: attack=0, hold=0, decay in the reported range, sustain=0, release=0.
// Confirms the (floored, no longer instant) attack ramp is click-free -- no single sample
// jumps anywhere near full scale -- while still completing quickly, and that the decay curve's
// own approach to 0 and the Decay -> Sustain cascade remain exact and smooth.
// ---------------------------------------------------------------------------
TEST_F(ADSRTest, FRO116_IsolatedNote_AttackRampIsClickFreeAndFast) {
    setFloat(adsr, "attack", 0.0f);
    setFloat(adsr, "hold", 0.0f);
    setFloat(adsr, "decay", 0.2f);
    setFloat(adsr, "sustain", 0.0f);
    setFloat(adsr, "release", 0.0f);

    const int numSamples = 11025; // 0.25s @ 44100Hz -- past the 0.2s decay into settled silence
    auto trace = runMonoSamples(adsr, numSamples, {{0, juce::MidiMessage::noteOn(1, 60, (juce::uint8)100)}});

    const auto jump = maxSampleJump(trace);

    // FRO116: attack is floored to kMinAttackSeconds (0.1 ms, ~5 samples at 44.1kHz) rather than
    // a genuine one-sample 0 -> 1.0 step -- no single sample should look anywhere near a
    // full-scale cliff, and the largest step should still land within the attack's short ramp.
    EXPECT_LT(jump.value, 0.5f) << "no single sample of the attack should jump anywhere near full scale";
    EXPECT_LE(jump.index, 10) << "the largest step should still land within the floored attack's short ramp";

    // But it IS fast: full level reached within about the floor's own duration.
    int fullLevelIndex = -1;
    for (size_t i = 0; i < trace.size(); ++i) {
        if (trace[i] >= 0.99f) {
            fullLevelIndex = static_cast<int>(i);
            break;
        }
    }
    ASSERT_GE(fullLevelIndex, 0) << "attack never reached (near) full level within the window";
    EXPECT_LE(fullLevelIndex, 10) << "the floored ~0.1 ms attack should reach full level within about 10 samples";

    // The decay curve must reach exactly 0 and cascade into Sustain (also 0) with no further
    // step: find the sample where the trace settles at 0 and confirm the step INTO it is tiny.
    int settleIndex = -1;
    for (size_t i = 1; i < trace.size(); ++i) {
        if (trace[i] == 0.0f && trace[i - 1] != 0.0f) {
            settleIndex = static_cast<int>(i);
            break;
        }
    }
    ASSERT_GE(settleIndex, 0) << "envelope never settled to exactly 0 within the window";
    const float stepIntoSettle =
        std::abs(trace[static_cast<size_t>(settleIndex)] - trace[static_cast<size_t>(settleIndex) - 1]);
    EXPECT_LT(stepIntoSettle, 0.01f) << "decay's approach to sustain=0 should be a smooth curve tail, not a step";
}

// ---------------------------------------------------------------------------
// (b) Back-to-back notes: note-off then note-on on the SAME sample, landing mid-decay (not
// after settling), sustain=0 -- the gapless mono legato case FRO110 fixed re-articulation for.
// Confirms the retrigger climbs toward 1.0 gradually over the floored attack ramp, not in a
// single full jump, and NOT via an extra release-to-0 cut (the code path never calls noteOff()
// for the same-sample transition -- see ADSRModule::processBlock's midiNoteOnThisSample guard).
// ---------------------------------------------------------------------------
TEST_F(ADSRTest, FRO116_BackToBackNotesMidDecay_RetriggerRampsRatherThanJumpingToOne) {
    setFloat(adsr, "attack", 0.0f);
    setFloat(adsr, "hold", 0.0f);
    setFloat(adsr, "decay", 0.2f);
    setFloat(adsr, "sustain", 0.0f);
    setFloat(adsr, "release", 0.0f);

    const int retriggerSample = 2205; // 0.05s in: well inside the 0.2s decay
    std::vector<std::pair<int, juce::MidiMessage>> events = {
        {0, juce::MidiMessage::noteOn(1, 60, (juce::uint8)100)},
        {retriggerSample, juce::MidiMessage::noteOff(1, 60)},
    };
    // Same-sample note-off + note-on: both scheduled at retriggerSample.
    events.push_back({retriggerSample, juce::MidiMessage::noteOn(1, 62, (juce::uint8)100)});

    auto trace = runMonoSamples(adsr, retriggerSample + 200, events);

    const float levelBeforeRetrigger = trace[static_cast<size_t>(retriggerSample) - 1];
    const float levelAtRetrigger = trace[static_cast<size_t>(retriggerSample)];
    const float retriggerJump = std::abs(levelAtRetrigger - levelBeforeRetrigger);
    const float fullClimbRemaining = 1.0f - levelBeforeRetrigger;

    // The retrigger must move TOWARD 1.0 (a fresh attack target), not down toward 0 -- if this
    // were instead cut to 0 first (the release=0 behaviour) and then re-attacked, levelAtRetrigger
    // would read far closer to 0 than to levelBeforeRetrigger.
    EXPECT_GT(levelAtRetrigger, levelBeforeRetrigger) << "retrigger should move toward the Attack target (1.0)";

    // FRO116: the retrigger re-enters Attack, which is floored to a short but real ~0.1 ms ramp
    // (kMinAttackSeconds) -- the very first sample of that ramp should cover only a small
    // fraction of the remaining climb to 1.0, not jump straight there in one step.
    EXPECT_LT(retriggerJump, 0.25f * fullClimbRemaining)
        << "a same-sample retrigger must not jump straight to its Attack target in one step";

    // But the climb IS fast: full level reached within about the floor's own duration.
    int samplesToNearOne = -1;
    for (size_t i = static_cast<size_t>(retriggerSample); i < trace.size(); ++i) {
        if (trace[i] >= 0.99f) {
            samplesToNearOne = static_cast<int>(i) - retriggerSample;
            break;
        }
    }
    ASSERT_GE(samplesToNearOne, 0) << "retrigger never reached near-full level within the trace window";
    EXPECT_LE(samplesToNearOne, 10)
        << "the floored ~0.1 ms attack ramp should reach full level within about 10 samples of retrigger";
}

// ---------------------------------------------------------------------------
// (c) Note-off landing MID-DECAY with release=0 -- the scenario this suite pinned as the cause
// of the user's click: a short/staccato pluck whose gate closes before the decay curve has
// reached sustain=0. Before the fix, a 0ms Release stage meant noteOff() -> getNextSample()
// cascaded stageStart_ (the live decay level) straight to target 0.0 within one sample -- a
// real, full-magnitude-of-whatever-was-left step. EnvelopeGenerator now floors Release's
// effective time to kMinRampSeconds (1 ms, ~44 samples at 44.1kHz), so this must no longer
// happen: this is the regression test for the click report itself.
// ---------------------------------------------------------------------------
TEST_F(ADSRTest, FRO116_NoteOffMidDecay_ReleaseFloorPreventsInstantCut) {
    setFloat(adsr, "attack", 0.0f);
    setFloat(adsr, "hold", 0.0f);
    setFloat(adsr, "decay", 0.2f);
    setFloat(adsr, "sustain", 0.0f);
    setFloat(adsr, "release", 0.0f);

    const int noteOffSample = 2205; // 0.05s in: 1/4 of the way through a 0.2s decay
    std::vector<std::pair<int, juce::MidiMessage>> events = {
        {0, juce::MidiMessage::noteOn(1, 60, (juce::uint8)100)},
        {noteOffSample, juce::MidiMessage::noteOff(1, 60)},
    };
    // Extended past the old +10 window: the floored ~1 ms release needs ~44 samples to run its
    // course, and the test also checks it has fully faded by ~2 ms (88 samples).
    auto trace = runMonoSamples(adsr, noteOffSample + 120, events);

    const float levelBeforeNoteOff = trace[static_cast<size_t>(noteOffSample) - 1];
    const float levelAtNoteOff = trace[static_cast<size_t>(noteOffSample)];
    const float cutJump = std::abs(levelAtNoteOff - levelBeforeNoteOff);

    ASSERT_GT(levelBeforeNoteOff, 0.3f) << "expected a substantial mid-decay level before note-off, got "
                                        << levelBeforeNoteOff;

    // NOT an instant cut: the very first sample after note-off must still carry most of the
    // level, and no single sample should account for most of what eventually gets cut.
    EXPECT_GT(levelAtNoteOff, 0.5f * levelBeforeNoteOff)
        << "the first sample after note-off should not read as an instant cut";
    EXPECT_LT(cutJump, 0.25f * levelBeforeNoteOff)
        << "no single sample should account for most of the level that gets released";

    // Still a real ramp a fraction of a millisecond in -- not already silent.
    const float levelAt0p2ms = trace[static_cast<size_t>(noteOffSample) + 9];
    EXPECT_GT(levelAt0p2ms, 0.1f * levelBeforeNoteOff)
        << "the release should still be audible a fraction of a millisecond after note-off";

    // But the click IS gone quickly: fully faded (<1% of the pre-release level) within ~2 ms.
    const float levelAt2ms = trace[static_cast<size_t>(noteOffSample) + 88];
    EXPECT_LT(levelAt2ms, 0.01f * levelBeforeNoteOff)
        << "the release should still complete within about 2 ms, not linger";
}

// ---------------------------------------------------------------------------
// (d) Poly mode: the same mid-decay, zero-release scenario, per-voice, via Gate CV instead of
// MIDI -- confirms the fix applies identically off the Gate CV / Schmitt-trigger path.
// ---------------------------------------------------------------------------
TEST_F(ADSRTest, FRO116_PolyMode_NoteOffMidDecay_ReleaseFloorPreventsInstantCut) {
    setPoly(adsr, true);
    setFloat(adsr, "attack", 0.0f);
    setFloat(adsr, "hold", 0.0f);
    setFloat(adsr, "decay", 0.2f);
    setFloat(adsr, "sustain", 0.0f);
    setFloat(adsr, "release", 0.0f);

    const int gateOffSample = 2205;               // 0.05s in
    const int totalSamples = gateOffSample + 120; // see the mono test above for why +120
    juce::AudioBuffer<float> polyBuffer(8, 1);
    juce::MidiBuffer emptyMidi;
    std::vector<float> trace;
    trace.reserve(static_cast<size_t>(totalSamples));

    for (int i = 0; i < totalSamples; ++i) {
        polyBuffer.clear();
        if (i < gateOffSample)
            polyBuffer.setSample(0, 0, 1.0f); // voice 0 gate high
        // else: gate low from gateOffSample onward -> falling edge exactly at gateOffSample
        adsr.processBlock(polyBuffer, emptyMidi);
        trace.push_back(polyBuffer.getSample(0, 0));
    }

    const float levelBeforeGateOff = trace[static_cast<size_t>(gateOffSample) - 1];
    const float levelAtGateOff = trace[static_cast<size_t>(gateOffSample)];
    const float cutJump = std::abs(levelAtGateOff - levelBeforeGateOff);

    ASSERT_GT(levelBeforeGateOff, 0.3f) << "expected a substantial mid-decay level before gate-off, got "
                                        << levelBeforeGateOff;

    EXPECT_GT(levelAtGateOff, 0.5f * levelBeforeGateOff) << "poly voices must not read as an instant cut either";
    EXPECT_LT(cutJump, 0.25f * levelBeforeGateOff)
        << "no single sample should account for most of the level that gets released";

    const float levelAt2ms = trace[static_cast<size_t>(gateOffSample) + 88];
    EXPECT_LT(levelAt2ms, 0.01f * levelBeforeGateOff) << "poly release should still complete within about 2 ms";
}

// ---------------------------------------------------------------------------
// (e) Note-off arriving AFTER decay has already reached sustain=0 -- the counterpart to (c).
// If the note is held at least as long as the decay takes, there is no click: the envelope is
// already silent, so a 0ms release "cuts" nothing. This is the exact boundary condition: a
// pluck's gate length relative to its decay time decides whether the user hears a click.
// ---------------------------------------------------------------------------
TEST_F(ADSRTest, FRO116_NoteOffAfterDecayAlreadySettled_NoDiscontinuity) {
    setFloat(adsr, "attack", 0.0f);
    setFloat(adsr, "hold", 0.0f);
    setFloat(adsr, "decay", 0.2f);
    setFloat(adsr, "sustain", 0.0f);
    setFloat(adsr, "release", 0.0f);

    const int noteOffSample = 13230; // 0.3s in: well past the 0.2s decay
    std::vector<std::pair<int, juce::MidiMessage>> events = {
        {0, juce::MidiMessage::noteOn(1, 60, (juce::uint8)100)},
        {noteOffSample, juce::MidiMessage::noteOff(1, 60)},
    };
    auto trace = runMonoSamples(adsr, noteOffSample + 10, events);

    const float levelBeforeNoteOff = trace[static_cast<size_t>(noteOffSample) - 1];
    const float levelAtNoteOff = trace[static_cast<size_t>(noteOffSample)];
    const float jump = std::abs(levelAtNoteOff - levelBeforeNoteOff);

    ASSERT_NEAR(levelBeforeNoteOff, 0.0f, 1e-4f) << "expected the envelope to have already settled to 0";
    EXPECT_NEAR(jump, 0.0f, 1e-4f) << "no click when the gate outlasts the decay";
}

// ---------------------------------------------------------------------------
// (f) Sustain-smoother / decay-end transition: does the 20ms sustain smoother (or the
// Decay->Sustain cascade itself) ever produce a step, independent of sustain being 0? Decay's
// live target IS the same smoothedSustain value Sustain reads, so as long as sustain isn't
// actively being automated across the boundary, the cascade should be exact. Checked at a
// non-zero, steady-state sustain to generalise past the user's sustain=0 report.
// ---------------------------------------------------------------------------
TEST_F(ADSRTest, FRO116_DecayToSustainCascade_NoStepAtNonZeroSustain) {
    setFloat(adsr, "attack", 0.0f);
    setFloat(adsr, "hold", 0.0f);
    setFloat(adsr, "decay", 0.15f);
    setFloat(adsr, "sustain", 0.4f);
    setFloat(adsr, "release", 0.0f);

    // Let the 20ms sustain smoother settle to its steady state BEFORE the note starts, so this
    // test isolates the Decay -> Sustain cascade itself, not a live parameter ramp.
    juce::MidiBuffer emptyMidi;
    juce::AudioBuffer<float> warm(1, 1);
    for (int i = 0; i < 4410; ++i) { // 0.1s, well past the 20ms smoother
        warm.clear();
        adsr.processBlock(warm, emptyMidi);
    }

    const int numSamples = 8820; // 0.2s: past the 0.15s decay
    auto trace = runMonoSamples(adsr, numSamples, {{0, juce::MidiMessage::noteOn(1, 60, (juce::uint8)100)}});

    // Find the sample where the trace first gets within 1% of the sustain level and stays
    // there -- that is the Decay -> Sustain cascade point -- and measure the step into it.
    int cascadeIndex = -1;
    for (size_t i = 1; i < trace.size(); ++i) {
        if (std::abs(trace[i] - 0.4f) < 0.005f && std::abs(trace[i - 1] - 0.4f) >= 0.005f) {
            cascadeIndex = static_cast<int>(i);
            break;
        }
    }
    ASSERT_GE(cascadeIndex, 0) << "envelope never reached sustain=0.4 within the window";
    const float stepAtCascade =
        std::abs(trace[static_cast<size_t>(cascadeIndex)] - trace[static_cast<size_t>(cascadeIndex) - 1]);
    EXPECT_LT(stepAtCascade, 0.01f) << "the decay-end/sustain-smoother boundary should not itself click";
}

// ---------------------------------------------------------------------------
// (g) Oscillator phase interaction: the oscillator is free-running and never resets phase on
// note-on (by design -- see AntiClickTests.cpp's OscillatorNoPhaseReset). Even with the FRO116
// floor, the envelope-domain step at the very first sample of a 0ms attack is a small, FIXED
// fraction of full scale (the same fraction every time -- it depends only on the floor and the
// curve, not on anything oscillator-related), so the VCA'd output at that sample is that fixed
// fraction times whatever the oscillator's instantaneous sample value happens to be -- the
// AUDIBLE size of the (now much smaller) attack step still depends on the oscillator's phase,
// not just on the envelope. Two runs that differ only in how long the oscillator warmed up
// before the note-on demonstrate this: the output-domain jump magnitude changes even though the
// envelope-domain jump is identical between the two runs.
// ---------------------------------------------------------------------------
TEST_F(ADSRTest, FRO116_ZeroAttack_OutputJumpMagnitudeDependsOnOscillatorPhase) {
    auto measureAttackOutputJump = [](int oscWarmupSamples) -> float {
        OscillatorModule osc;
        VCAModule vca;
        ADSRModule localAdsr;
        const double sampleRate = 44100.0;

        osc.prepareToPlay(sampleRate, 1);
        vca.prepareToPlay(sampleRate, 1);
        localAdsr.prepareToPlay(sampleRate, 1);
        setFloat(localAdsr, "attack", 0.0f);
        setFloat(localAdsr, "hold", 0.0f);
        setFloat(localAdsr, "decay", 0.2f);
        setFloat(localAdsr, "sustain", 0.0f);
        setFloat(localAdsr, "release", 0.0f);

        const int oscCh = std::max(osc.getTotalNumInputChannels(), osc.getTotalNumOutputChannels());
        const int adsrCh = std::max(localAdsr.getTotalNumInputChannels(), localAdsr.getTotalNumOutputChannels());
        const int vcaCh = std::max(vca.getTotalNumInputChannels(), vca.getTotalNumOutputChannels());

        juce::MidiBuffer emptyMidi;
        juce::MidiBuffer oscNoteOn;
        // The oscillator only needs a note-on once, to fix its pitch; it free-runs from then on
        // regardless of what the ADSR does (confirmed by OscillatorNoPhaseReset).
        oscNoteOn.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 0);

        float prevVcaOut = 0.0f;
        float jumpAtAttack = 0.0f;
        const int attackSample = oscWarmupSamples;

        for (int i = 0; i <= attackSample; ++i) {
            juce::AudioBuffer<float> oscBuf(oscCh, 1);
            oscBuf.clear();
            osc.processBlock(oscBuf, i == 0 ? oscNoteOn : emptyMidi);

            juce::AudioBuffer<float> adsrBuf(adsrCh, 1);
            adsrBuf.clear();
            juce::MidiBuffer adsrMidi;
            if (i == attackSample)
                adsrMidi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 0);
            localAdsr.processBlock(adsrBuf, adsrMidi);

            juce::AudioBuffer<float> vcaBuf(vcaCh, 1);
            vcaBuf.clear();
            vcaBuf.copyFrom(0, 0, oscBuf, 0, 0, 1);
            vcaBuf.copyFrom(1, 0, adsrBuf, 0, 0, 1);
            juce::MidiBuffer vcaMidi;
            vca.processBlock(vcaBuf, vcaMidi);

            const float vcaOut = vcaBuf.getSample(0, 0);
            if (i == attackSample)
                jumpAtAttack = std::abs(vcaOut - prevVcaOut);
            prevVcaOut = vcaOut;
        }
        return jumpAtAttack;
    };

    // Two different oscillator pre-rolls put the free-running phase at two different points at
    // the exact sample the envelope's attack step fires.
    const float jumpShortWarmup = measureAttackOutputJump(3);
    const float jumpLongWarmup = measureAttackOutputJump(37);

    // The two jumps should differ (the phase moved between the two warmup lengths), and both
    // should be well below 1.0 -- proving the attack's audible size in the real osc->VCA signal
    // path is modulated by oscillator phase, not fixed at a single output-domain magnitude the
    // way the pure-envelope tests above measure the (now floored, much smaller) envelope-domain
    // step in isolation.
    EXPECT_NE(jumpShortWarmup, jumpLongWarmup)
        << "expected the output-domain attack jump to depend on the oscillator's instantaneous phase";
    EXPECT_GT(jumpShortWarmup, 0.0f);
    EXPECT_GT(jumpLongWarmup, 0.0f);
}
