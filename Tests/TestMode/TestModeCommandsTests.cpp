// The command registry, run to completion on the test's own message thread: the same steps the socket thread hands
// over one at a time.

#include "TestMode/TestModeComponents.h"
#include "TestModeRig.h"
#include "UI/Layout/ReducedMotion.h"

using namespace synth::testmode;
using testmode_rig::args;
using testmode_rig::Rig;
using testmode_rig::run;

TEST(TestModeCommands, TheRegistryHasEveryProtocolCommand) {
    const auto names = CommandTable::standard().names();
    for (const char* name : {"ping", "list", "click", "hover", "drag", "key", "getState", "screenshot", "openProject",
                             "waitForIdle", "quit"})
        EXPECT_TRUE(names.contains(name)) << name;
    EXPECT_EQ(CommandTable::standard().find("nope"), nullptr);
}

TEST(TestModeCommands, PingReportsThePid) {
    const auto r = run("ping", args({}));
    ASSERT_EQ(r.state, StepResult::State::done);
    EXPECT_GT(static_cast<int>(r.value["pid"]), 0);
}

TEST(TestModeCommands, ClickDefaultsToTheCentreAndReportsThePoint) {
    Rig rig;
    if (!rig.show())
        GTEST_SKIP() << "no native window in this environment";
    const auto r = run("click", args({{"id", "left"}}));
    ASSERT_EQ(r.state, StepResult::State::done) << r.error;
    EXPECT_EQ(static_cast<int>(r.value["point"]["x"]), 50);
    EXPECT_EQ(rig.left.downs, 1);
    EXPECT_EQ(rig.left.ups, 1);
}

TEST(TestModeCommands, HoverRestsThePointerOnAComponentWithoutPressing) {
    Rig rig;
    if (!rig.show())
        GTEST_SKIP() << "no native window in this environment";
    const auto r = run("hover", args({{"id", "left"}}));
    ASSERT_EQ(r.state, StepResult::State::done) << r.error;
    EXPECT_GE(rig.left.enters, 1);
    EXPECT_EQ(rig.left.downs, 0);
    EXPECT_EQ(run("hover", args({{"id", "nope"}})).state, StepResult::State::failed);
}

TEST(TestModeCommands, ClickTakesAPointButtonClicksAndMods) {
    Rig rig;
    if (!rig.show())
        GTEST_SKIP() << "no native window in this environment";
    juce::Array<juce::var> mods;
    mods.add("shift");
    const auto r =
        run("click", args({{"id", "right"}, {"x", 5}, {"y", 6}, {"button", "right"}, {"clicks", 2}, {"mods", mods}}));
    ASSERT_EQ(r.state, StepResult::State::done) << r.error;
    EXPECT_EQ(rig.right.downs, 2);
    EXPECT_EQ(rig.right.doubleClicks, 1);
    EXPECT_TRUE(rig.right.downMods.isRightButtonDown());
    EXPECT_TRUE(rig.right.downMods.isShiftDown());
}

TEST(TestModeCommands, ClickOnAMissingIdOrBadModifierFails) {
    Rig rig;
    if (!rig.show())
        GTEST_SKIP() << "no native window in this environment";
    EXPECT_EQ(run("click", args({{"id", "nope"}})).state, StepResult::State::failed);
    EXPECT_EQ(run("click", args({})).state, StepResult::State::failed);
    juce::Array<juce::var> mods;
    mods.add("hyper");
    EXPECT_EQ(run("click", args({{"id", "left"}, {"mods", mods}})).state, StepResult::State::failed);
}

TEST(TestModeCommands, ModsMayBeASingleString) {
    // Regression test for FRO1313: a string "shift" was read as no modifiers at all.
    Rig rig;
    if (!rig.show())
        GTEST_SKIP() << "no native window in this environment";
    const auto r = run("click", args({{"id", "left"}, {"mods", "shift"}}));
    ASSERT_EQ(r.state, StepResult::State::done) << r.error;
    EXPECT_TRUE(rig.left.downMods.isShiftDown());
}

TEST(TestModeCommands, AnUnknownOrMalformedModifierIsAnErrorNamingIt) {
    Rig rig;
    if (!rig.show())
        GTEST_SKIP() << "no native window in this environment";
    for (const char* command : {"click", "drag"}) {
        const auto base = std::string(command) == "click"
                              ? juce::var(args({{"id", "left"}, {"mods", "hyper"}}))
                              : juce::var(args({{"from", "left"}, {"to", "right"}, {"mods", "hyper"}}));
        const auto r = run(command, base);
        EXPECT_EQ(r.state, StepResult::State::failed) << command;
        EXPECT_TRUE(r.error.contains("hyper")) << command << ": " << r.error;
    }
    const auto number = run("click", args({{"id", "left"}, {"mods", 3}}));
    EXPECT_EQ(number.state, StepResult::State::failed);
    EXPECT_TRUE(number.error.contains("mods")) << number.error;
    EXPECT_EQ(rig.left.downs, 0);
}

TEST(TestModeCommands, DragTakesAStringModifierToo) {
    Rig rig;
    if (!rig.show())
        GTEST_SKIP() << "no native window in this environment";
    const auto r = run("drag", args({{"from", "left"}, {"to", "right"}, {"steps", 4}, {"mods", "alt"}}));
    ASSERT_EQ(r.state, StepResult::State::done) << r.error;
    EXPECT_TRUE(rig.left.downMods.isAltDown());
}

TEST(TestModeCommands, DragPressesMovesInStepsAndReleases) {
    Rig rig;
    if (!rig.show())
        GTEST_SKIP() << "no native window in this environment";
    const auto r = run("drag", args({{"from", "left"}, {"to", "right"}, {"steps", 8}}));
    ASSERT_EQ(r.state, StepResult::State::done) << r.error;
    EXPECT_EQ(rig.left.downs, 1);
    EXPECT_GE(rig.left.drags, 4);
    EXPECT_EQ(rig.left.ups, 1);
}

TEST(TestModeCommands, KeyReachesTheFocusedComponentOrTheWindow) {
    Rig rig;
    if (!rig.show())
        GTEST_SKIP() << "no native window in this environment";
    const auto r = run("key", args({{"keys", "space"}}));
    ASSERT_EQ(r.state, StepResult::State::done) << r.error;
    EXPECT_EQ(rig.keys + rig.left.keys + rig.right.keys, 1);
    EXPECT_EQ(run("key", args({{"keys", "hyper+q"}})).state, StepResult::State::failed);
}

TEST(TestModeCommands, GetStateReadsAControl) {
    Rig rig;
    if (!rig.show())
        GTEST_SKIP() << "no native window in this environment";
    const auto r = run("getState", args({{"id", "volume"}}));
    ASSERT_EQ(r.state, StepResult::State::done) << r.error;
    EXPECT_EQ(static_cast<double>(r.value["value"]), 4.0);
    EXPECT_EQ(run("getState", args({{"id", "nope"}})).state, StepResult::State::failed);
}

TEST(TestModeCommands, ListReturnsTheTreeBelowARoot) {
    Rig rig;
    if (!rig.show())
        GTEST_SKIP() << "no native window in this environment";
    const auto r = run("list", args({{"root", "rigWindow"}, {"depth", 2}}));
    ASSERT_EQ(r.state, StepResult::State::done) << r.error;
    EXPECT_EQ(r.value["id"].toString(), "");
    EXPECT_GE(r.value["children"].getArray()->size(), 6);
    EXPECT_EQ(run("list", args({})).value.getArray() != nullptr, true);
}

TEST(TestModeCommands, ScreenshotWritesAPngOfTheComponentSize) {
    Rig rig;
    if (!rig.show())
        GTEST_SKIP() << "no native window in this environment";
    const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("testmode-shot-tests");
    dir.deleteRecursively();
    const auto file = dir.getChildFile("nested").getChildFile("left.png"); // parent folders are created
    const auto r = run("screenshot", args({{"id", "left"}, {"path", file.getFullPathName()}}));
    ASSERT_EQ(r.state, StepResult::State::done) << r.error;
    EXPECT_EQ(static_cast<int>(r.value["width"]), 100);
    EXPECT_EQ(static_cast<int>(r.value["height"]), 100);
    const auto image = juce::ImageFileFormat::loadFrom(file);
    ASSERT_TRUE(image.isValid());
    EXPECT_EQ(image.getWidth(), 100);

    const auto big = run("screenshot", args({{"id", "left"}, {"path", file.getFullPathName()}, {"scale", 2}}));
    EXPECT_EQ(static_cast<int>(big.value["width"]), 200);

    EXPECT_EQ(run("screenshot", args({{"id", "left"}, {"path", "relative.png"}})).state, StepResult::State::failed);
    dir.deleteRecursively();
}

TEST(TestModeCommands, OpenProjectNeedsAMainWindowAndABundle) {
    Rig rig; // a window, but not the app's
    if (!rig.show())
        GTEST_SKIP() << "no native window in this environment";
    EXPECT_EQ(run("openProject", args({{"path", "/nonexistent.agsproj"}})).state, StepResult::State::failed);
}

TEST(TestModeCommands, WaitForIdleReturnsOnceNothingAnimates) {
    Rig rig;
    if (!rig.show())
        GTEST_SKIP() << "no native window in this environment";
    const auto r = run("waitForIdle", args({{"settleMs", 30}, {"timeoutMs", 3000}}));
    EXPECT_EQ(r.state, StepResult::State::done) << r.error;
}

TEST(TestModeCommands, WaitForIdleWaitsOutARunningAnimation) {
    Rig rig;
    if (!rig.show())
        GTEST_SKIP() << "no native window in this environment";
    juce::Desktop::getInstance().getAnimator().animateComponent(&rig.left, {10, 10, 160, 100}, 1.0f, 250, false, 1.0,
                                                                1.0);
    // Animations are never switched off by this test, so the animator has real work to wait for.
    ASSERT_TRUE(juce::Desktop::getInstance().getAnimator().isAnimating());
    const auto r = run("waitForIdle", args({{"settleMs", 30}, {"timeoutMs", 3000}}));
    ASSERT_EQ(r.state, StepResult::State::done) << r.error;
    EXPECT_GE(static_cast<double>(r.value["waitedMs"]), 100.0);
    EXPECT_FALSE(juce::Desktop::getInstance().getAnimator().isAnimating());
}
