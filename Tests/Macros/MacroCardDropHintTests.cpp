// A cable hovering a folded macro's card shows where the drop would land
// (docs/macros/ports.md#a-cable-hovering-a-folded-card): the card outlines itself and rings the port that takes the
// cable, or marks the new-port slot. Driven through the real mouse path (ModuleComponent::mouseDown -> mouseDrag ->
// mouseUp with the press point held fixed, and Esc through GraphEditor::keyPressed), and every case checks that the
// hint at the last drag event is what the release then does.

#include "AudioEngine/AudioEngine.h"
#include "Modules/FilterModule.h"
#include "Modules/OscillatorModule.h"
#include "TestMode/TestModeStateSource.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Macros/MacroCardComponent/MacroCardComponent.h"
#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>

namespace macro_card_drop_hint_tests {

using NodeID = juce::AudioProcessorGraph::NodeID;
using Hint = MacroCardComponent::CableDropHint;

NodeID addAt(GraphEditor& editor, AudioEngine& engine, std::unique_ptr<juce::AudioProcessor> processor, int x, int y) {
    auto node = engine.getGraph().addNode(std::move(processor));
    node->properties.set("x", x);
    node->properties.set("y", y);
    node->properties.set("uuid", juce::Uuid().toDashedString());
    editor.updateComponents();
    return node->nodeID;
}

NodeID nodeForUuid(AudioEngine& engine, const juce::String& uuid) {
    for (auto* node : engine.getGraph().getNodes())
        if (node->properties["uuid"].toString() == uuid)
            return node->nodeID;
    return {};
}

bool connected(AudioEngine& engine, NodeID src, NodeID dst) {
    for (const auto& c : engine.getGraph().getConnections())
        if (c.source.nodeID == src && c.destination.nodeID == dst)
            return true;
    return false;
}

juce::MouseEvent mouseAt(juce::Component& comp, juce::Point<int> local, juce::Point<int> down, bool dragged) {
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), local.toFloat(),
                            juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                            &comp, &comp, juce::Time::getCurrentTime(), down.toFloat(), juce::Time::getCurrentTime(), 1,
                            dragged);
}

// A folded two-module macro and an Oscillator and a Filter outside it, to drag cables from.
struct HintCanvas {
    AudioEngine engine;
    GraphEditor editor{engine};
    juce::String macroId;
    NodeID extOsc, extFilter;
    ModuleComponent* source = nullptr;
    juce::Point<int> press;

    HintCanvas() {
        editor.setSize(1600, 1200);
        const auto a = addAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
        const auto b = addAt(editor, engine, std::make_unique<FilterModule>(), 500, 100);
        editor.setSelectedNodes({a, b});
        macroId = editor.getMacroController().groupSelectionIntoMacro();
        extOsc = addAt(editor, engine, std::make_unique<OscillatorModule>(), 900, 900);
        extFilter = addAt(editor, engine, std::make_unique<FilterModule>(), 900, 500);
    }

    MacroCardComponent& card() { return *editor.getMacroController().getMacroCardForTest(macroId); }
    const synth::Macro& macro() { return *editor.getMacros().find(macroId); }
    juce::String addPort(bool isInput, synth::MacroPortKind kind, const juce::String& name) {
        return editor.getMacroController().addMacroPort(macroId, isInput, kind, MacroPortShape::Mono, 1, name);
    }
    juce::Point<int> cardPoint(juce::Point<int> local) { return card().localPointToGlobal(local); }
    juce::Point<int> cardBody() { return cardPoint(card().getLocalBounds().getCentre().withY(60)); }
    juce::Point<int> portJack(const juce::String& uuid) {
        for (const auto& port : editor.getMacroController().macroCardPortLayout(macroId))
            if (port.nodeUuid == uuid)
                return cardPoint(port.jackPos);
        return {};
    }
    // Empty canvas, well clear of every card.
    juce::Point<int> nowhere() { return editor.localPointToGlobal(juce::Point<int>(1500, 50)); }

    ModuleComponent* card(NodeID id) {
        for (auto* c : editor.getModuleComponents())
            if (c->getNodeId() == id)
                return c;
        return nullptr;
    }
    void pressJack(NodeID id, bool isInput) {
        source = card(id);
        press = source->getPortCenter(0, isInput);
        source->mouseDown(mouseAt(*source, press, press, false));
    }
    void dragTo(juce::Point<int> screen) {
        source->mouseDrag(mouseAt(*source, source->getLocalPoint(nullptr, screen), press, true));
    }
    void releaseAt(juce::Point<int> screen) {
        source->mouseUp(mouseAt(*source, source->getLocalPoint(nullptr, screen), press, true));
    }
};

} // namespace macro_card_drop_hint_tests

using namespace macro_card_drop_hint_tests;

TEST(MacroCardDropHint, HoveringAFoldedCardShowsTheNewPortSlotAndLeavingItClearsTheHint) {
    HintCanvas c;
    ASSERT_TRUE(c.macro().collapsed);
    c.pressJack(c.extOsc, /*isInput=*/false);
    EXPECT_FALSE(c.card().getCableDropHint().has_value());

    c.dragTo(c.cardBody());
    ASSERT_TRUE(c.card().getCableDropHint().has_value()) << "a cable over a folded card shows where it would land";
    EXPECT_TRUE(c.card().getCableDropHint()->newPort);
    EXPECT_TRUE(c.card().getCableDropHint()->isInput) << "a cable from an output makes an input port";

    juce::DynamicObject state;
    static_cast<const synth::testmode::StateSource&>(c.card()).addTestModeState(state);
    EXPECT_EQ(state.getProperty("cableDropHint").toString(), "newPort");

    c.dragTo(c.nowhere());
    EXPECT_FALSE(c.card().getCableDropHint().has_value()) << "leaving the card clears the hint";
    c.releaseAt(c.nowhere());
    EXPECT_TRUE(c.macro().ports.empty());
}

TEST(MacroCardDropHint, EscapeCancelsTheDragAndClearsTheHint) {
    HintCanvas c;
    c.pressJack(c.extOsc, false);
    c.dragTo(c.cardBody());
    ASSERT_TRUE(c.card().getCableDropHint().has_value());

    EXPECT_TRUE(c.editor.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
    EXPECT_FALSE(c.card().getCableDropHint().has_value()) << "Esc drops the hint with the cable";
    c.releaseAt(c.cardBody()); // the button comes up after Esc: the drag is already over
    EXPECT_TRUE(c.macro().ports.empty()) << "nothing lands after Esc";
    EXPECT_FALSE(c.card().getCableDropHint().has_value());
}

TEST(MacroCardDropHint, TheNewPortHintIsWhatTheDropMakes) {
    HintCanvas c;
    c.pressJack(c.extOsc, false);
    c.dragTo(c.cardBody());
    const auto hint = c.card().getCableDropHint();
    ASSERT_TRUE(hint.has_value());
    ASSERT_TRUE(hint->newPort);

    c.releaseAt(c.cardBody());
    EXPECT_FALSE(c.card().getCableDropHint().has_value()) << "the drag's end clears the hint";
    ASSERT_EQ(c.macro().ports.size(), 1u) << "the hint promised a new port";
    EXPECT_EQ(c.macro().ports[0].isInput, hint->isInput);
    EXPECT_TRUE(connected(c.engine, c.extOsc, nodeForUuid(c.engine, c.macro().ports[0].nodeUuid)));
}

TEST(MacroCardDropHint, TheExistingPortHintNamesThePortTheDropWiresInto) {
    HintCanvas c;
    const auto port = c.addPort(true, synth::MacroPortKind::AudioCV, "Pitch In");
    ASSERT_FALSE(port.isEmpty());
    c.pressJack(c.extOsc, false);
    c.dragTo(c.portJack(port));
    const auto hint = c.card().getCableDropHint();
    ASSERT_TRUE(hint.has_value());
    EXPECT_FALSE(hint->newPort);
    EXPECT_EQ(hint->portUuid, port);

    juce::DynamicObject state;
    static_cast<const synth::testmode::StateSource&>(c.card()).addTestModeState(state);
    EXPECT_EQ(state.getProperty("cableDropHint").toString(), "port");
    EXPECT_EQ(state.getProperty("cableDropHintPort").toString(), "Pitch In");

    c.releaseAt(c.portJack(port));
    EXPECT_EQ(c.macro().ports.size(), 1u) << "the hint named an existing port, so no new one appears";
    EXPECT_TRUE(connected(c.engine, c.extOsc, nodeForUuid(c.engine, hint->portUuid)));
}

TEST(MacroCardDropHint, AJackOfTheWrongKindShowsNoHintAndTheDropIsRefused) {
    HintCanvas c;
    const auto midiPort = c.addPort(true, synth::MacroPortKind::Midi, "Notes");
    ASSERT_FALSE(midiPort.isEmpty());
    c.pressJack(c.extOsc, false); // an audio output over a MIDI input
    c.dragTo(c.cardBody());
    ASSERT_TRUE(c.card().getCableDropHint().has_value()) << "the card body would take the cable as a new port";
    c.dragTo(c.portJack(midiPort));
    EXPECT_FALSE(c.card().getCableDropHint().has_value()) << "no hint where the drop would be refused";

    c.releaseAt(c.portJack(midiPort));
    EXPECT_EQ(c.macro().ports.size(), 1u);
    EXPECT_FALSE(connected(c.engine, c.extOsc, nodeForUuid(c.engine, midiPort)));
}

TEST(MacroCardDropHint, AJackOfTheWrongDirectionShowsNoHintAndTheDropIsRefused) {
    HintCanvas c;
    const auto inPort = c.addPort(true, synth::MacroPortKind::AudioCV, "In");
    c.pressJack(c.extFilter, /*isInput=*/true); // an input looking for a source, over an input port
    c.dragTo(c.cardBody());
    ASSERT_TRUE(c.card().getCableDropHint().has_value()) << "the card body would take the cable as a new port";
    c.dragTo(c.portJack(inPort));
    EXPECT_FALSE(c.card().getCableDropHint().has_value());

    c.releaseAt(c.portJack(inPort));
    EXPECT_EQ(c.macro().ports.size(), 1u);
    EXPECT_FALSE(connected(c.engine, nodeForUuid(c.engine, inPort), c.extFilter));
}

TEST(MacroCardDropHint, TheSameCableDroppedAgainRingsThePortThatAlreadyCarriesIt) {
    HintCanvas c;
    c.pressJack(c.extOsc, false);
    c.dragTo(c.cardBody());
    c.releaseAt(c.cardBody());
    ASSERT_EQ(c.macro().ports.size(), 1u);
    const auto minted = c.macro().ports[0].nodeUuid;

    c.pressJack(c.extOsc, false);
    c.dragTo(c.cardBody());
    const auto hint = c.card().getCableDropHint();
    ASSERT_TRUE(hint.has_value());
    EXPECT_FALSE(hint->newPort) << "the drop makes nothing new, so the hint offers no new port";
    EXPECT_EQ(hint->portUuid, minted);

    c.releaseAt(c.cardBody());
    EXPECT_EQ(c.macro().ports.size(), 1u);
}

TEST(MacroCardDropHint, ACableFromAnInputOffersANewOutputPort) {
    HintCanvas c;
    c.pressJack(c.extFilter, /*isInput=*/true);
    c.dragTo(c.cardBody());
    const auto hint = c.card().getCableDropHint();
    ASSERT_TRUE(hint.has_value());
    EXPECT_TRUE(hint->newPort);
    EXPECT_FALSE(hint->isInput);

    c.releaseAt(c.cardBody());
    ASSERT_EQ(c.macro().ports.size(), 1u);
    EXPECT_FALSE(c.macro().ports[0].isInput);
}
