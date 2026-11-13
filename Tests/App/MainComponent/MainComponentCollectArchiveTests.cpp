// MainComponentCollectArchiveTests.cpp — the File menu's "Collect & Archive..." command, driven
// through the command manager with its prompts, progress window and message box replaced by
// CollectArchiveSeams: it saves the collected project, writes the zip, and leaves the document clean.
#include "AudioEngine/AudioEngine.h"
#include "MainComponentTestFixture.h"
#include "Modules/SamplerModule.h"
#include "ShortcutManager/AppCommands.h"
#include <gtest/gtest.h>
#include <juce_audio_formats/juce_audio_formats.h>

namespace {

bool writeWav(const juce::File& file) {
    file.getParentDirectory().createDirectory();
    std::unique_ptr<juce::FileOutputStream> stream(file.createOutputStream());
    if (stream == nullptr || stream->failedToOpen())
        return false;
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(stream.get(), 44100.0, 1, 16, {}, 0));
    if (writer == nullptr)
        return false;
    stream.release();
    juce::AudioBuffer<float> buffer(1, 4410);
    for (int i = 0; i < buffer.getNumSamples(); ++i)
        buffer.setSample(0, i, 0.5f * std::sin((float)i * 0.05f));
    return writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples());
}

SamplerModule* addSampler(MainComponent& mc, const juce::File& sample) {
    auto node = mc.getAudioEngine().getGraph().addNode(std::make_unique<SamplerModule>());
    auto* sampler = dynamic_cast<SamplerModule*>(node->getProcessor());
    EXPECT_TRUE(sampler->loadSampleFile(sample));
    return sampler;
}

// Every seam answered synchronously: the task runs inline, and reports are recorded.
void installSeams(MainComponent& mc, synth::CollectArchiveChoice choice, const juce::File& zip,
                  juce::StringArray& reports) {
    mc.collectArchiveSeams.choicePrompt = [choice](auto onChoice) { onChoice(choice); };
    mc.collectArchiveSeams.zipFilePrompt = [zip](auto onFile) { onFile(zip); };
    mc.collectArchiveSeams.runTask = [](const juce::String&, bool, auto work, auto done) {
        work([](double) { return true; });
        done(false);
    };
    mc.collectArchiveSeams.report = [&reports](const juce::String& title, const juce::String& message) {
        reports.add(title + ": " + message);
    };
}

} // namespace

TEST_F(MainComponentTest, CollectAndArchiveCommandIsMenuOnlyAndEnabled) {
    MainComponent mc(std::make_unique<MockProvider>());
    mc.getAudioEngine().suspendDeviceCallback();
    juce::ApplicationCommandInfo info(AppCommands::collectAndArchive);
    mc.getCommandInfo(AppCommands::collectAndArchive, info);
    EXPECT_TRUE((info.flags & juce::ApplicationCommandInfo::isDisabled) == 0);
    EXPECT_TRUE(info.defaultKeypresses.isEmpty()) << "menu-only: no chord";
}

TEST_F(MainComponentTest, CollectAndArchiveCopiesTheSampleSavesAndZips) {
    MainComponent mc(std::make_unique<MockProvider>());
    mc.getAudioEngine().suspendDeviceCallback();
    const auto original = tempRoot.getChildFile("Downloads/kick.wav");
    ASSERT_TRUE(writeWav(original));
    auto* sampler = addSampler(mc, original);
    const auto bundle = tempRoot.getChildFile("Song.agsproj");
    ASSERT_TRUE(mc.saveProjectForTest(bundle));

    const auto zip = tempRoot.getChildFile("Song.zip");
    juce::StringArray reports;
    installSeams(mc, synth::CollectArchiveChoice::CollectAndZip, zip, reports);
    ASSERT_TRUE(mc.getCommandManager().invokeDirectly(AppCommands::collectAndArchive, false));

    const auto copy = bundle.getChildFile("Samples/kick.wav");
    EXPECT_TRUE(reports.isEmpty()) << reports.joinIntoString("\n");
    EXPECT_TRUE(copy.existsAsFile());
    EXPECT_TRUE(original.existsAsFile());
    EXPECT_EQ(sampler->getSampleFilePath(), copy.getFullPathName());
    EXPECT_TRUE(bundle.getChildFile("project.json").loadFileAsString().contains("\"Samples/kick.wav\""))
        << "the collected references must be saved, not just held in memory";
    EXPECT_FALSE(mc.isProjectDirty());
    EXPECT_TRUE(zip.existsAsFile());
    EXPECT_NE(juce::ZipFile(zip).getEntry("Song.agsproj/Samples/kick.wav"), nullptr);
}

TEST_F(MainComponentTest, CollectOnlyReportsAMissingSampleAndWritesNoZip) {
    MainComponent mc(std::make_unique<MockProvider>());
    mc.getAudioEngine().suspendDeviceCallback();
    const auto original = tempRoot.getChildFile("Downloads/gone.wav");
    ASSERT_TRUE(writeWav(original));
    auto* sampler = addSampler(mc, original);
    const auto bundle = tempRoot.getChildFile("Song.agsproj");
    ASSERT_TRUE(mc.saveProjectForTest(bundle));
    ASSERT_TRUE(original.deleteFile());

    const auto zip = tempRoot.getChildFile("Song.zip");
    juce::StringArray reports;
    installSeams(mc, synth::CollectArchiveChoice::CollectOnly, zip, reports);
    ASSERT_TRUE(mc.getCommandManager().invokeDirectly(AppCommands::collectAndArchive, false));

    ASSERT_EQ(reports.size(), 1);
    EXPECT_TRUE(reports[0].contains(original.getFullPathName()));
    EXPECT_EQ(sampler->getSampleFilePath(), original.getFullPathName());
    EXPECT_FALSE(zip.exists());
}

TEST_F(MainComponentTest, CollectAndArchiveCancelChangesNothing) {
    MainComponent mc(std::make_unique<MockProvider>());
    mc.getAudioEngine().suspendDeviceCallback();
    const auto bundle = tempRoot.getChildFile("Song.agsproj");
    ASSERT_TRUE(mc.saveProjectForTest(bundle));
    const auto before = bundle.getChildFile("project.json").getLastModificationTime();

    juce::StringArray reports;
    installSeams(mc, synth::CollectArchiveChoice::Cancel, tempRoot.getChildFile("x.zip"), reports);
    bool ranTask = false;
    mc.collectArchiveSeams.runTask = [&ranTask](const juce::String&, bool, auto, auto) { ranTask = true; };
    ASSERT_TRUE(mc.getCommandManager().invokeDirectly(AppCommands::collectAndArchive, false));

    EXPECT_FALSE(ranTask);
    EXPECT_EQ(bundle.getChildFile("project.json").getLastModificationTime(), before);
}
