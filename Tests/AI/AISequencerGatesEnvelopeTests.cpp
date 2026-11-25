// Replays a real "16-step acid bassline on a poly sequencer" answer (Tests/fixtures/ai-plans/
// acid-bassline-sequencer-answer.json) through the production apply path, then renders it. The answer cabled the
// Poly Sequencer's MIDI to the Oscillator only; the track's ADSR heard notes only from Track In, the track had no
// clips, so the envelope never opened and the master peaked at -inf dBFS. Kind: ai-silent-patch.

#include "AI/AIProvider.h"
#include "AudioEngine/AudioEngine.h"
#include "MainComponent/MainComponent.h"
#include "Modules/ModuleBase.h"
#include "TestPaths.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "Transport/BounceExporter.h"
#include <cmath>
#include <gtest/gtest.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <set>
#include <vector>

namespace {

class SilentPatchProvider : public synth::AIProvider {
public:
    juce::String getProviderName() const override { return "none"; }
    void fetchAvailableModels(std::function<void(const juce::StringArray&, bool)> callback) override {
        callback({}, true);
    }
    RequestId sendPrompt(const std::vector<Message>&, CompletionCallback, const juce::var&,
                         std::function<void(const juce::String&)> = {}) override {
        return {};
    }
    void cancel(RequestId) override {}
    void setModel(const juce::String&) override {}
    juce::String getCurrentModel() const override { return {}; }
    void setRequestTimeoutMs(int) override {}
    int getRequestTimeoutMs() const override { return 0; }
};

// The empty project the model was shown: its Audio Output sits under a large id so the plan's small ids stay free.
constexpr const char* kEmptyProject = R"({"nodes": [{"id": 900000, "type": "Audio Output"}], "connections": []})";

double peakDb(const juce::File& wav) {
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(wav));
    if (reader == nullptr)
        return 1000.0;
    juce::AudioBuffer<float> buffer((int)reader->numChannels, (int)reader->lengthInSamples);
    reader->read(&buffer, 0, (int)reader->lengthInSamples, 0, true, true);
    const float peak = buffer.getMagnitude(0, buffer.getNumSamples());
    return peak > 0.0f ? 20.0 * std::log10((double)peak) : -std::numeric_limits<double>::infinity();
}

class AISequencerGatesEnvelopeTest : public ::testing::Test {
protected:
    void SetUp() override {
        scratch = juce::File::getSpecialLocation(juce::File::tempDirectory)
                      .getChildFile("agentsynth_fro1625_" + juce::Uuid().toString());
        scratch.createDirectory();
        mc = std::make_unique<MainComponent>(std::make_unique<SilentPatchProvider>());
        mc->setSize(1600, 900);
        mc->getAudioEngine().suspendDeviceCallback();
        const auto seed = scratch.getChildFile("seed.json");
        seed.replaceWithText(kEmptyProject);
        ASSERT_TRUE(mc->openPatchForTest(seed, /*append=*/false));
        const auto fixture =
            juce::File(TESTS_ROOT_DIR).getChildFile("fixtures/ai-plans/acid-bassline-sequencer-answer.json");
        const juce::var plan = juce::JSON::parse(fixture.loadFileAsString());
        ASSERT_TRUE(plan.isObject()) << fixture.getFullPathName();
        const auto applied = mc->getAiServiceForTest().applyProjectEdit(plan);
        ASSERT_TRUE(applied.ok) << applied.message;
    }
    void TearDown() override {
        mc.reset();
        scratch.deleteRecursively();
    }

    // The nodes of one processor name.
    std::vector<juce::AudioProcessorGraph::Node*> nodesNamed(const juce::String& name) {
        std::vector<juce::AudioProcessorGraph::Node*> found;
        for (auto* node : mc->getAudioEngine().getGraph().getNodes())
            if (node->getProcessor()->getName() == name)
                found.push_back(node);
        return found;
    }
    // True when MIDI cables lead from `from` to `to`, directly or through the merge nodes the app puts between them.
    bool midiReaches(juce::AudioProcessorGraph::NodeID from, juce::AudioProcessorGraph::NodeID to) {
        std::set<juce::uint32> seen{from.uid};
        std::vector<juce::AudioProcessorGraph::NodeID> todo{from};
        while (!todo.empty()) {
            const auto node = todo.back();
            todo.pop_back();
            for (const auto& c : mc->getAudioEngine().getGraph().getConnections()) {
                if (c.source.nodeID != node || !c.source.isMIDI() || !c.destination.isMIDI())
                    continue;
                if (c.destination.nodeID == to)
                    return true;
                if (seen.insert(c.destination.nodeID.uid).second)
                    todo.push_back(c.destination.nodeID);
            }
        }
        return false;
    }

    std::unique_ptr<MainComponent> mc;
    juce::File scratch;
};

} // namespace

TEST_F(AISequencerGatesEnvelopeTest, ThePolySequencerFeedsTheTracksEnvelopeAsWellAsItsInstrument) {
    const auto sequencers = nodesNamed("Poly Sequencer");
    const auto envelopes = nodesNamed("ADSR");
    const auto oscillators = nodesNamed("Oscillator");
    ASSERT_EQ(sequencers.size(), 1u);
    ASSERT_EQ(envelopes.size(), 1u);
    ASSERT_EQ(oscillators.size(), 1u);
    EXPECT_TRUE(midiReaches(sequencers[0]->nodeID, oscillators[0]->nodeID));
    EXPECT_TRUE(midiReaches(sequencers[0]->nodeID, envelopes[0]->nodeID))
        << "the envelope must hear the sequencer's notes, or it never opens (no clips feed Track In)";
}

TEST_F(AISequencerGatesEnvelopeTest, TheAnswerRendersAudibleAfterPlay) {
    mc->getAudioEngine().publishTimeline(mc->getTimelineDoc());
    synth::BounceOptions options;
    options.startBeat = 0.0;
    options.endBeat = 4.0;
    options.tailSeconds = 2.0;
    options.sampleRate = 44100.0;
    options.blockSize = 512;
    options.bitDepth = 32;
    options.numChannels = 2;
    options.format = synth::BounceFormat::Wav;
    const auto master = scratch.getChildFile("master.wav");
    const auto bounced = synth::BounceExporter::bounce(mc->getAudioEngine(), master, options);
    ASSERT_TRUE(bounced.ok) << bounced.message;
    const double peak = peakDb(master);
    std::printf("FRO1625 render master peak: %.2f dBFS\n", peak);
    EXPECT_GT(peak, -60.0) << "the render is silent (peak " << peak << " dBFS)";
}
