// AutoArrangeTests.cpp
//
// Auto-arrange through a real MainComponent: tracks created the way the app creates them (each one a collapsed
// channel macro), a shared LFO, then GraphEditor::autoArrange(). The pure layout is covered in
// Tests/UI/Layout/HierarchicalArrangeTests.cpp, macros on a bare canvas in AutoArrangeMacroTests.cpp. The end of this
// file checks the layout units themselves on saved projects (nothing invisible, every insert gap lands where aimed).
// (docs/layout/layout.md#auto-arrange)

#include "../../Mixer/ChannelFlow/ChannelFlowTestFixture.h"
#include "AutoArrangeTestHelpers.h"

#include "AppUndoManager.h"
#include "Mixer/MasterSplice.h"
#include "TestPaths.h"
#include "UI/Layout/InsertGap/InsertGapPlan.h"

#include <algorithm>
#include <functional>
#include <vector>

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
    // Regression test for FRO801: the LFO used to sit straight above the first channel; it belongs a column before
    // both channels it modulates.
    EXPECT_LE(lfo.getRight() + synth::LayoutUtil::kLayerGapX, t1.getX());
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

// ---- Layout units on saved projects -----------------------------------------------------------------------------
// Every rect the canvas places against (the layout units make-room, auto-arrange and the insert gap move, and the
// placement blockers a dropped card dodges) must be something the user can see: a showing module card, a showing folded
// macro card, or an open macro's painted border. A rect with nothing drawn there is a "ghost": a card dragged over
// empty canvas snags on it. And wherever a dragged card would open an insert gap, it has to land where it is aimed, not
// at a slot across the canvas (the next card after a wide open macro). Each saved project is opened through the real
// open path, then every level is walked.

namespace {

struct Drawn {
    juce::String what;
    juce::Rectangle<int> rect;
};

// What the user sees on the canvas: showing module cards, showing folded macro cards, open macros' borders.
std::vector<Drawn> drawnFootprints(GraphEditor& editor) {
    std::vector<Drawn> drawn;
    for (auto* comp : editor.getModuleComponents())
        if (comp != nullptr && comp->isVisible())
            drawn.push_back({"card " + comp->getTitle(), comp->getBounds()});
    // Folded macro cards are found by the name test mode reads them by ("macroCard:<macro name>").
    std::function<void(juce::Component&)> walk = [&](juce::Component& c) {
        for (auto* child : c.getChildren()) {
            if (child->isVisible() && child->getName().startsWith("macroCard"))
                drawn.push_back({child->getName(), child->getBounds()});
            walk(*child);
        }
    };
    walk(editor);
    auto& macros = editor.getMacros();
    for (const auto& macro : macros.getAll())
        if (!macros.isEffectivelyCollapsed(macro.id))
            drawn.push_back({"open macro " + macro.name, editor.getMacroController().macroHullBounds(macro.id)});
    return drawn;
}

const synth::LayoutUtil::LayoutUnit* findUnit(const std::vector<synth::LayoutUtil::LayoutUnit>& units,
                                              const juce::String& key) {
    const auto it = std::find_if(units.begin(), units.end(), [&key](const auto& u) { return u.key == key; });
    return it != units.end() ? &*it : nullptr;
}

bool isDrawn(const std::vector<Drawn>& drawn, juce::Rectangle<int> rect) {
    return std::any_of(drawn.begin(), drawn.end(), [&rect](const Drawn& d) { return d.rect == rect; });
}

class LayoutUnitFootprint : public ChannelFlowTest {
protected:
    // Opens a copy of a committed fixture (the open path may write sidecar files).
    void open(MainComponent& mc, const char* name) {
        const auto src = juce::File(juce::String(UI_VERIFY_FIXTURES_DIR)).getChildFile(name);
        ASSERT_TRUE(src.isDirectory()) << src.getFullPathName();
        copy_ = juce::File::createTempFile(".agsproj");
        ASSERT_TRUE(src.copyDirectoryTo(copy_));
        mc.setSize(1600, 900);
        mc.getAudioEngine().suspendDeviceCallback();
        ASSERT_TRUE(mc.openProjectForTest(copy_));
    }
    void TearDown() override {
        copy_.deleteRecursively();
        ChannelFlowTest::TearDown();
    }
    juce::File copy_;
};

// Every layout unit at every level, and every placement blocker, is drawn where it says.
void expectNoGhosts(GraphEditor& editor, const char* project) {
    SCOPED_TRACE(project);
    const auto drawn = drawnFootprints(editor);
    auto& ctl = editor.getMacroController();
    std::vector<juce::String> containers{juce::String()};
    for (const auto& macro : editor.getMacros().getAll())
        if (!editor.getMacros().isEffectivelyCollapsed(macro.id))
            containers.push_back(macro.id);
    int units = 0;
    for (const auto& container : containers)
        for (const auto& unit : ctl.buildLayoutUnits(container)) {
            ++units;
            EXPECT_TRUE(isDrawn(drawn, unit.rect)) << "layout unit " << unit.key << " in '" << container << "' at "
                                                   << unit.rect.toString() << " has nothing drawn there";
        }
    EXPECT_GT(units, 0);
    for (const auto& box : ctl.placementBlockers({}))
        EXPECT_TRUE(isDrawn(drawn, box.rect))
            << "placement blocker at " << box.rect.toString() << " has nothing drawn there";
}

// Every gap a card of `size` opens over a level (a pointer every 40 px over the level's units and a margin around them,
// over that level's own space) puts the card where it is aimed.
void expectEveryGapLandsWhereAimed(GraphEditor& editor, const char* project, juce::Point<int> size) {
    SCOPED_TRACE(project);
    namespace ig = synth::insert_gap;
    auto& ctl = editor.getMacroController();
    std::vector<juce::String> containers{juce::String()};
    for (const auto& macro : editor.getMacros().getAll())
        if (!editor.getMacros().isEffectivelyCollapsed(macro.id))
            containers.push_back(macro.id);
    int gaps = 0;
    for (const auto& container : containers) {
        const auto units = ctl.buildLayoutUnits(container);
        juce::Rectangle<int> area;
        for (const auto& unit : units)
            area = area.isEmpty() ? unit.rect : area.getUnion(unit.rect);
        area = area.expanded(size.x, size.y);
        for (int y = area.getY(); y < area.getBottom(); y += 40)
            for (int x = area.getX(); x < area.getRight(); x += 40) {
                if (ctl.macroHullAt({x, y}) != container)
                    continue; // over a border the card is not in: that is a join or leave, and no gap opens (InsertGap)
                const auto target = ig::pickTarget(units, {x, y}, size);
                if (!target.has_value())
                    continue;
                const auto plan = ig::planBefore(units, *target, size);
                if (!plan.has_value() || plan->moves.empty())
                    continue; // a gap that already fits opens nothing, and the drop is an ordinary one
                ++gaps;
                // The card lands level with the anchor, so the aimed card overlaps the anchor across the axis. Along it
                // the slot is the gap the pointer is over, up to an edge band deep inside a card (pickTarget).
                const auto* anchor = findUnit(units, target->anchorKey);
                ASSERT_NE(anchor, nullptr);
                const juce::Rectangle<int> aimed(x - size.x / 2, y - size.y / 2, size.x, size.y);
                const bool across =
                    target->axis == ig::Axis::Row
                        ? aimed.getY() < anchor->rect.getBottom() && anchor->rect.getY() < aimed.getBottom()
                        : aimed.getX() < anchor->rect.getRight() && anchor->rect.getX() < aimed.getRight();
                EXPECT_TRUE(across) << "aimed at " << aimed.toString() << " in '" << container
                                    << "', the gap in front of " << target->anchorKey << " lands the card at "
                                    << plan->slot.toString();
                // An open macro's hull spans many cards, so the slot itself (at its lead edge across) has to be across
                // from the aim, or a card aimed above the middle of a wide macro lands at its left edge.
                const auto* macro =
                    target->anchorKey.startsWith("m:")
                        ? editor.getMacros().find(target->anchorKey.fromFirstOccurrenceOf("m:", false, false))
                        : nullptr;
                if (macro != nullptr && !macro->collapsed) {
                    const bool slotAcross =
                        target->axis == ig::Axis::Row
                            ? aimed.getY() < plan->slot.getBottom() && plan->slot.getY() < aimed.getBottom()
                            : aimed.getX() < plan->slot.getRight() && plan->slot.getX() < aimed.getRight();
                    EXPECT_TRUE(slotAcross) << "aimed at " << aimed.toString() << " in '" << container
                                            << "', the gap in front of open macro " << target->anchorKey
                                            << " lands the card at " << plan->slot.toString();
                }
            }
    }
    EXPECT_GT(gaps, 0) << "the walk found no gap at all";
}

} // namespace

TEST_F(LayoutUnitFootprint, EveryInsertGapOfASavedProjectLandsTheCardWhereItIsAimed) {
    for (const char* project : {"fro1153.agsproj", "macros.agsproj", "fro830-test.agsproj"}) {
        MainComponent mc(std::make_unique<MockProviderCFT>());
        open(mc, project);
        auto& editor = mc.getGraphEditor();
        for (auto* comp : editor.getModuleComponents())
            if (comp != nullptr && comp->isVisible())
                expectEveryGapLandsWhereAimed(editor, project, {comp->getWidth(), comp->getHeight()});
        copy_.deleteRecursively();
    }
}

TEST_F(LayoutUnitFootprint, EveryUnitAndBlockerOfASavedProjectIsDrawn) {
    for (const char* project : {"fro1153.agsproj", "macros.agsproj", "fro830-test.agsproj"}) {
        MainComponent mc(std::make_unique<MockProviderCFT>());
        open(mc, project);
        expectNoGhosts(mc.getGraphEditor(), project);
        copy_.deleteRecursively();
    }
}
