// SequencerDefaultLayoutTests.cpp
//
// The designed default card layout of the Sequencer and the Poly Sequencer (Source/UI/Graph/CardBody/DefaultLayouts/
// DefaultCardLayoutsSequencers.cpp): the step grid on top, a mode row with Run, then the Pitch, Shape and Time lanes.
// BPM dims while Follow transport is on and never hides, so the card keeps its size and every control its place; Tab
// goes lane by lane, column first. docs/layout/module-card-layout.md#default-layouts.

#include "../../GraphEditor/GraphEditorTestHelpers.h"
#include "../CardBodyTestHelpers.h"
#include "AI/AIStateMapper/AIStateMapper.h"
#include "UI/Graph/CardBody/DefaultCardLayouts.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

using namespace cardbody_test;
using Kind = synth::CardBodyItem::Kind;

namespace {

const juce::StringArray kTypes{"Sequencer", "Poly Sequencer"};

// A section of the plan as text: each parameter's id, a view as "view:<name>", in card order.
juce::StringArray sectionItems(const synth::CardBodyPlan& plan, int section) {
    juce::StringArray items;
    for (int index : plan.sections[(size_t)section].items) {
        const auto& item = plan.items[(size_t)index];
        items.add(item.kind == Kind::View ? juce::String("view") : item.param->paramID);
    }
    return items;
}

// Sets a parameter as automation would, then runs the card's queued condition re-read.
void flip(CardCanvas& canvas, NodeID id, const juce::String& paramId, float plainValue) {
    auto* param = findParameterByID(canvas.processor(id), paramId);
    ASSERT_NE(param, nullptr) << paramId;
    param->setValueNotifyingHost(param->convertTo0to1(plainValue));
    canvas.card(id)->getCardBody()->flushPendingConditionUpdate();
}

} // namespace

TEST(SequencerDefaultLayout, BothCardsAreCodeDefaultsAtTheLaneRevision) {
    for (const auto& type : kTypes) {
        const auto* entry = synth::DefaultCardLayouts::builtIn().find(type);
        ASSERT_NE(entry, nullptr) << type;
        EXPECT_EQ(entry->defaultRevision, 3) << type << " (revision 3 is the lane grid)";
    }
}

TEST(SequencerDefaultLayout, TheStepGridIsOnTopThenARunModeRowThenPitchShapeAndTimeLanes) {
    for (const auto& type : kTypes) {
        SCOPED_TRACE(type.toStdString());
        CardCanvas canvas;
        const auto id = canvas.add(synth::AIStateMapper::createModule(type), 0, 0);
        canvas.editor.updateComponents();
        const auto& plan = canvas.card(id)->getCardBody()->getPlan();
        ASSERT_EQ(plan.sections.size(), 5u);
        EXPECT_EQ(sectionItems(plan, 0), juce::StringArray({"view"})) << "the step grid is the View zone";
        EXPECT_EQ(sectionItems(plan, 1), juce::StringArray({"run", "syncToTransport"}));
        EXPECT_TRUE(plan.sections[1].mode);
        EXPECT_EQ(sectionItems(plan, 2), juce::StringArray({"scaleRoot", "transpose"}));
        EXPECT_EQ(sectionItems(plan, 3), juce::StringArray({"gate", "velocity", "chance"}));
        EXPECT_EQ(sectionItems(plan, 4), juce::StringArray({"bpm", "rate", "steps"}));
        for (int lane : {2, 3, 4})
            EXPECT_TRUE(plan.sections[(size_t)lane].lane) << lane;
        EXPECT_EQ(plan.sections[2].title, juce::String("Pitch"));
        EXPECT_EQ(plan.sections[3].title, juce::String("Shape"));
        EXPECT_EQ(plan.sections[4].title, juce::String("Time"));
        EXPECT_FALSE(plan.sections.back().outLane) << "a sequencer has no level, so no Out lane";
        EXPECT_FALSE(plan.hasFooter());
        EXPECT_EQ(plan.items[(size_t)plan.findParam("bpm")].kind, Kind::Knob) << "BPM is a knob, not a fader";
        EXPECT_EQ(plan.items[(size_t)plan.findParam("syncToTransport")].kind, Kind::Toggle);
        EXPECT_TRUE(plan.items[(size_t)plan.findParam("syncToTransport")].pill);
        EXPECT_EQ(canvas.card(id)->getWidth(), 560) << "the step grid's width";
    }
}

// Regression test for the old layout: BPM was hidden while Follow transport was on, so the Play row shrank and the
// card moved. It now dims in place.
TEST(SequencerDefaultLayout, FollowTransportDimsBpmInPlaceAndTheCardNeverMoves) {
    for (const auto& type : kTypes) {
        SCOPED_TRACE(type.toStdString());
        CardCanvas canvas;
        const auto id = canvas.add(synth::AIStateMapper::createModule(type), 0, 0);
        canvas.editor.updateComponents();
        auto* card = canvas.card(id);
        auto* body = card->getCardBody();
        auto* bpm = body->findWidget("bpm");
        ASSERT_NE(bpm, nullptr);
        const auto cardBounds = card->getBounds();
        const auto bpmBounds = bpm->getBounds();
        const auto gateBounds = body->findWidget("gate")->getBounds();
        const auto rateBounds = body->findWidget("rate")->getBounds();
        EXPECT_FALSE(synth::theme::AppLookAndFeel::paintsDimmed(*bpm));

        flip(canvas, id, "syncToTransport", 1.0f);
        EXPECT_TRUE(bpm->isVisible()) << "dimmed, not hidden";
        EXPECT_TRUE(synth::theme::AppLookAndFeel::paintsDimmed(*bpm));
        EXPECT_EQ(card->getBounds(), cardBounds);
        EXPECT_EQ(bpm->getBounds(), bpmBounds);
        EXPECT_EQ(body->findWidget("gate")->getBounds(), gateBounds);
        EXPECT_EQ(body->findWidget("rate")->getBounds(), rateBounds);

        flip(canvas, id, "syncToTransport", 0.0f);
        EXPECT_FALSE(synth::theme::AppLookAndFeel::paintsDimmed(*bpm));
        EXPECT_EQ(card->getBounds(), cardBounds);
    }
}

TEST(SequencerDefaultLayout, TabGoesRunThenPitchShapeAndTimeColumnFirst) {
    for (const auto& type : kTypes) {
        SCOPED_TRACE(type.toStdString());
        CardCanvas canvas;
        const auto id = canvas.add(synth::AIStateMapper::createModule(type), 0, 0);
        canvas.editor.updateComponents();
        auto* card = canvas.card(id);
        juce::StringArray order;
        for (auto* stop : card->getKeyboardControls())
            if (stop->getTitle().isNotEmpty())
                order.add(stop->getTitle());
        const auto at = [&](const juce::String& title) {
            for (int i = 0; i < order.size(); ++i)
                if (order[i].startsWith(title))
                    return i;
            return -1;
        };
        for (const char* title :
             {"Run", "Transpose", "Gate", "Velocity", "Chance", "Follow transport", "BPM", "Rate", "Steps"})
            ASSERT_GE(at(title), 0) << title << " in " << order.joinIntoString(" | ");
        EXPECT_LT(at("Run"), at("Follow transport")) << "the mode row first";
        EXPECT_LT(at("Follow transport"), at("Transpose"));
        EXPECT_LT(at("Transpose"), at("Gate"));
        EXPECT_LT(at("Gate"), at("Velocity"));
        EXPECT_LT(at("Velocity"), at("Chance"));
        EXPECT_LT(at("Chance"), at("BPM")) << "Pitch, Shape, then Time";
        EXPECT_LT(at("BPM"), at("Rate"));
        EXPECT_LT(at("Rate"), at("Steps"));
    }
}

TEST(SequencerDefaultLayout, EveryControlIsKeyboardReachableWithATitleAndATooltip) {
    for (const auto& type : kTypes) {
        SCOPED_TRACE(type.toStdString());
        CardCanvas canvas;
        const auto id = canvas.add(synth::AIStateMapper::createModule(type), 0, 0);
        canvas.editor.updateComponents();
        for (auto& item : canvas.card(id)->getCardBody()->getPlan().items) {
            if (item.kind == Kind::View || item.widget == nullptr)
                continue;
            SCOPED_TRACE(item.param->paramID.toStdString());
            EXPECT_TRUE(item.widget->getTitle().isNotEmpty());
            auto* tip = dynamic_cast<juce::SettableTooltipClient*>(item.widget);
            ASSERT_NE(tip, nullptr);
            EXPECT_TRUE(tip->getTooltip().isNotEmpty());
        }
    }
}

TEST(SequencerDefaultLayout, TheEstimateEqualsTheRealCard) {
    for (const auto& type : kTypes) {
        SCOPED_TRACE(type.toStdString());
        CardCanvas canvas;
        const auto id = canvas.add(synth::AIStateMapper::createModule(type), 0, 0);
        canvas.editor.updateComponents();
        auto* card = canvas.card(id);
        EXPECT_EQ(GraphEditor::estimateModuleSize(type), juce::Point<int>(card->getWidth(), card->getHeight()));
    }
}
