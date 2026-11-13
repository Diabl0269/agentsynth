// CardBodyWidgetKindTests.cpp
//
// Source/UI/Graph/CardBody/ with the widgets a layout may choose (large knob, faders, segmented switch,
// stepper): the plan honours ParamItem.widget where it suits the parameter and falls back where it does
// not, each kind lays out at its own size, a plan never built measures exactly what the live card
// places, no value change resizes the card, and the automatic layout written out as explicit items
// builds the very same card (what the quick path starts from). The XY pad
// (docs/layout/module-card-layout.md#the-xy-pad) closes the file: its layout through JSON, its plan, the real mouse and
// key paths moving both macros as one undo step, the bipolar readout, the pure Show as / Y macro edits and the XY
// preset in the menu.

#include "../GraphEditor/GraphEditorTestHelpers.h"
#include "AI/AIStateMapper/AIStateMapper.h"
#include "CardBodyTestHelpers.h"
#include "Modules/FX/ParametricEQModule.h"
#include "Modules/MacroControlModule.h"
#include "Modules/OscillatorModule.h"
#include "UI/Graph/CardBody/CardBodyLayoutWalk.h"
#include "UI/Graph/CardBody/DefaultCardLayouts.h"
#include "UI/Graph/CardLayoutEditor/OnCard/OnCardControlOptions.h"
#include "UI/Graph/CardWidgets/CardFader.h"
#include "UI/Graph/CardWidgets/CardSegmentedSwitch.h"
#include "UI/Graph/CardWidgets/CardStepper.h"
#include "UI/Graph/CardWidgets/CardXYPad.h"
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

TEST(CardBodyWidgetKind, ALongVerticalFaderIsAVerticalFaderWithATallerCell) {
    OscillatorModule osc;
    const auto plan =
        synth::CardBodyPlan::forModule(osc, automaticLayoutWith(osc, {{"detune", CardWidget::FaderVLong}}));
    EXPECT_EQ(kindOf(plan, "detune"), CardBodyItem::Kind::FaderVLong);
    EXPECT_TRUE(CardBodyItem::isVerticalFader(CardBodyItem::Kind::FaderVLong));
    EXPECT_FALSE(CardBodyItem::isVerticalFader(CardBodyItem::Kind::FaderH));
    EXPECT_GT(synth::cardbody::kFaderVLongHeight, synth::cardbody::kFaderVHeight);
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
    const int measured = body->layout(100, g, /*apply*/ false);
    EXPECT_EQ(body->layout(100, g, /*apply*/ true), measured);
    const auto staticPlan = synth::CardBodyPlan::forModule(*canvas.processor(id), layout);
    EXPECT_EQ(synth::layoutCardBodySections(staticPlan, *canvas.processor(id), 100, g, false), measured);
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

// Every band parameter is edited by the EQ card's own selected-band row, so the generic card body builds no widget
// for any of them (and registers no MIDI learn target of its own); the output trim keeps its widget.
TEST(CardBodyWidgetKind, TheEqCardBodyLeavesEveryBandToTheSelectedBandRow) {
    ParametricEQModule eq;
    const auto plan = synth::CardBodyPlan::forModule(eq, std::nullopt);
    for (int b = 1; b <= ParametricEQModule::kMaxBands; ++b)
        for (const char* suffix : {"On", "Freq", "Gain", "Q", "Type", "Slope"})
            EXPECT_EQ(plan.findParam("band" + juce::String(b) + suffix), -1) << b << suffix;
    EXPECT_GE(plan.findParam("outputGain"), 0);
}

using Kind = synth::CardBodyItem::Kind;

namespace {

synth::CardLayout xyPreset() {
    for (const auto& [name, layout] : synth::DefaultCardLayouts::builtIn().find("Macros")->presets)
        if (name == "XY")
            return layout;
    return {};
}

struct XYCard {
    CardCanvas canvas;
    NodeID id;
    MacroControlModule* module = nullptr;

    XYCard() {
        auto macros = std::make_unique<MacroControlModule>();
        module = macros.get();
        id = canvas.add(std::move(macros), 400, 300, xyPreset());
        canvas.editor.updateComponents();
    }

    ModuleComponent& card() { return *canvas.card(id); }
    synth::ui::CardXYPad* pad() {
        for (auto* child : card().getChildren())
            if (auto* p = dynamic_cast<synth::ui::CardXYPad*>(child))
                return p;
        return nullptr;
    }
    juce::RangedAudioParameter* param(const juce::String& id) {
        for (auto* p : module->getParameters())
            if (auto* r = dynamic_cast<juce::RangedAudioParameter*>(p); r != nullptr && r->paramID == id)
                return r;
        return nullptr;
    }
    float value(const juce::String& id) { return param(id)->getValue(); }
    juce::Point<float> at(float x, float y) { return pad()->pixelsFor(x, y); }
    void mouse(void (juce::Component::*fn)(const juce::MouseEvent&), juce::Point<float> p) {
        (pad()->*fn)(mouseAt(*pad(), p, juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier)));
    }
    void drag(juce::Point<float> from, juce::Point<float> to) {
        mouse(&juce::Component::mouseDown, from);
        mouse(&juce::Component::mouseDrag, to);
        mouse(&juce::Component::mouseUp, to);
    }
};

} // namespace

TEST(CardXYPad, TheLayoutKeepsBothMacrosThroughJson) {
    const auto layout = xyPreset();
    const auto parsed = synth::CardLayout::fromVar(layout.toVar());
    ASSERT_EQ(parsed.status, synth::CardLayout::ParseStatus::Ok);
    const auto* pad = std::get_if<synth::CardParamItem>(&parsed.layout.sections[1].items[0]);
    ASSERT_NE(pad, nullptr);
    EXPECT_EQ(pad->widget, synth::CardWidget::XYPad);
    EXPECT_EQ(pad->yParamId.value_or(""), "macro2");
    EXPECT_EQ(parsed.layout, layout);
}

TEST(CardXYPad, ThePlanPlacesOnePadAndLeavesTheSecondMacroWithoutACellOrAMoreRow) {
    MacroControlModule module;
    const auto plan = synth::CardBodyPlan::forModule(module, xyPreset());
    const int x = plan.findParam("macro1");
    const int y = plan.findParam("macro2");
    ASSERT_GE(x, 0);
    ASSERT_GE(y, 0);
    EXPECT_EQ(plan.items[(size_t)x].kind, Kind::XYPad);
    EXPECT_EQ(plan.items[(size_t)x].partner, plan.items[(size_t)y].param);
    EXPECT_TRUE(plan.items[(size_t)y].consumed);
    EXPECT_TRUE(plan.more.empty() || std::find(plan.more.begin(), plan.more.end(), y) == plan.more.end());
}

TEST(CardXYPad, TheXYPresetIsInTheMenuAndTheEstimateEqualsTheCard) {
    const auto* entry = synth::DefaultCardLayouts::builtIn().find("Macros");
    ASSERT_NE(entry, nullptr);
    bool found = false;
    for (const auto& preset : entry->presets)
        found = found || preset.first == "XY";
    EXPECT_TRUE(found);

    XYCard c;
    ASSERT_NE(c.pad(), nullptr);
    EXPECT_GT(c.pad()->getHeight(), 0);
    EXPECT_TRUE(c.card().getLocalBounds().contains(c.pad()->getBounds()));
}

TEST(CardXYPad, ADragWithTheMouseMovesBothMacrosAndUndoesInOneStep) {
    XYCard c;
    ASSERT_NE(c.pad(), nullptr);
    const float x0 = c.value("macro1");
    const float y0 = c.value("macro2");
    c.drag(c.at(0.5f, 0.5f), c.at(0.2f, 0.8f));
    EXPECT_NEAR(c.value("macro1"), 0.2f, 0.02f);
    EXPECT_NEAR(c.value("macro2"), 0.8f, 0.02f);
    EXPECT_NEAR(c.pad()->getX(), 0.2f, 0.02f);
    EXPECT_NEAR(c.pad()->getY(), 0.8f, 0.02f);

    ASSERT_TRUE(c.canvas.undo.undo());
    EXPECT_NEAR(c.value("macro1"), x0, 1e-4f);
    EXPECT_NEAR(c.value("macro2"), y0, 1e-4f);
}

TEST(CardXYPad, AParameterChangeFromOutsideMovesTheDot) {
    XYCard c;
    c.param("macro1")->setValueNotifyingHost(0.9f);
    c.param("macro2")->setValueNotifyingHost(0.1f);
    EXPECT_NEAR(c.pad()->getX(), 0.9f, 1e-3f);
    EXPECT_NEAR(c.pad()->getY(), 0.1f, 1e-3f);
}

TEST(CardXYPad, ArrowKeysNudgeAndShiftIsFine) {
    XYCard c;
    auto* pad = c.pad();
    const float x0 = c.value("macro1");
    const float y0 = c.value("macro2");
    EXPECT_TRUE(pad->keyPressed(juce::KeyPress(juce::KeyPress::rightKey)));
    EXPECT_NEAR(c.value("macro1"), x0 + synth::ui::CardXYPad::kStep, 1e-3f);
    EXPECT_TRUE(pad->keyPressed(juce::KeyPress(juce::KeyPress::upKey)));
    EXPECT_NEAR(c.value("macro2"), y0 + synth::ui::CardXYPad::kStep, 1e-3f);
    EXPECT_TRUE(pad->keyPressed(juce::KeyPress(juce::KeyPress::leftKey, juce::ModifierKeys::shiftModifier, 0)));
    EXPECT_NEAR(c.value("macro1"), x0 + synth::ui::CardXYPad::kStep - synth::ui::CardXYPad::kFineStep, 1e-3f);
    EXPECT_TRUE(pad->getWantsKeyboardFocus());
    EXPECT_FALSE(pad->getTitle().isEmpty());
    EXPECT_TRUE(pad->getTitle().contains(pad->describe()));
}

TEST(CardXYPad, AKeyStepIsOneUndoStep) {
    XYCard c;
    const float x0 = c.value("macro1");
    c.canvas.undo.getUndoManager().beginNewTransaction();
    c.pad()->keyPressed(juce::KeyPress(juce::KeyPress::rightKey));
    ASSERT_TRUE(c.canvas.undo.undo());
    EXPECT_NEAR(c.value("macro1"), x0, 1e-4f);
}

TEST(CardXYPad, ABipolarMacroReadsMinusOneToOne) {
    XYCard c;
    c.param(MacroControlModule::bipolarParamId(0))->setValueNotifyingHost(1.0f);
    c.card().getCardBody()->pollMacros();
    c.param("macro1")->setValueNotifyingHost(0.0f);
    EXPECT_TRUE(c.pad()->describe().contains("-1.00")) << c.pad()->describe().toStdString();
}

TEST(CardXYPad, TheCardRebuiltFromItsStoredLayoutStillDrawsThePad) {
    XYCard c;
    c.canvas.editor.updateComponents();
    ASSERT_NE(c.pad(), nullptr);
    c.drag(c.at(0.5f, 0.5f), c.at(0.7f, 0.3f));
    EXPECT_NEAR(c.value("macro1"), 0.7f, 0.02f);
}

TEST(CardXYPad, ShowAsXYPadTakesTheNextMacroAndShowingItAsAKnobBringsItBack) {
    synth::CardLayout knobs;
    {
        MacroControlModule module;
        const auto plan = synth::CardBodyPlan::forModule(module, std::nullopt);
        knobs = automaticLayoutWith(module, {});
    }
    auto pad = synth::ui::withShowAs(knobs, "macro3", synth::CardWidget::XYPad);
    const auto* item = [&]() -> const synth::CardParamItem* {
        for (const auto& section : pad.sections)
            for (const auto& i : section.items)
                if (const auto* p = std::get_if<synth::CardParamItem>(&i); p != nullptr && p->paramId == "macro3")
                    return p;
        return nullptr;
    }();
    ASSERT_NE(item, nullptr);
    EXPECT_EQ(item->widget, synth::CardWidget::XYPad);
    EXPECT_EQ(item->yParamId.value_or(""), "macro4");
    EXPECT_TRUE(pad.hidden.contains("macro4"));

    auto moved = synth::ui::withXYPartner(pad, "macro3", "macro5");
    EXPECT_FALSE(moved.hidden.contains("macro4"));
    EXPECT_TRUE(moved.hidden.contains("macro5"));

    auto back = synth::ui::withShowAs(moved, "macro3", synth::CardWidget::Knob);
    EXPECT_FALSE(back.hidden.contains("macro5"));
    for (const auto& section : back.sections)
        for (const auto& i : section.items)
            if (const auto* p = std::get_if<synth::CardParamItem>(&i))
                EXPECT_FALSE(p->yParamId.has_value());
}

TEST(CardXYPad, RightClickOffersEachMacrosAutomateAndMidiLearnWithoutStartingADrag) {
    XYCard c;
    const float before = c.value("macro1");
    juce::PopupMenu captured;
    c.card().setShowContextMenuHookForTest([&](juce::PopupMenu& menu) { captured = menu; });
    const auto event = mouseAt(*c.pad(), c.at(0.3f, 0.3f), juce::ModifierKeys(juce::ModifierKeys::rightButtonModifier));
    c.pad()->mouseDown(event);
    c.card().mouseDown(event);
    c.pad()->mouseUp(event);
    c.card().setShowContextMenuHookForTest(nullptr);

    int submenus = 0;
    juce::PopupMenu::MenuItemIterator it(captured);
    while (it.next())
        if (it.getItem().subMenu != nullptr)
            ++submenus;
    EXPECT_EQ(submenus, 2) << "one submenu per macro";
    EXPECT_NEAR(c.value("macro1"), before, 1e-4f) << "a right-click moves nothing";
}
