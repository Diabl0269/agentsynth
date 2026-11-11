// DrumKitPadsRowTests.cpp
// The Pads row of the Drum Kit card reads label | Show all chip | count stepper: the chip sits a fixed gap after the
// title whatever the pads' state, the row clears the Level box above it, and Tab visits the chip before the stepper.
//
// Kinds: layout-overlap (the row used to touch the Level value box; the chip lived under the grid and moved).

#include "../CardBody/CardCanvas.h"
#include "Modules/DrumKit/DrumKitModule.h"
#include "UI/Graph/CardBody/CardBodyGeometry.h"
#include "UI/Graph/ModuleComponent/SamplerChrome/DrumKitChrome.h"
#include <algorithm>
#include <gtest/gtest.h>

using namespace cardbody_test;
using synth::ui::DrumKitChrome;
using synth::ui::DrumKitPadsSection;

namespace {

struct RowScene {
    CardCanvas canvas;
    NodeID id;

    RowScene() {
        id = canvas.add(std::make_unique<DrumKitModule>(), 0, 0);
        canvas.editor.updateComponents();
    }
    DrumKitModule& kit() { return *dynamic_cast<DrumKitModule*>(canvas.processor(id)); }
    ModuleComponent& card() { return *canvas.card(id); }
    DrumKitPadsSection& pads() { return dynamic_cast<DrumKitChrome*>(card().getInstrumentChrome())->getPads(); }

    juce::Component* child(const juce::String& componentId) {
        for (auto* c : card().getChildren())
            if (c->getComponentID() == componentId)
                return c;
        return nullptr;
    }
    juce::Label* padsLabel() {
        for (auto* c : card().getChildren())
            if (c->getName() == "Pads label")
                return dynamic_cast<juce::Label*>(c);
        return nullptr;
    }
};

} // namespace

TEST(DrumKitPadsRow, TheShowAllChipStaysAtOneXInEveryStateOfThePads) {
    RowScene scene;
    auto& chip = scene.pads().showAllChip();
    const int x = chip.getX();
    EXPECT_GT(x, scene.padsLabel()->getRight() - 1) << "the chip is after the title";
    scene.pads().setPadShown(2, false);
    EXPECT_TRUE(chip.isVisible());
    EXPECT_EQ(chip.getX(), x) << "hidden pad: the chip appears where it will stay";
    scene.pads().showAllChip().onClick();
    EXPECT_TRUE(chip.getToggleState());
    EXPECT_EQ(chip.getX(), x) << "revealed (lit): the chip does not move";
    scene.pads().showAllChip().onClick();
    EXPECT_EQ(chip.getX(), x) << "hidden again";
    scene.pads().setPadCountUndoably(32);
    EXPECT_EQ(chip.getX(), x) << "a different pad count does not move it";
}

TEST(DrumKitPadsRow, TheChipSitsOnThePadsRowAboveTheGridAndTheCaptionStaysBelow) {
    RowScene scene;
    scene.pads().setPadShown(2, false);
    auto& chip = scene.pads().showAllChip();
    auto* stepper = scene.child("drumPadCount");
    auto* grid = scene.child("drumPads");
    auto* label = scene.padsLabel();
    ASSERT_NE(stepper, nullptr);
    ASSERT_NE(grid, nullptr);
    ASSERT_NE(label, nullptr);
    EXPECT_LE(label->getRight(), chip.getX());
    EXPECT_LE(chip.getRight(), stepper->getX());
    EXPECT_GE(chip.getY(), stepper->getY());
    EXPECT_LE(chip.getBottom(), stepper->getBottom()) << "inside the Pads row";
    EXPECT_LE(chip.getBottom(), grid->getY());
    EXPECT_GE(scene.pads().caption().getY(), grid->getBottom()) << "the count caption stays under the grid";
    EXPECT_EQ(scene.pads().caption().getText(), "1 hidden");
}

TEST(DrumKitPadsRow, TheLevelBoxClearsThePadCountStepperByTheBlockGap) {
    RowScene scene;
    auto* level = dynamic_cast<juce::Slider*>(scene.child("Level"));
    auto* stepper = scene.child("drumPadCount");
    ASSERT_NE(level, nullptr);
    ASSERT_NE(stepper, nullptr);
    EXPECT_GE(stepper->getY() - level->getBottom(), synth::cardbody::kBlockGap);
    EXPECT_GE(scene.padsLabel()->getY() - level->getBottom(), synth::cardbody::kBlockGap);
}

TEST(DrumKitPadsRow, TabVisitsTheChipBeforeTheCountStepper) {
    RowScene scene;
    scene.pads().setPadShown(2, false);
    const auto order = scene.card().createKeyboardFocusTraverser()->getAllComponents(&scene.card());
    const auto at = [&](juce::Component* c) { return std::find(order.begin(), order.end(), c) - order.begin(); };
    auto* stepper = scene.child("drumPadCount");
    ASSERT_NE(stepper, nullptr);
    ASSERT_NE(std::find(order.begin(), order.end(), &scene.pads().showAllChip()), order.end());
    EXPECT_LT(at(&scene.pads().showAllChip()), at(stepper));
    EXPECT_LT(at(stepper), at(scene.child("drumPads"))) << "then the pad grid";
    EXPECT_FALSE(scene.padsLabel()->getWantsKeyboardFocus()) << "the title is not a stop";
}
