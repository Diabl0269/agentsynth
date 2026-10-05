// "Pick on canvas": the right half of the split button puts a layer over the canvas; a press on a module that can
// be a source adds it to the knob in one undo step; Esc, the half again, or the canvas being rebuilt stop it.
// docs/modules/modulation.md#the-mod-dot-menu.

#include "ModDotTestFixture.h"

#include "AudioEngine/ModuleTitle.h"
#include "UI/Graph/ModDot/ModDotCanvasPicker.h"
#include "UI/Layout/ReducedMotion.h"

namespace {

using synth::ui::ModDotPopover;

struct NoMotion {
    NoMotion() { synth::ui::setReducedMotionForTest(true); }
    ~NoMotion() { synth::ui::setReducedMotionForTest(std::nullopt); }
};

void mouseClick(juce::Component& c) {
    const auto at = c.getLocalBounds().getCentre();
    c.mouseDown(makeModuleClickWithMods(c, at, kPlain));
    c.mouseUp(makeModuleClickWithMods(c, at, kPlain));
    juce::MessageManager::getInstance()->runDispatchLoopUntil(30);
}

struct PickFixture : Fixture {
    juce::AudioProcessorGraph::NodeID lfo2;
    ModuleComponent* lfo2Card = nullptr;
    ModDotPopover* panel = nullptr;

    PickFixture() {
        lfo2 = engine.getGraph().addNode(std::make_unique<LFOModule>())->nodeID;
        refresh();
        lfo2Card = findModuleComp(*editor, engine.getGraph().getNodeForId(lfo2)->getProcessor());
        panel = clickDot();
    }

    void startPick() { mouseClick(panel->sourcesPage().splitButton().pickHalf()); }

    // A press on `card` as the picker layer receives it.
    void pressOn(ModuleComponent& card) {
        auto* picker = panel->canvasPicker();
        ASSERT_NE(picker, nullptr);
        const auto at = picker->getLocalPoint(&card, card.getLocalBounds().getCentre());
        picker->mouseDown(makeModuleClickWithMods(*picker, at, kPlain));
    }
};

} // namespace

TEST_F(ModuleComponentTest, ThePickHalfPutsALayerOverTheCanvasAndTheHalfAgainTakesItAway) {
    NoMotion motion;
    PickFixture f;
    ASSERT_NE(f.lfo2Card, nullptr);
    EXPECT_FALSE(f.panel->isPicking());

    f.startPick();

    ASSERT_TRUE(f.panel->isPicking());
    auto* picker = f.panel->canvasPicker();
    ASSERT_NE(picker, nullptr);
    EXPECT_EQ(picker->getParentComponent(), f.editor.get()) << "the layer covers the canvas";
    EXPECT_EQ(picker->getBounds(), f.editor->getLocalBounds());
    EXPECT_TRUE(f.panel->sourcesPage().splitButton().isPicking()) << "the half is lit";

    mouseClick(f.panel->sourcesPage().splitButton().pickHalf());
    EXPECT_FALSE(f.panel->isPicking());
    EXPECT_FALSE(f.panel->sourcesPage().splitButton().isPicking());
    EXPECT_EQ(f.editor->getNumChildComponents() > 0 ? f.editor->findChildWithID("modDotCanvasPicker") : nullptr,
              nullptr)
        << "the layer is gone from the canvas";
}

TEST_F(ModuleComponentTest, APressOnAnotherModuleAddsItAsASourceInOneUndoStepAndEndsThePick) {
    NoMotion motion;
    PickFixture f;
    ASSERT_NE(f.lfo2Card, nullptr);
    f.startPick();
    ASSERT_FALSE(f.undo.canUndo());

    f.pressOn(*f.lfo2Card);

    EXPECT_FALSE(f.panel->isPicking()) << "one pick, then the mode ends";
    ASSERT_EQ(f.engine.getModulationRoutings().size(), 2u);
    auto& rows = f.panel->sourcesPage();
    ASSERT_EQ(rows.rowCount(), 2);
    EXPECT_EQ(rows.rowAt(1)->source().sourceNodeId, f.lfo2);
    EXPECT_TRUE(rows.rowAt(1)->isSelected());
    EXPECT_NEAR(f.amount(rows.rowAt(1)->attenuverterId()), synth::ui::kModDotNewSourceDepth, 1e-4f);
    ASSERT_TRUE(f.undo.undo());
    EXPECT_EQ(f.engine.getModulationRoutings().size(), 1u);
    EXPECT_FALSE(f.undo.canUndo());
}

TEST_F(ModuleComponentTest, ThePicksOwnCardAndASourceAlreadyOnTheKnobAreNotPickable) {
    NoMotion motion;
    PickFixture f;
    f.startPick();
    auto* picker = f.panel->canvasPicker();
    ASSERT_NE(picker, nullptr);
    auto* lfoCard = findModuleComp(*f.editor, f.engine.getGraph().getNodeForId(f.lfoId)->getProcessor());
    ASSERT_NE(lfoCard, nullptr);

    EXPECT_EQ(picker->eligibleNodeAt(f.lfo2Card->getScreenBounds().getCentre()), f.lfo2);
    EXPECT_EQ(picker->eligibleNodeAt(f.vcaCard->getScreenBounds().getCentre()).uid, 0u) << "the knob's own card";
    EXPECT_EQ(picker->eligibleNodeAt(lfoCard->getScreenBounds().getCentre()).uid, 0u) << "already a source";

    f.pressOn(*f.vcaCard);
    f.pressOn(*lfoCard);
    EXPECT_TRUE(f.panel->isPicking()) << "a press on something that cannot be picked leaves the mode on";
    EXPECT_EQ(f.engine.getModulationRoutings().size(), 1u);
    EXPECT_FALSE(f.undo.canUndo());
}

TEST_F(ModuleComponentTest, EscapeStopsThePickWithoutClosingThePanel) {
    NoMotion motion;
    PickFixture f;
    int closed = 0;
    f.panel->onDismiss = [&] { ++closed; };
    f.startPick();
    ASSERT_TRUE(f.panel->isPicking());

    EXPECT_TRUE(f.panel->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));

    EXPECT_FALSE(f.panel->isPicking());
    EXPECT_EQ(closed, 0) << "the first Escape only stops the pick";
    EXPECT_TRUE(f.panel->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
    EXPECT_EQ(closed, 1);
}

TEST_F(ModuleComponentTest, EscapeOnTheLayerItselfStopsThePick) {
    NoMotion motion;
    PickFixture f;
    f.startPick();
    auto* picker = f.panel->canvasPicker();
    ASSERT_NE(picker, nullptr);
    EXPECT_TRUE(picker->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
    EXPECT_FALSE(f.panel->isPicking());
}

TEST_F(ModuleComponentTest, TheCanvasBeingRebuiltStopsThePick) {
    NoMotion motion;
    PickFixture f;
    f.startPick();
    ASSERT_TRUE(f.panel->isPicking());

    f.editor->detachAllModuleComponents(); // the seam every graph-replacing path goes through

    EXPECT_FALSE(f.panel->isPicking());
    f.refresh();
}

TEST_F(ModuleComponentTest, ThePickLayerIsNamedForAScreenReader) {
    NoMotion motion;
    PickFixture f;
    f.startPick();
    ASSERT_NE(f.panel->canvasPicker(), nullptr);
    EXPECT_EQ(f.panel->canvasPicker()->getTitle(), "Pick a source module");
    EXPECT_EQ(f.panel->sourcesPage().splitButton().pickHalf().getTitle(), "Pick on canvas, on");
}
