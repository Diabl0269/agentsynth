// CardLayoutEditorE2ETests.cpp
//
// One story end to end through the real handlers: add a Filter, open Layout List... from a control's
// right-click menu, hide Drive, rename Cutoff to Freq, make Level a fader, close the editor, save the
// project (graphToJSON), load it into a fresh canvas (applyJSONToGraph, trusted, as a project load
// does) and find the same card there.

#include "AI/AIStateMapper/AIStateMapper.h"
#include "CardLayoutEditorTestHelpers.h"
#include "Modules/FilterModule.h"
#include "UI/Graph/CardWidgets/CardFader.h"

using namespace cardlayouteditor_test;

namespace {

NodeID onlyFilter(cardbody_test::CardCanvas& canvas) {
    for (auto* node : canvas.engine.getGraph().getNodes())
        if (dynamic_cast<FilterModule*>(node->getProcessor()) != nullptr)
            return node->nodeID;
    return {};
}

} // namespace

TEST(CardLayoutEditorE2E, AnEditedFilterCardSurvivesSaveAndReload) {
    juce::var saved;
    juce::StringArray visibleBefore;
    {
        EditorCanvas rig;
        const auto id = rig.add(std::make_unique<FilterModule>(), 200, 200);
        auto* editor = rig.openFromControl(id, "cutoff");
        ASSERT_NE(editor, nullptr);

        editor->triggerRowToggleForTest(rowOf(*editor, "drive"));
        editor->setRowLabelForTest(rowOf(*editor, "cutoff"), "Freq");
        editor->commitRowLabelForTest(rowOf(*editor, "cutoff"));
        editor->selectWidgetForTest(rowOf(*editor, "outputLevel"), synth::CardWidget::FaderV);
        rig.close();

        auto* card = rig.card(id);
        for (const auto& item : card->getCardBody()->getPlan().items)
            if (item.widget != nullptr && item.widget->isVisible() && item.param != nullptr)
                visibleBefore.add(item.param->paramID);
        saved = synth::AIStateMapper::graphToJSON(rig.canvas.engine.getGraph());
    }
    ASSERT_TRUE(juce::JSON::toString(saved).contains("cardLayout")) << "the project carries the layout";

    EditorCanvas reopened;
    ASSERT_TRUE(synth::AIStateMapper::applyJSONToGraph(saved, reopened.canvas.engine.getGraph(), true, true, false));
    reopened.canvas.editor.updateComponents();
    const auto id = onlyFilter(reopened.canvas);
    auto* card = reopened.card(id);
    ASSERT_NE(card, nullptr);
    auto* body = card->getCardBody();

    EXPECT_TRUE(body->hasMoreRow());
    EXPECT_FALSE(body->findWidget("drive")->isVisible()) << "Drive is still hidden";
    EXPECT_EQ(captionOf(*card, "cutoff"), "Freq");
    EXPECT_NE(dynamic_cast<synth::ui::CardFader*>(body->findWidget("outputLevel")), nullptr) << "Level is a fader";
    juce::StringArray visibleAfter;
    for (const auto& item : body->getPlan().items)
        if (item.widget != nullptr && item.widget->isVisible() && item.param != nullptr)
            visibleAfter.add(item.param->paramID);
    EXPECT_EQ(visibleAfter, visibleBefore);
}

TEST(CardLayoutEditorE2E, ApplyToAllThenASecondFilterAddedLaterShowsTheSameLayout) {
    EditorCanvas rig;
    const auto first = rig.add(std::make_unique<FilterModule>(), 0, 0);
    auto* editor = rig.openFromModuleMenu(first);
    ASSERT_NE(editor, nullptr);
    editor->setApplyToAllInstancesForTest(true);
    editor->triggerRowToggleForTest(rowOf(*editor, "drive"));
    editor->setRowLabelForTest(rowOf(*editor, "cutoff"), "Freq");
    editor->commitRowLabelForTest(rowOf(*editor, "cutoff"));
    rig.close();

    const auto second = rig.add(std::make_unique<FilterModule>(), 0, 700);
    auto* card = rig.card(second);
    EXPECT_FALSE(card->getCardBody()->findWidget("drive")->isVisible());
    EXPECT_EQ(captionOf(*card, "cutoff"), "Freq");
    EXPECT_FALSE(rig.storedLayout(second).has_value()) << "it follows the type's default, with no copy of its own";
}
