#include "../Source/AI/AIStateMapper.h"
#include "../Source/Modules/ADSRModule.h"
#include "../Source/Modules/FX/DelayModule.h"
#include "../Source/Modules/LFOModule.h"
#include "../Source/Modules/MacroControlModule.h"
#include "../Source/Modules/MathModule.h"
#include "../Source/Modules/MidiKeyboardModule.h"
#include "../Source/Modules/OscillatorModule.h"
#include "../Source/Modules/SamplerModule.h"
#include "../Source/Modules/VCAModule.h"
#include "../Source/Modules/VoiceMixerModule.h"
#include "../Source/Modules/WavetableOscillatorModule.h"
#include "../Source/UI/GraphEditor.h"
#include "../Source/UI/LayoutUtil.h"
#include "../Source/UI/ModuleComponent.h"
#include "../Source/UI/ModuleLibraryComponent.h"
#include "../Source/UI/Theme/AppLookAndFeel.h"
#include "../Source/UI/Theme/BuiltInThemes.h"
#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
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

TEST_F(ModuleComponentTest, VoiceMixerLastInputJackOverflowsBounds) {
    AudioEngine engine;
    GraphEditor editor(engine);

    VoiceMixerModule mixer;
    ModuleComponent mixerComponent(&mixer, juce::AudioProcessorGraph::NodeID(3), editor);
    expectAllVisiblePortsWithinBounds(mixerComponent, mixer, "Voice Mixer");
}

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
    EXPECT_EQ(moduleComponent.getHeight(), 645)
        << "keep estimateModuleSize(\"Sampler\") in GraphEditor.cpp in sync with this";

    EXPECT_NO_THROW(moduleComponent.timerCallback());
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

// The drag ghost shown while dragging out of the library must match the component the drop actually
// creates, otherwise the ghost lies about where the module will land.
TEST_F(ModuleComponentTest, EstimatedModuleSizesMatchTheRealComponents) {
    // Driven off the library itself rather than a parallel hand-kept list, so a module added to the
    // library is covered automatically. (Bitcrusher and Pitch Shifter reached main without estimates
    // precisely because a hardcoded list here would not have noticed them.)
    ModuleLibraryComponent library;
    const juce::StringArray libraryTypes = library.getDraggableModuleNames();
    ASSERT_GT(libraryTypes.size(), 15) << "expected the full library list";

    AudioEngine engine;
    GraphEditor editor(engine);

    for (const auto& type : libraryTypes) {
        auto processor = synth::AIStateMapper::createModule(type);
        ASSERT_NE(processor, nullptr) << type << " is offered by the library but has no factory entry";

        ModuleComponent comp(processor.get(), juce::AudioProcessorGraph::NodeID(1), editor);
        const auto estimate = GraphEditor::estimateModuleSize(type);

        EXPECT_EQ(estimate.x, comp.getWidth()) << "estimateModuleSize(\"" << type << "\") width";
        EXPECT_EQ(estimate.y, comp.getHeight()) << "estimateModuleSize(\"" << type << "\") height";
    }
}

// Track In is deliberately absent from the library, so the loop above cannot cover it —
// but estimateModuleSize is still queried for it programmatically (the timeline's add-track flow
// places the node), and a stale estimate there misplaces the card. Same assertion, one type.
TEST_F(ModuleComponentTest, TrackInEstimatedSizeMatchesTheRealComponent) {
    ModuleLibraryComponent library;
    EXPECT_FALSE(library.getDraggableModuleNames().contains("Track In"))
        << "Track In is internal-only and must stay out of the module library";

    auto processor = synth::AIStateMapper::createModule("Track In");
    ASSERT_NE(processor, nullptr);

    AudioEngine engine;
    GraphEditor editor(engine);
    ModuleComponent comp(processor.get(), juce::AudioProcessorGraph::NodeID(1), editor);

    const auto estimate = GraphEditor::estimateModuleSize("Track In");
    EXPECT_EQ(estimate.x, comp.getWidth());
    EXPECT_EQ(estimate.y, comp.getHeight());
}

// Three knobs per row (the body sits below the ports, so it can use nearly the full card width).
TEST_F(ModuleComponentTest, KnobsAreLaidOutThreePerRow) {
    AudioEngine engine;
    GraphEditor editor(engine);
    SamplerModule processor; // 7 float/int params -> 3 rows of 3, 3, 1
    ModuleComponent moduleComponent(&processor, juce::AudioProcessorGraph::NodeID(1), editor);

    std::vector<juce::Slider*> knobs;
    for (auto* child : moduleComponent.getChildren())
        if (auto* slider = dynamic_cast<juce::Slider*>(child))
            knobs.push_back(slider);

    ASSERT_EQ(knobs.size(), 7u);

    // Children are added in parameter order, so the first three share a row.
    EXPECT_EQ(knobs[0]->getY(), knobs[1]->getY());
    EXPECT_EQ(knobs[1]->getY(), knobs[2]->getY());
    EXPECT_LT(knobs[0]->getX(), knobs[1]->getX());
    EXPECT_LT(knobs[1]->getX(), knobs[2]->getX());

    // The fourth wraps to a new row, back at the first column.
    EXPECT_GT(knobs[3]->getY(), knobs[2]->getY());
    EXPECT_EQ(knobs[3]->getX(), knobs[0]->getX());
}

TEST_F(ModuleComponentTest, NonSamplerModulesGetNoSamplerChrome) {
    AudioEngine engine;
    GraphEditor editor(engine);
    OscillatorModule processor;
    ModuleComponent moduleComponent(&processor, juce::AudioProcessorGraph::NodeID(1), editor);

    for (auto* child : moduleComponent.getChildren())
        EXPECT_EQ(dynamic_cast<SampleWaveformComponent*>(child), nullptr);
}

// --- Audio-file drag and drop -----------------------------------------------------------------

TEST_F(ModuleComponentTest, SamplerAcceptsAudioFileDropAndLoadsIt) {
    AudioEngine engine;
    GraphEditor editor(engine);
    SamplerModule processor;
    processor.prepareToPlay(44100.0, 512);
    ModuleComponent moduleComponent(&processor, juce::AudioProcessorGraph::NodeID(1), editor);

    // A real, readable wav so the drop exercises the actual load path.
    auto file = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("drop-test-146.wav");
    file.deleteFile();
    {
        juce::AudioBuffer<float> audio(1, 1024);
        audio.clear();
        for (int i = 0; i < 1024; ++i)
            audio.setSample(0, i, 0.25f);

        juce::WavAudioFormat wavFormat;
        std::unique_ptr<juce::FileOutputStream> stream(file.createOutputStream());
        ASSERT_NE(stream, nullptr);
        std::unique_ptr<juce::AudioFormatWriter> writer(wavFormat.createWriterFor(stream.get(), 44100.0, 1, 32, {}, 0));
        ASSERT_NE(writer, nullptr);
        stream.release();
        writer->writeFromAudioSampleBuffer(audio, 0, 1024);
    }

    juce::StringArray dropped{file.getFullPathName()};
    EXPECT_TRUE(moduleComponent.isInterestedInFileDrag(dropped));

    moduleComponent.fileDragEnter(dropped, 10, 10);
    EXPECT_TRUE(moduleComponent.isFileDragHighlighted());

    moduleComponent.filesDropped(dropped, 10, 10);
    EXPECT_FALSE(moduleComponent.isFileDragHighlighted()) << "the highlight must clear on drop";
    EXPECT_EQ(processor.getSampleFilePath(), file.getFullPathName());
    ASSERT_NE(processor.getSample(), nullptr);

    file.deleteFile();
}

TEST_F(ModuleComponentTest, SamplerIgnoresNonAudioFileDrag) {
    AudioEngine engine;
    GraphEditor editor(engine);
    SamplerModule processor;
    ModuleComponent moduleComponent(&processor, juce::AudioProcessorGraph::NodeID(1), editor);

    juce::StringArray notAudio{"/tmp/patch.json", "/tmp/readme.md"};
    EXPECT_FALSE(moduleComponent.isInterestedInFileDrag(notAudio));
}

TEST_F(ModuleComponentTest, NonSamplerModuleRefusesFileDragSoItFallsThroughToTheCanvas) {
    AudioEngine engine;
    GraphEditor editor(engine);
    OscillatorModule processor;
    ModuleComponent moduleComponent(&processor, juce::AudioProcessorGraph::NodeID(1), editor);

    // Returning false is what lets JUCE keep walking up to GraphEditor, which creates a new Sampler.
    juce::StringArray dropped{"/tmp/kick.wav"};
    EXPECT_FALSE(moduleComponent.isInterestedInFileDrag(dropped));
    EXPECT_FALSE(moduleComponent.isFileDragHighlighted());
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

// The ADSR has its own layout branch that used to position only its four sliders and return,
// leaving every auto-generated toggle at default (0,0,0,0) bounds. That made the "Poly" checkbox
// invisible and unclickable, so poly mode was unreachable on this module from the UI.
TEST_F(ModuleComponentTest, AdsrPolyToggleIsLaidOutInsideTheModule) {
    AudioEngine engine;
    GraphEditor editor(engine);
    ADSRModule processor;
    ModuleComponent moduleComponent(&processor, juce::AudioProcessorGraph::NodeID(1), editor);

    juce::ToggleButton* polyToggle = nullptr;
    for (auto* child : moduleComponent.getChildren())
        if (auto* toggle = dynamic_cast<juce::ToggleButton*>(child))
            if (toggle->getComponentID() == "Poly")
                polyToggle = toggle;

    ASSERT_NE(polyToggle, nullptr) << "ADSR exposes a 'poly' parameter, so it must render a Poly toggle";
    EXPECT_FALSE(polyToggle->getBounds().isEmpty()) << "Poly toggle must be given real bounds, not (0,0,0,0)";
    EXPECT_TRUE(moduleComponent.getLocalBounds().contains(polyToggle->getBounds()))
        << "Poly toggle must sit inside the module's bounds to be visible and clickable";
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

// ---------------------------------------------------------------------------
// Wavetable module card: bespoke display + "Load Wavetable..." button
// ---------------------------------------------------------------------------

TEST_F(ModuleComponentTest, WavetableCardBuildsDisplayAndLoadButton) {
    AudioEngine engine;
    GraphEditor editor(engine);
    WavetableOscillatorModule processor;
    ModuleComponent moduleComponent(&processor, juce::AudioProcessorGraph::NodeID(1), editor);

    WavetableDisplayComponent* display = nullptr;
    juce::TextButton* loadButton = nullptr;
    int comboCount = 0, sliderCount = 0, toggleCount = 0;

    for (auto* child : moduleComponent.getChildren()) {
        if (auto* d = dynamic_cast<WavetableDisplayComponent*>(child))
            display = d;
        else if (auto* b = dynamic_cast<juce::TextButton*>(child)) {
            if (b->getButtonText() == "Load Wavetable...")
                loadButton = b;
        } else if (dynamic_cast<juce::ComboBox*>(child) != nullptr)
            ++comboCount;
        else if (dynamic_cast<juce::Slider*>(child) != nullptr)
            ++sliderCount;
        else if (dynamic_cast<juce::ToggleButton*>(child) != nullptr)
            ++toggleCount;
    }

    ASSERT_NE(display, nullptr) << "Wavetable cards must own a WavetableDisplayComponent";
    ASSERT_NE(loadButton, nullptr) << "Wavetable cards must own a \"Load Wavetable...\" button";

    // Table/Warp/Stack/Sub Oct/Sub Wave/Sync In/Import/Interp -> 8 combos;
    // Position/Octave/Coarse/Fine/Level/Unison/Detune/Warp Amt/Phase/Rand Phase/Spread/
    // Width/Blend/Sub/Pan -> 15 sliders; Poly -> 1 toggle, plus the always-present
    // "Show Scope" toggle.
    EXPECT_EQ(comboCount, 8);
    EXPECT_EQ(sliderCount, 15);
    EXPECT_EQ(toggleCount, 2);

    // Both bespoke children must be laid out inside the card.
    EXPECT_FALSE(display->getBounds().isEmpty());
    EXPECT_FALSE(loadButton->getBounds().isEmpty());
    EXPECT_TRUE(moduleComponent.getLocalBounds().contains(display->getBounds()));
    EXPECT_TRUE(moduleComponent.getLocalBounds().contains(loadButton->getBounds()));

    // Height is deliberately not asserted here: EstimatedModuleSizesMatchTheRealComponents
    // already pins the real card against GraphEditor::estimateModuleSize for every offered
    // type, so duplicating the number would just be a second thing to update by hand.
    // Width IS asserted: the card went double-width in issue #180 and the wide-card branches
    // of layoutDefaultContent (6 knob columns, paired combos) hang off that.
    EXPECT_EQ(moduleComponent.getWidth(), synth::LayoutUtil::kDoubleWidth);
    EXPECT_GT(moduleComponent.getHeight(), 100);
}

// The folder browser is the module's, but its chrome is the card's — a Wavetable card without
// the prev/next buttons leaves the browser unreachable.
TEST_F(ModuleComponentTest, WavetableCardBuildsFolderBrowserChrome) {
    AudioEngine engine;
    GraphEditor editor(engine);
    WavetableOscillatorModule processor;
    ModuleComponent moduleComponent(&processor, juce::AudioProcessorGraph::NodeID(1), editor);

    juce::TextButton* folderButton = nullptr;
    juce::TextButton* prevButton = nullptr;
    juce::TextButton* nextButton = nullptr;

    for (auto* child : moduleComponent.getChildren()) {
        if (auto* b = dynamic_cast<juce::TextButton*>(child)) {
            if (b->getButtonText() == "Folder...")
                folderButton = b;
            else if (b->getButtonText() == "<")
                prevButton = b;
            else if (b->getButtonText() == ">")
                nextButton = b;
        }
    }

    ASSERT_NE(folderButton, nullptr) << "Wavetable cards must own a \"Folder...\" button";
    ASSERT_NE(prevButton, nullptr) << "Wavetable cards must own a previous-table button";
    ASSERT_NE(nextButton, nullptr) << "Wavetable cards must own a next-table button";

    for (auto* b : {folderButton, prevButton, nextButton}) {
        EXPECT_FALSE(b->getBounds().isEmpty());
        EXPECT_TRUE(moduleComponent.getLocalBounds().contains(b->getBounds()));
    }

    // Clicking next with no folder selected must not crash or throw — it just reports back.
    EXPECT_NO_THROW(nextButton->triggerClick());
}

// The 16 CV jacks run in two columns so the gutter stops dictating the card height — but BOTH
// stay on the left. Inputs-left / outputs-right is what makes signal flow read left to right,
// and splitting inputs across both edges costs more in comprehension than the height saves.
TEST_F(ModuleComponentTest, WavetableCardKeepsEveryInputJackOnTheLeft) {
    AudioEngine engine;
    GraphEditor editor(engine);
    WavetableOscillatorModule processor;
    ModuleComponent moduleComponent(&processor, juce::AudioProcessorGraph::NodeID(1), editor);

    const int numJacks = processor.getVisibleInputPortCount();
    const int numOuts = processor.getVisibleOutputPortCount();
    ASSERT_EQ(numJacks, WavetableOscillatorModule::kNumJacks);

    std::set<std::pair<int, int>> seen;
    std::set<int> columns;
    for (int i = 0; i < numJacks; ++i) {
        const auto p = moduleComponent.getPortCenter(i, true);
        EXPECT_TRUE(seen.insert({p.x, p.y}).second) << "jack " << i << " overlaps another jack";
        EXPECT_TRUE(moduleComponent.getLocalBounds().contains(p)) << "jack " << i << " sits outside the card";
        EXPECT_LT(p.x, moduleComponent.getWidth() / 2) << "input jack " << i << " must stay on the left half";
        columns.insert(p.x);
    }
    EXPECT_EQ(columns.size(), 2u) << "expected exactly two jack columns";

    // Outputs keep the right edge to themselves.
    for (int o = 0; o < numOuts; ++o)
        EXPECT_GT(moduleComponent.getPortCenter(o, false).x, moduleComponent.getWidth() / 2);

    // Column-major: the first half runs down column 0, so jack 0 and the midpoint jack share a row.
    EXPECT_EQ(moduleComponent.getPortCenter(0, true).y, moduleComponent.getPortCenter(numJacks / 2, true).y);
    EXPECT_LT(moduleComponent.getPortCenter(0, true).x, moduleComponent.getPortCenter(numJacks / 2, true).x);

    // The body must clear the LOWEST jack, which is not necessarily the last one.
    int lowest = 0;
    for (int i = 0; i < numJacks; ++i)
        lowest = std::max(lowest, moduleComponent.getPortCenter(i, true).y);
    for (auto* child : moduleComponent.getChildren())
        if (child->isVisible() && dynamic_cast<juce::Slider*>(child) != nullptr)
            EXPECT_GT(child->getY(), lowest) << "a knob overlaps the jack gutter";
}

// Serum-style modulation drop: releasing a cable on a KNOB resolves to that parameter's CV jack,
// so a 16-jack module can be patched without aiming at the gutter at all.
TEST_F(ModuleComponentTest, KnobsResolveToTheirCVJackAsModulationDropTargets) {
    AudioEngine engine;
    GraphEditor editor(engine);
    WavetableOscillatorModule processor;
    ModuleComponent moduleComponent(&processor, juce::AudioProcessorGraph::NodeID(1), editor);

    juce::Slider* position = nullptr;
    for (auto* child : moduleComponent.getChildren())
        if (auto* s = dynamic_cast<juce::Slider*>(child))
            if (s->getComponentID() == "Position")
                position = s;
    ASSERT_NE(position, nullptr);
    ASSERT_TRUE(position->isVisible()) << "Position is pinned, so it shows on every page";

    const auto hit = moduleComponent.getModTargetPortForPoint(position->getBounds().getCentre());
    ASSERT_TRUE(hit.has_value()) << "a visible knob must be a modulation drop target";
    EXPECT_TRUE(hit->isInput) << "a knob resolves to an INPUT port";
    EXPECT_FALSE(hit->isMidi);
    EXPECT_EQ(hit->index, WavetableOscillatorModule::kJackPosition)
        << "the drop must land on the parameter's own CV channel";

    // Empty card is not a drop target.
    EXPECT_FALSE(moduleComponent.getModTargetPortForPoint({moduleComponent.getWidth() - 4, 4}).has_value());

    // A knob on a hidden page keeps its bounds, so it must not swallow a drop aimed elsewhere.
    juce::Slider* octave = nullptr;
    for (auto* child : moduleComponent.getChildren())
        if (auto* s = dynamic_cast<juce::Slider*>(child))
            if (s->getComponentID() == "Octave")
                octave = s;
    ASSERT_NE(octave, nullptr);
    const auto octaveCentre = octave->getBounds().getCentre();
    ASSERT_TRUE(moduleComponent.getModTargetPortForPoint(octaveCentre).has_value());

    for (auto* child : moduleComponent.getChildren())
        if (auto* b = dynamic_cast<juce::TextButton*>(child))
            if (b->getComponentID() == "wtTab1")
                b->onClick(); // headless: triggerClick posts async, with no pump to deliver it

    EXPECT_FALSE(moduleComponent.getModTargetPortForPoint(octaveCentre).has_value())
        << "a knob whose page is hidden must not accept a modulation drop";
}

// The highlight is what makes the drop aimed rather than guessed at.
TEST_F(ModuleComponentTest, ModulationDropTargetHighlightTracksTheHoveredKnob) {
    AudioEngine engine;
    GraphEditor editor(engine);
    WavetableOscillatorModule processor;
    ModuleComponent moduleComponent(&processor, juce::AudioProcessorGraph::NodeID(1), editor);

    EXPECT_EQ(moduleComponent.getModDropTargetChannel(), -1);

    EXPECT_TRUE(moduleComponent.setModDropTargetChannel(WavetableOscillatorModule::kJackPosition));
    EXPECT_EQ(moduleComponent.getModDropTargetChannel(), WavetableOscillatorModule::kJackPosition);

    // Setting the same target again is not a change, so the caller can skip the repaint.
    EXPECT_FALSE(moduleComponent.setModDropTargetChannel(WavetableOscillatorModule::kJackPosition));

    EXPECT_TRUE(moduleComponent.setModDropTargetChannel(-1));
    EXPECT_EQ(moduleComponent.getModDropTargetChannel(), -1);

    // Painting with a highlight set must not crash (headless: no themed LookAndFeel).
    moduleComponent.setModDropTargetChannel(WavetableOscillatorModule::kJackPosition);
    juce::Image img(juce::Image::ARGB, moduleComponent.getWidth(), moduleComponent.getHeight(), true);
    juce::Graphics g(img);
    EXPECT_NO_THROW(moduleComponent.paint(g));
}

// Controls are paged. Switching pages must not resize the card — a card that grew and shrank
// would shove its neighbours around the canvas on every tab click.
TEST_F(ModuleComponentTest, WavetableTabsSwitchContentWithoutResizingTheCard) {
    AudioEngine engine;
    GraphEditor editor(engine);
    WavetableOscillatorModule processor;
    ModuleComponent moduleComponent(&processor, juce::AudioProcessorGraph::NodeID(1), editor);

    std::vector<juce::TextButton*> tabs;
    for (auto* child : moduleComponent.getChildren())
        if (auto* b = dynamic_cast<juce::TextButton*>(child))
            if (b->getComponentID().startsWith("wtTab"))
                tabs.push_back(b);

    ASSERT_GE(tabs.size(), 4u) << "expected a multi-page tab strip";

    const auto visibleSliderNames = [&] {
        std::set<juce::String> names;
        for (auto* child : moduleComponent.getChildren())
            if (auto* s = dynamic_cast<juce::Slider*>(child))
                if (s->isVisible())
                    names.insert(s->getComponentID());
        return names;
    };

    const int height = moduleComponent.getHeight();
    const auto firstPage = visibleSliderNames();

    // Position and Warp Amt are pinned above the strip, so they survive every page switch.
    EXPECT_TRUE(firstPage.count("Position")) << "Position must stay pinned";
    EXPECT_TRUE(firstPage.count("Warp Amt")) << "Warp Amt must stay pinned";

    for (size_t i = 1; i < tabs.size(); ++i) {
        tabs[i]->onClick(); // headless: triggerClick posts async, with no message pump to deliver it
        EXPECT_EQ(moduleComponent.getHeight(), height) << "the card resized on tab " << i;

        const auto page = visibleSliderNames();
        EXPECT_TRUE(page.count("Position")) << "Position vanished on tab " << i;
        EXPECT_TRUE(page.count("Warp Amt")) << "Warp Amt vanished on tab " << i;
        EXPECT_NE(page, firstPage) << "tab " << i << " shows the same controls as the first page";

        // Whatever is showing must be laid out inside the card.
        for (auto* child : moduleComponent.getChildren())
            if (child->isVisible() && dynamic_cast<juce::Slider*>(child) != nullptr)
                EXPECT_TRUE(moduleComponent.getLocalBounds().contains(child->getBounds()))
                    << child->getComponentID() << " is outside the card on tab " << i;
    }

    // Every knob must be reachable from some page — a control on no page is unusable.
    std::set<juce::String> everSeen;
    for (size_t i = 0; i < tabs.size(); ++i) {
        tabs[i]->onClick(); // headless: triggerClick posts async, with no message pump to deliver it
        for (const auto& n : visibleSliderNames())
            everSeen.insert(n);
    }
    int totalSliders = 0;
    for (auto* child : moduleComponent.getChildren())
        if (dynamic_cast<juce::Slider*>(child) != nullptr)
            ++totalSliders;
    EXPECT_EQ((int)everSeen.size(), totalSliders) << "some knob is not reachable from any tab";
}

// A modulation ring is drawn from its knob's bounds. A knob on an inactive tab page keeps the
// bounds it had when its page was last laid out, so before this guard the ring kept painting on
// empty card after a page switch — an orange arc floating with no knob under it.
TEST_F(ModuleComponentTest, ModulationRingsSkipKnobsOnInactiveTabPages) {
    AudioEngine engine;
    GraphEditor editor(engine);
    WavetableOscillatorModule processor;
    ModuleComponent moduleComponent(&processor, juce::AudioProcessorGraph::NodeID(1), editor);

    std::vector<juce::TextButton*> tabs;
    for (auto* child : moduleComponent.getChildren())
        if (auto* b = dynamic_cast<juce::TextButton*>(child))
            if (b->getComponentID().startsWith("wtTab"))
                tabs.push_back(b);
    ASSERT_GE(tabs.size(), 2u);

    // Page 0 (Tune) owns Octave; Position is pinned above the strip.
    EXPECT_GE(moduleComponent.getModRingSliderIndex("Octave"), 0);
    EXPECT_GE(moduleComponent.getModRingSliderIndex("Position"), 0);

    tabs[1]->onClick(); // headless: triggerClick posts async, with no message pump to deliver it

    EXPECT_EQ(moduleComponent.getModRingSliderIndex("Octave"), -1)
        << "a ring must not be drawn for a knob whose page is hidden";
    EXPECT_GE(moduleComponent.getModRingSliderIndex("Position"), 0) << "a pinned knob keeps its ring on every page";

    // A parameter with no knob at all never gets a ring.
    EXPECT_EQ(moduleComponent.getModRingSliderIndex("Not A Parameter"), -1);
}

// A Wavetable card claims audio-file drops itself. Before issue #180 it returned false and the
// drop fell through to GraphEditor, which spawned an unrelated Sampler next to it.
TEST_F(ModuleComponentTest, WavetableCardAcceptsAudioFileDrag) {
    AudioEngine engine;
    GraphEditor editor(engine);
    WavetableOscillatorModule processor;
    ModuleComponent moduleComponent(&processor, juce::AudioProcessorGraph::NodeID(1), editor);

    EXPECT_TRUE(moduleComponent.isInterestedInFileDrag({"/tmp/table.wav"}));
    EXPECT_TRUE(moduleComponent.isInterestedInFileDrag({"/tmp/table.flac"}));
    EXPECT_FALSE(moduleComponent.isInterestedInFileDrag({"/tmp/notes.txt"}));
}

TEST_F(ModuleComponentTest, WavetableCardPaintsAndTicksWithoutCrashing) {
    AudioEngine engine;
    GraphEditor editor(engine);
    WavetableOscillatorModule processor;
    ModuleComponent moduleComponent(&processor, juce::AudioProcessorGraph::NodeID(1), editor);

    EXPECT_NO_THROW(moduleComponent.timerCallback());

    juce::Image img(juce::Image::ARGB, moduleComponent.getWidth(), moduleComponent.getHeight(), true);
    juce::Graphics g(img);
    EXPECT_NO_THROW(moduleComponent.paint(g));
    EXPECT_TRUE(img.isValid());
}

// Theme switch must recolour the on-screen MidiKeyboardComponent. Colours are set on the
// component itself (not via AppLookAndFeel ColourIds — juce_audio_utils is not linked into
// Core), so lookAndFeelChanged() has to push them again or the keys keep the previous theme.
TEST_F(ModuleComponentTest, MidiKeyboardKeysFollowThemeChange) {
    AudioEngine engine;
    GraphEditor editor(engine);
    MidiKeyboardModule keyboard;
    ModuleComponent moduleComponent(&keyboard, juce::AudioProcessorGraph::NodeID(1), editor);

    juce::MidiKeyboardComponent* keys = nullptr;
    for (int i = 0; i < moduleComponent.getNumChildComponents(); ++i)
        if (auto* kb = dynamic_cast<juce::MidiKeyboardComponent*>(moduleComponent.getChildComponent(i)))
            keys = kb;
    ASSERT_NE(keys, nullptr) << "MIDI Keyboard card must host a MidiKeyboardComponent";

    synth::theme::AppLookAndFeel lf;
    lf.applyTheme(synth::theme::makeObsidian());
    moduleComponent.setLookAndFeel(&lf);
    EXPECT_EQ(keys->findColour(juce::MidiKeyboardComponent::whiteNoteColourId),
              synth::theme::makeObsidian().colors.bg1);
    EXPECT_EQ(keys->findColour(juce::MidiKeyboardComponent::blackNoteColourId),
              synth::theme::makeObsidian().colors.surfaceHi);
    EXPECT_EQ(keys->findColour(juce::MidiKeyboardComponent::keyDownOverlayColourId),
              synth::theme::makeObsidian().colors.accent);

    lf.applyTheme(synth::theme::makeNeon());
    moduleComponent.sendLookAndFeelChange();
    EXPECT_EQ(keys->findColour(juce::MidiKeyboardComponent::whiteNoteColourId), synth::theme::makeNeon().colors.bg1);
    EXPECT_EQ(keys->findColour(juce::MidiKeyboardComponent::blackNoteColourId),
              synth::theme::makeNeon().colors.surfaceHi);
    EXPECT_EQ(keys->findColour(juce::MidiKeyboardComponent::keyDownOverlayColourId),
              synth::theme::makeNeon().colors.accent);
    EXPECT_EQ(keys->findColour(juce::MidiKeyboardComponent::keySeparatorLineColourId),
              synth::theme::makeNeon().colors.border);
    EXPECT_EQ(keys->findColour(juce::MidiKeyboardComponent::textLabelColourId),
              synth::theme::makeNeon().colors.textPrimary);

    moduleComponent.setLookAndFeel(nullptr);
}

// --- Output-card identity treatment (module chrome) --------------------------
// Audio Output is a bare juce::AudioGraphIOProcessor, not a ModuleBase — setOutputDeviceInfoText
// / getOutputDeviceInfoTextForTest are the seam GraphEditor::refreshOutputDeviceInfo drives
// (MainComponent -> GraphEditor -> here). See docs/layout.md's module chrome section.

namespace {
/** Adds the graph's terminal audio sink the way AudioEngine does — the channel layout has to be
 *  set BEFORE the node is added (AudioGraphIOProcessor snapshots it once, in setParentGraph). */
juce::AudioProcessorGraph::Node::Ptr addAudioOutputNode(juce::AudioProcessorGraph& graph) {
    using IOProcessor = juce::AudioProcessorGraph::AudioGraphIOProcessor;
    graph.setPlayConfigDetails(0, 2, 44100.0, 512);
    return graph.addNode(std::make_unique<IOProcessor>(IOProcessor::audioOutputNode));
}
} // namespace

TEST_F(ModuleComponentTest, AudioOutputCardHasNoDeviceInfoTextUntilSet) {
    AudioEngine engine;
    GraphEditor editor(engine);
    auto node = addAudioOutputNode(engine.getGraph());
    ModuleComponent moduleComponent(node->getProcessor(), node->nodeID, editor);

    EXPECT_TRUE(moduleComponent.getOutputDeviceInfoTextForTest().isEmpty());

    // Headless: no themed LookAndFeel, so the CatIO icon is absent — must still not crash.
    juce::Image img(juce::Image::ARGB, moduleComponent.getWidth(), moduleComponent.getHeight(), true);
    juce::Graphics g(img);
    EXPECT_NO_THROW(moduleComponent.paint(g));
}

TEST_F(ModuleComponentTest, AudioOutputCardStoresAndPaintsDeviceInfoText) {
    AudioEngine engine;
    GraphEditor editor(engine);
    auto node = addAudioOutputNode(engine.getGraph());
    ModuleComponent moduleComponent(node->getProcessor(), node->nodeID, editor);

    const juce::String deviceText("Test Device - 48 kHz - 2ch");
    moduleComponent.setOutputDeviceInfoText(deviceText);
    EXPECT_EQ(moduleComponent.getOutputDeviceInfoTextForTest(), deviceText);

    // Setting the same text again is not a change (mirrors setModDropTargetChannel's contract) —
    // just confirms it stays idempotent rather than asserting an internal repaint count.
    moduleComponent.setOutputDeviceInfoText(deviceText);
    EXPECT_EQ(moduleComponent.getOutputDeviceInfoTextForTest(), deviceText);

    juce::Image img(juce::Image::ARGB, moduleComponent.getWidth(), moduleComponent.getHeight(), true);
    juce::Graphics g(img);
    EXPECT_NO_THROW(moduleComponent.paint(g));

    // Real themed LnF: the identity glyph + muted subtitle path must also survive (see
    // MidiKeyboardKeysFollowThemeChange above for the same real-AppLookAndFeel pattern).
    synth::theme::AppLookAndFeel lf;
    lf.applyTheme(synth::theme::makeObsidian());
    moduleComponent.setLookAndFeel(&lf);
    EXPECT_NO_THROW(moduleComponent.paint(g));
    moduleComponent.setLookAndFeel(nullptr);

    // Empty string (e.g. Hosted mode, or the device closing) hides the line again.
    moduleComponent.setOutputDeviceInfoText({});
    EXPECT_TRUE(moduleComponent.getOutputDeviceInfoTextForTest().isEmpty());
}

TEST_F(ModuleComponentTest, SetOutputDeviceInfoTextIsANoOpOnNonOutputModules) {
    AudioEngine engine;
    GraphEditor editor(engine);
    OscillatorModule processor;
    ModuleComponent moduleComponent(&processor, juce::AudioProcessorGraph::NodeID(1), editor);

    moduleComponent.setOutputDeviceInfoText("should not apply to a regular module");
    EXPECT_TRUE(moduleComponent.getOutputDeviceInfoTextForTest().isEmpty());
}

// The type-not-name idiom: Audio Input is also a bare AudioGraphIOProcessor, but the WRONG
// IODeviceType, and must not pick up the output-only identity treatment.
TEST_F(ModuleComponentTest, SetOutputDeviceInfoTextIsANoOpOnAudioInputNode) {
    AudioEngine engine;
    GraphEditor editor(engine);
    auto& graph = engine.getGraph();
    using IOProcessor = juce::AudioProcessorGraph::AudioGraphIOProcessor;
    graph.setPlayConfigDetails(2, 0, 44100.0, 512);
    auto node = graph.addNode(std::make_unique<IOProcessor>(IOProcessor::audioInputNode));
    ModuleComponent moduleComponent(node->getProcessor(), node->nodeID, editor);

    moduleComponent.setOutputDeviceInfoText("should not apply to Audio Input");
    EXPECT_TRUE(moduleComponent.getOutputDeviceInfoTextForTest().isEmpty());
}

// --- Output-card identity glyph alignment/sizing (visual follow-up) ----------
// outputCardIconBoundsForTest() is the exact geometry ModuleComponent::paint() draws the CatIO
// glyph into — see docs/layout.md's "Audio Output card identity treatment". These tests recompute
// the same public JUCE font-metric calls independently (never reach into ModuleComponent's private
// paint code) so they pin the FORMULA/contract, not a platform-specific pixel constant.

TEST(ModuleComponentOutputIconBounds, IconIsSquareAndProportionalToTitleCapHeightNotTheFullHeaderBand) {
    synth::theme::AppLookAndFeel lf;
    lf.applyTheme(synth::theme::makeObsidian());

    const auto bounds = ModuleComponent::outputCardIconBoundsForTest(lf);

    const juce::Font titleFont(juce::FontOptions(lf.getTheme().type.h2, juce::Font::bold));
    const float expectedCapHeight = titleFont.getAscent() * 0.72f;

    EXPECT_FLOAT_EQ(bounds.getWidth(), bounds.getHeight()) << "the glyph must stay square";
    EXPECT_NEAR(bounds.getHeight(), expectedCapHeight, 0.01f);
    // Proportional to the title, not the header band: strictly smaller than both the previous
    // fixed 16px box and the header's own 24px height.
    EXPECT_LT(bounds.getHeight(), 16.0f);
    EXPECT_LT(bounds.getHeight(), 24.0f);
    EXPECT_LT(bounds.getHeight(), titleFont.getHeight())
        << "must not be sized off the full ascent+descent box the title's own text is drawn in";
}

TEST(ModuleComponentOutputIconBounds, IconRightEdgeMatchesTheActivityLEDsRightEdgeForAnEightPxTextGap) {
    synth::theme::AppLookAndFeel lf;
    lf.applyTheme(synth::theme::makeNeon()); // a different theme: the geometry must not be theme-dependent

    const auto bounds = ModuleComponent::outputCardIconBoundsForTest(lf);

    // The activity LED is fillEllipse(6, 8, 8, 8) in ModuleComponent::paint() -> right edge x=14.
    // The title's own left inset is 22 (AppLookAndFeel::drawModulePanel). Pinning the icon's right
    // edge to the LED's is what keeps that established 8px gap regardless of the icon's width.
    EXPECT_NEAR(bounds.getRight(), 14.0f, 0.001f);
    constexpr float kTitleLeftInset = 22.0f;
    EXPECT_NEAR(kTitleLeftInset - bounds.getRight(), 8.0f, 0.001f);
}

TEST(ModuleComponentOutputIconBounds, IconIsVerticallyCentredOnTheTitlesCapHeightNotTheHeaderBandsMidline) {
    synth::theme::AppLookAndFeel lf;
    lf.applyTheme(synth::theme::makeObsidian());

    const auto bounds = ModuleComponent::outputCardIconBoundsForTest(lf);

    const juce::Font titleFont(juce::FontOptions(lf.getTheme().type.h2, juce::Font::bold));
    const float capHeight = titleFont.getAscent() * 0.72f;
    constexpr float kHeaderTop = 2.0f;
    constexpr float kHeaderHeight = 24.0f;
    const float textBoxTop = kHeaderTop + (kHeaderHeight - titleFont.getHeight()) * 0.5f;
    const float baseline = textBoxTop + titleFont.getAscent();
    const float expectedCapCentreY = baseline - capHeight * 0.5f;

    EXPECT_NEAR(bounds.getCentreY(), expectedCapCentreY, 0.01f);
    // Sanity: still fully inside the 24px header band ([2, 26] in local coordinates), whatever the
    // exact resolved font metrics turn out to be on this platform.
    EXPECT_GE(bounds.getY(), kHeaderTop);
    EXPECT_LE(bounds.getBottom(), kHeaderTop + kHeaderHeight);
}

// Colour lockup: the glyph must follow the title's own colour token, not the library sidebar's
// fixed textMuted bake — rendered end-to-end through paint() (not the bounds helper), because the
// tint swap happens at paint time on a per-call clone.
TEST_F(ModuleComponentTest, AudioOutputCardIconTintsToTheTitleColourNotTheLibraryMutedTint) {
    AudioEngine engine;
    GraphEditor editor(engine);
    auto node = addAudioOutputNode(engine.getGraph());
    ModuleComponent moduleComponent(node->getProcessor(), node->nodeID, editor);

    synth::theme::AppLookAndFeel lf;
    lf.applyTheme(synth::theme::makeObsidian());
    moduleComponent.setLookAndFeel(&lf);

    juce::Image img(juce::Image::ARGB, moduleComponent.getWidth(), moduleComponent.getHeight(), true);
    juce::Graphics g(img);
    EXPECT_NO_THROW(moduleComponent.paint(g));

    const auto bounds = ModuleComponent::outputCardIconBoundsForTest(lf).getSmallestIntegerContainer().expanded(1);
    // titleColour (not selected, not bypassed) is textPrimary — near-white — checked inline below.
    const auto libraryMutedTint = synth::theme::makeObsidian().colors.textMuted; // the OLD (wrong) tint

    // Counts, not a single any-pixel boolean: the icon is tiny (~cap-height px) against a dark
    // header, so its anti-aliased edges sweep through every grey between white and the background
    // on the way down — a LOOSE proximity check against an arbitrary mid-grey reference will always
    // find some blended edge pixel near it, tint bug or not. A near-EXACT match (tight tolerance)
    // over a MEANINGFUL fraction of the sampled pixels is what actually distinguishes "still solid-
    // filled with the old textMuted tint" from ordinary antialiasing.
    int nearWhiteCount = 0;
    int exactMutedCount = 0;
    int opaqueSamples = 0;
    for (int y = bounds.getY(); y < bounds.getBottom(); ++y) {
        for (int x = bounds.getX(); x < bounds.getRight(); ++x) {
            if (!img.getBounds().contains(x, y))
                continue;
            const auto p = img.getPixelAt(x, y);
            if (p.getAlpha() < 200)
                continue; // skip transparent pixels (the header is opaque, so this rarely fires)
            ++opaqueSamples;
            if (p.getRed() > 200 && p.getGreen() > 200 && p.getBlue() > 200)
                ++nearWhiteCount; // textPrimary (0xffEAEEF3) is near-white
            if (std::abs((int)p.getRed() - (int)libraryMutedTint.getRed()) <= 3 &&
                std::abs((int)p.getGreen() - (int)libraryMutedTint.getGreen()) <= 3 &&
                std::abs((int)p.getBlue() - (int)libraryMutedTint.getBlue()) <= 3)
                ++exactMutedCount;
        }
    }
    ASSERT_GT(opaqueSamples, 0);
    EXPECT_GT(nearWhiteCount, 0) << "the glyph should render in the title's (near-white) colour";
    // A regression that dropped the replaceColour() call would leave the WHOLE glyph solid-filled
    // in textMuted, not one stray edge pixel — so even a generous few-percent allowance still fails
    // that case hard while tolerating antialiasing.
    EXPECT_LE(exactMutedCount, opaqueSamples / 10)
        << "the glyph must not still be (mostly) solid-filled in the library sidebar's muted grey";

    moduleComponent.setLookAndFeel(nullptr);
}
