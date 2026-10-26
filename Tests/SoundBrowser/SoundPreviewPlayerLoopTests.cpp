// A loop the preview fits to the song: the clip it plays is as long as the stretch says, and a loop with no known
// tempo plays as recorded.
#include "AudioEngine/AudioEngine.h"
#include "SoundBrowser/SoundPreviewPlayer.h"
#include "SoundBrowserTestKit.h"
#include "Transport/OfflineTransportDriver.h"
#include <gtest/gtest.h>

using synth::soundbrowser::LoopTarget;

namespace {

constexpr double kRate = 48000.0;
constexpr int kBlock = 512;

struct Fixture {
    juce::File dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                         .getChildFile("preview-loop-" + juce::Uuid().toDashedString());
    AudioEngine engine{AudioEngine::HostMode::Hosted};
    std::unique_ptr<synth::OfflineTransportDriver> driver;
    Fixture() {
        engine.initialise();
        engine.getGraph().clear();
        driver = std::make_unique<synth::OfflineTransportDriver>(engine, kRate, kBlock, 2);
        engine.setMasterMute(true);
        engine.getSoundPreview().setGainDb(0.0f);
    }
    ~Fixture() {
        engine.releaseFromHost();
        engine.shutdown();
        dir.deleteRecursively();
    }

    /** Plays a 2 s tone and returns how many samples sound before it ends (the clip's length), or -1 if none do. */
    int soundedSamples(const LoopTarget* target) {
        const auto file = dir.getChildFile("tone.wav");
        sound_browser_test::writeSignal(file, 2.0, kRate, 0.5f, 440.0, 2);
        auto& preview = engine.getSoundPreview();
        if (target != nullptr)
            preview.play(file, false, *target);
        else
            preview.play(file, false);
        EXPECT_TRUE(preview.waitForDecodeForTests(20000));
        const auto audio = driver->renderBlocks(260); // 2.77 s
        int last = -1;
        for (int i = 0; i < audio.getNumSamples(); ++i)
            if (std::abs(audio.getSample(0, i)) > 0.001f)
                last = i;
        return last + 1;
    }
};

} // namespace

TEST(SoundPreviewPlayerLoopTest, ALoopAtASlowerSongTempoPlaysLonger) {
    EXPECT_NEAR(Fixture().soundedSamples(nullptr), 2.0 * kRate, 0.02 * kRate);
    const LoopTarget slower{120.0, 96.0, 0};
    EXPECT_NEAR(Fixture().soundedSamples(&slower), 2.5 * kRate, 0.04 * kRate) << "2 s at 120 BPM is 2.5 s at 96 BPM";
}

TEST(SoundPreviewPlayerLoopTest, ALoopAtAFasterSongTempoPlaysShorter) {
    const LoopTarget faster{100.0, 125.0, 0};
    EXPECT_NEAR(Fixture().soundedSamples(&faster), 1.6 * kRate, 0.04 * kRate);
}

TEST(SoundPreviewPlayerLoopTest, AnUnknownFileTempoPlaysAsRecorded) {
    const LoopTarget unknown{0.0, 96.0, 0};
    EXPECT_NEAR(Fixture().soundedSamples(&unknown), 2.0 * kRate, 0.02 * kRate);
}

TEST(SoundPreviewPlayerLoopTest, ATempoWithinHalfAPercentIsNotStretched) {
    const LoopTarget same{120.0, 120.4, 0};
    EXPECT_NEAR(Fixture().soundedSamples(&same), 2.0 * kRate, 0.02 * kRate);
}
