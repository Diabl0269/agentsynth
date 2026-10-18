// SamplerInstrumentModeTests.cpp -- the Sampler's Instrument mode through the module: MIDI playing, velocity layers,
// bypass/mute/mode switches, pedal, CV, saved state for each source, deferred restores and the project bundle.
#include "AI/AIStateMapper/AIStateMapper.h"
#include "MacroSet.h"
#include "MidiRemote/RemoteModel.h"
#include "Modules/ModuleFileStateKeys.h"
#include "Modules/Sampler/SoundPackLibrary.h"
#include "Modules/SamplerModule.h"
#include "PatchDocument.h"
#include "Project/DeferredAssetLoads.h"
#include "ProjectBundle.h"
#include "SamplerTestWavs.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include <cmath>
#include <functional>
#include <gtest/gtest.h>

using namespace sampler_test;

namespace {

constexpr double kRate = 44100.0;
constexpr int kBlock = 256;

void setActual(SamplerModule& module, const juce::String& id, float value) {
    auto* p = dynamic_cast<juce::RangedAudioParameter*>(findParameterByID(&module, id));
    ASSERT_NE(p, nullptr) << id;
    p->setValueNotifyingHost(p->convertTo0to1(value));
}

float actual(SamplerModule& module, const juce::String& id) {
    auto* p = dynamic_cast<juce::RangedAudioParameter*>(findParameterByID(&module, id));
    return p == nullptr ? -999.0f : p->convertFrom0to1(p->getValue());
}

struct Out {
    std::vector<float> left, right;
    float peak(size_t from = 0) const {
        float m = 0.0f;
        for (size_t i = from; i < left.size(); ++i)
            m = std::max({m, std::fabs(left[i]), std::fabs(right[i])});
        return m;
    }
    float tail() const { return left.empty() ? 0.0f : left.back(); }
};

using MidiFor = std::function<void(int block, juce::MidiBuffer&)>;

Out run(SamplerModule& module, int blocks, const MidiFor& midiFor = {}, int cvChannel = -1, float cv = 0.0f) {
    Out out;
    for (int b = 0; b < blocks; ++b) {
        juce::AudioBuffer<float> block(SamplerModule::kNumChannels, kBlock);
        block.clear();
        if (cvChannel >= 0)
            for (int i = 0; i < kBlock; ++i)
                block.setSample(cvChannel, i, cv);
        juce::MidiBuffer midi;
        if (midiFor)
            midiFor(b, midi);
        module.processBlock(block, midi);
        out.left.insert(out.left.end(), block.getReadPointer(0), block.getReadPointer(0) + kBlock);
        out.right.insert(out.right.end(), block.getReadPointer(1), block.getReadPointer(1) + kBlock);
    }
    return out;
}

MidiFor noteOnAt(std::initializer_list<int> notes, int velocity = 100, int block = 0) {
    const std::vector<int> list(notes);
    return [=](int b, juce::MidiBuffer& m) {
        if (b == block)
            for (int n : list)
                m.addEvent(juce::MidiMessage::noteOn(1, n, (juce::uint8)velocity), 0);
    };
}

// A Sampler with the knobs that make levels exact: Level 1, no Feel, an open Tone, a short Release.
struct Rig {
    Rig() {
        setActual(module, "level", 1.0f);
        setActual(module, "feel", 0.0f);
        setActual(module, "release", 0.05f);
        module.prepareToPlay(kRate, kBlock);
    }
    SamplerModule module;
};

// Two DC layers on C3: soft up to velocity 64, loud above; plus a sine sample for pitch checks.
struct Fixture {
    explicit Fixture(float soft = 0.2f, float loud = 0.6f) {
        writeWav(dir.file("soft.wav"), 88200, 1, kRate, [soft](int, int) { return soft; });
        writeWav(dir.file("loud.wav"), 88200, 1, kRate, [loud](int, int) { return loud; });
        writeWav(dir.file("sine.wav"), 88200, 1, kRate,
                 [](int, int i) { return 0.5f * std::sin(0.0628319f * (float)i); });
        dir.file("layers.sfz")
            .replaceWithText("<group> lokey=0 hikey=127 pitch_keycenter=60 loop_mode=loop_continuous\n"
                             "<region> sample=soft.wav lovel=1 hivel=64\n"
                             "<region> sample=loud.wav lovel=65 hivel=127\n");
        dir.file("sine.sfz").replaceWithText("<region> sample=sine.wav key=60 loop_mode=loop_continuous\n");
        dir.file("odd.sfz").replaceWithText("<region> sample=soft.wav key=60 cutoff=800\n");
    }
    TempDir dir;
};

int zeroCrossings(const std::vector<float>& x) {
    int n = 0;
    for (size_t i = 1; i < x.size(); ++i)
        n += (x[i - 1] < 0.0f && x[i] >= 0.0f) ? 1 : 0;
    return n;
}

} // namespace

TEST(SamplerInstrumentMode, TheModeIsAppendedAndTheNewParametersComeLast) {
    SamplerModule module;
    auto* mode = dynamic_cast<juce::AudioParameterChoice*>(findParameterByID(&module, "playMode"));
    ASSERT_NE(mode, nullptr);
    EXPECT_EQ(mode->choices.joinIntoString(","), "Sample,Granular,Instrument");
    juce::StringArray ids;
    for (auto* p : module.getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(p))
            ids.add(withId->paramID);
    ASSERT_GE(ids.size(), 6);
    juce::StringArray lastSix;
    for (int i = ids.size() - 6; i < ids.size(); ++i)
        lastSix.add(ids[i]);
    EXPECT_EQ(lastSix.joinIntoString(","), "attack,release,tone,feel,voices,releaseSound");
    EXPECT_LT(ids.indexOf("reverse"), ids.indexOf("muted"));
    EXPECT_NEAR(actual(module, "attack"), 0.002f, 1.0e-5f);
    EXPECT_NEAR(actual(module, "release"), 0.6f, 1.0e-4f);
    EXPECT_FLOAT_EQ(actual(module, "tone"), 1.0f);
    EXPECT_NEAR(actual(module, "feel"), 0.7f, 1.0e-5f);
    EXPECT_FLOAT_EQ(actual(module, "voices"), 16.0f);
    EXPECT_FLOAT_EQ(actual(module, "releaseSound"), 1.0f);
}

TEST(SamplerInstrumentMode, ALoadSelectsInstrumentModeAndNeverMakesASound) {
    Fixture f;
    Rig rig;
    EXPECT_EQ(rig.module.getInstrument(), nullptr);
    ASSERT_TRUE(rig.module.loadInstrumentFromSfz(f.dir.file("layers.sfz")));
    EXPECT_EQ(rig.module.getInstrumentName(), "layers");
    EXPECT_NE(rig.module.getInstrument(), nullptr);
    EXPECT_EQ(actual(rig.module, "playMode"), (float)SamplerModule::kModeInstrument);
    const int generation = rig.module.getInstrumentGeneration();
    EXPECT_GT(generation, 0);
    EXPECT_EQ(run(rig.module, 4).peak(), 0.0f);

    rig.module.clearInstrument();
    EXPECT_EQ(rig.module.getInstrument(), nullptr);
    EXPECT_GT(rig.module.getInstrumentGeneration(), generation);
    EXPECT_FALSE(rig.module.loadInstrumentFromFiles({}));
    EXPECT_FALSE(rig.module.loadInstrumentFromSfz(f.dir.file("missing.sfz")));
    EXPECT_FALSE(rig.module.getInstrumentMessages().isEmpty());
}

TEST(SamplerInstrumentMode, LoadMessagesCarryParseWarnings) {
    Fixture f;
    Rig rig;
    ASSERT_TRUE(rig.module.loadInstrumentFromSfz(f.dir.file("odd.sfz")));
    const auto messages = rig.module.getInstrumentMessages();
    ASSERT_EQ(messages.size(), 1);
    EXPECT_TRUE(messages[0].startsWith("warning: "));
}

TEST(SamplerInstrumentMode, AChordPlaysThreeVoicesOnBothChannels) {
    Fixture f(0.2f, 0.2f);
    Rig rig;
    ASSERT_TRUE(rig.module.loadInstrumentFromSfz(f.dir.file("layers.sfz")));
    const auto out = run(rig.module, 12, noteOnAt({60, 64, 67}));
    EXPECT_NEAR(out.tail(), 0.6f, 0.01f) << "three voices of the 0.2 layer";
    EXPECT_FLOAT_EQ(out.left.back(), out.right.back());
    EXPECT_EQ(rig.module.getSoundingNotes().count(), 3u);
}

TEST(SamplerInstrumentMode, VelocityPicksTheSoftOrLoudLayer) {
    Fixture f;
    Rig soft, loud;
    ASSERT_TRUE(soft.module.loadInstrumentFromSfz(f.dir.file("layers.sfz")));
    ASSERT_TRUE(loud.module.loadInstrumentFromSfz(f.dir.file("layers.sfz")));
    EXPECT_NEAR(run(soft.module, 12, noteOnAt({60}, 30)).tail(), 0.2f, 0.01f);
    EXPECT_NEAR(run(loud.module, 12, noteOnAt({60}, 110)).tail(), 0.6f, 0.01f);
}

TEST(SamplerInstrumentMode, SampleAndGranularStayAsTheyWereWithAnInstrumentLoaded) {
    Fixture f;
    const auto ramp = f.dir.file("ramp.wav");
    ASSERT_TRUE(writeWav(ramp, 8192, 1, kRate, [](int, int i) { return 0.4f * (float)i / 8191.0f; }));
    for (const float granular : {0.0f, 1.0f}) {
        Rig plain, withInstrument;
        for (auto* rig : {&plain, &withInstrument}) {
            ASSERT_TRUE(rig->module.loadSampleFile(ramp));
            setActual(rig->module, "spray", 0.0f); // no random jitter: both render the same grains
        }
        ASSERT_TRUE(withInstrument.module.loadInstrumentFromSfz(f.dir.file("layers.sfz")));
        for (auto* rig : {&plain, &withInstrument}) {
            setActual(rig->module, "playMode", granular);
            rig->module.prepareToPlay(kRate, kBlock);
        }
        const auto a = run(plain.module, 20);
        const auto b = run(withInstrument.module, 20);
        ASSERT_GT(a.peak(), 0.01f) << "mode " << granular;
        EXPECT_EQ(a.left, b.left) << "mode " << granular;
        EXPECT_EQ(withInstrument.module.getSoundingNotes().count(), 0u);
    }
}

TEST(SamplerInstrumentMode, BypassAndMuteSilenceTheOutputAndStopTheVoices) {
    Fixture f;
    for (const char* param : {"bypassed", "muted"}) {
        Rig rig;
        ASSERT_TRUE(rig.module.loadInstrumentFromSfz(f.dir.file("layers.sfz")));
        EXPECT_GT(run(rig.module, 6, noteOnAt({60})).peak(), 0.1f);
        ASSERT_GT(rig.module.getSoundingNotes().count(), 0u);

        setActual(rig.module, param, 1.0f);
        EXPECT_EQ(run(rig.module, 2).peak(), 0.0f) << param;
        EXPECT_EQ(rig.module.getSoundingNotes().count(), 0u) << param;

        setActual(rig.module, param, 0.0f);
        EXPECT_EQ(run(rig.module, 4).peak(), 0.0f) << param << ": the stopped note does not come back";
    }
}

TEST(SamplerInstrumentMode, LeavingInstrumentModeStopsTheVoices) {
    Fixture f;
    Rig rig;
    ASSERT_TRUE(rig.module.loadInstrumentFromSfz(f.dir.file("layers.sfz")));
    run(rig.module, 6, noteOnAt({60, 64}));
    ASSERT_EQ(rig.module.getSoundingNotes().count(), 2u);
    setActual(rig.module, "playMode", 0.0f);
    run(rig.module, 1);
    EXPECT_EQ(rig.module.getSoundingNotes().count(), 0u);
    setActual(rig.module, "playMode", (float)SamplerModule::kModeInstrument);
    EXPECT_EQ(run(rig.module, 4).peak(), 0.0f) << "back in Instrument mode nothing is left sounding";
}

TEST(SamplerInstrumentMode, TheSustainPedalHoldsReleasedNotesUntilItLifts) {
    Fixture f;
    Rig rig;
    ASSERT_TRUE(rig.module.loadInstrumentFromSfz(f.dir.file("layers.sfz")));
    const auto out = run(rig.module, 40, [](int b, juce::MidiBuffer& m) {
        if (b == 0) {
            m.addEvent(juce::MidiMessage::controllerEvent(1, 64, 127), 0);
            m.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 1);
            m.addEvent(juce::MidiMessage::noteOff(1, 60), 2);
        }
        if (b == 20)
            m.addEvent(juce::MidiMessage::controllerEvent(1, 64, 0), 0);
    });
    const size_t heldEnd = 19 * kBlock;
    EXPECT_NEAR(out.left[heldEnd], 0.6f, 0.01f) << "still sounding after the key came up";
    EXPECT_NEAR(out.tail(), 0.0f, 0.001f) << "released once the pedal lifted";
}

TEST(SamplerInstrumentMode, TheTriggerInputIsIgnored) {
    Fixture f;
    Rig rig;
    ASSERT_TRUE(rig.module.loadInstrumentFromSfz(f.dir.file("layers.sfz")));
    EXPECT_EQ(run(rig.module, 6, {}, SamplerModule::kTriggerCh, 1.0f).peak(), 0.0f);
}

TEST(SamplerInstrumentMode, PitchKnobAndPitchCvTransposeEveryNote) {
    Fixture f;
    auto crossings = [&](float pitch, int cvChannel, float cv) {
        Rig rig;
        EXPECT_TRUE(rig.module.loadInstrumentFromSfz(f.dir.file("sine.sfz")));
        setActual(rig.module, "pitch", pitch);
        return zeroCrossings(run(rig.module, 32, noteOnAt({60}), cvChannel, cv).left);
    };
    const int base = crossings(0.0f, -1, 0.0f);
    EXPECT_NEAR(base, 8192 / 100, 3);
    EXPECT_NEAR(crossings(12.0f, -1, 0.0f), 2 * base, 4);
    EXPECT_NEAR(crossings(0.0f, SamplerModule::kPitchCVCh, 0.5f), 2 * base, 4) << "CV of 0.5 is an octave";
    EXPECT_NEAR(crossings(0.0f, SamplerModule::kFineCVCh, 12.0f), 2 * base, 4)
        << "fine CV is in semitones as in Sample mode";
}

TEST(SamplerInstrumentMode, ToneAndFeelNeverAddLevel) {
    Fixture f;
    float reference = 0.0f;
    {
        Rig rig;
        ASSERT_TRUE(rig.module.loadInstrumentFromSfz(f.dir.file("sine.sfz")));
        setActual(rig.module, "tone", 1.0f);
        reference = run(rig.module, 24, noteOnAt({60}, 127)).peak();
    }
    for (const float tone : {0.0f, 0.5f, 1.0f})
        for (const float feel : {0.0f, 0.7f, 1.0f})
            for (const int velocity : {20, 127}) {
                Rig rig;
                ASSERT_TRUE(rig.module.loadInstrumentFromSfz(f.dir.file("sine.sfz")));
                setActual(rig.module, "tone", tone);
                setActual(rig.module, "feel", feel);
                EXPECT_LE(run(rig.module, 24, noteOnAt({60}, velocity)).peak(), reference + 1.0e-4f)
                    << "tone " << tone << " feel " << feel << " velocity " << velocity;
            }
}

TEST(SamplerInstrumentMode, ALoudSingleNoteIsNotClipped) {
    Fixture f(0.2f, 0.85f);
    Rig rig;
    ASSERT_TRUE(rig.module.loadInstrumentFromSfz(f.dir.file("layers.sfz")));
    EXPECT_NEAR(run(rig.module, 12, noteOnAt({60}, 127)).tail(), 0.85f, 0.005f);
    EXPECT_LT(run(rig.module, 12, noteOnAt({60, 62, 64, 65}, 127)).peak(), 1.0f)
        << "a loud chord is rounded, not clipped";
}

TEST(SamplerInstrumentMode, SoundingNotesFollowTheKeysAndClearAfterTheRelease) {
    Fixture f;
    Rig rig;
    ASSERT_TRUE(rig.module.loadInstrumentFromSfz(f.dir.file("layers.sfz")));
    run(rig.module, 4, noteOnAt({60, 100}));
    auto notes = rig.module.getSoundingNotes();
    EXPECT_TRUE(notes.test(60));
    EXPECT_TRUE(notes.test(100)) << "the upper word of the mask";
    EXPECT_EQ(notes.count(), 2u);
    run(rig.module, 40, [](int b, juce::MidiBuffer& m) {
        if (b == 0) {
            m.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
            m.addEvent(juce::MidiMessage::noteOff(1, 100), 0);
        }
    });
    EXPECT_EQ(rig.module.getSoundingNotes().count(), 0u);
}

namespace {

juce::var stateOf(SamplerModule& module) { return module.getExtraState(); }

void expectSameState(const juce::var& a, const juce::var& b) {
    EXPECT_EQ(juce::JSON::toString(a), juce::JSON::toString(b));
}

} // namespace

TEST(SamplerInstrumentMode, AnSfzIsSavedAsFolderPlusRelativePathAndRestored) {
    Fixture f;
    SamplerModule a;
    ASSERT_TRUE(a.loadInstrumentFromSfz(f.dir.file("layers.sfz")));
    const auto state = stateOf(a);
    EXPECT_EQ(state["instrumentFolder"].toString(), f.dir.path().getFullPathName());
    EXPECT_EQ(state["instrumentSfz"].toString(), "layers.sfz");
    EXPECT_FALSE(state.hasProperty("instrumentPack"));
    EXPECT_FALSE(state.hasProperty("instrumentFiles"));

    SamplerModule b;
    b.setExtraState(state);
    ASSERT_NE(b.getInstrument(), nullptr);
    EXPECT_EQ(b.getInstrumentName(), "layers");
    expectSameState(state, stateOf(b));

    // A path in a subfolder uses '/'.
    ASSERT_TRUE(f.dir.file("sub/inner.sfz").getParentDirectory().createDirectory());
    ASSERT_TRUE(f.dir.file("sub/inner.sfz").replaceWithText("<region> sample=../soft.wav key=60\n"));
    auto* nested = new juce::DynamicObject();
    nested->setProperty(synth::module_file_keys::kInstrumentFolder, f.dir.path().getFullPathName());
    nested->setProperty(synth::module_file_keys::kInstrumentSfz, "sub/inner.sfz");
    SamplerModule c;
    c.setExtraState(juce::var(nested));
    EXPECT_EQ(c.getInstrumentName(), "inner");
    EXPECT_NE(c.getInstrument(), nullptr);
}

TEST(SamplerInstrumentMode, APackInstrumentIsSavedByIdWithNoPathAndRestored) {
    Fixture f;
    TempDir packs;
    const auto pack = packs.file("keys");
    pack.createDirectory();
    ASSERT_TRUE(f.dir.file("layers.sfz").copyFileTo(pack.getChildFile("grand.sfz")));
    for (const char* wav : {"soft.wav", "loud.wav"})
        ASSERT_TRUE(f.dir.file(wav).copyFileTo(pack.getChildFile(wav)));
    ASSERT_TRUE(
        pack.getChildFile("pack.json")
            .replaceWithText(
                R"({"id":"keys","name":"Keys","instruments":[{"id":"grand","name":"Grand Piano","sfz":"grand.sfz"}]})"));
    synth::sampler::SoundPackLibrary::setRootsForTests(std::vector<juce::File>{packs.path()});

    SamplerModule a;
    ASSERT_TRUE(a.loadInstrumentFromPack("keys", "grand"));
    EXPECT_EQ(a.getInstrumentName(), "Grand Piano");
    const auto state = stateOf(a);
    EXPECT_EQ(state["instrumentPack"].toString(), "keys/grand");
    EXPECT_FALSE(state.hasProperty("instrumentFolder")) << "no path is saved for a pack instrument";

    SamplerModule b;
    b.setExtraState(state);
    EXPECT_EQ(b.getInstrumentName(), "Grand Piano");
    EXPECT_NE(b.getInstrument(), nullptr);

    EXPECT_FALSE(a.loadInstrumentFromPack("keys", "nope"));
    EXPECT_NE(a.getInstrument(), nullptr) << "a failed load keeps the loaded instrument";
    EXPECT_TRUE(a.getInstrumentMessages()[0].contains("not found"));
    synth::sampler::SoundPackLibrary::setRootsForTests(std::nullopt);
}

TEST(SamplerInstrumentMode, DroppedFilesKeepTheirPlacement) {
    Fixture f;
    ASSERT_TRUE(writeWav(f.dir.file("Piano_C3.wav"), 4000, 1, kRate, [](int, int) { return 0.3f; }));
    ASSERT_TRUE(writeWav(f.dir.file("Piano_C4.wav"), 4000, 1, kRate, [](int, int) { return 0.3f; }));
    SamplerModule a;
    ASSERT_TRUE(a.loadInstrumentFromFiles(
        {f.dir.file("Piano_C4.wav").getFullPathName(), f.dir.file("Piano_C3.wav").getFullPathName()}));
    EXPECT_EQ(a.getInstrumentName(), "Piano");
    auto state = stateOf(a);
    ASSERT_EQ(state["instrumentFiles"].size(), 2);
    EXPECT_EQ(state["instrumentFiles"][0].toString(), f.dir.file("Piano_C3.wav").getFullPathName()) << "name order";
    ASSERT_EQ(state["instrumentRegions"].size(), 2);

    // The user's own placement (the map editor writes this shape) survives a restore.
    auto* custom = state.getDynamicObject();
    juce::Array<juce::var> regions;
    for (int i = 0; i < 2; ++i) {
        auto* r = new juce::DynamicObject();
        r->setProperty("file", i);
        r->setProperty("lokey", 10 + i * 20);
        r->setProperty("hikey", 29 + i * 20);
        r->setProperty("key", 20 + i * 20);
        r->setProperty("lovel", 1);
        r->setProperty("hivel", 127);
        regions.add(juce::var(r));
    }
    custom->setProperty("instrumentRegions", regions);

    SamplerModule b;
    b.setExtraState(state);
    const auto inst = b.getInstrument();
    ASSERT_NE(inst, nullptr);
    ASSERT_EQ(inst->regions.size(), 2u);
    EXPECT_EQ(inst->regions[0].loKey, 10);
    EXPECT_EQ(inst->regions[0].rootKey, 20);
    EXPECT_EQ(inst->regions[1].hiKey, 49);
    expectSameState(state, stateOf(b));
}

TEST(SamplerInstrumentMode, TheSampleFileAndTheInstrumentAreBothHeld) {
    Fixture f;
    SamplerModule a;
    ASSERT_TRUE(a.loadSampleFile(f.dir.file("soft.wav")));
    ASSERT_TRUE(a.loadInstrumentFromSfz(f.dir.file("layers.sfz")));
    const auto state = stateOf(a);
    EXPECT_EQ(state[synth::module_file_keys::kSampleFile].toString(), f.dir.file("soft.wav").getFullPathName());
    EXPECT_TRUE(state.hasProperty("instrumentSfz"));
    SamplerModule empty;
    EXPECT_TRUE(empty.getExtraState().isVoid());
}

TEST(SamplerInstrumentMode, ARestoreInsideAProjectOpenDecodesLaterAndANewerLoadWins) {
    Fixture f;
    ASSERT_TRUE(writeWav(f.dir.file("other.wav"), 4000, 1, kRate, [](int, int) { return 0.3f; }));
    ASSERT_TRUE(f.dir.file("other.sfz").replaceWithText("<region> sample=other.wav key=60\n"));
    SamplerModule source;
    ASSERT_TRUE(source.loadInstrumentFromSfz(f.dir.file("layers.sfz")));
    const auto state = stateOf(source);

    synth::DeferredAssetLoads loads;
    loads.holdDecodesForTest(true);
    SamplerModule module;
    {
        const synth::DeferredAssetLoads::Scope scope(&loads);
        module.setExtraState(state);
    }
    EXPECT_TRUE(module.isInstrumentLoadPending());
    EXPECT_EQ(module.getInstrument(), nullptr);
    expectSameState(state, stateOf(module)); // a save meanwhile keeps the source
    ASSERT_EQ(loads.pendingOwners().size(), 1u);
    loads.finishAllForTest();
    EXPECT_FALSE(module.isInstrumentLoadPending());
    EXPECT_EQ(module.getInstrumentName(), "layers");

    SamplerModule stale;
    {
        const synth::DeferredAssetLoads::Scope scope(&loads);
        loads.holdDecodesForTest(true);
        stale.setExtraState(state);
    }
    ASSERT_TRUE(stale.loadInstrumentFromSfz(f.dir.file("other.sfz"))); // chosen before the first decode lands
    loads.finishAllForTest();
    EXPECT_EQ(stale.getInstrumentName(), "other");
    EXPECT_FALSE(stale.isInstrumentLoadPending());
}

TEST(SamplerInstrumentMode, AModuleGoneBeforeItsDecodeLandsIsSkipped) {
    Fixture f;
    SamplerModule source;
    ASSERT_TRUE(source.loadInstrumentFromSfz(f.dir.file("layers.sfz")));
    synth::DeferredAssetLoads loads;
    loads.holdDecodesForTest(true);
    {
        SamplerModule module;
        const synth::DeferredAssetLoads::Scope scope(&loads);
        module.setExtraState(stateOf(source));
    }
    loads.finishAllForTest();
    EXPECT_FALSE(loads.hasPending());
}

TEST(SamplerInstrumentMode, TheInstrumentSurvivesASaveAndLoadOfAMovedBundle) {
    Fixture f;
    TempDir root;
    const auto bundle = root.file("Song.agsproj");
    const auto folder = bundle.getChildFile("Instruments/Keys");
    ASSERT_TRUE(folder.createDirectory());
    for (const char* name : {"soft.wav", "loud.wav", "layers.sfz"})
        ASSERT_TRUE(f.dir.file(name).copyFileTo(folder.getChildFile(name)));

    juce::AudioProcessorGraph graph;
    auto* sampler = dynamic_cast<SamplerModule*>(graph.addNode(std::make_unique<SamplerModule>())->getProcessor());
    ASSERT_TRUE(sampler->loadInstrumentFromSfz(folder.getChildFile("layers.sfz")));
    synth::TimelineDoc timeline;
    synth::PatchDocument doc;
    ASSERT_TRUE(
        synth::ProjectBundle::save(bundle, graph, timeline, doc, synth::MacroSet(), synth::MidiRemoteProjectDoc()).ok);

    const auto json = bundle.getChildFile(synth::ProjectBundle::kProjectFileName).loadFileAsString();
    EXPECT_TRUE(json.contains("\"Instruments/Keys\""));
    EXPECT_FALSE(json.contains(bundle.getFullPathName())) << "no absolute path into the bundle is saved";

    const auto moved = root.file("Elsewhere/Renamed.agsproj");
    moved.getParentDirectory().createDirectory();
    ASSERT_TRUE(bundle.moveFileTo(moved));

    juce::AudioProcessorGraph reopened;
    synth::TimelineDoc timeline2;
    synth::PatchDocument doc2;
    synth::MacroSet macros;
    synth::MidiRemoteProjectDoc remote;
    ASSERT_TRUE(synth::ProjectBundle::load(moved, reopened, timeline2, doc2, macros, remote).ok);
    SamplerModule* loaded = nullptr;
    for (auto* node : reopened.getNodes())
        loaded = dynamic_cast<SamplerModule*>(node->getProcessor());
    ASSERT_NE(loaded, nullptr);
    EXPECT_EQ(loaded->getExtraState()["instrumentFolder"].toString(),
              moved.getChildFile("Instruments/Keys").getFullPathName());
    ASSERT_NE(loaded->getInstrument(), nullptr);
    EXPECT_EQ(loaded->getInstrumentName(), "layers");
}

TEST(SamplerInstrumentMode, ThePatchValidatorAcceptsTheNewAndTheOldModeLabels) {
    for (const char* label : {"Sample", "Granular", "Instrument"}) {
        juce::AudioProcessorGraph graph;
        const auto json =
            juce::JSON::parse(juce::String(R"({"nodes":[{"id":1,"type":"Sampler","params":{"playMode":")") + label +
                              R"("}}],"connections":[]})");
        EXPECT_TRUE(synth::AIStateMapper::validatePatch(json, graph, true, false).ok) << label;
    }
}

TEST(SamplerInstrumentMode, ApplyingAGranularPatchStillSelectsGranular) {
    juce::AudioProcessorGraph graph;
    const auto json =
        juce::JSON::parse(R"({"nodes":[{"id":1,"type":"Sampler","params":{"playMode":"Granular"}}],"connections":[]})");
    ASSERT_TRUE(synth::AIStateMapper::applyJSONToGraph(json, graph, true, false));
    SamplerModule* sampler = nullptr;
    for (auto* node : graph.getNodes())
        sampler = dynamic_cast<SamplerModule*>(node->getProcessor());
    ASSERT_NE(sampler, nullptr);
    EXPECT_EQ(actual(*sampler, "playMode"), (float)SamplerModule::kModeGranular);

    const auto instrument = juce::JSON::parse(
        R"({"nodes":[{"id":1,"type":"Sampler","params":{"playMode":"Instrument"}}],"connections":[]})");
    ASSERT_TRUE(synth::AIStateMapper::applyJSONToGraph(instrument, graph, true, false));
    for (auto* node : graph.getNodes())
        sampler = dynamic_cast<SamplerModule*>(node->getProcessor());
    EXPECT_EQ(actual(*sampler, "playMode"), (float)SamplerModule::kModeInstrument);
}

TEST(SamplerInstrumentMode, TheInstrumentCvInputsAreAppendedAfterFineAndReachTheirKnobs) {
    SamplerModule module;
    EXPECT_EQ(SamplerModule::kNumChannels, 15);
    const std::vector<std::pair<juce::String, int>> expected = {{"attack", SamplerModule::kAttackCVCh},
                                                                {"release", SamplerModule::kReleaseCVCh},
                                                                {"tone", SamplerModule::kToneCVCh},
                                                                {"feel", SamplerModule::kFeelCVCh},
                                                                {"voices", SamplerModule::kVoicesCVCh}};
    EXPECT_EQ(SamplerModule::kAttackCVCh, SamplerModule::kFineCVCh + 1);
    const auto targets = module.getModulationTargets();
    for (const auto& [paramId, channel] : expected) {
        const auto found = std::find_if(targets.begin(), targets.end(),
                                        [channel = channel](const auto& t) { return t.channelIndex == channel; });
        ASSERT_NE(found, targets.end()) << paramId;
        EXPECT_EQ(found->paramId, paramId);
        EXPECT_TRUE(found->knobOnly) << "the knobs show in Instrument mode only, so no gutter jack";
        EXPECT_EQ(module.mapInputChannel(channel).visibleJackIndex, channel);
        EXPECT_EQ(module.getInputPortLabel(channel).toLowerCase(), paramId);
    }
}

TEST(SamplerInstrumentMode, ToneCvReadsOverTheKnobSpan) {
    Fixture f;
    const auto peakWith = [&](float cv) {
        Rig rig;
        EXPECT_TRUE(rig.module.loadInstrumentFromSfz(f.dir.file("sine.sfz")));
        setActual(rig.module, "tone", 1.0f);
        return run(rig.module, 24, noteOnAt({60}), SamplerModule::kToneCVCh, cv).peak(8 * kBlock);
    };
    EXPECT_LT(peakWith(-1.0f), 0.8f * peakWith(0.0f)) << "Tone CV of -1 closes the filter from fully open";
}

TEST(SamplerInstrumentMode, VoicesCvLimitsTheNotesThatSound) {
    Fixture f;
    const auto sounding = [&](float cv) {
        Rig rig;
        EXPECT_TRUE(rig.module.loadInstrumentFromSfz(f.dir.file("layers.sfz")));
        setActual(rig.module, "voices", 16.0f);
        run(rig.module, 2, noteOnAt({60, 62, 64}), SamplerModule::kVoicesCVCh, cv);
        return rig.module.getSoundingNotes().count();
    };
    EXPECT_EQ(sounding(0.0f), 3u);
    EXPECT_EQ(sounding(-1.0f), 1u) << "Voices CV of -1 takes the cap from 16 down to 1";
}
