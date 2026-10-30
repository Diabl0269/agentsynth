// A port's connection list shows one row per connection, not one per drawn cable: a collapsed stereo pair keeps two
// raw edges (left to left, right to right, or one mono source wired to both legs) that land on one visible jack with
// the same far end. Removing the row removes every edge behind it, as one undo step.

#include "../../../TestWait.h"
#include "../GraphEditor/GraphEditorTestHelpers.h"

#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "Modules/FX/DelayModule.h"
#include "Modules/FilterModule.h"
#include "Modules/OscillatorModule.h"
#include "UI/Graph/PortPanel/PortConnectionList.h"
#include "UI/Graph/PortPanel/PortConnectionRow.h"
#include "UI/Graph/PortPanel/PortConnectionsPanel.h"
#include "UI/Graph/PortPanel/PortPanelController.h"

namespace {

using NodeID = juce::AudioProcessorGraph::NodeID;

struct PairFixture {
    AudioEngine engine;
    AppUndoManager undo;
    std::unique_ptr<GraphEditor> editor;
    std::unique_ptr<juce::Component> held;
    NodeID src, delay;

    PairFixture(std::unique_ptr<juce::AudioProcessor> source, bool sourceSplit, bool delaySplit) {
        engine.initialise();
        engine.getGraph().clear();
        editor = std::make_unique<GraphEditor>(engine, &undo);
        undo.setGraphEditor(editor.get());
        auto s = engine.getGraph().addNode(std::move(source));
        auto d = engine.getGraph().addNode(std::make_unique<DelayModule>());
        src = s->nodeID;
        delay = d->nodeID;
        s->properties.set("x", 40);
        s->properties.set("y", 40);
        d->properties.set("x", 600);
        d->properties.set("y", 40);
        setDualIOParam(*s->getProcessor(), sourceSplit);
        setDualIOParam(*d->getProcessor(), delaySplit);
        editor->setSize(1200, 800);
        editor->updateComponents();
        sizeModuleComponents(*editor);
        editor->getPortPanel().panelLauncher = [this](std::unique_ptr<juce::Component> content, juce::Component&,
                                                      juce::Rectangle<int>) { held = std::move(content); };
    }

    ~PairFixture() {
        held.reset();
        editor.reset();
        engine.shutdown();
    }

    int rightLeg() {
        auto* mb = dynamic_cast<ModuleBase*>(engine.getGraph().getNodeForId(src)->getProcessor());
        return mb->rightAudioLegChannel();
    }
    void connectVisible(int srcJack, int dstJack) { editor->connectPorts(src, srcJack, delay, dstJack, false, false); }
    void wire(int srcCh, int dstCh) { engine.getGraph().addConnection({{src, srcCh}, {delay, dstCh}}); }
    int edges() { return countAudioConnectionsBetween(engine.getGraph(), src, delay); }

    synth::ui::PortRef delayInput(int jack) const { return {delay, jack, /*isInput=*/true, /*isMidi=*/false}; }
    size_t rows(int jack) { return synth::ui::listPortConnections(*editor, delayInput(jack)).size(); }

    // Opens the panel on the Delay's input jack and presses the first row's remove button.
    void removeFirstRow(int jack) {
        ModuleComponent* card = nullptr;
        for (auto* c : editor->getModuleComponents())
            if (c->getNodeId() == delay)
                card = c;
        ASSERT_NE(card, nullptr);
        editor->getPortPanel().open(*card, card->getBounds(), delayInput(jack));
        auto* panel = editor->getPortPanel().getPanel();
        ASSERT_NE(panel, nullptr);
        ASSERT_NE(panel->rowAt(0), nullptr);
        panel->rowAt(0)->removeButton().triggerClick();
        synth::test::flushMessageQueue();
    }
};

} // namespace

// Regression test for FRO1074: the same Filter listed twice on the Delay's input.
TEST_F(GraphEditorTest, ACollapsedStereoPairIntoACollapsedPairIsOneRow) {
    PairFixture f(std::make_unique<FilterModule>(), false, false);
    f.connectVisible(0, 0);
    EXPECT_EQ(f.rows(0), 1u);
    f.removeFirstRow(0);
    EXPECT_EQ(f.edges(), 0) << "the row removes every edge behind it";
    ASSERT_TRUE(f.undo.undo());
    EXPECT_EQ(f.edges(), 2) << "in one undo step";
}

TEST_F(GraphEditorTest, AMonoSourceWiredToBothLegsOfACollapsedPairIsOneRow) {
    PairFixture f(std::make_unique<FilterModule>(), false, false);
    f.wire(0, 0);
    f.wire(0, 1);
    EXPECT_EQ(f.rows(0), 1u);
    f.removeFirstRow(0);
    EXPECT_EQ(f.edges(), 0);
}

TEST_F(GraphEditorTest, AnOscillatorWiredToBothLegsOfACollapsedDelayIsOneRow) {
    PairFixture f(std::make_unique<OscillatorModule>(), false, false);
    f.wire(0, 0);
    f.wire(0, 1);
    EXPECT_EQ(f.rows(0), 1u);
    f.removeFirstRow(0);
    EXPECT_EQ(f.edges(), 0);
}

TEST_F(GraphEditorTest, ASplitPairIntoACollapsedPairIsOneRowPerFarJack) {
    PairFixture f(std::make_unique<FilterModule>(), true, false);
    f.wire(0, 0);
    f.wire(f.rightLeg(), 1);
    EXPECT_EQ(f.rows(0), 2u) << "Left and Right are two different far jacks";
    f.removeFirstRow(0);
    EXPECT_EQ(f.edges(), 1);
}

TEST_F(GraphEditorTest, ACollapsedPairIntoASplitPairIsOneRowOnEachJack) {
    PairFixture f(std::make_unique<FilterModule>(), false, true);
    f.connectVisible(0, 0);
    f.connectVisible(0, 1);
    EXPECT_EQ(f.rows(0), 1u);
    EXPECT_EQ(f.rows(1), 1u);
    f.removeFirstRow(1);
    EXPECT_EQ(f.rows(0), 1u) << "the other jack keeps its connection";
    EXPECT_EQ(f.rows(1), 0u);
}

TEST_F(GraphEditorTest, ASplitPairIntoASplitPairIsOneRowPerJack) {
    PairFixture f(std::make_unique<FilterModule>(), true, true);
    f.wire(0, 0);
    f.wire(f.rightLeg(), 1);
    EXPECT_EQ(f.rows(0), 1u);
    EXPECT_EQ(f.rows(1), 1u);
}
