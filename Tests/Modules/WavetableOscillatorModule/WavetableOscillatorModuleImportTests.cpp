// WavetableOscillatorModuleImportTests.cpp
// Wavetable file loading, import-mode framing/interpolation, and the folder browser.

#include "WavetableOscillatorModuleTestFixture.h"

TEST_F(WavetableOscillatorModuleTest, LoadWavetableFileSplitsFrames) {
    const juce::File file = writeWavetableFile("agentsynth_wt_test_4frames.wav", 4);
    ASSERT_TRUE(file.existsAsFile());

    EXPECT_TRUE(module->loadWavetableFile(file));
    EXPECT_TRUE(module->hasLoadedWavetable());
    EXPECT_EQ(module->getWavetableFile(), file);

    setChoice(*module, "table", WavetableOscillatorModule::kLoadedTableChoice);
    EXPECT_EQ(module->getNumFrames(), 4);
    EXPECT_EQ(module->getWavetableName(), file.getFileNameWithoutExtension());

    const auto out = render(*module, 13, 4);
    EXPECT_GT(rms(out, 0), 0.05f) << "the loaded table should sound";

    file.deleteFile();
}

TEST_F(WavetableOscillatorModuleTest, LoadedFramesKeepTheirDistinctHarmonics) {
    // Frame 0 is the fundamental, frame 3 is the 4th harmonic.
    const juce::File file = writeWavetableFile("agentsynth_wt_test_harmonics.wav", 4);
    ASSERT_TRUE(file.existsAsFile());
    ASSERT_TRUE(module->loadWavetableFile(file));
    setChoice(*module, "table", WavetableOscillatorModule::kLoadedTableChoice);

    setFloat(*module, "position", 0.0f);
    module->prepareToPlay(kSampleRate, kBlockSize);
    const auto first = render(*module, 13, 8);
    EXPECT_GT(magnitudeAt(first, 0, 440.0f, kSampleRate), 0.5f);

    auto osc = std::make_unique<WavetableOscillatorModule>();
    ASSERT_TRUE(osc->loadWavetableFile(file));
    setChoice(*osc, "table", WavetableOscillatorModule::kLoadedTableChoice);
    setFloat(*osc, "position", 1.0f);
    osc->prepareToPlay(kSampleRate, kBlockSize);
    const auto last = render(*osc, 13, 8);
    EXPECT_GT(magnitudeAt(last, 0, 1760.0f, kSampleRate), 0.5f) << "last frame is the 4th harmonic";
    EXPECT_LT(magnitudeAt(last, 0, 440.0f, kSampleRate), 0.1f);

    file.deleteFile();
}

TEST_F(WavetableOscillatorModuleTest, LoadWavetableFileRejectsMissingAndInvalidFiles) {
    const juce::File missing =
        juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("agentsynth_wt_missing.wav");
    missing.deleteFile();
    EXPECT_FALSE(module->loadWavetableFile(missing));
    EXPECT_FALSE(module->hasLoadedWavetable());

    const juce::File garbage =
        juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("agentsynth_wt_garbage.wav");
    garbage.deleteFile();
    garbage.replaceWithText("this is not audio data");
    EXPECT_FALSE(module->loadWavetableFile(garbage));
    EXPECT_FALSE(module->hasLoadedWavetable());

    // The module still plays its built-in table.
    const auto out = render(*module, 13, 4);
    EXPECT_GT(rms(out, 0), 0.05f);

    garbage.deleteFile();
}

TEST_F(WavetableOscillatorModuleTest, LoadWavetableFileCapsFrameCount) {
    // A file with more frames than kMaxFrames is decimated, not truncated.
    const juce::File file =
        writeWavetableFile("agentsynth_wt_test_many.wav", WavetableOscillatorModule::kMaxFrames + 8);
    ASSERT_TRUE(file.existsAsFile());
    ASSERT_TRUE(module->loadWavetableFile(file));
    setChoice(*module, "table", WavetableOscillatorModule::kLoadedTableChoice);
    EXPECT_EQ(module->getNumFrames(), WavetableOscillatorModule::kMaxFrames);
    file.deleteFile();
}

TEST_F(WavetableOscillatorModuleTest, ReloadingWhileRenderingStaysStable) {
    const juce::File a = writeWavetableFile("agentsynth_wt_reload_a.wav", 4);
    const juce::File b = writeWavetableFile("agentsynth_wt_reload_b.wav", 8);
    ASSERT_TRUE(a.existsAsFile());
    ASSERT_TRUE(b.existsAsFile());

    setChoice(*module, "table", WavetableOscillatorModule::kLoadedTableChoice);
    juce::AudioBuffer<float> block(13, kBlockSize);
    for (int i = 0; i < 6; ++i) {
        ASSERT_TRUE(module->loadWavetableFile((i % 2 == 0) ? a : b));
        block.clear();
        juce::MidiBuffer midi;
        module->processBlock(block, midi);
        EXPECT_GT(rms(block, 0), 0.01f) << "iteration " << i;
    }
    EXPECT_EQ(module->getNumFrames(), 8);

    a.deleteFile();
    b.deleteFile();
}

TEST_F(WavetableOscillatorModuleTest, FixedImportSizeSplitsOnThatBoundary) {
    // Eight 2048-sample frames = 64 frames of 256 samples.
    const juce::File file = writeWavetableFile("wt180-fixed.wav", 8);
    ASSERT_TRUE(file.existsAsFile());

    setChoice(*module, "importMode", (int)WavetableOscillatorModule::ImportMode::Fixed256);
    ASSERT_TRUE(module->loadWavetableFile(file));
    setChoice(*module, "table", WavetableOscillatorModule::kLoadedTableChoice);
    EXPECT_EQ(module->getNumFrames(), 64);

    setChoice(*module, "importMode", (int)WavetableOscillatorModule::ImportMode::Fixed2048);
    ASSERT_TRUE(module->loadWavetableFile(file));
    EXPECT_EQ(module->getNumFrames(), 8);

    file.deleteFile();
}

TEST_F(WavetableOscillatorModuleTest, SingleCycleImportMakesExactlyOneFrame) {
    const juce::File file = writeWavetableFile("wt180-single.wav", 8);
    ASSERT_TRUE(file.existsAsFile());

    setChoice(*module, "importMode", (int)WavetableOscillatorModule::ImportMode::SingleCycle);
    ASSERT_TRUE(module->loadWavetableFile(file));
    setChoice(*module, "table", WavetableOscillatorModule::kLoadedTableChoice);
    EXPECT_EQ(module->getNumFrames(), 1);

    file.deleteFile();
}

TEST_F(WavetableOscillatorModuleTest, PitchDetectImportFindsTheSourcePeriod) {
    // A file whose period is 512 samples, written as 2048-sample blocks of the 4th harmonic.
    const int frameSize = WavetableOscillatorModule::kFrameSize;
    const int numBlocks = 8;
    juce::AudioBuffer<float> buffer(1, numBlocks * frameSize);
    float* data = buffer.getWritePointer(0);
    for (int i = 0; i < buffer.getNumSamples(); ++i)
        data[i] = 0.8f * std::sin((float)i / 512.0f * juce::MathConstants<float>::twoPi);

    const juce::File file = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("wt180-pitch.wav");
    file.deleteFile();
    juce::WavAudioFormat format;
    std::unique_ptr<juce::FileOutputStream> stream(file.createOutputStream());
    ASSERT_NE(stream, nullptr);
    std::unique_ptr<juce::AudioFormatWriter> writer(format.createWriterFor(stream.get(), kSampleRate, 1, 16, {}, 0));
    ASSERT_NE(writer, nullptr);
    stream.release();
    writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples());
    writer.reset();

    setChoice(*module, "importMode", (int)WavetableOscillatorModule::ImportMode::PitchDetect);
    ASSERT_TRUE(module->loadWavetableFile(file));
    setChoice(*module, "table", WavetableOscillatorModule::kLoadedTableChoice);

    // 8 * 2048 / 512 = 32 detected cycles, all of which fit under the 64-frame cap.
    EXPECT_EQ(module->getNumFrames(), 32);

    // Each detected cycle is one period, so the table plays back as a plain sine.
    module->prepareToPlay(kSampleRate, kBlockSize);
    const auto out = renderNote(*module, 69, 4);
    EXPECT_GT(magnitudeAt(out, 0, 440.0f, kSampleRate), 0.3f);
    EXPECT_LT(magnitudeAt(out, 0, 880.0f, kSampleRate), 0.05f) << "a detected single cycle must have no 2nd harmonic";

    file.deleteFile();
}

TEST_F(WavetableOscillatorModuleTest, SpectralImportKeepsMagnitudesAndDropsPhase) {
    // Two frames of the same harmonic in opposite phase. A normal import keeps them opposed,
    // so scanning between them cancels; a spectral import collapses both to sine phase, so the
    // midpoint keeps its level.
    const int frameSize = WavetableOscillatorModule::kFrameSize;
    juce::AudioBuffer<float> buffer(1, 2 * frameSize);
    float* data = buffer.getWritePointer(0);
    for (int i = 0; i < frameSize; ++i) {
        const float p = (float)i / (float)frameSize * juce::MathConstants<float>::twoPi;
        data[i] = 0.8f * std::sin(p);
        data[frameSize + i] = -0.8f * std::sin(p);
    }

    const juce::File file =
        juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("wt180-spectral.wav");
    file.deleteFile();
    juce::WavAudioFormat format;
    std::unique_ptr<juce::FileOutputStream> stream(file.createOutputStream());
    ASSERT_NE(stream, nullptr);
    std::unique_ptr<juce::AudioFormatWriter> writer(format.createWriterFor(stream.get(), kSampleRate, 1, 16, {}, 0));
    ASSERT_NE(writer, nullptr);
    stream.release();
    writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples());
    writer.reset();

    const auto midpointLevel = [&](WavetableOscillatorModule::ImportMode mode) {
        auto mod = std::make_unique<WavetableOscillatorModule>();
        setChoice(*mod, "importMode", (int)mode);
        EXPECT_TRUE(mod->loadWavetableFile(file));
        setChoice(*mod, "table", WavetableOscillatorModule::kLoadedTableChoice);
        setFloat(*mod, "position", 0.5f);
        mod->prepareToPlay(kSampleRate, kBlockSize);
        return rms(renderNote(*mod, 60, 4), 0);
    };

    const float plain = midpointLevel(WavetableOscillatorModule::ImportMode::Auto);
    const float spectral = midpointLevel(WavetableOscillatorModule::ImportMode::Spectral);

    EXPECT_LT(plain, 0.05f) << "opposed frames must cancel at the midpoint on a normal import";
    EXPECT_GT(spectral, 0.2f) << "a spectral import must make the frames phase-coherent";

    file.deleteFile();
}

TEST_F(WavetableOscillatorModuleTest, HermiteInterpolationTracksLinearButStaysBounded) {
    setFloat(*module, "position", 0.0f);
    setChoice(*module, "interpolation", (int)WavetableOscillatorModule::Interpolation::Hermite);
    module->prepareToPlay(kSampleRate, kBlockSize);

    const auto out = renderNote(*module, 60, 4);
    EXPECT_GT(magnitudeAt(out, 0, 261.6f, kSampleRate), 0.5f) << "a sine must still be a sine";
    EXPECT_LT(out.getMagnitude(0, 0, out.getNumSamples()), 1.5f);

    // The cubic fit must not ring on the coarse mips a high note selects.
    setChoice(*module, "table", 5); // Digital — the busiest built-in
    setFloat(*module, "position", 1.0f);
    module->prepareToPlay(kSampleRate, kBlockSize);
    const auto high = renderNote(*module, 108, 4);
    EXPECT_LT(high.getMagnitude(0, 0, high.getNumSamples()), 2.0f);
}

TEST_F(WavetableOscillatorModuleTest, FolderBrowserScansStepsAndWraps) {
    const juce::File folder = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("wt180-browser");
    folder.deleteRecursively();
    ASSERT_TRUE(folder.createDirectory());

    // Three readable tables plus a file the loader must ignore.
    for (const char* name : {"a-table.wav", "b-table.wav", "c-table.wav"}) {
        const juce::File src = writeWavetableFile(juce::String("tmp-") + name, 4);
        ASSERT_TRUE(src.existsAsFile());
        ASSERT_TRUE(src.moveFileTo(folder.getChildFile(name)));
    }
    folder.getChildFile("notes.txt").replaceWithText("not a wavetable");

    module->setWavetableFolder(folder);
    EXPECT_EQ(module->getWavetableFolder(), folder);
    ASSERT_EQ(module->getFolderWavetableCount(), 3) << "only audio files belong in the browser list";
    EXPECT_EQ(module->getFolderIndex(), -1) << "scanning must not load anything on its own";

    // Next from a fresh scan lands on the first entry, sorted by name.
    ASSERT_TRUE(module->nextWavetable());
    EXPECT_EQ(module->getFolderIndex(), 0);
    EXPECT_EQ(module->getWavetableFile().getFileName(), "a-table.wav");

    ASSERT_TRUE(module->nextWavetable());
    EXPECT_EQ(module->getWavetableFile().getFileName(), "b-table.wav");

    // Wrapping at both ends.
    ASSERT_TRUE(module->nextWavetable());
    ASSERT_TRUE(module->nextWavetable());
    EXPECT_EQ(module->getFolderIndex(), 0) << "next past the end must wrap to the start";

    ASSERT_TRUE(module->previousWavetable());
    EXPECT_EQ(module->getFolderIndex(), 2) << "previous past the start must wrap to the end";

    // An empty folder leaves the browser inert rather than failing.
    const juce::File empty = folder.getChildFile("empty");
    ASSERT_TRUE(empty.createDirectory());
    module->setWavetableFolder(empty);
    EXPECT_EQ(module->getFolderWavetableCount(), 0);
    EXPECT_FALSE(module->nextWavetable());

    folder.deleteRecursively();
}
