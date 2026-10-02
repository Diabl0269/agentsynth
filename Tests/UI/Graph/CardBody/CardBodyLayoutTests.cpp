// CardBodyLayoutTests.cpp
//
// Source/UI/Graph/CardBody/: the automatic layout reproduces the generic card (order, heights, the size
// estimate), one function measures and places, the More row appears only when a layout hides something
// and folds/unfolds the card's height, and no value drag ever changes a card's height. Pixel parity of
// every built card with the pre-CardBody card is CardBodyGoldenTests.cpp.

#include "../GraphEditor/GraphEditorTestHelpers.h"
#include "AI/AIStateMapper/AIStateMapper.h"
#include "CardBodyTestHelpers.h"
#include "Modules/FilterModule.h"
#include "Modules/ModuleBase.h"
#include "UI/Graph/CardBody/CardBodyMeasure.h"

using namespace cardbody_test;
using synth::CardBodyItem;
using synth::CardBodyPlan;

namespace {

int kindRank(CardBodyItem::Kind kind) {
    switch (kind) {
    case CardBodyItem::Kind::Choice:
        return 0;
    case CardBodyItem::Kind::Toggle:
        return 1;
    case CardBodyItem::Kind::View:
        return 2;
    case CardBodyItem::Kind::Knob:
        return 3;
    default:
        return -1; // the automatic layout never draws the other kinds
    }
}

int declarationIndex(juce::AudioProcessor& module, const juce::RangedAudioParameter* param) {
    return module.getParameters().indexOf(const_cast<juce::RangedAudioParameter*>(param));
}

juce::Component* childWithId(juce::Component& parent, const juce::String& id) {
    for (auto* child : parent.getChildren())
        if (child->getComponentID() == id)
            return child;
    return nullptr;
}

juce::Slider* knobNamed(ModuleComponent& card, const juce::String& name) {
    return dynamic_cast<juce::Slider*>(childWithId(card, name));
}

} // namespace

TEST(CardBodyLayout, AutomaticLayoutStacksCombosTogglesThresholdThenKnobsInDeclarationOrder) {
    for (const auto& type : synth::AIStateMapper::moduleFactoryTypeNames()) {
        SCOPED_TRACE(type.toStdString());
        auto module = synth::AIStateMapper::createModule(type);
        if (module == nullptr)
            continue;
        const auto plan = CardBodyPlan::forModule(*module, std::nullopt);
        ASSERT_EQ(plan.sections.size(), 1u);
        EXPECT_TRUE(plan.more.empty()) << "the automatic layout never hides anything";
        int lastRank = -1, lastIndex = -1;
        for (int index : plan.sections.front().items) {
            const auto& item = plan.items[(size_t)index];
            const int rank = kindRank(item.kind);
            EXPECT_GE(rank, lastRank) << "combos, then toggles, then the Threshold view, then knobs";
            if (rank != lastRank)
                lastIndex = -1;
            if (item.param != nullptr) {
                const int declared = declarationIndex(*module, item.param);
                EXPECT_GT(declared, lastIndex) << "each group in declaration order";
                lastIndex = declared;
            }
            lastRank = rank;
        }
    }
}

// The library drag ghost and drop placement read estimateModuleSize; every card drawn from layout data
// is now measured from its card-body plan rather than a hand-kept table, and must equal the real card.
TEST(CardBodyLayout, EveryDataDrivenLibraryCardIsMeasuredFromItsPlan) {
    AudioEngine engine;
    GraphEditor editor(engine);
    int measured = 0;
    for (const auto& type : libraryTypes()) {
        SCOPED_TRACE(type.toStdString());
        auto module = synth::AIStateMapper::createModule(type);
        ASSERT_NE(module, nullptr);
        auto* mb = dynamic_cast<ModuleBase*>(module.get());
        const bool dataDriven = mb != nullptr && synth::cardBodyLayoutIsDataDriven(*module) &&
                                mb->getModuleType() != ModuleType::AudioInput;
        const auto size = synth::measureDataDrivenCardSize(type);
        EXPECT_EQ(size.has_value(), dataDriven);
        if (!size.has_value())
            continue;
        ++measured;
        ModuleComponent card(module.get(), NodeID(1), editor);
        EXPECT_EQ(*size, juce::Point<int>(card.getWidth(), card.getHeight()));
    }
    EXPECT_GT(measured, 20) << "most library cards are drawn from layout data";
}

TEST(CardBodyLayout, MeasureAndApplyReturnTheSameHeight) {
    CardCanvas canvas;
    std::vector<NodeID> ids;
    int x = 0;
    for (const auto& type : libraryTypes()) {
        auto module = synth::AIStateMapper::createModule(type);
        if (module == nullptr || !synth::cardBodyLayoutIsDataDriven(*module))
            continue;
        // A second copy of each card hides its first parameter, so the More row is measured too.
        auto copy = synth::AIStateMapper::createModule(type);
        const auto plan = CardBodyPlan::forModule(*copy, std::nullopt);
        std::optional<synth::CardLayout> hiding;
        if (!plan.items.empty() && plan.items.front().param != nullptr)
            hiding = automaticLayoutHiding(*copy, {plan.items.front().param->paramID});
        ids.push_back(canvas.add(std::move(module), x, 0));
        ids.push_back(canvas.add(std::move(copy), x, 1000, hiding));
        x += 300;
    }
    canvas.editor.updateComponents();

    for (auto id : ids) {
        auto* card = canvas.card(id);
        ASSERT_NE(card, nullptr);
        auto* body = card->getCardBody();
        ASSERT_NE(body, nullptr);
        SCOPED_TRACE(card->getModule()->getName().toStdString());
        const auto g = synth::cardbody::BodyGeometry::forCardWidth(card->getWidth());
        for (bool unfolded : {false, true}) {
            if (body->hasMoreRow())
                body->setMoreUnfolded(unfolded);
            const int measuredBody = body->layout(100, g, /*apply*/ false);
            EXPECT_EQ(measuredBody, body->layout(100, g, /*apply*/ true));
            EXPECT_EQ(body->layoutMoreRow(500, g, false), body->layoutMoreRow(500, g, true));
        }
    }
}

TEST(CardBodyLayout, NoLibraryCardShowsAMoreRowWithoutAHiddenParameter) {
    CardCanvas canvas;
    std::vector<NodeID> ids;
    int x = 0;
    for (const auto& type : libraryTypes())
        if (auto module = synth::AIStateMapper::createModule(type)) {
            ids.push_back(canvas.add(std::move(module), x, 0));
            x += 600;
        }
    canvas.editor.updateComponents();
    for (auto id : ids) {
        auto* card = canvas.card(id);
        ASSERT_NE(card, nullptr);
        EXPECT_EQ(childWithId(*card, "cardMoreRow"), nullptr) << card->getModule()->getName();
        if (auto* body = card->getCardBody())
            EXPECT_FALSE(body->hasMoreRow());
    }
}

// The More row: a hidden parameter's widget is built and bound but folded away under a titled,
// focusable button at the bottom of the card; unfolding grows the card, folding gives the height back.
TEST(CardBodyLayout, HiddenParameterFoldsIntoTheMoreRowAndUnfoldingGrowsTheCard) {
    CardCanvas canvas;
    auto filter = std::make_unique<FilterModule>();
    const auto layout = automaticLayoutHiding(*filter, {"drive"});
    const auto plain = canvas.add(std::make_unique<FilterModule>(), 0, 0);
    const auto hidden = canvas.add(std::move(filter), 400, 0, layout);
    canvas.editor.updateComponents();
    auto* plainCard = canvas.card(plain);
    auto* card = canvas.card(hidden);
    ASSERT_NE(card, nullptr);
    ASSERT_NE(plainCard, nullptr);

    auto* more = dynamic_cast<juce::Button*>(childWithId(*card, "cardMoreRow"));
    ASSERT_NE(more, nullptr);
    EXPECT_EQ(more->getTitle(), "More controls (1)");
    EXPECT_TRUE(more->getTooltip().isNotEmpty());
    EXPECT_TRUE(more->getWantsKeyboardFocus());
    EXPECT_TRUE(more->isVisible());
    const int moreIndex = card->getIndexOfChildComponent(more);
    for (auto* child : card->getChildren())
        if (dynamic_cast<juce::Slider*>(child) != nullptr || dynamic_cast<juce::ComboBox*>(child) != nullptr)
            EXPECT_LT(card->getIndexOfChildComponent(child), moreIndex) << "built after every body control";

    auto* drive = knobNamed(*card, "Drive");
    ASSERT_NE(drive, nullptr);
    EXPECT_FALSE(drive->isVisible()) << "folded";
    EXPECT_GE(more->getY(), knobNamed(*card, "Level")->getBottom()) << "the More row sits under the body";
    EXPECT_LE(more->getBottom(), card->getHeight());

    const int folded = card->getHeight();
    ASSERT_TRUE(static_cast<juce::Component*>(more)->keyPressed(juce::KeyPress(juce::KeyPress::returnKey)));
    EXPECT_TRUE(card->getCardBody()->isMoreUnfolded());
    EXPECT_TRUE(drive->isVisible());
    EXPECT_GT(card->getHeight(), folded);
    EXPECT_GT(drive->getY(), more->getY()) << "unfolded controls sit under the More button";
    EXPECT_TRUE(card->getLocalBounds().contains(drive->getBounds()));

    ASSERT_TRUE(static_cast<juce::Component*>(more)->keyPressed(juce::KeyPress(juce::KeyPress::spaceKey)));
    EXPECT_FALSE(card->getCardBody()->isMoreUnfolded());
    EXPECT_FALSE(drive->isVisible());
    EXPECT_EQ(card->getHeight(), folded);
}

TEST(CardBodyLayout, MoreButtonIsATabStopAfterTheBodyControls) {
    CardCanvas canvas;
    auto filter = std::make_unique<FilterModule>();
    const auto layout = automaticLayoutHiding(*filter, {"resonance"});
    const auto id = canvas.add(std::move(filter), 0, 0, layout);
    canvas.editor.updateComponents();
    auto* card = canvas.card(id);
    ASSERT_NE(card, nullptr);
    auto* more = childWithId(*card, "cardMoreRow");
    ASSERT_NE(more, nullptr);

    juce::KeyboardFocusTraverser traverser;
    const auto stops = traverser.getAllComponents(card);
    const auto moreAt = std::find(stops.begin(), stops.end(), more);
    ASSERT_NE(moreAt, stops.end()) << "the More button is keyboard-reachable";
    for (auto it = stops.begin(); it != moreAt; ++it)
        EXPECT_NE(*it, knobNamed(*card, "Resonance")) << "a folded control is not a Tab stop";
    const auto cutoffAt = std::find(stops.begin(), stops.end(), knobNamed(*card, "Cutoff"));
    EXPECT_LT(cutoffAt, moreAt);
}

// Card height drives canvas make-room, so a card that grew while a knob turned would shove its
// neighbours around under the cursor. Every knob on every library card (and a card with an unfolded
// More row) is dragged end to end with a real synthesized mouse drag.
TEST(CardBodyLayout, CardHeightNeverChangesWhileAKnobIsDragged) {
    CardCanvas canvas;
    std::vector<NodeID> ids;
    int x = 0;
    for (const auto& type : libraryTypes())
        if (auto module = synth::AIStateMapper::createModule(type);
            module != nullptr && synth::cardBodyLayoutIsDataDriven(*module)) {
            ids.push_back(canvas.add(std::move(module), x, 0));
            x += 300;
        }
    auto filter = std::make_unique<FilterModule>();
    const auto layout = automaticLayoutHiding(*filter, {"drive"});
    const auto withMore = canvas.add(std::move(filter), x, 0, layout);
    ids.push_back(withMore);
    canvas.editor.updateComponents();
    canvas.card(withMore)->getCardBody()->setMoreUnfolded(true);

    for (auto id : ids) {
        auto* card = canvas.card(id);
        ASSERT_NE(card, nullptr);
        for (auto* child : card->getChildren()) {
            auto* knob = dynamic_cast<juce::Slider*>(child);
            if (knob == nullptr || !knob->isVisible())
                continue;
            SCOPED_TRACE((card->getModule()->getName() + " / " + knob->getComponentID()).toStdString());
            const int height = card->getHeight();
            const auto centre = knob->getLocalBounds().getCentre();
            const auto mods = plainLeftClick();
            knob->mouseDown(makeModuleClickWithMods(*knob, centre, mods));
            for (int dy : {-200, 200, -40})
                knob->mouseDrag(makeModuleClickWithMods(*knob, centre + juce::Point<int>(0, dy), mods));
            EXPECT_EQ(card->getHeight(), height) << "mid-drag";
            knob->mouseUp(makeModuleClickWithMods(*knob, centre + juce::Point<int>(0, -40), mods));
            EXPECT_EQ(card->getHeight(), height);
        }
    }
}
