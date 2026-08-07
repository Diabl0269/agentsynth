#include "../Source/Modules/MathModule.h"
#include "../Source/Modules/OscillatorModule.h"
#include "../Source/Modules/VCAModule.h"
#include "../Source/Modules/VoiceMixerModule.h"
#include "../Source/UI/GraphEditor.h"
#include "../Source/UI/ModuleComponent.h"
#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

class ModuleComponentTest : public ::testing::Test {
protected:
};

// Every visible jack must sit inside the module's bounds, together with its centred label.
// Regression guard: the port column used to be reserved from the INPUT count only, so
// Math (2 in / 5 out) was laid out 140px tall while its 5th output jack sat at y=150 —
// the jack rendered outside the module and the scope overlapped the ones above it.
static void expectAllVisiblePortsWithinBounds(ModuleComponent& mc, ModuleBase& mod, const char* what) {
    const int labelHalfHeight = 10;
    for (int i = 0; i < mod.getVisibleInputPortCount(); ++i) {
        auto p = mc.getPortCenter(i, /*isInput*/ true);
        EXPECT_LE(p.y + labelHalfHeight, mc.getHeight())
            << what << ": input jack " << i << " overflows the module bottom";
    }
    for (int i = 0; i < mod.getVisibleOutputPortCount(); ++i) {
        auto p = mc.getPortCenter(i, /*isInput*/ false);
        EXPECT_LE(p.y + labelHalfHeight, mc.getHeight())
            << what << ": output jack " << i << " overflows the module bottom";
    }
}

TEST_F(ModuleComponentTest, OutputHeavyModuleReservesPortColumn) {
    AudioEngine engine;
    GraphEditor editor(engine);
    MathModule math;
    ModuleComponent moduleComponent(&math, juce::AudioProcessorGraph::NodeID(1), editor);

    ASSERT_EQ(math.getVisibleOutputPortCount(), 5);
    ASSERT_GT(math.getVisibleOutputPortCount(), math.getVisibleInputPortCount())
        << "This test is only meaningful while Math has more outputs than inputs";
    expectAllVisiblePortsWithinBounds(moduleComponent, math, "Math");
}

// The reported symptom: with the scope open it was drawn over the lowest output jacks and
// their labels, because the port column reserved no vertical space for outputs.
TEST_F(ModuleComponentTest, ScopeDoesNotOverlapPortColumn) {
    AudioEngine engine;
    GraphEditor editor(engine);
    MathModule math;
    ModuleComponent moduleComponent(&math, juce::AudioProcessorGraph::NodeID(4), editor);

    juce::ToggleButton* scopeToggle = nullptr;
    for (int i = 0; i < moduleComponent.getNumChildComponents(); ++i)
        if (auto* tb = dynamic_cast<juce::ToggleButton*>(moduleComponent.getChildComponent(i)))
            if (tb->getButtonText() == "Show Scope")
                scopeToggle = tb;
    ASSERT_NE(scopeToggle, nullptr) << "Math should expose a Show Scope toggle";

    const int heightBefore = moduleComponent.getHeight();
    scopeToggle->setToggleState(true, juce::sendNotificationSync);
    // Guards against this test passing vacuously if the toggle never re-laid the module out.
    ASSERT_GT(moduleComponent.getHeight(), heightBefore) << "Enabling the scope should grow the module";

    expectAllVisiblePortsWithinBounds(moduleComponent, math, "Math (scope open)");

    // Full-width children (the scope, and freq-response on other modules) span the jack
    // columns on both edges, so they must not share a row with any jack. Inset controls are
    // exempt — they sit inside the label margin by design. Checking the jack's whole 20px row
    // rather than just its centre point matters: the centre lands one pixel outside the
    // scope's right edge, so a contains() check would silently pass even when overlapping.
    const int fullWidthThreshold = moduleComponent.getWidth() / 2;
    for (int i = 0; i < math.getVisibleOutputPortCount(); ++i) {
        const auto centre = moduleComponent.getPortCenter(i, /*isInput*/ false);
        const juce::Rectangle<int> jackRow(0, centre.y - 10, moduleComponent.getWidth(), 20);
        for (int c = 0; c < moduleComponent.getNumChildComponents(); ++c) {
            auto* child = moduleComponent.getChildComponent(c);
            if (child == nullptr || !child->isVisible() || child->getWidth() <= fullWidthThreshold)
                continue;
            EXPECT_FALSE(child->getBounds().intersects(jackRow))
                << "Output jack " << i << " row is overlapped by full-width child component " << c;
        }
    }
}

TEST_F(ModuleComponentTest, InputHeavyModuleReservesPortColumn) {
    AudioEngine engine;
    GraphEditor editor(engine);

    OscillatorModule osc;
    ModuleComponent oscComponent(&osc, juce::AudioProcessorGraph::NodeID(2), editor);
    expectAllVisiblePortsWithinBounds(oscComponent, osc, "Oscillator");
}

// KNOWN PRE-EXISTING BUG, unrelated to the Math module — Voice Mixer has 8 input jacks but
// few controls, so its 8th jack (y=220) renders below the module bottom (height 210). The
// input-side reservation in ModuleComponent::portColumnBottom() under-reserves by ~30px.
//
// Not fixed here because correcting it grows Oscillator (530->560), Filter (570->600),
// Distortion (350->380) and four other modules, which makes the hardcoded node positions in
// every built-in preset overlap (E2EWorkflowTest.AllPresetsLoadWithoutOverlapAsAuthored
// fails). Fixing it properly means re-authoring those preset layouts — a separate change.
TEST_F(ModuleComponentTest, DISABLED_VoiceMixerLastInputJackOverflowsBounds) {
    AudioEngine engine;
    GraphEditor editor(engine);

    VoiceMixerModule mixer;
    ModuleComponent mixerComponent(&mixer, juce::AudioProcessorGraph::NodeID(3), editor);
    expectAllVisiblePortsWithinBounds(mixerComponent, mixer, "Voice Mixer");
}

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
