// CardBodyBindingTests.cpp
//
// A card body changes only which widget a parameter gets and where; every binding stays: each widget
// kind drives its parameter, registers for MIDI Learn under it and (a knob) takes the modulation-amount
// gesture; a parameter folded into the More row keeps its value, stays in the patch and can still take
// a modulation cable, which unfolds the row when dragged over it (docs/layout/module-card-layout.md).

#include "AI/AIStateMapper/AIStateMapper.h"
#include "CardBodyTestHelpers.h"
#include "Modules/FilterModule.h"
#include "Modules/LFOModule.h"
#include "Modules/ModuleBase.h"
#include "UI/Graph/ModuleComponent/CardKnobSlider.h"

using namespace cardbody_test;
using synth::CardBodyItem;

namespace {

// Moves each widget kind through its own UI path and reads the parameter back.
void expectWidgetDrivesParameter(const CardBodyItem& item) {
    SCOPED_TRACE(item.param->paramID.toStdString());
    ASSERT_NE(item.widget, nullptr);
    if (item.kind == CardBodyItem::Kind::Choice) {
        auto* combo = dynamic_cast<juce::ComboBox*>(item.widget);
        auto* choice = dynamic_cast<juce::AudioParameterChoice*>(item.param);
        ASSERT_NE(combo, nullptr);
        ASSERT_NE(choice, nullptr);
        const int target = (choice->getIndex() + 1) % choice->choices.size();
        combo->setSelectedId(target + 1, juce::sendNotificationSync);
        EXPECT_EQ(choice->getIndex(), target);
    } else if (item.kind == CardBodyItem::Kind::Knob) {
        auto* knob = dynamic_cast<juce::Slider*>(item.widget);
        ASSERT_NE(knob, nullptr);
        const double target = knob->getMaximum();
        knob->setValue(target, juce::sendNotificationSync);
        EXPECT_NEAR(item.param->convertFrom0to1(item.param->getValue()), (float)target,
                    1.0e-3f * (float)(knob->getMaximum() - knob->getMinimum()) + 1.0e-4f);
    } else if (item.kind == CardBodyItem::Kind::Toggle) {
        auto* toggle = dynamic_cast<juce::Button*>(item.widget);
        auto* flag = dynamic_cast<juce::AudioParameterBool*>(item.param);
        ASSERT_NE(toggle, nullptr);
        ASSERT_NE(flag, nullptr);
        const bool target = !flag->get();
        toggle->setToggleState(target, juce::sendNotificationSync);
        EXPECT_EQ(flag->get(), target);
    }
}

juce::var nodeOfType(const juce::var& patch, const juce::String& type) {
    if (auto* nodes = patch.getProperty("nodes", {}).getArray())
        for (const auto& node : *nodes)
            if (node.getProperty("type", {}).toString() == type)
                return node;
    return {};
}

} // namespace

TEST(CardBodyBinding, EveryWidgetKindDrivesItsParameterShownOrFolded) {
    CardCanvas canvas;
    auto hiddenFilter = std::make_unique<FilterModule>();
    const auto layout = automaticLayoutHiding(*hiddenFilter, {"filterType", "poly", "cutoff"});
    const auto shown = canvas.add(std::make_unique<FilterModule>(), 0, 0);
    const auto folded = canvas.add(std::move(hiddenFilter), 400, 0, layout);
    canvas.editor.updateComponents();

    for (auto id : {shown, folded}) {
        auto* body = canvas.card(id)->getCardBody();
        ASSERT_NE(body, nullptr);
        bool sawChoice = false, sawKnob = false, sawToggle = false;
        for (const auto& item : body->getPlan().items) {
            if (item.kind == CardBodyItem::Kind::View)
                continue;
            sawChoice |= item.kind == CardBodyItem::Kind::Choice;
            sawKnob |= item.kind == CardBodyItem::Kind::Knob;
            sawToggle |= item.kind == CardBodyItem::Kind::Toggle;
            expectWidgetDrivesParameter(item);
        }
        EXPECT_TRUE(sawChoice && sawKnob && sawToggle) << "the Filter card has every widget kind";
    }
    EXPECT_EQ(canvas.card(folded)->getCardBody()->getPlan().more.size(), 3u);
}

// One registry entry per widget, keyed on the parameter the widget is bound to, and findWidget looks
// the widget up by paramId -- on every library card built through a card body.
TEST(CardBodyBinding, EveryWidgetIsLearnableUnderItsParameterAndFoundByParamId) {
    AudioEngine engine;
    GraphEditor editor(engine);
    for (const auto& type : libraryTypes()) {
        SCOPED_TRACE(type.toStdString());
        auto module = synth::AIStateMapper::createModule(type);
        ASSERT_NE(module, nullptr);
        ModuleComponent card(module.get(), NodeID(1), editor);
        auto* body = card.getCardBody();
        if (body == nullptr)
            continue;
        for (const auto& item : body->getPlan().items) {
            if (item.param == nullptr)
                continue;
            EXPECT_EQ(body->findWidget(item.param->paramID), item.widget);
            EXPECT_EQ(card.findMidiLearnableParamForTest(item.widget), item.param) << item.param->paramID;
            if (item.kind != CardBodyItem::Kind::Knob)
                continue;
            auto* knob = dynamic_cast<synth::ui::CardKnobSlider*>(item.widget);
            ASSERT_NE(knob, nullptr) << "every knob is a CardKnobSlider";
            EXPECT_TRUE(knob->wantsModAmountGesture && knob->onModAmountGesture) << "modulation-amount gesture";
            EXPECT_TRUE(knob->wantsCablePickupGesture && knob->onCablePickupGesture) << "knob-bound cable pickup";
        }
    }
}

TEST(CardBodyBinding, FoldedParameterKeepsItsValueAndStaysInThePatch) {
    CardCanvas canvas;
    auto filter = std::make_unique<FilterModule>();
    const auto layout = automaticLayoutHiding(*filter, {"cutoff"});
    const auto id = canvas.add(std::move(filter), 0, 0, layout);
    canvas.editor.updateComponents();
    auto* card = canvas.card(id);
    auto* body = card->getCardBody();
    auto* knob = dynamic_cast<juce::Slider*>(body->findWidget("cutoff"));
    ASSERT_NE(knob, nullptr);
    EXPECT_FALSE(knob->isVisible());

    knob->setValue(1234.0, juce::sendNotificationSync);
    auto* cutoff = findParameterByID(canvas.processor(id), "cutoff");
    ASSERT_NE(cutoff, nullptr);
    EXPECT_NEAR(cutoff->convertFrom0to1(cutoff->getValue()), 1234.0f, 1.0f);

    const auto patch = synth::AIStateMapper::graphToJSON(canvas.engine.getGraph());
    const auto node = nodeOfType(patch, "Filter");
    ASSERT_TRUE(node.isObject());
    EXPECT_NEAR((float)node["params"].getProperty("cutoff", 0.0f), 1234.0f, 1.0f) << "hidden, still in params";
    EXPECT_TRUE(node.hasProperty("cardLayout"));

    // Folded, the knob is out of view, so its CV jack is back in the gutter; unfolded, it is knob-bound.
    auto* mb = dynamic_cast<ModuleBase*>(canvas.processor(id));
    const int cutoffJack = mb->mapInputChannel(1).visibleJackIndex;
    EXPECT_FALSE(card->isInputJackKnobBound(cutoffJack));
    body->setMoreUnfolded(true);
    EXPECT_TRUE(card->isInputJackKnobBound(cutoffJack));
}

// A real cable drag through GraphEditor's own drag path: over the folded More row it unfolds, then the
// cable lands on the hidden parameter's knob and creates the routing.
TEST(CardBodyBinding, CableDraggedOverTheFoldedMoreRowUnfoldsItAndLandsOnTheHiddenKnob) {
    CardCanvas canvas;
    auto filter = std::make_unique<FilterModule>();
    const auto layout = automaticLayoutHiding(*filter, {"cutoff"});
    const auto lfoId = canvas.add(std::make_unique<LFOModule>(), 0, 0);
    const auto filterId = canvas.add(std::move(filter), 600, 0, layout);
    canvas.editor.updateComponents();
    auto* lfoCard = canvas.card(lfoId);
    auto* filterCard = canvas.card(filterId);
    ASSERT_NE(lfoCard, nullptr);
    ASSERT_NE(filterCard, nullptr);
    lfoCard->setTopLeftPosition(0, 0);
    filterCard->setTopLeftPosition(600, 0);

    auto* body = filterCard->getCardBody();
    auto* more = body->getMoreButton();
    ASSERT_NE(more, nullptr);
    ASSERT_FALSE(body->isMoreUnfolded());
    const int foldedHeight = filterCard->getHeight();

    canvas.editor.beginConnectionDrag(lfoCard, 0, /*isInput*/ false, /*isMidi*/ false, {0, 0});
    canvas.editor.dragConnection(filterCard->getPosition() + more->getBounds().getCentre());
    EXPECT_TRUE(body->isMoreUnfolded()) << "a cable over the folded row opens it";
    EXPECT_GT(filterCard->getHeight(), foldedHeight);

    auto* knob = body->findWidget("cutoff");
    ASSERT_NE(knob, nullptr);
    ASSERT_TRUE(knob->isVisible());
    const auto knobPoint = filterCard->getPosition() + knob->getBounds().getCentre();
    canvas.editor.dragConnection(knobPoint);
    canvas.editor.endConnectionDrag(knobPoint);

    bool routed = false;
    for (const auto& routing : canvas.engine.getModulationRoutings())
        routed |= routing.sourceNodeID == lfoId && routing.destNodeID == filterId && routing.destChannelIndex == 1;
    EXPECT_TRUE(routed) << "the cable landed on the hidden Cutoff knob";
}

// A cable dragged from an input (disconnect-and-redrag) never lands on a knob, so it does not open
// the row either.
TEST(CardBodyBinding, InputCableDragDoesNotUnfoldTheMoreRow) {
    CardCanvas canvas;
    auto filter = std::make_unique<FilterModule>();
    const auto layout = automaticLayoutHiding(*filter, {"cutoff"});
    const auto lfoId = canvas.add(std::make_unique<LFOModule>(), 0, 0);
    const auto filterId = canvas.add(std::move(filter), 600, 0, layout);
    canvas.editor.updateComponents();
    auto* filterCard = canvas.card(filterId);
    filterCard->setTopLeftPosition(600, 0);
    auto* body = filterCard->getCardBody();

    canvas.editor.beginConnectionDrag(canvas.card(lfoId), 0, /*isInput*/ true, /*isMidi*/ false, {0, 0});
    canvas.editor.dragConnection(filterCard->getPosition() + body->getMoreButton()->getBounds().getCentre());
    EXPECT_FALSE(body->isMoreUnfolded());
    canvas.editor.endConnectionDrag({0, 0});
}
