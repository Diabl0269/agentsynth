// AutoArrangeTests.cpp
//
// Auto-arrange through a real MainComponent: tracks created the way the app creates them (each one a collapsed
// channel macro), a shared LFO, then GraphEditor::autoArrange(). The pure layout is covered in
// Tests/UI/Layout/HierarchicalArrangeTests.cpp, macros on a bare canvas in AutoArrangeMacroTests.cpp.
// (docs/layout/layout.md#auto-arrange)

#include "../../Mixer/ChannelFlow/ChannelFlowTestFixture.h"
#include "AutoArrangeTestHelpers.h"

#include "AppUndoManager.h"
#include "Mixer/MasterSplice.h"

namespace {
using autoarrange_test::Snapshot;

class AutoArrangeTrackTest : public ChannelFlowTest {
protected:
    struct Rig {
        std::unique_ptr<MainComponent> mc;
        std::vector<juce::String> channelMacroIds; // in track order
        juce::AudioProcessorGraph::NodeID lfo;
    };

    // Three audio tracks plus one LFO modulating the Gate of the first two tracks' channels.
    Rig buildRig() {
        Rig rig;
        rig.mc = std::make_unique<MainComponent>(std::make_unique<MockProviderCFT>());
        rig.mc->setSize(1600, 900);
        rig.mc->getAudioEngine().suspendDeviceCallback();
        for (int i = 0; i < 3; ++i)
            addAudioTrack(*rig.mc);

        auto& editor = rig.mc->getGraphEditor();
        auto& graph = rig.mc->getAudioEngine().getGraph();
        for (const auto& track : rig.mc->getTimelineDoc().getTracks())
            for (const auto& macro : editor.getMacros().getAll())
                if (macro.name == track.name)
                    rig.channelMacroIds.push_back(macro.id);

        auto processor = synth::AIStateMapper::createModule("LFO");
        auto node = graph.addNode(std::move(processor));
        node->properties.set("x", 1500);
        node->properties.set("y", 700);
        node->properties.set("uuid", juce::Uuid().toDashedString());
        rig.lfo = node->nodeID;
        editor.updateComponents();

        for (int channel = 0; channel < 2; ++channel)
            routeLfoTo(*rig.mc, rig.lfo, rig.channelMacroIds[static_cast<size_t>(channel)]);
        return rig;
    }

    // One audio track and a loose LFO modulating the Gate inside its channel macro.
    Rig buildLoneTrackWithLfo(bool collapsed) {
        Rig rig;
        rig.mc = std::make_unique<MainComponent>(std::make_unique<MockProviderCFT>());
        rig.mc->setSize(1600, 900);
        rig.mc->getAudioEngine().suspendDeviceCallback();
        addAudioTrack(*rig.mc);
        auto& editor = rig.mc->getGraphEditor();
        auto& graph = rig.mc->getAudioEngine().getGraph();
        for (const auto& track : rig.mc->getTimelineDoc().getTracks())
            for (const auto& macro : editor.getMacros().getAll())
                if (macro.name == track.name)
                    rig.channelMacroIds.push_back(macro.id);
        auto node = graph.addNode(synth::AIStateMapper::createModule("LFO"));
        node->properties.set("x", 1500);
        node->properties.set("y", 700);
        node->properties.set("uuid", juce::Uuid().toDashedString());
        rig.lfo = node->nodeID;
        editor.updateComponents();
        routeLfoTo(*rig.mc, rig.lfo, rig.channelMacroIds[0]);
        editor.getMacroController().setMacroCollapsed(rig.channelMacroIds[0], collapsed);
        return rig;
    }

    // LFO -> the Gate inside `macroId`, on the first channel the engine accepts as a modulation target.
    static void routeLfoTo(MainComponent& mc, juce::AudioProcessorGraph::NodeID lfo, const juce::String& macroId) {
        auto& graph = mc.getAudioEngine().getGraph();
        const auto* macro = mc.getGraphEditor().getMacros().find(macroId);
        ASSERT_NE(macro, nullptr);
        for (auto* node : graph.getNodes()) {
            auto* mb = dynamic_cast<ModuleBase*>(node->getProcessor());
            if (mb == nullptr || mb->getModuleType() != ModuleType::Gate ||
                !macro->hasMember(node->properties["uuid"].toString()))
                continue;
            for (int channel = 1; channel < 8; ++channel)
                if (mc.getAudioEngine().addModRouting(lfo, 0, node->nodeID, channel) !=
                    juce::AudioProcessorGraph::NodeID{})
                    return;
        }
        FAIL() << "no Gate accepted a modulation routing";
    }

    static juce::Rectangle<int> cardOf(MainComponent& mc, const juce::String& macroId) {
        return mc.getGraphEditor().getMacroController().macroHullBounds(macroId).isEmpty()
                   ? mc.getGraphEditor().getMacroController().macroCableAnchorBounds(
                         *mc.getGraphEditor().getMacros().find(macroId))
                   : mc.getGraphEditor().getMacroController().macroHullBounds(macroId);
    }

    static juce::Rectangle<int> rectOfNode(MainComponent& mc, juce::AudioProcessorGraph::NodeID id) {
        for (auto* comp : mc.getGraphEditor().getModuleComponents())
            if (comp != nullptr && comp->getNodeId() == id)
                return comp->getBounds();
        return {};
    }
};
} // namespace

TEST_F(AutoArrangeTrackTest, ThreeTracksAndASharedLfoArrangeIntoAModulatorRowThenOneRowPerTrack) {
    auto rig = buildRig();
    ASSERT_EQ(rig.channelMacroIds.size(), 3u);
    auto& editor = rig.mc->getGraphEditor();

    editor.autoArrange();

    const auto lfo = rectOfNode(*rig.mc, rig.lfo);
    const auto t1 = cardOf(*rig.mc, rig.channelMacroIds[0]);
    const auto t2 = cardOf(*rig.mc, rig.channelMacroIds[1]);
    const auto t3 = cardOf(*rig.mc, rig.channelMacroIds[2]);
    ASSERT_FALSE(lfo.isEmpty());

    // The LFO feeds two rows, so it is the top row; the tracks follow in timeline order, one column wide.
    EXPECT_EQ(lfo.getY(), synth::LayoutUtil::kArrangeOriginY);
    EXPECT_LT(lfo.getBottom(), t1.getY());
    EXPECT_LT(t1.getBottom(), t2.getY());
    EXPECT_LT(t2.getBottom(), t3.getY());
    EXPECT_EQ(t1.getX(), t2.getX());
    EXPECT_EQ(t2.getX(), t3.getX());
    autoarrange_test::expectNoOverlaps(editor);

    // The dock (Master, Audio Output) sits right of everything, on the first row's y.
    const auto dock = synth::outputDockNodes(rig.mc->getAudioEngine().getGraph());
    ASSERT_FALSE(dock.empty());
    const int rightmost = std::max({lfo.getRight(), t1.getRight(), t2.getRight(), t3.getRight()});
    for (auto* node : dock) {
        const auto r = rectOfNode(*rig.mc, node->nodeID);
        EXPECT_GT(r.getX(), rightmost) << node->getProcessor()->getName();
        EXPECT_EQ(r.getY(), synth::LayoutUtil::kArrangeOriginY) << node->getProcessor()->getName();
    }
}

TEST_F(AutoArrangeTrackTest, ArrangingTwiceChangesNothing) {
    auto rig = buildRig();
    auto& editor = rig.mc->getGraphEditor();
    auto& graph = rig.mc->getAudioEngine().getGraph();

    editor.autoArrange();
    const auto first = autoarrange_test::snapshot(editor, graph);
    editor.autoArrange();

    EXPECT_TRUE(first == autoarrange_test::snapshot(editor, graph));
}

// With the channels expanded the stages inside them are real cards: Gate, EQ and Compressor share an x down the
// tracks, every hull contains its members and nothing overlaps at any level.
TEST_F(AutoArrangeTrackTest, ExpandedChannelsLineTheirStagesUpAcrossTracks) {
    auto rig = buildRig();
    auto& editor = rig.mc->getGraphEditor();
    auto& graph = rig.mc->getAudioEngine().getGraph();
    for (const auto& id : rig.channelMacroIds)
        editor.getMacroController().setMacroCollapsed(id, false);

    editor.autoArrange();

    auto xOfStage = [&](const juce::String& macroId, ModuleType type) {
        const auto* macro = editor.getMacros().find(macroId);
        for (auto* node : graph.getNodes())
            if (auto* mb = dynamic_cast<ModuleBase*>(node->getProcessor());
                mb != nullptr && mb->getModuleType() == type && macro->hasMember(node->properties["uuid"].toString()))
                return rectOfNode(*rig.mc, node->nodeID).getX();
        return -1;
    };
    for (auto type : {ModuleType::Gate, ModuleType::ParametricEQ, ModuleType::Compressor, ModuleType::ChannelStrip}) {
        const int x = xOfStage(rig.channelMacroIds[0], type);
        ASSERT_GE(x, 0);
        EXPECT_EQ(xOfStage(rig.channelMacroIds[1], type), x);
        EXPECT_EQ(xOfStage(rig.channelMacroIds[2], type), x);
    }
    const auto h1 = cardOf(*rig.mc, rig.channelMacroIds[0]);
    const auto h2 = cardOf(*rig.mc, rig.channelMacroIds[1]);
    const auto h3 = cardOf(*rig.mc, rig.channelMacroIds[2]);
    EXPECT_LT(h1.getBottom(), h2.getY());
    EXPECT_LT(h2.getBottom(), h3.getY());
    EXPECT_GE(h1.getX(), 0);
    autoarrange_test::expectNoOverlaps(editor);

    const auto first = autoarrange_test::snapshot(editor, graph);
    editor.autoArrange();
    EXPECT_TRUE(first == autoarrange_test::snapshot(editor, graph));
}

// One undo restores node x/y AND the collapsed cards' persisted bounds; redo re-applies both.
TEST_F(AutoArrangeTrackTest, OneUndoRestoresNodePositionsAndCardBoundsAndRedoReappliesThem) {
    auto rig = buildRig();
    auto& editor = rig.mc->getGraphEditor();
    auto& graph = rig.mc->getAudioEngine().getGraph();
    // Scatter the cards so the arrangement has real work to do.
    int shift = 0;
    for (const auto& id : rig.channelMacroIds)
        editor.getMacroController().moveUnitBy("m:" + id, {shift += 168, 104});
    const auto before = autoarrange_test::snapshot(editor, graph);

    editor.autoArrange();
    const auto after = autoarrange_test::snapshot(editor, graph);
    ASSERT_FALSE(before == after);
    ASSERT_NE(before.macroBounds, after.macroBounds) << "the cards moved, so their bounds must be part of the step";

    auto& undo = rig.mc->getUndoManager();
    ASSERT_TRUE(undo.undo());
    EXPECT_TRUE(before == autoarrange_test::snapshot(editor, graph));

    ASSERT_TRUE(undo.redo());
    EXPECT_TRUE(after == autoarrange_test::snapshot(editor, graph));
}

TEST_F(AutoArrangeTrackTest, TrackOrderFollowsTheTimelineNotNodeCreationOrder) {
    auto rig = buildRig();
    auto& editor = rig.mc->getGraphEditor();
    auto& doc = rig.mc->getTimelineDoc();
    ASSERT_GE(doc.getTracks().size(), 3u);
    const auto firstId = doc.getTracks().front().id;
    // Move the first track to the end of the timeline: its channel must now be the bottom row.
    ASSERT_TRUE(doc.moveTrack(firstId, static_cast<int>(doc.getTracks().size()) - 1));

    editor.autoArrange();

    const auto movedChannel = rig.channelMacroIds.front();
    const auto bottom = cardOf(*rig.mc, movedChannel);
    for (size_t i = 1; i < rig.channelMacroIds.size(); ++i)
        EXPECT_LT(cardOf(*rig.mc, rig.channelMacroIds[i]).getY(), bottom.getY());
}

// A loose LFO that only modulates a stage inside the track's channel macro belongs right before that macro, in the
// track's row, whether the macro is open or collapsed.
TEST_F(AutoArrangeTrackTest, ALooseLfoFeedingOnlyTheChannelMacroSitsRightBeforeItCollapsed) {
    auto rig = buildLoneTrackWithLfo(/*collapsed=*/true);
    rig.mc->getGraphEditor().autoArrange();
    autoarrange_test::expectBeforeInRow(rectOfNode(*rig.mc, rig.lfo), cardOf(*rig.mc, rig.channelMacroIds[0]),
                                        "track collapsed");
}

TEST_F(AutoArrangeTrackTest, ALooseLfoFeedingOnlyTheChannelMacroSitsRightBeforeItOpen) {
    auto rig = buildLoneTrackWithLfo(/*collapsed=*/false);
    rig.mc->getGraphEditor().autoArrange();
    autoarrange_test::expectBeforeInRow(rectOfNode(*rig.mc, rig.lfo), cardOf(*rig.mc, rig.channelMacroIds[0]),
                                        "track open");
}

TEST_F(AutoArrangeTrackTest, TheLfoPlacementIsIdempotent) {
    auto rig = buildLoneTrackWithLfo(/*collapsed=*/false);
    auto& editor = rig.mc->getGraphEditor();
    editor.autoArrange();
    const auto first = autoarrange_test::snapshot(editor, rig.mc->getAudioEngine().getGraph());
    editor.autoArrange();
    EXPECT_TRUE(first == autoarrange_test::snapshot(editor, rig.mc->getAudioEngine().getGraph()));
}
