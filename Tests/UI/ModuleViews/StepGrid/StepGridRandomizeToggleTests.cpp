// StepGridRandomizeToggleTests.cpp -- the Randomize button toggles its panel: a second request while the panel is open
// closes it through one fade (never a second panel), one while it fades does nothing, and Escape closes it once. The
// panel opens in a real pop-up window here, as a click on the button does.
#include "../../Layout/NativeWindowTestGuard.h"
#include "../../Layout/PopupAlphaTrace.h"
#include "../../Layout/PopupTestCleanup.h"
#include "StepGridCardFixture.h"
#include "TestMode/AutomationHiding.h"
#include "UI/Layout/PopupMotion.h"
#include <functional>
#include <gtest/gtest.h>
#include <vector>

using stepgrid_test::SeqCard;
using synth::ui::PopupMotion;
using synth::ui::StepGridRandomizePanel;
using Tool = synth::ui::StepGridTools::Tool;

namespace {

void pumpFor(const std::function<bool()>& done) {
    const auto deadline = juce::Time::getMillisecondCounter() + 3000;
    while (!done() && juce::Time::getMillisecondCounter() < deadline)
        juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
}

std::vector<juce::CallOutBox*> openBoxes() {
    std::vector<juce::CallOutBox*> boxes;
    auto& desktop = juce::Desktop::getInstance();
    for (int i = 0; i < desktop.getNumComponents(); ++i)
        if (auto* box = dynamic_cast<juce::CallOutBox*>(desktop.getComponent(i)))
            boxes.push_back(box);
    return boxes;
}

int panelsOpen() {
    int count = 0;
    for (auto* box : openBoxes())
        if (box->getNumChildComponents() > 0 && dynamic_cast<StepGridRandomizePanel*>(box->getChildComponent(0)))
            ++count;
    return count;
}

class RandomizeToggle : public ::testing::Test {
protected:
    void SetUp() override {
        if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() == nullptr)
            GTEST_SKIP() << "needs a display";
        if (nativeWindowsAbortHere())
            GTEST_SKIP() << "native windows abort the process under the ASAN job's Xvfb";
        synth::testmode::setAppInFrontForTest(true);
        PopupMotion::setAnimateOffScreenForTest(true);
    }
    void TearDown() override { synth::test::finishPopupTest(); }

    void click() { card.grid->getTools().getButton(Tool::Randomize).onClick(); }

    juce::CallOutBox* onlyBox() {
        const auto boxes = openBoxes();
        return boxes.size() == 1 ? boxes.front() : nullptr;
    }

    void fadeOut(juce::CallOutBox& box) {
        std::vector<float> frames{box.getAlpha()};
        for (const double ms : {30.0, 30.0, 30.0, 200.0})
            if (PopupMotion::isDismissing(box)) {
                PopupMotion::stepMotionForTest(box, ms);
                frames.push_back(box.getAlpha());
            }
        EXPECT_TRUE(synth::test::isNonIncreasing(frames));
        pumpFor([] { return openBoxes().empty(); });
        EXPECT_EQ(panelsOpen(), 0);
    }

    SeqCard card;
};

} // namespace

TEST_F(RandomizeToggle, ASecondClickWhileOpenClosesItThroughOneFadeAndNeverOpensASecondPanel) {
    click();
    auto* box = onlyBox();
    ASSERT_NE(box, nullptr);
    EXPECT_EQ(panelsOpen(), 1);

    click();
    EXPECT_EQ(panelsOpen(), 1) << "no second panel while the first fades";
    ASSERT_TRUE(PopupMotion::isDismissing(*box)) << "the second request closed the panel";

    click(); // while it leaves
    EXPECT_EQ(openBoxes().size(), 1u) << "a request while it is leaving does nothing";
    EXPECT_TRUE(PopupMotion::isDismissing(*box));
    fadeOut(*box);

    click();
    EXPECT_EQ(panelsOpen(), 1) << "after it has gone the button opens it again";
}

TEST_F(RandomizeToggle, EscapeClosesTheOpenPanelOnce) {
    click();
    auto* box = onlyBox();
    ASSERT_NE(box, nullptr);
    const juce::KeyPress escape(juce::KeyPress::escapeKey, juce::ModifierKeys(), 0);
    EXPECT_TRUE(box->keyPressed(escape));
    ASSERT_TRUE(PopupMotion::isDismissing(*box));
    EXPECT_TRUE(box->keyPressed(escape)) << "a second Escape while it fades is taken and does nothing";
    EXPECT_EQ(openBoxes().size(), 1u);
    fadeOut(*box);
}
