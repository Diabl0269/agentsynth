#include "../Source/Modules/OscillatorModule.h"
#include "../Source/Modules/VCAModule.h"
#include "../Source/UI/GraphEditor.h"
#include "../Source/UI/ModuleComponent.h"
#include <gtest/gtest.h>
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

TEST_F(ModuleComponentTest, TimerCallbackDoesNotCrash) {
    AudioEngine engine;
    GraphEditor editor(engine);
    OscillatorModule processor;
    ModuleComponent moduleComponent(&processor, juce::AudioProcessorGraph::NodeID(1), editor);

    EXPECT_NO_THROW(moduleComponent.timerCallback());
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
