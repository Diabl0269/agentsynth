// Every way a person removes a modulation reaches the app window's one removal: the panel's remove button, a
// double-click on the dot and a double-click on the cable. The window decides what removal does
// (it asks before an LFO is deleted), so a path that cut the chain itself would skip the question.
// docs/modules/modulation.md#the-mod-dot-menu.

#include "ModDotTestFixture.h"

#include "UI/Layout/ReducedMotion.h"
#include <gtest/gtest.h>

namespace {

struct NoMotion {
    NoMotion() { synth::ui::setReducedMotionForTest(true); }
    ~NoMotion() { synth::ui::setReducedMotionForTest(std::nullopt); }
};

// Collects what the host is asked to remove, and removes nothing.
struct HostSpy {
    explicit HostSpy(Fixture& f) {
        f.editor->getModDot().host.removeModulator = [this](const synth::ui::ModulatorInfo& m) { asked.push_back(m); };
    }
    std::vector<synth::ui::ModulatorInfo> asked;
};

// The middle of the fixture's one modulation cable, in the editor's own coordinates (where a click on it lands).
juce::Point<int> cableMiddle(Fixture& f) {
    for (const auto& cable : f.editor->buildVisibleCables())
        if (cable.kind == GraphEditor::VisibleCable::Kind::AttenuverterChain) {
            const auto mid = ((cable.p1 + cable.p2) / 2.0f).roundToInt();
            return f.editor->getLocalPoint(f.editor->getChildComponent(0), mid);
        }
    return {};
}

void expectOneRemovalOfTheFixturesChain(Fixture& f, const HostSpy& spy, const char* how) {
    ASSERT_EQ(spy.asked.size(), 1u) << how;
    EXPECT_EQ(spy.asked[0].paramId, "gain") << how;
    EXPECT_EQ(spy.asked[0].targetChannel, f.gainChannel) << how;
    EXPECT_TRUE(spy.asked[0].isLfo) << how;
    EXPECT_TRUE(spy.asked[0].attenuverterUuid.isNotEmpty()) << how;
    EXPECT_NE(f.engine.getGraph().getNodeForId(f.attenId), nullptr) << how << ": the host decides what removal does";
}

} // namespace

// Regression test for FRO792: a double-click on the cable and on the dot cut the chain without the
// question the timeline row and the panel's remove button asked.
TEST_F(ModuleComponentTest, DoubleClickingTheCableAsksTheHostLikeThePanelsRemoveButton) {
    NoMotion motion;
    Fixture f;
    HostSpy spy(f);
    const auto mid = cableMiddle(f);
    ASSERT_NE(mid, juce::Point<int>());

    f.editor->mouseDoubleClick(makeModuleClickWithMods(*f.editor, mid, kPlain, 2));

    expectOneRemovalOfTheFixturesChain(f, spy, "cable double-click");
}

TEST_F(ModuleComponentTest, DoubleClickingTheDotAsksTheHost) {
    NoMotion motion;
    Fixture f;
    HostSpy spy(f);
    const auto dot = f.dotInKnob();
    f.gainKnob->mouseDown(makeModuleClickWithMods(*f.gainKnob, dot, kPlain, 2));
    f.gainKnob->mouseUp(makeModuleClickWithMods(*f.gainKnob, dot, kPlain, 2));

    expectOneRemovalOfTheFixturesChain(f, spy, "dot double-click");
}

TEST_F(ModuleComponentTest, ThePanelsRemoveButtonAsksTheHostWithTheSameDescription) {
    NoMotion motion;
    Fixture f;
    HostSpy spy(f);
    clickNow(f.clickDot()->sourcesPage().rowAt(0)->removeButton());
    ASSERT_EQ(spy.asked.size(), 1u);
    const auto fromPanel = spy.asked[0];

    spy.asked.clear();
    f.editor->mouseDoubleClick(makeModuleClickWithMods(*f.editor, cableMiddle(f), kPlain, 2));
    ASSERT_EQ(spy.asked.size(), 1u);
    EXPECT_EQ(spy.asked[0].attenuverterUuid, fromPanel.attenuverterUuid);
    EXPECT_EQ(spy.asked[0].sourceUuid, fromPanel.sourceUuid);
    EXPECT_EQ(spy.asked[0].targetUuid, fromPanel.targetUuid);
    EXPECT_EQ(spy.asked[0].paramId, fromPanel.paramId);
    EXPECT_EQ(spy.asked[0].targetChannel, fromPanel.targetChannel);
}

TEST_F(ModuleComponentTest, WithNoHostTheCableDoubleClickStillCutsTheChainInOneUndoStep) {
    NoMotion motion;
    Fixture f;
    f.editor->mouseDoubleClick(makeModuleClickWithMods(*f.editor, cableMiddle(f), kPlain, 2));

    EXPECT_EQ(f.engine.getGraph().getNodeForId(f.attenId), nullptr);
    ASSERT_TRUE(f.undo.undo());
    EXPECT_FALSE(f.undo.canUndo());
}
