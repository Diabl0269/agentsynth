// WavetableLiveDisplayTests.cpp
// The Wavetable display follows the Position (and Warp) the oscillator plays: knob plus cable while a note sounds,
// the knob at rest. Audio-driven: a cable's CV is written into the module's input channels, blocks are rendered,
// and the module's live getters and the card's display are read back.
// Regression test for FRO1728: the display read the Position knob, so an LFO on Position swept the sound while the
// drawn frame stood still.

#include "UI/Layout/ReducedMotion.h"
#include "UI/ModuleViews/WavetableDisplayComponent.h"
#include "WavetableOscillatorModuleTestFixture.h"
#include <functional>
#include <set>

namespace {

using WT = WavetableOscillatorModule;
constexpr int kChannels = WT::kNumOutputs;

struct MotionMode {
    explicit MotionMode(synth::ui::AnimationMode mode) { synth::ui::setAnimationMode(mode); }
    ~MotionMode() { synth::ui::setAnimationMode(synth::ui::AnimationMode::followSystem); }
};

// One block's inputs: a constant (or per-sample) CV on any channel, MIDI, and the module rendered over them.
struct Rig {
    WT& module;
    bool poly = false;

    void render(const std::function<void(juce::AudioBuffer<float>&)>& fill = {}, juce::MidiBuffer midi = {}) {
        juce::AudioBuffer<float> buf(kChannels, kBlockSize);
        buf.clear();
        if (fill)
            fill(buf);
        module.processBlock(buf, midi);
    }

    void renderCV(int jack, float value, int blocks = 4) {
        for (int b = 0; b < blocks; ++b)
            render([&](juce::AudioBuffer<float>& buf) {
                juce::FloatVectorOperations::fill(buf.getWritePointer(WT::modCVChannelFor(jack, poly)), value,
                                                  kBlockSize);
            });
    }
};

juce::MidiBuffer noteOn(int note = 60) {
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, note, (juce::uint8)100), 0);
    return midi;
}

juce::MidiBuffer noteOff(int note = 60) {
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOff(1, note), 0);
    return midi;
}

} // namespace

TEST_F(WavetableOscillatorModuleTest, AConstantPositionCableShowsKnobPlusCable) {
    setFloat(*module, "position", 0.2f);
    Rig rig{*module};
    rig.renderCV(WT::kJackPosition, 0.5f);
    EXPECT_NEAR(module->getModulatedScanPosition(), 0.7f, 1.0e-4f);
    EXPECT_NEAR(module->getScanPosition(), 0.2f, 1.0e-6f) << "the knob itself does not move";
}

TEST_F(WavetableOscillatorModuleTest, TheLivePositionIsClampedToTheTable) {
    setFloat(*module, "position", 0.8f);
    Rig rig{*module};
    rig.renderCV(WT::kJackPosition, 0.6f);
    EXPECT_FLOAT_EQ(module->getModulatedScanPosition(), 1.0f);
    rig.renderCV(WT::kJackPosition, -2.0f);
    EXPECT_FLOAT_EQ(module->getModulatedScanPosition(), 0.0f);
}

TEST_F(WavetableOscillatorModuleTest, AnLfoOnPositionSweepsTheDisplayedFrame) {
    MotionMode motion(synth::ui::AnimationMode::off); // the drawn position lands on each value at once
    setFloat(*module, "position", 0.5f);
    WavetableDisplayComponent display(*module);
    display.setBounds(0, 0, 260, 80);
    display.tick(0.0);

    Rig rig{*module};
    std::set<int> frames;
    double now = 0.0;
    for (int b = 0; b < 32; ++b) {
        // A 1 Hz-ish LFO, sampled per block: depth 0.5 around the knob.
        const float lfo = 0.5f * std::sin((float)b * 0.2f);
        rig.renderCV(WT::kJackPosition, lfo, 1);
        EXPECT_NEAR(module->getModulatedScanPosition(), juce::jlimit(0.0f, 1.0f, 0.5f + lfo), 1.0e-4f);
        display.tick(now += 33.0);
        EXPECT_NEAR(display.getShownPosition(), module->getModulatedScanPosition(), 1.0e-6f);
        frames.insert(display.getShownFrame());
    }
    EXPECT_GT(frames.size(), 3u) << "the drawn frame sweeps with the LFO";
    EXPECT_TRUE(display.getCaption().contains(juce::String(display.getShownFrame()) + "/"));
}

TEST_F(WavetableOscillatorModuleTest, AtRestWithNoCableTheDisplayShowsTheKnob) {
    setFloat(*module, "position", 0.3f);
    EXPECT_FLOAT_EQ(module->getModulatedScanPosition(), 0.3f) << "before any block";
    Rig rig{*module};
    rig.render();
    EXPECT_FLOAT_EQ(module->getModulatedScanPosition(), 0.3f);
    setFloat(*module, "position", 0.9f);
    EXPECT_FLOAT_EQ(module->getModulatedScanPosition(), 0.9f) << "a knob turn shows at once, not a block later";
}

TEST_F(WavetableOscillatorModuleTest, RemovingTheCableSettlesOnTheKnob) {
    setFloat(*module, "position", 0.25f);
    Rig rig{*module};
    rig.renderCV(WT::kJackPosition, 0.5f);
    ASSERT_NEAR(module->getModulatedScanPosition(), 0.75f, 1.0e-4f);
    rig.render(); // the cable is gone: its channel is silent
    EXPECT_FLOAT_EQ(module->getModulatedScanPosition(), 0.25f);
}

TEST_F(WavetableOscillatorModuleTest, ReleasingTheNoteSettlesOnTheKnobAndANewNoteFollowsAgain) {
    setFloat(*module, "position", 0.1f);
    Rig rig{*module};
    const auto cv = [](juce::AudioBuffer<float>& buf) {
        juce::FloatVectorOperations::fill(buf.getWritePointer(WT::kJackPosition), 0.4f, kBlockSize);
    };
    rig.render(cv, noteOn());
    rig.render(cv);
    EXPECT_NEAR(module->getModulatedScanPosition(), 0.5f, 1.0e-4f) << "a held note shows the cable";
    rig.render(cv, noteOff());
    EXPECT_FLOAT_EQ(module->getModulatedScanPosition(), 0.1f) << "at rest the knob, not the last sounding value";
    rig.render(cv, noteOn(64));
    EXPECT_NEAR(module->getModulatedScanPosition(), 0.5f, 1.0e-4f);
}

TEST_F(WavetableOscillatorModuleTest, AGateHeldHighFollowsAndAGateLowRests) {
    setFloat(*module, "position", 0.0f);
    Rig rig{*module};
    const auto withGate = [](float gate) {
        return [gate](juce::AudioBuffer<float>& buf) {
            juce::FloatVectorOperations::fill(buf.getWritePointer(WT::kJackPosition), 0.6f, kBlockSize);
            juce::FloatVectorOperations::fill(buf.getWritePointer(WT::kJackGate), gate, kBlockSize);
        };
    };
    rig.render(withGate(1.0f));
    EXPECT_NEAR(module->getModulatedScanPosition(), 0.6f, 1.0e-4f);
    rig.render(withGate(0.0f));
    EXPECT_FLOAT_EQ(module->getModulatedScanPosition(), 0.0f);
}

TEST_F(WavetableOscillatorModuleTest, PolyShowsTheSharedPositionWhileAnyVoiceIsHeld) {
    setBool(*module, "poly", true);
    setFloat(*module, "position", 0.2f);
    module->prepareToPlay(kSampleRate, kBlockSize); // the knob's ramp starts on 0.2
    Rig rig{*module, /*poly*/ true};
    const auto voices = [](int heldVoice, float gate) {
        return [heldVoice, gate](juce::AudioBuffer<float>& buf) {
            juce::FloatVectorOperations::fill(buf.getWritePointer(heldVoice), 330.0f, kBlockSize);
            juce::FloatVectorOperations::fill(buf.getWritePointer(WT::kGateBase + heldVoice), gate, kBlockSize);
            juce::FloatVectorOperations::fill(buf.getWritePointer(WT::modCVChannelFor(WT::kJackPosition, true)), 0.3f,
                                              kBlockSize);
        };
    };
    rig.render(voices(5, 1.0f));
    EXPECT_NEAR(module->getModulatedScanPosition(), 0.5f, 1.0e-4f) << "the most recent held voice, voice 6";
    rig.render(voices(5, 0.0f));
    EXPECT_FLOAT_EQ(module->getModulatedScanPosition(), 0.2f);
}

TEST_F(WavetableOscillatorModuleTest, AWarpCableShowsInTheDrawnTrace) {
    setChoice(*module, "warp", 1); // any warp mode other than None
    setFloat(*module, "warpAmount", 0.0f);
    Rig rig{*module};
    std::vector<float> rest, live;
    module->getDisplayWaveformAt(rest, 64, 0.5f, module->getModulatedWarpAmount());
    rig.renderCV(WT::kJackWarp, 0.8f);
    EXPECT_NEAR(module->getModulatedWarpAmount(), 0.8f, 1.0e-4f);
    module->getDisplayWaveformAt(live, 64, 0.5f, module->getModulatedWarpAmount());
    float delta = 0.0f;
    for (size_t i = 0; i < rest.size(); ++i)
        delta = std::max(delta, std::abs(rest[i] - live[i]));
    EXPECT_GT(delta, 0.05f) << "the drawn frame is warped by knob plus cable";
    EXPECT_NE(module->getWarpSignature(module->getModulatedWarpAmount()), module->getWarpSignature())
        << "so the display's repaint gate opens";
}

TEST_F(WavetableOscillatorModuleTest, UnisonDoesNotChangeTheLivePosition) {
    setInt(*module, "unison", 5);
    setFloat(*module, "position", 0.4f);
    Rig rig{*module};
    rig.renderCV(WT::kJackPosition, 0.2f);
    EXPECT_NEAR(module->getModulatedScanPosition(), 0.6f, 1.0e-4f);
}

TEST_F(WavetableOscillatorModuleTest, BypassRests) {
    setFloat(*module, "position", 0.3f);
    Rig rig{*module};
    rig.renderCV(WT::kJackPosition, 0.5f);
    module->setBypassed(true);
    rig.renderCV(WT::kJackPosition, 0.5f); // past the bypass fade
    EXPECT_FLOAT_EQ(module->getModulatedScanPosition(), 0.3f);
}

// ---------------------------------------------------------------------------
// The glide between the audio thread's values, per Animations setting
// ---------------------------------------------------------------------------

namespace {

// Renders a block with the cable at `cv` and ticks the display `ms` later.
struct GlideRig {
    WT& module;
    WavetableDisplayComponent display{module};
    double now = 0.0;

    GlideRig(WT& m)
        : module(m) {
        module.prepareToPlay(kSampleRate, kBlockSize); // the knob's ramp starts on the knob set before
        display.setBounds(0, 0, 260, 80);
        display.tick(now);
    }
    void step(float cv, double ms) {
        Rig{module}.renderCV(WT::kJackPosition, cv, 1);
        display.tick(now += ms);
    }
};

} // namespace

TEST_F(WavetableOscillatorModuleTest, FullAnimationsGlideTheDrawnPositionToTheLiveOne) {
    MotionMode motion(synth::ui::AnimationMode::full);
    setFloat(*module, "position", 0.0f);
    GlideRig rig(*module);
    rig.step(1.0f, 33.0);
    const float first = rig.display.getShownPosition();
    EXPECT_GT(first, 0.05f) << "starts moving on the first tick";
    EXPECT_LT(first, 0.9f) << "but glides rather than jumps";
    EXPECT_EQ(rig.display.getTimerInterval(), 1000 / WavetableDisplayComponent::kMovingHz) << "30 Hz while moving";
    for (int i = 0; i < 20; ++i)
        rig.step(1.0f, 33.0);
    EXPECT_FLOAT_EQ(rig.display.getShownPosition(), 1.0f) << "and lands exactly";
    EXPECT_EQ(rig.display.getTimerInterval(), 1000 / WavetableDisplayComponent::kRestHz) << "15 Hz once still";
}

TEST_F(WavetableOscillatorModuleTest, ReduceMotionJumpsTheDrawnPositionToTheLiveOne) {
    MotionMode motion(synth::ui::AnimationMode::reduced);
    setFloat(*module, "position", 0.0f);
    GlideRig rig(*module);
    rig.step(1.0f, 33.0);
    EXPECT_FLOAT_EQ(rig.display.getShownPosition(), 1.0f);
}

TEST_F(WavetableOscillatorModuleTest, AnimationsOffJumpsTheDrawnPositionToTheLiveOne) {
    MotionMode motion(synth::ui::AnimationMode::off);
    setFloat(*module, "position", 0.0f);
    GlideRig rig(*module);
    rig.step(0.6f, 33.0);
    EXPECT_NEAR(rig.display.getShownPosition(), 0.6f, 1.0e-4f);
}

TEST_F(WavetableOscillatorModuleTest, AStillLiveValueKeepsTheRestRate) {
    MotionMode motion(synth::ui::AnimationMode::off);
    setFloat(*module, "position", 0.4f);
    GlideRig rig(*module);
    rig.step(0.2f, 33.0);
    const auto caption = rig.display.getCaption();
    rig.step(0.2f, 66.0);
    EXPECT_EQ(rig.display.getCaption(), caption);
    EXPECT_EQ(rig.display.getTimerInterval(), 1000 / WavetableDisplayComponent::kRestHz);
}
