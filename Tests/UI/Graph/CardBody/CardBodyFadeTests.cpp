// CardBodyFadeTests.cpp
//
// The parts of a module card that appear and disappear in place fade in and out with FadeVisibility while the card's
// height follows (docs/layout/animation.md#fading-things-in-and-out): the Show Scope and Show Response panels, the
// LFO's custom wave section, the ADSR's envelope view, the More row and a conditional section. Each test runs the
// animated path with FadeAnimateGuard and steps the fades by hand: the first frame is hidden (or whole) with the
// card's height unchanged, a middle frame is part way, the last frame lands exactly where a synchronous change lands;
// neighbours make room once, for the final size, when a panel opens and are given the room back when it has closed.
// Without the guard (a card off screen) every change lands before the call returns.

#include "../../Layout/FadeVisibilityTestGuard.h"
#include "../GraphEditor/GraphEditorTestHelpers.h"
#include "CardBodyTestHelpers.h"
#include "Modules/ADSRModule.h"
#include "Modules/FilterModule.h"
#include "Modules/LFOModule.h"
#include "Modules/OscillatorModule.h"
#include "UI/Graph/CardBody/DefaultLayouts/DefaultCardLayoutsFamilies.h"
#include "UI/Layout/LayoutUtil.h"
#include "UI/Layout/ZoomFrozenCachedImage.h"
#include "UI/ModuleViews/CurveEditor/CurveEditorComponent.h"
#include "UI/ModuleViews/FrequencyResponseComponent.h"
#include "UI/ModuleViews/ScopeComponent.h"

using namespace cardbody_test;
using namespace synth::cardlayout;
using synth::ui::FadeVisibility;

namespace {

template <typename T>
T* childOfType(juce::Component& card) {
    for (auto* child : card.getChildren())
        if (auto* typed = dynamic_cast<T*>(child))
            return typed;
    return nullptr;
}

juce::ToggleButton* toggleNamed(juce::Component& card, const juce::String& text) {
    for (auto* child : card.getChildren())
        if (auto* toggle = dynamic_cast<juce::ToggleButton*>(child);
            toggle != nullptr && toggle->getButtonText() == text)
            return toggle;
    return nullptr;
}

// A neighbour just under `id`'s card, clear of it by a little more than the collision gap; returns its y.
int placeNeighbourUnder(CardCanvas& canvas, NodeID id, NodeID neighbour) {
    const int y = canvas.card(id)->getBottom() + synth::LayoutUtil::kCollisionGap + 4;
    canvas.engine.getGraph().getNodeForId(neighbour)->properties.set("x", canvas.card(id)->getX());
    canvas.engine.getGraph().getNodeForId(neighbour)->properties.set("y", y);
    canvas.editor.updateComponents();
    return y;
}

void setParam(juce::AudioProcessor& module, const juce::String& id, float plainValue) {
    auto* param = findParameterByID(&module, id);
    ASSERT_NE(param, nullptr) << id;
    param->setValueNotifyingHost(param->convertTo0to1(plainValue));
}

synth::CardLayout layoutOf(std::vector<synth::CardSection> sections) {
    synth::CardLayout layout;
    layout.sections = std::move(sections);
    return layout;
}

// The card with a neighbour under it, and the heights a panel's toggle moves it between.
struct Rig {
    CardCanvas canvas;
    NodeID id;
    NodeID below;
    int belowY = 0;
    ModuleComponent* card = nullptr;

    void build(std::unique_ptr<juce::AudioProcessor> module, std::optional<synth::CardLayout> layout = std::nullopt) {
        id = canvas.add(std::move(module), 0, 0, std::move(layout));
        below = canvas.add(std::make_unique<OscillatorModule>(), 0, 0);
        canvas.editor.updateComponents();
        belowY = placeNeighbourUnder(canvas, id, below);
        card = canvas.card(id);
    }
    int neighbourY() { return canvas.card(below)->getY(); }
};

constexpr float kEps = 0.01f; // alpha is stored in 8 bits

} // namespace

TEST(CardFades, ShowScopeFadesInWhileTheCardGrowsAndTheNeighbourMakesRoomOnce) {
    Rig rig;
    rig.build(std::make_unique<OscillatorModule>());
    auto* scope = childOfType<ScopeComponent>(*rig.card);
    auto* toggle = toggleNamed(*rig.card, "Show Scope");
    ASSERT_NE(scope, nullptr);
    ASSERT_NE(toggle, nullptr);
    const int closed = rig.card->getHeight();

    FadeAnimateGuard guard;
    toggle->setToggleState(true, juce::sendNotificationSync);
    EXPECT_TRUE(scope->isVisible());
    EXPECT_NEAR(scope->getAlpha(), 0.0f, kEps) << "frame 0 is already the first fade frame";
    EXPECT_EQ(rig.card->getHeight(), closed) << "the card starts at its old height";
    EXPECT_GE(rig.neighbourY(), rig.card->getY() + closed + 100) << "the neighbour made room for the final size, once";

    FadeVisibility::stepAllForTest(0.5f);
    EXPECT_NEAR(scope->getAlpha(), 0.5f, kEps);
    EXPECT_GT(rig.card->getHeight(), closed);
    EXPECT_LT(rig.card->getHeight(), closed + 100) << "the card grows with the fade, it does not jump";
    EXPECT_EQ(scope->getHeight(), rig.card->getHeight() - closed) << "the panel is as tall as the room it has taken";

    FadeVisibility::stepAllForTest(1.0f);
    EXPECT_NEAR(scope->getAlpha(), 1.0f, kEps);
    EXPECT_EQ(rig.card->getHeight(), closed + 100);
    EXPECT_EQ(scope->getHeight(), 100);

    toggle->setToggleState(false, juce::sendNotificationSync);
    EXPECT_TRUE(scope->isVisible()) << "still there while it fades out";
    EXPECT_EQ(rig.card->getHeight(), closed + 100);
    FadeVisibility::stepAllForTest(0.5f);
    EXPECT_GT(rig.card->getHeight(), closed);
    EXPECT_LT(rig.card->getHeight(), closed + 100);
    EXPECT_GE(rig.neighbourY(), rig.card->getY() + closed + 100) << "the neighbour waits for the fade to end";

    FadeVisibility::stepAllForTest(1.0f);
    EXPECT_FALSE(scope->isVisible());
    EXPECT_NEAR(scope->getAlpha(), 1.0f, kEps) << "alpha goes back for the next show";
    EXPECT_EQ(rig.card->getHeight(), closed);
    EXPECT_EQ(rig.neighbourY(), rig.belowY) << "the room goes back once the panel has gone";
}

TEST(CardFades, ShowScopeLandsAtOnceWhenTheCardIsNotOnScreen) {
    Rig rig;
    rig.build(std::make_unique<OscillatorModule>());
    auto* scope = childOfType<ScopeComponent>(*rig.card);
    auto* toggle = toggleNamed(*rig.card, "Show Scope");
    const int closed = rig.card->getHeight();

    toggle->setToggleState(true, juce::sendNotificationSync);
    EXPECT_TRUE(scope->isVisible());
    EXPECT_NEAR(scope->getAlpha(), 1.0f, kEps);
    EXPECT_EQ(rig.card->getHeight(), closed + 100);
    toggle->setToggleState(false, juce::sendNotificationSync);
    EXPECT_FALSE(scope->isVisible());
    EXPECT_EQ(rig.card->getHeight(), closed);
    EXPECT_EQ(rig.neighbourY(), rig.belowY);
}

TEST(CardFades, ShowResponseFadesTheViewAndItsSpectrumToggleTogether) {
    Rig rig;
    rig.build(std::make_unique<FilterModule>());
    auto* response = childOfType<FrequencyResponseComponent>(*rig.card);
    auto* toggle = toggleNamed(*rig.card, "Show Response");
    auto* spectrum = toggleNamed(*rig.card, "Show Spectrum");
    ASSERT_NE(response, nullptr);
    ASSERT_NE(toggle, nullptr);
    ASSERT_NE(spectrum, nullptr);
    const int closed = rig.card->getHeight();
    EXPECT_FALSE(spectrum->isVisible());

    // Where a synchronous open lands: the same card, opened off screen.
    int open = 0;
    {
        Rig sync;
        sync.build(std::make_unique<FilterModule>());
        toggleNamed(*sync.card, "Show Response")->setToggleState(true, juce::sendNotificationSync);
        open = sync.card->getHeight();
    }
    ASSERT_GT(open, closed);

    FadeAnimateGuard guard;
    toggle->setToggleState(true, juce::sendNotificationSync);
    EXPECT_TRUE(response->isVisible());
    EXPECT_TRUE(spectrum->isVisible());
    EXPECT_NEAR(response->getAlpha(), 0.0f, kEps);
    EXPECT_NEAR(spectrum->getAlpha(), 0.0f, kEps);
    // The Spectrum pill joins the footer row at once (it may wrap to a second row); the panel itself starts at zero.
    const int start = rig.card->getHeight();
    EXPECT_LT(start, open);
    EXPECT_GE(rig.neighbourY(), rig.card->getY() + open);

    FadeVisibility::stepAllForTest(0.5f);
    EXPECT_GT(rig.card->getHeight(), start);
    EXPECT_LT(rig.card->getHeight(), open);
    EXPECT_NEAR(spectrum->getAlpha(), 0.5f, kEps);

    FadeVisibility::stepAllForTest(1.0f);
    EXPECT_EQ(rig.card->getHeight(), open);

    toggle->setToggleState(false, juce::sendNotificationSync);
    FadeVisibility::stepAllForTest(1.0f);
    EXPECT_FALSE(response->isVisible());
    EXPECT_FALSE(spectrum->isVisible());
    EXPECT_EQ(rig.card->getHeight(), closed);
    EXPECT_EQ(rig.neighbourY(), rig.belowY);
}

TEST(CardFades, LfoCustomWaveSectionFadesWithItsToolbarWhileTheCardGrows) {
    Rig rig;
    rig.build(std::make_unique<LFOModule>());
    auto* curve = childOfType<synth::ui::CurveEditorComponent>(*rig.card);
    ASSERT_NE(curve, nullptr);
    juce::TextButton* shapes = nullptr;
    for (auto* child : rig.card->getChildren())
        if (auto* button = dynamic_cast<juce::TextButton*>(child);
            button != nullptr && button->getButtonText() == "Shapes")
            shapes = button;
    ASSERT_NE(shapes, nullptr);
    const int plain = rig.card->getHeight();
    auto* shapeParam =
        dynamic_cast<juce::AudioParameterChoice*>(findParameterByID(rig.canvas.processor(rig.id), "shape"));
    ASSERT_NE(shapeParam, nullptr);

    int custom = 0;
    {
        Rig sync;
        sync.build(std::make_unique<LFOModule>());
        *dynamic_cast<juce::AudioParameterChoice*>(findParameterByID(sync.canvas.processor(sync.id), "shape")) =
            LFOModule::kCustomShapeIndex;
        custom = sync.card->getHeight();
    }
    ASSERT_GT(custom, plain);

    FadeAnimateGuard guard;
    *shapeParam = LFOModule::kCustomShapeIndex;
    EXPECT_TRUE(curve->isVisible());
    EXPECT_TRUE(shapes->isVisible());
    EXPECT_NEAR(curve->getAlpha(), 0.0f, kEps);
    EXPECT_NEAR(shapes->getAlpha(), 0.0f, kEps);
    EXPECT_EQ(rig.card->getHeight(), plain);
    EXPECT_GE(rig.neighbourY(), rig.card->getY() + custom);

    FadeVisibility::stepAllForTest(0.5f);
    EXPECT_GT(rig.card->getHeight(), plain);
    EXPECT_LT(rig.card->getHeight(), custom);
    EXPECT_TRUE(rig.card->getLocalBounds().contains(curve->getBounds()));

    FadeVisibility::stepAllForTest(1.0f);
    EXPECT_EQ(rig.card->getHeight(), custom);
    EXPECT_NEAR(curve->getAlpha(), 1.0f, kEps);

    *shapeParam = 0;
    EXPECT_TRUE(curve->isVisible()) << "fading out";
    EXPECT_EQ(rig.card->getHeight(), custom);
    FadeVisibility::stepAllForTest(1.0f);
    EXPECT_FALSE(curve->isVisible());
    EXPECT_FALSE(shapes->isVisible());
    EXPECT_EQ(rig.card->getHeight(), plain);
    EXPECT_EQ(rig.neighbourY(), rig.belowY);
}

TEST(CardFades, EnvelopeViewFadesOutAndBackInWhileTheCardFollows) {
    Rig rig;
    rig.build(std::make_unique<ADSRModule>());
    auto* body = rig.card->getCardBody();
    ASSERT_NE(body, nullptr);
    auto* view = body->findView(synth::CardView::Envelope);
    auto* toggle = toggleNamed(*rig.card, "Show Envelope Graph");
    ASSERT_NE(view, nullptr);
    ASSERT_NE(toggle, nullptr);
    ASSERT_TRUE(view->isVisible()) << "the designed default opens the graph";
    const int open = rig.card->getHeight();

    FadeAnimateGuard guard;
    toggle->setToggleState(false, juce::sendNotificationSync);
    EXPECT_TRUE(view->isVisible());
    EXPECT_EQ(rig.card->getHeight(), open);
    FadeVisibility::stepAllForTest(0.5f);
    EXPECT_NEAR(view->getAlpha(), 0.5f, kEps);
    const int mid = rig.card->getHeight();
    EXPECT_LT(mid, open);
    FadeVisibility::stepAllForTest(1.0f);
    EXPECT_FALSE(view->isVisible());
    const int closed = rig.card->getHeight();
    EXPECT_LT(closed, mid);

    toggle->setToggleState(true, juce::sendNotificationSync);
    EXPECT_TRUE(view->isVisible());
    EXPECT_NEAR(view->getAlpha(), 0.0f, kEps);
    EXPECT_EQ(rig.card->getHeight(), closed);
    FadeVisibility::stepAllForTest(0.5f);
    EXPECT_GT(rig.card->getHeight(), closed);
    EXPECT_LT(rig.card->getHeight(), open);
    FadeVisibility::stepAllForTest(1.0f);
    EXPECT_EQ(rig.card->getHeight(), open);
    EXPECT_FALSE(body->isFadeRunning());
}

TEST(CardFades, MoreRowControlsFadeAndTheCardGrowsWithThem) {
    const auto hidingLayout = [] {
        OscillatorModule module;
        return automaticLayoutHiding(module, {"level"});
    };
    Rig rig;
    rig.build(std::make_unique<OscillatorModule>(), hidingLayout());
    auto* body = rig.card->getCardBody();
    ASSERT_NE(body, nullptr);
    ASSERT_TRUE(body->hasMoreRow());
    auto* hidden = body->findWidget("level");
    ASSERT_NE(hidden, nullptr);
    EXPECT_FALSE(hidden->isVisible());
    const int folded = rig.card->getHeight();

    int unfolded = 0;
    {
        Rig sync;
        sync.build(std::make_unique<OscillatorModule>(), hidingLayout());
        sync.card->getCardBody()->setMoreUnfolded(true);
        unfolded = sync.card->getHeight();
    }
    ASSERT_GT(unfolded, folded);

    FadeAnimateGuard guard;
    body->setMoreUnfolded(true);
    EXPECT_TRUE(hidden->isVisible());
    EXPECT_NEAR(hidden->getAlpha(), 0.0f, kEps);
    // (the hidden knob's CV jack leaves the gutter at once, so the first frame may sit a little above `folded`)
    const int start = rig.card->getHeight();
    EXPECT_LE(start, folded);
    EXPECT_GE(rig.neighbourY(), rig.card->getY() + unfolded) << "room for the final size, once";
    FadeVisibility::stepAllForTest(0.5f);
    EXPECT_GT(rig.card->getHeight(), start);
    EXPECT_LT(rig.card->getHeight(), unfolded);
    EXPECT_NEAR(hidden->getAlpha(), 0.5f, kEps);
    FadeVisibility::stepAllForTest(1.0f);
    EXPECT_EQ(rig.card->getHeight(), unfolded);
    EXPECT_FALSE(body->isFadeRunning());

    body->setMoreUnfolded(false);
    EXPECT_TRUE(hidden->isVisible()) << "still there while it fades out";
    EXPECT_TRUE(body->isMoreUnfolded() == false);
    FadeVisibility::stepAllForTest(1.0f);
    EXPECT_FALSE(hidden->isVisible());
    EXPECT_EQ(rig.card->getHeight(), folded);
    EXPECT_EQ(rig.neighbourY(), rig.belowY);
}

TEST(CardFades, AConditionalSectionFadesInAndOutInsteadOfPopping) {
    const auto layout = layoutOf({section("main", std::nullopt, {param("shape"), param("level")}), [] {
                                      auto free = section("free", juce::String("Free running"),
                                                          {param("rateHz"), param("glide"), param("phase")});
                                      free.visibleWhen =
                                          synth::CardCondition{"mode", {"false"}, synth::CardConditionEffect::Show};
                                      return free;
                                  }()});
    Rig rig;
    rig.build(std::make_unique<LFOModule>(), layout);
    auto* body = rig.card->getCardBody();
    ASSERT_NE(body, nullptr);
    auto* glide = body->findWidget("glide");
    ASSERT_NE(glide, nullptr);
    ASSERT_FALSE(glide->isVisible()) << "Sync is on: the free-running section is away";
    const int away = rig.card->getHeight();
    const auto& plan = body->getPlan();
    auto* header = plan.sections[1].header;
    ASSERT_NE(header, nullptr);

    FadeAnimateGuard guard;
    setParam(*rig.canvas.processor(rig.id), "mode", 0.0f);
    body->flushPendingConditionUpdate();
    EXPECT_TRUE(glide->isVisible());
    EXPECT_TRUE(header->isVisible());
    EXPECT_NEAR(glide->getAlpha(), 0.0f, kEps);
    EXPECT_NEAR(header->getAlpha(), 0.0f, kEps);
    const int shown = rig.card->getHeight();
    EXPECT_GT(shown, away) << "the section's room is laid out at once, its controls fade into it";
    EXPECT_GE(rig.neighbourY(), rig.card->getY() + shown);
    EXPECT_FALSE(body->isSwapMotionRunning()) << "a section coming is not a swap";
    FadeVisibility::stepAllForTest(0.5f);
    EXPECT_NEAR(glide->getAlpha(), 0.5f, kEps);
    FadeVisibility::stepAllForTest(1.0f);
    EXPECT_NEAR(glide->getAlpha(), 1.0f, kEps);
    EXPECT_EQ(rig.card->getHeight(), shown);

    setParam(*rig.canvas.processor(rig.id), "mode", 1.0f);
    body->flushPendingConditionUpdate();
    EXPECT_TRUE(glide->isVisible()) << "fading out";
    EXPECT_TRUE(header->isVisible());
    EXPECT_EQ(rig.card->getHeight(), shown) << "the card closes up once the section has gone";
    EXPECT_GE(rig.neighbourY(), rig.card->getY() + shown);
    FadeVisibility::stepAllForTest(0.5f);
    EXPECT_NEAR(glide->getAlpha(), 0.5f, kEps);
    EXPECT_EQ(rig.card->getHeight(), shown);
    FadeVisibility::stepAllForTest(1.0f);
    EXPECT_FALSE(glide->isVisible());
    EXPECT_FALSE(header->isVisible());
    EXPECT_NEAR(glide->getAlpha(), 1.0f, kEps);
    EXPECT_EQ(rig.card->getHeight(), away);
    EXPECT_EQ(rig.neighbourY(), rig.belowY);
}

TEST(CardFades, ASectionThatComesBackMidFadeOutReversesFromWhereItIs) {
    const auto layout = layoutOf({section("main", std::nullopt, {param("shape")}), [] {
                                      auto free = section("free", std::nullopt, {param("rateHz"), param("glide")});
                                      free.visibleWhen =
                                          synth::CardCondition{"mode", {"false"}, synth::CardConditionEffect::Show};
                                      return free;
                                  }()});
    Rig rig;
    rig.build(std::make_unique<LFOModule>(), layout);
    auto* body = rig.card->getCardBody();
    auto* glide = body->findWidget("glide");
    FadeAnimateGuard guard;
    setParam(*rig.canvas.processor(rig.id), "mode", 0.0f);
    body->flushPendingConditionUpdate();
    FadeVisibility::stepAllForTest(1.0f);
    const int shown = rig.card->getHeight();

    setParam(*rig.canvas.processor(rig.id), "mode", 1.0f);
    body->flushPendingConditionUpdate();
    FadeVisibility::stepAllForTest(0.5f);
    const float mid = glide->getAlpha();
    EXPECT_NEAR(mid, 0.5f, kEps);

    setParam(*rig.canvas.processor(rig.id), "mode", 0.0f);
    body->flushPendingConditionUpdate();
    EXPECT_NEAR(glide->getAlpha(), mid, kEps) << "the reversal starts from the current opacity";
    FadeVisibility::stepAllForTest(1.0f);
    EXPECT_TRUE(glide->isVisible());
    EXPECT_NEAR(glide->getAlpha(), 1.0f, kEps);
    EXPECT_EQ(rig.card->getHeight(), shown);
}

TEST(CardFades, ASwapOfControlsKeepsItsOwnMotionAndStartsNoFade) {
    CardCanvas canvas;
    const auto layout =
        layoutOf({section("main", std::nullopt,
                          {param("shape"), showWhen(param("rateHz"), "mode", {"false"}),
                           showWhen(param("rateSync"), "mode", {"true"}), param("level"), param("glide")})});
    const auto id = canvas.add(std::make_unique<LFOModule>(), 0, 0, layout);
    canvas.editor.updateComponents();
    auto* card = canvas.card(id);
    auto* body = card->getCardBody();
    body->setForceAnimateForTest(true);
    const int height = card->getHeight();

    FadeAnimateGuard guard;
    setParam(*canvas.processor(id), "mode", 0.0f);
    body->flushPendingConditionUpdate();
    EXPECT_TRUE(body->isSwapMotionRunning());
    EXPECT_FALSE(body->isFadeRunning());
    EXPECT_EQ(card->getHeight(), height);
    body->finishSwapMotion();
}

TEST(CardFades, EveryFadeFrameReachesTheCardsCachedRasterAndASettledCardPaintsNothing) {
    // The section keeps the card's height while it fades, so only its opacity can invalidate the cached image.
    const auto layout = layoutOf({section("main", std::nullopt, {param("shape")}), [] {
                                      auto free = section("free", std::nullopt, {param("rateHz"), param("glide")});
                                      free.visibleWhen =
                                          synth::CardCondition{"mode", {"false"}, synth::CardConditionEffect::Show};
                                      return free;
                                  }()});
    Rig rig;
    rig.build(std::make_unique<LFOModule>(), layout);
    auto* body = rig.card->getCardBody();
    const auto* cache = rig.card->getRasterCacheForTest();
    ASSERT_NE(cache, nullptr);
    // A card paints through its cache when its parent paints it (paintEntireComponent on the card itself bypasses it).
    juce::Image image(juce::Image::ARGB, 1200, 900, true);
    const auto paintCard = [&] {
        juce::Graphics g(image);
        rig.canvas.editor.paintEntireComponent(g, false);
    };

    FadeAnimateGuard guard;
    setParam(*rig.canvas.processor(rig.id), "mode", 0.0f);
    body->flushPendingConditionUpdate();
    paintCard();
    const int first = cache->getRasterCountForTest();
    paintCard();
    EXPECT_EQ(cache->getRasterCountForTest(), first) << "nothing changed, nothing is painted again";

    FadeVisibility::stepAllForTest(0.5f);
    paintCard();
    const int mid = cache->getRasterCountForTest();
    EXPECT_GT(mid, first) << "a fade frame repaints the card through its cached image";
    FadeVisibility::stepAllForTest(1.0f);
    paintCard();
    const int last = cache->getRasterCountForTest();
    EXPECT_GT(last, mid) << "and so does the last one";
    paintCard();
    EXPECT_EQ(cache->getRasterCountForTest(), last) << "once landed the card is settled again";
}
