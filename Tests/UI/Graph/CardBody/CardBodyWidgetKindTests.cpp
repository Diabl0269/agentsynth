// CardBodyWidgetKindTests.cpp
//
// Source/UI/Graph/CardBody/ with the widgets a layout may choose (large knob, faders, segmented switch,
// stepper): the plan honours ParamItem.widget where it suits the parameter and falls back where it does
// not, each kind lays out at its own size, a plan never built measures exactly what the live card
// places, no value change resizes the card, and the automatic layout written out as explicit items
// builds the very same card (what the quick path starts from).

#include "AI/AIStateMapper/AIStateMapper.h"
#include "CardBodyTestHelpers.h"
#include "Modules/OscillatorModule.h"
#include "UI/Graph/CardBody/CardBodyLayoutWalk.h"
#include "UI/Graph/CardWidgets/CardFader.h"
#include "UI/Graph/CardWidgets/CardSegmentedSwitch.h"
#include "UI/Graph/CardWidgets/CardStepper.h"
#include "UI/Graph/ModuleComponent/CardKnobSlider.h"

using namespace cardbody_test;
using synth::CardBodyItem;
using synth::CardWidget;

namespace {

// Oscillator: a choice (waveform), small ints (octave, coarse, unison) and floats (fine, level, pan...).
std::map<juce::String, CardWidget> everyNewKind() {
    return {{"waveform", CardWidget::Segmented},
            {"octave", CardWidget::Stepper},
            {"fine", CardWidget::KnobLarge},
            {"level", CardWidget::FaderH},
            {"detune", CardWidget::FaderV}};
}

CardBodyItem::Kind kindOf(const synth::CardBodyPlan& plan, const juce::String& paramId) {
    return plan.items[(size_t)plan.findParam(paramId)].kind;
}

} // namespace

TEST(CardBodyWidgetKind, ThePlanHonoursEachWidgetThatSuitsItsParameter) {
    OscillatorModule osc;
    const auto plan = synth::CardBodyPlan::forModule(osc, automaticLayoutWith(osc, everyNewKind()));
    EXPECT_EQ(kindOf(plan, "waveform"), CardBodyItem::Kind::Segmented);
    EXPECT_EQ(kindOf(plan, "octave"), CardBodyItem::Kind::Stepper);
    EXPECT_EQ(kindOf(plan, "fine"), CardBodyItem::Kind::KnobLarge);
    EXPECT_EQ(kindOf(plan, "level"), CardBodyItem::Kind::FaderH);
    EXPECT_EQ(kindOf(plan, "detune"), CardBodyItem::Kind::FaderV);
    EXPECT_EQ(kindOf(plan, "coarse"), CardBodyItem::Kind::Knob) << "auto stays what it always was";
}

TEST(CardBodyWidgetKind, AWidgetThatDoesNotSuitItsParameterFallsBack) {
    OscillatorModule osc;
    const auto plan = synth::CardBodyPlan::forModule(osc, automaticLayoutWith(osc, {{"waveform", CardWidget::FaderV},
                                                                                    {"fine", CardWidget::Stepper},
                                                                                    {"level", CardWidget::Segmented},
                                                                                    {"coarse", CardWidget::Stepper}}));
    EXPECT_EQ(kindOf(plan, "waveform"), CardBodyItem::Kind::Choice) << "no fader on a choice";
    EXPECT_EQ(kindOf(plan, "fine"), CardBodyItem::Kind::Knob) << "no stepper on a float";
    EXPECT_EQ(kindOf(plan, "level"), CardBodyItem::Kind::Knob) << "no switch on a number";
    EXPECT_EQ(kindOf(plan, "coarse"), CardBodyItem::Kind::Stepper) << "-12..12 is small enough to step";
}

TEST(CardBodyWidgetKind, EachKindIsBuiltAsItsWidgetAtItsSizeAndMeasureEqualsApply) {
    CardCanvas canvas;
    auto osc = std::make_unique<OscillatorModule>();
    const auto layout = automaticLayoutWith(*osc, everyNewKind());
    const auto id = canvas.add(std::move(osc), 0, 0, layout);
    canvas.editor.updateComponents();
    auto* card = canvas.card(id);
    auto* body = card->getCardBody();

    EXPECT_NE(dynamic_cast<synth::ui::CardSegmentedSwitch*>(body->findWidget("waveform")), nullptr);
    EXPECT_NE(dynamic_cast<synth::ui::CardStepper*>(body->findWidget("octave")), nullptr);
    auto* large = dynamic_cast<synth::ui::CardKnobSlider*>(body->findWidget("fine"));
    ASSERT_NE(large, nullptr);
    EXPECT_EQ(large->getHeight(), synth::cardbody::kKnobLargeHeight) << "a 60 px dial over its text box";
    auto* faderH = dynamic_cast<synth::ui::CardFader*>(body->findWidget("level"));
    ASSERT_NE(faderH, nullptr);
    EXPECT_FALSE(faderH->isVerticalFader());
    EXPECT_EQ(faderH->getHeight(), synth::cardbody::kFaderHHeight);
    auto* faderV = dynamic_cast<synth::ui::CardFader*>(body->findWidget("detune"));
    ASSERT_NE(faderV, nullptr);
    EXPECT_TRUE(faderV->isVerticalFader());
    EXPECT_EQ(faderV->getHeight(), synth::cardbody::kFaderVHeight);
    EXPECT_EQ(faderV->getWidth(), synth::ui::CardFader::kVerticalWidth);

    // The live walk measures what it places, and a plan never built (null widgets) measures the same.
    const auto g = synth::cardbody::BodyGeometry::forCardWidth(card->getWidth());
    const int measured = body->layout(100, g, /*apply*/ false, false);
    EXPECT_EQ(body->layout(100, g, /*apply*/ true, false), measured);
    const auto staticPlan = synth::CardBodyPlan::forModule(*canvas.processor(id), layout);
    EXPECT_EQ(synth::layoutCardBodySections(staticPlan, *canvas.processor(id), 100, g, false, false), measured);
    int lowest = 0;
    for (const auto& item : body->getPlan().items)
        if (item.widget != nullptr && item.widget->isVisible())
            lowest = std::max(lowest, item.widget->getBottom());
    EXPECT_LE(lowest, measured);
    card->resized(); // put the card back the way it lays itself out
}

TEST(CardBodyWidgetKind, NoValueChangeOnAnyNewWidgetResizesTheCard) {
    CardCanvas canvas;
    auto osc = std::make_unique<OscillatorModule>();
    const auto layout = automaticLayoutWith(*osc, everyNewKind());
    const auto id = canvas.add(std::move(osc), 0, 0, layout);
    canvas.editor.updateComponents();
    auto* card = canvas.card(id);
    const auto size = card->getBounds().getHeight();
    for (const auto& item : card->getCardBody()->getPlan().items) {
        if (item.param == nullptr || item.widget == nullptr)
            continue;
        item.param->setValueNotifyingHost(1.0f);
        item.param->setValueNotifyingHost(0.0f);
        EXPECT_EQ(card->getHeight(), size) << item.param->paramID;
    }
}

// What the quick path starts from: the automatic layout as explicit items builds the same card, child
// for child, on every library card drawn from layout data. (CardBodyGolden pins the automatic card.)
TEST(CardBodyWidgetKind, TheAutomaticLayoutWrittenOutBuildsTheSameCard) {
    for (const auto& type : libraryTypes()) {
        SCOPED_TRACE(type.toStdString());
        CardCanvas canvas;
        const auto automatic = canvas.add(synth::AIStateMapper::createModule(type), 0, 0);
        canvas.editor.updateComponents();
        auto* card = canvas.card(automatic);
        if (card == nullptr || card->getCardBody() == nullptr || !card->getCardBody()->drawsFromLayout())
            continue;
        const auto explicitLayout = card->getCardBody()->explicitLayout();
        EXPECT_TRUE(explicitLayout.hidden.isEmpty());
        const auto written = canvas.add(synth::AIStateMapper::createModule(type), 0, 0, explicitLayout);
        canvas.editor.updateComponents();
        auto* rebuilt = canvas.card(written);
        card = canvas.card(automatic);
        ASSERT_NE(rebuilt, nullptr);
        EXPECT_EQ(rebuilt->getWidth(), card->getWidth());
        EXPECT_EQ(rebuilt->getHeight(), card->getHeight());
        ASSERT_EQ(rebuilt->getNumChildComponents(), card->getNumChildComponents());
        for (int i = 0; i < card->getNumChildComponents(); ++i) {
            EXPECT_EQ(rebuilt->getChildComponent(i)->getBounds(), card->getChildComponent(i)->getBounds()) << i;
            EXPECT_EQ(rebuilt->getChildComponent(i)->isVisible(), card->getChildComponent(i)->isVisible()) << i;
        }
    }
}
