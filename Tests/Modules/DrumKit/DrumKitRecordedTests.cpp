// DrumKitRecordedTests.cpp
// The Drum Kit playing a recorded kit: no kit changes nothing, the notes a kit has regions for play recordings and the
// rest stay synth, Recorded off falls back to synth, velocity picks the layer, a closed hat chokes a recorded open
// hat, Level and Tune reach the recordings, and every kind of source is saved and restored (a missing pack included).
// Kits are tiny constant-level WAVs written to a temp folder, so a level read off the output is exact.

#include "../Sampler/SamplerTestWavs.h"
#include "AI/AIStateMapper/AIStateMapper.h"
#include "Modules/DrumKit/DrumKitModule.h"
#include "Modules/Sampler/SamplerInstrumentPlayer.h"
#include "Modules/Sampler/SoundPackLibrary.h"
#include "Project/DeferredAssetLoads.h"
#include <algorithm>
#include <cmath>
#include <gtest/gtest.h>

using namespace sampler_test;

namespace {

constexpr double kRate = 44100.0;
constexpr int kBlock = 256;
constexpr int kKick = 36;
constexpr int kSnare = 38;
constexpr int kClosedHat = 42;
constexpr int kOpenHat = 46;
constexpr int kCrash = 49;

struct Hit {
    int note;
    float velocity = 1.0f;
    int atSample = 0;
};

struct Out {
    std::vector<float> left, right;
    // The mean of the output between two moments, to read a constant-level recording off.
    float meanBetween(double fromSec, double toSec) const {
        const auto a = (size_t)(fromSec * kRate);
        const auto b = std::min(left.size(), (size_t)(toSec * kRate));
        double sum = 0.0;
        for (size_t i = a; i < b; ++i)
            sum += left[i];
        return b > a ? (float)(sum / (double)(b - a)) : 0.0f;
    }
};

Out render(DrumKitModule& module, const std::vector<Hit>& hits, double seconds) {
    Out out;
    const int total = (int)(seconds * kRate);
    for (int start = 0; start < total; start += kBlock) {
        const int n = std::min(kBlock, total - start);
        juce::AudioBuffer<float> block(2, n);
        block.clear();
        juce::MidiBuffer midi;
        for (const auto& hit : hits)
            if (hit.atSample >= start && hit.atSample < start + n)
                midi.addEvent(juce::MidiMessage::noteOn(10, hit.note, hit.velocity), hit.atSample - start);
        module.processBlock(block, midi);
        out.left.insert(out.left.end(), block.getReadPointer(0), block.getReadPointer(0) + n);
        out.right.insert(out.right.end(), block.getReadPointer(1), block.getReadPointer(1) + n);
    }
    return out;
}

void setActual(DrumKitModule& module, const juce::String& id, float value) {
    auto* p = dynamic_cast<juce::RangedAudioParameter*>(findParameterByID(&module, id));
    ASSERT_NE(p, nullptr) << id;
    p->setValueNotifyingHost(p->convertTo0to1(value));
}

// A kit of constant-level recordings: kick on 35-36 (0.5), snare on 38 as a soft (0.2) and a loud (0.6) layer, a
// closed hat on 42-44 (0.3) and a long open hat on 46 (0.4). Nothing for the toms, the clap or the cymbals.
struct KitFiles {
    KitFiles() {
        const auto dc = [this](const char* name, float level, int frames) {
            writeWav(dir.file(name), frames, 1, kRate, [level](int, int) { return level; });
        };
        dc("kick.wav", 0.5f, 88200);
        dc("snareSoft.wav", 0.2f, 88200);
        dc("snareLoud.wav", 0.6f, 88200);
        dc("hatClosed.wav", 0.3f, 88200);
        dc("hatOpen.wav", 0.4f, 88200);
        dir.file("kit.sfz").replaceWithText("<group> loop_mode=one_shot\n"
                                            "<region> sample=kick.wav lokey=35 hikey=36 pitch_keycenter=36\n"
                                            "<region> sample=snareSoft.wav key=38 lovel=1 hivel=64\n"
                                            "<region> sample=snareLoud.wav key=38 lovel=65 hivel=127\n"
                                            "<region> sample=hatClosed.wav lokey=42 hikey=44 pitch_keycenter=42\n"
                                            "<region> sample=hatOpen.wav key=46\n");
    }
    juce::File sfz() const { return dir.file("kit.sfz"); }
    TempDir dir;
};

std::unique_ptr<DrumKitModule> makeKit() {
    auto kit = std::make_unique<DrumKitModule>();
    kit->prepareToPlay(kRate, kBlock);
    return kit;
}

std::unique_ptr<DrumKitModule> makeKitWith(const KitFiles& files) {
    auto kit = makeKit();
    EXPECT_TRUE(kit->loadInstrumentFromSfz(files.sfz()));
    return kit;
}

float peakOf(const Out& out, double fromSec = 0.0, double toSec = 1.0e9) {
    float peak = 0.0f;
    const auto b = std::min(out.left.size(), (size_t)(toSec * kRate));
    for (size_t i = (size_t)(fromSec * kRate); i < b; ++i)
        peak = std::max(peak, std::fabs(out.left[i]));
    return peak;
}

// What a recording of constant `level` reads at the output with the default Level knobs: group level and the master
// Level (a full-velocity hit loses nothing to Feel, and drums get no voice-sum headroom).
float expected(float level, float groupLevel, float master = 0.7f) { return level * groupLevel * master; }

juce::var stateOf(DrumKitModule& module) { return module.getExtraState(); }

} // namespace

TEST(DrumKitRecorded, TheRecordedSwitchesAreAppendedAfterEveryOlderParameterAndDefaultOn) {
    DrumKitModule kit;
    const auto& params = kit.getParameters();
    ASSERT_GE(params.size(), 7);
    const char* ids[] = {"kickRecorded", "snareRecorded",  "clapRecorded",   "hatRecorded",
                         "tomRecorded",  "cymbalRecorded", "cowbellRecorded"};
    int previous = -1;
    for (const char* id : ids) {
        auto* p = dynamic_cast<juce::AudioParameterBool*>(findParameterByID(&kit, id));
        ASSERT_NE(p, nullptr) << id;
        EXPECT_TRUE(p->get()) << id << " defaults on";
        EXPECT_GT(params.indexOf(p), previous) << "in declaration order";
        previous = params.indexOf(p);
    }
    EXPECT_GT(params.indexOf(findParameterByID(&kit, "kickRecorded")),
              params.indexOf(findParameterByID(&kit, "level")));
    EXPECT_EQ(DrumKitModule::kNumChannels, 2) << "a switch has no CV jack";
}

TEST(DrumKitRecorded, WithNoKitTheOutputIsTheSynthDrumsExactlyAsBefore) {
    const std::vector<Hit> hits = {{kKick, 1.0f, 0},       {kSnare, 0.8f, 3000},  {kClosedHat, 0.7f, 6000},
                                   {kOpenHat, 1.0f, 8000}, {kCrash, 0.9f, 10000}, {45, 1.0f, 12000}};
    auto plain = makeKit();
    const auto reference = render(*plain, hits, 1.0);
    ASSERT_GT(peakOf(reference), 0.05f);

    auto cleared = makeKit();
    KitFiles files;
    ASSERT_TRUE(cleared->loadInstrumentFromSfz(files.sfz()));
    cleared->clearInstrument();
    const auto afterClear = render(*cleared, hits, 1.0);
    EXPECT_EQ(afterClear.left, reference.left) << "clearing the kit gives back the synth output bit for bit";
    EXPECT_EQ(afterClear.right, reference.right);
    EXPECT_EQ(reference.left, reference.right) << "the synth drums stay mono-summed onto both sides";
    EXPECT_TRUE(stateOf(*plain).isVoid()) << "a kit with no recordings saves no state";
}

TEST(DrumKitRecorded, NotesTheKitHasRegionsForPlayRecordingsAndTheRestStaySynth) {
    KitFiles files;
    auto kit = makeKitWith(files);
    const auto kick = render(*kit, {{kKick}}, 0.6);
    EXPECT_NEAR(kick.meanBetween(0.2, 0.5), expected(0.5f, 0.9f), 2.0e-4f) << "the kick plays its recording";
    EXPECT_EQ(kick.left, kick.right) << "a mono recording lands on both sides";

    // The crash has no region: it is the synth crash, whose output is not a constant level and equals the module
    // without any kit.
    auto bare = makeKit();
    const auto synthCrash = render(*bare, {{kCrash}}, 0.5);
    auto kitCrash = makeKitWith(files);
    EXPECT_EQ(render(*kitCrash, {{kCrash}}, 0.5).left, synthCrash.left);

    // A snare and a crash together: the recording and the synth voice add.
    auto mixed = makeKitWith(files);
    const auto both = render(*mixed, {{kSnare}, {kCrash}}, 0.5);
    EXPECT_GT(peakOf(both), peakOf(synthCrash)) << "the snare recording sits on top of the synth crash";
}

TEST(DrumKitRecorded, RecordedOffFallsBackToTheSynthVoiceForThatGroupOnly) {
    KitFiles files;
    auto kit = makeKitWith(files);
    setActual(*kit, "kickRecorded", 0.0f);
    auto bare = makeKit();
    EXPECT_EQ(render(*kit, {{kKick}}, 0.5).left, render(*bare, {{kKick}}, 0.5).left);
    EXPECT_FALSE(kit->playsRecording(kKick));
    EXPECT_TRUE(kit->playsRecording(kSnare)) << "the other groups keep their recordings";

    auto snare = makeKitWith(files);
    EXPECT_NEAR(render(*snare, {{kSnare}}, 0.5).meanBetween(0.2, 0.4), expected(0.6f, 0.8f), 2.0e-4f);
}

TEST(DrumKitRecorded, AllSwitchesOffIsBitIdenticalToNoKit) {
    KitFiles files;
    auto kit = makeKitWith(files);
    for (const char* id : {"kickRecorded", "snareRecorded", "clapRecorded", "hatRecorded", "tomRecorded",
                           "cymbalRecorded", "cowbellRecorded"})
        setActual(*kit, id, 0.0f);
    auto bare = makeKit();
    const std::vector<Hit> hits = {{kKick}, {kSnare, 1.0f, 2000}, {kClosedHat, 1.0f, 4000}, {kOpenHat, 1.0f, 6000}};
    EXPECT_EQ(render(*kit, hits, 0.8).left, render(*bare, hits, 0.8).left);
}

TEST(DrumKitRecorded, VelocityPicksTheLayer) {
    KitFiles files;
    auto soft = makeKitWith(files);
    auto loud = makeKitWith(files);
    // Velocity 40/127 lies in the soft layer (0.2), 100/127 in the loud one (0.6); Feel shapes both by the same rule.
    const float softVel = 40.0f / 127.0f, loudVel = 100.0f / 127.0f;
    const auto feel = [](float v) { return 0.5f + 0.5f * std::pow(v, 1.6f); };
    EXPECT_NEAR(render(*soft, {{kSnare, softVel}}, 0.5).meanBetween(0.2, 0.4), expected(0.2f, 0.8f) * feel(softVel),
                2.0e-4f);
    EXPECT_NEAR(render(*loud, {{kSnare, loudVel}}, 0.5).meanBetween(0.2, 0.4), expected(0.6f, 0.8f) * feel(loudVel),
                2.0e-4f);
}

TEST(DrumKitRecorded, ALouderHitOfTheSameLayerIsLouder) {
    KitFiles files;
    auto quiet = makeKitWith(files);
    auto full = makeKitWith(files);
    const float a = render(*quiet, {{kKick, 0.4f}}, 0.5).meanBetween(0.2, 0.4);
    const float b = render(*full, {{kKick, 1.0f}}, 0.5).meanBetween(0.2, 0.4);
    EXPECT_GT(b, a * 1.2f);
}

TEST(DrumKitRecorded, AClosedHatChokesARecordedOpenHatWithoutAClick) {
    KitFiles files;
    auto open = makeKitWith(files);
    const auto ringing = render(*open, {{kOpenHat}}, 1.0);
    EXPECT_NEAR(ringing.meanBetween(0.6, 0.9), expected(0.4f, 0.6f), 2.0e-4f) << "the open hat rings on its own";

    auto choked = makeKitWith(files);
    const int at = (int)(0.3 * kRate);
    const auto out = render(*choked, {{kOpenHat, 1.0f, 0}, {kClosedHat, 1.0f, at}}, 1.0);
    // After the closed hat's own 2 s recording is excluded by muting it: compare against the closed hat alone.
    auto closedAlone = makeKitWith(files);
    const auto closed = render(*closedAlone, {{kClosedHat, 1.0f, at}}, 1.0);
    float worstStep = 0.0f;
    for (size_t i = (size_t)at; i + 1 < (size_t)at + 2000; ++i)
        worstStep =
            std::max(worstStep, std::fabs((out.left[i + 1] - out.left[i]) - (closed.left[i + 1] - closed.left[i])));
    EXPECT_LT(worstStep, 0.004f) << "the open hat fades over a few hundred samples, it does not stop in one";
    // Once the fade is over only the closed hat is left.
    for (size_t i = (size_t)at + 2000; i < out.left.size(); i += 997)
        EXPECT_NEAR(out.left[i], closed.left[i], 1.0e-4f);

    // A pedal hat (44) chokes it too.
    auto pedal = makeKitWith(files);
    const auto pedalOut = render(*pedal, {{kOpenHat, 1.0f, 0}, {44, 1.0f, at}}, 0.8);
    EXPECT_NEAR(pedalOut.meanBetween(0.6, 0.7), expected(0.3f, 0.6f) * 0.8f, 3.0e-4f)
        << "only the pedal hat is left, a little softer";
}

TEST(DrumKitRecorded, AClosedHatAlsoChokesASynthOpenHatAndAnOpenHatNeverChokesTheClosedOne) {
    KitFiles files;
    auto kit = makeKitWith(files);
    setActual(*kit, "hatRecorded", 0.0f);
    auto bare = makeKit();
    const std::vector<Hit> hits = {{kOpenHat, 1.0f, 0}, {kClosedHat, 1.0f, 4000}};
    EXPECT_EQ(render(*kit, hits, 0.5).left, render(*bare, hits, 0.5).left);

    auto reversed = makeKitWith(files);
    const auto out = render(*reversed, {{kClosedHat, 1.0f, 0}, {kOpenHat, 1.0f, 4000}}, 0.6);
    EXPECT_NEAR(out.meanBetween(0.4, 0.5), expected(0.3f, 0.6f) + expected(0.4f, 0.6f), 4.0e-4f)
        << "both ring: the closed hat is not cut by the open one";
}

TEST(DrumKitRecorded, GroupLevelScalesARecordingAndZeroSilencesIt) {
    KitFiles files;
    auto half = makeKitWith(files);
    setActual(*half, "kickLevel", 0.45f);
    EXPECT_NEAR(render(*half, {{kKick}}, 0.5).meanBetween(0.2, 0.4), expected(0.5f, 0.45f), 2.0e-4f);
    auto zero = makeKitWith(files);
    setActual(*zero, "kickLevel", 0.0f);
    EXPECT_LT(peakOf(render(*zero, {{kKick}}, 0.5)), 1.0e-6f);
}

TEST(DrumKitRecorded, GroupTuneRepitchesARecordingBySemitones) {
    KitFiles files;
    // A 100 Hz sine recording on the kick: +12 semitones doubles the frequency.
    writeWav(files.dir.file("kick.wav"), 44100, 1, kRate, [](int, int i) {
        return 0.5f * std::sin(juce::MathConstants<float>::twoPi * 100.0f * (float)i / 44100.0f);
    });
    const auto crossings = [&](float tune) {
        auto kit = makeKitWith(files);
        setActual(*kit, "kickTune", tune);
        const auto out = render(*kit, {{kKick}}, 0.5);
        int n = 0;
        for (size_t i = 2000; i + 1 < out.left.size(); ++i)
            n += (out.left[i] < 0.0f && out.left[i + 1] >= 0.0f) ? 1 : 0;
        return n;
    };
    const int base = crossings(0.0f);
    EXPECT_GT(base, 10);
    EXPECT_NEAR((double)crossings(12.0f) / (double)base, 2.0, 0.2);
    EXPECT_NEAR((double)crossings(-12.0f) / (double)base, 0.5, 0.1);
}

TEST(DrumKitRecorded, GroupDecayDoesNotChangeARecording) {
    KitFiles files;
    auto a = makeKitWith(files);
    auto b = makeKitWith(files);
    setActual(*b, "kickDecay", 1.2f);
    EXPECT_EQ(render(*a, {{kKick}}, 0.5).left, render(*b, {{kKick}}, 0.5).left);
    EXPECT_TRUE(a->decayIsMoot("closedDecay")) << "every note the closed decay shapes plays a recording";
    EXPECT_TRUE(a->decayIsMoot("kickDecay"));
    EXPECT_FALSE(a->decayIsMoot("tomDecay")) << "no toms in the kit";
    EXPECT_FALSE(a->decayIsMoot("cymbalDecay"));
    setActual(*a, "kickRecorded", 0.0f);
    EXPECT_FALSE(a->decayIsMoot("kickDecay")) << "the synth kick uses it again";
    DrumKitModule none;
    EXPECT_FALSE(none.decayIsMoot("kickDecay"));
}

TEST(DrumKitRecorded, AHitLeavesTheMasterLevelLive) {
    KitFiles files;
    auto kit = makeKitWith(files);
    setActual(*kit, "level", 0.35f);
    kit->prepareToPlay(kRate, kBlock);
    EXPECT_NEAR(render(*kit, {{kKick}}, 0.5).meanBetween(0.2, 0.4), expected(0.5f, 0.9f, 0.35f), 2.0e-4f);
}

TEST(DrumKitRecorded, TheVoiceCountIsTheRecordedVoicesSoundingAtTheEndOfTheBlock) {
    KitFiles files;
    auto kit = makeKitWith(files);
    EXPECT_EQ(kit->displayVoiceCount(), 0);
    render(*kit, {{kKick}, {kSnare, 1.0f, 100}, {kCrash, 1.0f, 100}}, 0.1);
    EXPECT_EQ(kit->displayVoiceCount(), 2) << "the synth crash is not a recorded voice";
    render(*kit, {}, 2.5);
    EXPECT_EQ(kit->displayVoiceCount(), 0);
    auto* source = dynamic_cast<synth::VoiceCountSource*>(kit.get());
    ASSERT_NE(source, nullptr);
}

TEST(DrumKitRecorded, BypassAndMuteSilenceTheRecordingsAndStopTheirVoices) {
    KitFiles files;
    for (const bool bypass : {true, false}) {
        auto kit = makeKitWith(files);
        render(*kit, {{kKick}}, 0.05);
        EXPECT_EQ(kit->displayVoiceCount(), 1);
        if (bypass)
            kit->setBypassed(true);
        else
            kit->setMuted(true);
        const auto silent = render(*kit, {}, 0.1);
        EXPECT_LT(peakOf(silent), 1.0e-9f);
        EXPECT_EQ(kit->displayVoiceCount(), 0);
        if (bypass)
            kit->setBypassed(false);
        else
            kit->setMuted(false);
        EXPECT_LT(peakOf(render(*kit, {}, 0.1)), 1.0e-9f) << "the cut voices do not come back";
        EXPECT_GT(peakOf(render(*kit, {{kKick}}, 0.2)), 0.01f) << "and the kit plays again";
    }
}

TEST(DrumKitRecorded, LoadingNeverMakesASoundAndAReplacedKitIsSilencedCleanly) {
    KitFiles files;
    auto kit = makeKit();
    ASSERT_TRUE(kit->loadInstrumentFromSfz(files.sfz()));
    EXPECT_LT(peakOf(render(*kit, {}, 0.2)), 1.0e-9f);
    render(*kit, {{kKick}}, 0.05);
    ASSERT_TRUE(kit->loadInstrumentFromSfz(files.sfz()));
    EXPECT_LT(peakOf(render(*kit, {}, 0.1)), 1.0e-9f) << "a new kit stops the old kit's voices";
}

// ---- Saving ----

TEST(DrumKitRecorded, AnSfzKitIsSavedAsFolderPlusRelativePathAndRestored) {
    KitFiles files;
    auto a = makeKitWith(files);
    const auto state = stateOf(*a);
    EXPECT_EQ(state["instrumentFolder"].toString(), files.dir.path().getFullPathName());
    EXPECT_EQ(state["instrumentSfz"].toString(), "kit.sfz");

    auto b = makeKit();
    b->setExtraState(state);
    EXPECT_EQ(b->getInstrumentKind(), synth::sampler::InstrumentSource::Kind::Sfz);
    EXPECT_NEAR(render(*b, {{kKick}}, 0.5).meanBetween(0.2, 0.4), expected(0.5f, 0.9f), 2.0e-4f);
    EXPECT_EQ(juce::JSON::toString(stateOf(*b)), juce::JSON::toString(state));
}

TEST(DrumKitRecorded, APackKitIsSavedByIdAndAMissingPackKeepsItsReference) {
    KitFiles files;
    TempDir packs;
    const auto pack = packs.file("core");
    pack.createDirectory();
    for (const char* name : {"kick.wav", "snareSoft.wav", "snareLoud.wav", "hatClosed.wav", "hatOpen.wav", "kit.sfz"})
        ASSERT_TRUE(files.dir.file(name).copyFileTo(pack.getChildFile(name)));
    ASSERT_TRUE(pack.getChildFile("pack.json")
                    .replaceWithText(R"({"id":"core","name":"Core","instruments":[{"id":"acoustic-kit",)"
                                     R"("name":"Acoustic Kit","sfz":"kit.sfz","category":"Drums"}]})"));
    synth::sampler::SoundPackLibrary::setRootsForTests(std::vector<juce::File>{packs.path()});

    auto a = makeKit();
    ASSERT_TRUE(a->loadInstrumentFromPack("core", "acoustic-kit"));
    const auto state = stateOf(*a);
    EXPECT_EQ(state["instrumentPack"].toString(), "core/acoustic-kit");
    EXPECT_FALSE(state.hasProperty("instrumentFolder")) << "no path is saved for a pack";
    auto b = makeKit();
    b->setExtraState(state);
    EXPECT_EQ(b->getInstrumentName(), "Acoustic Kit");
    EXPECT_NEAR(render(*b, {{kKick}}, 0.5).meanBetween(0.2, 0.4), expected(0.5f, 0.9f), 2.0e-4f);

    synth::sampler::SoundPackLibrary::setRootsForTests(std::vector<juce::File>{TempDir().path()});
    auto missing = makeKit();
    missing->setExtraState(state);
    EXPECT_FALSE(missing->hasRecordedKit());
    ASSERT_FALSE(missing->getInstrumentMessages().isEmpty());
    EXPECT_TRUE(missing->getInstrumentMessages()[0].contains("not found"));
    EXPECT_EQ(stateOf(*missing)["instrumentPack"].toString(), "core/acoustic-kit") << "a save keeps the reference";
    auto bare = makeKit();
    EXPECT_EQ(render(*missing, {{kKick}}, 0.5).left, render(*bare, {{kKick}}, 0.5).left) << "the synth drums play";
    synth::sampler::SoundPackLibrary::setRootsForTests(std::nullopt);
}

TEST(DrumKitRecorded, TheUsersFilesAreSavedWithTheirPlacementAndRestored) {
    KitFiles files;
    writeWav(files.dir.file("Kick_C1.wav"), 88200, 1, kRate, [](int, int) { return 0.5f; });
    writeWav(files.dir.file("Snare_D1.wav"), 88200, 1, kRate, [](int, int) { return 0.4f; });
    auto a = makeKit();
    ASSERT_TRUE(a->loadInstrumentFromFiles(
        {files.dir.file("Kick_C1.wav").getFullPathName(), files.dir.file("Snare_D1.wav").getFullPathName()}));
    const auto state = stateOf(*a);
    ASSERT_EQ(state["instrumentFiles"].size(), 2);
    ASSERT_EQ(state["instrumentRegions"].size(), 2);

    auto b = makeKit();
    b->setExtraState(state);
    EXPECT_EQ(b->getInstrumentKind(), synth::sampler::InstrumentSource::Kind::Files);
    EXPECT_EQ(juce::JSON::toString(stateOf(*b)), juce::JSON::toString(state));
    EXPECT_GT(peakOf(render(*b, {{kKick}}, 0.3)), 0.01f);
}

TEST(DrumKitRecorded, ARestoreInsideAProjectOpenDecodesLaterAndASaveMeanwhileKeepsTheSource) {
    KitFiles files;
    auto source = makeKitWith(files);
    const auto state = stateOf(*source);
    synth::DeferredAssetLoads loads;
    loads.holdDecodesForTest(true);
    auto kit = makeKit();
    {
        const synth::DeferredAssetLoads::Scope scope(&loads);
        kit->setExtraState(state);
    }
    EXPECT_TRUE(kit->isInstrumentLoadPending());
    EXPECT_EQ(juce::JSON::toString(stateOf(*kit)), juce::JSON::toString(state));
    loads.finishAllForTest();
    EXPECT_FALSE(kit->isInstrumentLoadPending());
    EXPECT_TRUE(kit->hasRecordedKit());
}

TEST(DrumKitRecorded, ASavedStateWithNoKitClearsTheKitAndTheSavedKitSurvivesThePatchFormat) {
    KitFiles files;
    auto kit = makeKitWith(files);
    kit->setExtraState(juce::var());
    EXPECT_FALSE(kit->hasRecordedKit());
    EXPECT_TRUE(stateOf(*kit).isVoid());

    juce::AudioProcessorGraph graph;
    auto* node = graph.addNode(std::make_unique<DrumKitModule>()).get();
    auto* module = dynamic_cast<DrumKitModule*>(node->getProcessor());
    ASSERT_TRUE(module->loadInstrumentFromSfz(files.sfz()));
    const auto json = synth::AIStateMapper::graphToJSON(graph);
    juce::AudioProcessorGraph reopened;
    ASSERT_TRUE(synth::AIStateMapper::applyJSONToGraph(json, reopened, true, /*trusted=*/true));
    DrumKitModule* loaded = nullptr;
    for (auto* n : reopened.getNodes())
        if (auto* k = dynamic_cast<DrumKitModule*>(n->getProcessor()))
            loaded = k;
    ASSERT_NE(loaded, nullptr);
    EXPECT_TRUE(loaded->hasRecordedKit());
}
