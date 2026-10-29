// Finding components by id: the order the matches are tried in, the scoped form, and what describe reports.

#include "TestMode/TestModeComponents.h"
#include "TestModeRig.h"

using namespace synth::testmode;
using testmode_rig::Rig;

TEST(TestModeComponents, FindsByComponentIdThenNameThenTitle) {
    Rig rig;
    if (!rig.show())
        GTEST_SKIP() << "no native window in this environment";
    EXPECT_EQ(findComponent("left").component, &rig.left);
    EXPECT_EQ(findComponent("rightByName").component, &rig.right); // by name
    rig.label.setTitle("spoken");
    EXPECT_EQ(findComponent("spoken").component, &rig.label); // by title

    // The id wins over a name that happens to be the same text.
    rig.right.setName("left");
    EXPECT_EQ(findComponent("left").component, &rig.left);
}

TEST(TestModeComponents, PrefersAShowingMatch) {
    Rig rig;
    if (!rig.show())
        GTEST_SKIP() << "no native window in this environment";
    rig.left.setComponentID("twin");
    rig.right.setComponentID("twin");
    rig.left.setVisible(false);
    EXPECT_EQ(findComponent("twin").component, &rig.right);
    rig.left.setVisible(true);
    EXPECT_EQ(findComponent("twin").component, &rig.left); // both showing: the first in tree order
}

TEST(TestModeComponents, ScopedFormSearchesInsideTheFirstMatch) {
    Rig rig;
    if (!rig.show())
        GTEST_SKIP() << "no native window in this environment";
    juce::Component inner;
    inner.setComponentID("inner");
    rig.left.addAndMakeVisible(inner);
    inner.setBounds(0, 0, 10, 10);
    EXPECT_EQ(findComponent("left > inner").component, &inner);
    EXPECT_EQ(findComponent("rigWindow > left > inner").component, &inner);
    EXPECT_EQ(findComponent("right > inner").component, nullptr);
}

TEST(TestModeComponents, ANotFoundIdSaysWhichPartMissed) {
    Rig rig;
    if (!rig.show())
        GTEST_SKIP() << "no native window in this environment";
    const auto missing = findComponent("nope");
    EXPECT_EQ(missing.component, nullptr);
    EXPECT_NE(missing.error.indexOf("nope"), -1);
    EXPECT_NE(findComponent("left > nope").error.indexOf("nope"), -1);
    EXPECT_TRUE(findComponent("  ").error.isNotEmpty());
}

TEST(TestModeComponents, WindowNamesTheMainWindowOrFirstTopLevel) {
    Rig rig;
    if (!rig.show())
        GTEST_SKIP() << "no native window in this environment";
    EXPECT_NE(findComponent("window").component, nullptr);
    EXPECT_EQ(findComponent("window").component, findMainWindow());
}

TEST(TestModeComponents, DescribeReportsIdsBoundsAndVisibility) {
    Rig rig;
    if (!rig.show())
        GTEST_SKIP() << "no native window in this environment";
    const auto d = describeComponent(rig.right);
    EXPECT_EQ(d["id"].toString(), "right");
    EXPECT_EQ(d["name"].toString(), "rightByName");
    EXPECT_TRUE(static_cast<bool>(d["showing"]));
    EXPECT_NE(d["type"].toString().indexOf("Probe"), -1);
    EXPECT_EQ(static_cast<int>(d["bounds"]["w"]), 100);
}

TEST(TestModeComponents, ListSkipsNonShowingUnlessAll) {
    Rig rig;
    if (!rig.show())
        GTEST_SKIP() << "no native window in this environment";
    rig.right.setVisible(false);
    const auto count = [&](bool all) {
        const auto tree = describeTree(rig, 3, all);
        return tree["children"].getArray()->size();
    };
    EXPECT_EQ(count(true), count(false) + 1);
    EXPECT_EQ(describeTree(rig, 0, true)["children"].getArray()->size(), 0);
}

TEST(TestModeComponents, StateIncludesTheStockWidgetFields) {
    Rig rig;
    if (!rig.show())
        GTEST_SKIP() << "no native window in this environment";
    EXPECT_EQ(describeState(rig.button)["text"].toString(), "Go");
    EXPECT_FALSE(static_cast<bool>(describeState(rig.button)["toggleState"]));
    EXPECT_EQ(static_cast<double>(describeState(rig.slider)["value"]), 4.0);
    EXPECT_EQ(static_cast<double>(describeState(rig.slider)["max"]), 10.0);
    EXPECT_EQ(describeState(rig.label)["text"].toString(), "hello");
    EXPECT_EQ(static_cast<int>(describeState(rig.combo)["selectedId"]), 2);
    EXPECT_EQ(describeState(rig.combo)["text"].toString(), "two");
    EXPECT_TRUE(static_cast<bool>(describeState(rig.left)["enabled"]));
}

TEST(TestModeComponents, AButtonsTooltipIsReported) {
    Rig rig;
    if (!rig.show())
        GTEST_SKIP() << "no native window in this environment";
    rig.button.setTooltip("Record (Ctrl+R)");
    EXPECT_EQ(describeState(rig.button)["tooltip"].toString(), "Record (Ctrl+R)");
    EXPECT_EQ(describeState(rig.left)["tooltip"].toString(), ""); // a component with no tooltip support reports none
}
