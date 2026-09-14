// ModuleComponent port geometry/hit-testing tests: port-column bounds, getPortCenter/getPortForPoint.

#include "ModuleComponentTestFixture.h"

#include "Modules/ADSRModule.h"
#include "Modules/MathModule.h"
#include "Modules/OscillatorModule.h"
#include "Modules/SamplerModule.h"
#include "Modules/VCAModule.h"
#include "Modules/VoiceMixerModule.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

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

TEST_F(ModuleComponentTest, VoiceMixerLastInputJackOverflowsBounds) {
    AudioEngine engine;
    GraphEditor editor(engine);

    VoiceMixerModule mixer;
    ModuleComponent mixerComponent(&mixer, juce::AudioProcessorGraph::NodeID(3), editor);
    expectAllVisiblePortsWithinBounds(mixerComponent, mixer, "Voice Mixer");
}

// The waveform view and every other body widget must sit BELOW the lowest port label. This is the
// regression guard for the reported overlap: the old layout started content at 30 + numInputs*20 + 10
// while getPortCenter() puts the first jack at y=70, so a 7-input module drew its body straight over
// the last jack.
TEST_F(ModuleComponentTest, BodyContentClearsEveryPortLabel) {
    AudioEngine engine;
    GraphEditor editor(engine);
    SamplerModule processor;
    ModuleComponent moduleComponent(&processor, juce::AudioProcessorGraph::NodeID(1), editor);

    // Lowest visible jack on either side, plus the half-height of its label box.
    const int lastInputY = moduleComponent.getPortCenter(processor.getVisibleInputPortCount() - 1, true).y;
    const int lastOutputY = moduleComponent.getPortCenter(processor.getVisibleOutputPortCount() - 1, false).y;
    const int portsBottom = std::max(lastInputY, lastOutputY) + 10;

    int checked = 0;
    for (auto* child : moduleComponent.getChildren()) {
        // Header buttons live in the title bar by design; everything else is body content.
        if (dynamic_cast<juce::DrawableButton*>(child) != nullptr)
            continue;
        if (!child->isVisible() || child->getBounds().isEmpty())
            continue;

        EXPECT_GE(child->getY(), portsBottom)
            << "child at y=" << child->getY() << " overlaps a port label (ports end at " << portsBottom << ")";
        ++checked;
    }
    EXPECT_GT(checked, 0) << "expected some body content to check";
}

// Regression guard for the "MIDI row too close under the header" complaint: the header's bottom
// hairline is drawn at a fixed y=24 (the headerHeight literal ModuleComponent::paint() passes to
// drawModulePanel()), and the MIDI-in dot used to sit only 1px below it. Pins the breathing-room
// fix (header offset base 30->38) numerically instead of relying on a visual check alone.
TEST_F(ModuleComponentTest, MidiInDotClearsHeaderHairlineWithBreathingRoom) {
    AudioEngine engine;
    GraphEditor editor(engine);
    // ADSR is a real MIDI-accepting module (its gate falls back to note-on/off — see
    // ADSRModule::acceptsMidi()), unlike Math, which used to accept MIDI only by inheriting
    // ModuleBase's true/true default before the per-module MIDI-flag audit corrected it.
    ADSRModule adsr;
    ASSERT_TRUE(adsr.acceptsMidi());
    ModuleComponent moduleComponent(&adsr, juce::AudioProcessorGraph::NodeID(1), editor);

    // Hit-test near the documented MIDI-in position rather than hardcoding its bounds, so this
    // keeps working if the x/y literals ever move together again.
    auto port = moduleComponent.getPortForPoint({10, 38});
    ASSERT_TRUE(port.has_value()) << "expected a MIDI input port near (10, 38)";
    EXPECT_TRUE(port->isMidi);
    EXPECT_TRUE(port->isInput);

    constexpr int headerHairlineY = 24; // literal passed to drawModulePanel() in ModuleComponent::paint()
    constexpr int kMinClearance = 6;
    EXPECT_GE(port->area.getY() - headerHairlineY, kMinClearance)
        << "MIDI dot top edge (" << port->area.getY() << ") sits too close under the header hairline ("
        << headerHairlineY << ")";
}

// Inc-4: Verify getPortCenter clamps out-of-range indices to the last visible jack
// so poly-bus wire endpoints never land at a phantom y below the module.
TEST_F(ModuleComponentTest, GetPortCenter_ClampsOutOfRangeToLastVisibleJack) {
    AudioEngine engine;
    GraphEditor editor(engine);
    VCAModule processor; // VCA: getVisibleInputPortCount() == 3, getVisibleOutputPortCount() == 2 (#219)
    ModuleComponent moduleComponent(&processor, juce::AudioProcessorGraph::NodeID(2), editor);
    moduleComponent.setSize(280, 200);

    // --- Input side ---
    // VCA has 3 visible input ports (indices 0-2).
    // Index 8 is far beyond visible range; it must clamp to index 2 (last visible).
    auto p_in_2 = moduleComponent.getPortCenter(2, /*isInput=*/true);
    auto p_in_8 = moduleComponent.getPortCenter(8, /*isInput=*/true);
    auto p_in_0 = moduleComponent.getPortCenter(0, /*isInput=*/true);
    auto p_in_0_ref = moduleComponent.getPortCenter(0, /*isInput=*/true);

    // Clamped (index 8 -> index 2): y must equal the y for index 2.
    EXPECT_EQ(p_in_8.y, p_in_2.y)
        << "getPortCenter(8,true).y should clamp to getPortCenter(2,true).y (last visible input jack)";

    // Must NOT equal the unbounded phantom formula value (headerHeight + portOffset + 8*20 + 20 = 30+0+160+20=210).
    // We check it is strictly less than that phantom value.
    int phantomY = 30 + 8 * 20 + 20; // headerHeight=30, yStep=20, portOffset=0 for VCA (no MIDI out)
    EXPECT_LT(p_in_8.y, phantomY) << "getPortCenter(8,true).y must not equal the phantom unbounded formula value";

    // In-bounds index (0) must be unchanged: clamped == index.
    EXPECT_EQ(p_in_0.y, p_in_0_ref.y) << "getPortCenter(0,true) must be unchanged (in-bounds index, no clamping)";

    // --- Output side ---
    // VCA has 2 visible output ports (Audio L / Audio R).
    auto p_out_1 = moduleComponent.getPortCenter(1, /*isInput=*/false);
    auto p_out_5 = moduleComponent.getPortCenter(5, /*isInput=*/false);

    EXPECT_EQ(p_out_5.y, p_out_1.y)
        << "getPortCenter(5,false).y should clamp to getPortCenter(1,false).y (last visible output jack)";
}
