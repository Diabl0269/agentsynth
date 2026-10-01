// EffectsDefaultLayoutTests.cpp
//
// Source/UI/Graph/CardBody/DefaultLayouts/DefaultCardLayoutsEffects.cpp: the designed default card of
// Delay, Reverb, Chorus, Phaser, Flanger, Distortion, Bitcrusher, Ring Modulator and Pitch Shifter, read
// on real cards: the sections in order, the named widgets, the conditions (a swap keeps its cell), every
// control reachable by keyboard with a title and a tooltip, and the size estimate against the real card.

#include "../../GraphEditor/GraphEditorTestHelpers.h"
#include "../CardBodyTestHelpers.h"
#include "AI/AIStateMapper/AIStateMapper.h"
#include "UI/Graph/CardBody/CardBodyMeasure.h"
#include "UI/Graph/CardBody/DefaultLayouts/DefaultCardLayoutsFamilies.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

using namespace cardbody_test;
using Kind = synth::CardBodyItem::Kind;

namespace {

struct Expected {
    juce::String type;
    std::vector<std::vector<juce::String>> sections; ///< Param ids per section; the footer last.
    std::map<juce::String, Kind> kinds;
};

std::vector<Expected> expectations() {
    return {
        {"Delay",
         {{"tempoSync", "time", "timeDiv", "feedback", "mix"}, {"pingPong", "outputLevel"}},
         {{"tempoSync", Kind::Toggle},
          {"time", Kind::KnobLarge},
          {"timeDiv", Kind::Choice},
          {"pingPong", Kind::Toggle},
          {"outputLevel", Kind::FaderH}}},
        {"Reverb",
         {{"roomSize", "damping", "preDelay"}, {"dry", "wet", "width"}, {"outputLevel"}},
         {{"dry", Kind::FaderV}, {"wet", Kind::FaderV}, {"width", Kind::Knob}, {"outputLevel", Kind::FaderH}}},
        {"Chorus",
         {{"rate", "depth", "mix"}, {"centreDelay", "feedback"}, {"outputLevel"}},
         {{"rate", Kind::Knob}, {"outputLevel", Kind::FaderH}}},
        {"Flanger",
         {{"rate", "depth", "mix"}, {"centreDelay", "feedback"}, {"outputLevel"}},
         {{"centreDelay", Kind::Knob}, {"outputLevel", Kind::FaderH}}},
        {"Phaser",
         {{"rate", "depth", "mix"}, {"centreFreq", "feedback"}, {"outputLevel"}},
         {{"centreFreq", Kind::Knob}, {"outputLevel", Kind::FaderH}}},
        {"Distortion",
         {{"type"}, {"drive", "mix"}, {"oversampling", "outputLevel"}},
         {{"type", Kind::Segmented},
          {"drive", Kind::KnobLarge},
          {"oversampling", Kind::Choice},
          {"outputLevel", Kind::FaderH}}},
        {"Bitcrusher",
         {{"depth", "rate", "mix"}, {"dither", "outputLevel"}},
         {{"dither", Kind::FaderH}, {"outputLevel", Kind::FaderH}}},
        {"Ring Modulator",
         {{"drive", "character", "mix"}, {"oversampling", "outputLevel"}},
         {{"drive", Kind::Knob}, {"oversampling", Kind::Choice}, {"outputLevel", Kind::FaderH}}},
        {"Pitch Shifter",
         {{"shiftMode"}, {"pitch", "shiftHz", "fine", "mix"}, {"window", "feedback"}, {"outputLevel"}},
         {{"shiftMode", Kind::Segmented},
          {"pitch", Kind::KnobLarge},
          {"shiftHz", Kind::KnobLarge},
          {"outputLevel", Kind::FaderH}}},
    };
}

juce::Component* widget(CardCanvas& canvas, NodeID id, const juce::String& paramId) {
    return canvas.card(id)->getCardBody()->findWidget(paramId);
}

void flip(CardCanvas& canvas, NodeID id, const juce::String& paramId, float plainValue) {
    auto* param = findParameterByID(canvas.processor(id), paramId);
    ASSERT_NE(param, nullptr) << paramId;
    param->setValueNotifyingHost(param->convertTo0to1(plainValue));
    canvas.card(id)->getCardBody()->flushPendingConditionUpdate();
}

} // namespace

TEST(EffectsDefaultLayout, EachCardHasTheDesignedSectionsAndWidgetsInOrder) {
    for (const auto& expected : expectations()) {
        SCOPED_TRACE(expected.type.toStdString());
        CardCanvas canvas;
        const auto id = canvas.add(synth::AIStateMapper::createModule(expected.type), 0, 0);
        canvas.editor.updateComponents();
        const auto& plan = canvas.card(id)->getCardBody()->getPlan();
        ASSERT_EQ(plan.sections.size(), expected.sections.size());
        for (size_t s = 0; s < expected.sections.size(); ++s) {
            std::vector<juce::String> ids;
            for (int index : plan.sections[s].items)
                ids.push_back(plan.items[(size_t)index].param->paramID);
            EXPECT_EQ(ids, expected.sections[s]) << "section " << s;
        }
        EXPECT_TRUE(plan.sections.back().footer);
        for (const auto& [paramId, kind] : expected.kinds) {
            const int index = plan.findParam(paramId);
            ASSERT_GE(index, 0) << paramId;
            EXPECT_EQ(plan.items[(size_t)index].kind, kind) << paramId;
        }
        for (const auto& section : plan.sections)
            if (section.title.has_value())
                EXPECT_NE(*section.title, section.title->toUpperCase()) << "section titles are sentence case";
    }
}

TEST(EffectsDefaultLayout, EveryShownControlIsKeyboardReachableWithATitleAndATooltip) {
    for (const auto& expected : expectations()) {
        SCOPED_TRACE(expected.type.toStdString());
        CardCanvas canvas;
        const auto id = canvas.add(synth::AIStateMapper::createModule(expected.type), 0, 0);
        canvas.editor.updateComponents();
        auto* card = canvas.card(id);
        const auto tabbable = card->createKeyboardFocusTraverser()->getAllComponents(card);
        const auto& plan = card->getCardBody()->getPlan();
        for (const auto& item : plan.items) {
            if (item.param == nullptr || item.section < 0 || !item.shown ||
                !plan.sections[(size_t)item.section].visible)
                continue;
            SCOPED_TRACE(item.param->paramID.toStdString());
            auto* control = card->getCardBody()->findWidget(item.param->paramID);
            ASSERT_NE(control, nullptr);
            EXPECT_TRUE(control->isVisible());
            EXPECT_TRUE(control->getWantsKeyboardFocus());
            EXPECT_NE(std::find(tabbable.begin(), tabbable.end(), control), tabbable.end());
            EXPECT_TRUE(control->getTitle().isNotEmpty());
            auto* tip = dynamic_cast<juce::SettableTooltipClient*>(control);
            ASSERT_NE(tip, nullptr);
            EXPECT_TRUE(tip->getTooltip().isNotEmpty());
        }
    }
}

TEST(EffectsDefaultLayout, TheDelayTempoSyncSwapsTimeForItsDivisionInOneCell) {
    CardCanvas canvas;
    const auto id = canvas.add(synth::AIStateMapper::createModule("Delay"), 0, 0);
    canvas.editor.updateComponents();
    auto* card = canvas.card(id);
    auto* time = widget(canvas, id, "time");
    auto* division = widget(canvas, id, "timeDiv");
    ASSERT_NE(time, nullptr);
    ASSERT_NE(division, nullptr);
    const auto cardBounds = card->getBounds();
    const auto feedbackBounds = widget(canvas, id, "feedback")->getBounds();

    EXPECT_TRUE(time->isVisible()) << "free time is the default";
    EXPECT_FALSE(division->isVisible());
    flip(canvas, id, "tempoSync", 1.0f);
    EXPECT_TRUE(division->isVisible());
    EXPECT_FALSE(time->isVisible());
    EXPECT_EQ(division->getPosition(), time->getPosition()) << "the swap keeps its cell";
    EXPECT_EQ(card->getBounds(), cardBounds);
    EXPECT_EQ(widget(canvas, id, "feedback")->getBounds(), feedbackBounds);
    flip(canvas, id, "tempoSync", 0.0f);
    EXPECT_TRUE(time->isVisible());
    EXPECT_EQ(card->getBounds(), cardBounds);
}

TEST(EffectsDefaultLayout, ThePitchShifterFrequencyModeSwapsShiftForPitchAndDimsFine) {
    CardCanvas canvas;
    const auto id = canvas.add(synth::AIStateMapper::createModule("Pitch Shifter"), 0, 0);
    canvas.editor.updateComponents();
    auto* card = canvas.card(id);
    auto* pitch = widget(canvas, id, "pitch");
    auto* shift = widget(canvas, id, "shiftHz");
    auto* fine = widget(canvas, id, "fine");
    const auto cardBounds = card->getBounds();
    const auto fineBounds = fine->getBounds();
    EXPECT_TRUE(pitch->isVisible());
    EXPECT_FALSE(shift->isVisible());
    EXPECT_FALSE(synth::theme::AppLookAndFeel::paintsDimmed(*fine));

    flip(canvas, id, "shiftMode", 1.0f); // Frequency
    EXPECT_FALSE(pitch->isVisible());
    EXPECT_TRUE(shift->isVisible());
    EXPECT_EQ(shift->getPosition(), pitch->getPosition()) << "Shift takes the cell Pitch had";
    EXPECT_EQ(card->getBounds(), cardBounds) << "a swap and a dim never resize the card";
    EXPECT_EQ(fine->getBounds(), fineBounds) << "Fine keeps its cell";
    EXPECT_TRUE(fine->isVisible());
    EXPECT_TRUE(fine->isEnabled());
    EXPECT_TRUE(fine->getWantsKeyboardFocus()) << "a dimmed Fine is still a Tab stop";
    EXPECT_TRUE(synth::theme::AppLookAndFeel::paintsDimmed(*fine));
    EXPECT_TRUE(card->getCardBody()->getPlan().more.empty() ||
                std::count(card->getCardBody()->getPlan().more.begin(), card->getCardBody()->getPlan().more.end(),
                           card->getCardBody()->getPlan().findParam("shiftHz")) == 0);

    flip(canvas, id, "shiftMode", 0.0f);
    EXPECT_TRUE(pitch->isVisible());
    EXPECT_FALSE(shift->isVisible());
    EXPECT_FALSE(synth::theme::AppLookAndFeel::paintsDimmed(*fine));
    EXPECT_EQ(card->getBounds(), cardBounds);
}

TEST(EffectsDefaultLayout, TheEstimateEqualsTheRealCardForEveryEffect) {
    CardCanvas canvas;
    std::map<juce::String, NodeID> ids;
    int x = 0;
    for (const auto& expected : expectations()) {
        ids[expected.type] = canvas.add(synth::AIStateMapper::createModule(expected.type), x, 0);
        x += 400;
    }
    canvas.editor.updateComponents();
    for (const auto& [type, id] : ids) {
        SCOPED_TRACE(type.toStdString());
        const auto estimate = synth::measureDataDrivenCardSize(type);
        ASSERT_TRUE(estimate.has_value());
        auto* card = canvas.card(id);
        EXPECT_EQ(*estimate, juce::Point<int>(card->getWidth(), card->getHeight()));
    }
}
