// SmartConnectionSavedProjectTests.cpp
//
// Smart connections in a saved, reopened multi-track project (Tests/fixtures/saved-project/SavedProjectFixture.h):
// tracks in channel macros with ports, one folded, one open. A Delay aimed between the open track's Gate and EQ is
// offered, and on drop wires, only that track's cards, whatever the folded track's hidden cards sit under
// (docs/layout/smart-connections.md#one-container-at-a-time).

#include "../../../fixtures/saved-project/SavedProjectFixture.h"

#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Graph/SmartConnectionEngine/SmartConnectionEngine.h"

#include <set>
#include <tuple>

namespace {
using NodeID = juce::AudioProcessorGraph::NodeID;
using Edge = std::tuple<juce::uint32, int, juce::uint32, int>;

std::set<Edge> edgesOf(AudioEngine& engine) {
    std::set<Edge> out;
    for (const auto& c : engine.getGraph().getConnections())
        out.insert({c.source.nodeID.uid, c.source.channelIndex, c.destination.nodeID.uid, c.destination.channelIndex});
    return out;
}

NodeID nodeOf(MainComponent& mc, const juce::String& uuid) {
    for (auto* node : mc.getAudioEngine().getGraph().getNodes())
        if (node->properties["uuid"].toString() == uuid)
            return node->nodeID;
    return {};
}

ModuleComponent* cardOf(GraphEditor& editor, NodeID id) {
    for (auto* c : editor.getModuleComponents())
        if (c != nullptr && c->getNodeId() == id)
            return c;
    return nullptr;
}

/** Every node (ports included) that is in `macroId` or one of its nested macros. */
std::set<juce::uint32> nodesIn(MainComponent& mc, const juce::String& macroId) {
    std::set<juce::uint32> out;
    for (const auto& uuid : mc.getGraphEditor().getMacros().descendantMembers(macroId))
        out.insert(nodeOf(mc, uuid).uid);
    return out;
}
} // namespace

// Regression test for FRO1044: a Delay dropped in one track wired a folded track's hidden cards under it.
// In the reopened project, the folded track's hidden cards are laid out right over the open track's row, as in the
// project the bug was reported in. Every aim between the open
// track's Gate and EQ names only that track's cards, with or without Ctrl, and the drop changes no other cable.
TEST(SmartConnectionSavedProject, ADelayDroppedInOneTrackNeverWiresTheFoldedTrackUnderIt) {
    synth::test::SavedProjectFixture project;
    ASSERT_TRUE(project.wasSaved());
    auto mc = project.open();
    ASSERT_NE(mc, nullptr);
    auto& editor = mc->getGraphEditor();
    using Fixture = synth::test::SavedProjectFixture;
    const auto folded = Fixture::trackMacroId(*mc, 0), open = Fixture::trackMacroId(*mc, 2);
    ASSERT_TRUE(editor.getMacros().find(folded)->collapsed);
    ASSERT_FALSE(editor.getMacros().find(open)->collapsed);

    const auto gate = nodeOf(*mc, Fixture::memberUuidOfType(*mc, open, "Gate"));
    const auto eq = nodeOf(*mc, Fixture::memberUuidOfType(*mc, open, "Parametric EQ"));
    ASSERT_NE(cardOf(editor, gate), nullptr);
    ASSERT_NE(cardOf(editor, eq), nullptr);
    const auto gateAt = cardOf(editor, gate)->getBounds(), eqAt = cardOf(editor, eq)->getBounds();

    // The folded track's hidden Gate and EQ, 128 px above the open track's own.
    for (const auto& [type, over] :
         {std::pair<const char*, juce::Rectangle<int>>{"Gate", gateAt}, {"Parametric EQ", eqAt}}) {
        const auto hidden = nodeOf(*mc, Fixture::memberUuidOfType(*mc, folded, type));
        auto* card = cardOf(editor, hidden);
        ASSERT_NE(card, nullptr);
        ASSERT_FALSE(card->isVisible()) << type << " is hidden by the fold";
        auto* node = mc->getAudioEngine().getGraph().getNodeForId(hidden);
        node->properties.set("x", over.getX());
        node->properties.set("y", over.getY() - 128);
        card->setTopLeftPosition(over.getX(), over.getY() - 128);
    }

    const auto allowed = nodesIn(*mc, open);
    const auto before = edgesOf(mc->getAudioEngine());
    auto probe = synth::AIStateMapper::createModule("Delay");
    ASSERT_NE(probe, nullptr);
    const auto size = GraphEditor::estimateModuleSize("Delay");
    auto& smart = editor.getSmartConnections();
    editor.setMacroDropCandidate(open); // what the library drag sets over the open track's border

    for (const bool ctrl : {false, true}) {
        smart.setInsertModifierOverrideForTests(ctrl);
        for (int y = gateAt.getY(); y <= gateAt.getCentreY(); y += 8) {
            SmartConnectionEngine::DragPreviewState drag;
            drag.active = true;
            drag.probe = probe.get();
            drag.ghost = juce::Rectangle<int>(size.x, size.y).withCentre({(gateAt.getRight() + eqAt.getX()) / 2, y});
            drag.aim = drag.ghost;
            smart.refreshSmartSuggestions(drag);
            for (const auto& s : smart.getSmartSuggestions()) {
                EXPECT_EQ(allowed.count(s.neighborId.uid), 1u)
                    << "ctrl " << ctrl << ", y " << y << ": node " << s.neighborId.uid << " is not in the open track";
                if (s.isInsert)
                    EXPECT_EQ(allowed.count(s.upstreamId.uid), 1u) << "ctrl " << ctrl << ", y " << y;
            }
        }
    }

    // Drop at the last aim (Ctrl held): the Delay goes in, and every cable outside the open track is the same one.
    auto delay = mc->getAudioEngine().getGraph().addNode(std::move(probe));
    smart.applySmartSuggestions(delay->nodeID, /*recordUndo=*/false);
    std::set<Edge> foreignBefore, foreignAfter;
    for (const auto& e : before)
        if (allowed.count(std::get<0>(e)) == 0 && allowed.count(std::get<2>(e)) == 0)
            foreignBefore.insert(e);
    for (const auto& e : edgesOf(mc->getAudioEngine()))
        if (allowed.count(std::get<0>(e)) == 0 && allowed.count(std::get<2>(e)) == 0 &&
            std::get<0>(e) != delay->nodeID.uid && std::get<2>(e) != delay->nodeID.uid)
            foreignAfter.insert(e);
    for (const auto& e : edgesOf(mc->getAudioEngine()))
        if (std::get<0>(e) == delay->nodeID.uid || std::get<2>(e) == delay->nodeID.uid) {
            const auto other = std::get<0>(e) == delay->nodeID.uid ? std::get<2>(e) : std::get<0>(e);
            EXPECT_EQ(allowed.count(other), 1u) << "the Delay is wired to node " << other << " outside the open track";
        }
    EXPECT_EQ(foreignAfter, foreignBefore);
    editor.setMacroDropCandidate({});
}
