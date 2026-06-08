#include "../Source/Modules/ADSRModule.h"
#include "../Source/Modules/AttenuverterModule.h"
#include "../Source/Modules/FilterModule.h"
#include "../Source/Modules/LFOModule.h"
#include "../Source/Modules/MidiKeyboardModule.h"
#include "../Source/Modules/ModuleBase.h"
#include "../Source/Modules/OscillatorModule.h"
#include "../Source/Modules/SequencerModule.h"
#include "../Source/PresetManager.h"
#include "../Source/UI/LayoutUtil.h"
#include <gtest/gtest.h>

TEST(PresetManagerTest, ListPresets) {
    auto names = gsynth::PresetManager::getPresetNames();
    ASSERT_EQ(names.size(), 7);
    EXPECT_EQ(names[0], "Default");
    EXPECT_EQ(names[1], "Simple Lead");
}

TEST(PresetManagerTest, LoadAllPresets) {
    juce::AudioProcessorGraph graph;
    for (int i = 0; i < gsynth::PresetManager::getPresetNames().size(); ++i) {
        graph.clear();
        EXPECT_TRUE(gsynth::PresetManager::loadPreset(i, graph)) << "Failed to load preset " << i;
        EXPECT_GT(graph.getNumNodes(), 0) << "Preset " << i << " loaded with no nodes";
    }
}

TEST(PresetManagerTest, LoadDefaultPreset) {
    juce::AudioProcessorGraph graph;
    EXPECT_TRUE(gsynth::PresetManager::loadDefaultPreset(graph));
    EXPECT_GT(graph.getNumNodes(), 0);
}

TEST(PresetManagerTest, InvalidPresetIndexReturnsFailure) {
    juce::AudioProcessorGraph graph;
    EXPECT_FALSE(gsynth::PresetManager::loadPreset(-1, graph));
    EXPECT_FALSE(gsynth::PresetManager::loadPreset(100, graph));
}

TEST(PresetManagerTest, PresetCategoriesAreValid) {
    auto presets = gsynth::PresetManager::getPresetList();
    auto categories = gsynth::PresetManager::getCategories();
    ASSERT_GT(presets.size(), 0);
    ASSERT_GT(categories.size(), 0);
    for (const auto& preset : presets) {
        EXPECT_TRUE(categories.contains(preset.category))
            << "Preset '" << preset.name.toStdString() << "' has unknown category '" << preset.category.toStdString()
            << "'";
    }
}

TEST(PresetManagerTest, DefaultPresetHasExpectedNodes) {
    juce::AudioProcessorGraph graph;
    gsynth::PresetManager::loadDefaultPreset(graph);
    // Default preset should have at minimum: Audio I/O + Oscillator + Filter + VCA
    EXPECT_GE(graph.getNumNodes(), 5);
}

TEST(PresetManagerTest, AllPresetsHaveAudioOutput) {
    juce::AudioProcessorGraph graph;
    for (int i = 0; i < gsynth::PresetManager::getPresetNames().size(); ++i) {
        graph.clear();
        gsynth::PresetManager::loadPreset(i, graph);
        bool hasOutput = false;
        for (auto* node : graph.getNodes()) {
            if (node->getProcessor()->getName() == "Audio Output")
                hasOutput = true;
        }
        EXPECT_TRUE(hasOutput) << "Preset " << i << " missing Audio Output";
    }
}

TEST(PresetManagerTest, AllPresetsHaveConnections) {
    juce::AudioProcessorGraph graph;
    for (int i = 0; i < gsynth::PresetManager::getPresetNames().size(); ++i) {
        graph.clear();
        gsynth::PresetManager::loadPreset(i, graph);
        EXPECT_GT(graph.getConnections().size(), 0) << "Preset " << i << " has no connections";
    }
}

TEST(PresetManagerTest, PresetNamesMatchPresetList) {
    auto names = gsynth::PresetManager::getPresetNames();
    auto presets = gsynth::PresetManager::getPresetList();
    ASSERT_EQ(names.size(), presets.size());
    for (int i = 0; i < names.size(); ++i) {
        EXPECT_EQ(names[i], presets[i].name);
    }
}

/**
 * AllFactoryPresetsLoadWithoutOverlap
 *
 * Loads each of the 7 factory presets headlessly, builds bounding boxes from
 * the node positions using the same estimateModuleSize table that GraphEditor
 * uses at drop time, and asserts zero pairwise intersections at kCollisionGap=12.
 *
 * This test validates the re-baked preset positions from Section 6 of the
 * grid-layout design (PresetManager.cpp).
 */
namespace {

// Mirror the estimateModuleSize table from GraphEditor.cpp (Section 4 of design).
// Footprints in canvas pixels (w, h).
juce::Point<int> estimateModuleSize(const juce::String& typeName) {
    // Match by processor getName() strings
    if (typeName.containsIgnoreCase("Sequencer") && !typeName.containsIgnoreCase("Poly"))
        return {510, 380};
    if (typeName.containsIgnoreCase("MidiKeyboard") || typeName.containsIgnoreCase("Midi Keyboard") ||
        typeName.containsIgnoreCase("MIDI Keyboard"))
        return {500, 150};
    if (typeName.containsIgnoreCase("ADSR") || typeName.containsIgnoreCase("Amp Env") ||
        typeName.containsIgnoreCase("Filter Env"))
        return {280, 180};
    if (typeName.containsIgnoreCase("Attenuverter"))
        return {280, 120};
    // Everything else: conservative default
    return {280, 300};
}

} // namespace

TEST(PresetManagerTest, AllFactoryPresetsLoadWithoutOverlap) {
    const int kNumPresets = 7;
    const int kGap = gsynth::LayoutUtil::kCollisionGap;

    for (int presetIdx = 0; presetIdx < kNumPresets; ++presetIdx) {
        juce::AudioProcessorGraph graph;
        ASSERT_TRUE(gsynth::PresetManager::loadPreset(presetIdx, graph)) << "Failed to load preset " << presetIdx;

        // Build bounding boxes for each module node
        struct Box {
            juce::AudioProcessorGraph::NodeID id;
            juce::Rectangle<int> rect;
            juce::String name;
        };
        std::vector<Box> boxes;

        for (auto* node : graph.getNodes()) {
            auto* proc = node->getProcessor();
            if (!proc)
                continue;

            int x = static_cast<int>(node->properties.getWithDefault("x", 0));
            int y = static_cast<int>(node->properties.getWithDefault("y", 0));
            auto size = estimateModuleSize(proc->getName());
            boxes.push_back({node->nodeID, {x, y, size.x, size.y}, proc->getName()});
        }

        // Check every pair for overlap (using gap-inflated bounding boxes)
        bool anyOverlap = false;
        for (size_t i = 0; i < boxes.size(); ++i) {
            for (size_t j = i + 1; j < boxes.size(); ++j) {
                // The gap is enforced between outer edges: A.right + gap <= B.left etc.
                // Use expanded() by kGap/2 on each side — equivalent to checking gap between edges.
                auto ri = boxes[i].rect.expanded(kGap / 2);
                auto rj = boxes[j].rect.expanded(kGap / 2);
                if (ri.intersects(rj)) {
                    anyOverlap = true;
                    ADD_FAILURE() << "Preset " << presetIdx << ": modules '" << boxes[i].name << "' at ("
                                  << boxes[i].rect.getX() << "," << boxes[i].rect.getY() << " "
                                  << boxes[i].rect.getWidth() << "x" << boxes[i].rect.getHeight() << ") and '"
                                  << boxes[j].name << "' at (" << boxes[j].rect.getX() << "," << boxes[j].rect.getY()
                                  << " " << boxes[j].rect.getWidth() << "x" << boxes[j].rect.getHeight()
                                  << ") overlap (gap=" << kGap << ")";
                }
            }
        }
        EXPECT_FALSE(anyOverlap) << "Preset " << presetIdx << " has overlapping modules";
    }
}

TEST(PresetManagerTest, PolyPad_NoEnvToOscLevelConnection) {
    juce::AudioProcessorGraph graph;
    ASSERT_TRUE(gsynth::PresetManager::loadPreset(6, graph)) << "Failed to load Poly Pad preset (index 6)";

    // Find the ADSR node named "Amp Env" and the Oscillator node
    juce::AudioProcessorGraph::Node* adsrNode = nullptr;
    juce::AudioProcessorGraph::Node* oscNode = nullptr;
    int attenuverterCount = 0;

    for (auto* node : graph.getNodes()) {
        if (node->getProcessor()->getName() == "Amp Env")
            adsrNode = node;
        else if (auto* osc = dynamic_cast<OscillatorModule*>(node->getProcessor()))
            oscNode = node;
        if (dynamic_cast<AttenuverterModule*>(node->getProcessor()) != nullptr)
            ++attenuverterCount;
    }

    ASSERT_NE(adsrNode, nullptr) << "Poly Pad should have an 'Amp Env' node";
    ASSERT_NE(oscNode, nullptr) << "Poly Pad should have an Oscillator node";

    // Assert the oscillator is in poly mode
    // OscillatorModule parameter indices: 0=bypassed, 1=waveform, 2=octave, 3=coarse, 4=fine,
    //   5=level, 6=poly, 7=unison, 8=detune, 9=muted
    auto* osc = dynamic_cast<OscillatorModule*>(oscNode->getProcessor());
    ASSERT_NE(osc, nullptr);
    auto* polyParam = dynamic_cast<juce::AudioParameterBool*>(osc->getParameters()[6]);
    ASSERT_NE(polyParam, nullptr);
    EXPECT_TRUE(polyParam->get()) << "Oscillator in Poly Pad should have poly=true";

    // Assert there is NO connection from (Amp Env, ch 0) to (Oscillator, ch 12).
    // The oscillator's Level CV (poly channel 12) is shared across all voices, so
    // routing Amp Env voice-0 into it couples all voices to voice 0's envelope.
    // The capability still works (see IntegrationTests), but we no longer bake
    // this particular wire into the factory preset.
    bool foundEnvToOscLevelConn = false;
    for (auto& conn : graph.getConnections()) {
        if (conn.source.nodeID == adsrNode->nodeID && conn.source.channelIndex == 0 &&
            conn.destination.nodeID == oscNode->nodeID && conn.destination.channelIndex == 12) {
            foundEnvToOscLevelConn = true;
            break;
        }
    }
    EXPECT_FALSE(foundEnvToOscLevelConn)
        << "Poly Pad must NOT have a direct connection from Amp Env (ch 0) to Oscillator (ch 12 = Level CV): "
           "that wire couples all voices to voice-0's envelope via a shared-channel";

    // Assert zero attenuverter nodes
    EXPECT_EQ(attenuverterCount, 0) << "Poly Pad should have 0 attenuverter nodes";
}
