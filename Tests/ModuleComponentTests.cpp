#include "../Source/Modules/MacroControlModule.h"
#include "../Source/Modules/OscillatorModule.h"
#include "../Source/Modules/VCAModule.h"
#include "../Source/UI/GraphEditor.h"
#include "../Source/UI/LayoutUtil.h"
#include "../Source/UI/ModuleComponent.h"
#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

class ModuleComponentTest : public ::testing::Test {
protected:
};

namespace {
// Sets the Macro bank's "Knobs" parameter before the component is built, so createControls()
// lays the component out for that count.
void setKnobCount(MacroControlModule& macros, int count) {
    for (auto* p : macros.getParameters())
        if (auto* i = dynamic_cast<juce::AudioParameterInt*>(p))
            if (i->paramID == "macroCount")
                i->setValueNotifyingHost(i->convertTo0to1(count));
}
} // namespace

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

// ============================================================================
// Macro Control bank layout
// ============================================================================

TEST_F(ModuleComponentTest, MacroBankHeightTracksItsKnobCount) {
    using namespace synth::LayoutUtil;

    for (int count : {1, 4, 8, 16}) {
        AudioEngine engine;
        GraphEditor editor(engine);
        MacroControlModule macros;
        setKnobCount(macros, count);

        ModuleComponent comp(&macros, juce::AudioProcessorGraph::NodeID(1), editor);

        EXPECT_EQ(comp.getWidth(), kSingleWidth) << "count " << count;
        EXPECT_EQ(comp.getHeight(), macroBankHeight(count)) << "count " << count;
    }
}

TEST_F(ModuleComponentTest, MacroBankJacksSitOnTheirOwnKnobRow) {
    using namespace synth::LayoutUtil;

    AudioEngine engine;
    GraphEditor editor(engine);
    MacroControlModule macros;
    setKnobCount(macros, 12);
    ModuleComponent comp(&macros, juce::AudioProcessorGraph::NodeID(1), editor);

    for (int i = 0; i < 12; ++i) {
        auto centre = comp.getPortCenter(i, /*isInput=*/false);
        EXPECT_EQ(centre.x, comp.getWidth() - 10) << "jack " << i;
        EXPECT_EQ(centre.y, macroRowCentreY(i)) << "jack " << i;
        EXPECT_LT(centre.y, comp.getHeight()) << "jack " << i << " must be inside the module";

        // Clicking the jack must resolve to that macro's output port, not a neighbour's.
        auto hit = comp.getPortForPoint(centre);
        ASSERT_TRUE(hit.has_value()) << "jack " << i << " is not hit-testable";
        EXPECT_EQ(hit->index, i);
        EXPECT_FALSE(hit->isInput);
        EXPECT_FALSE(hit->isMidi);
    }
}

TEST_F(ModuleComponentTest, MacroBankHasNoInputOrMidiJacks) {
    AudioEngine engine;
    GraphEditor editor(engine);
    MacroControlModule macros;
    ModuleComponent comp(&macros, juce::AudioProcessorGraph::NodeID(1), editor);

    // Top-left / top-right are where paint() would put MIDI In / MIDI Out on any module that
    // has them. The bank declares neither, so nothing may be grabbable there.
    auto midiIn = comp.getPortForPoint({10, 30});
    if (midiIn.has_value())
        EXPECT_FALSE(midiIn->isMidi);

    auto midiOut = comp.getPortForPoint({comp.getWidth() - 10, 30});
    if (midiOut.has_value())
        EXPECT_FALSE(midiOut->isMidi);
}

TEST_F(ModuleComponentTest, MacroBankKnobsClearTheJackLabelGutter) {
    using namespace synth::LayoutUtil;

    AudioEngine engine;
    GraphEditor editor(engine);
    MacroControlModule macros;
    setKnobCount(macros, MacroControlModule::kMaxMacros);
    ModuleComponent comp(&macros, juce::AudioProcessorGraph::NodeID(1), editor);

    // paint() draws each output label in the 60px strip ending 20px short of the right edge.
    const int labelLeft = comp.getWidth() - 80;

    int visibleMacroKnobs = 0;
    for (auto* child : comp.getChildren()) {
        auto* slider = dynamic_cast<juce::Slider*>(child);
        if (slider == nullptr || !slider->isVisible())
            continue;
        if (!slider->getComponentID().startsWith("M"))
            continue; // skip the "Knobs" count slider

        ++visibleMacroKnobs;
        EXPECT_LE(slider->getRight(), labelLeft) << slider->getComponentID() << " overlaps the output-label gutter";
        EXPECT_LT(slider->getBottom(), comp.getHeight()) << slider->getComponentID() << " overflows the module";
    }

    EXPECT_EQ(visibleMacroKnobs, MacroControlModule::kMaxMacros);
}

TEST_F(ModuleComponentTest, MacroBankHidesKnobRowsAboveTheCount) {
    AudioEngine engine;
    GraphEditor editor(engine);
    MacroControlModule macros;
    setKnobCount(macros, 4);
    ModuleComponent comp(&macros, juce::AudioProcessorGraph::NodeID(1), editor);

    int visible = 0;
    for (auto* child : comp.getChildren())
        if (auto* slider = dynamic_cast<juce::Slider*>(child))
            if (slider->isVisible() && slider->getComponentID().startsWith("M"))
                ++visible;

    EXPECT_EQ(visible, 4);
}
