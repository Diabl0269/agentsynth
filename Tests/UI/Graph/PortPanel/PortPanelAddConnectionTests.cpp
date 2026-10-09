// PortPanelAddConnectionTests.cpp: the port connections panel's "Add connection": the search page that replaces the
// list (and Esc back), the compatible targets it lists, the shared search on them, picking one (a jack, a knob, a new
// module; one undo step each), the dashed preview cable, Pick on canvas, and the page swap's motion.
// docs/layout/cables.md#port-connections-panel.

#include "PortPanelTestFixture.h"

#include "AudioEngine/AudioEngine.h"
#include "Modules/ADSRModule.h"
#include "Modules/ExternalMidiModule.h"
#include "Modules/FilterModule.h"
#include "Modules/LFOModule.h"
#include "Modules/MathModule.h"
#include "UI/Graph/ModDot/KnobModSources.h"
#include "UI/Graph/PortPanel/PortConnector.h"
#include "UI/Graph/PortPanel/PortTargetList.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/Layout/SearchMatch.h"
#include <algorithm>
#include <set>

namespace {

using synth::ui::PortRef;
using synth::ui::PortTarget;
using NodeID = juce::AudioProcessorGraph::NodeID;

struct ReducedMotionGuard {
    explicit ReducedMotionGuard(bool reduced) { synth::ui::setReducedMotionForTest(reduced); }
    ~ReducedMotionGuard() { synth::ui::setReducedMotionForTest(std::nullopt); }
};

// The oscillator, one loose VCA and the modules a few of the tests need, on a real editor.
struct AddFixture : PortFixture {
    NodeID math, lfo, filter, extMidi, adsr;

    AddFixture()
        : PortFixture(0, 1) {
        auto& graph = engine.getGraph();
        math = place(graph.addNode(std::make_unique<MathModule>()), 900, 40);
        lfo = place(graph.addNode(std::make_unique<LFOModule>()), 900, 360);
        filter = place(graph.addNode(std::make_unique<FilterModule>()), 900, 680);
        extMidi = place(graph.addNode(std::make_unique<ExternalMidiModule>()), 40, 700);
        adsr = place(graph.addNode(std::make_unique<ADSRModule>()), 500, 700);
        engine.updateModuleNames();
        refresh();
        sizeModuleComponents(*editor);
        refresh();
    }

    static NodeID place(juce::AudioProcessorGraph::Node::Ptr node, int x, int y) {
        node->properties.set("x", x);
        node->properties.set("y", y);
        return node->nodeID;
    }
    PortConnectionsPanel* openFor(NodeID node, int jack, bool isInput, bool isMidi = false) {
        auto* c = card(node);
        const auto at = isMidi ? c->getMidiPortCenter(!isInput) : c->getPortCenter(jack, isInput);
        click(*c, at);
        if (controller().hasPendingOpenForTest()) // one cable and double-click-to-disconnect: it waits out the interval
            controller().firePendingOpenForTest();
        return panel();
    }
    PortConnectionsPanel* openAdd(NodeID node, int jack, bool isInput, bool isMidi = false) {
        auto* p = openFor(node, jack, isInput, isMidi);
        if (p != nullptr)
            clickNow(p->addConnectionButton());
        return p;
    }
    int audioCables(NodeID a, NodeID b) { return countAudioConnectionsBetween(engine.getGraph(), a, b); }
    bool midiCable(NodeID a, NodeID b) {
        for (const auto& c : engine.getGraph().getConnections())
            if (c.source.nodeID == a && c.destination.nodeID == b &&
                c.source.channelIndex == juce::AudioProcessorGraph::midiChannelIndex)
                return true;
        return false;
    }
};

// The row whose painted label is `label`.
synth::ui::ModDotChoiceRow* rowLabelled(PortConnectionsPanel& panel, const juce::String& label) {
    for (int i = 0; auto* row = panel.searchPage().visibleRow(i); ++i)
        if (row->item().label() == label)
            return row;
    return nullptr;
}

} // namespace

TEST_F(GraphEditorTest, AddConnectionOpensTheSearchPageAndEscapeReturnsToTheList) {
    AddFixture f;
    auto* panel = f.openFor(f.oscId, 0, false);
    ASSERT_NE(panel, nullptr);
    const int listHeight = panel->getHeight();
    EXPECT_FALSE(panel->isSearchOpen());

    clickNow(panel->addConnectionButton());

    EXPECT_TRUE(panel->isSearchOpen());
    EXPECT_FLOAT_EQ(panel->searchAmount(), 1.0f);
    EXPECT_TRUE(panel->searchPage().isVisible());
    EXPECT_FLOAT_EQ(panel->searchPage().getAlpha(), 1.0f);
    EXPECT_EQ(panel->getHeight(), panel->searchPage().getHeight()) << "the panel is as tall as the search page";
    EXPECT_NE(panel->getHeight(), listHeight);
    EXPECT_TRUE(panel->splitButton().isLeftLit()) << "the half is lit while its page is open";

    EXPECT_TRUE(panel->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));

    ASSERT_EQ(f.panel(), panel) << "Esc on the search page goes back, it does not close the panel";
    EXPECT_FALSE(panel->isSearchOpen());
    EXPECT_FLOAT_EQ(panel->searchAmount(), 0.0f);
    EXPECT_EQ(panel->getHeight(), listHeight);
    EXPECT_FALSE(panel->searchPage().isVisible());

    // The Back link does the same.
    clickNow(panel->addConnectionButton());
    ASSERT_TRUE(panel->isSearchOpen());
    clickNow(panel->searchPage().backButton());
    EXPECT_FALSE(panel->isSearchOpen());
}

TEST_F(GraphEditorTest, EscapeClearsTheQueryBeforeGoingBack) {
    AddFixture f;
    auto* panel = f.openAdd(f.oscId, 0, false);
    ASSERT_NE(panel, nullptr);
    panel->searchPage().setQuery("math");
    ASSERT_FALSE(panel->searchPage().visibleRowLabels().empty());

    panel->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
    EXPECT_TRUE(panel->isSearchOpen()) << "the first Esc only clears the search";
    EXPECT_TRUE(panel->searchPage().searchEditor().getText().isEmpty());

    panel->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
    EXPECT_FALSE(panel->isSearchOpen());
    EXPECT_EQ(f.panel(), panel);
}

TEST_F(GraphEditorTest, AModuleWithOneTargetIsOneRowAndAModuleWithSeveralIsAGroup) {
    AddFixture f;
    // MIDI: each module that takes MIDI has exactly one MIDI jack, so each is a single row "module . port".
    auto* panel = f.openAdd(f.extMidi, juce::AudioProcessorGraph::midiChannelIndex, false, true);
    ASSERT_NE(panel, nullptr);
    auto& page = panel->searchPage();
    const auto midiIn = f.module(f.lfo)->getInputPortLabel(0); // unused: the label below is the jack's own name
    (void)midiIn;
    EXPECT_TRUE(page.visibleGroupNames().empty()) << "single targets have no group header";
    EXPECT_EQ(page.visibleRowLabels().size(), 3u) << "the oscillator, the LFO and the envelope take MIDI";
    for (const auto id : {f.lfo, f.adsr, f.oscId})
        EXPECT_NE(rowLabelled(*panel, f.titleOf(id) + f.dot() + "Midi input"), nullptr) << f.titleOf(id).toStdString();

    // Audio: Math has the jacks A and B, so it is a group with its ports under the header.
    clickNow(panel->searchPage().backButton());
    f.controller().close();
    panel = f.openAdd(f.oscId, 0, false);
    ASSERT_NE(panel, nullptr);
    auto& audio = panel->searchPage();
    const auto groups = audio.visibleGroupNames();
    EXPECT_NE(std::find(groups.begin(), groups.end(), f.titleOf(f.math)), groups.end());
    EXPECT_NE(audio.header(f.titleOf(f.math)), nullptr);
    EXPECT_TRUE(audio.isGroupExpanded(f.titleOf(f.math)));
    const auto labels = audio.visibleRowLabels();
    EXPECT_NE(std::find(labels.begin(), labels.end(), "A"), labels.end());
    EXPECT_NE(std::find(labels.begin(), labels.end(), "B"), labels.end());

    // A group folds from its header.
    audio.header(f.titleOf(f.math))->triggerClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(30);
    EXPECT_FALSE(audio.isGroupExpanded(f.titleOf(f.math)));
}

TEST_F(GraphEditorTest, TheListHoldsOnlyCompatibleTargetsAndNeverTheSameModule) {
    AddFixture f;
    // From a MIDI output: only MIDI inputs, of other modules.
    const auto midi = synth::ui::listPortTargets(
        *f.editor, PortRef{f.extMidi, juce::AudioProcessorGraph::midiChannelIndex, false, true});
    ASSERT_FALSE(midi.empty());
    for (const auto& m : midi)
        for (const auto& t : m.targets) {
            EXPECT_TRUE(t.jack.isMidi && t.jack.isInput) << t.label().toStdString();
            EXPECT_NE(t.node, f.extMidi);
        }
    for (const auto id : {f.math, f.filter, f.vcaIds[0]})
        for (const auto& m : midi)
            EXPECT_NE(m.node, id) << "no MIDI jack: " << f.titleOf(id).toStdString();

    // From an audio output: audio inputs, never MIDI, never the oscillator itself; modules with nothing left out.
    const auto audio = synth::ui::listPortTargets(*f.editor, PortRef{f.oscId, 0, false, false});
    for (const auto& m : audio) {
        EXPECT_NE(m.node, f.oscId);
        EXPECT_NE(m.node, f.extMidi) << "it has no audio input";
        for (const auto& t : m.targets) {
            EXPECT_FALSE(t.jack.isMidi) << t.label().toStdString();
            EXPECT_TRUE(t.jack.isInput);
        }
    }
    // An input jack lists output jacks.
    const auto outputs = synth::ui::listPortTargets(*f.editor, PortRef{f.vcaIds[0], 0, true, false});
    ASSERT_FALSE(outputs.empty());
    for (const auto& m : outputs)
        for (const auto& t : m.targets)
            EXPECT_FALSE(t.jack.isInput) << t.label().toStdString();
}

TEST_F(GraphEditorTest, ModulesAreListedNearestFirst) {
    AddFixture f;
    const auto modules = synth::ui::listPortTargets(*f.editor, PortRef{f.oscId, 0, false, false});
    ASSERT_GE(modules.size(), 2u);
    auto distance = [&f](NodeID id) {
        return f.card(id)->getBounds().getCentre().getDistanceFrom(f.oscCard()->getBounds().getCentre());
    };
    for (size_t i = 1; i < modules.size(); ++i)
        EXPECT_LE(distance(modules[i - 1].node), distance(modules[i].node)) << "module " << i;
}

TEST_F(GraphEditorTest, TypingFiltersWithTheSharedSearchAndHighlightsTheMatchedLetters) {
    AddFixture f;
    auto* panel = f.openAdd(f.extMidi, juce::AudioProcessorGraph::midiChannelIndex, false, true);
    ASSERT_NE(panel, nullptr);
    auto& page = panel->searchPage();

    page.setQuery("lfo");

    const auto labels = page.visibleRowLabels();
    ASSERT_EQ(labels.size(), 2u) << "the LFO's jack and the \"New LFO\" row";
    EXPECT_EQ(labels[0], f.titleOf(f.lfo) + f.dot() + "Midi input");
    EXPECT_EQ(labels[1], "New LFO") << "New rows come after the existing targets";
    auto* row = page.visibleRow(0);
    ASSERT_NE(row, nullptr);
    EXPECT_EQ(row->query(), "lfo");
    const auto spans = synth::ui::searchHighlightSpans(row->item().label(), "lfo");
    ASSERT_EQ(spans.size(), 1u);
    EXPECT_EQ(spans[0].start, 0);
    EXPECT_EQ(spans[0].length, 3);

    // Words match in any order, ignoring case.
    page.setQuery("INPUT lfo");
    ASSERT_EQ(page.visibleRowLabels().size(), 1u) << "both words, any order, ignoring case";
    EXPECT_FALSE(page.visibleRow(0)->isNew());

    page.setQuery("zzzz");
    EXPECT_TRUE(page.visibleRowLabels().empty());
    EXPECT_EQ(page.noMatchText(), "Nothing matches");

    // The module name is searched on a grouped row even though only the port is painted.
    f.controller().close();
    panel = f.openAdd(f.oscId, 0, false);
    panel->searchPage().setQuery(f.titleOf(f.math));
    EXPECT_EQ(panel->searchPage().visibleRowLabels(), (std::vector<juce::String>{"A", "B", "New Math"}));
}

TEST_F(GraphEditorTest, AJackAlreadyConnectedIsGreyedAndCannotBePicked) {
    AddFixture f;
    f.editor->connectPorts(f.oscId, 0, f.vcaIds[0], 0, false, false);
    f.refresh();
    auto* panel = f.openAdd(f.oscId, 0, false);
    ASSERT_NE(panel, nullptr);
    const auto vcaFirstJack = f.module(f.vcaIds[0])->getInputPortLabel(0);

    auto* row = rowLabelled(*panel, vcaFirstJack);
    ASSERT_NE(row, nullptr);
    EXPECT_TRUE(row->isAdded()) << "greyed";
    EXPECT_EQ(row->usageText(), "Connected");
    EXPECT_NE(row->getTitle().indexOf("connected"), -1) << "read out as connected";
    const int before = f.audioCables(f.oscId, f.vcaIds[0]);
    row->pick();
    EXPECT_EQ(f.audioCables(f.oscId, f.vcaIds[0]), before) << "nothing happens";
    EXPECT_TRUE(panel->isSearchOpen());

    // The other jack of the same module is free.
    const auto vcaSecondJack = f.module(f.vcaIds[0])->getInputPortLabel(1);
    ASSERT_NE(rowLabelled(*panel, vcaSecondJack), nullptr);
    EXPECT_FALSE(rowLabelled(*panel, vcaSecondJack)->isAdded());
}

TEST_F(GraphEditorTest, ReturnWithNothingFocusedConnectsTheBestMatchAsOneUndoStep) {
    AddFixture f;
    auto* panel = f.openAdd(f.oscId, 0, false);
    ASSERT_NE(panel, nullptr);
    ASSERT_EQ(f.audioCables(f.oscId, f.math), 0);
    panel->searchPage().setQuery(f.titleOf(f.math));

    EXPECT_TRUE(panel->searchPage().searchEditor().keyPressed(juce::KeyPress(juce::KeyPress::returnKey)));

    EXPECT_GE(f.audioCables(f.oscId, f.math), 1) << "the best match was connected";
    EXPECT_FALSE(panel->isSearchOpen()) << "and the panel is back on its list";
    EXPECT_EQ(panel->rowCount(), 1) << "the list shows the new connection";
    EXPECT_EQ(f.editor->getCableRetractForTest().numGrowing(), 1u) << "its cable grows out of the jack";

    ASSERT_TRUE(f.undo.undo());
    EXPECT_EQ(f.audioCables(f.oscId, f.math), 0) << "one Cmd+Z takes it away";
}

TEST_F(GraphEditorTest, ABestMatchPrefersAnExistingTargetToANewModule) {
    AddFixture f;
    auto* panel = f.openAdd(f.oscId, 0, false);
    ASSERT_NE(panel, nullptr);
    const int nodes = f.engine.getGraph().getNumNodes();
    panel->searchPage().setQuery("filter");

    panel->searchPage().searchEditor().keyPressed(juce::KeyPress(juce::KeyPress::returnKey));

    EXPECT_GE(f.audioCables(f.oscId, f.filter), 1);
    EXPECT_EQ(f.engine.getGraph().getNumNodes(), nodes) << "no new module was made";
}

TEST_F(GraphEditorTest, AKnobOfAnotherModuleIsOfferedToAModulationOutputAndPickingItAddsAModulation) {
    AddFixture f;
    auto* panel = f.openAdd(f.lfo, 0, false);
    ASSERT_NE(panel, nullptr);
    auto& page = panel->searchPage();
    const auto offered = synth::ui::listPortTargets(*f.editor, PortRef{f.lfo, 0, false, false});
    const auto* gain = synth::ui::findKnobTarget(offered, f.vcaIds[0], 1); // the VCA's gain knob
    ASSERT_NE(gain, nullptr);
    ASSERT_TRUE(gain->portName.isNotEmpty());
    page.setQuery(gain->portName);
    synth::ui::ModDotChoiceRow* row = nullptr;
    for (int i = 0; auto* candidate = page.visibleRow(i); ++i)
        if (const auto* t = page.targetOf(*candidate); t != nullptr && t->node == f.vcaIds[0] && t->knobChannel == 1)
            row = candidate;
    ASSERT_NE(row, nullptr) << "a knob row, under its module's header";
    EXPECT_EQ(row->item().label(), gain->portName);
    EXPECT_NE(page.header(f.titleOf(f.vcaIds[0])), nullptr);
    ASSERT_NE(page.targetOf(*row), nullptr);
    EXPECT_EQ(page.targetOf(*row)->kind, PortTarget::Kind::Knob);
    ASSERT_TRUE(f.engine.getModulationRoutings().empty());

    row->pick();

    const auto routings = f.engine.getModulationRoutings();
    ASSERT_EQ(routings.size(), 1u);
    EXPECT_EQ(routings[0].sourceNodeID, f.lfo);
    EXPECT_EQ(routings[0].destNodeID, f.vcaIds[0]);
    EXPECT_EQ(routings[0].destChannelIndex, 1);
    EXPECT_NEAR(synth::ui::attenuverterAmount(f.engine.getGraph(), routings[0].attenuverterNodeID), 0.25f, 0.01f)
        << "at the mod dot's new-source depth";
    EXPECT_FALSE(panel->isSearchOpen());
    EXPECT_EQ(panel->rowCount(), 1);

    ASSERT_TRUE(f.undo.undo());
    EXPECT_TRUE(f.engine.getModulationRoutings().empty()) << "one undo step";

    // Wired again, the knob shows as Connected.
    synth::ui::PortConnector::connectToKnob(*f.editor, PortRef{f.lfo, 0, false, false}, f.vcaIds[0], 1);
    const auto modules = synth::ui::listPortTargets(*f.editor, PortRef{f.lfo, 0, false, false});
    const auto* knob = synth::ui::findKnobTarget(modules, f.vcaIds[0], 1);
    ASSERT_NE(knob, nullptr);
    EXPECT_TRUE(knob->connected);
    // An audio output is not a modulation output: it is offered jacks only.
    const auto audio = synth::ui::listPortTargets(*f.editor, PortRef{f.oscId, 0, false, false});
    EXPECT_EQ(synth::ui::findKnobTarget(audio, f.vcaIds[0], 1), nullptr);
}

TEST_F(GraphEditorTest, ANewModuleRowCreatesTheModuleAndConnectsItInOneUndoStep) {
    AddFixture f;
    auto* panel = f.openAdd(f.oscId, 0, false);
    ASSERT_NE(panel, nullptr);
    auto& page = panel->searchPage();
    page.setQuery("delay");
    synth::ui::ModDotChoiceRow* newRow = nullptr;
    for (int i = 0; auto* row = page.visibleRow(i); ++i)
        if (row->isNew())
            newRow = row;
    ASSERT_NE(newRow, nullptr) << "a typed query offers New rows";
    EXPECT_TRUE(newRow->item().label().startsWith("New "));
    EXPECT_NE(newRow->getTitle().indexOf("new module"), -1);
    const int nodes = f.engine.getGraph().getNumNodes();
    const auto cablesBefore = f.editor->buildVisibleCables().size();

    newRow->pick();

    EXPECT_EQ(f.engine.getGraph().getNumNodes(), nodes + 1);
    EXPECT_GT(f.editor->buildVisibleCables().size(), cablesBefore) << "a stereo cable per leg";
    NodeID created;
    for (auto* n : f.engine.getGraph().getNodes())
        if (n->nodeID.uid != f.oscId.uid && f.audioCables(f.oscId, n->nodeID) > 0)
            created = n->nodeID;
    ASSERT_NE(created.uid, 0u) << "the oscillator's output is cabled to the new module";
    EXPECT_GT(f.card(created)->getX(), f.oscCard()->getRight() - 1) << "beside the card, to its right";
    EXPECT_GE(panel->rowCount(), 1) << "the list shows the new connection";

    ASSERT_TRUE(f.undo.undo());
    EXPECT_EQ(f.engine.getGraph().getNumNodes(), nodes) << "the module and its cable go in one undo step";
    EXPECT_EQ(f.totalOscCables(), 0);
}

TEST_F(GraphEditorTest, NewModuleRowsAreOfferedOnlyForATypedQueryAndOnlyWithACompatibleJack) {
    AddFixture f;
    auto* panel = f.openAdd(f.extMidi, juce::AudioProcessorGraph::midiChannelIndex, false, true);
    ASSERT_NE(panel, nullptr);
    auto& page = panel->searchPage();
    for (int i = 0; auto* row = page.visibleRow(i); ++i)
        EXPECT_FALSE(row->isNew()) << "none before anything is typed";
    page.setQuery("new");
    bool anyNew = false;
    for (int i = 0; auto* row = page.visibleRow(i); ++i) {
        if (!row->isNew())
            continue;
        anyNew = true;
        EXPECT_GE(synth::ui::PortConnector::firstCompatibleJack(row->newType(), PortRef{f.extMidi, 0, false, true}), 0)
            << row->newType().toStdString() << " takes MIDI";
    }
    EXPECT_TRUE(anyNew);
    // Singleton IO modules and a class' second key are not offered.
    const auto all = synth::ui::listNewModuleTargets(PortRef{f.oscId, 0, false, false});
    std::set<juce::String> types;
    for (const auto& t : all) {
        EXPECT_TRUE(types.insert(t.newType).second) << "once: " << t.newType.toStdString();
        EXPECT_FALSE(GraphEditor::isSingletonIOModule(t.newType)) << t.newType.toStdString();
    }
}

TEST_F(GraphEditorTest, HoveringOrFocusingARowPreviewsADashedCableAndLeavingClearsIt) {
    AddFixture f;
    auto* panel = f.openAdd(f.oscId, 0, false);
    ASSERT_NE(panel, nullptr);
    auto* row = rowLabelled(*panel, f.module(f.vcaIds[0])->getInputPortLabel(0));
    ASSERT_NE(row, nullptr);
    EXPECT_FALSE(f.controller().hasPreview());

    row->mouseEnter(portEvent(*row, {5, 5}, {5, 5}));

    ASSERT_TRUE(f.controller().hasPreview());
    const auto& preview = *f.controller().preview();
    const auto from = f.oscCard()->getBounds().getPosition() + f.oscOut();
    const auto to = f.card(f.vcaIds[0])->getBounds().getPosition() + f.card(f.vcaIds[0])->getPortCenter(0, true);
    EXPECT_NEAR(preview.p1.x, (float)from.x, 0.6f) << "from this jack";
    EXPECT_NEAR(preview.p1.y, (float)from.y, 0.6f);
    EXPECT_NEAR(preview.p2.x, (float)to.x, 0.6f) << "to the target";
    EXPECT_NEAR(preview.p2.y, (float)to.y, 0.6f);

    row->mouseExit(portEvent(*row, {5, 5}, {5, 5}));
    EXPECT_FALSE(f.controller().hasPreview());

    row->mouseEnter(portEvent(*row, {5, 5}, {5, 5}));
    ASSERT_TRUE(f.controller().hasPreview());
    f.controller().close();
    EXPECT_FALSE(f.controller().hasPreview()) << "closing the panel clears it";

    // An input jack previews with the cable running from the output at the other end.
    panel = f.openAdd(f.vcaIds[0], 0, true);
    ASSERT_NE(panel, nullptr);
    auto* outRow = rowLabelled(*panel, f.titleOf(f.oscId) + f.dot() + f.module(f.oscId)->getOutputPortLabel(0));
    if (outRow == nullptr) // the oscillator's output is a group of two jacks: its rows are named by port
        outRow = rowLabelled(*panel, f.module(f.oscId)->getOutputPortLabel(0));
    ASSERT_NE(outRow, nullptr);
    outRow->mouseEnter(portEvent(*outRow, {5, 5}, {5, 5}));
    ASSERT_TRUE(f.controller().hasPreview());
    EXPECT_NEAR(f.controller().preview()->p2.x,
                (float)(f.card(f.vcaIds[0])->getX() + f.card(f.vcaIds[0])->getPortCenter(0, true).x), 0.6f)
        << "the input end is the destination";
}

TEST_F(GraphEditorTest, PickOnCanvasConnectsToTheJackThatIsClickedAndEscapeCancels) {
    AddFixture f;
    auto* panel = f.openFor(f.oscId, 0, false);
    ASSERT_NE(panel, nullptr);
    clickNow(panel->splitButton().rightHalf());
    ASSERT_TRUE(panel->isPicking());
    EXPECT_TRUE(panel->splitButton().isRightLit());
    auto* picker = panel->canvasPicker();
    ASSERT_NE(picker, nullptr);
    EXPECT_EQ(picker->getParentComponent(), f.editor.get());

    auto* vca = f.card(f.vcaIds[0]);
    const auto jack = vca->getPortCenter(0, true);
    const auto screen = vca->localPointToGlobal(jack);
    EXPECT_TRUE(picker->eligibleJackAt(screen).has_value()) << "a jack the output can connect to";
    const auto ownJack = f.oscCard()->localPointToGlobal(f.oscOut());
    EXPECT_FALSE(picker->eligibleJackAt(ownJack).has_value()) << "not its own";

    const auto at = picker->getLocalPoint(vca, jack);
    picker->mouseDown(makeModuleClickWithMods(*picker, at, plainLeftClick()));

    EXPECT_GE(f.audioCables(f.oscId, f.vcaIds[0]), 1);
    EXPECT_FALSE(panel->isPicking());
    EXPECT_EQ(f.panel(), panel) << "the panel stayed open";
    EXPECT_EQ(panel->rowCount(), 1);
    ASSERT_TRUE(f.undo.undo());
    EXPECT_EQ(f.audioCables(f.oscId, f.vcaIds[0]), 0);

    // Esc cancels.
    clickNow(panel->splitButton().rightHalf());
    ASSERT_TRUE(panel->isPicking());
    EXPECT_TRUE(panel->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
    EXPECT_FALSE(panel->isPicking());
    EXPECT_EQ(f.panel(), panel);
    EXPECT_EQ(f.totalOscCables(), 0);
}

TEST_F(GraphEditorTest, PickOnCanvasLeavesAnAlreadyConnectedJackAlone) {
    AddFixture f;
    f.editor->connectPorts(f.oscId, 0, f.vcaIds[0], 0, false, false);
    f.refresh();
    auto* panel = f.openFor(f.oscId, 0, false);
    ASSERT_NE(panel, nullptr);
    panel->startPick();
    auto* picker = panel->canvasPicker();
    ASSERT_NE(picker, nullptr);
    auto* vca = f.card(f.vcaIds[0]);
    EXPECT_FALSE(picker->eligibleJackAt(vca->localPointToGlobal(vca->getPortCenter(0, true))).has_value());
    EXPECT_TRUE(picker->eligibleJackAt(vca->localPointToGlobal(vca->getPortCenter(1, true))).has_value());
}

TEST_F(GraphEditorTest, TheConnectHelperMakesTheSameCableAsTheDragDrop) {
    // A drag from the oscillator's output to the VCA's input, and PortConnector for the same two jacks, leave the same
    // graph (the helper is the drop's own code).
    AddFixture f;
    auto* vca = f.card(f.vcaIds[0]);
    auto* source = f.oscCard();
    const auto screen = vca->localPointToGlobal(vca->getPortCenter(0, true));
    f.editor->beginConnectionDrag(source, 0, false, false, source->localPointToGlobal(f.oscOut()));
    f.editor->endConnectionDrag(screen);
    const int dragged = f.audioCables(f.oscId, f.vcaIds[0]);
    ASSERT_GE(dragged, 1);
    ASSERT_TRUE(f.undo.undo());
    ASSERT_EQ(f.audioCables(f.oscId, f.vcaIds[0]), 0);

    const auto result = synth::ui::PortConnector::connectToJack(*f.editor, PortRef{f.oscId, 0, false, false},
                                                                PortRef{f.vcaIds[0], 0, true, false});
    EXPECT_TRUE(result.connected);
    EXPECT_EQ(f.audioCables(f.oscId, f.vcaIds[0]), dragged);
    ASSERT_TRUE(f.undo.undo()) << "one undo step";
    EXPECT_EQ(f.audioCables(f.oscId, f.vcaIds[0]), 0);
    // Direction and kind are checked: two outputs, or audio to MIDI, make nothing.
    EXPECT_FALSE(synth::ui::PortConnector::connectToJack(*f.editor, PortRef{f.oscId, 0, false, false},
                                                         PortRef{f.lfo, 0, false, false})
                     .connected);
    EXPECT_FALSE(synth::ui::PortConnector::connectToJack(
                     *f.editor, PortRef{f.oscId, 0, false, false},
                     PortRef{f.adsr, juce::AudioProcessorGraph::midiChannelIndex, true, true})
                     .connected);
    // From the input side it is the same cable.
    EXPECT_TRUE(synth::ui::PortConnector::connectToJack(*f.editor, PortRef{f.vcaIds[0], 0, true, false},
                                                        PortRef{f.oscId, 0, false, false})
                    .connected);
    EXPECT_EQ(f.audioCables(f.oscId, f.vcaIds[0]), dragged);
    // MIDI.
    EXPECT_TRUE(synth::ui::PortConnector::connectToJack(
                    *f.editor, PortRef{f.extMidi, juce::AudioProcessorGraph::midiChannelIndex, false, true},
                    PortRef{f.adsr, juce::AudioProcessorGraph::midiChannelIndex, true, true})
                    .connected);
    EXPECT_TRUE(f.midiCable(f.extMidi, f.adsr));
}

TEST_F(GraphEditorTest, EveryNewControlOfTheAddConnectionPageHasANameAndATooltip) {
    AddFixture f;
    f.editor->connectPorts(f.oscId, 0, f.vcaIds[0], 0, false, false);
    f.refresh();
    auto* panel = f.openFor(f.oscId, 0, false);
    ASSERT_NE(panel, nullptr);
    for (auto* half : {&panel->splitButton().leftHalf(), &panel->splitButton().rightHalf()}) {
        EXPECT_TRUE(half->getTitle().isNotEmpty());
        EXPECT_TRUE(half->getTooltip().isNotEmpty());
        EXPECT_TRUE(half->getWantsKeyboardFocus());
        EXPECT_NE(half->getTitle(), half->getTitle().toUpperCase()) << "no all-caps text";
    }
    clickNow(panel->addConnectionButton());
    auto& page = panel->searchPage();
    EXPECT_TRUE(page.getTitle().isNotEmpty());
    EXPECT_TRUE(page.backButton().getTitle().isNotEmpty());
    EXPECT_TRUE(page.backButton().getTooltip().isNotEmpty());
    EXPECT_TRUE(page.searchEditor().getTitle().isNotEmpty());
    EXPECT_TRUE(dynamic_cast<juce::SettableTooltipClient&>(page.searchEditor()).getTooltip().isNotEmpty());
    int rows = 0;
    for (int i = 0; auto* row = page.visibleRow(i); ++i, ++rows) {
        EXPECT_TRUE(row->getTitle().isNotEmpty()) << "row " << i;
        EXPECT_TRUE(row->getTooltip().isNotEmpty()) << "row " << i;
        EXPECT_TRUE(row->getWantsKeyboardFocus()) << "row " << i;
    }
    EXPECT_GT(rows, 0);
    for (const auto& name : page.visibleGroupNames()) {
        auto* header = page.header(name);
        ASSERT_NE(header, nullptr);
        EXPECT_TRUE(header->getTitle().isNotEmpty());
        EXPECT_TRUE(header->getTooltip().isNotEmpty());
    }
    page.setQuery("new");
    for (int i = 0; auto* row = page.visibleRow(i); ++i) {
        EXPECT_TRUE(row->getTitle().isNotEmpty());
        EXPECT_TRUE(row->getTooltip().isNotEmpty());
        EXPECT_NE(row->item().label(), row->item().label().toUpperCase());
    }
}

TEST_F(GraphEditorTest, ThePagesCrossFadeWhileTheHeightSettlesAndUnderReducedMotionOnlyTheFadeIsSeen) {
    {
        ReducedMotionGuard motion(false);
        AddFixture f;
        auto* panel = f.openFor(f.oscId, 0, false);
        ASSERT_NE(panel, nullptr);
        panel->setForceAnimateForTest(true);
        const int listHeight = panel->getHeight();
        clickNow(panel->addConnectionButton());
        ASSERT_TRUE(panel->isSearchOpen());
        ASSERT_TRUE(panel->isPageAnimating());
        const int searchHeight = panel->settledHeight(true);
        ASSERT_NE(searchHeight, listHeight);
        EXPECT_FLOAT_EQ(panel->searchAmount(), 0.0f) << "the swap starts from the list";

        panel->applyPageTweenAt(0.5f);
        EXPECT_NEAR(panel->searchPage().getAlpha(), 0.5f, 0.01f) << "incoming 0 -> 1";
        EXPECT_TRUE(panel->searchPage().isVisible());
        EXPECT_GT(panel->getHeight(), std::min(listHeight, searchHeight));
        EXPECT_LT(panel->getHeight(), std::max(listHeight, searchHeight)) << "the height is settling to the new page's";

        panel->applyPageTweenAt(1.0f);
        EXPECT_EQ(panel->getHeight(), searchHeight);
        EXPECT_FLOAT_EQ(panel->searchPage().getAlpha(), 1.0f);
        EXPECT_EQ(panel->searchPage().getBounds().getHeight(), searchHeight);

        // And back, the same swap reversed.
        panel->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
        ASSERT_FALSE(panel->isSearchOpen());
        panel->applyPageTweenAt(0.5f);
        EXPECT_NEAR(panel->searchPage().getAlpha(), 0.5f, 0.01f) << "outgoing 1 -> 0";
        EXPECT_LT(panel->getHeight(), searchHeight);
        panel->applyPageTweenAt(1.0f);
        EXPECT_EQ(panel->getHeight(), listHeight);
        EXPECT_FALSE(panel->searchPage().isVisible());
    }
    {
        ReducedMotionGuard motion(true);
        AddFixture f;
        auto* panel = f.openFor(f.oscId, 0, false);
        panel->setForceAnimateForTest(true);
        clickNow(panel->addConnectionButton());
        ASSERT_TRUE(panel->isPageAnimating()) << "the fade still plays";
        EXPECT_EQ(panel->getHeight(), panel->settledHeight(true)) << "but the height is the new page's at once";
        panel->applyPageTweenAt(0.5f);
        EXPECT_NEAR(panel->searchPage().getAlpha(), 0.5f, 0.01f);
        EXPECT_EQ(panel->getHeight(), panel->settledHeight(true)) << "no height animation";
    }
}

TEST_F(GraphEditorTest, WithNoConnectionsAddConnectionIsTheMainAction) {
    AddFixture f;
    auto* panel = f.openFor(f.oscId, 0, false);
    ASSERT_NE(panel, nullptr);
    EXPECT_EQ(panel->rowCount(), 0);
    EXPECT_TRUE(panel->titleText().contains("No connections yet"));
    EXPECT_TRUE(panel->addConnectionButton().isVisible());
    EXPECT_FALSE(panel->isDisconnectAllShown());
    panel->focusEntry(); // nothing to remove, so the keys go to Add connection
    EXPECT_EQ(panel->getHeight(), panel->settledHeight(false));
}
