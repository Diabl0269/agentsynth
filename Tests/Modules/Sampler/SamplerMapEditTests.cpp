// SamplerMapEditTests.cpp -- editing an instrument's regions: the pure map rules (rows, arrow keys, drops), the
// module's edit API (what plays after an edit), the conversion of an SFZ or pack instrument into the module's own
// recordings, and the saved state of every region field.
#include "Modules/ModuleFileStateKeys.h"
#include "Modules/Sampler/SamplerInstrument.h"
#include "Modules/Sampler/SamplerRegionEdit.h"
#include "Modules/Sampler/SoundPackLibrary.h"
#include "Modules/SamplerModule.h"
#include "SamplerTestWavs.h"
#include <cmath>
#include <gtest/gtest.h>

using namespace sampler_test;
using namespace synth::sampler;

namespace {

constexpr double kRate = 44100.0;
constexpr int kBlock = 256;
constexpr float kHeadroom = 0.35481339f;

struct Fixture {
    Fixture() {
        writeWav(dir.file("soft.wav"), 88200, 1, kRate, [](int, int) { return 0.2f; });
        writeWav(dir.file("loud.wav"), 88200, 1, kRate, [](int, int) { return 0.6f; });
        writeWav(dir.file("sine.wav"), 88200, 1, kRate,
                 [](int, int i) { return 0.5f * std::sin(0.0628319f * (float)i); });
        dir.file("layers.sfz")
            .replaceWithText("<group> lokey=0 hikey=127 pitch_keycenter=60\n"
                             "<region> sample=soft.wav lovel=1 hivel=64\n"
                             "<region> sample=loud.wav lovel=65 hivel=127\n");
        dir.file("sine.sfz").replaceWithText("<region> sample=sine.wav key=60 loop_mode=loop_continuous\n");
        // Every RegionSpec field set to something other than its default.
        dir.file("rich.sfz")
            .replaceWithText("<group> lokey=40 hikey=90 pitch_keycenter=62\n"
                             "<region> sample=soft.wav lovel=1 hivel=64 tune=30 volume=-3 loop_mode=loop_continuous "
                             "loop_start=100 loop_end=5000 ampeg_release=0.2\n"
                             "<region> sample=loud.wav lovel=65 hivel=127 seq_length=2 seq_position=1\n"
                             "<region> sample=sine.wav lovel=65 hivel=127 seq_length=2 seq_position=2 "
                             "loop_mode=one_shot\n"
                             "<region> sample=soft.wav trigger=release lovel=1 hivel=127 volume=-6\n");
    }
    TempDir dir;
};

struct Out {
    std::vector<float> left;
    float peak() const {
        float m = 0.0f;
        for (float v : left)
            m = std::max(m, std::fabs(v));
        return m;
    }
};

void setActual(SamplerModule& module, const juce::String& id, float value) {
    auto* p = dynamic_cast<juce::RangedAudioParameter*>(findParameterByID(&module, id));
    ASSERT_NE(p, nullptr) << id;
    p->setValueNotifyingHost(p->convertTo0to1(value));
}

struct Rig {
    Rig() {
        setActual(module, "level", 1.0f);
        setActual(module, "feel", 0.0f);
        setActual(module, "release", 0.05f);
        module.prepareToPlay(kRate, kBlock);
    }

    // Presses `notes` at `velocity` in the first block and lets go after 20 blocks; renders `blocks` in all.
    Out play(std::initializer_list<int> notes, int velocity, int blocks = 60) {
        Out out;
        const std::vector<int> list(notes);
        for (int b = 0; b < blocks; ++b) {
            juce::AudioBuffer<float> buffer(SamplerModule::kNumChannels, kBlock);
            buffer.clear();
            juce::MidiBuffer midi;
            for (int n : list) {
                if (b == 0)
                    midi.addEvent(juce::MidiMessage::noteOn(1, n, (juce::uint8)velocity), 0);
                if (b == 20)
                    midi.addEvent(juce::MidiMessage::noteOff(1, n), 0);
            }
            module.processBlock(buffer, midi);
            out.left.insert(out.left.end(), buffer.getReadPointer(0), buffer.getReadPointer(0) + kBlock);
        }
        return out;
    }
    SamplerModule module;
};

int upwardCrossings(const std::vector<float>& x) {
    int n = 0;
    for (size_t i = 1; i < x.size(); ++i)
        n += (x[i - 1] < 0.0f && x[i] >= 0.0f) ? 1 : 0;
    return n;
}

void expectSameRegion(const RegionSpec& a, const RegionSpec& b) {
    EXPECT_EQ(a.samplePath, b.samplePath);
    EXPECT_EQ(a.loKey, b.loKey);
    EXPECT_EQ(a.hiKey, b.hiKey);
    EXPECT_EQ(a.rootKey, b.rootKey);
    EXPECT_EQ(a.loVel, b.loVel);
    EXPECT_EQ(a.hiVel, b.hiVel);
    EXPECT_FLOAT_EQ(a.tuneCents, b.tuneCents);
    EXPECT_FLOAT_EQ(a.volumeDb, b.volumeDb);
    EXPECT_EQ((int)a.loop, (int)b.loop);
    EXPECT_EQ(a.loopStart, b.loopStart);
    EXPECT_EQ(a.loopEnd, b.loopEnd);
    EXPECT_EQ(a.seqLength, b.seqLength);
    EXPECT_EQ(a.seqPosition, b.seqPosition);
    EXPECT_EQ(a.releaseTrigger, b.releaseTrigger);
    EXPECT_FLOAT_EQ(a.ampReleaseSeconds, b.ampReleaseSeconds);
}

void expectSameRegions(const std::vector<RegionSpec>& a, const std::vector<RegionSpec>& b) {
    ASSERT_EQ(a.size(), b.size());
    for (size_t i = 0; i < a.size(); ++i)
        expectSameRegion(a[i], b[i]);
}

int hitsAt(const SamplerModule& module, int note, int velocity) {
    std::array<RegionHit, kMaxHitsPerNote> hits;
    return selectRegions(*module.getInstrument(), note, velocity, false, 0, hits);
}

RegionSpec spec(const juce::String& path, int lo, int hi, int loVel, int hiVel, int root = 60) {
    RegionSpec r;
    r.samplePath = path;
    r.loKey = lo;
    r.hiKey = hi;
    r.loVel = loVel;
    r.hiVel = hiVel;
    r.rootKey = root;
    return r;
}

} // namespace

// ---- The map's rules ----

TEST(SamplerRegionEdit, RowsGroupRecordingsByVelocityLoudFirstAndReleaseSoundsLast) {
    auto release = spec("r.wav", 0, 127, 1, 127);
    release.releaseTrigger = true;
    const std::vector<RegionSpec> specs = {spec("a.wav", 60, 71, 1, 63), spec("b.wav", 48, 59, 64, 127),
                                           spec("c.wav", 48, 59, 1, 63), spec("d.wav", 60, 71, 64, 127), release};
    const auto layers = mapLayers(specs);
    ASSERT_EQ(layers.size(), 3u);
    EXPECT_EQ(layers[0].regions, (std::vector<int>{1, 3})) << "loud row, in key order";
    EXPECT_EQ(layers[1].regions, (std::vector<int>{2, 0}));
    EXPECT_TRUE(layers[2].release);
    EXPECT_EQ(layerName(layers[0], 2, 0), "Loud");
    EXPECT_EQ(layerName(layers[1], 2, 1), "Soft");
    EXPECT_EQ(layerName(layers[2], 2, 2), "Release sounds");
    EXPECT_EQ(layerName(layers[0], 1, 0), "All velocities");
    EXPECT_EQ(layerName(layers[1], 3, 1), "Velocity 1 to 63");
}

TEST(SamplerRegionEdit, ArrowKeysWalkTheRowsAndStopAtTheEdges) {
    const std::vector<RegionSpec> specs = {spec("a.wav", 60, 71, 1, 63, 66), spec("b.wav", 48, 59, 64, 127, 54),
                                           spec("c.wav", 48, 59, 1, 63, 54), spec("d.wav", 60, 71, 64, 127, 66)};
    const auto layers = mapLayers(specs); // loud: b(1) d(3); soft: c(2) a(0)
    EXPECT_EQ(moveSelection(specs, layers, 1, MapMove::Right), 3);
    EXPECT_EQ(moveSelection(specs, layers, 3, MapMove::Right), 3) << "nowhere to go";
    EXPECT_EQ(moveSelection(specs, layers, 3, MapMove::Left), 1);
    EXPECT_EQ(moveSelection(specs, layers, 3, MapMove::Down), 0) << "the soft block under it";
    EXPECT_EQ(moveSelection(specs, layers, 0, MapMove::Up), 3);
    EXPECT_EQ(moveSelection(specs, layers, 1, MapMove::Up), 1);
    EXPECT_EQ(moveSelection(specs, layers, 2, MapMove::Down), 2);
}

TEST(SamplerRegionEdit, ADropReplacesOrAddsAndNeverLeavesTheInstrumentEmpty) {
    auto loop = spec("old.wav", 40, 50, 1, 127);
    loop.loopStart = 100;
    loop.loopEnd = 900;
    loop.loop = RegionSpec::Loop::Continuous;
    auto replaced = replaceRecording({loop}, 0, "new.wav");
    EXPECT_EQ(replaced[0].samplePath, "new.wav");
    EXPECT_EQ(replaced[0].loopStart, -1) << "loop points belong to the old file";
    EXPECT_EQ(replaced[0].loKey, 40);
    EXPECT_EQ((int)replaced[0].loop, (int)RegionSpec::Loop::Continuous);

    auto added = addRecording(replaced, "/x/Bell_C4.wav", 70, 20, 90);
    ASSERT_EQ(added.size(), 2u);
    EXPECT_EQ(added[1].loKey, 70);
    EXPECT_EQ(added[1].hiKey, 70);
    EXPECT_EQ(added[1].rootKey, 72) << "the note in the file name is the root, C4 = 72";
    EXPECT_EQ(added[1].loVel, 20);
    EXPECT_EQ(added[1].hiVel, 90);
    EXPECT_EQ(addRecording({}, "/x/plain.wav", 61, 1, 127)[0].rootKey, 61);

    EXPECT_EQ(removeRegion(added, 0).size(), 1u);
    EXPECT_EQ(removeRegion(replaced, 0).size(), 1u) << "the last recording stays";
    EXPECT_EQ(describeRegion(spec("/x/a.wav", 48, 52, 1, 63, 50)), "a.wav, keys C2 to E2, root D2, velocity 1 to 63");
}

// ---- The module's edit API ----

TEST(SamplerMapEdit, ChangingTheKeysChangesWhatPlaysOnThem) {
    Fixture f;
    Rig rig;
    ASSERT_TRUE(rig.module.loadInstrumentFromSfz(f.dir.file("layers.sfz")));
    ASSERT_EQ(hitsAt(rig.module, 5, 20), 1);
    ASSERT_EQ(hitsAt(rig.module, 20, 20), 1);
    auto regions = rig.module.getInstrumentRegions();
    ASSERT_EQ(regions.size(), 2u);
    regions[0].loKey = 30;
    regions[0].hiKey = 40;
    ASSERT_TRUE(rig.module.editInstrumentRegions(regions));
    EXPECT_EQ(hitsAt(rig.module, 5, 20), 0) << "the soft recording moved off these keys";
    EXPECT_EQ(hitsAt(rig.module, 35, 20), 1);
    EXPECT_EQ(rig.play({35}, 20).peak() > 0.0f, true);
    EXPECT_NEAR(rig.play({5}, 20).peak(), 0.0f, 1.0e-6f);
}

TEST(SamplerMapEdit, VelocityVolumeAndTuningChangeWhatRenders) {
    Fixture f;
    Rig rig;
    ASSERT_TRUE(rig.module.loadInstrumentFromSfz(f.dir.file("layers.sfz")));
    const float before = rig.play({60}, 110).peak();
    EXPECT_NEAR(before, 0.6f * kHeadroom, 0.01f);

    auto regions = rig.module.getInstrumentRegions();
    regions[1].volumeDb = -6.0206f;
    ASSERT_TRUE(rig.module.editInstrumentRegions(regions));
    EXPECT_NEAR(rig.play({60}, 110).peak(), before * 0.5f, 0.005f);

    regions[0].hiVel = 127; // the soft layer now reaches every hit, the loud one only the hardest
    regions[1].loVel = 120; // so a hit of 110 falls to the soft layer
    regions[1].volumeDb = 0.0f;
    ASSERT_TRUE(rig.module.editInstrumentRegions(regions));
    EXPECT_NEAR(rig.play({60}, 110).peak(), 0.2f * kHeadroom, 0.01f);

    ASSERT_TRUE(rig.module.loadInstrumentFromSfz(f.dir.file("sine.sfz")));
    const int base = upwardCrossings(rig.play({60}, 100, 30).left);
    auto sine = rig.module.getInstrumentRegions();
    sine[0].tuneCents = 1200.0f;
    ASSERT_TRUE(rig.module.editInstrumentRegions(sine));
    const int octave = upwardCrossings(rig.play({60}, 100, 30).left);
    EXPECT_NEAR((double)octave / (double)base, 2.0, 0.1);
}

TEST(SamplerMapEdit, AnEditKeepsTheDecodedSamplesOfUnchangedFiles) {
    Fixture f;
    SamplerModule module;
    ASSERT_TRUE(module.loadInstrumentFromSfz(f.dir.file("layers.sfz")));
    const auto* soft = module.getInstrument()->regions[0].sample.get();
    auto regions = module.getInstrumentRegions();
    regions[1].samplePath = f.dir.file("sine.wav").getFullPathName();
    ASSERT_TRUE(module.editInstrumentRegions(regions));
    EXPECT_EQ(module.getInstrument()->regions[0].sample.get(), soft);
    EXPECT_EQ(module.getInstrument()->regions[1].samplePath, f.dir.file("sine.wav").getFullPathName());
}

TEST(SamplerMapEdit, AnEditThatCannotPlayKeepsTheLoadedInstrumentAndSaysWhy) {
    Fixture f;
    SamplerModule module;
    ASSERT_TRUE(module.loadInstrumentFromSfz(f.dir.file("layers.sfz")));
    const auto regions = module.getInstrumentRegions();
    EXPECT_FALSE(module.editInstrumentRegions({}));
    auto broken = regions;
    for (auto& r : broken)
        r.samplePath = f.dir.file("missing.wav").getFullPathName();
    EXPECT_FALSE(module.editInstrumentRegions(broken));
    EXPECT_FALSE(module.getInstrumentMessages().isEmpty());
    expectSameRegions(module.getInstrumentRegions(), regions);
    EXPECT_EQ(module.getInstrumentKind(), InstrumentSource::Kind::Sfz) << "nothing was converted";
}

TEST(SamplerMapEdit, AnInstrumentCanBeBuiltFromNothingByAddingRecordings) {
    Fixture f;
    SamplerModule module;
    EXPECT_EQ(module.getInstrumentKind(), InstrumentSource::Kind::None);
    ASSERT_TRUE(module.editInstrumentRegions({spec(f.dir.file("loud.wav").getFullPathName(), 60, 60, 1, 127)}));
    EXPECT_EQ(module.getInstrumentKind(), InstrumentSource::Kind::Files);
    EXPECT_EQ(module.getInstrumentName(), "My instrument");
}

// ---- First edit: SFZ and pack instruments become the module's own recordings ----

TEST(SamplerMapEdit, AnSfzBecomesFilesOnTheFirstEditAndRendersTheSameBeforeIt) {
    Fixture f;
    Rig sfz, converted;
    ASSERT_TRUE(sfz.module.loadInstrumentFromSfz(f.dir.file("rich.sfz")));
    ASSERT_TRUE(converted.module.loadInstrumentFromSfz(f.dir.file("rich.sfz")));
    const auto original = converted.module.getInstrumentRegions();
    ASSERT_EQ(original.size(), 4u);
    EXPECT_EQ(converted.module.getInstrumentKind(), InstrumentSource::Kind::Sfz);

    ASSERT_TRUE(converted.module.editInstrumentRegions(original));
    EXPECT_EQ(converted.module.getInstrumentKind(), InstrumentSource::Kind::Files);
    expectSameRegions(converted.module.getInstrumentRegions(), original);

    for (int velocity : {40, 100}) {
        const auto a = sfz.play({60, 66}, velocity);
        const auto b = converted.play({60, 66}, velocity);
        ASSERT_EQ(a.left.size(), b.left.size());
        EXPECT_EQ(a.left, b.left) << "velocity " << velocity;
        EXPECT_GT(a.peak(), 0.0f);
    }
}

TEST(SamplerMapEdit, EveryRegionFieldSurvivesTheSavedState) {
    Fixture f;
    SamplerModule a;
    ASSERT_TRUE(a.loadInstrumentFromSfz(f.dir.file("rich.sfz")));
    ASSERT_TRUE(a.editInstrumentRegions(a.getInstrumentRegions()));
    const auto state = a.getExtraState();
    ASSERT_NE(state.getDynamicObject(), nullptr);
    EXPECT_FALSE(state.hasProperty(synth::module_file_keys::kInstrumentSfz));
    EXPECT_EQ(state[synth::module_file_keys::kInstrumentName].toString(), "rich");
    ASSERT_EQ(state[synth::module_file_keys::kInstrumentFiles].size(), 3) << "one entry per distinct file";
    for (int i = 0; i < 3; ++i)
        EXPECT_TRUE(juce::File::isAbsolutePath(state[synth::module_file_keys::kInstrumentFiles][i].toString()));

    SamplerModule b;
    b.setExtraState(state);
    ASSERT_NE(b.getInstrument(), nullptr);
    expectSameRegions(b.getInstrumentRegions(), a.getInstrumentRegions());
    EXPECT_EQ(b.getInstrumentName(), "rich");
    EXPECT_EQ(juce::JSON::toString(b.getExtraState()), juce::JSON::toString(state));
}

TEST(SamplerMapEdit, PlainDroppedRecordingsAreSavedInTheOldShape) {
    Fixture f;
    ASSERT_TRUE(writeWav(f.dir.file("Piano_C3.wav"), 4000, 1, kRate, [](int, int) { return 0.3f; }));
    ASSERT_TRUE(writeWav(f.dir.file("Piano_C4.wav"), 4000, 1, kRate, [](int, int) { return 0.3f; }));
    SamplerModule a;
    ASSERT_TRUE(a.loadInstrumentFromFiles(
        {f.dir.file("Piano_C3.wav").getFullPathName(), f.dir.file("Piano_C4.wav").getFullPathName()}));
    const auto state = a.getExtraState();
    EXPECT_FALSE(state.hasProperty(synth::module_file_keys::kInstrumentName));
    const auto first = state[synth::module_file_keys::kInstrumentRegions][0];
    for (const char* key : {"tune", "volume", "loop", "loopstart", "seqlength", "release", "ampeg_release"})
        EXPECT_FALSE(first.hasProperty(key)) << key << " is written only when it is not the default";
    EXPECT_TRUE(first.hasProperty("lokey"));
}

TEST(SamplerMapEdit, APackInstrumentBecomesFilesPointingAtThePacksSamples) {
    Fixture f;
    TempDir packs;
    const auto pack = packs.file("keys");
    pack.createDirectory();
    ASSERT_TRUE(f.dir.file("rich.sfz").copyFileTo(pack.getChildFile("grand.sfz")));
    for (const char* wav : {"soft.wav", "loud.wav", "sine.wav"})
        ASSERT_TRUE(f.dir.file(wav).copyFileTo(pack.getChildFile(wav)));
    ASSERT_TRUE(
        pack.getChildFile("pack.json")
            .replaceWithText(
                R"({"id":"keys","name":"Keys","instruments":[{"id":"grand","name":"Grand Piano","sfz":"grand.sfz"}]})"));
    SoundPackLibrary::setRootsForTests(std::vector<juce::File>{packs.path()});

    Rig fromPack, converted;
    ASSERT_TRUE(fromPack.module.loadInstrumentFromPack("keys", "grand"));
    ASSERT_TRUE(converted.module.loadInstrumentFromPack("keys", "grand"));
    EXPECT_EQ(converted.module.getInstrumentKind(), InstrumentSource::Kind::Pack);
    ASSERT_TRUE(converted.module.editInstrumentRegions(converted.module.getInstrumentRegions()));
    EXPECT_EQ(converted.module.getInstrumentKind(), InstrumentSource::Kind::Files);
    EXPECT_EQ(converted.module.getInstrumentName(), "Grand Piano");

    const auto state = converted.module.getExtraState();
    EXPECT_FALSE(state.hasProperty(synth::module_file_keys::kInstrumentPack));
    EXPECT_EQ(state[synth::module_file_keys::kInstrumentFiles][0].toString(),
              pack.getChildFile("soft.wav").getFullPathName());
    EXPECT_EQ(fromPack.play({60}, 100).left, converted.play({60}, 100).left);
    SoundPackLibrary::setRootsForTests(std::nullopt);
}

TEST(SamplerMapEdit, RestoringTheStateBeforeAnEditUndoesIt) {
    Fixture f;
    SamplerModule module;
    ASSERT_TRUE(module.loadInstrumentFromSfz(f.dir.file("layers.sfz")));
    const auto before = module.getExtraState();
    const auto original = module.getInstrumentRegions();
    auto edited = original;
    edited[0].hiKey = 20;
    ASSERT_TRUE(module.editInstrumentRegions(edited));
    const auto after = module.getExtraState();
    ASSERT_EQ(module.getInstrumentRegions()[0].hiKey, 20);

    module.setExtraState(before);
    expectSameRegions(module.getInstrumentRegions(), original);
    EXPECT_EQ(module.getInstrumentKind(), InstrumentSource::Kind::Sfz);
    module.setExtraState(after);
    EXPECT_EQ(module.getInstrumentRegions()[0].hiKey, 20);
}
