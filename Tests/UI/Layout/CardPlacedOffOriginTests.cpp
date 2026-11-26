// Guard for the bug kind "card-placed-off-origin": a placer that puts a new card at a negative x or y, where it is
// unpainted and unclickable (the origin is the canvas wall, docs/layout/layout.md#canvas-frame). Every way a card
// appears is run with its anchor at x = 0, 40 and 200 (a fed card close to the left wall, where `anchor - gap -
// width` goes negative), and each card it places must sit at x >= 0, y >= 0 and overlap no other card.
//
// Placers reached: the shared helpers (findFreeSlot, findFreeSlotBelow, findPolyMidiSlotBefore,
// computeDropFinalPosition, resolvePlacement: drop, AI add, library ghost), a new module beside its connections (both
// sides), Poly turned on from a card (also its Cmd+Z), the new poly track's Poly MIDI slot, paste at a point left of
// the wall, and duplicate.

#include "AI/AIStateMapper/AIStateMapper.h"
#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "Modules/ModuleBase.h"
#include "Modules/PolyMidiModule.h"
#include "ProjectBundle.h"
#include "TestPaths.h"
#include "UI/Graph/CardGlideAnimator/CardGlideAnimator.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Graph/NewModulePlacement/NewModulePlacement.h"
#include "UI/Graph/PolyChain/PolyChainController.h"
#include "UI/Layout/LayoutUtil.h"
#include "UI/Layout/ReducedMotion.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace {

using NodeID = juce::AudioProcessorGraph::NodeID;
using synth::LayoutUtil::kLayerGapX;
constexpr int kAnchors[] = {0, 40, 200};

struct PlacementRig {
    AudioEngine engine{AudioEngine::HostMode::Hosted};
    AppUndoManager undo;
    GraphEditor editor{engine, &undo};

    PlacementRig() {
        engine.getGraph().setPlayConfigDetails(2, 2, 44100.0, 512);
        undo.setGraphEditor(&editor);
        editor.setSize(3000, 1600);
        editor.getPolyChain().confirmForTest = [](const synth::ui::PolyChainPrompt&,
                                                  std::function<void(synth::ui::PolyChainChoice)> reply) {
            reply(synth::ui::PolyChainChoice::thisTrackOnly);
        };
    }

    juce::AudioProcessorGraph& graph() { return engine.getGraph(); }

    NodeID add(const juce::String& type, int x, int y) {
        auto node = graph().addNode(synth::AIStateMapper::createModule(type));
        const auto uuid = juce::Uuid().toDashedString();
        node->properties.set("uuid", uuid);
        if (auto* module = dynamic_cast<ModuleBase*>(node->getProcessor()))
            module->setNodeUuid(uuid);
        node->properties.set("x", x);
        node->properties.set("y", y);
        return node->nodeID;
    }

    std::vector<juce::Rectangle<int>> cards() {
        std::vector<juce::Rectangle<int>> out;
        for (auto* comp : editor.getModuleComponents())
            out.push_back(comp->getBounds());
        return out;
    }
};

// Every card: its model position and its component inside the wall; with `noOverlap`, no two overlapping.
void expectEveryCardOnTheCanvas(PlacementRig& rig, const juce::String& what, bool noOverlap = true) {
    for (auto* node : rig.graph().getNodes()) {
        EXPECT_GE((int)node->properties.getWithDefault("x", 0), 0) << what << ": a card's saved x is left of the wall";
        EXPECT_GE((int)node->properties.getWithDefault("y", 0), 0) << what << ": a card's saved y is above the wall";
    }
    const auto cards = rig.cards();
    for (size_t i = 0; i < cards.size(); ++i) {
        EXPECT_GE(cards[i].getX(), 0) << what << ": card " << i << " is left of the origin wall";
        EXPECT_GE(cards[i].getY(), 0) << what << ": card " << i << " is above the origin wall";
        for (size_t j = i + 1; noOverlap && j < cards.size(); ++j)
            EXPECT_FALSE(cards[i].intersects(cards[j])) << what << ": cards " << i << " and " << j << " overlap";
    }
}

void expectClear(juce::Point<int> at, juce::Point<int> size, const std::vector<synth::LayoutUtil::Box>& others,
                 const juce::String& what) {
    const juce::Rectangle<int> card(at.x, at.y, size.x, size.y);
    EXPECT_GE(card.getX(), 0) << what;
    EXPECT_GE(card.getY(), 0) << what;
    for (const auto& other : others)
        EXPECT_FALSE(card.intersects(other.rect)) << what << ": overlaps a card";
}

} // namespace

TEST(CardPlacedOffOrigin, EverySharedSlotHelperStaysOnTheCanvasForAnAnchorNearTheWall) {
    const auto size = GraphEditor::estimateModuleSize("Oscillator");
    for (const int anchor : kAnchors) {
        const juce::Rectangle<int> fed(anchor, 100, size.x, size.y);
        const std::vector<synth::LayoutUtil::Box> blockers{{NodeID(1), fed}};
        const juce::Point<int> desired(anchor - kLayerGapX - size.x, fed.getY());
        const auto tag = "anchor x=" + juce::String(anchor);

        expectClear(synth::LayoutUtil::findFreeSlot(desired, size.x, size.y, blockers, NodeID{}), size, blockers,
                    "findFreeSlot " + tag);
        expectClear(synth::LayoutUtil::findFreeSlotBelow(desired, size.x, size.y, blockers), size, blockers,
                    "findFreeSlotBelow " + tag);
        expectClear(GraphEditor::computeDropFinalPosition(desired, size.x, size.y, blockers, NodeID{}), size, blockers,
                    "computeDropFinalPosition " + tag);
        if (const auto slot = synth::LayoutUtil::findPolyMidiSlotBefore(fed, desired, size, {}, blockers))
            expectClear(*slot, size, blockers, "findPolyMidiSlotBefore " + tag);
    }
}

TEST(CardPlacedOffOrigin, ADropOrAiAddOrLibraryGhostNearTheWallLandsOnTheCanvas) {
    for (const int anchor : kAnchors) {
        PlacementRig rig;
        rig.add("Oscillator", anchor, 100);
        rig.editor.updateComponents();
        const auto size = GraphEditor::estimateModuleSize("Filter");
        const auto spot = rig.editor.resolvePlacement({anchor - kLayerGapX - size.x, 100}, size.x, size.y, NodeID{});
        const auto cards = rig.cards();
        std::vector<synth::LayoutUtil::Box> boxes;
        for (const auto& card : cards)
            boxes.push_back({NodeID(1), card});
        expectClear(spot, size, boxes, "resolvePlacement anchor x=" + juce::String(anchor));
    }
}

TEST(CardPlacedOffOrigin, ANewModuleBesideItsConnectionsStaysOnTheCanvasOnEitherSide) {
    for (const int anchor : kAnchors) {
        PlacementRig rig;
        const auto osc = rig.add("Oscillator", anchor, 100);
        const auto before = rig.add("Filter", -1, -1); // feeds the Oscillator: wants the left of it
        const auto after = rig.add("VCA", -1, -1);     // fed by the Oscillator: wants the right of it
        rig.graph().addConnection({{before, 0}, {osc, 0}});
        rig.graph().addConnection({{osc, 0}, {after, 0}});
        synth::placeNewModulesBesideConnections(rig.editor, {before, after});
        rig.editor.updateComponents();
        expectEveryCardOnTheCanvas(rig, "new module beside connections, anchor x=" + juce::String(anchor));
    }
}

TEST(CardPlacedOffOrigin, TurningPolyOnNearTheWallPutsPolyMidiOnTheCanvasBesideItsCard) {
    for (const int anchor : kAnchors) {
        PlacementRig rig;
        const auto source = rig.add("Midi Input", anchor, 900);
        const auto osc = rig.add("Oscillator", anchor, 100);
        rig.graph().addConnection({{source, juce::AudioProcessorGraph::midiChannelIndex},
                                   {osc, juce::AudioProcessorGraph::midiChannelIndex}});
        rig.editor.updateComponents();
        const auto tag = "Poly on, anchor x=" + juce::String(anchor);

        rig.editor.getPolyChain().turnPolyOn(osc);
        rig.editor.updateComponents();

        auto* poly = [&]() -> ModuleComponent* {
            for (auto* comp : rig.editor.getModuleComponents())
                if (dynamic_cast<PolyMidiModule*>(comp->getModule()) != nullptr)
                    return comp;
            return nullptr;
        }();
        ASSERT_NE(poly, nullptr) << tag;
        EXPECT_GE(poly->getX(), 0) << tag;
        EXPECT_LE(poly->getRight(),
                  [&] {
                      for (auto* comp : rig.editor.getModuleComponents())
                          if (comp->getNodeId() == osc)
                              return comp->getX();
                      return 0;
                  }())
            << tag << ": still just before the card it feeds";
        expectEveryCardOnTheCanvas(rig, tag);

        rig.undo.undo();
        rig.editor.updateComponents();
        expectEveryCardOnTheCanvas(rig, tag + " after undo");
    }
}

TEST(CardPlacedOffOrigin, ANewPolyTrackSlotForPolyMidiStaysOnTheCanvas) {
    // The arguments MainComponent::buildInstrumentEnvelopeChain gives the shared helper.
    const auto size = GraphEditor::estimateModuleSize("Poly MIDI");
    const auto instrumentSize = GraphEditor::estimateModuleSize("Oscillator");
    for (const int anchor : kAnchors) {
        const juce::Rectangle<int> trackIn(0, 100, 280, 200);
        const juce::Rectangle<int> instrument(anchor, 100, instrumentSize.x, instrumentSize.y);
        const std::vector<synth::LayoutUtil::Box> blockers{{NodeID(1), trackIn}};
        const auto slot =
            synth::LayoutUtil::findPolyMidiSlotBefore(instrument, {anchor - kLayerGapX - size.x, instrument.getY()},
                                                      size, trackIn.getUnion(instrument), blockers)
                .value_or(synth::LayoutUtil::findFreeSlotBelow({trackIn.getX(), trackIn.getBottom()}, size.x, size.y,
                                                               blockers));
        expectClear(slot, size, blockers, "new poly track, instrument x=" + juce::String(anchor));
    }
}

TEST(CardPlacedOffOrigin, PasteLeftOfTheWallAndDuplicateStayOnTheCanvas) {
    for (const int anchor : kAnchors) {
        PlacementRig rig;
        const auto osc = rig.add("Oscillator", anchor, 100);
        rig.editor.updateComponents();
        rig.editor.setSelectedNodes({osc});
        ASSERT_TRUE(rig.editor.copySelection());
        ASSERT_TRUE(rig.editor.pasteClipboardAt({anchor - 300, 100}));
        rig.editor.updateComponents();
        expectEveryCardOnTheCanvas(rig, "paste, anchor x=" + juce::String(anchor), /*noOverlap=*/false);

        rig.editor.setSelectedNodes({osc});
        ASSERT_TRUE(rig.editor.duplicateSelection());
        rig.editor.updateComponents();
        // A paste and a duplicate cascade a step down-right of the original on purpose, so they may overlap it.
        expectEveryCardOnTheCanvas(rig, "duplicate, anchor x=" + juce::String(anchor), /*noOverlap=*/false);
    }
}

// The patch slide that makes room for Poly MIDI glides (nothing pops), and Cmd+Z glides it back.
TEST(CardPlacedOffOrigin, ThePatchSlideForPolyMidiGlidesAndUndoGlidesBack) {
    synth::ui::setAnimationMode(synth::ui::AnimationMode::full);
    synth::ui::setReducedMotionForTest(false);
    PlacementRig rig;
    const auto source = rig.add("Midi Input", 40, 900);
    const auto osc = rig.add("Oscillator", 40, 104);
    rig.graph().addConnection(
        {{source, juce::AudioProcessorGraph::midiChannelIndex}, {osc, juce::AudioProcessorGraph::midiChannelIndex}});
    rig.editor.updateComponents();
    auto& glide = rig.editor.getCardGlideForTest();
    glide.setForceAnimateForTest(true); // not on screen: the glide is what is under test
    auto oscComp = [&] {
        for (auto* comp : rig.editor.getModuleComponents())
            if (comp->getNodeId() == osc)
                return comp;
        return static_cast<ModuleComponent*>(nullptr);
    };
    ASSERT_NE(oscComp(), nullptr);
    const int oldX = oscComp()->getX();

    rig.editor.getPolyChain().turnPolyOn(osc);
    const int newX = oscComp()->getX();
    ASSERT_GT(newX, oldX) << "the Oscillator slid right to make room";
    EXPECT_EQ(glide.currentRectFor(oscComp()).getX(), oldX) << "drawn where it was when the click lands";
    rig.editor.advanceCardGlideForTest(0.5f);
    const int midX = glide.currentRectFor(oscComp()).getX();
    EXPECT_GT(midX, oldX) << "mid-glide, not still at the old place";
    EXPECT_LT(midX, newX) << "mid-glide, not already at the new place";
    rig.editor.finishCardGlideForTest();
    EXPECT_EQ(oscComp()->getX(), newX);

    ASSERT_TRUE(rig.undo.undo());
    ASSERT_EQ(oscComp()->getX(), oldX);
    EXPECT_EQ(glide.currentRectFor(oscComp()).getX(), newX) << "undo draws it where it was, then glides back";
    rig.editor.advanceCardGlideForTest(0.5f);
    const int backX = glide.currentRectFor(oscComp()).getX();
    EXPECT_GT(backX, oldX);
    EXPECT_LT(backX, newX);
    rig.editor.finishCardGlideForTest();

    synth::ui::setAnimationMode(synth::ui::AnimationMode::followSystem);
    synth::ui::setReducedMotionForTest(std::nullopt);
}

namespace {

bool loadInto(PlacementRig& rig, const juce::File& bundle, synth::ViewDoc* view = nullptr) {
    synth::TimelineDoc timeline;
    synth::MidiRemoteProjectDoc midiRemote;
    rig.editor.detachAllModuleComponents();
    const auto result = synth::ProjectBundle::load(bundle, rig.graph(), timeline, rig.editor.getPatchDocument(),
                                                   rig.editor.getMacros(), midiRemote, nullptr, nullptr, nullptr, view);
    rig.editor.updateComponents();
    return result.ok;
}

juce::Point<int> savedPosition(PlacementRig& rig, const juce::String& type) {
    for (auto* node : rig.graph().getNodes())
        if (node->getProcessor()->getName() == type)
            return {(int)node->properties["x"], (int)node->properties["y"]};
    ADD_FAILURE() << "no " << type;
    return {};
}

} // namespace

// Regression test for FRO2364: a project saved with its Poly MIDI card left of the wall (x = -320) opened with the
// card unpainted and unclickable. Every committed project, opened, has every card on the canvas.
TEST(CardPlacedOffOrigin, EveryCommittedProjectOpensWithEveryCardOnTheCanvas) {
    int loaded = 0;
    for (const auto& entry : juce::RangedDirectoryIterator(juce::File(juce::String(UI_VERIFY_FIXTURES_DIR)), false,
                                                           "*.agsproj", juce::File::findDirectories)) {
        PlacementRig rig;
        if (!loadInto(rig, entry.getFile()))
            continue; // a fixture needing a plugin or sound pack this machine lacks
        ++loaded;
        expectEveryCardOnTheCanvas(rig, entry.getFile().getFileName(), /*noOverlap=*/false);
    }
    EXPECT_GT(loaded, 20) << "the sweep must actually open the committed projects";
}

TEST(CardPlacedOffOrigin, OpeningAPatchLeftOfTheWallSlidesItWholeAndPolyOffAndOnKeepsIt) {
    PlacementRig rig;
    ASSERT_TRUE(
        loadInto(rig, juce::File(juce::String(UI_VERIFY_FIXTURES_DIR)).getChildFile("poly-chord-into-output.agsproj")));
    const auto polyMidi = savedPosition(rig, "Poly MIDI");
    const auto osc = savedPosition(rig, "Oscillator");
    EXPECT_EQ(polyMidi.x, 0) << "the card that was furthest left now sits at the wall";
    EXPECT_EQ(osc.x - polyMidi.x, 360) << "the cards keep their places relative to each other (saved 40 and -320)";
    EXPECT_EQ(osc.y, polyMidi.y);
    expectEveryCardOnTheCanvas(rig, "poly-chord-into-output", /*noOverlap=*/false);

    // Poly off and on again from the Oscillator keeps every card on the canvas.
    NodeID oscId;
    for (auto* node : rig.graph().getNodes())
        if (node->getProcessor()->getName() == "Oscillator")
            oscId = node->nodeID;
    rig.editor.getPolyChain().polyPillClicked(oscId, false);
    rig.editor.updateComponents();
    expectEveryCardOnTheCanvas(rig, "after Poly off", /*noOverlap=*/false);
    rig.editor.getPolyChain().turnPolyOn(oscId);
    rig.editor.updateComponents();
    expectEveryCardOnTheCanvas(rig, "after Poly on again", /*noOverlap=*/false);
}

TEST(CardPlacedOffOrigin, ALoadSlidesAPatchAboveAndLeftOfTheWallWhole) {
    PlacementRig rig;
    const auto a = rig.add("Oscillator", -500, -40);
    const auto b = rig.add("Filter", 200, 300);
    const auto slide = synth::ProjectBundle::moveOntoCanvas(rig.graph(), rig.editor.getMacros());
    EXPECT_EQ(slide, juce::Point<int>(500, 40));
    auto at = [&](NodeID id) {
        auto* node = rig.graph().getNodeForId(id);
        return juce::Point<int>((int)node->properties["x"], (int)node->properties["y"]);
    };
    EXPECT_EQ(at(a), juce::Point<int>(0, 0));
    EXPECT_EQ(at(b), juce::Point<int>(700, 340));
    EXPECT_EQ(synth::ProjectBundle::moveOntoCanvas(rig.graph(), rig.editor.getMacros()), juce::Point<int>())
        << "a patch already on the canvas is left alone";
}
