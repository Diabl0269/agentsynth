// MotionLoopUsersTests.cpp -- the Animations preference reaches every continuous, decorative loop. A Source/UI file
// that drives a loop from the clock (a millisecond counter feeding a sine, cosine or fmod) or from a phase it steps
// each tick must ask loopRate(), prefersReducedMotion() or animationsOff(), so Reduced slows it and Off holds it
// still (docs/layout/animation.md#continuous-loops). Live data (meters, scopes, the playhead) is not a decorative loop
// and goes in kNotALoop with a reason.
#include "../SourceScan.h"
#include "UI/Assistant/AIChatComponent/AIChatComponent.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/MidiRemote/MidiLearnMenu.h"
#include <gtest/gtest.h>
#include <map>
#include <regex>
#include <string>

namespace {

using synth::ui::AnimationMode;

// File -> why it is live data or not a loop. Shrink-only: never add an entry for a decorative loop, make the loop
// follow the preference instead.
const std::map<std::string, const char*> kNotALoop = {};

// Restores the mode so a later test never inherits Reduced or Off.
class MotionLoopMode : public ::testing::Test {
protected:
    void TearDown() override {
        synth::ui::setAnimationMode(AnimationMode::followSystem);
        synth::ui::setReducedMotionForTest(std::nullopt);
    }
};

using MotionLoopUsers = MotionLoopMode;
using ReducedMotionLoopRate = MotionLoopMode;

} // namespace

TEST_F(MotionLoopUsers, EveryTimeDrivenLoopFollowsTheAnimationsSetting) {
    const std::regex clockLoop(R"(getMillisecondCounter\w*)");
    const std::regex periodic(R"(std::sin\(|std::cos\(|std::fmod\(|\bfmod\()");
    const std::regex phaseStep(R"([A-Za-z]*[Pp]hase\s*\+=)");
    const std::regex follows(R"(loopRate\(|prefersReducedMotion\(|animationsOff\()");
    for (const auto& [file, text] : synth::test::source_scan::sourceTexts()) {
        if (file.rfind("Source/UI/", 0) != 0)
            continue;
        const bool isLoop = (std::regex_search(text, clockLoop) && std::regex_search(text, periodic)) ||
                            std::regex_search(text, phaseStep);
        if (!isLoop || kNotALoop.count(file) > 0)
            continue;
        EXPECT_TRUE(std::regex_search(text, follows))
            << file << " runs a time- or phase-driven loop without asking synth::ui::loopRate()";
    }
}

TEST_F(ReducedMotionLoopRate, FullReducedAndOff) {
    synth::ui::setAnimationMode(AnimationMode::full);
    EXPECT_EQ(synth::ui::loopRate(), 1.0);
    synth::ui::setAnimationMode(AnimationMode::reduced);
    EXPECT_EQ(synth::ui::loopRate(), synth::ui::kReducedLoopRate);
    synth::ui::setAnimationMode(AnimationMode::off);
    EXPECT_EQ(synth::ui::loopRate(), 0.0);
}

TEST_F(ReducedMotionLoopRate, TheMidiLearnOutlineBreathesSlowerUnderReducedAndHoldsUnderOff) {
    using synth::ui::midilearn::midiLearnArmedOutlineAlpha;
    synth::ui::setAnimationMode(AnimationMode::off);
    for (const double ms : {0.0, 300.0, 600.0, 1234.0})
        EXPECT_FLOAT_EQ(midiLearnArmedOutlineAlpha(ms), 1.0f);

    synth::ui::setAnimationMode(AnimationMode::full);
    EXPECT_NEAR(midiLearnArmedOutlineAlpha(0.0), 0.4f, 1e-4f);
    EXPECT_NEAR(midiLearnArmedOutlineAlpha(600.0), 1.0f, 1e-4f);

    synth::ui::setAnimationMode(AnimationMode::reduced);
    EXPECT_LT(midiLearnArmedOutlineAlpha(600.0), 0.8f); // period 4 s: 600 ms is still near the dim end
    EXPECT_NEAR(midiLearnArmedOutlineAlpha(2000.0), 1.0f, 1e-4f);
}

TEST_F(ReducedMotionLoopRate, TheWaitingDotPulsesSlowerUnderReduced) {
    synth::ui::setAnimationMode(AnimationMode::full);
    EXPECT_DOUBLE_EQ(synth::AIChatComponent::waitingDotHalfCycleMs(), 600.0);
    synth::ui::setAnimationMode(AnimationMode::reduced);
    EXPECT_NEAR(synth::AIChatComponent::waitingDotHalfCycleMs(), 2000.0, 1e-6);
}
