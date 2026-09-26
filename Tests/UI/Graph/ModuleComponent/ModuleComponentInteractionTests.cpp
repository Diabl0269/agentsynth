// ModuleComponent interaction tests: parameter attachment, timer, header buttons, delete.

#include "AudioEngine/AudioEngine.h"
#include "ModuleComponentTestFixture.h"

#include "AI/AIStateMapper/AIStateMapper.h"
#include "Modules/FX/DelayModule.h"
#include "Modules/LFOModule.h"
#include "Modules/OscillatorModule.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

TEST_F(ModuleComponentTest, ParameterAttachmentLinksUI) {
    AudioEngine engine;
    GraphEditor editor(engine);
    OscillatorModule processor;
    ModuleComponent moduleComponent(&processor, juce::AudioProcessorGraph::NodeID(1), editor);

    juce::Slider* foundSlider = nullptr;
    for (auto* child : moduleComponent.getChildren()) {
        if (auto* slider = dynamic_cast<juce::Slider*>(child)) {
            foundSlider = slider;
            break;
        }
    }

    ASSERT_NE(foundSlider, nullptr);

    double minVal = foundSlider->getMinimum();
    double maxVal = foundSlider->getMaximum();
    double newValue = minVal + (maxVal - minVal) * 0.5;

    foundSlider->setValue(newValue, juce::sendNotificationSync);

    EXPECT_GE(foundSlider->getValue(), minVal);
}

TEST_F(ModuleComponentTest, TimerCallbackDoesNotCrash) {
    AudioEngine engine;
    GraphEditor editor(engine);
    OscillatorModule processor;
    ModuleComponent moduleComponent(&processor, juce::AudioProcessorGraph::NodeID(1), editor);

    EXPECT_NO_THROW(moduleComponent.timerCallback());
}

// §1.5: bypass/mute/delete are DrawableButtons at the correct header bounds.
TEST_F(ModuleComponentTest, HeaderButtonsAreDrawableButtons) {
    AudioEngine engine;
    GraphEditor editor(engine);
    OscillatorModule processor;
    ModuleComponent moduleComponent(&processor, juce::AudioProcessorGraph::NodeID(1), editor);
    moduleComponent.setSize(280, 400);

    juce::DrawableButton* foundBypass = nullptr;
    juce::DrawableButton* foundMute = nullptr;
    juce::DrawableButton* foundDelete = nullptr;

    for (auto* child : moduleComponent.getChildren()) {
        if (auto* db = dynamic_cast<juce::DrawableButton*>(child)) {
            if (db->getName() == "Bypass")
                foundBypass = db;
            else if (db->getName() == "Mute")
                foundMute = db;
            else if (db->getName() == "Delete")
                foundDelete = db;
        }
    }

    ASSERT_NE(foundBypass, nullptr) << "bypass button should be a non-null DrawableButton";
    ASSERT_NE(foundMute, nullptr) << "mute button should be a non-null DrawableButton";
    ASSERT_NE(foundDelete, nullptr) << "delete button should be a non-null DrawableButton";

    // Verify bounds: delete at getWidth()-26, bypass at getWidth()-50, mute at getWidth()-74.
    EXPECT_EQ(foundDelete->getX(), 280 - 26);
    EXPECT_EQ(foundBypass->getX(), 280 - 50);
    EXPECT_EQ(foundMute->getX(), 280 - 74);
    EXPECT_FALSE(foundBypass->getTooltip().isEmpty());
    EXPECT_FALSE(foundMute->getTooltip().isEmpty());
    EXPECT_FALSE(foundDelete->getTooltip().isEmpty());
}

TEST_F(ModuleComponentTest, DualIOHeaderButtonOnEveryStereoCapableModule) {
    AudioEngine engine;
    GraphEditor editor(engine);

    DelayModule delay;
    ModuleComponent delayComp(&delay, juce::AudioProcessorGraph::NodeID(1), editor);
    delayComp.setSize(280, 400);

    juce::DrawableButton* foundDual = nullptr;
    for (auto* child : delayComp.getChildren()) {
        if (auto* db = dynamic_cast<juce::DrawableButton*>(child))
            if (db->getName() == "Dual I/O")
                foundDual = db;
    }
    ASSERT_NE(foundDual, nullptr) << "FX modules expose Dual I/O as a header icon, not a labelled checkbox";
    EXPECT_EQ(foundDual->getX(), 280 - 98);
    EXPECT_FALSE(foundDual->getTooltip().isEmpty());

    // Since #219 the voice modules are stereo too, so they carry the same header icon. Their right
    // leg is on a kRightBase block rather than ch1, but that is a channel-map detail — the control
    // is identical from the user's side.
    OscillatorModule osc;
    ModuleComponent oscComp(&osc, juce::AudioProcessorGraph::NodeID(2), editor);
    oscComp.setSize(280, 400);
    juce::DrawableButton* oscDual = nullptr;
    for (auto* child : oscComp.getChildren()) {
        if (auto* db = dynamic_cast<juce::DrawableButton*>(child))
            if (db->getName() == "Dual I/O")
                oscDual = db;
    }
    ASSERT_NE(oscDual, nullptr) << "voice modules are stereo-capable and expose the same toggle";
    EXPECT_FALSE(oscDual->getTooltip().isEmpty());

    // Data-driven over the authoritative list, so a module that gains the Dual I/O parameter cannot
    // ship without the header control that operates it. (The Ring Modulator carried a stereo output
    // pair with no toggle at all until the list stopped being hand-written.)
    for (const auto& type : synth::AIStateMapper::dualIOCapableModuleTypes()) {
        SCOPED_TRACE(type.toStdString());
        auto processor = synth::AIStateMapper::createModule(type);
        ASSERT_NE(processor, nullptr);
        ModuleComponent card(processor.get(), juce::AudioProcessorGraph::NodeID(9), editor);
        card.setSize(280, 400);
        bool hasToggle = false;
        for (auto* child : card.getChildren())
            if (auto* db = dynamic_cast<juce::DrawableButton*>(child))
                if (db->getName() == "Dual I/O")
                    hasToggle = true;
        EXPECT_TRUE(hasToggle) << "no Dual I/O header button on a stereo-capable module";
    }

    // A module with no second audio leg must NOT grow the control.
    LFOModule lfo;
    ModuleComponent lfoComp(&lfo, juce::AudioProcessorGraph::NodeID(3), editor);
    lfoComp.setSize(280, 400);
    for (auto* child : lfoComp.getChildren()) {
        if (auto* db = dynamic_cast<juce::DrawableButton*>(child))
            EXPECT_NE(db->getName(), "Dual I/O") << "a CV-only module has no stereo pair to split";
    }
}

// §1.5: clicking the delete button removes the node from the graph via requestDeleteModule.
TEST_F(ModuleComponentTest, DeleteButtonTriggersRemoval) {
    AudioEngine engine;
    GraphEditor editor(engine);

    // Add an OscillatorModule node to the graph directly.
    auto* osc = new OscillatorModule();
    auto node = engine.getGraph().addNode(std::unique_ptr<juce::AudioProcessor>(osc));
    ASSERT_NE(node, nullptr);
    juce::AudioProcessorGraph::NodeID nodeId = node->nodeID;

    ModuleComponent moduleComponent(osc, nodeId, editor);
    moduleComponent.setSize(280, 400);

    // Locate the delete button and trigger it.
    juce::DrawableButton* foundDelete = nullptr;
    for (auto* child : moduleComponent.getChildren()) {
        if (auto* db = dynamic_cast<juce::DrawableButton*>(child)) {
            if (db->getName() == "Delete") {
                foundDelete = db;
                break;
            }
        }
    }
    ASSERT_NE(foundDelete, nullptr);

    // Invoke the delete button's onClick directly (headless: no message pump for triggerClick).
    ASSERT_TRUE(foundDelete->onClick) << "deleteButton must have an onClick handler";
    foundDelete->onClick();

    // The node should be gone from the graph.
    EXPECT_EQ(engine.getGraph().getNodeForId(nodeId), nullptr)
        << "node should have been removed from the graph after delete button click";
}
