// ChannelFlowCoreSoundsTests.cpp
//
// A timelineOps `addInstrumentTrack` op that names a core pack sound, applied through the REAL app host: the pack's
// instrument when it is installed, the stand-in synth marked to be swapped when it is not, that mark surviving a
// save, and the swap (one undo step) once the pack arrives. The pack is a tiny fake in a temp folder.

#include "../../SoundPacks/CoreSoundTestPack.h"
#include "../../SoundPacks/SoundPackTestKit.h"
#include "AI/AIStateMapper/AIStateMapper.h"
#include "ChannelFlowTestFixture.h"
#include "MainComponent/CoreSoundSwapper.h"
#include "Modules/DrumKit/DrumKitModule.h"
#include "Modules/ModuleBase.h"
#include "Modules/PendingSound.h"
#include "Modules/SamplerModule.h"
#include "SoundPacks/CoreSounds.h"
#include <gtest/gtest.h>
#include <map>

namespace {

using core_sound_test::FakeCorePack;

int countNamed(juce::AudioProcessorGraph& graph, const juce::String& name) {
    int count = 0;
    for (auto* node : graph.getNodes())
        if (node != nullptr && node->getProcessor() != nullptr &&
            synth::AIStateMapper::getFactoryTypeName(node->getProcessor()) == name)
            ++count;
    return count;
}

template <class T>
T* firstOf(juce::AudioProcessorGraph& graph, const juce::String& name) {
    for (auto* node : graph.getNodes())
        if (node != nullptr && node->getProcessor() != nullptr &&
            synth::AIStateMapper::getFactoryTypeName(node->getProcessor()) == name)
            if (auto* module = dynamic_cast<T*>(node->getProcessor()))
                return module;
    return nullptr;
}

double paramValue(juce::AudioProcessor& processor, const juce::String& id) {
    for (auto* p : processor.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(p); ranged != nullptr && ranged->paramID == id)
            return ranged->convertFrom0to1(ranged->getValue());
    return -1.0;
}

juce::String trackOp(const juce::String& name, const juce::String& instrument, const juce::String& sound,
                     const juce::String& extra = {}) {
    return R"({"timelineOps": [{"op": "addInstrumentTrack", "name": ")" + name + R"(", "instrument": ")" + instrument +
           R"(", "sound": ")" + sound + "\"" + extra + "}]}";
}

struct SoundsRig {
    explicit SoundsRig(bool packInstalled)
        : pack(packInstalled)
        , mc(std::make_unique<MockProviderCFT>()) {
        mc.setSize(1600, 900);
        mc.getAudioEngine().suspendDeviceCallback();
    }
    bool apply(const juce::String& json) {
        const auto result = mc.getAiServiceForTest().applyTimelineOps(juce::JSON::parse(json));
        EXPECT_TRUE(result.ok) << result.message;
        return result.ok;
    }
    juce::AudioProcessorGraph& graph() { return mc.getAudioEngine().getGraph(); }

    FakeCorePack pack;
    MainComponent mc;
};

} // namespace

TEST_F(ChannelFlowTest, ACoreSoundTrackWithThePackInstalledPlaysThePackInstrumentOnASampler) {
    SoundsRig rig(true);
    const int mallets = countNamed(rig.graph(), "Mallet");
    ASSERT_TRUE(rig.apply(trackOp("Keys", "Mallet", "core/upright-piano", R"(, "instrumentParams": {"level": 0.5})")));

    EXPECT_EQ(countNamed(rig.graph(), "Mallet"), mallets) << "no stand-in was built";
    auto* sampler = firstOf<SamplerModule>(rig.graph(), "Sampler");
    ASSERT_NE(sampler, nullptr);
    EXPECT_EQ(sampler->getInstrumentKind(), synth::sampler::InstrumentSource::Kind::Pack);
    EXPECT_EQ(paramValue(*sampler, "playMode"), 2.0) << "Instrument mode";
    EXPECT_EQ(synth::pendingSoundOf(*sampler), "");
    bool named = false;
    for (const auto& track : rig.mc.getTimelineDoc().getTracks())
        named = named || track.name == "Keys";
    EXPECT_TRUE(named) << "the track keeps the name the op gave it";
}

TEST_F(ChannelFlowTest, ACoreDrumKitWithThePackInstalledHasTheKitLoaded) {
    SoundsRig rig(true);
    ASSERT_TRUE(rig.apply(trackOp("Drums", "Drum Kit", "core/acoustic-kit")));
    auto* kit = firstOf<DrumKitModule>(rig.graph(), "Drum Kit");
    ASSERT_NE(kit, nullptr);
    EXPECT_TRUE(kit->hasRecordedKit());
    EXPECT_EQ(kit->getExtraState().getProperty("instrumentPack", {}).toString(), "core/acoustic-kit");
}

TEST_F(ChannelFlowTest, ACoreSoundTrackWithoutThePackBuildsTheStandInAndRemembersTheSound) {
    SoundsRig rig(false);
    ASSERT_TRUE(rig.apply(trackOp("Bass", "Pluck", "core/double-bass-plucked")));
    auto* pluck = firstOf<ModuleBase>(rig.graph(), "Pluck");
    ASSERT_NE(pluck, nullptr);
    EXPECT_EQ(synth::pendingSoundOf(*pluck), "core/double-bass-plucked");
    EXPECT_EQ(countNamed(rig.graph(), "Sampler"), 0);
}

TEST_F(ChannelFlowTest, AStandInKeepsItsParamsAndAnInstalledSamplerKeepsOnlyLevel) {
    {
        SoundsRig rig(false);
        ASSERT_TRUE(
            rig.apply(trackOp("Bass", "Pluck", "core/electric-guitar", R"(, "instrumentParams": {"level": 0.25})")));
        auto* pluck = firstOf<ModuleBase>(rig.graph(), "Pluck");
        ASSERT_NE(pluck, nullptr);
        const double level = paramValue(*pluck, "level");
        EXPECT_NEAR(level, 0.25, 0.01);
    }
    SoundsRig rig(true);
    ASSERT_TRUE(rig.apply(
        trackOp("Lead", "Wavetable", "core/flute",
                R"(, "poly": true, "instrumentParams": {"level": 0.25}, "envelope": {"params": {"attack": 0.2}})")));
    auto* sampler = firstOf<SamplerModule>(rig.graph(), "Sampler");
    ASSERT_NE(sampler, nullptr);
    EXPECT_NEAR(paramValue(*sampler, "level"), 0.25, 0.01);
}

TEST_F(ChannelFlowTest, ACoreDrumKitWithoutThePackNamesTheKitAndPlaysSynthDrums) {
    SoundsRig rig(false);
    ASSERT_TRUE(rig.apply(trackOp("Drums", "Drum Kit", "core/acoustic-kit")));
    auto* kit = firstOf<DrumKitModule>(rig.graph(), "Drum Kit");
    ASSERT_NE(kit, nullptr);
    EXPECT_FALSE(kit->hasRecordedKit());
    EXPECT_EQ(kit->getExtraState().getProperty("instrumentPack", {}).toString(), "core/acoustic-kit");
    EXPECT_EQ(synth::pendingSoundOf(*kit), "") << "a kit's reference is its pending sound";

    rig.pack.install();
    EXPECT_EQ(rig.mc.getCoreSoundSwapperForTest().swapNow(), 1);
    EXPECT_TRUE(kit->hasRecordedKit()) << "the kit resolves when the pack is installed";
}

TEST_F(ChannelFlowTest, ASoundThatDoesNotMatchTheStandInIsIgnored) {
    SoundsRig rig(true);
    ASSERT_TRUE(rig.apply(trackOp("Keys", "Oscillator", "core/upright-piano")));
    EXPECT_EQ(countNamed(rig.graph(), "Sampler"), 0) << "the sound is dropped and the oscillator track stays";
}

TEST(CoreSoundPending, TheMarkIsSavedAndRestoredAndNeverShownToAModel) {
    auto original = synth::AIStateMapper::createModule("Wavetable");
    auto* module = dynamic_cast<ModuleBase*>(original.get());
    ASSERT_NE(module, nullptr);
    synth::setPendingSound(*module, "core/flute");
    juce::AudioProcessorGraph graph;
    graph.addNode(std::move(original));
    const auto json = synth::AIStateMapper::graphToJSON(graph);

    juce::AudioProcessorGraph restored;
    ASSERT_TRUE(synth::AIStateMapper::applyJSONToGraph(json, restored, true, /*trusted=*/true));
    auto* back = firstOf<ModuleBase>(restored, "Wavetable");
    ASSERT_NE(back, nullptr);
    EXPECT_EQ(synth::pendingSoundOf(*back), "core/flute");

    juce::AudioProcessorGraph fromModel;
    ASSERT_TRUE(synth::AIStateMapper::applyJSONToGraph(json, fromModel, true, /*trusted=*/false));
    auto* untrusted = firstOf<ModuleBase>(fromModel, "Wavetable");
    ASSERT_NE(untrusted, nullptr);
    EXPECT_EQ(synth::pendingSoundOf(*untrusted), "") << "model output cannot set it";
}

TEST_F(ChannelFlowTest, TheSwapAfterTheDownloadReplacesEveryStandInInOneUndoStepAndUndoPutsThemBack) {
    SoundsRig rig(false);
    ASSERT_TRUE(rig.apply(trackOp("Keys", "Mallet", "core/upright-piano")));
    ASSERT_TRUE(rig.apply(trackOp("Bass", "Pluck", "core/double-bass-plucked")));
    ASSERT_TRUE(rig.apply(trackOp("Plain", "Oscillator", "")));
    const auto before = snapshotCFT(rig.mc);
    const int samplers = countNamed(rig.graph(), "Sampler");

    int toldTracks = 0;
    bool toldUndoable = false;
    auto& swapper = rig.mc.getCoreSoundSwapperForTest();
    swapper.onSwapped = [&](int tracks, bool undoable) {
        toldTracks = tracks;
        toldUndoable = undoable;
    };
    rig.pack.install();
    EXPECT_EQ(swapper.swapNow(), 2);
    EXPECT_EQ(toldTracks, 2);
    EXPECT_TRUE(toldUndoable);
    EXPECT_EQ(countNamed(rig.graph(), "Mallet"), 0);
    EXPECT_EQ(countNamed(rig.graph(), "Pluck"), 0);
    EXPECT_EQ(countNamed(rig.graph(), "Sampler"), samplers + 2);
    EXPECT_EQ(countNamed(rig.graph(), "Oscillator") > 0, true) << "a track with no sound is untouched";
    EXPECT_EQ(swapper.swapNow(), 0) << "nothing is left to swap";

    ASSERT_TRUE(rig.mc.getUndoManager().undo());
    EXPECT_EQ(countNamed(rig.graph(), "Sampler"), samplers) << "one undo puts both stand-ins back";
    EXPECT_EQ(countNamed(rig.graph(), "Mallet"), 1);
    EXPECT_EQ(countNamed(rig.graph(), "Pluck"), 1);
    EXPECT_EQ(synth::pendingSoundOf(*firstOf<ModuleBase>(rig.graph(), "Mallet")), "core/upright-piano");
    EXPECT_EQ(synth::pendingSoundOf(*firstOf<ModuleBase>(rig.graph(), "Pluck")), "core/double-bass-plucked");
    const auto restored = snapshotCFT(rig.mc);
    EXPECT_EQ(restored.doc, before.doc);
    EXPECT_EQ(restored.macros, before.macros) << "the stand-ins are back inside their macros";
}

namespace {

// A core pack zip whose instrument plays a real (tiny) WAV, so the Sampler that replaces a stand-in can load it.
juce::MemoryBlock corePackZipWithAudio() {
    sampler_test::TempDir scratch;
    const auto wavFile = scratch.file("x.wav");
    sampler_test::writeWav(wavFile, 2000, 1, 44100.0, [](int, int i) { return 0.01f * (float)(i % 10); });
    juce::MemoryBlock wav;
    wavFile.loadFileAsData(wav);
    const juce::String sfz = "<region> sample=x.wav";
    const juce::String manifest =
        R"({"id":"core","name":"Core","version":"1.0.0","instruments":[{"id":"upright-piano","name":"Upright piano",)"
        R"("sfz":"x.sfz","category":"Keys"}]})";
    juce::ZipFile::Builder builder;
    const auto now = juce::Time::getCurrentTime();
    builder.addEntry(new juce::MemoryInputStream(manifest.toRawUTF8(), manifest.getNumBytesAsUTF8(), true), 5,
                     "core/pack.json", now);
    builder.addEntry(new juce::MemoryInputStream(sfz.toRawUTF8(), sfz.getNumBytesAsUTF8(), true), 5, "core/x.sfz", now);
    builder.addEntry(new juce::MemoryInputStream(wav.getData(), wav.getSize(), true), 0, "core/x.wav", now);
    juce::MemoryBlock block;
    juce::MemoryOutputStream out(block, false);
    builder.writeToStream(out, nullptr);
    return block;
}

} // namespace

TEST_F(ChannelFlowTest, TheInstallerFinishingTheCorePackSwapsTheStandInsWithoutAnyCallFromTheTest) {
    juce::SharedResourcePointer<synth::soundpacks::SoundPackInstaller> installer;
    sound_pack_test::TempDir dir;
    sound_pack_test::FakeServer server;
    installer->setInstallRootForTesting(dir.path());
    installer->setIndexUrlForTesting(sound_pack_test::kIndexUrl);
    installer->setFetcherForTesting(server.fetcher());
    installer->setRunInlineForTesting(true);
    const auto zip = corePackZipWithAudio();
    const auto index = sound_pack_test::indexJson("core", "1.0.0", zip);
    server.bodies[sound_pack_test::kIndexUrl] = juce::MemoryBlock(index.toRawUTF8(), index.getNumBytesAsUTF8());
    server.bodies["https://example.test/core-1.0.0.zip"] = zip;
    // The library searches the folder the installer unpacks into, so the pack the install lands is the one it reads.
    synth::sampler::SoundPackLibrary::setRootsForTests(std::vector<juce::File>{dir.path()});

    {
        MainComponent mc(std::make_unique<MockProviderCFT>());
        mc.setSize(1600, 900);
        mc.getAudioEngine().suspendDeviceCallback();
        const auto applied = mc.getAiServiceForTest().applyTimelineOps(
            juce::JSON::parse(trackOp("Keys", "Mallet", "core/upright-piano")));
        ASSERT_TRUE(applied.ok) << applied.message;
        auto& graph = mc.getAudioEngine().getGraph();
        ASSERT_EQ(countNamed(graph, "Mallet"), 1);
        int toldTracks = 0;
        mc.getCoreSoundSwapperForTest().onSwapped = [&](int tracks, bool) { toldTracks = tracks; };

        installer->refreshIndex();
        ASSERT_TRUE(installer->install("core"));

        EXPECT_EQ(countNamed(graph, "Mallet"), 0);
        EXPECT_EQ(toldTracks, 1);
        auto* sampler = firstOf<SamplerModule>(graph, "Sampler");
        ASSERT_NE(sampler, nullptr);
        EXPECT_EQ(sampler->getInstrumentKind(), synth::sampler::InstrumentSource::Kind::Pack);
    }
    synth::sampler::SoundPackLibrary::setRootsForTests(std::nullopt);
    installer->setFetcherForTesting(synth::soundpacks::makeHttpFetcher());
}

namespace {

std::map<juce::String, int> moduleTypeCounts(juce::AudioProcessorGraph& graph) {
    std::map<juce::String, int> counts;
    for (auto* node : graph.getNodes())
        if (node != nullptr && node->getProcessor() != nullptr)
            ++counts[synth::AIStateMapper::getFactoryTypeName(node->getProcessor())];
    return counts;
}

juce::var recordedPlan() {
    return juce::JSON::parse(juce::File(AI_PLAN_FIXTURES_DIR).getChildFile("orchestral-core-sound.json"));
}

float firstRegionVolumeDb(SamplerModule& sampler) {
    const auto& regions = sampler.getInstrumentRegions();
    return regions.empty() ? -999.0f : regions.front().volumeDb;
}

} // namespace

// Regression test for FRO1016: a recorded song answer whose Wavetable track names a core sound and an envelope id was
// rejected with "has no envelope" when the pack was installed, because the Sampler track was built without the ADSR.
TEST_F(ChannelFlowTest, ARecordedSongWithAnEnvelopeOnACoreSoundBuildsWithThePackInstalled) {
    std::map<juce::String, int> installedTypes, missingTypes;
    {
        SoundsRig rig(true);
        const auto applied = rig.mc.getAiServiceForTest().applyProjectEdit(recordedPlan());
        ASSERT_TRUE(applied.ok) << applied.message;
        installedTypes = moduleTypeCounts(rig.graph());
        auto* sampler = firstOf<SamplerModule>(rig.graph(), "Sampler");
        ASSERT_NE(sampler, nullptr);
        EXPECT_EQ(sampler->getInstrumentKind(), synth::sampler::InstrumentSource::Kind::Pack);
        auto* adsr = firstOf<ModuleBase>(rig.graph(), "ADSR");
        ASSERT_NE(adsr, nullptr) << "the envelope the song's ops address exists";
        EXPECT_NEAR(paramValue(*adsr, "attack"), 1.2, 0.01) << "and carries the op's params";
    }
    {
        SoundsRig rig(false);
        const auto applied = rig.mc.getAiServiceForTest().applyProjectEdit(recordedPlan());
        ASSERT_TRUE(applied.ok) << applied.message;
        missingTypes = moduleTypeCounts(rig.graph());
        EXPECT_EQ(countNamed(rig.graph(), "Wavetable"), 1) << "the pack missing still builds the stand-in";
        EXPECT_EQ(synth::pendingSoundOf(*firstOf<ModuleBase>(rig.graph(), "Wavetable")), "core/string-section");
        EXPECT_EQ(countNamed(rig.graph(), "ADSR"), 1);
    }
    // The track body is the stand-in's, whichever instrument plays: only Wavetable and Sampler differ, and the poly
    // stand-in's Poly MIDI stage, which a Sampler (no poly mode) does without.
    missingTypes["Sampler"] += missingTypes["Wavetable"];
    missingTypes["Wavetable"] = 0;
    missingTypes["Poly MIDI"] = 0;
    installedTypes["Poly MIDI"] = 0;
    installedTypes["Wavetable"] = 0;
    EXPECT_EQ(installedTypes, missingTypes);
}

TEST_F(ChannelFlowTest, TheTrimsAreOnTheMeasuredSoundsAndTheKitHasNone) {
    for (const char* id : {"core/upright-piano", "core/string-section", "core/trumpet", "core/french-horn",
                           "core/flute", "core/clarinet"}) {
        const auto* sound = synth::soundpacks::findCoreSound(id);
        ASSERT_NE(sound, nullptr);
        EXPECT_GT(sound->trimDb, 0.0f) << id;
        EXPECT_LE(sound->trimDb, 24.0f) << id;
    }
    EXPECT_EQ(synth::soundpacks::findCoreSound("core/acoustic-kit")->trimDb, 0.0f);
}

TEST_F(ChannelFlowTest, TheSoundsTrimIsAppliedWhenTheTrackIsBuiltAndWhenTheStandInIsSwapped) {
    const auto* sound = synth::soundpacks::findCoreSound("core/clarinet");
    ASSERT_NE(sound, nullptr);
    {
        SoundsRig rig(true);
        ASSERT_TRUE(rig.apply(trackOp("Reed", "Wavetable", "core/clarinet")));
        auto* sampler = firstOf<SamplerModule>(rig.graph(), "Sampler");
        ASSERT_NE(sampler, nullptr);
        EXPECT_NEAR(firstRegionVolumeDb(*sampler), sound->trimDb, 0.001f) << "at build, pack installed";
    }
    SoundsRig rig(false);
    ASSERT_TRUE(rig.apply(trackOp("Reed", "Wavetable", "core/clarinet", R"(, "instrumentParams": {"level": 0.37})")));
    rig.pack.install();
    ASSERT_EQ(rig.mc.getCoreSoundSwapperForTest().swapNow(), 1);
    auto* sampler = firstOf<SamplerModule>(rig.graph(), "Sampler");
    ASSERT_NE(sampler, nullptr);
    EXPECT_NEAR(firstRegionVolumeDb(*sampler), sound->trimDb, 0.001f) << "in the swap after the download";
    EXPECT_NEAR(paramValue(*sampler, "level"), 0.37, 0.01) << "the stand-in's Level comes along, as at build";

    // The trim is saved with the instrument and comes back; an instrument picked by hand carries none.
    auto restored = synth::AIStateMapper::createModule("Sampler");
    auto* copy = dynamic_cast<SamplerModule*>(restored.get());
    copy->setExtraState(sampler->getExtraState());
    EXPECT_NEAR(firstRegionVolumeDb(*copy), sound->trimDb, 0.001f);
    auto picked = synth::AIStateMapper::createModule("Sampler");
    auto* plain = dynamic_cast<SamplerModule*>(picked.get());
    ASSERT_TRUE(plain->loadInstrumentFromPack("core", "clarinet"));
    EXPECT_EQ(firstRegionVolumeDb(*plain), 0.0f);
    EXPECT_FALSE(plain->getExtraState().getProperty("instrumentGainDb", {}).isDouble());
}
