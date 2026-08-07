#include "../Source/AI/AIStateMapper.h"
#include "../Source/Modules/OscillatorModule.h"
#include "../Source/Modules/SamplerModule.h"
#include "../Source/Modules/VCAModule.h"
#include "../Source/Modules/WavetableOscillatorModule.h"
#include "../Source/UI/GraphEditor.h"
#include "../Source/UI/LayoutUtil.h"
#include "../Source/UI/ModuleComponent.h"
#include "../Source/UI/ModuleLibraryComponent.h"
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
    EXPECT_EQ(moduleComponent.getHeight(), 657)
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

    // Table choice -> 1 combo; Position/Octave/Coarse/Fine/Level/Unison/Detune -> 7 sliders;
    // Poly -> 1 toggle, plus the always-present "Show Scope" toggle.
    EXPECT_EQ(comboCount, 1);
    EXPECT_EQ(sliderCount, 7);
    EXPECT_EQ(toggleCount, 2);

    // Both bespoke children must be laid out inside the card.
    EXPECT_FALSE(display->getBounds().isEmpty());
    EXPECT_FALSE(loadButton->getBounds().isEmpty());
    EXPECT_TRUE(moduleComponent.getLocalBounds().contains(display->getBounds()));
    EXPECT_TRUE(moduleComponent.getLocalBounds().contains(loadButton->getBounds()));

    // Height is deliberately not asserted here: EstimatedModuleSizesMatchTheRealComponents
    // already pins the real card against GraphEditor::estimateModuleSize for every offered
    // type, so duplicating the number would just be a second thing to update by hand.
    EXPECT_EQ(moduleComponent.getWidth(), synth::LayoutUtil::kSingleWidth);
    EXPECT_GT(moduleComponent.getHeight(), 100);
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
