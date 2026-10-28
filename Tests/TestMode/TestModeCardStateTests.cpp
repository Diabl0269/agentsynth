// A module card is findable by a stable id and reports whether it is selected, through the StateSource seam.

#include "../Macros/MacroContainer/MacroContainerTestHelpers.h"
#include "AppUndoManager.h"
#include "Modules/ADSRModule.h"
#include "Modules/LFOModule.h"
#include "Modules/OscillatorModule.h"
#include "TestMode/TestModeComponents.h"
#include "TestMode/TestModeInput.h"
#include "TestModeRig.h"
#include <gtest/gtest.h>

using namespace synth::testmode;

namespace {

struct CardRig : testmode_rig::Rig {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor{engine, &undo};

    CardRig() {
        undo.setGraphEditor(&editor);
        addAndMakeVisible(editor);
        editor.setBounds(0, 0, 1200, 800);
        setSize(1200, 800);
    }
};

} // namespace

TEST(TestModeCardState, CardsAreNamedByTitleAndSelectionIsReported) {
    CardRig rig;
    const auto a = addModuleAt(rig.editor, rig.engine, std::make_unique<LFOModule>(), 300, 200);
    const auto b = addModuleAt(rig.editor, rig.engine, std::make_unique<LFOModule>(), 700, 200);
    auto* cardA = findComponent(rig.editor, a);
    auto* cardB = findComponent(rig.editor, b);
    ASSERT_NE(cardA, nullptr);
    ASSERT_NE(cardB, nullptr);
    EXPECT_TRUE(cardA->getComponentID().startsWith("card:"));
    // The id is the card's title, which the app numbers per type ("LFO 1", "LFO 2"); this helper adds raw modules.
    EXPECT_EQ(cardA->getComponentID(), "card:" + cardA->getTitle());
    EXPECT_EQ(cardB->getComponentID(), "card:" + cardB->getTitle());

    EXPECT_FALSE(static_cast<bool>(describeState(*cardA)["selected"]));
    rig.editor.setSelectedNodes({a});
    const auto state = describeState(*cardA);
    EXPECT_TRUE(static_cast<bool>(state["selected"]));
    EXPECT_EQ(static_cast<int>(state["nodeId"]), static_cast<int>(a.uid));
    EXPECT_EQ(state["title"].toString(), cardA->cardTitle());
    EXPECT_TRUE(state["moduleType"].toString().isNotEmpty());
    EXPECT_FALSE(static_cast<bool>(describeState(*cardB)["selected"]));
}

TEST(TestModeCardState, ACardIsFoundByItsIdOnAWindow) {
    CardRig rig;
    if (!rig.show())
        GTEST_SKIP() << "no native window in this environment";
    const auto a = addModuleAt(rig.editor, rig.engine, std::make_unique<OscillatorModule>(), 300, 200);
    auto* card = findComponent(rig.editor, a);
    ASSERT_NE(card, nullptr);
    EXPECT_EQ(findComponent(card->getComponentID()).component, card);
}

TEST(TestModeCardState, ARealClickOnACardSelectsIt) {
    CardRig rig;
    if (!rig.show())
        GTEST_SKIP() << "no native window in this environment";
    const auto a = addModuleAt(rig.editor, rig.engine, std::make_unique<ADSRModule>(), 300, 200);
    auto* card = findComponent(rig.editor, a);
    ASSERT_NE(card, nullptr);
    rig.editor.setSelectedNodes({});
    ASSERT_FALSE(static_cast<bool>(describeState(*card)["selected"]));

    juce::String error;
    ASSERT_TRUE(click(*card, {card->getWidth() / 2, ModuleComponent::kHeaderHeight / 2}, Button::left, 1,
                      juce::ModifierKeys(), error))
        << error;
    EXPECT_TRUE(static_cast<bool>(describeState(*card)["selected"]));
}
