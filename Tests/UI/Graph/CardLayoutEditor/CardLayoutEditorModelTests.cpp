// CardLayoutEditorModelTests.cpp
//
// Source/UI/Graph/CardLayoutEditor/CardLayoutEditorModel: the editor's working layout with no UI, in
// both of its modes -- hidden rows that stay in place (a built-in card) and rows that leave the
// layout when unticked (a hosted plugin).

#include "UI/Graph/CardLayoutEditor/CardLayoutEditorModel.h"
#include <gtest/gtest.h>

using synth::CardLayout;
using synth::CardParamItem;
using synth::CardSection;
using synth::ui::CardLayoutEditorModel;
using synth::ui::CardLayoutEditorParam;
using HiddenRows = synth::ui::CardLayoutEditorSource::HiddenRows;

namespace {

std::vector<CardLayoutEditorParam> params(std::initializer_list<const char*> ids) {
    std::vector<CardLayoutEditorParam> out;
    int index = 0;
    for (const auto* id : ids)
        out.push_back(
            {id, juce::String(id).toUpperCase().substring(0, 1) + juce::String(id).substring(1), index++, {}});
    return out;
}

CardLayout sectionsOf(std::vector<std::vector<const char*>> groups) {
    CardLayout layout;
    int n = 0;
    for (const auto& group : groups) {
        CardSection section;
        section.id = "s" + juce::String(n++);
        for (const auto* id : group) {
            CardParamItem item;
            item.paramId = id;
            section.items.emplace_back(item);
        }
        layout.sections.push_back(section);
    }
    return layout;
}

juce::StringArray keys(const CardLayoutEditorModel& model, const juce::String& search = {}) {
    juce::StringArray out;
    for (const auto& row : model.rows(search))
        out.add(row.key);
    return out;
}

} // namespace

TEST(CardLayoutEditorModel, LoadingPlacesEveryParameterOnceAndSetsUnknownOnesAside) {
    CardLayoutEditorModel model(params({"a", "b", "c"}), HiddenRows::StayInPlace, true);
    model.load(sectionsOf({{"a", "gone", "a"}}));
    EXPECT_EQ(keys(model), juce::StringArray("#0", "a", "b", "c"));
    const auto layout = model.toLayout();
    ASSERT_EQ(layout.sections.size(), 1u);
    EXPECT_EQ(layout.sections[0].items.size(), 3u) << "a once, then b and c appended";
    EXPECT_TRUE(layout.hidden.contains("b")) << "a parameter the layout never placed stays in the More row";
    EXPECT_TRUE(layout.hidden.contains("c"));
    EXPECT_EQ(model.missingNames(), juce::StringArray("gone"));
}

TEST(CardLayoutEditorModel, UntickingInPlaceKeepsTheRowWhereItIs) {
    CardLayoutEditorModel model(params({"a", "b", "c"}), HiddenRows::StayInPlace, true);
    model.load(sectionsOf({{"a", "b", "c"}}));
    model.setShown("b", false);
    EXPECT_EQ(keys(model), juce::StringArray("#0", "a", "b", "c"));
    EXPECT_FALSE(model.isShown("b"));
    EXPECT_TRUE(model.toLayout().hidden.contains("b"));
    model.setShown("b", true);
    EXPECT_TRUE(model.toLayout().hidden.isEmpty());
}

TEST(CardLayoutEditorModel, UntickingAHostedRowTakesItOutAndTickingAppendsIt) {
    CardLayoutEditorModel model(params({"a", "b", "c"}), HiddenRows::LeaveTheLayout, false);
    model.load(sectionsOf({{"c"}}));
    EXPECT_EQ(keys(model), juce::StringArray("c", "a", "b")) << "ticked first, then the rest in order";
    model.setShown("a", true);
    EXPECT_EQ(keys(model), juce::StringArray("c", "a", "b"));
    model.setShown("c", false);
    EXPECT_EQ(keys(model), juce::StringArray("a", "b", "c"));
    const auto layout = model.toLayout();
    ASSERT_EQ(layout.sections[0].items.size(), 1u);
    EXPECT_EQ(std::get<CardParamItem>(layout.sections[0].items[0]).indexHint, 0) << "the hosted rescue key";
}

TEST(CardLayoutEditorModel, MovingByOneCrossesIntoTheNeighbouringGroupAtItsEdge) {
    CardLayoutEditorModel model(params({"a", "b", "c"}), HiddenRows::StayInPlace, true);
    model.load(sectionsOf({{"a", "b"}, {"c"}}));
    EXPECT_TRUE(model.moveBy("b", 1));
    EXPECT_EQ(keys(model), juce::StringArray("#0", "a", "#1", "b", "c"));
    EXPECT_TRUE(model.moveBy("b", -1));
    EXPECT_EQ(keys(model), juce::StringArray("#0", "a", "b", "#1", "c"));
    EXPECT_FALSE(model.moveBy("a", -1)) << "nothing above the first group";
}

TEST(CardLayoutEditorModel, ADropUnderAHeaderJoinsThatGroupAndHiddenRowsKeepTheirPlaces) {
    CardLayoutEditorModel model(params({"a", "b", "c", "d"}), HiddenRows::StayInPlace, true);
    model.load(sectionsOf({{"a", "b", "c"}, {"d"}}));
    model.dropAt("a", {"#0", "b", "#1", "a", "d"});
    EXPECT_EQ(keys(model), juce::StringArray("#0", "b", "c", "#1", "a", "d"));
    // A search showing only b and d: b dropped after d lands after d, c (hidden by the search) stays put.
    model.dropAt("b", {"#0", "#1", "a", "d", "b"});
    EXPECT_EQ(keys(model), juce::StringArray("#0", "c", "#1", "a", "d", "b"));
    model.dropAt("c", {"c", "#0", "#1"});
    EXPECT_EQ(keys(model, {}).indexOf("c"), 1) << "above the first header: first in the first group";
}

TEST(CardLayoutEditorModel, LabelsTitlesWidgetsAndGroups) {
    CardLayoutEditorModel model(params({"a", "b"}), HiddenRows::StayInPlace, true);
    model.load(sectionsOf({{"a", "b"}}));
    model.setLabel("a", "  Alpha one ");
    model.setLabel("b", "B"); // the parameter's own name clears the override
    model.setWidget("a", synth::CardWidget::FaderV);
    const int group = model.addGroup();
    model.setSectionTitle(group, " Tone ");
    const auto layout = model.toLayout();
    EXPECT_EQ(std::get<CardParamItem>(layout.sections[0].items[0]).label, juce::String("Alpha one"));
    EXPECT_FALSE(std::get<CardParamItem>(layout.sections[0].items[1]).label.has_value());
    EXPECT_EQ(std::get<CardParamItem>(layout.sections[0].items[0]).widget, synth::CardWidget::FaderV);
    ASSERT_EQ(layout.sections.size(), 2u);
    EXPECT_EQ(layout.sections[1].title, juce::String("Tone"));
    EXPECT_NE(layout.sections[1].id, layout.sections[0].id);
    model.setSectionTitle(group, "");
    EXPECT_FALSE(model.toLayout().sections[1].title.has_value());
}

TEST(CardLayoutEditorModel, SearchMatchesDisplayNamesCaseBlind) {
    CardLayoutEditorModel model(params({"cutoff", "res"}), HiddenRows::LeaveTheLayout, false);
    model.load(sectionsOf({{"res"}}));
    EXPECT_EQ(keys(model, "CUT"), juce::StringArray("cutoff"));
}
