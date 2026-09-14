// Topic: factory-preset rendering through the real graph -- bypass semantics, stereo balance, silence checks, and the
// Poly Pad per-block note-injection/jitter determinism regression (FRO84).

#include "AudioRenderingTestHelpers.h"

// ===========================================================================
// Test 8: All factory presets render non-silent
// Uses the graph via AudioEngine + PresetManager (presets are graph-based).
// Injects MIDI via MidiKeyboardModule (same as the real app).
// ===========================================================================
// Distilled from a user patch ("I only hear the right side, out of a module I disabled"): an
// oscillator into a BYPASSED Ring Modulator's Modulator input, nothing on Carrier, module straight
// out to Audio Output. Bypass returned both raw channels untouched, so ch1 - the Modulator INPUT -
// became the right output leg: hard right, nothing left, from a module the user had switched off.
//
// Bypass on an FX is a dry pass-through BY DESIGN, and that part was working as intended; what was
// wrong is WHICH signal this module calls dry. Its dry path is the carrier (mix = 0 duplicates ch0
// onto both legs), so with no carrier patched a bypassed Ring Modulator must be silent.
TEST_F(AudioRenderingTest, BypassedRingModulatorDoesNotLeakItsModulatorInputToTheRight) {
    auto render = [](bool wireCarrier) {
        AudioEngine engine;
        auto& graph = engine.getGraph();
        graph.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);

        auto keys = graph.addNode(std::make_unique<MidiKeyboardModule>());
        auto osc = graph.addNode(std::make_unique<OscillatorModule>());
        auto ring = graph.addNode(std::make_unique<RingModulatorModule>());
        auto out = graph.addNode(std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor>(
            juce::AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode));

        graph.addConnection({{keys->nodeID, juce::AudioProcessorGraph::midiChannelIndex},
                             {osc->nodeID, juce::AudioProcessorGraph::midiChannelIndex}});
        graph.addConnection({{osc->nodeID, 0}, {ring->nodeID, wireCarrier ? 0 : 1}});
        graph.addConnection({{ring->nodeID, 0}, {out->nodeID, 0}});
        graph.addConnection({{ring->nodeID, 1}, {out->nodeID, 1}});

        dynamic_cast<ModuleBase*>(ring->getProcessor())->setBypassed(true);
        graph.prepareToPlay(kSampleRate, kBlockSize);
        dynamic_cast<MidiKeyboardModule*>(keys->getProcessor())->getKeyboardState().noteOn(1, 60, 1.0f);

        juce::AudioBuffer<float> result(2, kBlockSize * 8);
        result.clear();
        for (int b = 0; b < 8; ++b) {
            juce::AudioBuffer<float> block(2, kBlockSize);
            block.clear();
            juce::MidiBuffer midi;
            graph.processBlock(block, midi);
            for (int ch = 0; ch < 2; ++ch)
                result.copyFrom(ch, b * kBlockSize, block, ch, 0, kBlockSize);
        }
        return result;
    };

    const auto modulatorOnly = render(/*wireCarrier=*/false);
    EXPECT_LT(modulatorOnly.getRMSLevel(0, 0, modulatorOnly.getNumSamples()), 1.0e-6f);
    EXPECT_LT(modulatorOnly.getRMSLevel(1, 0, modulatorOnly.getNumSamples()), 1.0e-6f)
        << "a bypassed Ring Modulator must not emit its Modulator input as the right channel";

    const auto carrierWired = render(/*wireCarrier=*/true);
    const float left = carrierWired.getRMSLevel(0, 0, carrierWired.getNumSamples());
    const float right = carrierWired.getRMSLevel(1, 0, carrierWired.getNumSamples());
    EXPECT_GT(left, 1.0e-4f) << "with a carrier patched, bypass passes it dry";
    EXPECT_NEAR(right, left, 1.0e-6f) << "centred on both channels, exactly like mix = 0";
}

// Every factory preset must arrive in STEREO: both output channels carrying signal, at comparable
// level wherever the patch is symmetric. Presets are authored data, and their connection lists carry
// the right leg explicitly (Oscillator kRightBase -> Filter kRightBase -> VCA kRightBase -> the FX
// pair or Audio Output R) because the voice modules load SPLIT from their constructor defaults - so a
// single-leg cable list would leave every Audio R jack dangling and, where the old lists wired a
// silent pass-through channel as if it were a right leg, feed one side nothing at all.
//
// Parameterised over the preset list itself rather than a hand-kept copy, so a preset added later is
// covered without touching this file.
class PresetStereoTest : public ::testing::TestWithParam<int> {};

TEST_P(PresetStereoTest, RendersBothChannels) {
    const int index = GetParam();
    SCOPED_TRACE(synth::PresetManager::getPresetNames()[index].toStdString());

    AudioEngine engine; // never initialise(): that opens a real device
    auto& graph = engine.getGraph();
    graph.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
    ASSERT_TRUE(synth::PresetManager::loadPreset(index, graph));
    graph.prepareToPlay(kSampleRate, kBlockSize);

    for (auto* node : graph.getNodes())
        if (auto* kb = dynamic_cast<MidiKeyboardModule*>(node->getProcessor()))
            kb->getKeyboardState().noteOn(1, 60, 1.0f);

    const int totalSamples = static_cast<int>(kSampleRate);
    juce::AudioBuffer<float> result(2, totalSamples);
    result.clear();
    int rendered = 0;
    while (rendered < totalSamples) {
        const int n = std::min(kBlockSize, totalSamples - rendered);
        juce::AudioBuffer<float> block(2, n);
        block.clear();
        juce::MidiBuffer midi;
        graph.processBlock(block, midi);
        for (int ch = 0; ch < 2; ++ch)
            result.copyFrom(ch, rendered, block, ch, 0, n);
        rendered += n;
    }

    // Skip the first block: the note starts there and the FX tails have not filled yet.
    const int skip = std::min(kBlockSize, totalSamples - 1);
    const float left = result.getRMSLevel(0, skip, totalSamples - skip);
    const float right = result.getRMSLevel(1, skip, totalSamples - skip);

    ASSERT_GT(left, 1.0e-4f) << "left channel is silent (L=" << left << " R=" << right << ")";
    EXPECT_GT(right, 1.0e-4f) << "right channel is silent, so the preset is wired one-sided (L=" << left
                              << " R=" << right << ")";

    // Balance, with room for real stereo. No preset pans anything, but the two legs are now genuinely
    // separate signal paths, and a wide reverb is decorrelated by design: at width = 1 juce::Reverb
    // cross-mixes nothing, so each channel runs its own comb/allpass tuning and a big room (Ambient
    // Pad's roomSize 0.9, wet 0.5) legitimately differs by several dB over a one-second window. The
    // band below is wide enough for that and still catches the failure this test exists for - a leg
    // fed from a silent pass-through channel, which lands two orders of magnitude down, not six dB.
    const float ratio = right / left;
    EXPECT_GT(ratio, 0.35f) << "right channel is starved (L=" << left << " R=" << right << ")";
    EXPECT_LT(ratio, 2.9f) << "left channel is starved (L=" << left << " R=" << right << ")";
}

INSTANTIATE_TEST_SUITE_P(EveryFactoryPreset, PresetStereoTest,
                         ::testing::Range(0, 7)); // PresetManager::getPresetNames().size()

TEST_F(AudioRenderingTest, AllPresetsRenderNonSilent) {
    // Configure graph headlessly — do NOT call engine.initialise() as it opens the audio
    // device and routes processed audio to speakers.
    AudioEngine engine;
    auto& graph = engine.getGraph();
    graph.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
    graph.prepareToPlay(kSampleRate, kBlockSize);

    auto presetNames = synth::PresetManager::getPresetNames();

    for (int i = 0; i < presetNames.size(); ++i) {
        SCOPED_TRACE(presetNames[i].toStdString());

        synth::PresetManager::loadPreset(i, graph);
        graph.prepareToPlay(kSampleRate, kBlockSize);

        // Find the MidiKeyboardModule in the preset and inject a note
        for (auto* node : graph.getNodes()) {
            if (auto* kb = dynamic_cast<MidiKeyboardModule*>(node->getProcessor())) {
                kb->getKeyboardState().noteOn(1, 60, 1.0f);
                break;
            }
        }

        // Render 1 second
        int totalSamples = static_cast<int>(kSampleRate);
        juce::AudioBuffer<float> result(2, totalSamples);
        result.clear();
        int rendered = 0;
        while (rendered < totalSamples) {
            int n = std::min(kBlockSize, totalSamples - rendered);
            juce::AudioBuffer<float> block(2, n);
            block.clear();
            juce::MidiBuffer emptyMidi;
            graph.processBlock(block, emptyMidi);
            result.copyFrom(0, rendered, block, 0, 0, n);
            if (block.getNumChannels() > 1)
                result.copyFrom(1, rendered, block, 1, 0, n);
            rendered += n;
        }

        // Presets with sequencer should produce sound even without keyboard
        // Some presets might not have a keyboard module, but sequencer runs automatically
        // We just check that at least something comes out
        bool hasMidi = false;
        for (auto* node : graph.getNodes()) {
            if (dynamic_cast<MidiKeyboardModule*>(node->getProcessor()) ||
                dynamic_cast<SequencerModule*>(node->getProcessor())) {
                hasMidi = true;
                break;
            }
        }
        if (hasMidi) {
            EXPECT_FALSE(TestAudioHelpers::isSilent(result, 0))
                << "Preset '" << presetNames[i].toStdString() << "' rendered silent audio";
        }
    }
}

// ===========================================================================
// Test 8b: the "Poly Pad" preset renders identically twice (issue #198)
//
// Pushes an over-full chord (12 notes, 8 voices) through the real preset graph — a forward guard
// against future nondeterminism. Notes go in one per block: MidiKeyboardState::noteOn() stamps
// events with juce::Time::getMillisecondCounter(), and processNextMidiBuffer() spreads a block's
// queue by those stamps, so queuing all 12 up front let wall-clock noteOn() timing pick which
// voices got stolen on a slow/coverage runner. jitterMs sleeps between injections to prove that
// no longer matters.
// ===========================================================================
juce::AudioBuffer<float> renderPolyPadChord(int numNotes, int numBlocks, int jitterMs = 0) {
    AudioEngine engine; // do NOT initialise() — that would open a real audio device
    auto& graph = engine.getGraph();
    graph.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
    synth::PresetManager::loadPreset(6, graph);
    graph.prepareToPlay(kSampleRate, kBlockSize);
    MidiKeyboardModule* kb = nullptr;
    for (auto* node : graph.getNodes())
        if (auto* k = dynamic_cast<MidiKeyboardModule*>(node->getProcessor()))
            kb = k;
    juce::AudioBuffer<float> result(2, numBlocks * kBlockSize);
    result.clear();
    for (int b = 0; b < numBlocks; ++b) {
        if (kb != nullptr && b < numNotes) {
            if (jitterMs > 0)
                juce::Thread::sleep(jitterMs);
            kb->getKeyboardState().noteOn(1, 60 + b, 1.0f);
        }
        juce::AudioBuffer<float> block(2, kBlockSize);
        block.clear();
        juce::MidiBuffer emptyMidi;
        graph.processBlock(block, emptyMidi);
        for (int ch = 0; ch < 2; ++ch)
            result.copyFrom(ch, b * kBlockSize, block, ch, 0, kBlockSize);
    }
    for (auto* node : graph.getNodes())
        if (auto* poly = dynamic_cast<PolyMidiModule*>(node->getProcessor()))
            EXPECT_EQ(std::popcount(poly->getActiveVoiceMask()), 8) << "8 voices should be active";
    return result;
}

TEST_F(AudioRenderingTest, PolyPadPresetRendersIdenticallyTwice) {
    ASSERT_EQ(synth::PresetManager::getPresetNames()[6], "Poly Pad") << "preset index 6 moved";
    const auto first = renderPolyPadChord(12, 20);     // 12 notes into 8 voices — 4 steals
    const auto second = renderPolyPadChord(12, 20, 3); // jitter between injections must not matter
    ASSERT_EQ(first.getNumSamples(), second.getNumSamples());
    EXPECT_FALSE(TestAudioHelpers::isSilent(first, 0)) << "nothing was rendered, so nothing is proven";
    EXPECT_EQ(TestAudioHelpers::compareBuffers(first, second), 0.0f)
        << "the same chord must render bit-identically — voice stealing may not depend on wall-clock time";
}
