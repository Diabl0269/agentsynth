// The status bar voice count of the Sampler (Sample mode), Oscillator and Wavetable: held notes count, a note shorter
// than the status bar's 200 ms poll still shows, and the count falls to 0 once the hold passes.

#include "AudioEngine/AudioEngine.h"
#include "Modules/OscillatorModule.h"
#include "Modules/SamplerModule.h"
#include "Modules/VoiceCountSource.h"
#include "Modules/WavetableOscillatorModule/WavetableOscillatorModule.h"
#include "Sampler/SamplerTestWavs.h"
#include <gtest/gtest.h>

namespace {

constexpr double kRate = 48000.0;
constexpr int kBlock = 256;

// Renders `blocks` blocks; `events(block, midi)` fills each block's MIDI.
template <class Module, class Events>
void render(Module& m, int blocks, Events events, int firstBlock = 0) {
    for (int b = 0; b < blocks; ++b) {
        juce::AudioBuffer<float> buf(m.getTotalNumOutputChannels(), kBlock);
        buf.clear();
        juce::MidiBuffer midi;
        events(firstBlock + b, midi);
        m.processBlock(buf, midi);
    }
}

void on(juce::MidiBuffer& m, int note, int at = 0) {
    m.addEvent(juce::MidiMessage::noteOn(1, note, (juce::uint8)100), at);
}
void off(juce::MidiBuffer& m, int note, int at = 0) { m.addEvent(juce::MidiMessage::noteOff(1, note), at); }

constexpr int kHoldBlocks = (int)(0.25 * kRate / kBlock) + 1; // past the 200 ms hold

// The same four checks for every module that counts held MIDI notes itself.
template <class Module>
void expectNoteCounting(Module& m, int polyChordExpected) {
    m.prepareToPlay(kRate, kBlock);
    auto& source = static_cast<synth::VoiceCountSource&>(m);
    EXPECT_EQ(source.displayVoiceCount(), 0);

    // A 50 ms note, on and off inside one block, is still visible right after.
    render(m, 1, [](int, juce::MidiBuffer& midi) {
        on(midi, 60, 0);
        off(midi, 60, 100);
    });
    EXPECT_EQ(source.displayVoiceCount(), 1) << "a note shorter than a block shows";
    render(m, kHoldBlocks, [](int, juce::MidiBuffer&) {}, 1);
    EXPECT_EQ(source.displayVoiceCount(), 0) << "and falls to zero after the hold";

    // Two single short notes with a gap read 1 after every block, never 2.
    int worst = 0;
    for (int b = 0; b < 2 * kHoldBlocks; ++b) {
        render(
            m, 1,
            [&](int blk, juce::MidiBuffer& midi) {
                if (blk == 0)
                    on(midi, 60);
                if (blk == 10)
                    off(midi, 60);
                if (blk == 20)
                    on(midi, 64);
                if (blk == 30)
                    off(midi, 64);
            },
            b);
        worst = std::max(worst, source.displayVoiceCount());
    }
    EXPECT_EQ(worst, 1);
    EXPECT_EQ(source.displayVoiceCount(), 0);

    // A held chord counts the notes held; releasing it reads 0 after the hold.
    render(m, 4, [](int b, juce::MidiBuffer& midi) {
        if (b == 0)
            for (int n : {60, 64, 67})
                on(midi, n);
    });
    EXPECT_EQ(source.displayVoiceCount(), polyChordExpected);
    render(m, 1, [](int, juce::MidiBuffer& midi) {
        for (int n : {60, 64, 67})
            off(midi, n);
    });
    render(m, kHoldBlocks, [](int, juce::MidiBuffer&) {});
    EXPECT_EQ(source.displayVoiceCount(), 0);
}

} // namespace

TEST(VoiceCountLatch, HoldsThePeakForTwoHundredMillisecondsThenReadsZero) {
    synth::VoiceCountLatch latch;
    const int window = (int)(synth::VoiceCountLatch::kHoldSeconds * kRate);
    latch.publish(3, kBlock, kRate);
    latch.publish(0, kBlock, kRate);
    EXPECT_EQ(latch.value(), 3) << "the peak holds after the notes end";
    for (int s = kBlock; s < window - kBlock; s += kBlock)
        latch.publish(0, kBlock, kRate);
    EXPECT_EQ(latch.value(), 3) << "still inside the window";
    for (int s = 0; s < 2 * kBlock; s += kBlock)
        latch.publish(0, kBlock, kRate);
    EXPECT_EQ(latch.value(), 0) << "past the window with nothing held";
}

TEST(VoiceCountLatch, TwoShortNotesInARowReadOneNeverTwo) {
    synth::VoiceCountLatch latch;
    latch.publish(1, kBlock, kRate);
    latch.publish(0, kBlock, kRate);
    latch.publish(0, kBlock, kRate);
    latch.publish(1, kBlock, kRate);
    EXPECT_EQ(latch.value(), 1);
}

TEST(VoiceCountLatch, HeldNotesStayAtTheirCountAndResetClearsAtOnce) {
    synth::VoiceCountLatch latch;
    for (int i = 0; i < 400; ++i)
        latch.publish(2, kBlock, kRate);
    EXPECT_EQ(latch.value(), 2);
    latch.publish(1, kBlock, kRate);
    EXPECT_EQ(latch.value(), 2) << "a released key still reads the peak until the hold passes";
    latch.reset();
    EXPECT_EQ(latch.value(), 0);
}

TEST(VoiceCountLatch, ASourceChangeDropsTheOtherSourcesPeak) {
    synth::VoiceCountLatch latch;
    latch.publish(3, kBlock, kRate, 1);
    latch.publish(0, kBlock, kRate, 2);
    EXPECT_EQ(latch.value(), 0);
}

TEST(VoiceCountSources, SamplerInSampleModeCountsHeldNotes) {
    sampler_test::TempDir dir;
    ASSERT_TRUE(sampler_test::writeWav(dir.file("dc.wav"), 96000, 1, kRate, [](int, int) { return 0.2f; }));
    SamplerModule m;
    m.prepareToPlay(kRate, kBlock);
    ASSERT_TRUE(m.loadSampleFile(dir.file("dc.wav")));
    expectNoteCounting(m, 1); // Sample mode plays one voice whatever the chord
}

TEST(VoiceCountSources, OscillatorCountsItsOwnMidiNotes) {
    OscillatorModule mono;
    expectNoteCounting(mono, 1);
    OscillatorModule poly;
    for (auto* p : poly.getParameters())
        if (auto* pp = dynamic_cast<juce::AudioProcessorParameterWithID*>(p); pp != nullptr && pp->paramID == "poly")
            pp->setValueNotifyingHost(1.0f);
    expectNoteCounting(poly, 3);
}

TEST(VoiceCountSources, WavetableCountsItsOwnMidiNotes) {
    WavetableOscillatorModule mono;
    expectNoteCounting(mono, 1);
    WavetableOscillatorModule poly;
    for (auto* p : poly.getParameters())
        if (auto* pp = dynamic_cast<juce::AudioProcessorParameterWithID*>(p); pp != nullptr && pp->paramID == "poly")
            pp->setValueNotifyingHost(1.0f);
    expectNoteCounting(poly, 3);
}

TEST(VoiceCountSources, AnOscillatorFedOnlyByGateAndPitchCvCountsNothing) {
    OscillatorModule poly;
    for (auto* p : poly.getParameters())
        if (auto* pp = dynamic_cast<juce::AudioProcessorParameterWithID*>(p); pp != nullptr && pp->paramID == "poly")
            pp->setValueNotifyingHost(1.0f);
    poly.prepareToPlay(kRate, kBlock);
    juce::AudioBuffer<float> buf(poly.getTotalNumOutputChannels(), kBlock);
    buf.clear();
    for (int i = 0; i < kBlock; ++i)
        buf.setSample(OscillatorModule::kGateBase, i, 1.0f); // a Poly MIDI module's gate, no MIDI of its own
    juce::MidiBuffer none;
    poly.processBlock(buf, none);
    EXPECT_EQ(poly.displayVoiceCount(), 0) << "the Poly MIDI module counts those notes";
}

TEST(VoiceCountSources, EngineSumShowsAShortOscillatorNote) {
    AudioEngine engine;
    auto node = engine.getGraph().addNode(std::make_unique<OscillatorModule>());
    auto* osc = dynamic_cast<OscillatorModule*>(node->getProcessor());
    ASSERT_NE(osc, nullptr);
    osc->prepareToPlay(kRate, kBlock);
    EXPECT_EQ(engine.getDisplayVoiceCount(), 0);
    render(*osc, 1, [](int, juce::MidiBuffer& midi) {
        on(midi, 60, 0);
        off(midi, 60, 100);
    });
    EXPECT_EQ(engine.getDisplayVoiceCount(), 1);
}
