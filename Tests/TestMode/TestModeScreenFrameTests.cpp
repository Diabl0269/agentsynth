// The `screenshot` id "screen": every visible top-level window of the app at its screen position, and the pointer.

#include "TestModeRig.h"
#include "UI/Timeline/ToolCursors.h"

using namespace synth::testmode;
using testmode_rig::args;
using testmode_rig::Rig;
using testmode_rig::run;

namespace {

// A solid red top-level window, as a pop-up or the shape editor window is one of its own beside the main window.
struct RedWindow : juce::Component {
    void paint(juce::Graphics& g) override { g.fillAll(juce::Colours::red); }
    bool show(juce::Rectangle<int> screenBounds) {
        addToDesktop(juce::ComponentPeer::windowIsTemporary);
        setVisible(true);
        setBounds(screenBounds);
        return isOnDesktop();
    }
};

struct ScreenShot {
    StepResult result;
    juce::Image image;
};

ScreenShot takeScreenShot() {
    const auto file =
        juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("testmode-screen-frame.png");
    ScreenShot shot{run("screenshot", args({{"id", "screen"}, {"path", file.getFullPathName()}})), {}};
    shot.image = juce::ImageFileFormat::loadFrom(file);
    file.deleteFile();
    return shot;
}

} // namespace

TEST(TestModeScreenFrame, ItIsTheUnionOfTheMainWindowAndAnotherTopLevelWindowAtItsOffset) {
    Rig rig;
    if (!rig.show())
        GTEST_SKIP() << "no native window in this environment";
    RedWindow other;
    // 50 px right of the main window's right edge and 100 px below its top: it sticks out of the main window's box.
    const auto mainBounds = rig.getScreenBounds();
    if (!other.show({mainBounds.getRight() + 50, mainBounds.getY() + 100, 120, 260}))
        GTEST_SKIP() << "no native window in this environment";

    const auto shot = takeScreenShot();
    ASSERT_EQ(shot.result.state, StepResult::State::done) << shot.result.error;
    ASSERT_TRUE(shot.image.isValid());
    EXPECT_EQ(shot.image.getWidth(), 400 + 50 + 120);
    EXPECT_EQ(shot.image.getHeight(), 360); // 100 + 260 is below the main window's 300
    EXPECT_EQ(static_cast<int>(shot.result.value["width"]), 570);
    const auto* windows = shot.result.value["windows"].getArray();
    ASSERT_NE(windows, nullptr);
    ASSERT_EQ(windows->size(), 2);
    EXPECT_EQ(static_cast<int>((*windows)[1]["x"]), 450);
    EXPECT_EQ(static_cast<int>((*windows)[1]["y"]), 100);
    EXPECT_EQ(shot.image.getPixelAt(450 + 10, 100 + 10), juce::Colours::red);
    EXPECT_EQ(shot.image.getPixelAt(450 + 10, 99), juce::Colours::black) << "above the second window is not drawn";
}

TEST(TestModeScreenFrame, ThePointerIsDrawnAtTheLastSyntheticPoint) {
    Rig rig;
    if (!rig.show())
        GTEST_SKIP() << "no native window in this environment";
    ASSERT_EQ(run("hover", args({{"id", "left"}, {"x", 50}, {"y", 50}})).state, StepResult::State::done);

    const auto shot = takeScreenShot();
    ASSERT_EQ(shot.result.state, StepResult::State::done) << shot.result.error;
    const auto& pointer = shot.result.value["pointer"];
    EXPECT_EQ(static_cast<int>(pointer["x"]), 60); // left sits at (10, 10) in the window
    EXPECT_EQ(static_cast<int>(pointer["y"]), 60);
    EXPECT_EQ(pointer["kind"].toString(), "arrow");
    // The arrow's body is white; the window behind it is not.
    EXPECT_GT(shot.image.getPixelAt(60 + 2, 60 + 8).getBrightness(), 0.9f);
    EXPECT_LT(shot.image.getPixelAt(60 + 40, 60 + 40).getBrightness(), 0.5f);
}

TEST(TestModeScreenFrame, ACustomImageCursorUnderThePointerIsDrawnAtItsHotspot) {
    Rig rig;
    if (!rig.show())
        GTEST_SKIP() << "no native window in this environment";
    juce::DrawablePath icon;
    juce::Path square;
    square.addRectangle(0.0f, 0.0f, 24.0f, 24.0f);
    icon.setPath(square);
    icon.setFill(juce::Colours::lime);
    rig.left.setMouseCursor(synth::ui::makeToolCursor(synth::ui::EditTool::Range, &icon));
    ASSERT_EQ(run("hover", args({{"id", "left"}, {"x", 50}, {"y", 50}})).state, StepResult::State::done);

    const auto shot = takeScreenShot();
    ASSERT_EQ(shot.result.state, StepResult::State::done) << shot.result.error;
    EXPECT_EQ(shot.result.value["pointer"]["kind"].toString(), "tool:range");
    // The range cursor's hotspot is the icon's centre, so the icon spans 12 px each way around the pointer.
    EXPECT_EQ(shot.image.getPixelAt(60, 60), juce::Colours::lime);
    EXPECT_EQ(shot.image.getPixelAt(60 - 11, 60 - 11), juce::Colours::lime);
    EXPECT_EQ(shot.image.getPixelAt(60 + 10, 60 + 10), juce::Colours::lime);
    EXPECT_LT(shot.image.getPixelAt(60 + 14, 60 + 14).getBrightness(), 0.5f);
}
