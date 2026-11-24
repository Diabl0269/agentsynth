// CardModeToggleNeighboursTests.cpp
//
// The guard for the live-reconfigure / layout-overlap bug kind on the canvas: clicking a card's mode switch (the Poly
// pill, on every card that has one) must not move the modules around it. Neighbours are placed clear of the card in
// every direction; after the click (and again after a second click that switches it back) each keeps the exact bounds
// it had, and the card keeps its own position. A card whose size changes with the switch may only push modules it now
// covers, and none of these are covered.
// Regression test for FRO2323: clicking Poly in the Filter was reported to move the modules around it to the left.

#include "../../../TestWait.h"
#include "AI/AIStateMapper/AIStateMapper.h"
#include "CardBodyTestHelpers.h"
#include "Modules/ModuleBase.h"
#include "UI/Graph/CardBody/CardBody.h"
#include "UI/Layout/LayoutUtil.h"
#include <map>

using namespace cardbody_test;

namespace {

constexpr int kClear = 3 * synth::LayoutUtil::kCollisionGap; // well clear of the card, so nothing is ever covered

// What the card looks like after its poly pill is clicked, and what each neighbour did.
void clickPolyAndExpectNeighboursStill(const juce::String& type) {
    SCOPED_TRACE(type);
    CardCanvas canvas;
    const auto id = canvas.add(synth::AIStateMapper::createModule(type), 800, 600);
    canvas.editor.updateComponents();
    auto* card = canvas.card(id);
    ASSERT_NE(card, nullptr);
    auto* body = card->getCardBody();
    ASSERT_NE(body, nullptr);
    auto* pill = dynamic_cast<juce::Button*>(body->findWidget("poly"));
    ASSERT_NE(pill, nullptr) << "a card with a poly parameter shows a poly pill";

    const auto home = card->getBounds();
    const auto probe = canvas.add(synth::AIStateMapper::createModule("Noise"), 0, 0);
    canvas.editor.updateComponents();
    const auto size = canvas.card(probe)->getBounds();
    canvas.engine.getGraph().removeNode(probe);
    canvas.editor.updateComponents();
    card = canvas.card(id);
    ASSERT_NE(card, nullptr);

    std::map<NodeID, juce::Rectangle<int>> expected;
    const std::vector<juce::Point<int>> spots = {
        {home.getRight() + kClear, home.getY()},
        {home.getX(), home.getBottom() + kClear},
        {home.getRight() + kClear, home.getBottom() + kClear},
        {home.getX() - size.getWidth() - kClear, home.getY()},
        {home.getX(), home.getY() - size.getHeight() - kClear},
    };
    for (const auto& spot : spots)
        expected[canvas.add(synth::AIStateMapper::createModule("Noise"), spot.x, spot.y)] = {};
    canvas.editor.updateComponents();
    card = canvas.card(id);
    ASSERT_NE(card, nullptr);
    ASSERT_EQ(card->getBounds().getPosition(), home.getPosition()) << "adding neighbours does not move the card";
    for (auto& [neighbour, rect] : expected) {
        ASSERT_NE(canvas.card(neighbour), nullptr);
        rect = canvas.card(neighbour)->getBounds();
        ASSERT_FALSE(rect.expanded(2).intersects(card->getBounds())) << "the neighbours start clear of the card";
    }

    for (int click = 0; click < 2; ++click) {
        auto* live = dynamic_cast<juce::Button*>(canvas.card(id)->getCardBody()->findWidget("poly"));
        ASSERT_NE(live, nullptr);
        live->triggerClick();
        synth::test::flushMessageQueue();
        ASSERT_NE(canvas.card(id), nullptr);
        EXPECT_EQ(canvas.card(id)->getPosition(), home.getPosition()) << "click " << click << ": the card moved";
        for (const auto& [neighbour, rect] : expected)
            if (auto* comp = canvas.card(neighbour))
                EXPECT_EQ(comp->getBounds(), rect)
                    << "click " << click << ": a neighbour moved (card is now "
                    << canvas.card(id)->getBounds().toString() << ", was " << home.toString() << ")";
    }
}

} // namespace

TEST(CardModeToggleNeighbours, ClickingPolyOnAnyCardLeavesTheModulesAroundItWhereTheyWere) {
    int checked = 0;
    for (const auto& type : libraryTypes()) {
        auto probe = synth::AIStateMapper::createModule(type);
        if (probe == nullptr || findParameterByID(probe.get(), "poly") == nullptr)
            continue;
        CardCanvas canvas;
        const auto id = canvas.add(std::move(probe), 0, 0);
        canvas.editor.updateComponents();
        if (canvas.card(id) == nullptr || canvas.card(id)->getCardBody() == nullptr ||
            canvas.card(id)->getCardBody()->findWidget("poly") == nullptr)
            continue; // no poly pill on this card
        clickPolyAndExpectNeighboursStill(type);
        ++checked;
    }
    EXPECT_GT(checked, 0);
}

TEST(CardModeToggleNeighbours, TheFilterIsAmongTheCardsWithAPolyPill) {
    CardCanvas canvas;
    const auto id = canvas.add(synth::AIStateMapper::createModule("Filter"), 0, 0);
    canvas.editor.updateComponents();
    ASSERT_NE(canvas.card(id), nullptr);
    EXPECT_NE(canvas.card(id)->getCardBody()->findWidget("poly"), nullptr);
}

// A real chain: the cards the click flips are wired together and stand a normal cable's length apart. Flipping Poly
// changes what each shows, never its size, so no card of the chain, and none beside it, may move.
TEST(CardModeToggleNeighbours, ClickingPolyInAWiredChainKeepsEveryCardWhereItWasAndItsSize) {
    CardCanvas canvas;
    const std::vector<juce::String> types = {"Oscillator", "Filter", "ADSR", "Delay", "Reverb"};
    std::vector<NodeID> ids;
    int x = 100;
    for (const auto& type : types) {
        ids.push_back(canvas.add(synth::AIStateMapper::createModule(type), x, 100));
        canvas.editor.updateComponents();
        x = canvas.card(ids.back())->getRight() + 3 * synth::LayoutUtil::kCollisionGap;
    }
    for (size_t i = 0; i + 1 < ids.size(); ++i)
        canvas.engine.getGraph().addConnection({{ids[i], 0}, {ids[i + 1], 0}});
    canvas.editor.updateComponents();

    std::vector<juce::Rectangle<int>> before;
    for (const auto id : ids) {
        ASSERT_NE(canvas.card(id), nullptr);
        before.push_back(canvas.card(id)->getBounds());
    }
    auto* pill = dynamic_cast<juce::Button*>(canvas.card(ids[1])->getCardBody()->findWidget("poly"));
    ASSERT_NE(pill, nullptr);
    for (int click = 0; click < 2; ++click) {
        dynamic_cast<juce::Button*>(canvas.card(ids[1])->getCardBody()->findWidget("poly"))->triggerClick();
        synth::test::flushMessageQueue();
        for (size_t i = 0; i < ids.size(); ++i)
            EXPECT_EQ(canvas.card(ids[i])->getBounds(), before[i]) << types[i] << " after click " << click;
    }
}

// The voice chain a track really has: a Sequencer on MIDI into the Oscillator, then audio on through the Filter. Poly
// inserts a Poly MIDI node between the Sequencer and the Oscillator; the cards that were there stay where they were
// (the new card takes a free place beside them).
TEST(CardModeToggleNeighbours, ClickingPolyOnAFilterOfASequencedChainLeavesTheExistingCardsWhereTheyWere) {
    CardCanvas canvas;
    const std::vector<juce::String> types = {"Sequencer", "Oscillator", "Filter", "ADSR", "Delay"};
    std::vector<NodeID> ids;
    int x = 100;
    for (const auto& type : types) {
        ids.push_back(canvas.add(synth::AIStateMapper::createModule(type), x, 100));
        canvas.editor.updateComponents();
        x = canvas.card(ids.back())->getRight() + 3 * synth::LayoutUtil::kCollisionGap;
    }
    auto& graph = canvas.engine.getGraph();
    const auto midi = juce::AudioProcessorGraph::midiChannelIndex;
    EXPECT_TRUE(graph.addConnection({{ids[0], midi}, {ids[1], midi}}));
    for (size_t i = 1; i + 1 < ids.size(); ++i)
        graph.addConnection({{ids[i], 0}, {ids[i + 1], 0}});
    canvas.editor.updateComponents();

    std::vector<juce::Rectangle<int>> before;
    for (const auto id : ids)
        before.push_back(canvas.card(id)->getBounds());
    auto* pill = dynamic_cast<juce::Button*>(canvas.card(ids[2])->getCardBody()->findWidget("poly"));
    ASSERT_NE(pill, nullptr);
    for (int click = 0; click < 2; ++click) {
        dynamic_cast<juce::Button*>(canvas.card(ids[2])->getCardBody()->findWidget("poly"))->triggerClick();
        synth::test::flushMessageQueue();
        for (size_t i = 0; i < ids.size(); ++i)
            EXPECT_EQ(canvas.card(ids[i])->getBounds(), before[i]) << types[i] << " after click " << click;
    }
}
