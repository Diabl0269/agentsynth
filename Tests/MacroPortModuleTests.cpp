// MacroPortModuleTests.cpp
// Module-level sanity for the four Macro I/O port node types (P8-15, docs/macros.md §5.1, §7
// item 1) — MacroInlet/MacroOutlet (audio/CV) and MacroMidiInlet/MacroMidiOutlet (MIDI). Driven
// directly, no graph/engine, the same way RecordTapTests exercises RecordTapModule in isolation.
//
//   • pass-through   — audio/CV or MIDI reaches the output unchanged
//   • bypass         — the two-branch contract: visible channels stay dry either way (there is
//                       no processing to skip), hidden channels are always cleared
//   • extra state    — the audio/CV variants' visible-channel-count round-trips through
//                       getExtraState/setExtraState (the mechanism §7 item 3 will drive)
//   • registration   — internal-only exclusion from the AI factory checklist lives in
//                       Tests/AIStateMapperTests.cpp (AuthorableModuleTypesGolden,
//                       UntrustedPatchRejectsInternalOnlyModuleTypes); not duplicated here.

#include "../Source/Modules/MacroInletModule.h"
#include "../Source/Modules/MacroMidiInletModule.h"
#include "../Source/Modules/MacroMidiOutletModule.h"
#include "../Source/Modules/MacroOutletModule.h"
#include <gtest/gtest.h>

namespace {
constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 64;
} // namespace

// ---------------------------------------------------------------------------------------------
// MacroInletModule / MacroOutletModule — audio/CV pass-through
// ---------------------------------------------------------------------------------------------

TEST(MacroInletModuleTest, ConstructsWithMonoVisibleByDefault) {
    MacroInletModule inlet;
    EXPECT_EQ(inlet.getVisibleInputPortCount(), 1);
    EXPECT_EQ(inlet.getVisibleOutputPortCount(), 1);
    EXPECT_EQ(inlet.getModuleType(), ModuleType::MacroInlet);
    EXPECT_FALSE(inlet.acceptsMidi());
    EXPECT_FALSE(inlet.producesMidi());
}

TEST(MacroInletModuleTest, PassesAudioThroughUnchangedWhenNotBypassed) {
    MacroInletModule inlet;
    inlet.prepareToPlay(kSampleRate, kBlockSize);

    juce::AudioBuffer<float> buffer(MacroInletModule::kMaxChannels, kBlockSize);
    buffer.clear();
    for (int i = 0; i < kBlockSize; ++i)
        buffer.getWritePointer(0)[i] = 0.5f * (float)i / (float)kBlockSize;

    juce::MidiBuffer midi;
    inlet.processBlock(buffer, midi);

    for (int i = 0; i < kBlockSize; ++i)
        EXPECT_FLOAT_EQ(buffer.getReadPointer(0)[i], 0.5f * (float)i / (float)kBlockSize);
}

TEST(MacroInletModuleTest, BypassLeavesTheVisibleChannelDryButClearsHiddenChannels) {
    MacroInletModule inlet;
    inlet.prepareToPlay(kSampleRate, kBlockSize);
    inlet.setBypassed(true);

    juce::AudioBuffer<float> buffer(MacroInletModule::kMaxChannels, kBlockSize);
    buffer.clear();
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        for (int i = 0; i < kBlockSize; ++i)
            buffer.getWritePointer(ch)[i] = 0.25f;

    juce::MidiBuffer midi;
    inlet.processBlock(buffer, midi);

    // Visible channel (0, since default visible count is 1): dry pass-through — the standard
    // two-branch bypass contract leaves it untouched.
    for (int i = 0; i < kBlockSize; ++i)
        EXPECT_FLOAT_EQ(buffer.getReadPointer(0)[i], 0.25f);

    // Hidden channels (>= visible count): cleared regardless of bypass — nothing should be wired
    // there, but the bus is always kMaxChannels wide.
    for (int ch = 1; ch < buffer.getNumChannels(); ++ch)
        for (int i = 0; i < kBlockSize; ++i)
            EXPECT_FLOAT_EQ(buffer.getReadPointer(ch)[i], 0.0f) << "channel " << ch;
}

TEST(MacroInletModuleTest, HiddenChannelsAreClearedWhenNotBypassedToo) {
    MacroInletModule inlet;
    inlet.prepareToPlay(kSampleRate, kBlockSize);

    juce::AudioBuffer<float> buffer(MacroInletModule::kMaxChannels, kBlockSize);
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        for (int i = 0; i < kBlockSize; ++i)
            buffer.getWritePointer(ch)[i] = 1.0f;

    juce::MidiBuffer midi;
    inlet.processBlock(buffer, midi);

    for (int ch = 1; ch < buffer.getNumChannels(); ++ch)
        for (int i = 0; i < kBlockSize; ++i)
            EXPECT_FLOAT_EQ(buffer.getReadPointer(ch)[i], 0.0f) << "channel " << ch;
}

TEST(MacroInletModuleTest, ExtraStateRoundTripsTheChosenShapeAndVoiceCount) {
    MacroInletModule inlet;
    ASSERT_EQ(inlet.getVisibleOutputPortCount(), 1); // default: Mono

    auto* obj = new juce::DynamicObject();
    obj->setProperty("shape", "poly");
    obj->setProperty("voices", 4);
    inlet.setExtraState(juce::var(obj));

    EXPECT_EQ(inlet.getPortShape(), MacroPortShape::Poly);
    EXPECT_EQ(inlet.getVoiceCount(), 4);
    EXPECT_EQ(inlet.getVisibleInputPortCount(), 1); // Poly is still ONE visible jack (a fanned bus)
    EXPECT_EQ(inlet.getVisibleOutputPortCount(), 1);

    const juce::var saved = inlet.getExtraState();
    MacroInletModule reloaded;
    reloaded.setExtraState(saved);
    EXPECT_EQ(reloaded.getPortShape(), MacroPortShape::Poly);
    EXPECT_EQ(reloaded.getVoiceCount(), 4);
}

TEST(MacroInletModuleTest, ExtraStateClampsVoiceCountToTheDeclaredMaximum) {
    MacroInletModule inlet;
    auto* obj = new juce::DynamicObject();
    obj->setProperty("shape", "poly");
    obj->setProperty("voices", 999);
    inlet.setExtraState(juce::var(obj));
    EXPECT_EQ(inlet.getVoiceCount(), MacroInletModule::kMaxChannels);
}

TEST(MacroInletModuleTest, AbsentShapeKeyParsesAsMono) {
    // Every P8-15a save (and every hand-built one predating shape/voices) has no "shape" key.
    MacroInletModule inlet;
    auto* obj = new juce::DynamicObject();
    obj->setProperty("voices", 4); // present but irrelevant without a shape saying Poly
    inlet.setExtraState(juce::var(obj));
    EXPECT_EQ(inlet.getPortShape(), MacroPortShape::Mono);
    EXPECT_EQ(inlet.getVisibleOutputPortCount(), 1);
}

TEST(MacroInletModuleTest, SetPortShapeMonoExposesOnlyRawChannelZero) {
    MacroInletModule inlet;
    inlet.setPortShape(MacroPortShape::Mono);
    EXPECT_EQ(inlet.getVisibleInputPortCount(), 1);
    EXPECT_EQ(inlet.mapOutputChannel(0).role, PortRole::Audio);
    EXPECT_EQ(inlet.mapOutputChannel(0).visibleJackIndex, 0);
    EXPECT_TRUE(inlet.mapOutputChannel(0).isPolyGroupHead);
    for (int ch = 1; ch < MacroInletModule::kMaxChannels; ++ch)
        EXPECT_NE(inlet.mapOutputChannel(ch).role, PortRole::Audio) << "channel " << ch;
}

TEST(MacroInletModuleTest, SetPortShapeStereoExposesChannelZeroAndKRightBaseAsTwoJacks) {
    MacroInletModule inlet;
    inlet.setPortShape(MacroPortShape::Stereo);
    EXPECT_EQ(inlet.getVisibleInputPortCount(), 2);
    EXPECT_EQ(inlet.getVisibleOutputPortCount(), 2);
    EXPECT_EQ(inlet.rightAudioLegChannel(), MacroInletModule::kRightBase);

    const auto left = inlet.mapOutputChannel(0);
    EXPECT_EQ(left.role, PortRole::Audio);
    EXPECT_EQ(left.visibleJackIndex, 0);
    EXPECT_TRUE(left.isPolyGroupHead);

    const auto right = inlet.mapOutputChannel(MacroInletModule::kRightBase);
    EXPECT_EQ(right.role, PortRole::Audio);
    EXPECT_EQ(right.visibleJackIndex, 1);
    EXPECT_TRUE(right.isPolyGroupHead);

    // ch1 must stay untouched — the whole point of the split-block convention.
    EXPECT_NE(inlet.mapOutputChannel(1).role, PortRole::Audio);
}

TEST(MacroInletModuleTest, SetPortShapePolyFansAllVoicesOntoOneJack) {
    MacroInletModule inlet;
    inlet.setPortShape(MacroPortShape::Poly, 4);
    EXPECT_EQ(inlet.getVisibleInputPortCount(), 1);
    EXPECT_EQ(inlet.rightAudioLegChannel(), -1); // Poly is not Stereo — no split-block leg

    for (int ch = 0; ch < 4; ++ch) {
        const auto p = inlet.mapOutputChannel(ch);
        EXPECT_EQ(p.role, PortRole::Audio) << "channel " << ch;
        EXPECT_EQ(p.visibleJackIndex, 0) << "channel " << ch;
        EXPECT_EQ(p.isPolyGroupHead, ch == 0) << "channel " << ch;
        EXPECT_EQ(p.polyVoiceSpan, 4) << "channel " << ch;
    }
    for (int ch = 4; ch < MacroInletModule::kMaxChannels; ++ch)
        EXPECT_NE(inlet.mapOutputChannel(ch).role, PortRole::Audio) << "channel " << ch;
}

TEST(MacroInletModuleTest, StereoShapeOnlyClearsInactiveRawChannels) {
    MacroInletModule inlet;
    inlet.setPortShape(MacroPortShape::Stereo);
    inlet.prepareToPlay(kSampleRate, kBlockSize);

    juce::AudioBuffer<float> buffer(MacroInletModule::kMaxChannels, kBlockSize);
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        for (int i = 0; i < kBlockSize; ++i)
            buffer.getWritePointer(ch)[i] = 0.6f;

    juce::MidiBuffer midi;
    inlet.processBlock(buffer, midi);

    EXPECT_FLOAT_EQ(buffer.getReadPointer(0)[0], 0.6f);
    EXPECT_FLOAT_EQ(buffer.getReadPointer(MacroInletModule::kRightBase)[0], 0.6f);
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch) {
        if (ch == 0 || ch == MacroInletModule::kRightBase)
            continue;
        EXPECT_FLOAT_EQ(buffer.getReadPointer(ch)[0], 0.0f) << "channel " << ch;
    }
}

TEST(MacroOutletModuleTest, ConstructsAndPassesThroughLikeInlet) {
    MacroOutletModule outlet;
    EXPECT_EQ(outlet.getModuleType(), ModuleType::MacroOutlet);
    EXPECT_EQ(outlet.getVisibleOutputPortCount(), 1);

    outlet.prepareToPlay(kSampleRate, kBlockSize);
    juce::AudioBuffer<float> buffer(MacroOutletModule::kMaxChannels, kBlockSize);
    buffer.clear();
    buffer.getWritePointer(0)[0] = 0.75f;

    juce::MidiBuffer midi;
    outlet.processBlock(buffer, midi);
    EXPECT_FLOAT_EQ(buffer.getReadPointer(0)[0], 0.75f);
}

// ---------------------------------------------------------------------------------------------
// MacroMidiInletModule / MacroMidiOutletModule — MIDI pass-through, zero audio channels
// ---------------------------------------------------------------------------------------------

TEST(MacroMidiInletModuleTest, ConstructsWithZeroAudioChannelsAndAcceptsAndProducesMidi) {
    MacroMidiInletModule inlet;
    EXPECT_EQ(inlet.getModuleType(), ModuleType::MacroMidiInlet);
    EXPECT_TRUE(inlet.acceptsMidi());
    EXPECT_TRUE(inlet.producesMidi());
    EXPECT_EQ(inlet.getTotalNumInputChannels(), 0);
    EXPECT_EQ(inlet.getTotalNumOutputChannels(), 0);
}

TEST(MacroMidiInletModuleTest, PassesMidiThroughUnchangedWhenNotBypassed) {
    MacroMidiInletModule inlet;
    inlet.prepareToPlay(kSampleRate, kBlockSize);

    juce::AudioBuffer<float> buffer(0, kBlockSize);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 5);

    inlet.processBlock(buffer, midi);

    int count = 0;
    for (const auto metadata : midi) {
        const auto msg = metadata.getMessage();
        EXPECT_TRUE(msg.isNoteOn());
        EXPECT_EQ(msg.getNoteNumber(), 60);
        EXPECT_EQ(metadata.samplePosition, 5);
        ++count;
    }
    EXPECT_EQ(count, 1);
}

TEST(MacroMidiInletModuleTest, MidiStillPassesThroughWhileBypassed) {
    // No dry-vs-processed distinction to make (there is no processing) — bypass only gates the
    // activity LED, matching Rec Tap's "pass-through is literally nothing" for audio.
    MacroMidiInletModule inlet;
    inlet.prepareToPlay(kSampleRate, kBlockSize);
    inlet.setBypassed(true);

    juce::AudioBuffer<float> buffer(0, kBlockSize);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 72, (juce::uint8)90), 0);

    inlet.processBlock(buffer, midi);

    EXPECT_FALSE(midi.isEmpty());
}

TEST(MacroMidiOutletModuleTest, ConstructsAndPassesThroughLikeInlet) {
    MacroMidiOutletModule outlet;
    EXPECT_EQ(outlet.getModuleType(), ModuleType::MacroMidiOutlet);
    EXPECT_TRUE(outlet.acceptsMidi());
    EXPECT_TRUE(outlet.producesMidi());

    outlet.prepareToPlay(kSampleRate, kBlockSize);
    juce::AudioBuffer<float> buffer(0, kBlockSize);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOff(2, 40), 3);

    outlet.processBlock(buffer, midi);
    EXPECT_FALSE(midi.isEmpty());
}
