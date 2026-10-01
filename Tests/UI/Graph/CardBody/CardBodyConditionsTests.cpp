// CardBodyConditionsTests.cpp
//
// Source/UI/Graph/CardBody/CardBodyConditions.cpp: a layout's conditions, read live on real cards. A
// `dim` greys a control out in its own cell; a `show` swap group shares one cell, so flipping its
// parameter moves nothing on the canvas and never sends a swapped-out control to the More row; a
// section's `visibleWhen` grows the card and makes room, then gives it back; a code default's numeric
// dim rule applies only while that default is the card's layout. Every flip goes through the real
// parameter listener (the change is queued, then flushed as the message loop would).

#include "../GraphEditor/GraphEditorTestHelpers.h"
#include "CardBodyTestHelpers.h"
#include "Modules/LFOModule.h"
#include "Modules/OscillatorModule.h"
#include "UI/Graph/CardBody/DefaultLayouts/DefaultCardLayoutsFamilies.h"
#include "UI/Graph/CardBody/ModuleCardLayoutResolver.h"
#include "UI/Layout/LayoutUtil.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

using namespace cardbody_test;
using namespace synth::cardlayout;

namespace {

void setParam(juce::AudioProcessor& module, const juce::String& id, float plainValue) {
    auto* param = findParameterByID(&module, id);
    ASSERT_NE(param, nullptr) << id;
    param->setValueNotifyingHost(param->convertTo0to1(plainValue));
}

// Sets a parameter as the user or automation would, then lets the queued re-read run.
void flip(CardCanvas& canvas, NodeID id, const juce::String& paramId, float plainValue) {
    setParam(*canvas.processor(id), paramId, plainValue);
    canvas.card(id)->getCardBody()->flushPendingConditionUpdate();
}

synth::CardLayout layoutOf(std::vector<synth::CardSection> sections) {
    synth::CardLayout layout;
    layout.sections = std::move(sections);
    return layout;
}

// The LFO's Rate in Hz and its tempo division share one cell, swapped by the Sync switch ("mode").
synth::CardLayout lfoRateSwapLayout() {
    return layoutOf({section("main", std::nullopt,
                             {param("shape"), showWhen(param("rateHz"), "mode", {"false"}),
                              showWhen(param("rateSync"), "mode", {"true"}), param("level"), param("glide")})});
}

juce::Component* widget(CardCanvas& canvas, NodeID id, const juce::String& paramId) {
    return canvas.card(id)->getCardBody()->findWidget(paramId);
}

// A neighbour just under `id`'s card, clear of it by a little more than the collision gap (so its old
// spot is free again once the card shrinks back); returns its y.
int placeNeighbourUnder(CardCanvas& canvas, NodeID id, NodeID neighbour) {
    const int y = canvas.card(id)->getBottom() + synth::LayoutUtil::kCollisionGap + 4;
    canvas.engine.getGraph().getNodeForId(neighbour)->properties.set("x", canvas.card(id)->getX());
    canvas.engine.getGraph().getNodeForId(neighbour)->properties.set("y", y);
    canvas.editor.updateComponents();
    return y;
}

} // namespace

TEST(CardBodyConditions, ADimmedControlIsGreyedOutAndKeepsItsCell) {
    CardCanvas canvas;
    const auto layout = layoutOf(
        {section("main", std::nullopt, {param("shape"), param("level"), dimUnless(param("glide"), "shape", {"S&H"})})});
    const auto id = canvas.add(std::make_unique<LFOModule>(), 0, 0, layout);
    canvas.editor.updateComponents();
    auto* card = canvas.card(id);
    auto* glide = widget(canvas, id, "glide");
    ASSERT_NE(glide, nullptr);
    const auto bounds = glide->getBounds();
    const int height = card->getHeight();
    const auto& item = card->getCardBody()->getPlan().items[(size_t)card->getCardBody()->getPlan().findParam("glide")];

    ASSERT_NE(item.label, nullptr);
    const auto tooltipOf = [](juce::Component& c) {
        return dynamic_cast<juce::SettableTooltipClient&>(c).getTooltip();
    };
    // Greyed out unless the shape is S&H, but still a control: enabled, a Tab stop, in the
    // accessibility tree, with the reason in its description and tooltip.
    auto expectDimmed = [&] {
        EXPECT_TRUE(glide->isVisible());
        EXPECT_TRUE(glide->isEnabled());
        EXPECT_TRUE(glide->getWantsKeyboardFocus());
        EXPECT_TRUE(synth::theme::AppLookAndFeel::paintsDimmed(*glide));
        EXPECT_TRUE(synth::theme::AppLookAndFeel::paintsDimmed(*item.label)) << "its caption greys out with it";
        EXPECT_TRUE(item.label->isEnabled());
        EXPECT_EQ(glide->getDescription(), "Inactive in this mode");
        EXPECT_EQ(tooltipOf(*glide), "Glide (inactive in this mode)");
        // The accessibility tree is built from the focus traverser, which skips disabled components.
        const auto reachable = card->createFocusTraverser()->getAllComponents(card);
        EXPECT_NE(std::find(reachable.begin(), reachable.end(), glide), reachable.end())
            << "still in the accessibility tree";
        const auto tabbable = card->createKeyboardFocusTraverser()->getAllComponents(card);
        EXPECT_NE(std::find(tabbable.begin(), tabbable.end(), glide), tabbable.end()) << "still a Tab stop";
    };
    expectDimmed();

    flip(canvas, id, "shape", 4.0f); // S&H
    EXPECT_FALSE(synth::theme::AppLookAndFeel::paintsDimmed(*glide));
    EXPECT_FALSE(synth::theme::AppLookAndFeel::paintsDimmed(*item.label));
    EXPECT_TRUE(glide->getDescription().isEmpty()) << "the hint goes when the dim lifts";
    EXPECT_EQ(tooltipOf(*glide), "Glide");
    EXPECT_EQ(glide->getBounds(), bounds) << "a dim never moves the control";
    EXPECT_EQ(card->getHeight(), height);

    flip(canvas, id, "shape", 0.0f); // Sine
    expectDimmed();
    EXPECT_EQ(glide->getBounds(), bounds);

    // A dimmed knob is still operable.
    auto* knob = dynamic_cast<juce::Slider*>(glide);
    ASSERT_NE(knob, nullptr);
    knob->setValue(0.5, juce::sendNotificationSync);
    EXPECT_NEAR(findParameterByID(canvas.processor(id), "glide")->getValue(), 0.5f, 0.01f);
}

TEST(CardBodyConditions, AShowSwapSharesOneCellAndMovesNothingOnTheCanvas) {
    CardCanvas canvas;
    const auto id = canvas.add(std::make_unique<LFOModule>(), 0, 0, lfoRateSwapLayout());
    const auto below = canvas.add(std::make_unique<OscillatorModule>(), 0, 0);
    canvas.editor.updateComponents();
    const int belowY = placeNeighbourUnder(canvas, id, below);
    auto* card = canvas.card(id);
    const auto cardBounds = card->getBounds();
    auto* rateHz = widget(canvas, id, "rateHz");
    auto* rateSync = widget(canvas, id, "rateSync");
    ASSERT_NE(rateHz, nullptr);
    ASSERT_NE(rateSync, nullptr);

    // Sync is on by default: the division shows, the Hz knob is out of view in the same cell.
    EXPECT_TRUE(rateSync->isVisible());
    EXPECT_FALSE(rateHz->isVisible());
    EXPECT_EQ(rateHz->getX(), rateSync->getX());
    EXPECT_EQ(rateHz->getY(), rateSync->getY());
    EXPECT_TRUE(card->getCardBody()->isSwappedOut(*rateHz));
    const auto levelBounds = widget(canvas, id, "level")->getBounds();

    flip(canvas, id, "mode", 0.0f); // free running: Hz
    EXPECT_TRUE(rateHz->isVisible());
    EXPECT_FALSE(rateSync->isVisible());
    EXPECT_EQ(rateHz->getPosition(), rateSync->getPosition()) << "the swap stays in its cell";
    EXPECT_EQ(card->getBounds(), cardBounds) << "a swap never resizes the card";
    EXPECT_EQ(widget(canvas, id, "level")->getBounds(), levelBounds) << "nor moves its neighbours in the grid";
    EXPECT_EQ(canvas.card(below)->getY(), belowY) << "nor a card on the canvas";

    flip(canvas, id, "mode", 1.0f);
    EXPECT_TRUE(rateSync->isVisible());
    EXPECT_FALSE(rateHz->isVisible());
    EXPECT_EQ(card->getBounds(), cardBounds);
    EXPECT_EQ(canvas.card(below)->getY(), belowY);
}

TEST(CardBodyConditions, ASwappedOutControlIsNeverInTheMoreRow) {
    CardCanvas canvas;
    const auto id = canvas.add(std::make_unique<LFOModule>(), 0, 0, lfoRateSwapLayout());
    canvas.editor.updateComponents();
    auto* body = canvas.card(id)->getCardBody();
    const auto& plan = body->getPlan();
    ASSERT_TRUE(body->hasMoreRow()) << "the parameters the layout leaves out fold into More";
    for (const auto* paramId : {"rateHz", "rateSync"}) {
        const int index = plan.findParam(paramId);
        EXPECT_EQ(std::count(plan.more.begin(), plan.more.end(), index), 0) << paramId;
    }
    EXPECT_EQ(body->getMoreButton()->getTitle(), "More controls (" + juce::String((int)plan.more.size()) + ")");

    body->setMoreUnfolded(true);
    EXPECT_FALSE(widget(canvas, id, "rateHz")->isVisible()) << "unfolding More never shows a swapped-out control";
}

TEST(CardBodyConditions, AConditionalSectionGrowsTheCardPushesANeighbourAndGivesItBack) {
    CardCanvas canvas;
    const auto layout = layoutOf({section("main", std::nullopt, {param("shape"), param("level")}), [] {
                                      auto free = section("free", juce::String("Free running"),
                                                          {param("rateHz"), param("glide"), param("phase")});
                                      free.visibleWhen =
                                          synth::CardCondition{"mode", {"false"}, synth::CardConditionEffect::Show};
                                      return free;
                                  }()});
    const auto id = canvas.add(std::make_unique<LFOModule>(), 0, 0, layout);
    const auto below = canvas.add(std::make_unique<OscillatorModule>(), 0, 0);
    canvas.editor.updateComponents();
    const int belowY = placeNeighbourUnder(canvas, id, below);
    auto* card = canvas.card(id);
    const int height = card->getHeight();
    EXPECT_FALSE(widget(canvas, id, "glide")->isVisible()) << "Sync is on: the free-running section is away";

    flip(canvas, id, "mode", 0.0f);
    EXPECT_TRUE(widget(canvas, id, "glide")->isVisible());
    EXPECT_GT(card->getHeight(), height) << "the section takes room";
    EXPECT_GE(canvas.card(below)->getY(), card->getBottom()) << "the neighbour is pushed clear";
    const auto& plan = card->getCardBody()->getPlan();
    ASSERT_NE(plan.sections[1].header, nullptr);
    EXPECT_TRUE(plan.sections[1].header->isVisible());

    flip(canvas, id, "mode", 1.0f);
    EXPECT_FALSE(widget(canvas, id, "glide")->isVisible());
    EXPECT_FALSE(plan.sections[1].header->isVisible());
    EXPECT_EQ(card->getHeight(), height);
    EXPECT_EQ(canvas.card(below)->getY(), belowY) << "the neighbour comes back";
}

TEST(CardBodyConditions, AValueThatChangesNoConditionResultLeavesTheCardAlone) {
    CardCanvas canvas;
    const auto id = canvas.add(std::make_unique<LFOModule>(), 0, 0, lfoRateSwapLayout());
    canvas.editor.updateComponents();
    auto* card = canvas.card(id);
    const auto bounds = card->getBounds();
    // A parameter no condition reads is not even listened to.
    setParam(*canvas.processor(id), "level", 0.3f);
    card->getCardBody()->flushPendingConditionUpdate();
    flip(canvas, id, "mode", 1.0f); // already Sync: the same result
    EXPECT_EQ(card->getBounds(), bounds);
    EXPECT_TRUE(widget(canvas, id, "rateSync")->isVisible());
}

TEST(CardBodyConditions, ACodeDimRuleGreysWhileItsPredicateHolds) {
    OscillatorModule osc;
    synth::CardDimRule detuneAtOneVoice{"detune", {"unison"}, [](juce::AudioProcessor& module) {
                                            return findParameterByID(&module, "unison")->getValue() <= 0.0f;
                                        }};
    const auto layout = layoutOf({section("main", std::nullopt, {param("unison"), param("detune")})});
    auto plan = synth::CardBodyPlan::forModule(osc, layout, {detuneAtOneVoice});
    const int detune = plan.findParam("detune");
    EXPECT_TRUE(plan.items[(size_t)detune].dimmed) << "one voice: Detune does nothing";
    const auto watched = plan.watchedParameters(osc);
    ASSERT_EQ(watched.size(), 1u);
    EXPECT_EQ(watched.front()->paramID, "unison");

    setParam(osc, "unison", 3.0f);
    const auto change = plan.evaluateConditions(osc);
    EXPECT_TRUE(change.any);
    EXPECT_FALSE(change.sections);
    EXPECT_FALSE(plan.items[(size_t)detune].dimmed);
    EXPECT_FALSE(plan.evaluateConditions(osc).any) << "nothing changed since";

    // The automatic layout carries no rules.
    EXPECT_FALSE(synth::CardBodyPlan::forModule(osc, std::nullopt, {detuneAtOneVoice}).items[(size_t)detune].dimmed);
}

TEST(CardBodyConditions, CodeDimRulesComeWithEveryLayoutOfTheirType) {
    synth::DefaultCardLayouts defaults;
    const auto layout = layoutOf({section("main", std::nullopt, {param("unison"), param("detune")})});
    defaults.add("Oscillator", layout, 1,
                 {synth::CardDimRule{"detune", {"unison"}, [](juce::AudioProcessor&) { return true; }}});
    const auto fromCode = synth::resolveModuleCardLayout("Oscillator", {}, nullptr, defaults);
    EXPECT_EQ(fromCode.source, synth::ResolvedModuleCardLayout::Source::CodeDefault);
    EXPECT_EQ(fromCode.dimRules.size(), 1u);

    const auto fromUser = synth::resolveModuleCardLayout("Oscillator", layout.toVar(), nullptr, defaults);
    EXPECT_EQ(fromUser.source, synth::ResolvedModuleCardLayout::Source::Instance);
    EXPECT_EQ(fromUser.dimRules.size(), 1u) << "a rule describes the module, so an edited card keeps it";

    const auto root = juce::File::getSpecialLocation(juce::File::tempDirectory)
                          .getChildFile("CardBodyConditionsTests-" + juce::Uuid().toString());
    synth::ModuleCardLayoutStore store(root);
    ASSERT_TRUE(store.setDefault("Oscillator", layout));
    const auto fromStore = synth::resolveModuleCardLayout("Oscillator", {}, &store, defaults);
    EXPECT_EQ(fromStore.source, synth::ResolvedModuleCardLayout::Source::TypeDefault);
    EXPECT_EQ(fromStore.dimRules.size(), 1u);
    root.deleteRecursively();

    EXPECT_TRUE(synth::resolveModuleCardLayout("Filter", layout.toVar(), nullptr, defaults).dimRules.empty())
        << "another type's rules never apply";
}

TEST(CardBodyConditions, ChoiceConditionsMatchValueStringsAndBoolsTrueOrFalse) {
    LFOModule lfo;
    using synth::cardConditionHolds;
    const synth::CardCondition sampleAndHold{"shape", {"S&H"}, synth::CardConditionEffect::Dim};
    const synth::CardCondition synced{"mode", {"true"}, synth::CardConditionEffect::Show};
    const synth::CardCondition missing{"noSuchParam", {"x"}, synth::CardConditionEffect::Show};
    EXPECT_FALSE(cardConditionHolds(sampleAndHold, lfo));
    EXPECT_TRUE(cardConditionHolds(synced, lfo));
    EXPECT_TRUE(cardConditionHolds(missing, lfo)) << "an unknown parameter never applies its effect";
    setParam(lfo, "shape", 4.0f);
    setParam(lfo, "mode", 0.0f);
    EXPECT_TRUE(cardConditionHolds(sampleAndHold, lfo));
    EXPECT_FALSE(cardConditionHolds(synced, lfo));
}
