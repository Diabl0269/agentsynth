#include "../Source/Modules/ADSRModule.h"
#include "../Source/Modules/AttenuverterModule.h"
#include "../Source/Modules/OscillatorModule.h"
#include "../Source/PresetManager.h"
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
