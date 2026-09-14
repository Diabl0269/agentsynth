// WavetableOscillatorModulePortsTests.cpp
// Factory/parameter surface, port & modulation-target mapping, and mip geometry.

#include "WavetableOscillatorModuleTestFixture.h"

TEST_F(WavetableOscillatorModuleTest, FactoryInitialisation) {
    using WT = WavetableOscillatorModule;
    EXPECT_EQ(module->getModuleType(), ModuleType::Wavetable);
    EXPECT_EQ(module->getName(), "Wavetable");
    // Bypassed, Table, Position, Octave, Coarse, Fine, Level, Poly, Unison, Detune, Warp,
    // Warp Amt, Phase, Rand Phase, Spread, Width, Blend, Stack, Sub, Sub Oct, Sub Wave, Pan,
    // Sync In, Import, Interp, Dual I/O (#219), Muted
    EXPECT_EQ(module->getParameters().size(), 27);
    EXPECT_EQ(module->getTotalNumInputChannels(), WT::kNumInputs);
    EXPECT_EQ(module->getTotalNumOutputChannels(), WT::kNumOutputs);
    EXPECT_EQ(module->getVisibleInputPortCount(), WT::kNumJacks);
    EXPECT_EQ(module->getVisibleOutputPortCount(), 2); // Audio L + Audio R
    EXPECT_EQ(module->getModulationCategory(), ModulationCategory::Oscillator);

    // The Audio R block has to clear the whole shared-CV block, or a stereo read would land
    // on a mod-CV input channel.
    EXPECT_GE(WT::kRightBase, WT::kNumInputs);
    EXPECT_GE(WT::kNumOutputs, WT::kRightBase + WT::kNumVoices);
}

TEST_F(WavetableOscillatorModuleTest, DeclaresEnoughOutputsForEveryCVInput) {
    // Guards the buffer-aliasing invariant: JUCE only makes a private copy of an input
    // channel when inputChan < getTotalNumOutputChannels().
    int highestCVChannel = 0;
    setBool(*module, "poly", true);
    for (const auto& target : module->getModulationTargets())
        highestCVChannel = std::max(highestCVChannel, target.channelIndex);
    EXPECT_LT(highestCVChannel, module->getTotalNumOutputChannels());
}

TEST_F(WavetableOscillatorModuleTest, PortLabelsAndModulationTargets) {
    using WT = WavetableOscillatorModule;

    EXPECT_EQ(module->getInputPortLabel(WT::kJackPitch), "Pitch");
    EXPECT_EQ(module->getInputPortLabel(WT::kJackPosition), "Position");
    EXPECT_EQ(module->getInputPortLabel(WT::kJackLevel), "Level");
    EXPECT_EQ(module->getInputPortLabel(WT::kJackWarp), "Warp");
    EXPECT_EQ(module->getInputPortLabel(WT::kJackSync), "Sync");
    EXPECT_EQ(module->getOutputPortLabel(0), "Audio L");
    EXPECT_EQ(module->getOutputPortLabel(1), "Audio R");

    // Mono lists every jack including Pitch; poly drives pitch from the fan instead.
    const auto mono = module->getModulationTargets();
    ASSERT_EQ(mono.size(), (size_t)WT::kNumJacks);
    EXPECT_EQ(mono[WT::kJackPosition].name, "Position");
    EXPECT_EQ(mono[WT::kJackPosition].channelIndex, WT::kJackPosition);
    EXPECT_EQ(mono[WT::kJackWarp].name, "Warp");
    EXPECT_EQ(mono[WT::kJackWarp].channelIndex, WT::kJackWarp);

    setBool(*module, "poly", true);
    const auto poly = module->getModulationTargets();
    ASSERT_EQ(poly.size(), (size_t)WT::kNumModCV);
    EXPECT_EQ(poly[0].name, "Position");
    EXPECT_EQ(poly[0].channelIndex, WT::kPolyModCVBase);
    EXPECT_EQ(poly.back().name, "Sync");
    EXPECT_EQ(poly.back().channelIndex, WT::kNumInputs - 1);
}

// Channels that existed before issue #180 must not have moved: a patch saved against the old
// six-jack module routes by raw channel index, so shifting Octave/Coarse/Fine/Level would
// silently repoint every saved modulation.
TEST_F(WavetableOscillatorModuleTest, LegacyModCVChannelsKeepTheirIndices) {
    using WT = WavetableOscillatorModule;

    EXPECT_EQ(WT::kJackPitch, 0);
    EXPECT_EQ(WT::kJackPosition, 1);
    EXPECT_EQ(WT::kJackOctave, 2);
    EXPECT_EQ(WT::kJackCoarse, 3);
    EXPECT_EQ(WT::kJackFine, 4);
    EXPECT_EQ(WT::kJackLevel, 5);

    // Poly's shared block still starts at channel 8 with the same five entries in order.
    EXPECT_EQ(WT::modCVChannelFor(WT::kJackPosition, true), 8);
    EXPECT_EQ(WT::modCVChannelFor(WT::kJackOctave, true), 9);
    EXPECT_EQ(WT::modCVChannelFor(WT::kJackCoarse, true), 10);
    EXPECT_EQ(WT::modCVChannelFor(WT::kJackFine, true), 11);
    EXPECT_EQ(WT::modCVChannelFor(WT::kJackLevel, true), 12);
}

TEST_F(WavetableOscillatorModuleTest, StereoOutputPortsMapToSeparateChannelBlocks) {
    using WT = WavetableOscillatorModule;

    // Mono: one channel per leg, neither of which is a poly head spanning voices.
    const auto monoL = module->mapOutputChannel(0);
    EXPECT_EQ(monoL.visibleJackIndex, 0);
    EXPECT_EQ(monoL.role, PortRole::Audio);
    EXPECT_EQ(monoL.polyVoiceSpan, 1);

    const auto monoR = module->mapOutputChannel(WT::kRightBase);
    EXPECT_EQ(monoR.visibleJackIndex, 1);
    EXPECT_EQ(monoR.role, PortRole::Audio);

    setBool(*module, "poly", true);

    // Poly: both legs fan eight wide, so the poly-bus collapse in AudioEngine can carry a
    // stereo unison stack into the Voice Mixer as two cables rather than sixteen.
    const auto polyL = module->mapOutputChannel(0);
    EXPECT_TRUE(polyL.isPolyGroupHead);
    EXPECT_EQ(polyL.polyVoiceSpan, WT::kNumVoices);

    const auto polyR = module->mapOutputChannel(WT::kRightBase);
    EXPECT_EQ(polyR.visibleJackIndex, 1);
    EXPECT_TRUE(polyR.isPolyGroupHead);
    EXPECT_EQ(polyR.polyVoiceSpan, WT::kNumVoices);

    for (int v = 1; v < WT::kNumVoices; ++v) {
        EXPECT_FALSE(module->mapOutputChannel(v).isPolyGroupHead);
        EXPECT_FALSE(module->mapOutputChannel(WT::kRightBase + v).isPolyGroupHead);
        EXPECT_EQ(module->mapOutputChannel(WT::kRightBase + v).visibleJackIndex, 1);
    }

    // A channel between the two audio blocks is a silent pass-through, never a bus head.
    EXPECT_FALSE(module->mapOutputChannel(WT::kPolyModCVBase).isPolyGroupHead);
}

TEST_F(WavetableOscillatorModuleTest, LogicalPortMappingMonoAndPoly) {
    // Mono: raw 0-5 map straight onto the six visible jacks.
    for (int raw = 0; raw <= 5; ++raw) {
        const auto port = module->mapInputChannel(raw);
        EXPECT_EQ(port.visibleJackIndex, raw);
        EXPECT_EQ(port.polyVoiceSpan, 1);
    }

    setBool(*module, "poly", true);
    const auto head = module->mapInputChannel(0);
    EXPECT_EQ(head.visibleJackIndex, 0);
    EXPECT_EQ(head.role, PortRole::Pitch);
    EXPECT_TRUE(head.isPolyGroupHead);
    EXPECT_EQ(head.polyVoiceSpan, 8);

    for (int raw = 1; raw <= 7; ++raw) {
        const auto voice = module->mapInputChannel(raw);
        EXPECT_EQ(voice.visibleJackIndex, 0);
        EXPECT_FALSE(voice.isPolyGroupHead);
        EXPECT_EQ(voice.polyVoiceSpan, 1);
    }

    for (int raw = 8; raw <= 12; ++raw) {
        const auto cv = module->mapInputChannel(raw);
        EXPECT_EQ(cv.visibleJackIndex, raw - 7);
        EXPECT_EQ(cv.role, PortRole::ModCV);
        EXPECT_TRUE(cv.isPolyGroupHead);
    }

    // Poly CV connections stay plain DirectCV routings (no auto attenuverter).
    EXPECT_FALSE(module->isAutoPromotableModTarget(8));
    setBool(*module, "poly", false);
    EXPECT_TRUE(module->isAutoPromotableModTarget(1));
}

TEST(WavetableMipGeometry, LimitsDecreaseMonotonicallyToTheFundamental) {
    using WT = WavetableOscillatorModule;
    EXPECT_EQ(WT::mipHarmonicLimit(0), 1023);
    EXPECT_EQ(WT::mipHarmonicLimit(WT::kNumMips - 1), 1);

    for (int m = 1; m < WT::kNumMips; ++m) {
        EXPECT_LT(WT::mipHarmonicLimit(m), WT::mipHarmonicLimit(m - 1)) << "mip " << m << " must be coarser";
        // Every mip must be able to store its own harmonics without aliasing.
        EXPECT_LE(WT::mipHarmonicLimit(m), WT::mipLength(m) / 2 - 1) << "mip " << m << " exceeds its own Nyquist";
        EXPECT_GE(WT::mipLength(m), 64);
    }
}
