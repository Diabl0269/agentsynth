// WavetableOscillatorModuleStateTests.cpp
// Binary state round-trip and the trusted/untrusted graph-JSON path presets and undo use.

#include "AI/AIStateMapper/AIStateMapper.h"
#include "WavetableOscillatorModuleTestFixture.h"

TEST_F(WavetableOscillatorModuleTest, StateRoundTripRestoresParametersAndWavetable) {
    const juce::File file = writeWavetableFile("agentsynth_wt_state.wav", 6);
    ASSERT_TRUE(file.existsAsFile());
    ASSERT_TRUE(module->loadWavetableFile(file));

    setChoice(*module, "table", WavetableOscillatorModule::kLoadedTableChoice);
    setFloat(*module, "position", 0.75f);
    setInt(*module, "unison", 4);

    juce::MemoryBlock state;
    module->getStateInformation(state);

    auto restored = std::make_unique<WavetableOscillatorModule>();
    restored->setStateInformation(state.getData(), (int)state.getSize());
    restored->prepareToPlay(kSampleRate, kBlockSize);

    EXPECT_TRUE(restored->hasLoadedWavetable());
    EXPECT_EQ(restored->getWavetableFile(), file);
    EXPECT_EQ(restored->getNumFrames(), 6);
    EXPECT_NEAR(restored->getScanPosition(), 0.75f, 1.0e-4f);

    const auto out = render(*restored, 13, 4);
    EXPECT_GT(rms(out, 0), 0.01f);

    file.deleteFile();
}

TEST_F(WavetableOscillatorModuleTest, StateRoundTripSurvivesAMissingWavetableFile) {
    const juce::File file = writeWavetableFile("agentsynth_wt_gone.wav", 4);
    ASSERT_TRUE(file.existsAsFile());
    ASSERT_TRUE(module->loadWavetableFile(file));
    setChoice(*module, "table", WavetableOscillatorModule::kLoadedTableChoice);

    juce::MemoryBlock state;
    module->getStateInformation(state);
    file.deleteFile();

    auto restored = std::make_unique<WavetableOscillatorModule>();
    EXPECT_NO_THROW(restored->setStateInformation(state.getData(), (int)state.getSize()));
    restored->prepareToPlay(kSampleRate, kBlockSize);
    EXPECT_FALSE(restored->hasLoadedWavetable());

    // Falls back to a built-in rather than going silent.
    const auto out = render(*restored, 13, 4);
    EXPECT_GT(rms(out, 0), 0.05f);
}

TEST_F(WavetableOscillatorModuleTest, TrustedGraphJSONRestoresTheWavetable) {
    // getStateInformation is NOT what presets or undo use — AIStateMapper::graphToJSON
    // persists parameters plus getExtraState(). A wavetable path that only lived in the
    // binary ModuleState blob would be silently dropped on every preset load.
    const juce::File file = writeWavetableFile("agentsynth_wt_graphstate.wav", 4);
    ASSERT_TRUE(file.existsAsFile());

    juce::AudioProcessorGraph source;
    auto node = source.addNode(std::make_unique<WavetableOscillatorModule>());
    ASSERT_NE(node, nullptr);
    auto* sourceModule = dynamic_cast<WavetableOscillatorModule*>(node->getProcessor());
    ASSERT_NE(sourceModule, nullptr);
    ASSERT_TRUE(sourceModule->loadWavetableFile(file));
    setChoice(*sourceModule, "table", WavetableOscillatorModule::kLoadedTableChoice);

    const juce::var json = synth::AIStateMapper::graphToJSON(source);

    juce::AudioProcessorGraph restored;
    ASSERT_TRUE(synth::AIStateMapper::applyJSONToGraph(json, restored, /*clearExisting=*/true, /*trusted=*/true));

    WavetableOscillatorModule* restoredModule = nullptr;
    for (auto* n : restored.getNodes())
        if (auto* w = dynamic_cast<WavetableOscillatorModule*>(n->getProcessor()))
            restoredModule = w;

    ASSERT_NE(restoredModule, nullptr);
    EXPECT_EQ(restoredModule->getWavetableFile(), file) << "undo/redo and preset load must not drop the wavetable";
    EXPECT_EQ(restoredModule->getNumFrames(), 4);

    file.deleteFile();
}

TEST_F(WavetableOscillatorModuleTest, UntrustedPatchCannotNameAWavetableFileToOpen) {
    // Same guard as the Sampler: a model-authored patch must never make the app read a
    // file it chose, so setExtraState is only reachable on the trusted path.
    const juce::File file = writeWavetableFile("agentsynth_wt_untrusted.wav", 4);
    ASSERT_TRUE(file.existsAsFile());

    juce::var json = juce::JSON::parse(R"({
      "nodes": [{"id": 1, "type": "Wavetable", "state": {"wavetableFile": "PLACEHOLDER"}}],
      "connections": []
    })");
    ASSERT_TRUE(json.isObject());
    json.getDynamicObject()
        ->getProperty("nodes")
        .getArray()
        ->getReference(0)
        .getDynamicObject()
        ->getProperty("state")
        .getDynamicObject()
        ->setProperty("wavetableFile", file.getFullPathName());

    juce::AudioProcessorGraph graph;
    ASSERT_TRUE(synth::AIStateMapper::applyJSONToGraph(json, graph, /*clearExisting=*/true, /*trusted=*/false));

    WavetableOscillatorModule* module = nullptr;
    for (auto* n : graph.getNodes())
        if (auto* w = dynamic_cast<WavetableOscillatorModule*>(n->getProcessor()))
            module = w;

    ASSERT_NE(module, nullptr) << "the node itself is still legal to author";
    EXPECT_FALSE(module->hasLoadedWavetable()) << "untrusted JSON must never make the app read a file it named";

    file.deleteFile();
}

TEST_F(WavetableOscillatorModuleTest, StateRoundTripRestoresNewParametersAndFolder) {
    const juce::File folder = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("wt180-state");
    folder.deleteRecursively();
    ASSERT_TRUE(folder.createDirectory());

    setChoice(*module, "warp", (int)WavetableOscillatorModule::Warp::Mirror);
    setFloat(*module, "warpAmount", 0.75f);
    setFloat(*module, "phase", 180.0f);
    setFloat(*module, "spread", 0.5f);
    setFloat(*module, "width", 0.8f);
    setFloat(*module, "pan", -0.4f);
    setFloat(*module, "subLevel", 0.6f);
    setChoice(*module, "stack", (int)WavetableOscillatorModule::Stack::Minor);
    setChoice(*module, "importMode", (int)WavetableOscillatorModule::ImportMode::Fixed512);
    setChoice(*module, "interpolation", (int)WavetableOscillatorModule::Interpolation::Hermite);
    module->setWavetableFolder(folder);

    juce::MemoryBlock state;
    module->getStateInformation(state);

    auto restored = std::make_unique<WavetableOscillatorModule>();
    restored->setStateInformation(state.getData(), (int)state.getSize());

    const auto readFloat = [](juce::AudioProcessor& m, const juce::String& id) {
        for (auto* p : m.getParameters())
            if (auto* f = dynamic_cast<juce::AudioParameterFloat*>(p))
                if (f->paramID == id)
                    return f->get();
        return -999.0f;
    };
    const auto readChoice = [](juce::AudioProcessor& m, const juce::String& id) {
        for (auto* p : m.getParameters())
            if (auto* c = dynamic_cast<juce::AudioParameterChoice*>(p))
                if (c->paramID == id)
                    return c->getIndex();
        return -1;
    };

    EXPECT_EQ(readChoice(*restored, "warp"), (int)WavetableOscillatorModule::Warp::Mirror);
    EXPECT_NEAR(readFloat(*restored, "warpAmount"), 0.75f, 0.01f);
    EXPECT_NEAR(readFloat(*restored, "phase"), 180.0f, 0.5f);
    EXPECT_NEAR(readFloat(*restored, "spread"), 0.5f, 0.01f);
    EXPECT_NEAR(readFloat(*restored, "width"), 0.8f, 0.01f);
    EXPECT_NEAR(readFloat(*restored, "pan"), -0.4f, 0.01f);
    EXPECT_NEAR(readFloat(*restored, "subLevel"), 0.6f, 0.01f);
    EXPECT_EQ(readChoice(*restored, "stack"), (int)WavetableOscillatorModule::Stack::Minor);
    EXPECT_EQ(readChoice(*restored, "importMode"), (int)WavetableOscillatorModule::ImportMode::Fixed512);
    EXPECT_EQ(readChoice(*restored, "interpolation"), (int)WavetableOscillatorModule::Interpolation::Hermite);
    EXPECT_EQ(restored->getWavetableFolder(), folder);

    // The same settings must survive the AIStateMapper extra-state path presets use.
    auto viaExtra = std::make_unique<WavetableOscillatorModule>();
    viaExtra->setExtraState(module->getExtraState());
    EXPECT_EQ(viaExtra->getWavetableFolder(), folder);

    folder.deleteRecursively();
}
