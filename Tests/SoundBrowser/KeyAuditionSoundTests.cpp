// The default sound the key pickers play a scale with: the rendered run (pure), its way through the sound preview
// (lock-free audio thread, after the master), and what a user hears on an empty project: a quiet scale, and nothing of
// it in a bounce.
#include "../Modules/Filter/AllocationCounter.h"
#include "../TestWait.h"
#include "AudioEngine/AudioEngine.h"
#include "SoundBrowser/KeyAuditionSound.h"
#include "SoundBrowser/SoundPreviewPlayer.h"
#include "Transport/BounceGuards.h"
#include "Transport/OfflineTransportDriver.h"
#include "UI/Layout/KeyPicker/KeyPicker.h"
#include "UI/Timeline/SongKeyField.h"
#include <cmath>
#include <gtest/gtest.h>

using synth::soundbrowser::renderKeyAuditionSound;
using synth::ui::KeyPicker;
using synth::ui::SongKeyField;

namespace {

constexpr double kRate = 48000.0;
constexpr int kBlock = 512;
const std::vector<int> kMajor{60, 62, 64, 65, 67, 69, 71, 72};

// The preview's own default gain is -6 dB; the whole run must stay far below full scale after it.
constexpr float kQuietBound = 0.15f;

struct Fixture {
    AudioEngine engine{AudioEngine::HostMode::Hosted};
    std::unique_ptr<synth::OfflineTransportDriver> driver;
    Fixture() {
        engine.initialise();
        engine.getGraph().clear(); // an empty project: no tracks, no instrument
        driver = std::make_unique<synth::OfflineTransportDriver>(engine, kRate, kBlock, 2);
    }
    ~Fixture() {
        engine.releaseFromHost();
        engine.shutdown();
    }
    /** A key field wired the way the app wires it: the default sound goes through the engine's sound preview. */
    void wire(SongKeyField& field) {
        auto& preview = engine.getSoundPreview();
        field.getAuditioner().playDefaultSound = [&preview](const std::vector<int>& pitches) {
            synth::soundbrowser::playKeyAuditionSound(preview, pitches);
        };
        field.getAuditioner().stopDefaultSound = [&preview] { preview.stop(); };
    }
    float renderPeak(int blocks) {
        const auto audio = driver->renderBlocks(blocks);
        return audio.getMagnitude(0, audio.getNumSamples());
    }
};

} // namespace

TEST(KeyAuditionSound, TheRunIsStereoOneStepPerNoteAndRingsOutAfterTheLastOne) {
    const auto audio = renderKeyAuditionSound(kMajor, kRate);
    ASSERT_EQ(audio.getNumChannels(), 2);
    const int step = (int)std::llround(synth::soundbrowser::kKeyAuditionStepMs / 1000.0 * kRate);
    EXPECT_GT(audio.getNumSamples(), step * (int)kMajor.size()) << "the last note has room to ring";
    EXPECT_LE(audio.getNumSamples(), step * ((int)kMajor.size() + 3));
    for (int i = 0; i < audio.getNumSamples(); i += 97)
        EXPECT_EQ(audio.getSample(0, i), audio.getSample(1, i));
    EXPECT_TRUE(renderKeyAuditionSound({}, kRate).getNumSamples() == 0);
    EXPECT_TRUE(renderKeyAuditionSound(kMajor, 0.0).getNumSamples() == 0);
}

TEST(KeyAuditionSound, EveryNoteSoundsAtItsOwnPitchAndTheRunHasNoClickAtEitherEnd) {
    const auto audio = renderKeyAuditionSound(kMajor, kRate);
    const int n = audio.getNumSamples();
    EXPECT_EQ(audio.getSample(0, 0), 0.0f);
    EXPECT_LT(std::abs(audio.getSample(0, n - 1)), 1.0e-4f);
    float maxStep = 0.0f;
    for (int i = 1; i < n; ++i)
        maxStep = std::max(maxStep, std::abs(audio.getSample(0, i) - audio.getSample(0, i - 1)));
    EXPECT_LT(maxStep, 0.05f) << "no sample-to-sample jump a click would make, including where one note hands over";
    EXPECT_LT(audio.getMagnitude(0, n), 0.25f) << "a quiet sound before the preview's own gain";
    EXPECT_GT(audio.getMagnitude(0, n), 0.1f);
    // Each slot carries energy, so every pitch of the run is heard.
    const int step = (int)std::llround(synth::soundbrowser::kKeyAuditionStepMs / 1000.0 * kRate);
    for (size_t i = 0; i < kMajor.size(); ++i)
        EXPECT_GT(audio.getMagnitude(0, (int)i * step, step), 0.05f) << "note " << i;
}

TEST(KeyAuditionSound, TheSameInputGivesTheSameSamples) {
    const auto a = renderKeyAuditionSound(kMajor, kRate);
    const auto b = renderKeyAuditionSound(kMajor, kRate);
    ASSERT_EQ(a.getNumSamples(), b.getNumSamples());
    for (int i = 0; i < a.getNumSamples(); ++i)
        ASSERT_EQ(a.getSample(0, i), b.getSample(0, i));
}

TEST(KeyAuditionSound, PlayRenderedAllocatesNothingOnTheAudioThreadAndIgnoresAnEmptyBuffer) {
    synth::soundbrowser::SoundPreviewPlayer player;
    EXPECT_EQ(player.playRendered(renderKeyAuditionSound(kMajor, kRate)), 0u) << "no rate yet: nothing to play";
    player.prepare(kRate, kBlock);
    EXPECT_EQ(player.getSampleRate(), kRate);
    EXPECT_EQ(player.playRendered({}), 0u);
    EXPECT_GT(player.playRendered(renderKeyAuditionSound(kMajor, kRate)), 0u);

    synth::BlockTimeInfo info;
    info.sampleRate = kRate;
    info.bpm = 120.0;
    info.timeSigNumerator = 4;
    info.timeSigDenominator = 4;
    info.numSamples = kBlock;
    info.loopWrapSample = -1;
    juce::AudioBuffer<float> buffer(2, kBlock);
    int allocations = 0;
    float peak = 0.0f;
    for (int block = 0; block < 60; ++block) {
        buffer.clear();
        synth::test::AllocationScope scope;
        player.process(buffer, 2, info);
        allocations += scope.count();
        peak = std::max(peak, buffer.getMagnitude(0, kBlock));
    }
    EXPECT_EQ(allocations, 0);
    EXPECT_GT(peak, 0.0f);
}

// Regression test for FRO1235: the scale preview was silent in a project with no instrument.
// An empty project has no instrument, yet hovering a key in the Song key picker is heard: audio-unheard guard. The
// whole path runs: the picker's hover delay, the field's default sound, the engine's sound preview, a real render.
TEST(KeyAuditionSound, HoveringAKeyOnAnEmptyProjectIsHeardQuietly) {
    Fixture f;
    SongKeyField field;
    f.wire(field);
    std::unique_ptr<juce::Component> held;
    field.pickerLauncher = [&held](std::unique_ptr<juce::Component> picker, juce::Rectangle<int>) {
        held = std::move(picker);
    };
    field.openPicker();
    auto* picker = dynamic_cast<KeyPicker*>(held.get());
    ASSERT_NE(picker, nullptr);
    picker->hoverVisibleKey(1);

    float peak = 0.0f;
    synth::test::waitUntil(
        [&] {
            peak = std::max(peak, f.renderPeak(8));
            return peak > 0.0f;
        },
        5000);
    EXPECT_GT(peak, 0.02f) << "the scale is heard";
    for (int i = 0; i < 40; ++i)
        peak = std::max(peak, f.renderPeak(2));
    RecordProperty("default_sound_peak", std::to_string(peak));
    EXPECT_LT(peak, kQuietBound) << "and it is quiet";
}

TEST(KeyAuditionSound, ABounceGuardKeepsItOutOfTheRender) {
    Fixture f;
    SongKeyField field;
    f.wire(field);
    {
        synth::MetronomeForceOffGuard guard(f.engine.getMetronome(), f.engine.getSoundPreview());
        field.startAudition(synth::makeScale(0, synth::scalePresetIndex("Major")));
        EXPECT_EQ(f.renderPeak(80), 0.0f) << "nothing of it in an offline render";
    }
    EXPECT_EQ(f.renderPeak(8), 0.0f) << "and it does not start late once the guard is gone";
    field.startAudition(synth::makeScale(0, synth::scalePresetIndex("Major")));
    EXPECT_GT(f.renderPeak(20), 0.0f) << "the next audition after the render is heard again";
}

TEST(KeyAuditionSound, StoppingTheAuditionCutsTheDefaultSound) {
    Fixture f;
    SongKeyField field;
    f.wire(field);
    field.startAudition(synth::makeScale(0, synth::scalePresetIndex("Major")));
    EXPECT_GT(f.renderPeak(8), 0.0f);
    field.stopAudition();
    f.renderPeak(4); // the 5 ms fade
    EXPECT_EQ(f.renderPeak(8), 0.0f);
}
