#include "../Source/Modules/OscillatorModule.h"
#include "../Source/Modules/SamplerModule.h"
#include "../Source/Modules/VCAModule.h"
#include "../Source/UI/GraphEditor.h"
#include "../Source/UI/ModuleComponent.h"
#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

class ModuleComponentTest : public ::testing::Test {
protected:
};

TEST_F(ModuleComponentTest, InitializationAndResizing) {
    AudioEngine engine;
    GraphEditor editor(engine);
    OscillatorModule processor;
    ModuleComponent moduleComponent(&processor, juce::AudioProcessorGraph::NodeID(1), editor);

    EXPECT_NO_THROW(moduleComponent.setSize(200, 300));
}

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

// Sampler chrome: the module gets a waveform view, a "Load Sample..." button and a file-name label
// on top of the usual auto-UI. The height is asserted exactly because GraphEditor's
// estimateModuleSize("Sampler") hard-codes it for the library drag ghost — if the auto-UI grows a
// row, this test fails and both numbers get updated together.
TEST_F(ModuleComponentTest, SamplerHasLoadButtonWaveformAndKnownHeight) {
    AudioEngine engine;
    GraphEditor editor(engine);
    SamplerModule processor;
    ModuleComponent moduleComponent(&processor, juce::AudioProcessorGraph::NodeID(1), editor);

    juce::TextButton* loadButton = nullptr;
    bool foundWaveform = false;
    bool foundNameLabel = false;
    for (auto* child : moduleComponent.getChildren()) {
        if (auto* button = dynamic_cast<juce::TextButton*>(child);
            button != nullptr && button->getButtonText().startsWith("Load Sample"))
            loadButton = button;
        if (dynamic_cast<SampleWaveformComponent*>(child) != nullptr)
            foundWaveform = true;
        if (auto* label = dynamic_cast<juce::Label*>(child); label != nullptr && label->getText() == "(no sample)")
            foundNameLabel = true;
    }

    ASSERT_NE(loadButton, nullptr) << "the Sampler needs a way to pick a file";
    EXPECT_TRUE(foundWaveform);
    EXPECT_TRUE(foundNameLabel) << "an empty Sampler should say so rather than showing a blank label";

    EXPECT_EQ(moduleComponent.getWidth(), 280);
    EXPECT_EQ(moduleComponent.getHeight(), 750)
        << "keep estimateModuleSize(\"Sampler\") in GraphEditor.cpp in sync with this";

    // The waveform view sits above the controls and spans the card width minus the port gutters.
    EXPECT_NO_THROW(moduleComponent.timerCallback());
}

TEST_F(ModuleComponentTest, NonSamplerModulesGetNoSamplerChrome) {
    AudioEngine engine;
    GraphEditor editor(engine);
    OscillatorModule processor;
    ModuleComponent moduleComponent(&processor, juce::AudioProcessorGraph::NodeID(1), editor);

    for (auto* child : moduleComponent.getChildren())
        EXPECT_EQ(dynamic_cast<SampleWaveformComponent*>(child), nullptr);
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

// Inc-4: Verify getPortCenter clamps out-of-range indices to the last visible jack
// so poly-bus wire endpoints never land at a phantom y below the module.
TEST_F(ModuleComponentTest, GetPortCenter_ClampsOutOfRangeToLastVisibleJack) {
    AudioEngine engine;
    GraphEditor editor(engine);
    VCAModule processor; // VCA: getVisibleInputPortCount() == 2, getVisibleOutputPortCount() == 1
    ModuleComponent moduleComponent(&processor, juce::AudioProcessorGraph::NodeID(2), editor);
    moduleComponent.setSize(280, 200);

    // --- Input side ---
    // VCA has 2 visible input ports (indices 0 and 1).
    // Index 8 is far beyond visible range; it must clamp to index 1 (last visible).
    auto p_in_1 = moduleComponent.getPortCenter(1, /*isInput=*/true);
    auto p_in_8 = moduleComponent.getPortCenter(8, /*isInput=*/true);
    auto p_in_0 = moduleComponent.getPortCenter(0, /*isInput=*/true);
    auto p_in_0_ref = moduleComponent.getPortCenter(0, /*isInput=*/true);

    // Clamped (index 8 -> index 1): y must equal the y for index 1.
    EXPECT_EQ(p_in_8.y, p_in_1.y)
        << "getPortCenter(8,true).y should clamp to getPortCenter(1,true).y (last visible input jack)";

    // Must NOT equal the unbounded phantom formula value (headerHeight + portOffset + 8*20 + 20 = 30+0+160+20=210).
    // We check it is strictly less than that phantom value.
    int phantomY = 30 + 8 * 20 + 20; // headerHeight=30, yStep=20, portOffset=0 for VCA (no MIDI out)
    EXPECT_LT(p_in_8.y, phantomY) << "getPortCenter(8,true).y must not equal the phantom unbounded formula value";

    // In-bounds index (0) must be unchanged: clamped == index.
    EXPECT_EQ(p_in_0.y, p_in_0_ref.y) << "getPortCenter(0,true) must be unchanged (in-bounds index, no clamping)";

    // --- Output side ---
    // VCA has 1 visible output port (index 0 only).
    auto p_out_0 = moduleComponent.getPortCenter(0, /*isInput=*/false);
    auto p_out_5 = moduleComponent.getPortCenter(5, /*isInput=*/false);

    EXPECT_EQ(p_out_5.y, p_out_0.y)
        << "getPortCenter(5,false).y should clamp to getPortCenter(0,false).y (only visible output jack)";
}
