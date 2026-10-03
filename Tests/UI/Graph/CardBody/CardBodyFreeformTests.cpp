// CardBodyFreeformTests.cpp
//
// Source/UI/Graph/CardBody/: a section whose items carry a free position (`at`) is placed by position
// (kept inside the content width, the card growing to fit) with its other cells flowing after it, one
// function still measures and places, a layout `range` narrows a knob's travel without touching the
// parameter, and a stored layout with positions survives the project load path.

#include "../GraphEditor/GraphEditorTestHelpers.h"
#include "AI/AIStateMapper/AIStateMapper.h"
#include "CardBodyTestHelpers.h"
#include "Modules/FilterModule.h"
#include "UI/Graph/CardBody/CardBodyGeometry.h"

using namespace cardbody_test;

namespace {

// The automatic Filter layout with `cutoff` and `resonance` given free positions.
synth::CardLayout positionedFilterLayout(juce::AudioProcessor& filter, juce::Point<int> cutoffAt,
                                         juce::Point<int> resonanceAt) {
    auto layout = automaticLayoutWith(filter, {});
    for (auto& item : layout.sections.front().items)
        if (auto* param = std::get_if<synth::CardParamItem>(&item)) {
            if (param->paramId == "cutoff")
                param->at = cutoffAt;
            else if (param->paramId == "resonance")
                param->at = resonanceAt;
        }
    return layout;
}

struct Built {
    CardCanvas canvas;
    NodeID id;
    ModuleComponent* card = nullptr;

    explicit Built(const synth::CardLayout& layout) {
        id = canvas.add(std::make_unique<FilterModule>(), 0, 0, layout);
        canvas.editor.updateComponents();
        card = canvas.card(id);
    }

    const synth::CardBodyItem& item(const juce::String& paramId) const {
        const auto& plan = card->getCardBody()->getPlan();
        return plan.items[(size_t)plan.findParam(paramId)];
    }
};

} // namespace

TEST(CardBodyFreeform, PositionedItemsSitAtTheirPositionAndTheCardGrowsToFitThem) {
    FilterModule probe;
    Built built(positionedFilterLayout(probe, {100, 40}, {0, 0}));
    ASSERT_NE(built.card, nullptr);
    const auto g = synth::cardbody::BodyGeometry::forCardWidth(built.card->getWidth());
    const auto& cutoff = built.item("cutoff");
    const auto& resonance = built.item("resonance");
    ASSERT_NE(cutoff.label, nullptr);
    ASSERT_NE(resonance.label, nullptr);

    EXPECT_EQ(cutoff.label->getX(), g.contentX + 100);
    EXPECT_EQ(resonance.label->getX(), g.contentX);
    EXPECT_EQ(cutoff.label->getY(), resonance.label->getY() + 40) << "y is from the section's top";
    EXPECT_EQ(cutoff.widget->getY(), cutoff.label->getBottom());
    EXPECT_LE(cutoff.widget->getBottom(), built.card->getHeight());
    EXPECT_TRUE(built.card->getLocalBounds().contains(cutoff.widget->getBounds()));
}

TEST(CardBodyFreeform, MeasuringAndPlacingAgreeOnTheSectionHeight) {
    FilterModule probe;
    Built built(positionedFilterLayout(probe, {100, 40}, {0, 0}));
    ASSERT_NE(built.card, nullptr);
    auto* body = built.card->getCardBody();
    const auto g = synth::cardbody::BodyGeometry::forCardWidth(built.card->getWidth());
    EXPECT_EQ(body->layout(100, g, /*apply*/ false), body->layout(100, g, /*apply*/ true));
    EXPECT_GT(body->layout(100, g, false), 100 + 40 + synth::cardbody::kLabelHeight + synth::cardbody::kKnobHeight);
}

TEST(CardBodyFreeform, APositionPastTheRightEdgeIsClampedInsideTheContentWidth) {
    FilterModule probe;
    Built built(positionedFilterLayout(probe, {4000, 0}, {0, 0}));
    ASSERT_NE(built.card, nullptr);
    const auto g = synth::cardbody::BodyGeometry::forCardWidth(built.card->getWidth());
    const auto& cutoff = built.item("cutoff");
    EXPECT_GE(cutoff.label->getX(), g.contentX);
    EXPECT_LE(cutoff.label->getRight(), g.contentX + g.contentW);
}

TEST(CardBodyFreeform, UnpositionedItemsFlowBelowThePositionedOnes) {
    FilterModule probe;
    Built built(positionedFilterLayout(probe, {100, 40}, {0, 0}));
    ASSERT_NE(built.card, nullptr);
    const int lowest = std::max(built.item("cutoff").widget->getBottom(), built.item("resonance").widget->getBottom());
    const auto& drive = built.item("drive");
    ASSERT_NE(drive.label, nullptr);
    EXPECT_GE(drive.label->getY(), lowest);
    EXPECT_TRUE(built.card->getLocalBounds().contains(drive.widget->getBounds()));
}

TEST(CardBodyFreeform, ALayoutWithoutPositionsStaysAnOrdinaryRunLayout) {
    FilterModule probe;
    Built built(automaticLayoutWith(probe, {}));
    ASSERT_NE(built.card, nullptr);
    for (const auto& section : built.card->getCardBody()->getPlan().sections)
        EXPECT_FALSE(section.freeform);
}

TEST(CardBodyFreeform, ALayoutRangeNarrowsTheKnobButNotTheParameter) {
    FilterModule probe;
    auto layout = automaticLayoutWith(probe, {});
    for (auto& item : layout.sections.front().items)
        if (auto* param = std::get_if<synth::CardParamItem>(&item); param != nullptr && param->paramId == "cutoff")
            param->range = juce::Range<double>(200.0, 2000.0);
    Built built(layout);
    ASSERT_NE(built.card, nullptr);

    auto* knob = dynamic_cast<juce::Slider*>(built.card->getCardBody()->findWidget("cutoff"));
    ASSERT_NE(knob, nullptr);
    EXPECT_DOUBLE_EQ(knob->getMinimum(), 200.0);
    EXPECT_DOUBLE_EQ(knob->getMaximum(), 2000.0);

    auto* cutoff = findParameterByID(built.canvas.processor(built.id), "cutoff");
    ASSERT_NE(cutoff, nullptr);
    cutoff->setValueNotifyingHost(cutoff->convertTo0to1(5000.0f));
    EXPECT_NEAR(cutoff->convertFrom0to1(cutoff->getValue()), 5000.0f, 1.0f) << "the parameter keeps its value";
    EXPECT_DOUBLE_EQ(knob->getValue(), 2000.0) << "the knob only clamps what it shows";
}

TEST(CardBodyFreeform, AStoredLayoutWithPositionsOpensWithTheKnobAtThatPosition) {
    const auto json = juce::JSON::parse(R"({"nodes":[{"id":1,"type":"Filter","x":300,"y":200,"cardLayout":
        {"version":2,"sections":[{"id":"main","items":[
            {"paramId":"resonance","x":0,"y":0},
            {"paramId":"cutoff","x":100,"y":40,"min":200,"max":2000}]}],"hidden":[]}}]})");
    CardCanvas canvas;
    ASSERT_TRUE(synth::AIStateMapper::applyJSONToGraph(json, canvas.engine.getGraph(), /*clearExisting=*/true,
                                                       /*trusted=*/true));
    canvas.editor.updateComponents();
    ASSERT_EQ(canvas.editor.getModuleComponents().size(), 1);
    auto* card = canvas.editor.getModuleComponents().getFirst();
    ASSERT_NE(card, nullptr);
    ASSERT_NE(card->getCardBody(), nullptr);

    const auto& plan = card->getCardBody()->getPlan();
    const auto& cutoff = plan.items[(size_t)plan.findParam("cutoff")];
    const auto& resonance = plan.items[(size_t)plan.findParam("resonance")];
    ASSERT_NE(cutoff.label, nullptr);
    const auto g = synth::cardbody::BodyGeometry::forCardWidth(card->getWidth());
    EXPECT_EQ(cutoff.label->getX(), g.contentX + 100);
    EXPECT_EQ(cutoff.label->getY(), resonance.label->getY() + 40);
    EXPECT_DOUBLE_EQ(static_cast<juce::Slider*>(cutoff.widget)->getMaximum(), 2000.0);
}
