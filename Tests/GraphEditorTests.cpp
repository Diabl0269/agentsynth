#include "../Source/AI/AIStateMapper.h"
#include "../Source/AppUndoManager.h"
#include "../Source/Modules/ADSRModule.h"
#include "../Source/Modules/AttenuverterModule.h"
#include "../Source/Modules/FX/ChorusModule.h"
#include "../Source/Modules/FX/DelayModule.h"
#include "../Source/Modules/FX/DistortionModule.h"
#include "../Source/Modules/FX/ReverbModule.h"
#include "../Source/Modules/FX/RingModulatorModule.h"
#include "../Source/Modules/FilterModule.h"
#include "../Source/Modules/LFOModule.h"
#include "../Source/Modules/MathModule.h"
#include "../Source/Modules/MidiKeyboardModule.h"
#include "../Source/Modules/ModuleBase.h"
#include "../Source/Modules/OscillatorModule.h"
#include "../Source/Modules/PolyMidiModule.h"
#include "../Source/Modules/SamplerModule.h"
#include "../Source/Modules/SequencerModule.h"
#include "../Source/Modules/VCAModule.h"
#include "../Source/Modules/WavetableOscillatorModule.h"
#include "../Source/PresetManager.h"
#include "../Source/UI/GraphEditor.h"
#include "../Source/UI/LayoutUtil.h"
#include "../Source/UI/ModuleComponent.h"
#include "../Source/UI/Theme/BuiltInThemes.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

// Define a dummy component to act as the drag source
class DummyDragSource : public juce::Component {};

class GraphEditorTest : public ::testing::Test {};

// Helper to find and set a bool parameter by ID (mirrors LogicalPortTests.cpp's setPolyParam).
static void setPolyParam(juce::AudioProcessor& proc, bool value) {
    for (auto* param : proc.getParameters()) {
        if (auto* p = dynamic_cast<juce::AudioProcessorParameterWithID*>(param)) {
            if (p->paramID == "poly") {
                p->setValueNotifyingHost(value ? 1.0f : 0.0f);
                return;
            }
        }
    }
}

static void setDualIOParam(juce::AudioProcessor& proc, bool value) {
    for (auto* param : proc.getParameters()) {
        if (auto* p = dynamic_cast<juce::AudioProcessorParameterWithID*>(param)) {
            if (p->paramID == "dualIO") {
                p->setValueNotifyingHost(value ? 1.0f : 0.0f);
                return;
            }
        }
    }
}

TEST_F(GraphEditorTest, InitializationAndResizing) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    EXPECT_FALSE(editor.isModMatrixVisible());
    EXPECT_NO_THROW(editor.resized());
}
TEST_F(GraphEditorTest, ToggleModMatrixVisibility) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    EXPECT_FALSE(editor.isModMatrixVisible());
    editor.toggleModMatrixVisibility();
    EXPECT_TRUE(editor.isModMatrixVisible());
    editor.toggleModMatrixVisibility();
    EXPECT_FALSE(editor.isModMatrixVisible());
}

TEST_F(GraphEditorTest, DropModuleCreatesNode) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    DummyDragSource dummySource;
    juce::var description("Oscillator");
    juce::DragAndDropTarget::SourceDetails details(description, &dummySource, juce::Point<int>(100, 100));

    EXPECT_TRUE(editor.isInterestedInDragSource(details));

    auto initialNodeCount = engine.getGraph().getNodes().size();
    editor.itemDropped(details);

    EXPECT_EQ(engine.getGraph().getNodes().size(), initialNodeCount + 1);

    bool foundOsc = false;
    for (auto* node : engine.getGraph().getNodes()) {
        if (node->getProcessor()->getName() == "Oscillator") {
            foundOsc = true;
            // Drop position is now snapped to the layout grid (anti-overlap may also offset it).
            EXPECT_EQ(static_cast<int>(node->properties.getWithDefault("x", -1)) % synth::LayoutUtil::kGridSize, 0)
                << "Dropped module x should snap to grid";
            EXPECT_EQ(static_cast<int>(node->properties.getWithDefault("y", -1)) % synth::LayoutUtil::kGridSize, 0)
                << "Dropped module y should snap to grid";
            break;
        }
    }
    EXPECT_TRUE(foundOsc);
}

TEST_F(GraphEditorTest, NewDualIOModuleHonoursDefaultPreference) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);
    editor.setDefaultDualIOForNewModules(true);

    DummyDragSource dummySource;
    juce::var description("Delay");
    juce::DragAndDropTarget::SourceDetails details(description, &dummySource, juce::Point<int>(100, 100));
    editor.itemDropped(details);

    ModuleBase* delay = nullptr;
    for (auto* node : engine.getGraph().getNodes()) {
        if (node->getProcessor()->getName() == "Delay") {
            delay = dynamic_cast<ModuleBase*>(node->getProcessor());
            break;
        }
    }

    ASSERT_NE(delay, nullptr);
    ASSERT_TRUE(delay->hasDualIOParameter());
    EXPECT_TRUE(delay->isDualIO());
}

// The preference is what the user sets to say "I want split jacks on everything I make". Before
// #219 it only reached FX, and it could only ever force Dual I/O *on* — so a module whose own
// default is dual could not be made single from Preferences at all.
TEST_F(GraphEditorTest, DefaultDualIOPreferenceReachesVoiceModulesInBothDirections) {
    auto dropAndFind = [](bool preferDual, const juce::String& type) {
        auto engine = std::make_unique<AudioEngine>();
        GraphEditor editor(*engine);
        editor.setSize(800, 600);
        editor.setDefaultDualIOForNewModules(preferDual);

        DummyDragSource dummySource;
        juce::DragAndDropTarget::SourceDetails details(juce::var(type), &dummySource, juce::Point<int>(100, 100));
        editor.itemDropped(details);

        bool dual = false;
        bool found = false;
        for (auto* node : engine->getGraph().getNodes()) {
            if (node->getProcessor()->getName() == type) {
                auto* mb = dynamic_cast<ModuleBase*>(node->getProcessor());
                found = mb != nullptr && mb->hasDualIOParameter();
                dual = mb != nullptr && mb->isDualIO();
            }
        }
        EXPECT_TRUE(found) << type << " should expose a Dual I/O parameter";
        return dual;
    };

    for (const juce::String& type : {"Oscillator", "Filter", "VCA", "Wavetable", "Sampler", "Delay"}) {
        EXPECT_TRUE(dropAndFind(true, type)) << type << " ignored the split-jacks preference";
        EXPECT_FALSE(dropAndFind(false, type)) << type << " ignored the single-jack preference";
    }
}

// An invisible jack cannot be unplugged: collapsing a split-block module has to take its right-leg
// cables with it, or they keep sounding with no way to reach them.
TEST_F(GraphEditorTest, CollapsingASplitBlockModuleDropsItsHiddenRightLegWires) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1000, 600);

    auto& graph = engine.getGraph();
    auto oscNode = graph.addNode(std::make_unique<OscillatorModule>());
    auto filterNode = graph.addNode(std::make_unique<FilterModule>());
    ASSERT_NE(oscNode, nullptr);
    ASSERT_NE(filterNode, nullptr);
    editor.updateComponents();

    const int oscR = OscillatorModule::kRightBase;
    const int filterR = FilterModule::kRightBase;
    graph.addConnection({{oscNode->nodeID, 0}, {filterNode->nodeID, 0}});
    graph.addConnection({{oscNode->nodeID, oscR}, {filterNode->nodeID, filterR}});
    ASSERT_TRUE(graph.isConnected({{oscNode->nodeID, oscR}, {filterNode->nodeID, filterR}}));

    // Collapse the Filter.
    ModuleComponent* filterComp = nullptr;
    for (auto* mc : editor.getModuleComponents())
        if (mc != nullptr && mc->getNodeId() == filterNode->nodeID)
            filterComp = mc;
    ASSERT_NE(filterComp, nullptr);

    if (auto* dual = findParameterByID(filterNode->getProcessor(), "dualIO"))
        dual->setValueNotifyingHost(0.0f);
    // The same call ModuleComponent::applyDualIOLayoutChange makes when the header toggle flips.
    editor.completeStereoPairConnections(filterComp);

    EXPECT_FALSE(graph.isConnected({{oscNode->nodeID, oscR}, {filterNode->nodeID, filterR}}))
        << "the wire into the now-hidden Audio R jack must be dropped";
    EXPECT_TRUE(graph.isConnected({{oscNode->nodeID, 0}, {filterNode->nodeID, 0}})) << "the left leg is untouched";
}

// --- Audio-file drop on the canvas ------------------------------------------------------------
// Dropping a sample on empty canvas should build a Sampler already holding it, so the user never has
// to open the file chooser.

TEST_F(GraphEditorTest, AudioFileDroppedOnCanvasCreatesAPreloadedSampler) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    auto file = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("canvas-drop-146.wav");
    file.deleteFile();
    {
        juce::AudioBuffer<float> audio(1, 512);
        audio.clear();
        for (int i = 0; i < 512; ++i)
            audio.setSample(0, i, 0.3f);

        juce::WavAudioFormat wavFormat;
        std::unique_ptr<juce::FileOutputStream> stream(file.createOutputStream());
        ASSERT_NE(stream, nullptr);
        std::unique_ptr<juce::AudioFormatWriter> writer(wavFormat.createWriterFor(stream.get(), 44100.0, 1, 32, {}, 0));
        ASSERT_NE(writer, nullptr);
        stream.release();
        writer->writeFromAudioSampleBuffer(audio, 0, 512);
    }

    juce::StringArray files{file.getFullPathName()};
    EXPECT_TRUE(editor.isInterestedInFileDrag(files));

    const auto before = engine.getGraph().getNodes().size();
    editor.filesDropped(files, 200, 200);
    ASSERT_EQ(engine.getGraph().getNodes().size(), before + 1);

    SamplerModule* created = nullptr;
    for (auto* node : engine.getGraph().getNodes())
        if (auto* sampler = dynamic_cast<SamplerModule*>(node->getProcessor()))
            created = sampler;

    ASSERT_NE(created, nullptr) << "the drop should create a Sampler";
    EXPECT_EQ(created->getSampleFilePath(), file.getFullPathName()) << "and it should already hold the file";

    file.deleteFile();
}

TEST_F(GraphEditorTest, NonAudioFileDragIsRejectedByTheCanvas) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    juce::StringArray files{"/tmp/preset.json", "/tmp/notes.txt"};
    EXPECT_FALSE(editor.isInterestedInFileDrag(files));

    const auto before = engine.getGraph().getNodes().size();
    editor.filesDropped(files, 200, 200);
    EXPECT_EQ(engine.getGraph().getNodes().size(), before) << "a non-audio drop must not create nodes";
}

// Serum-style modulation drop: releasing a cable on a KNOB wires the source to that parameter's
// CV jack. On a module with 16 CV inputs, aiming at the gutter is the slow path; this is the one
// people actually use.
TEST_F(GraphEditorTest, DroppingACableOnAKnobCreatesAModRouting) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1200, 900);

    auto lfoNode = engine.getGraph().addNode(std::make_unique<LFOModule>());
    auto wtNode = engine.getGraph().addNode(std::make_unique<WavetableOscillatorModule>());
    editor.updateComponents();

    ModuleComponent* lfoComp = nullptr;
    ModuleComponent* wtComp = nullptr;
    if (auto* content = editor.getChildComponent(0))
        for (auto* child : content->getChildren())
            if (auto* mod = dynamic_cast<ModuleComponent*>(child)) {
                if (mod->getModule() == lfoNode->getProcessor())
                    lfoComp = mod;
                if (mod->getModule() == wtNode->getProcessor())
                    wtComp = mod;
            }
    ASSERT_NE(lfoComp, nullptr);
    ASSERT_NE(wtComp, nullptr);

    lfoComp->setTopLeftPosition(0, 0);
    wtComp->setTopLeftPosition(400, 0);

    // "Position" is pinned above the tab strip, so it is on screen whichever page is showing.
    juce::Slider* position = nullptr;
    for (auto* child : wtComp->getChildren())
        if (auto* s = dynamic_cast<juce::Slider*>(child))
            if (s->getComponentID() == "Position")
                position = s;
    ASSERT_NE(position, nullptr);

    const auto knobPoint = wtComp->getBounds().getPosition() + position->getBounds().getCentre();

    editor.beginConnectionDrag(lfoComp, 0, /*isInput*/ false, /*isMidi*/ false, {0, 0});
    editor.dragConnection(knobPoint);

    // Hovering a knob arms it as the drop target, so the user can see where the cable will land.
    EXPECT_EQ(wtComp->getModDropTargetChannel(), WavetableOscillatorModule::kJackPosition);

    editor.endConnectionDrag(knobPoint);

    EXPECT_EQ(wtComp->getModDropTargetChannel(), -1) << "the highlight must clear once the cable lands";

    bool routed = false;
    for (const auto& r : engine.getModulationRoutings())
        if (r.sourceNodeID == lfoNode->nodeID && r.destNodeID == wtNode->nodeID &&
            r.destChannelIndex == WavetableOscillatorModule::kJackPosition)
            routed = true;
    EXPECT_TRUE(routed) << "dropping on the Position knob must modulate Position";
}

// A knob only accepts a cable coming FROM an output — a mod source drives a destination, and
// dragging out of an input and releasing on a knob would otherwise wire it backwards.
TEST_F(GraphEditorTest, KnobDropIsIgnoredForACableDraggedFromAnInput) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1200, 900);

    auto lfoNode = engine.getGraph().addNode(std::make_unique<LFOModule>());
    auto wtNode = engine.getGraph().addNode(std::make_unique<WavetableOscillatorModule>());
    editor.updateComponents();

    ModuleComponent* lfoComp = nullptr;
    ModuleComponent* wtComp = nullptr;
    if (auto* content = editor.getChildComponent(0))
        for (auto* child : content->getChildren())
            if (auto* mod = dynamic_cast<ModuleComponent*>(child)) {
                if (mod->getModule() == lfoNode->getProcessor())
                    lfoComp = mod;
                if (mod->getModule() == wtNode->getProcessor())
                    wtComp = mod;
            }
    ASSERT_NE(lfoComp, nullptr);
    ASSERT_NE(wtComp, nullptr);

    lfoComp->setTopLeftPosition(0, 0);
    wtComp->setTopLeftPosition(400, 0);

    juce::Slider* position = nullptr;
    for (auto* child : wtComp->getChildren())
        if (auto* s = dynamic_cast<juce::Slider*>(child))
            if (s->getComponentID() == "Position")
                position = s;
    ASSERT_NE(position, nullptr);

    const auto knobPoint = wtComp->getBounds().getPosition() + position->getBounds().getCentre();
    const auto before = engine.getModulationRoutings().size();

    editor.beginConnectionDrag(lfoComp, 0, /*isInput*/ true, /*isMidi*/ false, {0, 0});
    editor.dragConnection(knobPoint);
    EXPECT_EQ(wtComp->getModDropTargetChannel(), -1) << "an input-sourced drag must not arm a knob";

    editor.endConnectionDrag(knobPoint);
    EXPECT_EQ(engine.getModulationRoutings().size(), before);
}

TEST_F(GraphEditorTest, DragConnectionCreatesLink) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    auto oscNode = engine.getGraph().addNode(std::make_unique<OscillatorModule>());
    auto filterNode = engine.getGraph().addNode(std::make_unique<FilterModule>());

    editor.updateComponents();

    ModuleComponent* oscComp = nullptr;
    ModuleComponent* filterComp = nullptr;

    // Find modules via content component (first child of GraphEditor)
    auto* content = editor.getChildComponent(0);
    if (content) {
        for (auto* contentChild : content->getChildren()) {
            if (auto* mod = dynamic_cast<ModuleComponent*>(contentChild)) {
                if (mod->getModule() == oscNode->getProcessor())
                    oscComp = mod;
                if (mod->getModule() == filterNode->getProcessor())
                    filterComp = mod;
            }
        }
    }

    ASSERT_NE(oscComp, nullptr);
    ASSERT_NE(filterComp, nullptr);

    oscComp->setBounds(0, 0, 100, 100);
    filterComp->setBounds(200, 0, 100, 100);

    editor.beginConnectionDrag(oscComp, 0, false, false, juce::Point<int>(0, 0));
    editor.dragConnection(juce::Point<int>(50, 0));

    auto filterTargetPoint = filterComp->getBounds().getPosition() + filterComp->getPortCenter(0, true);
    editor.endConnectionDrag(filterTargetPoint);

    bool connectionFound = false;
    for (auto& conn : engine.getGraph().getConnections()) {
        if (conn.source.nodeID == oscNode->nodeID && conn.destination.nodeID == filterNode->nodeID) {
            connectionFound = true;
            break;
        }
    }

    EXPECT_TRUE(connectionFound);
}

TEST_F(GraphEditorTest, ReplaceModulePreservesPosition) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    auto& graph = engine.getGraph();
    auto oscNode = graph.addNode(std::make_unique<OscillatorModule>());
    oscNode->properties.set("x", 200);
    oscNode->properties.set("y", 300);
    editor.updateComponents();

    // Find the ModuleComponent for the oscillator
    ModuleComponent* oscComp = nullptr;
    auto* content = editor.getChildComponent(0);
    if (content) {
        for (auto* child : content->getChildren()) {
            if (auto* mod = dynamic_cast<ModuleComponent*>(child)) {
                if (mod->getModule() == oscNode->getProcessor())
                    oscComp = mod;
            }
        }
    }
    ASSERT_NE(oscComp, nullptr);

    editor.replaceModule(oscComp, "Filter");

    // Verify: oscillator is gone, filter exists at same position
    bool foundOsc = false, foundFilter = false;
    int filterX = 0, filterY = 0;
    for (auto* node : graph.getNodes()) {
        if (dynamic_cast<OscillatorModule*>(node->getProcessor()))
            foundOsc = true;
        if (dynamic_cast<FilterModule*>(node->getProcessor())) {
            foundFilter = true;
            filterX = node->properties.getWithDefault("x", 0);
            filterY = node->properties.getWithDefault("y", 0);
        }
    }
    EXPECT_FALSE(foundOsc);
    EXPECT_TRUE(foundFilter);
    EXPECT_EQ(filterX, 200);
    EXPECT_EQ(filterY, 300);
}

TEST_F(GraphEditorTest, ReplaceModulePreservesAudioConnections) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    auto& graph = engine.getGraph();
    auto oscNode = graph.addNode(std::make_unique<OscillatorModule>());
    auto filterNode = graph.addNode(std::make_unique<FilterModule>());

    // Connect oscillator output 0 -> filter input 0
    graph.addConnection({{oscNode->nodeID, 0}, {filterNode->nodeID, 0}});
    editor.updateComponents();

    // Find the filter ModuleComponent
    ModuleComponent* filterComp = nullptr;
    auto* content = editor.getChildComponent(0);
    if (content) {
        for (auto* child : content->getChildren()) {
            if (auto* mod = dynamic_cast<ModuleComponent*>(child)) {
                if (mod->getModule() == filterNode->getProcessor())
                    filterComp = mod;
            }
        }
    }
    ASSERT_NE(filterComp, nullptr);

    // Replace filter with VCA (both have input on channel 0)
    editor.replaceModule(filterComp, "VCA");

    // Find the new VCA node
    juce::AudioProcessorGraph::NodeID vcaNodeId;
    for (auto* node : graph.getNodes()) {
        if (dynamic_cast<VCAModule*>(node->getProcessor()))
            vcaNodeId = node->nodeID;
    }
    EXPECT_NE(vcaNodeId.uid, 0u);

    // Verify connection Osc -> VCA on channel 0
    bool connectionFound = false;
    for (auto& conn : graph.getConnections()) {
        if (conn.source.nodeID == oscNode->nodeID && conn.source.channelIndex == 0 &&
            conn.destination.nodeID == vcaNodeId && conn.destination.channelIndex == 0) {
            connectionFound = true;
            break;
        }
    }
    EXPECT_TRUE(connectionFound);
}

TEST_F(GraphEditorTest, ReplaceModuleDropsIncompatibleConnections) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    auto& graph = engine.getGraph();
    auto lfoNode = graph.addNode(std::make_unique<LFOModule>());
    auto oscNode = graph.addNode(std::make_unique<OscillatorModule>());

    // Connect LFO output 0 -> Oscillator input 13 (Oscillator has 14 inputs)
    graph.addConnection({{lfoNode->nodeID, 0}, {oscNode->nodeID, 13}});
    editor.updateComponents();

    // Find the oscillator ModuleComponent
    ModuleComponent* oscComp = nullptr;
    auto* content = editor.getChildComponent(0);
    if (content) {
        for (auto* child : content->getChildren()) {
            if (auto* mod = dynamic_cast<ModuleComponent*>(child)) {
                if (mod->getModule() == oscNode->getProcessor())
                    oscComp = mod;
            }
        }
    }
    ASSERT_NE(oscComp, nullptr);

    // Replace Oscillator (14 inputs) with LFO (1 input) — channel 13 is incompatible
    editor.replaceModule(oscComp, "LFO");

    // Find the new LFO node (replacement)
    juce::AudioProcessorGraph::NodeID newNodeId;
    for (auto* node : graph.getNodes()) {
        if (node->nodeID != lfoNode->nodeID && dynamic_cast<LFOModule*>(node->getProcessor()))
            newNodeId = node->nodeID;
    }
    EXPECT_NE(newNodeId.uid, 0u);

    // Verify NO connection from LFO to replacement (channel 13 doesn't exist on LFO)
    bool connectionFound = false;
    for (auto& conn : graph.getConnections()) {
        if (conn.source.nodeID == lfoNode->nodeID && conn.destination.nodeID == newNodeId) {
            connectionFound = true;
            break;
        }
    }
    EXPECT_FALSE(connectionFound);
}

TEST_F(GraphEditorTest, ReplaceModulePreservesMidiConnections) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    auto& graph = engine.getGraph();
    auto seqNode = graph.addNode(std::make_unique<SequencerModule>());
    auto oscNode = graph.addNode(std::make_unique<OscillatorModule>());

    // Connect Sequencer MIDI out -> Oscillator MIDI in
    graph.addConnection({{seqNode->nodeID, juce::AudioProcessorGraph::midiChannelIndex},
                         {oscNode->nodeID, juce::AudioProcessorGraph::midiChannelIndex}});
    editor.updateComponents();

    // Find the oscillator ModuleComponent
    ModuleComponent* oscComp = nullptr;
    auto* content = editor.getChildComponent(0);
    if (content) {
        for (auto* child : content->getChildren()) {
            if (auto* mod = dynamic_cast<ModuleComponent*>(child)) {
                if (mod->getModule() == oscNode->getProcessor())
                    oscComp = mod;
            }
        }
    }
    ASSERT_NE(oscComp, nullptr);

    // Replace Oscillator with ADSR (both accept MIDI)
    editor.replaceModule(oscComp, "ADSR");

    // Find the new ADSR node
    juce::AudioProcessorGraph::NodeID adsrNodeId;
    for (auto* node : graph.getNodes()) {
        if (dynamic_cast<ADSRModule*>(node->getProcessor()))
            adsrNodeId = node->nodeID;
    }
    EXPECT_NE(adsrNodeId.uid, 0u);

    // Verify MIDI connection Sequencer -> ADSR
    bool midiConnFound = false;
    for (auto& conn : graph.getConnections()) {
        if (conn.source.nodeID == seqNode->nodeID &&
            conn.source.channelIndex == juce::AudioProcessorGraph::midiChannelIndex &&
            conn.destination.nodeID == adsrNodeId &&
            conn.destination.channelIndex == juce::AudioProcessorGraph::midiChannelIndex) {
            midiConnFound = true;
            break;
        }
    }
    EXPECT_TRUE(midiConnFound);
}

// Inc-4: Verify that for the Poly Pad preset's PolyBus (ADSR->VCA) routing,
// sourceVisibleJack and destVisibleJack are within their module's visible port counts.
// This guarantees getPortCenter() lands on a real rendered jack, not a phantom y.
// Placed in AudioEngine-level fixture (no GUI needed) to keep the test headless.
TEST_F(GraphEditorTest, PolyBusWireResolvesToVisibleJacks) {
    AudioEngine engine;
    engine.initialise();
    engine.getGraph().clear();

    bool loaded = synth::PresetManager::loadPreset(6, engine.getGraph());
    ASSERT_TRUE(loaded) << "Poly Pad preset (index 6) must load successfully";

    auto routings = engine.getModulationRoutings();

    // Find the PolyBus routing
    const AudioEngine::ModulationRouting* polyRouting = nullptr;
    for (const auto& r : routings) {
        if (r.kind == AudioEngine::RoutingKind::PolyBus) {
            polyRouting = &r;
            break;
        }
    }
    ASSERT_NE(polyRouting, nullptr) << "Expected a PolyBus routing in the Poly Pad preset";

    // Locate source and dest processors to query their visible port counts.
    auto& graph = engine.getGraph();
    juce::AudioProcessor* srcProcessor = nullptr;
    juce::AudioProcessor* dstProcessor = nullptr;
    for (auto* node : graph.getNodes()) {
        if (node->nodeID == polyRouting->sourceNodeID)
            srcProcessor = node->getProcessor();
        if (node->nodeID == polyRouting->destNodeID)
            dstProcessor = node->getProcessor();
    }
    ASSERT_NE(srcProcessor, nullptr) << "Source node must exist in graph";
    ASSERT_NE(dstProcessor, nullptr) << "Dest node must exist in graph";

    // Get visible port counts — use ModuleBase if available, else fall back.
    int srcVisibleOuts = srcProcessor->getTotalNumOutputChannels();
    int dstVisibleIns = dstProcessor->getTotalNumInputChannels();
    if (auto* mb = dynamic_cast<ModuleBase*>(srcProcessor))
        srcVisibleOuts = mb->getVisibleOutputPortCount();
    if (auto* mb = dynamic_cast<ModuleBase*>(dstProcessor))
        dstVisibleIns = mb->getVisibleInputPortCount();

    // Key assertions: both visible jacks must be within the visible range,
    // so paint() / getPortCenter() will use a real jack (not a phantom y).
    EXPECT_LT(polyRouting->sourceVisibleJack, srcVisibleOuts)
        << "PolyBus sourceVisibleJack (" << polyRouting->sourceVisibleJack
        << ") must be < source visible output count (" << srcVisibleOuts << ")";
    EXPECT_GE(polyRouting->sourceVisibleJack, 0) << "PolyBus sourceVisibleJack must be non-negative";

    EXPECT_LT(polyRouting->destVisibleJack, dstVisibleIns)
        << "PolyBus destVisibleJack (" << polyRouting->destVisibleJack << ") must be < dest visible input count ("
        << dstVisibleIns << ")";
    EXPECT_GE(polyRouting->destVisibleJack, 0) << "PolyBus destVisibleJack must be non-negative";

    engine.shutdown();
}

TEST_F(GraphEditorTest, ReplaceModuleIsUndoable) {
    AudioEngine engine;
    AppUndoManager undoMgr;
    GraphEditor editor(engine, &undoMgr);
    editor.setSize(800, 600);

    auto& graph = engine.getGraph();
    auto oscNode = graph.addNode(std::make_unique<OscillatorModule>());
    oscNode->properties.set("x", 100);
    oscNode->properties.set("y", 200);
    auto oscNodeId = oscNode->nodeID;
    editor.updateComponents();

    // Find the oscillator ModuleComponent
    ModuleComponent* oscComp = nullptr;
    auto* content = editor.getChildComponent(0);
    if (content) {
        for (auto* child : content->getChildren()) {
            if (auto* mod = dynamic_cast<ModuleComponent*>(child)) {
                if (mod->getModule() == oscNode->getProcessor())
                    oscComp = mod;
            }
        }
    }
    ASSERT_NE(oscComp, nullptr);

    // Replace oscillator with filter
    editor.replaceModule(oscComp, "Filter");

    // Verify filter exists, oscillator gone
    bool hasFilter = false, hasOsc = false;
    for (auto* node : graph.getNodes()) {
        if (dynamic_cast<FilterModule*>(node->getProcessor()))
            hasFilter = true;
        if (dynamic_cast<OscillatorModule*>(node->getProcessor()))
            hasOsc = true;
    }
    EXPECT_TRUE(hasFilter);
    EXPECT_FALSE(hasOsc);

    // Undo
    EXPECT_TRUE(undoMgr.undo());

    // Verify oscillator is back, filter gone
    hasFilter = false;
    hasOsc = false;
    for (auto* node : graph.getNodes()) {
        if (dynamic_cast<FilterModule*>(node->getProcessor()))
            hasFilter = true;
        if (dynamic_cast<OscillatorModule*>(node->getProcessor()))
            hasOsc = true;
    }
    EXPECT_FALSE(hasFilter);
    EXPECT_TRUE(hasOsc);
}

// ============================================================================
// Grid-layout / anti-overlap tests
// ============================================================================

// DropSnapsPositionToGrid: after itemDropped at a non-grid coordinate, the node's
// persisted x,y must both be multiples of kGridSize=8.
TEST_F(GraphEditorTest, DropSnapsPositionToGrid) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    // Non-grid drop point: (103, 97) — neither is a multiple of 8
    DummyDragSource dummySource;
    juce::var description("Oscillator");
    juce::DragAndDropTarget::SourceDetails details(description, &dummySource, juce::Point<int>(103, 97));

    auto nodeBefore = engine.getGraph().getNodes().size();
    editor.itemDropped(details);
    ASSERT_EQ(engine.getGraph().getNodes().size(), nodeBefore + 1);

    // Find the newly added oscillator node
    juce::AudioProcessorGraph::Node* newNode = nullptr;
    for (auto* node : engine.getGraph().getNodes()) {
        if (dynamic_cast<OscillatorModule*>(node->getProcessor())) {
            newNode = node;
        }
    }
    ASSERT_NE(newNode, nullptr) << "Should find the dropped Oscillator node";

    int x = static_cast<int>(newNode->properties.getWithDefault("x", -1));
    int y = static_cast<int>(newNode->properties.getWithDefault("y", -1));

    EXPECT_GE(x, 0) << "Node x property must be set";
    EXPECT_GE(y, 0) << "Node y property must be set";
    EXPECT_EQ(x % synth::LayoutUtil::kGridSize, 0)
        << "Node x=" << x << " must be a multiple of kGridSize=" << synth::LayoutUtil::kGridSize;
    EXPECT_EQ(y % synth::LayoutUtil::kGridSize, 0)
        << "Node y=" << y << " must be a multiple of kGridSize=" << synth::LayoutUtil::kGridSize;
}

// DropOnOccupiedCellOffsetsToClearSlot: dropping two modules at the same position
// must result in non-overlapping bounding boxes (gap >= kCollisionGap).
TEST_F(GraphEditorTest, DropOnOccupiedCellOffsetsToClearSlot) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    // Drop first module
    DummyDragSource dummySource;
    {
        juce::var description("Oscillator");
        juce::DragAndDropTarget::SourceDetails details(description, &dummySource, juce::Point<int>(200, 200));
        editor.itemDropped(details);
    }

    // Drop second module at the same position
    {
        juce::var description("Filter");
        juce::DragAndDropTarget::SourceDetails details(description, &dummySource, juce::Point<int>(200, 200));
        editor.itemDropped(details);
    }

    // Gather positions and sizes from module components
    struct ModInfo {
        juce::Rectangle<int> bounds;
    };
    std::vector<ModInfo> mods;

    auto* content = editor.getChildComponent(0);
    ASSERT_NE(content, nullptr);
    for (auto* child : content->getChildren()) {
        if (auto* mc = dynamic_cast<ModuleComponent*>(child)) {
            mods.push_back({mc->getBoundsInParent()});
        }
    }

    ASSERT_GE(static_cast<int>(mods.size()), 2) << "Expected at least 2 module components after two drops";

    // Check every pair: bounding boxes must not intersect when inflated by kCollisionGap/2
    const int gap = synth::LayoutUtil::kCollisionGap;
    for (size_t i = 0; i < mods.size(); ++i) {
        for (size_t j = i + 1; j < mods.size(); ++j) {
            auto ri = mods[i].bounds.expanded(gap / 2);
            auto rj = mods[j].bounds.expanded(gap / 2);
            EXPECT_FALSE(ri.intersects(rj))
                << "Module " << i << " (" << mods[i].bounds.toString() << ") and module " << j << " ("
                << mods[j].bounds.toString() << ") overlap after anti-overlap resolution";
        }
    }
}

// DragPreviewGhostTracksResolvedPlacement: beginDragPreview / updateDragPreview set a ghost
// equal to resolvePlacement and the ghost does NOT intersect an existing module.
// endDragPreview() resets the active flag.
TEST_F(GraphEditorTest, DragPreviewGhostTracksResolvedPlacement) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    // Place a module at a known canvas position so the ghost has something to avoid.
    auto& graph = engine.getGraph();
    auto oscNode = graph.addNode(std::make_unique<OscillatorModule>());
    oscNode->properties.set("x", 100);
    oscNode->properties.set("y", 100);
    editor.updateComponents();

    // Find and size the module component so its bounds are valid for collision checks.
    auto* content = editor.getChildComponent(0);
    ASSERT_NE(content, nullptr);
    for (auto* child : content->getChildren()) {
        if (auto* mc = dynamic_cast<ModuleComponent*>(child)) {
            if (mc->getModule() == oscNode->getProcessor())
                mc->setSize(280, 300);
        }
    }

    // Start a drag preview for a new (library) module: selfId = {} (no existing node)
    EXPECT_FALSE(editor.isDragPreviewActive());
    editor.beginDragPreview(280, 300, juce::AudioProcessorGraph::NodeID{});
    EXPECT_TRUE(editor.isDragPreviewActive());

    // Desired position OVERLAPS the existing oscillator (same coordinates).
    juce::Point<int> desiredOverlap(100, 100);
    editor.updateDragPreview(desiredOverlap);

    auto ghost = editor.getDragPreviewGhost();
    EXPECT_FALSE(ghost.isEmpty()) << "Ghost rect should be non-empty after updateDragPreview";

    // The ghost must equal what resolvePlacement returns for the same inputs.
    auto expected = editor.resolvePlacement(desiredOverlap, 280, 300, juce::AudioProcessorGraph::NodeID{});
    EXPECT_EQ(ghost.getTopLeft(), expected) << "Ghost top-left must equal resolvePlacement result; got "
                                            << ghost.getTopLeft().toString() << " but expected " << expected.toString();

    // The ghost must NOT intersect the existing module's bounds (collision was resolved).
    juce::Rectangle<int> oscBounds(100, 100, 280, 300);
    const int gap = synth::LayoutUtil::kCollisionGap;
    EXPECT_FALSE(ghost.expanded(gap / 2).intersects(oscBounds.expanded(gap / 2)))
        << "Ghost rect (" << ghost.toString() << ") must not overlap existing module (" << oscBounds.toString()
        << ") after anti-overlap resolution";

    // endDragPreview clears the active flag.
    editor.endDragPreview();
    EXPECT_FALSE(editor.isDragPreviewActive());
    EXPECT_TRUE(editor.getDragPreviewGhost().isEmpty());
}

// DropUsesRealModuleSizeForAntiOverlap: drop two tall Oscillator modules at the same canvas
// point. With the old 300px estimate both would land on the same slot because the estimate
// was too short to detect overlap; with real-size finalize they must not overlap.
TEST_F(GraphEditorTest, DropUsesRealModuleSizeForAntiOverlap) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1200, 900);

    DummyDragSource dummySource;

    // Drop two Oscillators at the same position. The second must be displaced because the
    // first occupies that slot (real Oscillator height ~530px far exceeds old 300px estimate).
    {
        juce::var description("Oscillator");
        juce::DragAndDropTarget::SourceDetails details(description, &dummySource, juce::Point<int>(200, 200));
        editor.itemDropped(details);
    }
    {
        juce::var description("Oscillator");
        juce::DragAndDropTarget::SourceDetails details(description, &dummySource, juce::Point<int>(200, 200));
        editor.itemDropped(details);
    }

    // Collect all ModuleComponent bounds from the content component.
    auto* content = editor.getChildComponent(0);
    ASSERT_NE(content, nullptr);

    std::vector<juce::Rectangle<int>> bounds;
    for (auto* child : content->getChildren()) {
        if (auto* mc = dynamic_cast<ModuleComponent*>(child)) {
            auto b = mc->getBoundsInParent();
            if (b.getWidth() > 0 && b.getHeight() > 0)
                bounds.push_back(b);
        }
    }

    ASSERT_GE(static_cast<int>(bounds.size()), 2) << "Expected at least 2 module components after two drops";

    // All pairs must be non-overlapping (with collision gap).
    const int gap = synth::LayoutUtil::kCollisionGap;
    for (size_t i = 0; i < bounds.size(); ++i) {
        for (size_t j = i + 1; j < bounds.size(); ++j) {
            auto ri = bounds[i].expanded(gap / 2);
            auto rj = bounds[j].expanded(gap / 2);
            EXPECT_FALSE(ri.intersects(rj))
                << "Oscillator " << i << " (" << bounds[i].toString() << ") and Oscillator " << j << " ("
                << bounds[j].toString() << ") overlap — real-size finalize should have displaced the second";
        }
    }
}

// ============================================================================
// Item 4: Alignment guide rendering tests
// ============================================================================

// ============================================================================
// Macro Control bank — runtime resize
// ============================================================================

namespace {

juce::AudioParameterInt* knobCountParam(juce::AudioProcessor* p) {
    for (auto* param : p->getParameters())
        if (auto* i = dynamic_cast<juce::AudioParameterInt*>(param))
            if (i->paramID == "macroCount")
                return i;
    return nullptr;
}

ModuleComponent* componentFor(GraphEditor& editor, juce::AudioProcessorGraph::NodeID id) {
    for (auto* comp : editor.getModuleComponents())
        if (comp != nullptr && comp->getNodeId() == id)
            return comp;
    return nullptr;
}

void setKnobs(juce::AudioProcessor* macros, int count) {
    auto* p = knobCountParam(macros);
    p->setValueNotifyingHost(p->convertTo0to1(count));
    // parameterValueChanged marshals the resize onto the message thread. A single fixed 50 ms
    // pump was measured too tight on a loaded CI runner (the macOS job flaked exactly here), so
    // pump in slices until the module actually reports the new count — bounded, then one extra
    // slice so the same message-thread callback's routing cleanup has run too.
    auto* mb = dynamic_cast<ModuleBase*>(macros);
    for (int i = 0; i < 40; ++i) {
        juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
        if (mb != nullptr && mb->getVisibleOutputPortCount() == count)
            break;
    }
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
}

} // namespace

TEST_F(GraphEditorTest, MacroBankGrowsAndPushesTheModuleBelowItDown) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1200, 900);

    auto& graph = engine.getGraph();
    auto macroNode = graph.addNode(synth::AIStateMapper::createModule("Macros"));
    auto vcaNode = graph.addNode(std::make_unique<VCAModule>());
    ASSERT_NE(macroNode, nullptr);
    ASSERT_NE(vcaNode, nullptr);

    setKnobs(macroNode->getProcessor(), 4);

    macroNode->properties.set("x", 100);
    macroNode->properties.set("y", 100);
    vcaNode->properties.set("x", 100);
    vcaNode->properties.set("y", 500);
    editor.updateComponents();

    auto* macroComp = componentFor(editor, macroNode->nodeID);
    auto* vcaComp = componentFor(editor, vcaNode->nodeID);
    ASSERT_NE(macroComp, nullptr);
    ASSERT_NE(vcaComp, nullptr);

    const auto macroTopLeftBefore = macroComp->getPosition();
    const int vcaYBefore = vcaComp->getY();
    ASSERT_LT(macroComp->getBottom(), vcaComp->getY()) << "test setup: the two must start clear of each other";

    setKnobs(macroNode->getProcessor(), 16);

    EXPECT_EQ(macroComp->getHeight(), synth::LayoutUtil::macroBankHeight(16));
    EXPECT_EQ(macroComp->getPosition(), macroTopLeftBefore) << "the resized module must not move";
    EXPECT_GT(vcaComp->getY(), vcaYBefore) << "the module below must be pushed clear";
    EXPECT_GE(vcaComp->getY(), macroComp->getBottom() + synth::LayoutUtil::kCollisionGap);

    // The displaced position must be persisted, or a reload would drop it back into the overlap.
    EXPECT_EQ(static_cast<int>(vcaNode->properties.getWithDefault("y", -1)), vcaComp->getY());
}

TEST_F(GraphEditorTest, ShrinkingTheMacroBankDropsRoutingsOnTheJacksItHides) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1200, 900);

    auto& graph = engine.getGraph();
    auto macroNode = graph.addNode(synth::AIStateMapper::createModule("Macros"));
    auto filterNode = graph.addNode(std::make_unique<FilterModule>());
    ASSERT_NE(macroNode, nullptr);
    ASSERT_NE(filterNode, nullptr);

    setKnobs(macroNode->getProcessor(), 16);
    editor.updateComponents();

    // M1 (kept) and M12 (about to be hidden) both drive the filter's cutoff CV.
    engine.addModRouting(macroNode->nodeID, 0, filterNode->nodeID, 8);
    engine.addModRouting(macroNode->nodeID, 11, filterNode->nodeID, 8);

    auto sourcesFrom = [&](int channel) {
        int n = 0;
        for (const auto& c : graph.getConnections())
            if (c.source.nodeID == macroNode->nodeID && c.source.channelIndex == channel)
                ++n;
        return n;
    };

    ASSERT_EQ(sourcesFrom(0), 1);
    ASSERT_EQ(sourcesFrom(11), 1);
    const int attenuvertersBefore = (int)graph.getNodes().size();

    setKnobs(macroNode->getProcessor(), 4);

    EXPECT_EQ(sourcesFrom(0), 1) << "a jack that is still visible must keep its routing";
    EXPECT_EQ(sourcesFrom(11), 0) << "the hidden jack's routing must be removed";
    EXPECT_LT((int)graph.getNodes().size(), attenuvertersBefore)
        << "the orphaned attenuverter must be removed with the routing, not left behind";
}

TEST_F(GraphEditorTest, AlignmentGuideDrawingThemeAware) {
    // Verify paintOverChildren() uses theme colors correctly.
    synth::theme::AppLookAndFeel lf;
    lf.applyTheme(synth::theme::makeObsidian());

    const auto& m = lf.getTheme().metrics;
    const auto guideColor = lf.getTheme().colors.textMuted.withAlpha(m.guideAlpha);

    // Verify opacity matches Item 4 spec (70%)
    EXPECT_FLOAT_EQ(m.guideAlpha, 0.7f);
    EXPECT_NEAR(guideColor.getFloatAlpha(), 0.7f, 0.01f);

    // Verify line width matches spec
    EXPECT_FLOAT_EQ(m.guideLineWidth, 1.5f);
}

// ============================================================================
// Issue #163: poly connections auto-fan-out
// ============================================================================

// ---- Pure resolvePolyLink tests (no graph needed) ----

TEST_F(GraphEditorTest, ResolvePolyLinkFansEnvelopeToPolyVCA) {
    ADSRModule adsr;
    VCAModule vca;
    setPolyParam(adsr, true);
    setPolyParam(vca, true);

    auto link = GraphEditor::resolvePolyLink(&adsr, 0, &vca, 2); // VCA CV moved to jack 2 behind Audio L/R (#219)
    EXPECT_EQ(link.sourceRawChannel, 0);
    EXPECT_EQ(link.destRawChannel, 8);
    EXPECT_EQ(link.voiceCount, 8);
}

TEST_F(GraphEditorTest, ResolvePolyLinkPicksFanMatchingRole) {
    PolyMidiModule polyMidi;
    OscillatorModule osc;
    ADSRModule adsr;
    setPolyParam(osc, true);
    setPolyParam(adsr, true);

    // PolyMidi's "Poly Out" jack fronts both a Pitch fan (raw 0) and a Gate fan (raw 8).
    // Into the Oscillator's Pitch input, the Pitch fan must win.
    auto linkToOsc = GraphEditor::resolvePolyLink(&polyMidi, 0, &osc, 0);
    EXPECT_EQ(linkToOsc.sourceRawChannel, 0);
    EXPECT_EQ(linkToOsc.destRawChannel, 0);
    EXPECT_EQ(linkToOsc.voiceCount, 8);

    // Into the ADSR's Gate input (same source visible jack), the Gate fan must win instead.
    auto linkToAdsr = GraphEditor::resolvePolyLink(&polyMidi, 0, &adsr, 0);
    EXPECT_EQ(linkToAdsr.sourceRawChannel, 8);
    EXPECT_EQ(linkToAdsr.destRawChannel, 0);
    EXPECT_EQ(linkToAdsr.voiceCount, 8);
}

TEST_F(GraphEditorTest, ResolvePolyLinkStaysMonoWhenDestIsMono) {
    ADSRModule polyAdsr;
    setPolyParam(polyAdsr, true);
    VCAModule monoVca; // poly defaults to false

    // A poly source into a mono jack must not sum eight envelopes onto one CV channel.
    auto link = GraphEditor::resolvePolyLink(&polyAdsr, 0, &monoVca, 2); // CV jack (#219)
    EXPECT_EQ(link.sourceRawChannel, 0);
    EXPECT_EQ(link.destRawChannel, 1);
    EXPECT_EQ(link.voiceCount, 1);
    EXPECT_EQ(link.sourceStride, 1);
}

TEST_F(GraphEditorTest, ResolvePolyLinkBroadcastsMonoSourceAcrossModCvFan) {
    // One mono modulator on a per-voice mod-CV fan drives every voice: all eight wires leave the
    // same source channel (sourceStride == 0) and land on VCA raw channels 8-15.
    LFOModule lfo;
    VCAModule polyVca;
    setPolyParam(polyVca, true);

    auto link = GraphEditor::resolvePolyLink(&lfo, 0, &polyVca, 2); // CV jack (#219)
    EXPECT_EQ(link.sourceRawChannel, 0);
    EXPECT_EQ(link.destRawChannel, 8);
    EXPECT_EQ(link.voiceCount, 8);
    EXPECT_EQ(link.sourceStride, 0);
}

TEST_F(GraphEditorTest, ResolvePolyLinkBroadcastsMonoIntoCollapsedStereoPair) {
    // Collapsed Dual I/O (voiceSpan 2, PortRole::Audio) is a stereo bus, not a poly voice fan —
    // mono sources duplicate onto L and R so a single cable feeds both FX legs.
    OscillatorModule osc;
    DelayModule delay; // Dual I/O defaults off → one Audio jack spanning raw 0/1

    auto link = GraphEditor::resolvePolyLink(&osc, 0, &delay, 0);
    EXPECT_EQ(link.sourceRawChannel, 0);
    EXPECT_EQ(link.destRawChannel, 0);
    EXPECT_EQ(link.voiceCount, 2);
    EXPECT_EQ(link.sourceStride, 0);
}

TEST_F(GraphEditorTest, TogglingDualIOKeepsBothStereoLegs) {
    // Dual I/O only changes jack visibility. A collapsed Audio cable fans onto raw ch0 and ch1;
    // flipping Dual I/O on must not drop the right leg.
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    auto oscNode = engine.getGraph().addNode(std::make_unique<OscillatorModule>());
    auto delayNode = engine.getGraph().addNode(std::make_unique<DelayModule>());
    editor.updateComponents();

    ModuleComponent* oscComp = nullptr;
    ModuleComponent* delayComp = nullptr;
    auto* content = editor.getChildComponent(0);
    if (content) {
        for (auto* contentChild : content->getChildren()) {
            if (auto* mod = dynamic_cast<ModuleComponent*>(contentChild)) {
                if (mod->getModule() == oscNode->getProcessor())
                    oscComp = mod;
                if (mod->getModule() == delayNode->getProcessor())
                    delayComp = mod;
            }
        }
    }
    ASSERT_NE(oscComp, nullptr);
    ASSERT_NE(delayComp, nullptr);

    oscComp->setBounds(0, 0, 100, 100);
    delayComp->setBounds(200, 0, 100, 100);

    editor.beginConnectionDrag(oscComp, 0, false, false, juce::Point<int>(0, 0));
    editor.dragConnection(juce::Point<int>(50, 0));
    editor.endConnectionDrag(delayComp->getBounds().getPosition() + delayComp->getPortCenter(0, true));

    auto& graph = engine.getGraph();
    auto hasEdge = [&](int srcCh, int dstCh) {
        for (auto& conn : graph.getConnections())
            if (conn.source.nodeID == oscNode->nodeID && conn.source.channelIndex == srcCh &&
                conn.destination.nodeID == delayNode->nodeID && conn.destination.channelIndex == dstCh)
                return true;
        return false;
    };
    ASSERT_TRUE(hasEdge(0, 0));
    ASSERT_TRUE(hasEdge(0, 1)) << "collapsed Audio jack must fan onto Delay Right before the toggle";

    setDualIOParam(*delayNode->getProcessor(), true);

    EXPECT_TRUE(hasEdge(0, 0)) << "Left leg must survive Dual I/O on";
    // The right leg is still fed — but by the Oscillator's own Audio R block, not by a second copy
    // of Audio L. The collapsed jack's mono duplicate has to go with it: keeping both would sum
    // L+R into the Delay's Right input (+6 dB on one side) behind what draws as a single cable.
    //
    // RECONCILED with the later ruling that a module split while wired must come up with both legs
    // live (SplittingAMidChainVoiceModule*): that ruling introduced a mono BROADCAST, and this test
    // is not in tension with it. A broadcast is the fallback for a peer that has no right leg at
    // all; here the Oscillator upstream is dual and owns a real Audio R, so the real leg wins and
    // the stand-in copy is removed. The two rules are one rule: prefer the peer's right leg, copy
    // the left only when there is none. Pinned from the other direction by
    // SplittingAMidChainVoiceModulePrefersRealRightLegsOverABroadcast.
    EXPECT_TRUE(hasEdge(OscillatorModule::kRightBase, 1)) << "Right leg must survive Dual I/O on";
    EXPECT_FALSE(hasEdge(0, 1)) << "the collapsed jack's duplicate of Audio L must not double-feed Right";

    setDualIOParam(*delayNode->getProcessor(), false);

    EXPECT_TRUE(hasEdge(0, 0));
    EXPECT_TRUE(hasEdge(OscillatorModule::kRightBase, 1)) << "Right leg must survive Dual I/O off as well";
}

TEST_F(GraphEditorTest, TogglingDualIOCompletesStereoOutputPair) {
    // A collapsed Audio output dropped on a 2-channel dest (Audio Output, or Dual I/O) often
    // only records the left edge. Toggling Dual I/O on must add the matching right edge.
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    auto srcNode = engine.getGraph().addNode(std::make_unique<DelayModule>());
    auto dstNode = engine.getGraph().addNode(std::make_unique<DelayModule>());
    editor.updateComponents();

    ModuleComponent* srcComp = nullptr;
    auto* content = editor.getChildComponent(0);
    if (content) {
        for (auto* contentChild : content->getChildren()) {
            if (auto* mod = dynamic_cast<ModuleComponent*>(contentChild))
                if (mod->getModule() == srcNode->getProcessor())
                    srcComp = mod;
        }
    }
    ASSERT_NE(srcComp, nullptr);

    auto& graph = engine.getGraph();
    ASSERT_TRUE(graph.addConnection({{srcNode->nodeID, 0}, {dstNode->nodeID, 0}}));
    {
        bool hasRightBefore = false;
        for (const auto& conn : graph.getConnections())
            if (conn.source.nodeID == srcNode->nodeID && conn.source.channelIndex == 1)
                hasRightBefore = true;
        ASSERT_FALSE(hasRightBefore);
    }

    setDualIOParam(*srcNode->getProcessor(), true);

    bool hasRight = false;
    for (const auto& conn : graph.getConnections())
        if (conn.source.nodeID == srcNode->nodeID && conn.source.channelIndex == 1 &&
            conn.destination.nodeID == dstNode->nodeID && conn.destination.channelIndex == 1)
            hasRight = true;
    EXPECT_TRUE(hasRight) << "Dual I/O on must pair Delay Right out to the dest Right in";
}

TEST_F(GraphEditorTest, DualIOOnSourceDrawsRightCableOntoCollapsedDest) {
    // Both Dual I/O off: one Audio→Audio cable, two raw edges. Splitting only the source must
    // still draw the Right output onto the dest's remaining Audio jack — not wait until the dest
    // is split too (raw ch1 used to be treated as hidden because dest visibleCount == 1).
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    auto srcNode = engine.getGraph().addNode(std::make_unique<DelayModule>());
    auto dstNode = engine.getGraph().addNode(std::make_unique<DelayModule>());
    editor.updateComponents();

    ModuleComponent* srcComp = nullptr;
    ModuleComponent* dstComp = nullptr;
    auto* content = editor.getChildComponent(0);
    if (content) {
        for (auto* contentChild : content->getChildren()) {
            if (auto* mod = dynamic_cast<ModuleComponent*>(contentChild)) {
                if (mod->getModule() == srcNode->getProcessor())
                    srcComp = mod;
                if (mod->getModule() == dstNode->getProcessor())
                    dstComp = mod;
            }
        }
    }
    ASSERT_NE(srcComp, nullptr);
    ASSERT_NE(dstComp, nullptr);
    srcComp->setBounds(0, 0, 200, 200);
    dstComp->setBounds(300, 0, 200, 200);

    editor.beginConnectionDrag(srcComp, 0, false, false, juce::Point<int>(0, 0));
    editor.dragConnection(juce::Point<int>(50, 0));
    editor.endConnectionDrag(dstComp->getBounds().getPosition() + dstComp->getPortCenter(0, true));

    setDualIOParam(*srcNode->getProcessor(), true);
    ASSERT_FALSE(dynamic_cast<ModuleBase*>(dstNode->getProcessor())->isDualIO());

    bool drawnFromRight = false;
    for (const auto& cable : editor.buildVisibleCables())
        if (cable.id.srcUid == srcNode->nodeID.uid && cable.id.dstUid == dstNode->nodeID.uid && cable.id.srcPort == 1)
            drawnFromRight = true;
    EXPECT_TRUE(drawnFromRight) << "Right out must draw onto the dest Audio jack while dest Dual I/O is still off";
}

TEST_F(GraphEditorTest, DualIOOnDestDrawsRightCableFromCollapsedSource) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    auto srcNode = engine.getGraph().addNode(std::make_unique<DelayModule>());
    auto dstNode = engine.getGraph().addNode(std::make_unique<DelayModule>());
    editor.updateComponents();

    ModuleComponent* srcComp = nullptr;
    ModuleComponent* dstComp = nullptr;
    auto* content = editor.getChildComponent(0);
    if (content) {
        for (auto* contentChild : content->getChildren()) {
            if (auto* mod = dynamic_cast<ModuleComponent*>(contentChild)) {
                if (mod->getModule() == srcNode->getProcessor())
                    srcComp = mod;
                if (mod->getModule() == dstNode->getProcessor())
                    dstComp = mod;
            }
        }
    }
    ASSERT_NE(srcComp, nullptr);
    ASSERT_NE(dstComp, nullptr);
    srcComp->setBounds(0, 0, 200, 200);
    dstComp->setBounds(300, 0, 200, 200);

    editor.beginConnectionDrag(srcComp, 0, false, false, juce::Point<int>(0, 0));
    editor.dragConnection(juce::Point<int>(50, 0));
    editor.endConnectionDrag(dstComp->getBounds().getPosition() + dstComp->getPortCenter(0, true));

    setDualIOParam(*dstNode->getProcessor(), true);
    ASSERT_FALSE(dynamic_cast<ModuleBase*>(srcNode->getProcessor())->isDualIO());

    bool drawnOntoRight = false;
    for (const auto& cable : editor.buildVisibleCables())
        if (cable.id.srcUid == srcNode->nodeID.uid && cable.id.dstUid == dstNode->nodeID.uid && cable.id.dstPort == 1)
            drawnOntoRight = true;
    EXPECT_TRUE(drawnOntoRight)
        << "Dest Right in must draw from the source Audio jack while source Dual I/O is still off";
}

// ---------------------------------------------------------------------------
// Dual I/O toggle: wiring the right leg (issue: "toggling Dual I/O on leaves Audio R dangling")
//
// A split-block source (its Audio R on a kRightBase block) is the case the FX-pair tests above do
// not cover, and it is the one that reached a user: an Oscillator split into Audio L / Audio R with
// only Audio L wired, and no cable on Audio R at all.
// ---------------------------------------------------------------------------

namespace {
// Every card the editor built, by the processor it fronts.
ModuleComponent* cardFor(GraphEditor& editor, juce::AudioProcessor* proc) {
    if (auto* content = editor.getChildComponent(0))
        for (auto* child : content->getChildren())
            if (auto* mod = dynamic_cast<ModuleComponent*>(child))
                if (mod->getModule() == proc)
                    return mod;
    return nullptr;
}

bool graphHasEdge(juce::AudioProcessorGraph& graph, juce::AudioProcessorGraph::NodeID src, int srcCh,
                  juce::AudioProcessorGraph::NodeID dst, int dstCh) {
    for (const auto& c : graph.getConnections())
        if (c.source.nodeID == src && c.source.channelIndex == srcCh && c.destination.nodeID == dst &&
            c.destination.channelIndex == dstCh)
            return true;
    return false;
}

int feedCount(juce::AudioProcessorGraph& graph, juce::AudioProcessorGraph::NodeID dst, int dstCh) {
    int n = 0;
    for (const auto& c : graph.getConnections())
        if (c.destination.nodeID == dst && !c.destination.isMIDI() && c.destination.channelIndex == dstCh)
            ++n;
    return n;
}
} // namespace

TEST_F(GraphEditorTest, SplittingASplitBlockSourceWiresAudioRIntoACollapsedFXDestination) {
    // Oscillator (Audio R at kRightBase) into a collapsed Delay, wired while the Oscillator was
    // collapsed. Splitting it must move the Delay's right raw leg over to Audio R — and the
    // duplicate of Audio L that was standing in for it must go, or Right carries L+R.
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    auto& graph = engine.getGraph();
    auto oscNode = graph.addNode(std::make_unique<OscillatorModule>());
    auto delayNode = graph.addNode(std::make_unique<DelayModule>());
    setDualIOParam(*oscNode->getProcessor(), false);
    editor.updateComponents();

    auto* oscComp = cardFor(editor, oscNode->getProcessor());
    auto* delayComp = cardFor(editor, delayNode->getProcessor());
    ASSERT_NE(oscComp, nullptr);
    ASSERT_NE(delayComp, nullptr);
    oscComp->setBounds(0, 0, 200, 400);
    delayComp->setBounds(300, 0, 200, 400);

    editor.beginConnectionDrag(oscComp, 0, false, false, juce::Point<int>(0, 0));
    editor.dragConnection(juce::Point<int>(50, 0));
    editor.endConnectionDrag(delayComp->getBounds().getPosition() + delayComp->getPortCenter(0, true));

    ASSERT_TRUE(graphHasEdge(graph, oscNode->nodeID, 0, delayNode->nodeID, 0));
    ASSERT_TRUE(graphHasEdge(graph, oscNode->nodeID, 0, delayNode->nodeID, 1))
        << "the collapsed pair starts out fed twice from the one Audio jack";

    setDualIOParam(*oscNode->getProcessor(), true);

    EXPECT_TRUE(graphHasEdge(graph, oscNode->nodeID, 0, delayNode->nodeID, 0)) << "Audio L keeps the left leg";
    EXPECT_TRUE(graphHasEdge(graph, oscNode->nodeID, OscillatorModule::kRightBase, delayNode->nodeID, 1))
        << "Audio R must be wired onto the collapsed destination's second raw leg, not left dangling";
    EXPECT_EQ(feedCount(graph, delayNode->nodeID, 1), 1)
        << "the destination's right leg must be fed exactly once — Audio R, not Audio L + Audio R";
}

TEST_F(GraphEditorTest, SplittingASourceWiresAudioRIntoADualDestinationsOwnRightBlock) {
    // Both ends split-block: the right leg lands on the DESTINATION's kRightBase, never on its ch1
    // (which is the Filter's Cutoff CV).
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    auto& graph = engine.getGraph();
    auto oscNode = graph.addNode(std::make_unique<OscillatorModule>());
    auto filterNode = graph.addNode(std::make_unique<FilterModule>());
    setDualIOParam(*oscNode->getProcessor(), false);
    ASSERT_TRUE(dynamic_cast<ModuleBase*>(filterNode->getProcessor())->isDualIO()) << "Filter defaults to dual";
    ASSERT_TRUE(graph.addConnection({{oscNode->nodeID, 0}, {filterNode->nodeID, 0}}));
    editor.updateComponents();

    setDualIOParam(*oscNode->getProcessor(), true);

    EXPECT_TRUE(graphHasEdge(graph, oscNode->nodeID, OscillatorModule::kRightBase, filterNode->nodeID,
                             FilterModule::kRightBase))
        << "Audio R must reach the destination's own Audio R block";
    EXPECT_FALSE(graphHasEdge(graph, oscNode->nodeID, OscillatorModule::kRightBase, filterNode->nodeID, 1))
        << "ch1 is the Filter's Cutoff CV and must never be wired as audio";
}

TEST_F(GraphEditorTest, SplittingASourceSumsAudioRIntoACollapsedPeersMonoJackNeverItsHiddenBlock) {
    // The reported screenshot: a collapsed Oscillator wired into a COLLAPSED Filter's single Audio
    // jack, then split. Audio R used to come up visibly dangling because the Filter has no second
    // audio input jack to pair with.
    //
    // USER RULING: wire it into that same mono jack — a summed second cable, exactly what dragging
    // both legs there by hand produces. Two things the ruling did NOT change, both still asserted
    // here: the destination's hidden kRightBase block stays unwired (no audible, unpluggable cable),
    // and a destination that HAS a right leg still gets a real pair
    // (SplittingAMidChainVoiceModuleWiresAllFourLegsWhenTheDownstreamCanTakeIt, and dual->dual in
    // SplittingAMidChainVoiceModulePrefersRealRightLegsOverABroadcast).
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    auto& graph = engine.getGraph();
    auto oscNode = graph.addNode(std::make_unique<OscillatorModule>());
    auto filterNode = graph.addNode(std::make_unique<FilterModule>());
    setDualIOParam(*oscNode->getProcessor(), false);
    setDualIOParam(*filterNode->getProcessor(), false);
    ASSERT_TRUE(graph.addConnection({{oscNode->nodeID, 0}, {filterNode->nodeID, 0}}));
    editor.updateComponents();

    setDualIOParam(*oscNode->getProcessor(), true);

    EXPECT_TRUE(graphHasEdge(graph, oscNode->nodeID, 0, filterNode->nodeID, 0)) << "the left leg is untouched";
    EXPECT_TRUE(graphHasEdge(graph, oscNode->nodeID, OscillatorModule::kRightBase, filterNode->nodeID, 0))
        << "Audio R must be wired into the collapsed destination's mono jack, not left dangling";
    EXPECT_EQ(feedCount(graph, filterNode->nodeID, 0), 2) << "exactly one extra cable: Audio L plus Audio R";
    for (int ch = FilterModule::kRightBase; ch < filterNode->getProcessor()->getTotalNumInputChannels(); ++ch)
        EXPECT_EQ(feedCount(graph, filterNode->nodeID, ch), 0)
            << "nothing may be wired onto a hidden right block (channel " << ch << ")";
    EXPECT_FALSE(graphHasEdge(graph, oscNode->nodeID, OscillatorModule::kRightBase, filterNode->nodeID, 1))
        << "ch1 is the Filter's Cutoff CV and must never be wired as audio";

    // ...and toggling back off round-trips exactly: the extra cable rides the hidden right block, so
    // the collapse-drops rule takes it away and the single mono cable is all that is left.
    setDualIOParam(*oscNode->getProcessor(), false);
    EXPECT_EQ(feedCount(graph, filterNode->nodeID, 0), 1) << "collapsing must remove the summed second cable";
    EXPECT_TRUE(graphHasEdge(graph, oscNode->nodeID, 0, filterNode->nodeID, 0)) << "leaving the original cable";
}

// ---------------------------------------------------------------------------
// Expanding a DESTINATION that already carries a summed pair: migrate, never add
//
// The sum above is a cable aimed at a jack that no longer exists once the destination splits. The
// reported sequence made that concrete: Osc to dual left two cables on the Filter's mono jack, and
// then splitting the FILTER wired a third (L->L, the old R->L sum, plus a new R->R). Expanding has
// to MOVE the second feed onto the new Audio R, so every jack ends up with exactly one cable.
// ---------------------------------------------------------------------------

TEST_F(GraphEditorTest, ExpandingADestinationMigratesASummedPairInsteadOfAddingAThirdCable) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    auto& graph = engine.getGraph();
    auto oscNode = graph.addNode(std::make_unique<OscillatorModule>());
    auto filterNode = graph.addNode(std::make_unique<FilterModule>());
    setDualIOParam(*oscNode->getProcessor(), false);
    setDualIOParam(*filterNode->getProcessor(), false);
    ASSERT_TRUE(graph.addConnection({{oscNode->nodeID, 0}, {filterNode->nodeID, 0}}));
    editor.updateComponents();

    auto edgesBetween = [&] {
        int n = 0;
        for (const auto& c : graph.getConnections())
            if (c.source.nodeID == oscNode->nodeID && c.destination.nodeID == filterNode->nodeID && !c.source.isMIDI())
                ++n;
        return n;
    };

    // Step 1 - split the SOURCE: its Audio R is summed into the Filter's still-mono jack.
    setDualIOParam(*oscNode->getProcessor(), true);
    ASSERT_EQ(edgesBetween(), 2);
    ASSERT_EQ(feedCount(graph, filterNode->nodeID, 0), 2);

    // Step 2 - split the DESTINATION. This is the bug: it used to add a third cable.
    setDualIOParam(*filterNode->getProcessor(), true);

    EXPECT_EQ(edgesBetween(), 2) << "expanding the destination must migrate the sum, not add to it";
    EXPECT_TRUE(graphHasEdge(graph, oscNode->nodeID, 0, filterNode->nodeID, 0)) << "L to L";
    EXPECT_TRUE(graphHasEdge(graph, oscNode->nodeID, OscillatorModule::kRightBase, filterNode->nodeID,
                             FilterModule::kRightBase))
        << "R to R, moved off the mono jack rather than duplicated";
    EXPECT_EQ(feedCount(graph, filterNode->nodeID, 0), 1) << "exactly one cable per jack";
    EXPECT_EQ(feedCount(graph, filterNode->nodeID, FilterModule::kRightBase), 1);
    EXPECT_FALSE(graphHasEdge(graph, oscNode->nodeID, OscillatorModule::kRightBase, filterNode->nodeID, 0))
        << "the old summed cable must be gone, not left alongside the new pair";
}

TEST_F(GraphEditorTest, SplittingASourceSumsAudioRIntoADedicatedMonoAudioInput) {
    // The Ring Modulator's Carrier (ch0) and Modulator (ch1) are two mono audio inputs with distinct
    // roles - never a stereo pair. Splitting an Oscillator that feeds Modulator used to wire Audio L
    // and stop, because the rewire only looked at cables landing on the destination's ch0. Same
    // ruling as the collapsed mono jack: sum the right leg into that very input.
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    auto& graph = engine.getGraph();
    auto oscNode = graph.addNode(std::make_unique<OscillatorModule>());
    auto ringNode = graph.addNode(std::make_unique<RingModulatorModule>());
    setDualIOParam(*oscNode->getProcessor(), false);
    ASSERT_TRUE(graph.addConnection({{oscNode->nodeID, 0}, {ringNode->nodeID, 1}})) << "Osc into Modulator";
    editor.updateComponents();

    setDualIOParam(*oscNode->getProcessor(), true);

    EXPECT_TRUE(graphHasEdge(graph, oscNode->nodeID, 0, ringNode->nodeID, 1)) << "Audio L keeps the Modulator input";
    EXPECT_TRUE(graphHasEdge(graph, oscNode->nodeID, OscillatorModule::kRightBase, ringNode->nodeID, 1))
        << "Audio R must land on the same Modulator input, not dangle";
    EXPECT_EQ(feedCount(graph, ringNode->nodeID, 1), 2) << "exactly one extra cable";
    EXPECT_EQ(feedCount(graph, ringNode->nodeID, 0), 0) << "Carrier is a different jack and must stay empty";
    for (int ch = 2; ch < 5; ++ch)
        EXPECT_EQ(feedCount(graph, ringNode->nodeID, ch), 0)
            << "and no audio may be dumped onto a CV jack (channel " << ch << ")";

    // Collapse round-trips: the extra cable hangs off the hidden right block.
    setDualIOParam(*oscNode->getProcessor(), false);
    EXPECT_EQ(feedCount(graph, ringNode->nodeID, 1), 1);
    EXPECT_TRUE(graphHasEdge(graph, oscNode->nodeID, 0, ringNode->nodeID, 1));
}

TEST_F(GraphEditorTest, SplittingASourceNeverSumsAudioRIntoAModulationInput) {
    // The counterpart guard: a cable the user aimed at a CV jack must not gain an audio-rate copy of
    // the right leg. Filter ch2 is Resonance CV.
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    auto& graph = engine.getGraph();
    auto oscNode = graph.addNode(std::make_unique<OscillatorModule>());
    auto filterNode = graph.addNode(std::make_unique<FilterModule>());
    setDualIOParam(*oscNode->getProcessor(), false);
    ASSERT_TRUE(graph.addConnection({{oscNode->nodeID, 0}, {filterNode->nodeID, 2}}));
    editor.updateComponents();

    setDualIOParam(*oscNode->getProcessor(), true);

    EXPECT_EQ(feedCount(graph, filterNode->nodeID, 2), 1) << "a CV jack gains nothing from a split";
    EXPECT_FALSE(graphHasEdge(graph, oscNode->nodeID, OscillatorModule::kRightBase, filterNode->nodeID, 2));
}

TEST_F(GraphEditorTest, ExpandingADestinationMigratesOneRawOfACollapsedUpstreamJack) {
    // The duplicate-of-one-jack variant: a collapsed Delay's single Audio jack owns raw0 AND raw1, and
    // both are wired onto a collapsed Filter's mono jack. Expanding the Filter moves the raw1 feed
    // onto Audio R, because that raw is the upstream's right leg even though it fronts one jack.
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    auto& graph = engine.getGraph();
    auto delayNode = graph.addNode(std::make_unique<DelayModule>());
    auto filterNode = graph.addNode(std::make_unique<FilterModule>());
    setDualIOParam(*filterNode->getProcessor(), false);
    ASSERT_FALSE(dynamic_cast<ModuleBase*>(delayNode->getProcessor())->isDualIO());
    ASSERT_TRUE(graph.addConnection({{delayNode->nodeID, 0}, {filterNode->nodeID, 0}}));
    ASSERT_TRUE(graph.addConnection({{delayNode->nodeID, 1}, {filterNode->nodeID, 0}}));
    editor.updateComponents();

    setDualIOParam(*filterNode->getProcessor(), true);

    EXPECT_TRUE(graphHasEdge(graph, delayNode->nodeID, 0, filterNode->nodeID, 0)) << "raw0 stays on Audio L";
    EXPECT_TRUE(graphHasEdge(graph, delayNode->nodeID, 1, filterNode->nodeID, FilterModule::kRightBase))
        << "raw1 migrates to Audio R";
    EXPECT_EQ(feedCount(graph, filterNode->nodeID, 0), 1);
    EXPECT_EQ(feedCount(graph, filterNode->nodeID, FilterModule::kRightBase), 1);
}

TEST_F(GraphEditorTest, ExpandingADestinationLeavesAHandBuiltMixOfTwoSourcesAlone) {
    // Two feeds from two DIFFERENT modules is a mix the user built. Splitting must not move half of
    // it onto the new jack, and with the mono jack no longer single-fed it does not broadcast either.
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    auto& graph = engine.getGraph();
    auto oscA = graph.addNode(std::make_unique<OscillatorModule>());
    auto oscB = graph.addNode(std::make_unique<OscillatorModule>());
    auto filterNode = graph.addNode(std::make_unique<FilterModule>());
    setDualIOParam(*oscA->getProcessor(), false);
    setDualIOParam(*oscB->getProcessor(), false);
    setDualIOParam(*filterNode->getProcessor(), false);
    ASSERT_TRUE(graph.addConnection({{oscA->nodeID, 0}, {filterNode->nodeID, 0}}));
    ASSERT_TRUE(graph.addConnection({{oscB->nodeID, 0}, {filterNode->nodeID, 0}}));
    editor.updateComponents();

    setDualIOParam(*filterNode->getProcessor(), true);

    EXPECT_EQ(feedCount(graph, filterNode->nodeID, 0), 2) << "the hand-built mix stays intact";
    EXPECT_EQ(feedCount(graph, filterNode->nodeID, FilterModule::kRightBase), 0)
        << "and nothing is moved or copied onto Audio R";
}

TEST_F(GraphEditorTest, TheWholeSplitThenCollapseSequenceReturnsToOneMonoCable) {
    // Split source, split destination, collapse destination, collapse source: back to the single
    // cable the patch started with, with nothing stranded on either hidden right block.
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    auto& graph = engine.getGraph();
    auto oscNode = graph.addNode(std::make_unique<OscillatorModule>());
    auto filterNode = graph.addNode(std::make_unique<FilterModule>());
    setDualIOParam(*oscNode->getProcessor(), false);
    setDualIOParam(*filterNode->getProcessor(), false);
    ASSERT_TRUE(graph.addConnection({{oscNode->nodeID, 0}, {filterNode->nodeID, 0}}));
    editor.updateComponents();

    setDualIOParam(*oscNode->getProcessor(), true);
    setDualIOParam(*filterNode->getProcessor(), true);
    setDualIOParam(*filterNode->getProcessor(), false);
    setDualIOParam(*oscNode->getProcessor(), false);

    int audioEdges = 0;
    for (const auto& c : graph.getConnections())
        if (!c.source.isMIDI() && !c.destination.isMIDI())
            ++audioEdges;
    EXPECT_EQ(audioEdges, 1) << "the sequence must land back on exactly one cable";
    EXPECT_TRUE(graphHasEdge(graph, oscNode->nodeID, 0, filterNode->nodeID, 0)) << "and it is the original one";
}

// ---------------------------------------------------------------------------
// Dual I/O toggle on a SPLIT-BLOCK module that was wired while collapsed
//
// Reported shape: a Filter sitting mid-chain, flipped to Dual I/O while patched. Audio L in and out
// stayed wired and BOTH right jacks came up dangling, because a collapsed neighbour exposes no
// second audio jack for rightAudioLegOf() to find. The ruling: a module the user just split must
// arrive with both legs live.
//
// Both sides end up wired, by different means:
//   * INPUT  - copy the feed the left leg already has onto the right leg. Driving one more
//              destination from the same source channel cannot change the mix.
//   * OUTPUT - wire the right leg to the destination's right leg when the destination HAS one
//              (collapsed FX pair raw1, or a dual peer's own block). When it has none, wire it into
//              the destination's mono jack as a summed second cable, per user ruling: a dangling
//              Audio R was the complaint, and stereo-into-mono summing is what hand-wiring both
//              legs there already does. While the legs are still identical that sum is +6 dB, which
//              is transient - it lasts until the legs differ, which is why one splits.
//
// One thing neither side may do: wire the peer's HIDDEN kRightBase block. A cable there is audible
// and impossible to unplug, and dropHiddenRightLegConnections exists to keep it that way.
// ---------------------------------------------------------------------------

TEST_F(GraphEditorTest, SplittingAMidChainVoiceModuleFeedsItsRightInputFromAMonoUpstream) {
    // The screenshot, exactly: collapsed Oscillator -> Filter -> collapsed VCA, and the Filter is the
    // one being toggled. Audio R in is broadcast-fed from the mono upstream; Audio R out is summed
    // into the collapsed VCA's mono jack (see the header comment).
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    auto& graph = engine.getGraph();
    auto oscNode = graph.addNode(std::make_unique<OscillatorModule>());
    auto filterNode = graph.addNode(std::make_unique<FilterModule>());
    auto vcaNode = graph.addNode(std::make_unique<VCAModule>());
    setDualIOParam(*oscNode->getProcessor(), false);
    setDualIOParam(*filterNode->getProcessor(), false);
    setDualIOParam(*vcaNode->getProcessor(), false);
    ASSERT_TRUE(graph.addConnection({{oscNode->nodeID, 0}, {filterNode->nodeID, 0}}));
    ASSERT_TRUE(graph.addConnection({{filterNode->nodeID, 0}, {vcaNode->nodeID, 0}}));
    editor.updateComponents();

    setDualIOParam(*filterNode->getProcessor(), true);

    EXPECT_TRUE(graphHasEdge(graph, oscNode->nodeID, 0, filterNode->nodeID, 0)) << "Audio L in survives";
    EXPECT_TRUE(graphHasEdge(graph, filterNode->nodeID, 0, vcaNode->nodeID, 0)) << "Audio L out survives";
    EXPECT_TRUE(graphHasEdge(graph, oscNode->nodeID, 0, filterNode->nodeID, FilterModule::kRightBase))
        << "Audio R in must be fed by the mono upstream, not left dangling";
    EXPECT_EQ(feedCount(graph, filterNode->nodeID, FilterModule::kRightBase), 1)
        << "and fed exactly once, so a second toggle cannot stack feeds";

    // The output side. This assertion was the reverse until the user ruled on it: it used to require
    // exactly one feed on the VCA's mono jack, on the grounds that summing two identical legs makes a
    // layout toggle +6 dB louder. The ruling accepted that transient jump in exchange for both jacks
    // being wired, so the same shape now expects the second cable - while still never touching the
    // VCA's hidden right block.
    EXPECT_TRUE(graphHasEdge(graph, filterNode->nodeID, FilterModule::kRightBase, vcaNode->nodeID, 0))
        << "Audio R out must be summed into the collapsed VCA's mono jack";
    EXPECT_EQ(feedCount(graph, vcaNode->nodeID, 0), 2) << "exactly one extra cable, not a stack of them";
    for (int ch = VCAModule::kRightBase; ch < vcaNode->getProcessor()->getTotalNumInputChannels(); ++ch)
        EXPECT_EQ(feedCount(graph, vcaNode->nodeID, ch), 0) << "no cable onto the collapsed VCA's hidden block";

    // Toggling back off leaves the mono chain exactly as it started, on both sides: the broadcast and
    // the summed cable both hang off the Filter's hidden right block, so the collapse-drops rule
    // takes them with it.
    setDualIOParam(*filterNode->getProcessor(), false);
    EXPECT_TRUE(graphHasEdge(graph, oscNode->nodeID, 0, filterNode->nodeID, 0));
    EXPECT_EQ(feedCount(graph, filterNode->nodeID, FilterModule::kRightBase), 0)
        << "collapsing unhooks the hidden right leg again";
    EXPECT_EQ(feedCount(graph, filterNode->nodeID, 0), 1) << "and does not re-point the broadcast onto the left leg";
    EXPECT_EQ(feedCount(graph, vcaNode->nodeID, 0), 1) << "and the summed second cable is gone";
    EXPECT_TRUE(graphHasEdge(graph, filterNode->nodeID, 0, vcaNode->nodeID, 0)) << "leaving the original cable";
}

TEST_F(GraphEditorTest, SplittingAMidChainVoiceModuleWiresAllFourLegsWhenTheDownstreamCanTakeIt) {
    // Same shape with an FX downstream: a collapsed Distortion's one Audio jack owns raw0 AND raw1,
    // so the right leg has a legal, visible target and all four of the Filter's jacks end up wired.
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    auto& graph = engine.getGraph();
    auto oscNode = graph.addNode(std::make_unique<OscillatorModule>());
    auto filterNode = graph.addNode(std::make_unique<FilterModule>());
    auto distNode = graph.addNode(std::make_unique<DistortionModule>());
    setDualIOParam(*oscNode->getProcessor(), false);
    setDualIOParam(*filterNode->getProcessor(), false);
    ASSERT_FALSE(dynamic_cast<ModuleBase*>(distNode->getProcessor())->isDualIO()) << "FX default to collapsed";
    ASSERT_TRUE(graph.addConnection({{oscNode->nodeID, 0}, {filterNode->nodeID, 0}}));
    ASSERT_TRUE(graph.addConnection({{filterNode->nodeID, 0}, {distNode->nodeID, 0}}));
    ASSERT_TRUE(graph.addConnection({{filterNode->nodeID, 0}, {distNode->nodeID, 1}}))
        << "a mono feed into a collapsed FX pair fans onto both raw legs";
    editor.updateComponents();

    setDualIOParam(*filterNode->getProcessor(), true);

    EXPECT_TRUE(graphHasEdge(graph, oscNode->nodeID, 0, filterNode->nodeID, 0)) << "Audio L in";
    EXPECT_TRUE(graphHasEdge(graph, oscNode->nodeID, 0, filterNode->nodeID, FilterModule::kRightBase))
        << "Audio R in, broadcast from the mono upstream";
    EXPECT_TRUE(graphHasEdge(graph, filterNode->nodeID, 0, distNode->nodeID, 0)) << "Audio L out";
    EXPECT_TRUE(graphHasEdge(graph, filterNode->nodeID, FilterModule::kRightBase, distNode->nodeID, 1))
        << "Audio R out must reach the collapsed pair's second raw leg";
    EXPECT_FALSE(graphHasEdge(graph, filterNode->nodeID, 0, distNode->nodeID, 1))
        << "and the left leg's stand-in copy must go with it, or the right leg carries L+R";
    EXPECT_EQ(feedCount(graph, distNode->nodeID, 1), 1);
}

TEST_F(GraphEditorTest, SplittingAnOutputOnlyVoiceModuleTouchesOnlyItsOutputSide) {
    // The Oscillator has no audio input at all (its 14 inputs are pitch and mod CV), so the input
    // half of the rewire must not fire, and its kRightBase must not be mistaken for an audio in.
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    auto& graph = engine.getGraph();
    auto oscNode = graph.addNode(std::make_unique<OscillatorModule>());
    auto delayNode = graph.addNode(std::make_unique<DelayModule>());
    setDualIOParam(*oscNode->getProcessor(), false);
    ASSERT_TRUE(graph.addConnection({{oscNode->nodeID, 0}, {delayNode->nodeID, 0}}));
    ASSERT_TRUE(graph.addConnection({{oscNode->nodeID, 0}, {delayNode->nodeID, 1}}));
    editor.updateComponents();

    setDualIOParam(*oscNode->getProcessor(), true);

    EXPECT_TRUE(graphHasEdge(graph, oscNode->nodeID, OscillatorModule::kRightBase, delayNode->nodeID, 1))
        << "Audio R out pairs with the collapsed Delay's second raw leg";
    EXPECT_FALSE(graphHasEdge(graph, oscNode->nodeID, 0, delayNode->nodeID, 1)) << "no double feed";

    for (int ch = 0; ch < oscNode->getProcessor()->getTotalNumInputChannels(); ++ch)
        EXPECT_EQ(feedCount(graph, oscNode->nodeID, ch), 0)
            << "an output-only module must gain no input cables (channel " << ch << ")";
}

TEST_F(GraphEditorTest, SplittingAMidChainVoiceModulePrefersRealRightLegsOverABroadcast) {
    // Dual neighbours on both sides: the real Audio R blocks win, and no broadcast happens. This is
    // the rule that keeps TogglingDualIOKeepsBothStereoLegs true - a copy of Audio L stands in for
    // the right leg only while there is no right leg to be had.
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    auto& graph = engine.getGraph();
    auto oscNode = graph.addNode(std::make_unique<OscillatorModule>());
    auto filterNode = graph.addNode(std::make_unique<FilterModule>());
    auto vcaNode = graph.addNode(std::make_unique<VCAModule>());
    ASSERT_TRUE(dynamic_cast<ModuleBase*>(oscNode->getProcessor())->isDualIO()) << "voice modules default to dual";
    ASSERT_TRUE(dynamic_cast<ModuleBase*>(vcaNode->getProcessor())->isDualIO());
    setDualIOParam(*filterNode->getProcessor(), false);
    ASSERT_TRUE(graph.addConnection({{oscNode->nodeID, 0}, {filterNode->nodeID, 0}}));
    ASSERT_TRUE(graph.addConnection({{filterNode->nodeID, 0}, {vcaNode->nodeID, 0}}));
    editor.updateComponents();

    setDualIOParam(*filterNode->getProcessor(), true);

    EXPECT_TRUE(graphHasEdge(graph, oscNode->nodeID, OscillatorModule::kRightBase, filterNode->nodeID,
                             FilterModule::kRightBase))
        << "Audio R in comes from the upstream's own right block";
    EXPECT_FALSE(graphHasEdge(graph, oscNode->nodeID, 0, filterNode->nodeID, FilterModule::kRightBase))
        << "so no broadcast of Audio L";
    EXPECT_TRUE(
        graphHasEdge(graph, filterNode->nodeID, FilterModule::kRightBase, vcaNode->nodeID, VCAModule::kRightBase))
        << "Audio R out reaches the downstream's own right block, never its ch1 CV";
    EXPECT_FALSE(graphHasEdge(graph, filterNode->nodeID, FilterModule::kRightBase, vcaNode->nodeID, 1))
        << "ch1 is the VCA gain CV";
}

TEST_F(GraphEditorTest, SplittingAMidChainVoiceModuleIsOneUndoableStep) {
    AudioEngine engine;
    AppUndoManager undoMgr;
    GraphEditor editor(engine, &undoMgr);
    editor.setSize(800, 600);

    auto& graph = engine.getGraph();
    auto oscNode = graph.addNode(std::make_unique<OscillatorModule>());
    auto filterNode = graph.addNode(std::make_unique<FilterModule>());
    setDualIOParam(*oscNode->getProcessor(), false);
    setDualIOParam(*filterNode->getProcessor(), false);
    ASSERT_TRUE(graph.addConnection({{oscNode->nodeID, 0}, {filterNode->nodeID, 0}}));
    editor.updateComponents();
    ASSERT_NE(cardFor(editor, filterNode->getProcessor()), nullptr) << "the card is what listens to the gesture";

    juce::AudioProcessorParameter* dualParam = nullptr;
    for (auto* p : filterNode->getProcessor()->getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(p); withId && withId->paramID == "dualIO")
            dualParam = p;
    ASSERT_NE(dualParam, nullptr);

    dualParam->beginChangeGesture();
    dualParam->setValueNotifyingHost(1.0f);
    dualParam->endChangeGesture();

    ASSERT_TRUE(graphHasEdge(graph, oscNode->nodeID, 0, filterNode->nodeID, FilterModule::kRightBase))
        << "the broadcast happened";

    ASSERT_TRUE(undoMgr.undo());

    juce::AudioProcessorGraph::NodeID osc, filter;
    for (auto* node : graph.getNodes()) {
        if (dynamic_cast<OscillatorModule*>(node->getProcessor()) != nullptr)
            osc = node->nodeID;
        if (dynamic_cast<FilterModule*>(node->getProcessor()) != nullptr)
            filter = node->nodeID;
    }
    ASSERT_NE(filter.uid, 0u);

    auto* restoredFilter = dynamic_cast<ModuleBase*>(graph.getNodeForId(filter)->getProcessor());
    ASSERT_NE(restoredFilter, nullptr);
    EXPECT_FALSE(restoredFilter->isDualIO()) << "undo restores the parameter";
    EXPECT_TRUE(graphHasEdge(graph, osc, 0, filter, 0)) << "and the mono cable it was wired with";
    EXPECT_EQ(feedCount(graph, filter, FilterModule::kRightBase), 0) << "undo takes the broadcast back out";
}

// ---------------------------------------------------------------------------
// Dual I/O toggle: collapsing (issue: "turning Dual I/O off biases the mix left")
// ---------------------------------------------------------------------------

TEST_F(GraphEditorTest, CollapsingRePointsTheRightLegCableOntoTheSurvivingLeftLeg) {
    // The default patch's shape: VCA's Audio R block feeds the FX chain's ch1. Collapsing the VCA
    // hides that block, so the cable moves to the leg that survives instead of vanishing — without
    // it, the whole collapsed FX tail renders silence on the right.
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    auto& graph = engine.getGraph();
    auto vcaNode = graph.addNode(std::make_unique<VCAModule>());
    auto distNode = graph.addNode(std::make_unique<DistortionModule>());
    ASSERT_TRUE(dynamic_cast<ModuleBase*>(vcaNode->getProcessor())->isDualIO());
    ASSERT_TRUE(graph.addConnection({{vcaNode->nodeID, 0}, {distNode->nodeID, 0}}));
    ASSERT_TRUE(graph.addConnection({{vcaNode->nodeID, VCAModule::kRightBase}, {distNode->nodeID, 1}}));
    editor.updateComponents();

    setDualIOParam(*vcaNode->getProcessor(), false);

    EXPECT_FALSE(graphHasEdge(graph, vcaNode->nodeID, VCAModule::kRightBase, distNode->nodeID, 1))
        << "the hidden right block must not keep a cable";
    EXPECT_TRUE(graphHasEdge(graph, vcaNode->nodeID, 0, distNode->nodeID, 1))
        << "the cable must re-point onto the surviving left leg, or the right channel goes silent";
    EXPECT_EQ(feedCount(graph, distNode->nodeID, 1), 1);
}

TEST_F(GraphEditorTest, CollapsingRePointsTheRightLegCableOntoAudioOutput) {
    // Same rule when the far end is the graph's Audio Output node, which is not a ModuleBase: its
    // Right channel is still a real, visible jack, so the cable moves there.
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    auto& graph = engine.getGraph();
    graph.setPlayConfigDetails(0, 2, 44100.0, 512);
    auto vcaNode = graph.addNode(std::make_unique<VCAModule>());
    auto outNode = graph.addNode(std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor>(
        juce::AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode));
    ASSERT_TRUE(graph.addConnection({{vcaNode->nodeID, 0}, {outNode->nodeID, 0}}));
    ASSERT_TRUE(graph.addConnection({{vcaNode->nodeID, VCAModule::kRightBase}, {outNode->nodeID, 1}}));
    editor.updateComponents();

    setDualIOParam(*vcaNode->getProcessor(), false);

    EXPECT_TRUE(graphHasEdge(graph, vcaNode->nodeID, 0, outNode->nodeID, 0));
    EXPECT_TRUE(graphHasEdge(graph, vcaNode->nodeID, 0, outNode->nodeID, 1))
        << "collapsing must not leave the hardware's right channel unfed";
    EXPECT_FALSE(graphHasEdge(graph, vcaNode->nodeID, VCAModule::kRightBase, outNode->nodeID, 1));
}

TEST_F(GraphEditorTest, CollapsingDoesNotDoubleFeedACollapsedInputJack) {
    // The input-side mirror is deliberately NOT symmetric: our left input is already fed by the
    // same upstream, and re-pointing there too would sum L+R into one mono jack.
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    auto& graph = engine.getGraph();
    auto oscNode = graph.addNode(std::make_unique<OscillatorModule>());
    auto filterNode = graph.addNode(std::make_unique<FilterModule>());
    ASSERT_TRUE(graph.addConnection({{oscNode->nodeID, 0}, {filterNode->nodeID, 0}}));
    ASSERT_TRUE(graph.addConnection(
        {{oscNode->nodeID, OscillatorModule::kRightBase}, {filterNode->nodeID, FilterModule::kRightBase}}));
    editor.updateComponents();

    setDualIOParam(*filterNode->getProcessor(), false);

    EXPECT_EQ(feedCount(graph, filterNode->nodeID, 0), 1)
        << "the surviving mono input must keep exactly one feed, not L + R summed";
    EXPECT_EQ(feedCount(graph, filterNode->nodeID, FilterModule::kRightBase), 0);
}

TEST_F(GraphEditorTest, CollapsingIsOneUndoableStepIncludingTheRewire) {
    // The rewire rides inside the parameter gesture's own snapshot, so one undo puts back both the
    // parameter and every cable the collapse moved.
    AudioEngine engine;
    AppUndoManager undoMgr;
    GraphEditor editor(engine, &undoMgr);
    editor.setSize(800, 600);

    auto& graph = engine.getGraph();
    auto vcaNode = graph.addNode(std::make_unique<VCAModule>());
    auto distNode = graph.addNode(std::make_unique<DistortionModule>());
    ASSERT_TRUE(graph.addConnection({{vcaNode->nodeID, 0}, {distNode->nodeID, 0}}));
    ASSERT_TRUE(graph.addConnection({{vcaNode->nodeID, VCAModule::kRightBase}, {distNode->nodeID, 1}}));
    editor.updateComponents();
    ASSERT_NE(cardFor(editor, vcaNode->getProcessor()), nullptr) << "the card is what listens to the gesture";

    juce::AudioProcessorParameter* dualParam = nullptr;
    for (auto* p : vcaNode->getProcessor()->getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(p); withId && withId->paramID == "dualIO")
            dualParam = p;
    ASSERT_NE(dualParam, nullptr);

    dualParam->beginChangeGesture();
    dualParam->setValueNotifyingHost(0.0f);
    dualParam->endChangeGesture();

    ASSERT_FALSE(dynamic_cast<ModuleBase*>(vcaNode->getProcessor())->isDualIO());
    ASSERT_TRUE(graphHasEdge(graph, vcaNode->nodeID, 0, distNode->nodeID, 1)) << "the re-point happened";

    ASSERT_TRUE(undoMgr.undo());

    auto* restoredVca = [&]() -> juce::AudioProcessorGraph::Node* {
        for (auto* node : graph.getNodes())
            if (dynamic_cast<VCAModule*>(node->getProcessor()) != nullptr)
                return node;
        return nullptr;
    }();
    auto* restoredDist = [&]() -> juce::AudioProcessorGraph::Node* {
        for (auto* node : graph.getNodes())
            if (dynamic_cast<DistortionModule*>(node->getProcessor()) != nullptr)
                return node;
        return nullptr;
    }();
    ASSERT_NE(restoredVca, nullptr);
    ASSERT_NE(restoredDist, nullptr);

    EXPECT_TRUE(dynamic_cast<ModuleBase*>(restoredVca->getProcessor())->isDualIO()) << "undo restores the parameter";
    EXPECT_TRUE(graphHasEdge(graph, restoredVca->nodeID, VCAModule::kRightBase, restoredDist->nodeID, 1))
        << "undo restores the right-leg cable the collapse moved";
    EXPECT_FALSE(graphHasEdge(graph, restoredVca->nodeID, 0, restoredDist->nodeID, 1))
        << "and takes the re-pointed duplicate back out";
}

TEST_F(GraphEditorTest, SplittingIsOneUndoableStepIncludingTheRewire) {
    AudioEngine engine;
    AppUndoManager undoMgr;
    GraphEditor editor(engine, &undoMgr);
    editor.setSize(800, 600);

    auto& graph = engine.getGraph();
    auto vcaNode = graph.addNode(std::make_unique<VCAModule>());
    auto distNode = graph.addNode(std::make_unique<DistortionModule>());
    setDualIOParam(*vcaNode->getProcessor(), false);
    ASSERT_TRUE(graph.addConnection({{vcaNode->nodeID, 0}, {distNode->nodeID, 0}}));
    ASSERT_TRUE(graph.addConnection({{vcaNode->nodeID, 0}, {distNode->nodeID, 1}}));
    editor.updateComponents();

    juce::AudioProcessorParameter* dualParam = nullptr;
    for (auto* p : vcaNode->getProcessor()->getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(p); withId && withId->paramID == "dualIO")
            dualParam = p;
    ASSERT_NE(dualParam, nullptr);

    dualParam->beginChangeGesture();
    dualParam->setValueNotifyingHost(1.0f);
    dualParam->endChangeGesture();

    ASSERT_TRUE(graphHasEdge(graph, vcaNode->nodeID, VCAModule::kRightBase, distNode->nodeID, 1));
    ASSERT_FALSE(graphHasEdge(graph, vcaNode->nodeID, 0, distNode->nodeID, 1));

    ASSERT_TRUE(undoMgr.undo());

    juce::AudioProcessorGraph::NodeID vca, dist;
    for (auto* node : graph.getNodes()) {
        if (dynamic_cast<VCAModule*>(node->getProcessor()) != nullptr)
            vca = node->nodeID;
        if (dynamic_cast<DistortionModule*>(node->getProcessor()) != nullptr)
            dist = node->nodeID;
    }
    EXPECT_TRUE(graphHasEdge(graph, vca, 0, dist, 1)) << "undo puts the collapsed jack's duplicate back";
    EXPECT_FALSE(graphHasEdge(graph, vca, VCAModule::kRightBase, dist, 1));
}

// ---------------------------------------------------------------------------
// …and what all of the above is FOR: the rendered mix must not move sideways when Dual I/O flips.
// ---------------------------------------------------------------------------

namespace {
constexpr double kRenderSampleRate = 44100.0;
constexpr int kRenderBlockSize = 512;

struct StereoLevels {
    float left = 0.0f;
    float right = 0.0f;
};

StereoLevels renderStereoRms(juce::AudioProcessorGraph& graph, int totalSamples) {
    graph.prepareToPlay(kRenderSampleRate, kRenderBlockSize);

    for (auto* node : graph.getNodes())
        if (auto* kb = dynamic_cast<MidiKeyboardModule*>(node->getProcessor()))
            kb->getKeyboardState().noteOn(1, 60, 1.0f);

    juce::AudioBuffer<float> result(2, totalSamples);
    result.clear();
    int rendered = 0;
    while (rendered < totalSamples) {
        const int n = std::min(kRenderBlockSize, totalSamples - rendered);
        juce::AudioBuffer<float> block(2, n);
        block.clear();
        juce::MidiBuffer midi;
        graph.processBlock(block, midi);
        for (int ch = 0; ch < 2; ++ch)
            result.copyFrom(ch, rendered, block, ch, 0, n);
        rendered += n;
    }

    // Drop the first block: the note starts there and the FX tail has not filled yet.
    const int skip = std::min(kRenderBlockSize, totalSamples - 1);
    return {result.getRMSLevel(0, skip, totalSamples - skip), result.getRMSLevel(1, skip, totalSamples - skip)};
}
} // namespace

TEST_F(GraphEditorTest, CollapsingDualIOAcrossTheDefaultPatchKeepsLeftAndRightLevelsEqual) {
    // The reported bug, end to end: "turning Dual I/O off puts more sound on the left, which should
    // not happen". The default patch carries both legs from the Oscillator down to the FX tail
    // (Distortion → Delay → Reverb → Audio Output); collapsing every module used to drop the VCA's
    // right-leg cable into Distortion ch1 and leave every right channel after it silent.
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    auto& graph = engine.getGraph();
    graph.setPlayConfigDetails(0, 2, kRenderSampleRate, kRenderBlockSize);
    ASSERT_TRUE(synth::PresetManager::loadDefaultPreset(graph));
    editor.updateComponents();

    const auto before = renderStereoRms(graph, static_cast<int>(kRenderSampleRate));
    ASSERT_GT(before.left, 1.0e-4f) << "nothing was rendered, so nothing is proven";
    ASSERT_GT(before.right, 1.0e-4f) << "the patch is not stereo before the toggle";

    editor.applyDualIOToExistingModules(false);

    const auto after = renderStereoRms(graph, static_cast<int>(kRenderSampleRate));
    ASSERT_GT(after.left, 1.0e-4f) << "the collapse silenced the patch outright";
    EXPECT_GT(after.right, 1.0e-4f) << "the right channel went silent — the mix collapsed to the left";
    EXPECT_NEAR(after.right / after.left, 1.0f, 0.35f)
        << "L/R must stay level through a collapse (L=" << after.left << " R=" << after.right << ")";
}

TEST_F(GraphEditorTest, CollapsingDualIOKeepsAMonoChainBitEqualInBothChannels) {
    // The same claim with the Reverb's stereo width taken out of the picture: MIDI → Oscillator →
    // Distortion → Audio Output has no channel-dependent DSP at all, so after a collapse the two
    // output channels must be identical, not merely close. The Oscillator is the split-block end
    // here — collapsing it is what drops the kRightBase cable that feeds Distortion ch1.
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    auto& graph = engine.getGraph();
    graph.setPlayConfigDetails(0, 2, kRenderSampleRate, kRenderBlockSize);

    auto keysNode = graph.addNode(std::make_unique<MidiKeyboardModule>());
    auto oscNode = graph.addNode(std::make_unique<OscillatorModule>());
    auto distNode = graph.addNode(std::make_unique<DistortionModule>());
    auto outNode = graph.addNode(std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor>(
        juce::AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode));

    ASSERT_TRUE(graph.addConnection({{keysNode->nodeID, juce::AudioProcessorGraph::midiChannelIndex},
                                     {oscNode->nodeID, juce::AudioProcessorGraph::midiChannelIndex}}));
    ASSERT_TRUE(graph.addConnection({{oscNode->nodeID, 0}, {distNode->nodeID, 0}}));
    ASSERT_TRUE(graph.addConnection({{oscNode->nodeID, OscillatorModule::kRightBase}, {distNode->nodeID, 1}}));
    ASSERT_TRUE(graph.addConnection({{distNode->nodeID, 0}, {outNode->nodeID, 0}}));
    ASSERT_TRUE(graph.addConnection({{distNode->nodeID, 1}, {outNode->nodeID, 1}}));
    editor.updateComponents();

    editor.applyDualIOToExistingModules(false);

    const auto after = renderStereoRms(graph, kRenderBlockSize * 4);
    ASSERT_GT(after.left, 1.0e-4f);
    EXPECT_NEAR(after.right, after.left, 1.0e-6f)
        << "a collapsed mono chain must arrive at the same level in both output channels";
}

TEST_F(GraphEditorTest, ResolvePolyLinkFansCollapsedStereoSourceOntoStereoDest) {
    DelayModule src;
    DelayModule dst;

    auto toOutput = GraphEditor::resolvePolyLink(&src, 0, nullptr, 0);
    EXPECT_EQ(toOutput.sourceRawChannel, 0);
    EXPECT_EQ(toOutput.destRawChannel, 0);
    EXPECT_EQ(toOutput.voiceCount, 2);
    EXPECT_EQ(toOutput.sourceStride, 1);

    auto toFx = GraphEditor::resolvePolyLink(&src, 0, &dst, 0);
    EXPECT_EQ(toFx.voiceCount, 2);
    EXPECT_EQ(toFx.sourceStride, 1);
}

TEST_F(GraphEditorTest, ResolvePolyLinkDoesNotBroadcastAudioOrPitchFans) {
    // Broadcasting is limited to ModCV. Audio would build a paraphonic voice stack, and Pitch/Gate
    // would make all eight voices sound the same note — both stay single head-to-head wires.
    OscillatorModule monoOsc; // poly defaults to false
    FilterModule polyFilter;
    setPolyParam(polyFilter, true);

    auto audioLink = GraphEditor::resolvePolyLink(&monoOsc, 0, &polyFilter, 0);
    EXPECT_EQ(audioLink.voiceCount, 1);
    EXPECT_EQ(audioLink.sourceStride, 1);

    LFOModule lfo;
    OscillatorModule polyOsc;
    setPolyParam(polyOsc, true);

    auto pitchLink = GraphEditor::resolvePolyLink(&lfo, 0, &polyOsc, 0);
    EXPECT_EQ(pitchLink.voiceCount, 1);
    EXPECT_EQ(pitchLink.sourceStride, 1);

    ADSRModule polyAdsr;
    setPolyParam(polyAdsr, true);

    auto gateLink = GraphEditor::resolvePolyLink(&lfo, 0, &polyAdsr, 0);
    EXPECT_EQ(gateLink.voiceCount, 1);
    EXPECT_EQ(gateLink.sourceStride, 1);
}

// ---- Integration tests (full graph + drag/toggle interactions) ----

TEST_F(GraphEditorTest, DragBetweenPolyModulesFansOutAllVoices) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    auto adsrNode = engine.getGraph().addNode(std::make_unique<ADSRModule>());
    auto vcaNode = engine.getGraph().addNode(std::make_unique<VCAModule>());
    setPolyParam(*adsrNode->getProcessor(), true);
    setPolyParam(*vcaNode->getProcessor(), true);

    editor.updateComponents();

    ModuleComponent* adsrComp = nullptr;
    ModuleComponent* vcaComp = nullptr;

    auto* content = editor.getChildComponent(0);
    if (content) {
        for (auto* contentChild : content->getChildren()) {
            if (auto* mod = dynamic_cast<ModuleComponent*>(contentChild)) {
                if (mod->getModule() == adsrNode->getProcessor())
                    adsrComp = mod;
                if (mod->getModule() == vcaNode->getProcessor())
                    vcaComp = mod;
            }
        }
    }

    ASSERT_NE(adsrComp, nullptr);
    ASSERT_NE(vcaComp, nullptr);

    adsrComp->setBounds(0, 0, 100, 100);
    vcaComp->setBounds(200, 0, 100, 100);

    editor.beginConnectionDrag(adsrComp, 0, false, false, juce::Point<int>(0, 0));
    editor.dragConnection(juce::Point<int>(50, 0));

    auto vcaTargetPoint = vcaComp->getBounds().getPosition() + vcaComp->getPortCenter(2, true);
    editor.endConnectionDrag(vcaTargetPoint);

    auto& graph = engine.getGraph();

    for (int i = 0; i < 8; ++i) {
        bool found = false;
        for (auto& conn : graph.getConnections()) {
            if (conn.source.nodeID == adsrNode->nodeID && conn.source.channelIndex == i &&
                conn.destination.nodeID == vcaNode->nodeID && conn.destination.channelIndex == 8 + i) {
                found = true;
                break;
            }
        }
        EXPECT_TRUE(found) << "Missing fan connection for voice " << i;
    }

    int connectionCount = 0;
    for (auto& conn : graph.getConnections())
        if (conn.source.nodeID == adsrNode->nodeID && conn.destination.nodeID == vcaNode->nodeID)
            ++connectionCount;
    EXPECT_EQ(connectionCount, 8);

    bool foundAttenuverter = false;
    for (auto* node : graph.getNodes())
        if (dynamic_cast<AttenuverterModule*>(node->getProcessor()) != nullptr)
            foundAttenuverter = true;
    EXPECT_FALSE(foundAttenuverter);
}

TEST_F(GraphEditorTest, DragBetweenMonoModulesIsUnchanged) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    auto adsrNode = engine.getGraph().addNode(std::make_unique<ADSRModule>());
    auto vcaNode = engine.getGraph().addNode(std::make_unique<VCAModule>());

    editor.updateComponents();

    ModuleComponent* adsrComp = nullptr;
    ModuleComponent* vcaComp = nullptr;

    auto* content = editor.getChildComponent(0);
    if (content) {
        for (auto* contentChild : content->getChildren()) {
            if (auto* mod = dynamic_cast<ModuleComponent*>(contentChild)) {
                if (mod->getModule() == adsrNode->getProcessor())
                    adsrComp = mod;
                if (mod->getModule() == vcaNode->getProcessor())
                    vcaComp = mod;
            }
        }
    }

    ASSERT_NE(adsrComp, nullptr);
    ASSERT_NE(vcaComp, nullptr);

    adsrComp->setBounds(0, 0, 100, 100);
    vcaComp->setBounds(200, 0, 100, 100);

    editor.beginConnectionDrag(adsrComp, 0, false, false, juce::Point<int>(0, 0));
    editor.dragConnection(juce::Point<int>(50, 0));

    auto vcaTargetPoint = vcaComp->getBounds().getPosition() + vcaComp->getPortCenter(2, true);
    editor.endConnectionDrag(vcaTargetPoint);

    auto& graph = engine.getGraph();

    // Legacy behaviour: a mono mod-CV wire is mediated by an attenuverter, not a direct connection.
    bool foundAttenuverter = false;
    for (auto* node : graph.getNodes())
        if (dynamic_cast<AttenuverterModule*>(node->getProcessor()) != nullptr)
            foundAttenuverter = true;
    EXPECT_TRUE(foundAttenuverter);

    bool directConnectionFound = false;
    for (auto& conn : graph.getConnections())
        if (conn.source.nodeID == adsrNode->nodeID && conn.destination.nodeID == vcaNode->nodeID)
            directConnectionFound = true;
    EXPECT_FALSE(directConnectionFound);
}

TEST_F(GraphEditorTest, DisconnectPolyPortRemovesEntireFan) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    auto adsrNode = engine.getGraph().addNode(std::make_unique<ADSRModule>());
    auto vcaNode = engine.getGraph().addNode(std::make_unique<VCAModule>());
    setPolyParam(*adsrNode->getProcessor(), true);
    setPolyParam(*vcaNode->getProcessor(), true);

    editor.updateComponents();

    ModuleComponent* adsrComp = nullptr;
    ModuleComponent* vcaComp = nullptr;

    auto* content = editor.getChildComponent(0);
    if (content) {
        for (auto* contentChild : content->getChildren()) {
            if (auto* mod = dynamic_cast<ModuleComponent*>(contentChild)) {
                if (mod->getModule() == adsrNode->getProcessor())
                    adsrComp = mod;
                if (mod->getModule() == vcaNode->getProcessor())
                    vcaComp = mod;
            }
        }
    }

    ASSERT_NE(adsrComp, nullptr);
    ASSERT_NE(vcaComp, nullptr);

    adsrComp->setBounds(0, 0, 100, 100);
    vcaComp->setBounds(200, 0, 100, 100);

    editor.beginConnectionDrag(adsrComp, 0, false, false, juce::Point<int>(0, 0));
    editor.dragConnection(juce::Point<int>(50, 0));

    auto vcaTargetPoint = vcaComp->getBounds().getPosition() + vcaComp->getPortCenter(2, true);
    editor.endConnectionDrag(vcaTargetPoint);

    auto& graph = engine.getGraph();

    int preCount = 0;
    for (auto& conn : graph.getConnections())
        if (conn.source.nodeID == adsrNode->nodeID && conn.destination.nodeID == vcaNode->nodeID)
            ++preCount;
    ASSERT_EQ(preCount, 8) << "Setup must produce the 8-voice fan before disconnecting";

    editor.disconnectPort(vcaComp, 2, true, false);

    int postCount = 0;
    for (auto& conn : graph.getConnections())
        if (conn.source.nodeID == adsrNode->nodeID && conn.destination.nodeID == vcaNode->nodeID)
            ++postCount;
    EXPECT_EQ(postCount, 0);
}

// Poly MIDI's pitch fan carries raw Hz, not normalised CV. Wrapping it in an attenuverter scaled an
// absolute frequency AND fed an Hz-magnitude peak into the wire-activity metering, which multiplied
// the stroke width until the "wire" painted as a screen-filling blob.
TEST_F(GraphEditorTest, PitchSourceIsNeverWrappedInAnAttenuverter) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    auto polyMidiNode = engine.getGraph().addNode(std::make_unique<PolyMidiModule>());
    auto oscNode = engine.getGraph().addNode(std::make_unique<OscillatorModule>());
    // Mono oscillator: channel 0 is "Pitch", which getModulationTargets() lists as promotable.
    setPolyParam(*oscNode->getProcessor(), false);

    editor.updateComponents();

    ModuleComponent* polyMidiComp = nullptr;
    ModuleComponent* oscComp = nullptr;

    auto* content = editor.getChildComponent(0);
    if (content) {
        for (auto* contentChild : content->getChildren()) {
            if (auto* mod = dynamic_cast<ModuleComponent*>(contentChild)) {
                if (mod->getModule() == polyMidiNode->getProcessor())
                    polyMidiComp = mod;
                if (mod->getModule() == oscNode->getProcessor())
                    oscComp = mod;
            }
        }
    }

    ASSERT_NE(polyMidiComp, nullptr);
    ASSERT_NE(oscComp, nullptr);

    polyMidiComp->setBounds(0, 0, 100, 100);
    oscComp->setBounds(200, 0, 100, 100);

    editor.beginConnectionDrag(polyMidiComp, 0, false, false, juce::Point<int>(0, 0));
    editor.dragConnection(juce::Point<int>(50, 0));
    editor.endConnectionDrag(oscComp->getBounds().getPosition() + oscComp->getPortCenter(0, true));

    auto& graph = engine.getGraph();

    for (auto* node : graph.getNodes())
        EXPECT_EQ(dynamic_cast<AttenuverterModule*>(node->getProcessor()), nullptr)
            << "A pitch-role source must be wired direct, never through an attenuverter";

    bool foundDirect = false;
    for (auto& conn : graph.getConnections())
        if (conn.source.nodeID == polyMidiNode->nodeID && conn.destination.nodeID == oscNode->nodeID)
            foundDirect = true;
    EXPECT_TRUE(foundDirect) << "Poly MIDI should still connect directly to the oscillator's pitch input";
}

// Toggling poly off collapses the fan back through the same connect path, which is how the blob was
// originally triggered — it must not reintroduce an attenuverter on the pitch wire either.
TEST_F(GraphEditorTest, TogglingPolyOffKeepsPitchWireDirect) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    auto polyMidiNode = engine.getGraph().addNode(std::make_unique<PolyMidiModule>());
    auto oscNode = engine.getGraph().addNode(std::make_unique<OscillatorModule>());
    setPolyParam(*oscNode->getProcessor(), true);

    editor.updateComponents();

    ModuleComponent* polyMidiComp = nullptr;
    ModuleComponent* oscComp = nullptr;

    auto* content = editor.getChildComponent(0);
    if (content) {
        for (auto* contentChild : content->getChildren()) {
            if (auto* mod = dynamic_cast<ModuleComponent*>(contentChild)) {
                if (mod->getModule() == polyMidiNode->getProcessor())
                    polyMidiComp = mod;
                if (mod->getModule() == oscNode->getProcessor())
                    oscComp = mod;
            }
        }
    }

    ASSERT_NE(polyMidiComp, nullptr);
    ASSERT_NE(oscComp, nullptr);

    polyMidiComp->setBounds(0, 0, 100, 100);
    oscComp->setBounds(200, 0, 100, 100);

    editor.beginConnectionDrag(polyMidiComp, 0, false, false, juce::Point<int>(0, 0));
    editor.dragConnection(juce::Point<int>(50, 0));
    editor.endConnectionDrag(oscComp->getBounds().getPosition() + oscComp->getPortCenter(0, true));

    setPolyParam(*oscNode->getProcessor(), false);

    for (auto* node : engine.getGraph().getNodes())
        EXPECT_EQ(dynamic_cast<AttenuverterModule*>(node->getProcessor()), nullptr)
            << "Collapsing a pitch fan to mono must not insert an attenuverter";
}

TEST_F(GraphEditorTest, AudioIONodesAreSingletons) {
    EXPECT_TRUE(GraphEditor::isSingletonIOModule("Audio Output"));
    EXPECT_TRUE(GraphEditor::isSingletonIOModule("Audio Input"));
    EXPECT_FALSE(GraphEditor::isSingletonIOModule("Oscillator"));

    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    auto& graph = engine.getGraph();
    graph.clear();
    EXPECT_FALSE(GraphEditor::graphHasModuleNamed(graph, "Audio Output"));

    DummyDragSource dummySource;
    juce::var description("Audio Output");
    juce::DragAndDropTarget::SourceDetails details(description, &dummySource, juce::Point<int>(100, 100));

    // The first drop creates it — the module library offers Audio Output so a deleted one is
    // recoverable rather than stranding the patch with no way to hear it.
    editor.itemDropped(details);
    EXPECT_TRUE(GraphEditor::graphHasModuleNamed(graph, "Audio Output"));
    auto countAfterFirst = graph.getNodes().size();

    // A second drop must be a no-op: two output nodes would sum into the same device buffer.
    editor.itemDropped(details);
    EXPECT_EQ(graph.getNodes().size(), countAfterFirst) << "A duplicate Audio Output must not be created";
}

TEST_F(GraphEditorTest, DragMonoLfoOntoPolyVcaBroadcastsToEveryVoice) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    auto lfoNode = engine.getGraph().addNode(std::make_unique<LFOModule>());
    auto vcaNode = engine.getGraph().addNode(std::make_unique<VCAModule>());
    setPolyParam(*vcaNode->getProcessor(), true);

    editor.updateComponents();

    ModuleComponent* lfoComp = nullptr;
    ModuleComponent* vcaComp = nullptr;

    auto* content = editor.getChildComponent(0);
    if (content) {
        for (auto* contentChild : content->getChildren()) {
            if (auto* mod = dynamic_cast<ModuleComponent*>(contentChild)) {
                if (mod->getModule() == lfoNode->getProcessor())
                    lfoComp = mod;
                if (mod->getModule() == vcaNode->getProcessor())
                    vcaComp = mod;
            }
        }
    }

    ASSERT_NE(lfoComp, nullptr);
    ASSERT_NE(vcaComp, nullptr);

    lfoComp->setBounds(0, 0, 100, 100);
    vcaComp->setBounds(200, 0, 100, 100);

    editor.beginConnectionDrag(lfoComp, 0, false, false, juce::Point<int>(0, 0));
    editor.dragConnection(juce::Point<int>(50, 0));

    auto vcaTargetPoint = vcaComp->getBounds().getPosition() + vcaComp->getPortCenter(2, true);
    editor.endConnectionDrag(vcaTargetPoint);

    auto& graph = engine.getGraph();

    // Every voice's gain CV is fed from the SAME LFO output channel — that is the broadcast.
    for (int i = 0; i < 8; ++i) {
        bool found = false;
        for (auto& conn : graph.getConnections()) {
            if (conn.source.nodeID == lfoNode->nodeID && conn.source.channelIndex == 0 &&
                conn.destination.nodeID == vcaNode->nodeID && conn.destination.channelIndex == 8 + i) {
                found = true;
                break;
            }
        }
        EXPECT_TRUE(found) << "Missing broadcast connection for voice " << i;
    }

    int connectionCount = 0;
    for (auto& conn : graph.getConnections())
        if (conn.source.nodeID == lfoNode->nodeID && conn.destination.nodeID == vcaNode->nodeID)
            ++connectionCount;
    EXPECT_EQ(connectionCount, 8);

    // The broadcast must read back as ONE PolyBus wire with an x8 badge, not eight stacked wires.
    int polyBusCount = 0;
    for (const auto& r : engine.getModulationRoutings()) {
        if (r.kind == AudioEngine::RoutingKind::PolyBus && r.sourceNodeID == lfoNode->nodeID &&
            r.destNodeID == vcaNode->nodeID) {
            ++polyBusCount;
            EXPECT_EQ(r.voiceCount, 8);
            EXPECT_EQ(r.destChannelIndex, 8);
            EXPECT_EQ(r.role, PortRole::ModCV);
        }
    }
    EXPECT_EQ(polyBusCount, 1);

    int directCvCount = 0;
    for (const auto& r : engine.getModulationRoutings())
        if (r.kind == AudioEngine::RoutingKind::DirectCV && r.sourceNodeID == lfoNode->nodeID)
            ++directCvCount;
    EXPECT_EQ(directCvCount, 0) << "Broadcast edges should be consumed by the PolyBus collapse";
}

TEST_F(GraphEditorTest, TogglingPolyOnBroadcastsExistingMonoModWire) {
    // A mono LFO -> mono VCA CV wire (an attenuverter chain) must spread across all eight voices
    // when the VCA is switched to poly, rather than staying on voice 0.
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    auto lfoNode = engine.getGraph().addNode(std::make_unique<LFOModule>());
    auto vcaNode = engine.getGraph().addNode(std::make_unique<VCAModule>());

    editor.updateComponents();

    ModuleComponent* lfoComp = nullptr;
    ModuleComponent* vcaComp = nullptr;

    auto* content = editor.getChildComponent(0);
    if (content) {
        for (auto* contentChild : content->getChildren()) {
            if (auto* mod = dynamic_cast<ModuleComponent*>(contentChild)) {
                if (mod->getModule() == lfoNode->getProcessor())
                    lfoComp = mod;
                if (mod->getModule() == vcaNode->getProcessor())
                    vcaComp = mod;
            }
        }
    }

    ASSERT_NE(lfoComp, nullptr);
    ASSERT_NE(vcaComp, nullptr);

    lfoComp->setBounds(0, 0, 100, 100);
    vcaComp->setBounds(200, 0, 100, 100);

    editor.beginConnectionDrag(lfoComp, 0, false, false, juce::Point<int>(0, 0));
    editor.dragConnection(juce::Point<int>(50, 0));
    editor.endConnectionDrag(vcaComp->getBounds().getPosition() + vcaComp->getPortCenter(2, true));

    auto& graph = engine.getGraph();

    setPolyParam(*vcaNode->getProcessor(), true);

    for (int i = 0; i < 8; ++i) {
        bool found = false;
        for (auto& conn : graph.getConnections()) {
            if (conn.source.nodeID == lfoNode->nodeID && conn.source.channelIndex == 0 &&
                conn.destination.nodeID == vcaNode->nodeID && conn.destination.channelIndex == 8 + i) {
                found = true;
                break;
            }
        }
        EXPECT_TRUE(found) << "Missing broadcast connection for voice " << i << " after poly toggle";
    }

    bool foundAttenuverter = false;
    for (auto* node : graph.getNodes())
        if (dynamic_cast<AttenuverterModule*>(node->getProcessor()) != nullptr)
            foundAttenuverter = true;
    EXPECT_FALSE(foundAttenuverter) << "A poly fan is wired direct, so the attenuverter must be gone";
}

TEST_F(GraphEditorTest, TogglingPolyOnFansOutExistingConnection) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    auto oscNode = engine.getGraph().addNode(std::make_unique<OscillatorModule>());
    auto filterNode = engine.getGraph().addNode(std::make_unique<FilterModule>());

    editor.updateComponents();

    ModuleComponent* oscComp = nullptr;
    ModuleComponent* filterComp = nullptr;

    auto* content = editor.getChildComponent(0);
    if (content) {
        for (auto* contentChild : content->getChildren()) {
            if (auto* mod = dynamic_cast<ModuleComponent*>(contentChild)) {
                if (mod->getModule() == oscNode->getProcessor())
                    oscComp = mod;
                if (mod->getModule() == filterNode->getProcessor())
                    filterComp = mod;
            }
        }
    }

    ASSERT_NE(oscComp, nullptr);
    ASSERT_NE(filterComp, nullptr);

    oscComp->setBounds(0, 0, 100, 100);
    filterComp->setBounds(200, 0, 100, 100);

    editor.beginConnectionDrag(oscComp, 0, false, false, juce::Point<int>(0, 0));
    editor.dragConnection(juce::Point<int>(50, 0));

    auto filterTargetPoint = filterComp->getBounds().getPosition() + filterComp->getPortCenter(0, true);
    editor.endConnectionDrag(filterTargetPoint);

    auto& graph = engine.getGraph();

    int monoCount = 0;
    for (auto& conn : graph.getConnections())
        if (conn.source.nodeID == oscNode->nodeID && conn.source.channelIndex == 0 &&
            conn.destination.nodeID == filterNode->nodeID && conn.destination.channelIndex == 0)
            ++monoCount;
    ASSERT_EQ(monoCount, 1) << "Setup must produce a single mono connection before toggling poly";

    // Toggling poly on both ends must re-anchor the mono wire onto the 8-voice fan.
    setPolyParam(*oscNode->getProcessor(), true);
    setPolyParam(*filterNode->getProcessor(), true);

    for (int i = 0; i < 8; ++i) {
        bool found = false;
        for (auto& conn : graph.getConnections()) {
            if (conn.source.nodeID == oscNode->nodeID && conn.source.channelIndex == i &&
                conn.destination.nodeID == filterNode->nodeID && conn.destination.channelIndex == i) {
                found = true;
                break;
            }
        }
        EXPECT_TRUE(found) << "Missing fan connection for voice " << i;
    }

    int fanCount = 0;
    for (auto& conn : graph.getConnections())
        if (conn.source.nodeID == oscNode->nodeID && conn.destination.nodeID == filterNode->nodeID)
            ++fanCount;
    EXPECT_EQ(fanCount, 8);
}

TEST_F(GraphEditorTest, TogglingPolyOffCollapsesFanToSingleConnection) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    auto oscNode = engine.getGraph().addNode(std::make_unique<OscillatorModule>());
    auto filterNode = engine.getGraph().addNode(std::make_unique<FilterModule>());

    editor.updateComponents();

    ModuleComponent* oscComp = nullptr;
    ModuleComponent* filterComp = nullptr;

    auto* content = editor.getChildComponent(0);
    if (content) {
        for (auto* contentChild : content->getChildren()) {
            if (auto* mod = dynamic_cast<ModuleComponent*>(contentChild)) {
                if (mod->getModule() == oscNode->getProcessor())
                    oscComp = mod;
                if (mod->getModule() == filterNode->getProcessor())
                    filterComp = mod;
            }
        }
    }

    ASSERT_NE(oscComp, nullptr);
    ASSERT_NE(filterComp, nullptr);

    oscComp->setBounds(0, 0, 100, 100);
    filterComp->setBounds(200, 0, 100, 100);

    editor.beginConnectionDrag(oscComp, 0, false, false, juce::Point<int>(0, 0));
    editor.dragConnection(juce::Point<int>(50, 0));

    auto filterTargetPoint = filterComp->getBounds().getPosition() + filterComp->getPortCenter(0, true);
    editor.endConnectionDrag(filterTargetPoint);

    auto& graph = engine.getGraph();

    // Toggle poly ON for both to build the 8-voice fan.
    setPolyParam(*oscNode->getProcessor(), true);
    setPolyParam(*filterNode->getProcessor(), true);

    int fanCount = 0;
    for (auto& conn : graph.getConnections())
        if (conn.source.nodeID == oscNode->nodeID && conn.destination.nodeID == filterNode->nodeID)
            ++fanCount;
    ASSERT_EQ(fanCount, 8) << "Setup must produce the 8-voice fan before toggling poly off";

    // Toggle poly OFF for both — the fan must collapse back to a single mono wire.
    setPolyParam(*oscNode->getProcessor(), false);
    setPolyParam(*filterNode->getProcessor(), false);

    int monoCount = 0;
    for (auto& conn : graph.getConnections())
        if (conn.source.nodeID == oscNode->nodeID && conn.destination.nodeID == filterNode->nodeID)
            ++monoCount;
    EXPECT_EQ(monoCount, 1);

    bool foundMonoConn = false;
    for (auto& conn : graph.getConnections())
        if (conn.source.nodeID == oscNode->nodeID && conn.source.channelIndex == 0 &&
            conn.destination.nodeID == filterNode->nodeID && conn.destination.channelIndex == 0)
            foundMonoConn = true;
    EXPECT_TRUE(foundMonoConn);
}

TEST_F(GraphEditorTest, TogglingPolyMovesModCvWireOntoPolyChannels) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    auto adsrNode = engine.getGraph().addNode(std::make_unique<ADSRModule>());
    auto vcaNode = engine.getGraph().addNode(std::make_unique<VCAModule>());

    editor.updateComponents();

    ModuleComponent* adsrComp = nullptr;
    ModuleComponent* vcaComp = nullptr;

    auto* content = editor.getChildComponent(0);
    if (content) {
        for (auto* contentChild : content->getChildren()) {
            if (auto* mod = dynamic_cast<ModuleComponent*>(contentChild)) {
                if (mod->getModule() == adsrNode->getProcessor())
                    adsrComp = mod;
                if (mod->getModule() == vcaNode->getProcessor())
                    vcaComp = mod;
            }
        }
    }

    ASSERT_NE(adsrComp, nullptr);
    ASSERT_NE(vcaComp, nullptr);

    adsrComp->setBounds(0, 0, 100, 100);
    vcaComp->setBounds(200, 0, 100, 100);

    // Mono ADSR out jack0 -> mono VCA in jack1 (CV) creates an attenuverter chain.
    editor.beginConnectionDrag(adsrComp, 0, false, false, juce::Point<int>(0, 0));
    editor.dragConnection(juce::Point<int>(50, 0));

    auto vcaTargetPoint = vcaComp->getBounds().getPosition() + vcaComp->getPortCenter(2, true);
    editor.endConnectionDrag(vcaTargetPoint);

    auto& graph = engine.getGraph();

    bool foundAttenuverterBefore = false;
    for (auto* node : graph.getNodes())
        if (dynamic_cast<AttenuverterModule*>(node->getProcessor()) != nullptr)
            foundAttenuverterBefore = true;
    ASSERT_TRUE(foundAttenuverterBefore) << "Setup must create an attenuverter chain before toggling poly";

    // Toggling poly on both ends must collapse the attenuverter chain into a direct 8-voice fan
    // landing on raw channels 8-15 (VCA's poly CV bus), not the stale raw channel 1.
    setPolyParam(*adsrNode->getProcessor(), true);
    setPolyParam(*vcaNode->getProcessor(), true);

    bool foundAttenuverterAfter = false;
    for (auto* node : graph.getNodes())
        if (dynamic_cast<AttenuverterModule*>(node->getProcessor()) != nullptr)
            foundAttenuverterAfter = true;
    EXPECT_FALSE(foundAttenuverterAfter);

    for (int i = 0; i < 8; ++i) {
        bool found = false;
        for (auto& conn : graph.getConnections()) {
            if (conn.source.nodeID == adsrNode->nodeID && conn.source.channelIndex == i &&
                conn.destination.nodeID == vcaNode->nodeID && conn.destination.channelIndex == 8 + i) {
                found = true;
                break;
            }
        }
        EXPECT_TRUE(found) << "Missing direct fan connection for voice " << i;
    }

    int directCount = 0;
    for (auto& conn : graph.getConnections())
        if (conn.source.nodeID == adsrNode->nodeID && conn.destination.nodeID == vcaNode->nodeID)
            ++directCount;
    EXPECT_EQ(directCount, 8);
}

// ============================================================================
// Minimap (issue #159)
// ============================================================================

namespace {

// Hand-built MouseEvent, same pattern as MinimapComponentTests.cpp — no OS mouse source exists
// headlessly, but MouseInputSource is copyable and Desktop always exposes one.
juce::MouseEvent makeGraphEditorMouseEvent(juce::Component& comp, juce::Point<float> position) {
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), position, juce::ModifierKeys(), 0.0f,
                            0.0f, 0.0f, 0.0f, 0.0f, &comp, &comp, juce::Time::getCurrentTime(), position,
                            juce::Time::getCurrentTime(), 1, false);
}

// Maps a GraphEditor-local screen point to the canvas point currently under it, derived purely
// from getVisibleCanvasRect() (no access to the private pan/zoom state needed).
juce::Point<float> screenToCanvas(const GraphEditor& editor, juce::Point<float> screenPt) {
    const auto rect = editor.getVisibleCanvasRect();
    const auto w = static_cast<float>(editor.getWidth());
    const auto h = static_cast<float>(editor.getHeight());
    return {rect.getX() + (screenPt.x / w) * rect.getWidth(), rect.getY() + (screenPt.y / h) * rect.getHeight()};
}

} // namespace

// toggleMinimapVisibility() flips the reported preference each call.
TEST_F(GraphEditorTest, ToggleMinimapVisibilityFlipsState) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    ASSERT_TRUE(editor.isMinimapVisible());
    editor.toggleMinimapVisibility();
    EXPECT_FALSE(editor.isMinimapVisible());
    editor.toggleMinimapVisibility();
    EXPECT_TRUE(editor.isMinimapVisible());
}

// setMinimapVisible(false) actually hides the child component, not just the preference flag.
TEST_F(GraphEditorTest, SetMinimapVisibleFalseHidesChildComponent) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    ASSERT_TRUE(editor.getMinimap().isVisible());
    editor.setMinimapVisible(false);
    EXPECT_FALSE(editor.isMinimapVisible());
    EXPECT_FALSE(editor.getMinimap().isVisible());
}

// Below the 480x360 auto-hide threshold the minimap child must not be visible even though the
// user preference is untouched; growing back above the threshold restores it.
TEST_F(GraphEditorTest, MinimapAutoHidesBelowThresholdAndPreferenceSurvives) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);
    ASSERT_TRUE(editor.getMinimap().isVisible());

    editor.setSize(400, 300); // below kMinEditorW/H
    EXPECT_TRUE(editor.isMinimapVisible()) << "preference must survive an auto-hide";
    EXPECT_FALSE(editor.getMinimap().isVisible());

    editor.setSize(800, 600); // back above threshold
    EXPECT_TRUE(editor.isMinimapVisible());
    EXPECT_TRUE(editor.getMinimap().isVisible());
}

// getVisibleCanvasRect() at identity zoom/pan equals the editor's own local bounds.
TEST_F(GraphEditorTest, VisibleCanvasRectAtIdentityEqualsLocalBounds) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    const auto rect = editor.getVisibleCanvasRect();
    EXPECT_NEAR(rect.getX(), 0.0f, 0.01f);
    EXPECT_NEAR(rect.getY(), 0.0f, 0.01f);
    EXPECT_NEAR(rect.getWidth(), 800.0f, 0.01f);
    EXPECT_NEAR(rect.getHeight(), 600.0f, 0.01f);
}

// centreViewOn(p) must put p at the centre of the returned visible-canvas rect.
TEST_F(GraphEditorTest, CentreViewOnMovesViewportCentreToRequestedPoint) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    const juce::Point<float> target(1000.0f, -200.0f);
    editor.centreViewOn(target);

    const auto rect = editor.getVisibleCanvasRect();
    EXPECT_NEAR(rect.getCentreX(), target.x, 0.5f);
    EXPECT_NEAR(rect.getCentreY(), target.y, 0.5f);
}

// zoomAroundCentre's sign matches wheel-zoom direction: positive narrows the visible rect (zoom
// in), negative widens it (zoom out).
TEST_F(GraphEditorTest, ZoomAroundCentreMatchesWheelZoomDirection) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    const auto widthBefore = editor.getVisibleCanvasRect().getWidth();
    editor.zoomAroundCentre(1.0f);
    EXPECT_LT(editor.getVisibleCanvasRect().getWidth(), widthBefore);

    const auto widthBeforeOut = editor.getVisibleCanvasRect().getWidth();
    editor.zoomAroundCentre(-1.0f);
    EXPECT_GT(editor.getVisibleCanvasRect().getWidth(), widthBeforeOut);
}

// zoomAroundCentre keeps the canvas point at the viewport centre fixed while zooming.
TEST_F(GraphEditorTest, ZoomAroundCentreKeepsCanvasCentrePointFixed) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    const auto canvasCentreBefore = editor.getVisibleCanvasRect().getCentre();
    editor.zoomAroundCentre(1.0f);
    const auto canvasCentreAfter = editor.getVisibleCanvasRect().getCentre();

    EXPECT_NEAR(canvasCentreAfter.x, canvasCentreBefore.x, 0.5f);
    EXPECT_NEAR(canvasCentreAfter.y, canvasCentreBefore.y, 0.5f);
}

// Zoom stays clamped to [0.1, 2.0] no matter how many times it's driven in one direction.
TEST_F(GraphEditorTest, ZoomAroundCentreStaysClampedUnderRepeatedCalls) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    for (int i = 0; i < 200; ++i)
        editor.zoomAroundCentre(10.0f);
    EXPECT_NEAR(editor.getVisibleCanvasRect().getWidth(), 800.0f / 2.0f, 1.0f) << "zoom must clamp at 2.0";

    for (int i = 0; i < 200; ++i)
        editor.zoomAroundCentre(-10.0f);
    EXPECT_NEAR(editor.getVisibleCanvasRect().getWidth(), 800.0f / 0.1f, 1.0f) << "zoom must clamp at 0.1";
}

// Regression guard for the applyZoomAt extraction (shared by mouseWheelMove and
// zoomAroundCentre): a wheel event at an arbitrary screen position must still keep the canvas
// point under the cursor fixed, and must still actually change the zoom.
TEST_F(GraphEditorTest, WheelZoomKeepsCanvasPointUnderCursorFixed) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    // Start from a non-trivial pan so this isn't only exercising the identity case.
    editor.centreViewOn({300.0f, 250.0f});

    const juce::Point<float> cursor(150.0f, 400.0f);
    const auto canvasBefore = screenToCanvas(editor, cursor);
    const auto widthBefore = editor.getVisibleCanvasRect().getWidth();

    juce::MouseWheelDetails wheel{}; // value-init: the struct has no default member initialisers
    wheel.deltaY = 1.5f;
    editor.mouseWheelMove(makeGraphEditorMouseEvent(editor, cursor), wheel);

    const auto canvasAfter = screenToCanvas(editor, cursor);
    EXPECT_NEAR(canvasAfter.x, canvasBefore.x, 0.5f);
    EXPECT_NEAR(canvasAfter.y, canvasBefore.y, 0.5f);
    EXPECT_LT(editor.getVisibleCanvasRect().getWidth(), widthBefore) << "the wheel event must still have zoomed";
}

// buildMinimapModel() returns one node per rendered ModuleComponent, and a viewport equal to
// getVisibleCanvasRect().
TEST_F(GraphEditorTest, BuildMinimapModelReturnsOneNodePerModuleAndMatchingViewport) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    auto& graph = engine.getGraph();
    auto oscNode = graph.addNode(std::make_unique<OscillatorModule>());
    oscNode->properties.set("x", 50);
    oscNode->properties.set("y", 50);
    auto filterNode = graph.addNode(std::make_unique<FilterModule>());
    filterNode->properties.set("x", 400);
    filterNode->properties.set("y", 300);
    editor.updateComponents();

    const auto model = editor.buildMinimapModel();
    EXPECT_EQ(model.nodes.size(), 2u);
    EXPECT_TRUE(model.viewport == editor.getVisibleCanvasRect());
}

// --- Zoom-perf: cable memoization + zoom-gesture raster freeze --------------

namespace {
struct OscFilterVcaChain {
    ModuleComponent* osc = nullptr;
    ModuleComponent* filter = nullptr;
    ModuleComponent* vca = nullptr;
};

// Oscillator -> Filter and Oscillator -> VCA: 3 modules, 2 plain audio cables. Both cables leave
// the same source so no channel/poly mapping quirks are in play — just geometry to memoize.
OscFilterVcaChain buildOscFilterVcaChain(AudioEngine& engine, GraphEditor& editor) {
    auto& graph = engine.getGraph();
    auto oscNode = graph.addNode(std::make_unique<OscillatorModule>());
    auto filterNode = graph.addNode(std::make_unique<FilterModule>());
    auto vcaNode = graph.addNode(std::make_unique<VCAModule>());
    graph.addConnection({{oscNode->nodeID, 0}, {filterNode->nodeID, 0}});
    graph.addConnection({{oscNode->nodeID, 0}, {vcaNode->nodeID, 0}});
    editor.updateComponents();

    OscFilterVcaChain result;
    if (auto* content = editor.getChildComponent(0)) {
        for (auto* child : content->getChildren()) {
            if (auto* mc = dynamic_cast<ModuleComponent*>(child)) {
                if (mc->getModule() == oscNode->getProcessor())
                    result.osc = mc;
                else if (mc->getModule() == filterNode->getProcessor())
                    result.filter = mc;
                else if (mc->getModule() == vcaNode->getProcessor())
                    result.vca = mc;
            }
        }
    }
    // Explicit bounds (as the Dual I/O cable tests do above): a card's default constructed size
    // is enough to have real ports, but a known, non-overlapping layout keeps geometry legible.
    if (result.osc != nullptr)
        result.osc->setBounds(0, 0, 200, 200);
    if (result.filter != nullptr)
        result.filter->setBounds(300, 0, 200, 200);
    if (result.vca != nullptr)
        result.vca->setBounds(300, 300, 200, 200);
    return result;
}
} // namespace

// Cable geometry is canvas-space (zoom/pan-invariant): a zoom gesture must reuse the memoized
// list rather than rebuilding it, and the geometry it returns must not move at all.
TEST_F(GraphEditorTest, ZoomDoesNotRebuildTheCableList) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);
    auto chain = buildOscFilterVcaChain(engine, editor);
    ASSERT_NE(chain.osc, nullptr);
    ASSERT_NE(chain.filter, nullptr);
    ASSERT_NE(chain.vca, nullptr);

    const std::vector<GraphEditor::VisibleCable> before = editor.buildVisibleCables();
    ASSERT_EQ(before.size(), 2u);
    const int rebuildBefore = editor.getCableRebuildCountForTest();

    for (int i = 0; i < 20; ++i) {
        editor.zoomAroundCentre(0.05f);
        const auto& after = editor.buildVisibleCables();
        ASSERT_EQ(after.size(), before.size());
        for (size_t j = 0; j < after.size(); ++j) {
            EXPECT_FLOAT_EQ(after[j].p1.x, before[j].p1.x);
            EXPECT_FLOAT_EQ(after[j].p1.y, before[j].p1.y);
            EXPECT_FLOAT_EQ(after[j].p2.x, before[j].p2.x);
            EXPECT_FLOAT_EQ(after[j].p2.y, before[j].p2.y);
        }
    }
    EXPECT_EQ(editor.getCableRebuildCountForTest(), rebuildBefore)
        << "zoom must never touch the cable memo — zoom cannot move a cable";
}

// The 30 Hz tick is the ONLY thing that must keep the memo fresh absent an explicit graph edit
// (it is what re-reads activity/bypass values onto existing cables).
TEST_F(GraphEditorTest, TheThirtyHzTickInvalidatesTheCableCache) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);
    buildOscFilterVcaChain(engine, editor);

    editor.buildVisibleCables();
    const int before = editor.getCableRebuildCountForTest();
    editor.timerCallback();
    editor.buildVisibleCables();
    EXPECT_EQ(editor.getCableRebuildCountForTest(), before + 1);
}

// paint() and hit-testing must read the literal same list — the strengthened §14 invariant.
TEST_F(GraphEditorTest, PaintAndHitTestShareOneBuild) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);
    buildOscFilterVcaChain(engine, editor);

    const auto& cables = editor.buildVisibleCables();
    ASSERT_FALSE(cables.empty());
    const auto first = cables.front();
    const int rebuildAfterFirstBuild = editor.getCableRebuildCountForTest();

    // A point that lies exactly ON the drawn bezier, regardless of the card layout above.
    const auto path = GraphEditor::buildCablePath(first.p1, first.p2);
    const auto midpoint = path.getPointAlongPath(path.getLength() * 0.5f);

    const auto hit = editor.getCableAt(midpoint);
    ASSERT_TRUE(hit.has_value());
    EXPECT_TRUE(hit->id == first.id);
    EXPECT_EQ(editor.getCableRebuildCountForTest(), rebuildAfterFirstBuild)
        << "getCableAt must reuse the memoized list, not rebuild it";
}

// A graph edit (disconnect, or a node add via updateComponents()) invalidates the memo
// immediately — the NEXT build reflects it, but nothing rebuilds until asked.
TEST_F(GraphEditorTest, AGraphEditInvalidatesImmediately) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);
    buildOscFilterVcaChain(engine, editor);

    const std::vector<GraphEditor::VisibleCable> before = editor.buildVisibleCables();
    ASSERT_EQ(before.size(), 2u);
    const auto toRemove = before.front();
    const int rebuildBeforeDisconnect = editor.getCableRebuildCountForTest();

    editor.disconnectCable(toRemove);
    // Invalidated, not yet rebuilt: repaintCanvas() only drops the memo.
    EXPECT_EQ(editor.getCableRebuildCountForTest(), rebuildBeforeDisconnect);

    const auto& afterDisconnect = editor.buildVisibleCables();
    EXPECT_EQ(editor.getCableRebuildCountForTest(), rebuildBeforeDisconnect + 1);
    EXPECT_EQ(afterDisconnect.size(), 1u);
    for (const auto& c : afterDisconnect)
        EXPECT_FALSE(c.id == toRemove.id);

    // Same shape for updateComponents(): a node appearing invalidates immediately too.
    const int rebuildBeforeAdd = editor.getCableRebuildCountForTest();
    engine.getGraph().addNode(std::make_unique<VCAModule>());
    editor.updateComponents();
    EXPECT_EQ(editor.getCableRebuildCountForTest(), rebuildBeforeAdd);
    editor.buildVisibleCables();
    EXPECT_EQ(editor.getCableRebuildCountForTest(), rebuildBeforeAdd + 1);
}

// A zoom gesture freezes every card's raster scale; settling (here, forced via the test seam
// since the VBlank driver never ticks headless) thaws every card again.
TEST_F(GraphEditorTest, ZoomFreezesEveryCardThenSettleThaws) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);
    buildOscFilterVcaChain(engine, editor);

    EXPECT_FALSE(editor.isZoomGestureActive());
    editor.zoomAroundCentre(0.1f);
    EXPECT_TRUE(editor.isZoomGestureActive());
    ASSERT_FALSE(editor.getModuleComponents().isEmpty());
    for (auto* mc : editor.getModuleComponents())
        EXPECT_TRUE(mc->isRasterFrozen());

    editor.settleZoomNowForTest();
    EXPECT_FALSE(editor.isZoomGestureActive());
    for (auto* mc : editor.getModuleComponents())
        EXPECT_FALSE(mc->isRasterFrozen());
}

// A card created mid-gesture (paste/duplicate/drop while zooming) must join the freeze, or it
// rasterizes once at the pre-gesture scale and again at thaw instead of exactly once overall.
TEST_F(GraphEditorTest, ACardCreatedMidGestureJoinsTheFreeze) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);
    auto chain = buildOscFilterVcaChain(engine, editor);

    editor.zoomAroundCentre(0.1f);
    ASSERT_TRUE(editor.isZoomGestureActive());

    auto newNode = engine.getGraph().addNode(std::make_unique<VCAModule>());
    editor.updateComponents();

    ModuleComponent* newComp = nullptr;
    for (auto* mc : editor.getModuleComponents())
        if (mc->getModule() == newNode->getProcessor())
            newComp = mc;
    ASSERT_NE(newComp, nullptr);
    EXPECT_TRUE(newComp->isRasterFrozen());
}

// A wheel tick clamped at the [0.1, 2.0] ceiling/floor must not start (or refresh) a gesture, or
// cards at the extremes of the zoom range would be left soft forever (oldZoom == zoomLevel guard).
TEST_F(GraphEditorTest, AClampedZoomTickDoesNotStartAGesture) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    for (int i = 0; i < 200; ++i)
        editor.zoomAroundCentre(10.0f); // drive to the 2.0 ceiling
    editor.settleZoomNowForTest();
    ASSERT_FALSE(editor.isZoomGestureActive());

    editor.zoomAroundCentre(0.1f); // already clamped: must be a no-op on zoomLevel
    EXPECT_FALSE(editor.isZoomGestureActive());
}

// --- Smart connections -------------------------------------------------------

static int countAudioConnectionsBetween(juce::AudioProcessorGraph& graph, juce::AudioProcessorGraph::NodeID a,
                                        juce::AudioProcessorGraph::NodeID b) {
    int n = 0;
    for (const auto& c : graph.getConnections()) {
        if (c.source.isMIDI() || c.destination.isMIDI())
            continue;
        if ((c.source.nodeID == a && c.destination.nodeID == b) || (c.source.nodeID == b && c.destination.nodeID == a))
            ++n;
    }
    return n;
}

/** Cursor position that puts a library ghost's TOP-LEFT at `topLeft`. A library drag CENTRES the
 *  ghost on the cursor, so a test that wants the ghost at a particular spot has to say so in cursor
 *  terms rather than passing the top-left it used to. */
static juce::Point<int> libraryCursorForGhostTopLeft(const juce::String& moduleType, juce::Point<int> topLeft) {
    const auto size = GraphEditor::estimateModuleSize(moduleType);
    return topLeft + juce::Point<int>(size.x / 2, size.y / 2);
}

static void sizeModuleComponents(GraphEditor& editor, int w = 280, int h = 300) {
    auto* content = editor.getChildComponent(0);
    ASSERT_NE(content, nullptr);
    for (auto* child : content->getChildren()) {
        if (auto* mc = dynamic_cast<ModuleComponent*>(child))
            mc->setSize(w, h);
    }
}

TEST_F(GraphEditorTest, SmartConnectionOffDoesNotAutoWireOnDrop) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);
    editor.setSmartConnectionMode(GraphEditor::SmartConnectionMode::Off);

    auto& graph = engine.getGraph();
    auto filterNode = graph.addNode(std::make_unique<FilterModule>());
    filterNode->properties.set("x", 400);
    filterNode->properties.set("y", 100);
    editor.updateComponents();
    sizeModuleComponents(editor);

    DummyDragSource dummySource;
    juce::DragAndDropTarget::SourceDetails details(juce::var("Oscillator"), &dummySource,
                                                   libraryCursorForGhostTopLeft("Oscillator", {120, 120}));
    editor.itemDragEnter(details);
    editor.itemDragMove(details);
    EXPECT_EQ(editor.getSmartSuggestionCount(), 0);

    editor.itemDropped(details);
    juce::AudioProcessorGraph::NodeID oscId{};
    for (auto* node : graph.getNodes()) {
        if (node->getProcessor()->getName() == "Oscillator")
            oscId = node->nodeID;
    }
    ASSERT_NE(oscId.uid, 0u);
    EXPECT_EQ(countAudioConnectionsBetween(graph, oscId, filterNode->nodeID), 0);
}

TEST_F(GraphEditorTest, SmartConnectionSuggestsNearCompatibleNeighbor) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);
    editor.setSmartConnectionMode(GraphEditor::SmartConnectionMode::NewAndUnwired);

    auto& graph = engine.getGraph();
    auto filterNode = graph.addNode(std::make_unique<FilterModule>());
    filterNode->properties.set("x", 360);
    filterNode->properties.set("y", 100);
    editor.updateComponents();
    sizeModuleComponents(editor);

    DummyDragSource dummySource;
    // Drop point near the filter (within the 96 px proximity window after anti-overlap).
    juce::DragAndDropTarget::SourceDetails details(juce::var("Oscillator"), &dummySource,
                                                   libraryCursorForGhostTopLeft("Oscillator", {80, 100}));
    editor.itemDragEnter(details);
    editor.itemDragMove(details);

    EXPECT_GT(editor.getSmartSuggestionCount(), 0) << "Oscillator ghost near a Filter should suggest an audio cable";

    // Far away — suggestions clear.
    juce::DragAndDropTarget::SourceDetails far(juce::var("Oscillator"), &dummySource,
                                               libraryCursorForGhostTopLeft("Oscillator", {50, 500}));
    editor.itemDragMove(far);
    EXPECT_EQ(editor.getSmartSuggestionCount(), 0);
    editor.endDragPreview();
}

TEST_F(GraphEditorTest, SmartConnectionNewDropAutoWires) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);
    editor.setSmartConnectionMode(GraphEditor::SmartConnectionMode::NewOnly);

    auto& graph = engine.getGraph();
    auto filterNode = graph.addNode(std::make_unique<FilterModule>());
    filterNode->properties.set("x", 360);
    filterNode->properties.set("y", 100);
    editor.updateComponents();
    sizeModuleComponents(editor);

    DummyDragSource dummySource;
    juce::DragAndDropTarget::SourceDetails details(juce::var("Oscillator"), &dummySource,
                                                   libraryCursorForGhostTopLeft("Oscillator", {80, 100}));
    editor.itemDragEnter(details);
    editor.itemDragMove(details);
    ASSERT_GT(editor.getSmartSuggestionCount(), 0);
    editor.itemDropped(details);

    juce::AudioProcessorGraph::NodeID oscId{};
    for (auto* node : graph.getNodes()) {
        if (node->getProcessor()->getName() == "Oscillator")
            oscId = node->nodeID;
    }
    ASSERT_NE(oscId.uid, 0u);
    EXPECT_GT(countAudioConnectionsBetween(graph, oscId, filterNode->nodeID), 0);
}

TEST_F(GraphEditorTest, SmartConnectionNewOnlyDoesNotWireOnUnwiredMove) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);
    editor.setSmartConnectionMode(GraphEditor::SmartConnectionMode::NewOnly);

    auto& graph = engine.getGraph();
    auto oscNode = graph.addNode(std::make_unique<OscillatorModule>());
    oscNode->properties.set("x", 40);
    oscNode->properties.set("y", 100);
    auto filterNode = graph.addNode(std::make_unique<FilterModule>());
    filterNode->properties.set("x", 400);
    filterNode->properties.set("y", 100);
    editor.updateComponents();
    sizeModuleComponents(editor);

    ModuleComponent* oscComp = nullptr;
    for (auto* c : editor.getModuleComponents()) {
        if (c->getNodeId() == oscNode->nodeID)
            oscComp = c;
    }
    ASSERT_NE(oscComp, nullptr);

    editor.beginDragPreview(oscComp->getWidth(), oscComp->getHeight(), oscComp->getNodeId());
    editor.updateDragPreview({280, 100}); // slide near filter
    EXPECT_EQ(editor.getSmartSuggestionCount(), 0) << "NewOnly must not suggest on moves";
    editor.finalizeModuleDrag(oscComp);
    editor.endDragPreview();
    EXPECT_EQ(countAudioConnectionsBetween(graph, oscNode->nodeID, filterNode->nodeID), 0);
}

TEST_F(GraphEditorTest, SmartConnectionNewAndUnwiredWiresUnwiredMove) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);
    editor.setSmartConnectionMode(GraphEditor::SmartConnectionMode::NewAndUnwired);

    auto& graph = engine.getGraph();
    auto oscNode = graph.addNode(std::make_unique<OscillatorModule>());
    oscNode->properties.set("x", 40);
    oscNode->properties.set("y", 100);
    auto filterNode = graph.addNode(std::make_unique<FilterModule>());
    filterNode->properties.set("x", 400);
    filterNode->properties.set("y", 100);
    editor.updateComponents();
    sizeModuleComponents(editor);

    ModuleComponent* oscComp = nullptr;
    for (auto* c : editor.getModuleComponents()) {
        if (c->getNodeId() == oscNode->nodeID)
            oscComp = c;
    }
    ASSERT_NE(oscComp, nullptr);
    EXPECT_FALSE(editor.nodeHasCables(oscNode->nodeID));

    editor.beginDragPreview(oscComp->getWidth(), oscComp->getHeight(), oscComp->getNodeId());
    // Land just left of the Filter so output/input jacks face each other (not overlapping).
    editor.updateDragPreview({100, 100});
    ASSERT_GT(editor.getSmartSuggestionCount(), 0);
    editor.finalizeModuleDrag(oscComp);
    editor.endDragPreview();
    EXPECT_GT(countAudioConnectionsBetween(graph, oscNode->nodeID, filterNode->nodeID), 0);
}

TEST_F(GraphEditorTest, SmartConnectionNewAndUnwiredSkipsAlreadyWiredMove) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1000, 600);
    editor.setSmartConnectionMode(GraphEditor::SmartConnectionMode::NewAndUnwired);

    auto& graph = engine.getGraph();
    auto oscNode = graph.addNode(std::make_unique<OscillatorModule>());
    oscNode->properties.set("x", 40);
    oscNode->properties.set("y", 100);
    auto filterNode = graph.addNode(std::make_unique<FilterModule>());
    filterNode->properties.set("x", 400);
    filterNode->properties.set("y", 100);
    auto vcaNode = graph.addNode(std::make_unique<VCAModule>());
    vcaNode->properties.set("x", 700);
    vcaNode->properties.set("y", 100);
    editor.updateComponents();
    sizeModuleComponents(editor);

    // Pre-wire Osc -> Filter so the oscillator is no longer "unwired".
    editor.connectPorts(oscNode->nodeID, 0, filterNode->nodeID, 0, false, false);
    ASSERT_TRUE(editor.nodeHasCables(oscNode->nodeID));

    ModuleComponent* oscComp = nullptr;
    for (auto* c : editor.getModuleComponents()) {
        if (c->getNodeId() == oscNode->nodeID)
            oscComp = c;
    }
    ASSERT_NE(oscComp, nullptr);

    editor.beginDragPreview(oscComp->getWidth(), oscComp->getHeight(), oscComp->getNodeId());
    editor.updateDragPreview({560, 100}); // near VCA
    EXPECT_EQ(editor.getSmartSuggestionCount(), 0);
    editor.finalizeModuleDrag(oscComp);
    editor.endDragPreview();
    EXPECT_EQ(countAudioConnectionsBetween(graph, oscNode->nodeID, vcaNode->nodeID), 0);
}

TEST_F(GraphEditorTest, SmartConnectionDoesNotWrapAroundToRightNeighbor) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1000, 600);
    editor.setSmartConnectionMode(GraphEditor::SmartConnectionMode::NewAndUnwired);

    auto& graph = engine.getGraph();
    auto filterNode = graph.addNode(std::make_unique<FilterModule>());
    filterNode->properties.set("x", 40);
    filterNode->properties.set("y", 100);
    auto delayNode = graph.addNode(std::make_unique<DelayModule>());
    delayNode->properties.set("x", 400);
    delayNode->properties.set("y", 100);
    editor.updateComponents();
    sizeModuleComponents(editor);

    ModuleComponent* filterComp = nullptr;
    for (auto* c : editor.getModuleComponents()) {
        if (c->getNodeId() == filterNode->nodeID)
            filterComp = c;
    }
    ASSERT_NE(filterComp, nullptr);

    editor.beginDragPreview(filterComp->getWidth(), filterComp->getHeight(), filterComp->getNodeId());
    editor.updateDragPreview({100, 100}); // slide toward the Delay on the right
    for (const auto& s : editor.getSmartSuggestions()) {
        EXPECT_TRUE(s.ghostIsSource) << "must not wrap Delay's right outputs into Filter's left inputs";
        EXPECT_FALSE(s.isMidi);
        EXPECT_EQ(s.neighborId, delayNode->nodeID);
    }
    editor.endDragPreview();
}

TEST_F(GraphEditorTest, SmartConnectionNewAndUnwiredWiresFreeOutputDespiteOtherCables) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1200, 600);
    editor.setSmartConnectionMode(GraphEditor::SmartConnectionMode::NewAndUnwired);

    auto& graph = engine.getGraph();
    auto oscNode = graph.addNode(std::make_unique<OscillatorModule>());
    oscNode->properties.set("x", 40);
    oscNode->properties.set("y", 100);
    auto filterNode = graph.addNode(std::make_unique<FilterModule>());
    filterNode->properties.set("x", 400);
    filterNode->properties.set("y", 100);
    auto delayNode = graph.addNode(std::make_unique<DelayModule>());
    delayNode->properties.set("x", 760);
    delayNode->properties.set("y", 100);
    editor.updateComponents();
    sizeModuleComponents(editor);

    editor.connectPorts(oscNode->nodeID, 0, filterNode->nodeID, 0, false, false);
    ASSERT_TRUE(editor.nodeHasCables(filterNode->nodeID));

    ModuleComponent* filterComp = nullptr;
    for (auto* c : editor.getModuleComponents()) {
        if (c->getNodeId() == filterNode->nodeID)
            filterComp = c;
    }
    ASSERT_NE(filterComp, nullptr);

    editor.beginDragPreview(filterComp->getWidth(), filterComp->getHeight(), filterComp->getNodeId());
    editor.updateDragPreview({420, 100}); // near Delay; Filter audio in is taken, audio out is free
    ASSERT_GT(editor.getSmartSuggestionCount(), 0)
        << "NewAndUnwired should still offer Filter → Delay when the output jack is free";
    for (const auto& s : editor.getSmartSuggestions()) {
        EXPECT_TRUE(s.ghostIsSource);
        EXPECT_EQ(s.neighborId, delayNode->nodeID);
    }
    editor.finalizeModuleDrag(filterComp);
    editor.endDragPreview();
    EXPECT_GT(countAudioConnectionsBetween(graph, filterNode->nodeID, delayNode->nodeID), 0);
}

TEST_F(GraphEditorTest, SmartConnectionAllMovesCanAddWireToFreeJack) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1000, 600);
    editor.setSmartConnectionMode(GraphEditor::SmartConnectionMode::AllMoves);

    auto& graph = engine.getGraph();
    auto oscNode = graph.addNode(std::make_unique<OscillatorModule>());
    oscNode->properties.set("x", 40);
    oscNode->properties.set("y", 100);
    auto filterNode = graph.addNode(std::make_unique<FilterModule>());
    filterNode->properties.set("x", 400);
    filterNode->properties.set("y", 100);
    auto vcaNode = graph.addNode(std::make_unique<VCAModule>());
    vcaNode->properties.set("x", 700);
    vcaNode->properties.set("y", 100);
    editor.updateComponents();
    sizeModuleComponents(editor);

    editor.connectPorts(oscNode->nodeID, 0, filterNode->nodeID, 0, false, false);

    ModuleComponent* oscComp = nullptr;
    for (auto* c : editor.getModuleComponents()) {
        if (c->getNodeId() == oscNode->nodeID)
            oscComp = c;
    }
    ASSERT_NE(oscComp, nullptr);

    editor.beginDragPreview(oscComp->getWidth(), oscComp->getHeight(), oscComp->getNodeId());
    editor.updateDragPreview({560, 100});
    // Osc already feeds Filter; a free VCA audio in can still be suggested under AllMoves.
    if (editor.getSmartSuggestionCount() > 0) {
        editor.finalizeModuleDrag(oscComp);
        editor.endDragPreview();
        EXPECT_GT(countAudioConnectionsBetween(graph, oscNode->nodeID, vcaNode->nodeID), 0);
    } else {
        // Acceptable if heuristics prefer not to dual-route the same output; AllMoves still
        // must not crash and must leave the existing Filter wire intact.
        editor.endDragPreview();
        EXPECT_GT(countAudioConnectionsBetween(graph, oscNode->nodeID, filterNode->nodeID), 0);
    }
}

TEST_F(GraphEditorTest, SmartConnectionIncompatiblePairSuggestsNothing) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);
    editor.setSmartConnectionMode(GraphEditor::SmartConnectionMode::NewAndUnwired);

    auto& graph = engine.getGraph();
    auto filterNode = graph.addNode(std::make_unique<FilterModule>());
    filterNode->properties.set("x", 360);
    filterNode->properties.set("y", 100);
    editor.updateComponents();
    sizeModuleComponents(editor);

    // Occupy the Filter's audio input so remaining free inputs are mod-CV (skipped in v1).
    auto oscNode = graph.addNode(std::make_unique<OscillatorModule>());
    oscNode->properties.set("x", 40);
    oscNode->properties.set("y", 400);
    editor.updateComponents();
    sizeModuleComponents(editor);
    editor.connectPorts(oscNode->nodeID, 0, filterNode->nodeID, 0, false, false);

    DummyDragSource dummySource;
    juce::DragAndDropTarget::SourceDetails details(juce::var("LFO"), &dummySource,
                                                   libraryCursorForGhostTopLeft("LFO", {80, 100}));
    editor.itemDragEnter(details);
    editor.itemDragMove(details);
    // LFO is not a known MIDI source; Filter audio in is taken; mod CV is not suggested in v1.
    EXPECT_EQ(editor.getSmartSuggestionCount(), 0);
    editor.endDragPreview();
}

TEST_F(GraphEditorTest, SmartConnectionModeRoundTrip) {
    EXPECT_EQ(GraphEditor::smartConnectionModeFromString("Off"), GraphEditor::SmartConnectionMode::Off);
    EXPECT_EQ(GraphEditor::smartConnectionModeFromString("NewOnly"), GraphEditor::SmartConnectionMode::NewOnly);
    EXPECT_EQ(GraphEditor::smartConnectionModeFromString("AllMoves"), GraphEditor::SmartConnectionMode::AllMoves);
    EXPECT_EQ(GraphEditor::smartConnectionModeFromString("NewAndUnwired"),
              GraphEditor::SmartConnectionMode::NewAndUnwired);
    EXPECT_EQ(GraphEditor::smartConnectionModeFromString("bogus"), GraphEditor::SmartConnectionMode::NewAndUnwired);
    EXPECT_EQ(GraphEditor::smartConnectionModeToString(GraphEditor::SmartConnectionMode::Off), "Off");
}

TEST_F(GraphEditorTest, SmartConnectionStereoToStereoWiresBothLegs) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1000, 600);
    editor.setSmartConnectionMode(GraphEditor::SmartConnectionMode::NewAndUnwired);

    auto& graph = engine.getGraph();
    auto reverbNode = graph.addNode(std::make_unique<ReverbModule>());
    reverbNode->properties.set("x", 400);
    reverbNode->properties.set("y", 100);
    editor.updateComponents();
    sizeModuleComponents(editor);

    DummyDragSource dummySource;
    juce::DragAndDropTarget::SourceDetails details(juce::var("Delay"), &dummySource,
                                                   libraryCursorForGhostTopLeft("Delay", {80, 100}));
    editor.itemDragEnter(details);
    editor.itemDragMove(details);
    ASSERT_GE(editor.getSmartSuggestionCount(), 1) << "Dual I/O off: one Audio→Audio preview, which fans both raw legs";
    editor.itemDropped(details);

    juce::AudioProcessorGraph::NodeID delayId{};
    for (auto* node : graph.getNodes()) {
        if (node->getProcessor()->getName() == "Delay")
            delayId = node->nodeID;
    }
    ASSERT_NE(delayId.uid, 0u);

    // Collapsed Audio jacks still own raw L/R — one visible cable, two graph edges.
    EXPECT_TRUE(graph.isConnected({{delayId, 0}, {reverbNode->nodeID, 0}}));
    EXPECT_TRUE(graph.isConnected({{delayId, 1}, {reverbNode->nodeID, 1}}));
}

TEST_F(GraphEditorTest, SmartConnectionMonoToStereoFansBothInputs) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1000, 600);
    editor.setSmartConnectionMode(GraphEditor::SmartConnectionMode::NewAndUnwired);

    auto& graph = engine.getGraph();
    auto delayNode = graph.addNode(std::make_unique<DelayModule>());
    delayNode->properties.set("x", 400);
    delayNode->properties.set("y", 100);
    editor.updateComponents();
    sizeModuleComponents(editor);

    DummyDragSource dummySource;
    juce::DragAndDropTarget::SourceDetails details(juce::var("Oscillator"), &dummySource,
                                                   libraryCursorForGhostTopLeft("Oscillator", {80, 100}));
    editor.itemDragEnter(details);
    editor.itemDragMove(details);
    ASSERT_GE(editor.getSmartSuggestionCount(), 1) << "Mono→collapsed stereo should preview Delay's Audio jack";
    editor.itemDropped(details);

    juce::AudioProcessorGraph::NodeID oscId{};
    for (auto* node : graph.getNodes()) {
        if (node->getProcessor()->getName() == "Oscillator")
            oscId = node->nodeID;
    }
    ASSERT_NE(oscId.uid, 0u);
    EXPECT_TRUE(graph.isConnected({{oscId, 0}, {delayNode->nodeID, 0}}));
    EXPECT_TRUE(graph.isConnected({{oscId, 0}, {delayNode->nodeID, 1}}));
}

TEST_F(GraphEditorTest, SmartConnectionStereoToMonoFansBothOutputs) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1000, 600);
    editor.setSmartConnectionMode(GraphEditor::SmartConnectionMode::NewAndUnwired);

    auto& graph = engine.getGraph();
    auto filterNode = graph.addNode(std::make_unique<FilterModule>());
    filterNode->properties.set("x", 400);
    filterNode->properties.set("y", 100);
    editor.updateComponents();
    sizeModuleComponents(editor);

    DummyDragSource dummySource;
    juce::DragAndDropTarget::SourceDetails details(juce::var("Delay"), &dummySource,
                                                   libraryCursorForGhostTopLeft("Delay", {80, 100}));
    editor.itemDragEnter(details);
    editor.itemDragMove(details);
    ASSERT_GE(editor.getSmartSuggestionCount(), 1) << "Collapsed stereo→mono should preview Delay Audio into Filter";
    editor.itemDropped(details);

    juce::AudioProcessorGraph::NodeID delayId{};
    for (auto* node : graph.getNodes()) {
        if (node->getProcessor()->getName() == "Delay")
            delayId = node->nodeID;
    }
    ASSERT_NE(delayId.uid, 0u);
    EXPECT_TRUE(graph.isConnected({{delayId, 0}, {filterNode->nodeID, 0}}));
    EXPECT_FALSE(graph.isConnected({{delayId, 1}, {filterNode->nodeID, 0}}))
        << "Filter ch1 is Cutoff, not a second audio input — Dual I/O off must not dump Right onto it";
}

TEST_F(GraphEditorTest, SmartConnectionDoesNotTreatMathABAsStereo) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1000, 600);
    editor.setSmartConnectionMode(GraphEditor::SmartConnectionMode::NewAndUnwired);

    auto& graph = engine.getGraph();
    auto mathNode = graph.addNode(std::make_unique<MathModule>());
    mathNode->properties.set("x", 400);
    mathNode->properties.set("y", 100);
    editor.updateComponents();
    sizeModuleComponents(editor);

    DummyDragSource dummySource;
    juce::DragAndDropTarget::SourceDetails details(juce::var("Oscillator"), &dummySource,
                                                   libraryCursorForGhostTopLeft("Oscillator", {80, 100}));
    editor.itemDragEnter(details);
    editor.itemDragMove(details);
    // Math A/B are unlabeled PortRole::Other, so they are never a stereo destination pair. The
    // Oscillator became a stereo SOURCE in #219 (Audio L/R), so it may legitimately offer both legs
    // — but every one of them must land on Math A. B is a second operand, not a right channel.
    const int suggestions = editor.getSmartSuggestionCount();
    EXPECT_LE(suggestions, 2);
    if (suggestions >= 1) {
        editor.itemDropped(details);
        juce::AudioProcessorGraph::NodeID oscId{};
        for (auto* node : graph.getNodes()) {
            if (node->getProcessor()->getName() == "Oscillator")
                oscId = node->nodeID;
        }
        ASSERT_NE(oscId.uid, 0u);

        bool anyIntoB = false;
        for (const auto& conn : graph.getConnections())
            if (conn.source.nodeID == oscId && conn.destination.nodeID == mathNode->nodeID &&
                conn.destination.channelIndex == 1)
                anyIntoB = true;
        EXPECT_FALSE(anyIntoB) << "Math B is a second operand, not a right audio channel";
        EXPECT_TRUE(graph.isConnected({{oscId, 0}, {mathNode->nodeID, 0}})) << "Audio L should reach Math A";
    } else {
        editor.endDragPreview();
    }
}

TEST_F(GraphEditorTest, SmartConnectionMonoToStereoIsBothOrNeither) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1000, 600);
    editor.setSmartConnectionMode(GraphEditor::SmartConnectionMode::NewAndUnwired);

    auto& graph = engine.getGraph();
    auto delayNode = graph.addNode(std::make_unique<DelayModule>());
    delayNode->properties.set("x", 400);
    delayNode->properties.set("y", 100);
    editor.updateComponents();
    sizeModuleComponents(editor);

    // Occupy Delay Left only.
    auto filler = graph.addNode(std::make_unique<OscillatorModule>());
    filler->properties.set("x", 40);
    filler->properties.set("y", 400);
    editor.updateComponents();
    sizeModuleComponents(editor);
    editor.connectPorts(filler->nodeID, 0, delayNode->nodeID, 0, false, false);

    DummyDragSource dummySource;
    juce::DragAndDropTarget::SourceDetails details(juce::var("Oscillator"), &dummySource,
                                                   libraryCursorForGhostTopLeft("Oscillator", {80, 100}));
    editor.itemDragEnter(details);
    editor.itemDragMove(details);
    // Left taken → both-or-neither: no mono→stereo fan onto Right alone.
    EXPECT_EQ(editor.getSmartSuggestionCount(), 0);
    editor.endDragPreview();
}

// --- Occupied audio destinations: parallel add vs. Ctrl insert-in-series ------
// Default (no modifier): the terminal audio sink accepts an ADDITIVE parallel cable, because it is
// wired in essentially every real patch and summing into the mix bus is what a hand-dragged cable
// there already does. Every other occupied destination stays a hard stop.
// Ctrl held: INSERT IN SERIES at ANY module — the upstream cabling is rerouted through the ghost.
// Ctrl is sampled live per drag tick, never latched at mouse-down (see isInsertModifierDown).

/** Adds the graph's terminal audio sink the way AudioEngine does. The channel layout has to be set
 *  BEFORE the node is added: AudioGraphIOProcessor snapshots it once, in setParentGraph, and an
 *  unconfigured graph reports 0 channels — every connection into the node would then be rejected. */
static juce::AudioProcessorGraph::Node::Ptr addAudioOutputNode(juce::AudioProcessorGraph& graph, int x, int y) {
    using IOProcessor = juce::AudioProcessorGraph::AudioGraphIOProcessor;
    graph.setPlayConfigDetails(0, 2, 44100.0, 512);
    auto node = graph.addNode(std::make_unique<IOProcessor>(IOProcessor::audioOutputNode));
    node->properties.set("x", x);
    node->properties.set("y", y);
    return node;
}

static juce::AudioProcessorGraph::NodeID findNodeIdByName(juce::AudioProcessorGraph& graph, const juce::String& name) {
    for (auto* node : graph.getNodes())
        if (node->getProcessor() != nullptr && node->getProcessor()->getName() == name)
            return node->nodeID;
    return {};
}

/** Sink at (760,100) fed on both legs by a collapsed Reverb at (40,100) — the factory-preset shape.
 *  Leaves room for a 280px-wide ghost at x=440 to sit just left of the sink. */
struct WiredSinkFixture {
    juce::AudioProcessorGraph::Node::Ptr outNode, reverbNode;
    juce::AudioProcessorGraph::NodeID outId, reverbId;
};
static WiredSinkFixture makeWiredSink(AudioEngine& engine, GraphEditor& editor) {
    WiredSinkFixture f;
    auto& graph = engine.getGraph();
    f.outNode = addAudioOutputNode(graph, 760, 100);
    f.reverbNode = graph.addNode(std::make_unique<ReverbModule>());
    f.reverbNode->properties.set("x", 40);
    f.reverbNode->properties.set("y", 100);
    editor.updateComponents();
    sizeModuleComponents(editor);
    f.outId = f.outNode->nodeID;
    f.reverbId = f.reverbNode->nodeID;
    editor.connectPorts(f.reverbId, 0, f.outId, 0, false, false);
    return f;
}

// ---- Default: parallel add at the terminal sink -----------------------------

TEST_F(GraphEditorTest, SmartConnectionAddsParallelCableAtOccupiedAudioOutput) {
    AudioEngine engine;
    AppUndoManager undoMgr;
    GraphEditor editor(engine, &undoMgr);
    editor.setSize(1200, 700);
    editor.setSmartConnectionMode(GraphEditor::SmartConnectionMode::NewAndUnwired);
    editor.setInsertModifierOverrideForTests(false); // no Ctrl

    auto& graph = engine.getGraph();
    auto f = makeWiredSink(engine, editor);
    ASSERT_EQ(countAudioConnectionsBetween(graph, f.reverbId, f.outId), 2);

    DummyDragSource dummySource;
    juce::DragAndDropTarget::SourceDetails details(juce::var("Chorus"), &dummySource,
                                                   libraryCursorForGhostTopLeft("Chorus", {440, 100}));
    editor.itemDragEnter(details);
    editor.itemDragMove(details);
    ASSERT_GT(editor.getSmartSuggestionCount(), 0) << "an occupied sink must still offer a parallel cable";
    for (const auto& s : editor.getSmartSuggestions()) {
        EXPECT_FALSE(s.isInsert) << "without Ctrl nothing is ever rerouted";
        EXPECT_TRUE(s.doomedLinks.empty());
        EXPECT_EQ(s.neighborId, f.outId);
    }
    editor.itemDropped(details);

    const auto chorusId = findNodeIdByName(graph, "Chorus");
    ASSERT_NE(chorusId.uid, 0u);

    // Added alongside, not instead of: the pre-existing cable is untouched.
    EXPECT_EQ(countAudioConnectionsBetween(graph, f.reverbId, f.outId), 2)
        << "the existing cable must survive a parallel add";
    EXPECT_TRUE(graph.isConnected({{f.reverbId, 0}, {f.outId, 0}}));
    EXPECT_TRUE(graph.isConnected({{f.reverbId, 1}, {f.outId, 1}}));
    EXPECT_TRUE(graph.isConnected({{chorusId, 0}, {f.outId, 0}}));
    EXPECT_TRUE(graph.isConnected({{chorusId, 1}, {f.outId, 1}}));
    EXPECT_FALSE(graph.isConnected({{chorusId, 0}, {f.outId, 1}}))
        << "a collapsed jack already fans both legs — a second cable would sum Left into Right";

    ASSERT_TRUE(undoMgr.undo());
    EXPECT_EQ(findNodeIdByName(graph, "Chorus").uid, 0u);
    const auto restoredReverb = findNodeIdByName(graph, "Reverb");
    const auto restoredOut = findNodeIdByName(graph, "Audio Output");
    ASSERT_NE(restoredReverb.uid, 0u);
    ASSERT_NE(restoredOut.uid, 0u);
    EXPECT_EQ(countAudioConnectionsBetween(graph, restoredReverb, restoredOut), 2);
}

TEST_F(GraphEditorTest, SmartConnectionAddsParallelCableForPureSourceAtOccupiedAudioOutput) {
    // A pure source has no audio input, so it can never be inserted — but summing it into the mix
    // bus is a perfectly ordinary thing to want, and is exactly what wiring it by hand would do.
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1200, 700);
    editor.setSmartConnectionMode(GraphEditor::SmartConnectionMode::NewAndUnwired);
    editor.setInsertModifierOverrideForTests(false);

    auto& graph = engine.getGraph();
    auto f = makeWiredSink(engine, editor);

    DummyDragSource dummySource;
    juce::DragAndDropTarget::SourceDetails details(juce::var("Oscillator"), &dummySource,
                                                   libraryCursorForGhostTopLeft("Oscillator", {440, 100}));
    editor.itemDragEnter(details);
    editor.itemDragMove(details);
    ASSERT_GT(editor.getSmartSuggestionCount(), 0) << "a source at an occupied sink gets a parallel cable";
    for (const auto& s : editor.getSmartSuggestions())
        EXPECT_FALSE(s.isInsert);
    editor.itemDropped(details);

    const auto oscId = findNodeIdByName(graph, "Oscillator");
    ASSERT_NE(oscId.uid, 0u);
    EXPECT_GT(countAudioConnectionsBetween(graph, oscId, f.outId), 0);
    EXPECT_EQ(countAudioConnectionsBetween(graph, f.reverbId, f.outId), 2) << "existing cable untouched";
}

// ---- Ctrl: insert-in-series, at any module -----------------------------------

/** Reverb (upstream, 40/600) → Delay (insert target, 760/100), one collapsed cable covering both
 *  raw legs, so the target's audio input group is FULLY occupied. Leaves room for a 280px ghost at
 *  x=440 just left of the Delay. Collapsed FX on both ends keeps the group a single leg — a Filter
 *  fronts TWO audio input legs (L/R), and wiring only one leaves a mixed free/occupied group that
 *  insert deliberately refuses (see SmartConnectionCtrlDoesNotInsertIntoPartlyWiredStereoInput). */
struct WiredChainFixture {
    juce::AudioProcessorGraph::Node::Ptr upstreamNode, targetNode;
    juce::AudioProcessorGraph::NodeID upstreamId, targetId;
};
static WiredChainFixture makeWiredChain(AudioEngine& engine, GraphEditor& editor, bool wireIt = true) {
    WiredChainFixture f;
    auto& graph = engine.getGraph();
    f.targetNode = graph.addNode(std::make_unique<DelayModule>());
    f.targetNode->properties.set("x", 760);
    f.targetNode->properties.set("y", 100);
    f.upstreamNode = graph.addNode(std::make_unique<ReverbModule>());
    f.upstreamNode->properties.set("x", 40);
    f.upstreamNode->properties.set("y", 600);
    editor.updateComponents();
    sizeModuleComponents(editor);
    f.targetId = f.targetNode->nodeID;
    f.upstreamId = f.upstreamNode->nodeID;
    if (wireIt)
        editor.connectPorts(f.upstreamId, 0, f.targetId, 0, false, false);
    return f;
}

TEST_F(GraphEditorTest, SmartConnectionWithoutCtrlNeverInsertsIntoOccupiedModule) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1200, 700);
    editor.setSmartConnectionMode(GraphEditor::SmartConnectionMode::NewAndUnwired);
    editor.setInsertModifierOverrideForTests(false);

    auto f = makeWiredChain(engine, editor, /*wireIt=*/false);

    DummyDragSource dummySource;
    juce::DragAndDropTarget::SourceDetails details(juce::var("Chorus"), &dummySource,
                                                   libraryCursorForGhostTopLeft("Chorus", {440, 100}));

    // Positive control, so the zero below is the modifier rule and not a geometry accident.
    editor.itemDragEnter(details);
    editor.itemDragMove(details);
    ASSERT_GT(editor.getSmartSuggestionCount(), 0) << "geometry check: Chorus → free Delay input is in range";
    editor.endDragPreview();

    editor.connectPorts(f.upstreamId, 0, f.targetId, 0, false, false);

    editor.itemDragEnter(details);
    editor.itemDragMove(details);
    // The group is fully occupied and a valid insert in every other respect — the ONLY thing
    // missing is the modifier. A surprise reroute mid-patch is exactly what this prevents.
    EXPECT_EQ(editor.getSmartSuggestionCount(), 0);
    editor.endDragPreview();
}

TEST_F(GraphEditorTest, SmartConnectionCtrlInsertsIntoOccupiedOrdinaryModule) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1200, 700);
    editor.setSmartConnectionMode(GraphEditor::SmartConnectionMode::NewAndUnwired);
    editor.setInsertModifierOverrideForTests(true); // Ctrl held

    auto f = makeWiredChain(engine, editor);

    DummyDragSource dummySource;
    juce::DragAndDropTarget::SourceDetails details(juce::var("Chorus"), &dummySource,
                                                   libraryCursorForGhostTopLeft("Chorus", {440, 100}));
    editor.itemDragEnter(details);
    editor.itemDragMove(details);

    ASSERT_GT(editor.getSmartSuggestionCount(), 0) << "Ctrl must offer an insert at an ordinary module";
    for (const auto& s : editor.getSmartSuggestions()) {
        EXPECT_TRUE(s.isInsert);
        EXPECT_TRUE(s.ghostIsSource);
        EXPECT_EQ(s.neighborId, f.targetId) << "insert is no longer limited to the terminal sink";
        EXPECT_EQ(s.upstreamId, f.upstreamId);
        ASSERT_FALSE(s.doomedLinks.empty());
        ASSERT_FALSE(s.upstreamCables.empty());
        for (const auto& d : s.doomedLinks) {
            EXPECT_NE(d.p1, juce::Point<float>());
            EXPECT_NE(d.p2, juce::Point<float>());
        }
        for (const auto& c : s.upstreamCables) {
            EXPECT_NE(c.p1, juce::Point<float>());
            EXPECT_NE(c.p2, juce::Point<float>());
        }
    }
    editor.endDragPreview();
}

TEST_F(GraphEditorTest, SmartConnectionCtrlInsertAtOccupiedModuleIsOneUndoStep) {
    AudioEngine engine;
    AppUndoManager undoMgr;
    GraphEditor editor(engine, &undoMgr);
    editor.setSize(1200, 700);
    editor.setSmartConnectionMode(GraphEditor::SmartConnectionMode::NewAndUnwired);
    editor.setInsertModifierOverrideForTests(true);

    auto& graph = engine.getGraph();
    auto f = makeWiredChain(engine, editor);
    ASSERT_GT(countAudioConnectionsBetween(graph, f.upstreamId, f.targetId), 0);

    DummyDragSource dummySource;
    juce::DragAndDropTarget::SourceDetails details(juce::var("Chorus"), &dummySource,
                                                   libraryCursorForGhostTopLeft("Chorus", {440, 100}));
    editor.itemDragEnter(details);
    editor.itemDragMove(details);
    ASSERT_GT(editor.getSmartSuggestionCount(), 0);
    editor.itemDropped(details);

    const auto chorusId = findNodeIdByName(graph, "Chorus");
    ASSERT_NE(chorusId.uid, 0u);
    EXPECT_EQ(countAudioConnectionsBetween(graph, f.upstreamId, f.targetId), 0)
        << "the direct cable is rerouted, not kept";
    EXPECT_GT(countAudioConnectionsBetween(graph, f.upstreamId, chorusId), 0);
    EXPECT_GT(countAudioConnectionsBetween(graph, chorusId, f.targetId), 0);

    ASSERT_TRUE(undoMgr.undo());
    EXPECT_EQ(findNodeIdByName(graph, "Chorus").uid, 0u);
    const auto restoredUpstream = findNodeIdByName(graph, "Reverb");
    const auto restoredTarget = findNodeIdByName(graph, "Delay");
    ASSERT_NE(restoredUpstream.uid, 0u);
    ASSERT_NE(restoredTarget.uid, 0u);
    EXPECT_GT(countAudioConnectionsBetween(graph, restoredUpstream, restoredTarget), 0)
        << "one undo must put the rerouted cable back";
}

TEST_F(GraphEditorTest, SmartConnectionCtrlDoesNotInsertPureSourceIntoOccupiedModule) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1200, 700);
    editor.setSmartConnectionMode(GraphEditor::SmartConnectionMode::NewAndUnwired);
    editor.setInsertModifierOverrideForTests(true); // Ctrl held, and still refused

    auto f = makeWiredChain(engine, editor, /*wireIt=*/false);

    DummyDragSource dummySource;
    juce::DragAndDropTarget::SourceDetails details(juce::var("Oscillator"), &dummySource,
                                                   libraryCursorForGhostTopLeft("Oscillator", {440, 100}));

    editor.itemDragEnter(details);
    editor.itemDragMove(details);
    ASSERT_GT(editor.getSmartSuggestionCount(), 0) << "geometry check: Osc → free Delay input is in range";
    editor.endDragPreview();

    editor.connectPorts(f.upstreamId, 0, f.targetId, 0, false, false);

    editor.itemDragEnter(details);
    editor.itemDragMove(details);
    // Even with Ctrl: an Oscillator has no audio input, so there is nothing to put in series. And
    // outside the terminal sink a parallel sum is not offered either.
    EXPECT_EQ(editor.getSmartSuggestionCount(), 0);
    editor.endDragPreview();
}

TEST_F(GraphEditorTest, SmartConnectionCtrlInsertIsBothOrNeitherAcrossStereoInputLegs) {
    // Both-or-neither. A Filter fronts TWO audio input legs (Left/Right). With only one wired the
    // group is half occupied and rerouting it would silently change what sums where — refused even
    // with Ctrl. Wire the rest and the very same drag becomes a valid insert, which is what makes
    // the refusal above a rule rather than an accident of geometry.
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1200, 700);
    editor.setSmartConnectionMode(GraphEditor::SmartConnectionMode::NewAndUnwired);
    editor.setInsertModifierOverrideForTests(true);

    auto& graph = engine.getGraph();
    auto filterNode = graph.addNode(std::make_unique<FilterModule>());
    filterNode->properties.set("x", 760);
    filterNode->properties.set("y", 100);
    auto reverbNode = graph.addNode(std::make_unique<ReverbModule>());
    reverbNode->properties.set("x", 40);
    reverbNode->properties.set("y", 600);
    editor.updateComponents();
    sizeModuleComponents(editor);

    // Discover the Filter's audio input legs by label rather than assuming indices — the split-block
    // voice modules put Audio R on its own block, so the second leg is not necessarily jack 1.
    auto* filterMb = dynamic_cast<ModuleBase*>(filterNode->getProcessor());
    ASSERT_NE(filterMb, nullptr);
    std::vector<int> audioLegs;
    for (int j = 0; j < filterMb->getVisibleInputPortCount(); ++j) {
        const auto label = filterMb->getInputPortLabel(j).trim().toLowerCase();
        if (label == "left" || label == "right" || label == "audio l" || label == "audio r" || label == "audio")
            audioLegs.push_back(j);
    }
    ASSERT_GE(audioLegs.size(), 2u) << "this test needs a destination with a multi-leg audio input group";

    DummyDragSource dummySource;
    juce::DragAndDropTarget::SourceDetails details(juce::var("Chorus"), &dummySource,
                                                   libraryCursorForGhostTopLeft("Chorus", {440, 100}));

    // Phase 1: only the first leg wired → half-occupied group, no insert.
    editor.connectPorts(reverbNode->nodeID, 0, filterNode->nodeID, audioLegs[0], false, false);
    editor.itemDragEnter(details);
    editor.itemDragMove(details);
    for (const auto& s : editor.getSmartSuggestions())
        EXPECT_FALSE(s.isInsert) << "a half-wired stereo input must not be rerouted";
    editor.endDragPreview();

    // Phase 2: wire the rest → fully occupied group, and now the same drag inserts.
    for (size_t i = 1; i < audioLegs.size(); ++i)
        editor.connectPorts(reverbNode->nodeID, 0, filterNode->nodeID, audioLegs[i], false, false);
    editor.itemDragEnter(details);
    editor.itemDragMove(details);
    ASSERT_GT(editor.getSmartSuggestionCount(), 0);
    for (const auto& s : editor.getSmartSuggestions()) {
        EXPECT_TRUE(s.isInsert) << "a fully occupied group is a valid Ctrl insert";
        EXPECT_EQ(s.upstreamId, reverbNode->nodeID);
    }
    editor.endDragPreview();
}

// ---- Ctrl insert: stereo fan correctness ------------------------------------

TEST_F(GraphEditorTest, SmartConnectionCtrlInsertRemovesEveryDoomedLegOfADualIOUpstream) {
    // Regression: a Dual I/O upstream feeds the sink through TWO distinct cables (jack0→raw0,
    // jack1→raw1). A collapsed ghost's output fans across both raw legs, so the fan dedupe keeps
    // only one jack pair — and the doomed links used to hang off the surviving pair, so the second
    // cable was never removed and kept summing into the sink's right leg beside the ghost's output.
    AudioEngine engine;
    AppUndoManager undoMgr;
    GraphEditor editor(engine, &undoMgr);
    editor.setSize(1200, 700);
    editor.setSmartConnectionMode(GraphEditor::SmartConnectionMode::NewAndUnwired);
    editor.setInsertModifierOverrideForTests(true);

    auto& graph = engine.getGraph();
    auto outNode = addAudioOutputNode(graph, 760, 100);
    auto reverbNode = graph.addNode(std::make_unique<ReverbModule>());
    reverbNode->properties.set("x", 40);
    reverbNode->properties.set("y", 100);
    setDualIOParam(*reverbNode->getProcessor(), true); // split Left/Right BEFORE wiring
    editor.updateComponents();
    sizeModuleComponents(editor);

    const auto reverbId = reverbNode->nodeID;
    const auto outId = outNode->nodeID;
    editor.connectPorts(reverbId, 0, outId, 0, false, false); // Left  -> sink raw0
    editor.connectPorts(reverbId, 1, outId, 1, false, false); // Right -> sink raw1
    ASSERT_TRUE(graph.isConnected({{reverbId, 0}, {outId, 0}}));
    ASSERT_TRUE(graph.isConnected({{reverbId, 1}, {outId, 1}}));
    ASSERT_EQ(countAudioConnectionsBetween(graph, reverbId, outId), 2);

    DummyDragSource dummySource;
    juce::DragAndDropTarget::SourceDetails details(juce::var("Chorus"), &dummySource,
                                                   libraryCursorForGhostTopLeft("Chorus", {440, 100}));
    editor.itemDragEnter(details);
    editor.itemDragMove(details);
    ASSERT_GT(editor.getSmartSuggestionCount(), 0);
    // Both legs must be marked for removal even though only one jack pair survives the dedupe.
    for (const auto& s : editor.getSmartSuggestions()) {
        ASSERT_TRUE(s.isInsert);
        EXPECT_EQ(s.doomedLinks.size(), 2u) << "one doomed cable per occupied sink leg";
    }
    editor.itemDropped(details);

    const auto chorusId = findNodeIdByName(graph, "Chorus");
    ASSERT_NE(chorusId.uid, 0u);

    // The whole point: NO direct upstream->sink edge survives on EITHER raw channel.
    EXPECT_FALSE(graph.isConnected({{reverbId, 0}, {outId, 0}}));
    EXPECT_FALSE(graph.isConnected({{reverbId, 1}, {outId, 1}}));
    EXPECT_EQ(countAudioConnectionsBetween(graph, reverbId, outId), 0)
        << "a doomed leg left behind would sum into the sink alongside the ghost's output";

    EXPECT_TRUE(graph.isConnected({{chorusId, 0}, {outId, 0}}));
    EXPECT_TRUE(graph.isConnected({{chorusId, 1}, {outId, 1}}));
    EXPECT_GT(countAudioConnectionsBetween(graph, reverbId, chorusId), 0) << "the upstream now feeds the ghost";

    // Still one undo step, and it restores both original cables exactly.
    ASSERT_TRUE(undoMgr.undo());
    EXPECT_EQ(findNodeIdByName(graph, "Chorus").uid, 0u);
    const auto restoredReverb = findNodeIdByName(graph, "Reverb");
    const auto restoredOut = findNodeIdByName(graph, "Audio Output");
    ASSERT_NE(restoredReverb.uid, 0u);
    ASSERT_NE(restoredOut.uid, 0u);
    EXPECT_TRUE(graph.isConnected({{restoredReverb, 0}, {restoredOut, 0}}));
    EXPECT_TRUE(graph.isConnected({{restoredReverb, 1}, {restoredOut, 1}}));
    EXPECT_EQ(countAudioConnectionsBetween(graph, restoredReverb, restoredOut), 2);
}

TEST_F(GraphEditorTest, SmartConnectionCtrlInsertDoesNotDuplicateOneUpstreamLegOntoADualIOGhost) {
    // The mirror image of the test above: a COLLAPSED upstream reaches the sink through one visible
    // cable that owns both raw legs, while a Dual I/O ghost has two separate input jacks. Wiring
    // that one upstream jack into each of them would fan its LEFT leg over both ghost legs, summing
    // on the right. Only one upstream->ghost cable is correct; it already carries L->L and R->R.
    //
    // Uses the MOVE path deliberately: the library-drop ghost is an AIStateMapper probe that never
    // has the Dual I/O default applied, so a dropped module's real jack layout can differ from the
    // one the preview measured. Dragging a module already on the canvas makes the ghost the real
    // processor, which is what this shape needs.
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1200, 700);
    editor.setSmartConnectionMode(GraphEditor::SmartConnectionMode::NewAndUnwired);
    editor.setInsertModifierOverrideForTests(true);

    auto& graph = engine.getGraph();
    auto outNode = addAudioOutputNode(graph, 760, 100);
    auto reverbNode = graph.addNode(std::make_unique<ReverbModule>()); // Dual I/O off: collapsed
    reverbNode->properties.set("x", 40);
    reverbNode->properties.set("y", 100);
    auto chorusNode = graph.addNode(std::make_unique<ChorusModule>());
    chorusNode->properties.set("x", 40);
    chorusNode->properties.set("y", 600);
    setDualIOParam(*chorusNode->getProcessor(), true); // ghost splits Left/Right
    editor.updateComponents();
    sizeModuleComponents(editor);

    const auto reverbId = reverbNode->nodeID;
    const auto outId = outNode->nodeID;
    const auto chorusId = chorusNode->nodeID;
    editor.connectPorts(reverbId, 0, outId, 0, false, false); // one cable, both raw legs
    ASSERT_EQ(countAudioConnectionsBetween(graph, reverbId, outId), 2);

    ModuleComponent* chorusComp = nullptr;
    for (auto* c : editor.getModuleComponents()) {
        if (c->getNodeId() == chorusId)
            chorusComp = c;
    }
    ASSERT_NE(chorusComp, nullptr);

    editor.beginDragPreview(chorusComp->getWidth(), chorusComp->getHeight(), chorusId);
    editor.updateDragPreview({440, 100});
    ASSERT_GT(editor.getSmartSuggestionCount(), 0);
    for (const auto& s : editor.getSmartSuggestions()) {
        ASSERT_TRUE(s.isInsert);
        EXPECT_EQ(s.upstreamCables.size(), 1u) << "one collapsed upstream jack needs exactly one cable";
    }
    editor.finalizeModuleDrag(chorusComp);
    editor.endDragPreview();

    EXPECT_EQ(countAudioConnectionsBetween(graph, reverbId, outId), 0);
    EXPECT_TRUE(graph.isConnected({{reverbId, 0}, {chorusId, 0}}));
    EXPECT_TRUE(graph.isConnected({{reverbId, 1}, {chorusId, 1}}));
    EXPECT_FALSE(graph.isConnected({{reverbId, 0}, {chorusId, 1}}))
        << "the upstream's LEFT leg must not also land on the ghost's RIGHT input";
    EXPECT_TRUE(graph.isConnected({{chorusId, 0}, {outId, 0}}));
    EXPECT_TRUE(graph.isConnected({{chorusId, 1}, {outId, 1}}));
}

// ---- Preview must show exactly what the drop wires -------------------------

TEST_F(GraphEditorTest, SmartConnectionParallelAddPreviewCoversBothOutputLegs) {
    // One suggestion is not one cable. A collapsed ghost jack fans across the sink's whole raw pair,
    // and the sink fronts those raws as two SEPARATE visible jacks (no ModuleBase to group them), so
    // the drop wires two cables. The preview used to draw a single wire to the left leg only.
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1200, 700);
    editor.setSmartConnectionMode(GraphEditor::SmartConnectionMode::NewAndUnwired);
    editor.setInsertModifierOverrideForTests(false);
    editor.setDefaultDualIOForNewModules(false); // collapsed ghost: one jack owning both raw legs

    auto f = makeWiredSink(engine, editor);

    DummyDragSource dummySource;
    juce::DragAndDropTarget::SourceDetails details(juce::var("Chorus"), &dummySource,
                                                   libraryCursorForGhostTopLeft("Chorus", {440, 100}));
    editor.itemDragEnter(details);
    editor.itemDragMove(details);
    ASSERT_GT(editor.getSmartSuggestionCount(), 0);

    std::set<int> previewedSinkJacks;
    for (const auto& s : editor.getSmartSuggestions()) {
        ASSERT_FALSE(s.isInsert);
        ASSERT_FALSE(s.mainPreviewLegs.empty()) << "the preview must enumerate its resolved legs";
        for (const auto& leg : s.mainPreviewLegs) {
            previewedSinkJacks.insert(leg.toJack);
            EXPECT_NE(leg.p1, leg.p2) << "every previewed leg needs real endpoints";
        }
    }
    EXPECT_EQ(previewedSinkJacks, (std::set<int>{0, 1}))
        << "preview drew " << previewedSinkJacks.size() << " sink leg(s); the drop fans both";

    // And the drop really does wire both, so the preview above is the truth and not just a guess.
    editor.itemDropped(details);
    const auto chorusId = findNodeIdByName(engine.getGraph(), "Chorus");
    ASSERT_NE(chorusId.uid, 0u);
    EXPECT_TRUE(engine.getGraph().isConnected({{chorusId, 0}, {f.outId, 0}}));
    EXPECT_TRUE(engine.getGraph().isConnected({{chorusId, 1}, {f.outId, 1}}));
}

TEST_F(GraphEditorTest, SmartConnectionProbeHonoursTheDualIODefault) {
    // The library-drop ghost is an AIStateMapper probe, and it decides both the preview and the plan
    // that gets applied. It never used to receive applyDefaultDualIOForNewModule, so with the
    // default set to dual the plan was computed for a COLLAPSED ghost and then applied to a module
    // that spawned dual — wiring only the left legs.
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1200, 700);
    editor.setSmartConnectionMode(GraphEditor::SmartConnectionMode::NewAndUnwired);
    editor.setInsertModifierOverrideForTests(false);
    editor.setDefaultDualIOForNewModules(true); // ghost must front Left/Right, same as the real drop

    auto f = makeWiredSink(engine, editor);

    DummyDragSource dummySource;
    juce::DragAndDropTarget::SourceDetails details(juce::var("Chorus"), &dummySource,
                                                   libraryCursorForGhostTopLeft("Chorus", {440, 100}));
    editor.itemDragEnter(details);
    editor.itemDragMove(details);
    ASSERT_GT(editor.getSmartSuggestionCount(), 0);

    // A dual ghost has one output jack per leg, so the plan must name BOTH of them.
    std::set<int> plannedGhostJacks;
    for (const auto& s : editor.getSmartSuggestions())
        plannedGhostJacks.insert(s.ghostJack);
    EXPECT_EQ(plannedGhostJacks, (std::set<int>{0, 1}))
        << "the probe still looks collapsed — plan and spawned module disagree";

    editor.itemDropped(details);
    const auto chorusId = findNodeIdByName(engine.getGraph(), "Chorus");
    ASSERT_NE(chorusId.uid, 0u);
    auto* chorusMb = dynamic_cast<ModuleBase*>(engine.getGraph().getNodeForId(chorusId)->getProcessor());
    ASSERT_NE(chorusMb, nullptr);
    EXPECT_TRUE(chorusMb->isDualIO()) << "the spawned module is dual, which is what the probe must match";
    EXPECT_TRUE(engine.getGraph().isConnected({{chorusId, 0}, {f.outId, 0}}));
    EXPECT_TRUE(engine.getGraph().isConnected({{chorusId, 1}, {f.outId, 1}}))
        << "a dual ghost must wire its RIGHT leg too";
}

TEST_F(GraphEditorTest, SmartConnectionCtrlInsertWiresBothLegsOfADualGhostBetweenDualNeighbours) {
    // The exact user repro: Delay L+R → Reverb L+R, ctrl-drag a Chorus between them. Every node dual.
    // Expect four new cables (Delay L→Chorus L, Delay R→Chorus R, Chorus L→Reverb L, Chorus R→Reverb
    // R), no direct Delay→Reverb left, and nothing dangling on any Right jack.
    AudioEngine engine;
    AppUndoManager undoMgr;
    GraphEditor editor(engine, &undoMgr);
    editor.setSize(1400, 700);
    editor.setSmartConnectionMode(GraphEditor::SmartConnectionMode::NewAndUnwired);
    editor.setInsertModifierOverrideForTests(true);
    editor.setDefaultDualIOForNewModules(true); // the dropped Chorus spawns dual, like the user's

    auto& graph = engine.getGraph();
    auto delayNode = graph.addNode(std::make_unique<DelayModule>());
    delayNode->properties.set("x", 40);
    delayNode->properties.set("y", 100);
    auto reverbNode = graph.addNode(std::make_unique<ReverbModule>());
    reverbNode->properties.set("x", 760);
    reverbNode->properties.set("y", 100);
    setDualIOParam(*delayNode->getProcessor(), true);
    setDualIOParam(*reverbNode->getProcessor(), true);
    editor.updateComponents();
    sizeModuleComponents(editor);

    const auto delayId = delayNode->nodeID;
    const auto reverbId = reverbNode->nodeID;
    editor.connectPorts(delayId, 0, reverbId, 0, false, false); // L → L
    editor.connectPorts(delayId, 1, reverbId, 1, false, false); // R → R
    ASSERT_TRUE(graph.isConnected({{delayId, 0}, {reverbId, 0}}));
    ASSERT_TRUE(graph.isConnected({{delayId, 1}, {reverbId, 1}}));
    ASSERT_EQ(countAudioConnectionsBetween(graph, delayId, reverbId), 2);

    DummyDragSource dummySource;
    juce::DragAndDropTarget::SourceDetails details(juce::var("Chorus"), &dummySource,
                                                   libraryCursorForGhostTopLeft("Chorus", {440, 100}));
    editor.itemDragEnter(details);
    editor.itemDragMove(details);
    ASSERT_GT(editor.getSmartSuggestionCount(), 0);
    for (const auto& s : editor.getSmartSuggestions()) {
        ASSERT_TRUE(s.isInsert);
        EXPECT_EQ(s.upstreamId, delayId);
        EXPECT_EQ(s.doomedLinks.size(), 2u) << "both original cables are doomed";
        EXPECT_EQ(s.upstreamCables.size(), 2u) << "a dual ghost takes one cable per leg, not one total";
    }
    editor.itemDropped(details);

    const auto chorusId = findNodeIdByName(graph, "Chorus");
    ASSERT_NE(chorusId.uid, 0u);

    EXPECT_EQ(countAudioConnectionsBetween(graph, delayId, reverbId), 0) << "no direct cable may survive";
    EXPECT_TRUE(graph.isConnected({{delayId, 0}, {chorusId, 0}})) << "Delay L → Chorus L";
    EXPECT_TRUE(graph.isConnected({{delayId, 1}, {chorusId, 1}})) << "Delay R → Chorus R (was dangling)";
    EXPECT_TRUE(graph.isConnected({{chorusId, 0}, {reverbId, 0}})) << "Chorus L → Reverb L";
    EXPECT_TRUE(graph.isConnected({{chorusId, 1}, {reverbId, 1}})) << "Chorus R → Reverb R (was dangling)";
    // No cross-wiring: a leg must not be duplicated across both of the far end's inputs.
    EXPECT_FALSE(graph.isConnected({{delayId, 0}, {chorusId, 1}}));
    EXPECT_FALSE(graph.isConnected({{chorusId, 0}, {reverbId, 1}}));

    // Nothing dangling: every leg that was carrying signal before is carrying signal after.
    EXPECT_FALSE(editor.isOutputJackFreeForTests(delayId, 1)) << "Delay Right OUT must not be left dangling";
    EXPECT_FALSE(editor.isInputJackFreeForTests(reverbId, 1)) << "Reverb Right IN must not be left dangling";
    EXPECT_FALSE(editor.isInputJackFreeForTests(chorusId, 1)) << "Chorus Right IN must be fed";
    EXPECT_FALSE(editor.isOutputJackFreeForTests(chorusId, 1)) << "Chorus Right OUT must be used";

    // Still one undo step, restoring both original cables exactly.
    ASSERT_TRUE(undoMgr.undo());
    EXPECT_EQ(findNodeIdByName(graph, "Chorus").uid, 0u);
    const auto restoredDelay = findNodeIdByName(graph, "Delay");
    const auto restoredReverb = findNodeIdByName(graph, "Reverb");
    ASSERT_NE(restoredDelay.uid, 0u);
    ASSERT_NE(restoredReverb.uid, 0u);
    EXPECT_TRUE(graph.isConnected({{restoredDelay, 0}, {restoredReverb, 0}}));
    EXPECT_TRUE(graph.isConnected({{restoredDelay, 1}, {restoredReverb, 1}}));
    EXPECT_EQ(countAudioConnectionsBetween(graph, restoredDelay, restoredReverb), 2);
}

// ---- Ctrl gesture plumbing: both press orderings, and selection integrity ---
//
// Two SEPARATE mechanisms both keyed to Ctrl, and the tests below drive each on its own terms:
//   * press-time classification reads the MouseEvent's own mods (ModuleComponent::mouseDown), so
//     these tests hand it a real Ctrl-flagged event;
//   * the live per-tick sample reads the keyboard (GraphEditor::isInsertModifierDown), which a
//     headless test cannot press, so it uses the override.

static ModuleComponent* findModuleComp(GraphEditor& editor, juce::AudioProcessor* proc); // defined below

static juce::MouseEvent makeModuleClickWithMods(juce::Component& comp, juce::Point<int> position,
                                                juce::ModifierKeys mods, int clicks = 1) {
    const auto pos = position.toFloat();
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), pos, mods, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                            &comp, &comp, juce::Time::getCurrentTime(), pos, juce::Time::getCurrentTime(), clicks,
                            false);
}

static juce::ModifierKeys ctrlLeftClick() {
    return juce::ModifierKeys(juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::leftButtonModifier);
}
static juce::ModifierKeys plainLeftClick() { return juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier); }

/** Reverb upstream (40/600) → Delay target (760/100) already cabled, plus a Chorus at (40/300) to
 *  drag. Real modules throughout, so the ghost is the actual processor (canvas-move path). */
struct CtrlDragFixture {
    juce::AudioProcessorGraph::NodeID upstreamId, targetId, ghostId;
    ModuleComponent* ghostComp = nullptr;
};
static CtrlDragFixture makeCtrlDragFixture(AudioEngine& engine, GraphEditor& editor) {
    CtrlDragFixture f;
    auto& graph = engine.getGraph();
    auto target = graph.addNode(std::make_unique<DelayModule>());
    target->properties.set("x", 760);
    target->properties.set("y", 100);
    auto upstream = graph.addNode(std::make_unique<ReverbModule>());
    upstream->properties.set("x", 40);
    upstream->properties.set("y", 600);
    auto ghost = graph.addNode(std::make_unique<ChorusModule>());
    ghost->properties.set("x", 40);
    ghost->properties.set("y", 300);
    editor.updateComponents();
    sizeModuleComponents(editor);
    f.targetId = target->nodeID;
    f.upstreamId = upstream->nodeID;
    f.ghostId = ghost->nodeID;
    editor.connectPorts(f.upstreamId, 0, f.targetId, 0, false, false);
    f.ghostComp = findModuleComp(editor, ghost->getProcessor());
    return f;
}

TEST_F(GraphEditorTest, SmartConnectionCtrlHeldBeforePressStillArmsAnInsertDrag) {
    // Ordering (b): Ctrl down FIRST, then press and drag. Two things used to swallow this press
    // before it could arm a drag, and this test guards both:
    //   1. the additive-selection branch returned early on Ctrl;
    //   2. on macOS isPopupMenu() is (rightButton | ctrl), so Ctrl+LEFT-click opened the module
    //      context menu and returned.
    // Either one leaves dragPreviewActive false, which makes updateDragPreview a no-op and yields
    // zero suggestions — so a non-zero suggestion count here proves the press armed the drag.
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1400, 1000);
    editor.setSmartConnectionMode(GraphEditor::SmartConnectionMode::NewAndUnwired);
    editor.setInsertModifierOverrideForTests(true); // Ctrl physically held

    auto f = makeCtrlDragFixture(engine, editor);
    ASSERT_NE(f.ghostComp, nullptr);

    const juce::Point<int> bodyPoint{f.ghostComp->getWidth() / 2, f.ghostComp->getHeight() / 2};
    ASSERT_FALSE(f.ghostComp->getPortForPoint(bodyPoint).has_value()) << "the press must land on the card BODY";

    f.ghostComp->mouseDown(makeModuleClickWithMods(*f.ghostComp, bodyPoint, ctrlLeftClick()));
    editor.updateDragPreview({440, 100}); // drag it between upstream and target

    ASSERT_GT(editor.getSmartSuggestionCount(), 0)
        << "Ctrl-held press never armed the drag (selection early-return, or a context menu)";
    for (const auto& s : editor.getSmartSuggestions()) {
        EXPECT_TRUE(s.isInsert);
        EXPECT_EQ(s.neighborId, f.targetId);
        EXPECT_EQ(s.upstreamId, f.upstreamId);
    }
    editor.endDragPreview();
}

TEST_F(GraphEditorTest, SmartConnectionCtrlPressedMidDragTurnsTheSuggestionIntoAnInsert) {
    // Ordering (a): start an ordinary drag, THEN press Ctrl. The modifier is sampled per tick, so
    // the very same ghost position flips from "nothing on offer" to an insert.
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1400, 1000);
    editor.setSmartConnectionMode(GraphEditor::SmartConnectionMode::NewAndUnwired);
    editor.setInsertModifierOverrideForTests(false); // no modifier yet

    auto f = makeCtrlDragFixture(engine, editor);
    ASSERT_NE(f.ghostComp, nullptr);

    const juce::Point<int> bodyPoint{f.ghostComp->getWidth() / 2, f.ghostComp->getHeight() / 2};
    f.ghostComp->mouseDown(makeModuleClickWithMods(*f.ghostComp, bodyPoint, plainLeftClick()));
    editor.updateDragPreview({440, 100});
    EXPECT_EQ(editor.getSmartSuggestionCount(), 0)
        << "the target's input is occupied and it is not the sink, so an unmodified drag gets nothing";

    editor.setInsertModifierOverrideForTests(true); // user presses Ctrl mid-drag
    editor.updateDragPreview({440, 100});

    ASSERT_GT(editor.getSmartSuggestionCount(), 0) << "pressing Ctrl mid-drag must offer the insert";
    for (const auto& s : editor.getSmartSuggestions()) {
        EXPECT_TRUE(s.isInsert);
        EXPECT_EQ(s.upstreamId, f.upstreamId);
    }
    editor.endDragPreview();
}

TEST_F(GraphEditorTest, CtrlClickTogglesSelectionButCtrlDragDoesNot) {
    // The deferred classification, from the selection's point of view. A Ctrl+CLICK must behave as a
    // pure additive toggle and leave the rest of the selection alone; a Ctrl+DRAG must move the card
    // and NOT leave a stray toggle behind.
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1400, 1000);
    editor.setInsertModifierOverrideForTests(true);

    auto& graph = engine.getGraph();
    auto a = graph.addNode(std::make_unique<OscillatorModule>());
    a->properties.set("x", 40);
    a->properties.set("y", 40);
    auto b = graph.addNode(std::make_unique<FilterModule>());
    b->properties.set("x", 400);
    b->properties.set("y", 40);
    auto c = graph.addNode(std::make_unique<DelayModule>());
    c->properties.set("x", 40);
    c->properties.set("y", 500);
    editor.updateComponents();
    sizeModuleComponents(editor);

    auto* compC = findModuleComp(editor, c->getProcessor());
    ASSERT_NE(compC, nullptr);
    const juce::Point<int> bodyPoint{compC->getWidth() / 2, compC->getHeight() / 2};

    // A + B selected; Ctrl+click C ADDS it and keeps A and B.
    editor.setSelectedNodes({a->nodeID, b->nodeID});
    compC->mouseDown(makeModuleClickWithMods(*compC, bodyPoint, ctrlLeftClick()));
    compC->mouseUp(makeModuleClickWithMods(*compC, bodyPoint, ctrlLeftClick()));
    EXPECT_EQ(editor.getSelectionCount(), 3);
    EXPECT_TRUE(editor.isNodeSelected(a->nodeID)) << "a Ctrl+click must not discard the rest of the selection";
    EXPECT_TRUE(editor.isNodeSelected(b->nodeID));
    EXPECT_TRUE(editor.isNodeSelected(c->nodeID));

    // Ctrl+click C again REMOVES it, still keeping A and B.
    compC->mouseDown(makeModuleClickWithMods(*compC, bodyPoint, ctrlLeftClick()));
    compC->mouseUp(makeModuleClickWithMods(*compC, bodyPoint, ctrlLeftClick()));
    EXPECT_EQ(editor.getSelectionCount(), 2);
    EXPECT_FALSE(editor.isNodeSelected(c->nodeID)) << "a second Ctrl+click must toggle it back off";
    EXPECT_TRUE(editor.isNodeSelected(a->nodeID));
    EXPECT_TRUE(editor.isNodeSelected(b->nodeID));

    // Now a Ctrl+DRAG: the press collapses onto C so the move is single-module (a group drag would
    // suppress smart connections), it actually moves, and mouse-up must NOT run the toggle.
    editor.setSelectedNodes({a->nodeID, b->nodeID});
    compC->mouseDown(makeModuleClickWithMods(*compC, bodyPoint, ctrlLeftClick()));
    EXPECT_EQ(editor.getSelectionCount(), 1) << "the press collapses onto the dragged card";
    EXPECT_TRUE(editor.isNodeSelected(c->nodeID));
    compC->setTopLeftPosition(compC->getPosition() + juce::Point<int>(120, 0)); // the drag moved it
    compC->mouseUp(makeModuleClickWithMods(*compC, bodyPoint, ctrlLeftClick()));

    EXPECT_TRUE(editor.isNodeSelected(c->nodeID)) << "a Ctrl+drag must not toggle the dragged card away";
    EXPECT_EQ(editor.getSelectionCount(), 1) << "and must not resurrect the pre-press selection either";
    editor.endDragPreview();
}

// ---- Round 5 regressions: library Ctrl, live downgrade, jack alignment, dual fan ----

TEST_F(GraphEditorTest, GhostPortEstimateMatchesTheRealJackCentre) {
    // The drag ghost's jack positions come from GraphEditor::estimatePortCenter while a real card's
    // come from ModuleComponent::getPortCenter. They carried separate header literals (30 vs 38), so
    // every preview cable terminated 8px ABOVE the jack dot it claimed to land on.
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1000, 700);

    auto& graph = engine.getGraph();
    auto node = graph.addNode(std::make_unique<DelayModule>());
    node->properties.set("x", 200);
    node->properties.set("y", 120);
    editor.updateComponents();
    sizeModuleComponents(editor);

    auto* comp = findModuleComp(editor, node->getProcessor());
    ASSERT_NE(comp, nullptr);
    const auto bounds = comp->getBounds();

    for (bool isInput : {true, false}) {
        for (int jack = 0; jack < 2; ++jack) {
            const auto real = bounds.getPosition() + comp->getPortCenter(jack, isInput);
            const auto ghost = GraphEditor::estimatePortCenter(node->getProcessor(), bounds, jack, isInput, false);
            EXPECT_EQ(ghost, real) << "ghost estimate drifted from the real jack centre for "
                                   << (isInput ? "input" : "output") << " jack " << jack;
        }
    }
}

TEST_F(GraphEditorTest, SmartConnectionPreviewLegsLandOnTheRealDestinationJack) {
    // End-to-end version of the above: the previewed leg's destination endpoint must sit on the
    // destination card's actual jack dot, not floating over its label row.
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1200, 700);
    editor.setSmartConnectionMode(GraphEditor::SmartConnectionMode::NewAndUnwired);
    editor.setInsertModifierOverrideForTests(false);

    auto& graph = engine.getGraph();
    auto dest = graph.addNode(std::make_unique<DelayModule>());
    dest->properties.set("x", 760);
    dest->properties.set("y", 100);
    editor.updateComponents();
    sizeModuleComponents(editor);
    auto* destComp = findModuleComp(editor, dest->getProcessor());
    ASSERT_NE(destComp, nullptr);

    DummyDragSource dummySource;
    juce::DragAndDropTarget::SourceDetails details(juce::var("Chorus"), &dummySource,
                                                   libraryCursorForGhostTopLeft("Chorus", {440, 100}));
    editor.itemDragEnter(details);
    editor.itemDragMove(details);
    ASSERT_GT(editor.getSmartSuggestionCount(), 0);

    for (const auto& s : editor.getSmartSuggestions()) {
        ASSERT_FALSE(s.mainPreviewLegs.empty());
        for (const auto& leg : s.mainPreviewLegs) {
            const auto expected =
                (destComp->getBounds().getPosition() + destComp->getPortCenter(leg.toJack, true)).toFloat();
            EXPECT_LT(leg.p2.getDistanceFrom(expected), 1.0f)
                << "preview leg ends at " << leg.p2.y << " but the jack dot is at " << expected.y;
        }
    }
    editor.endDragPreview();
}

TEST_F(GraphEditorTest, SmartConnectionReleasingCtrlMidDragDowngradesTheInsert) {
    // Pressing or releasing the modifier is not a mouse move. Suggestions used to be recomputed only
    // from updateDragPreview, so letting Ctrl go without moving left the insert preview on screen.
    // The drag tick re-samples and must flip it back, and forward again, with no mouse movement.
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1400, 1000);
    editor.setSmartConnectionMode(GraphEditor::SmartConnectionMode::NewAndUnwired);
    editor.setInsertModifierOverrideForTests(true);

    auto f = makeCtrlDragFixture(engine, editor);
    ASSERT_NE(f.ghostComp, nullptr);

    const juce::Point<int> bodyPoint{f.ghostComp->getWidth() / 2, f.ghostComp->getHeight() / 2};
    f.ghostComp->mouseDown(makeModuleClickWithMods(*f.ghostComp, bodyPoint, ctrlLeftClick()));
    editor.updateDragPreview({440, 100});
    ASSERT_GT(editor.getSmartSuggestionCount(), 0);
    for (const auto& s : editor.getSmartSuggestions())
        ASSERT_TRUE(s.isInsert);

    // Ctrl released, mouse perfectly still: the tick must downgrade the preview.
    editor.setInsertModifierOverrideForTests(false);
    editor.pumpDragModifierTickForTests();
    for (const auto& s : editor.getSmartSuggestions())
        EXPECT_FALSE(s.isInsert) << "releasing Ctrl left a stale insert preview on screen";

    // And pressing it again, still without moving, must bring the insert back.
    editor.setInsertModifierOverrideForTests(true);
    editor.pumpDragModifierTickForTests();
    ASSERT_GT(editor.getSmartSuggestionCount(), 0);
    for (const auto& s : editor.getSmartSuggestions())
        EXPECT_TRUE(s.isInsert) << "re-pressing Ctrl without moving must re-offer the insert";

    editor.endDragPreview();
}

TEST_F(GraphEditorTest, SmartConnectionDualOutputWiresBothLegsIntoACollapsedInput) {
    // A dual Reverb dropped beside a collapsed Chorus wired only Left: the planner treated the
    // Left jack as MONO and let connectPorts duplicate it onto both destination raw legs (stride 0),
    // which also made the Right jack's pair redundant and dropped it. Left must reach raw0 and Right
    // must reach raw1.
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1200, 700);
    editor.setSmartConnectionMode(GraphEditor::SmartConnectionMode::NewAndUnwired);
    editor.setInsertModifierOverrideForTests(false);

    auto& graph = engine.getGraph();
    auto chorusNode = graph.addNode(std::make_unique<ChorusModule>()); // Dual I/O off: collapsed input
    chorusNode->properties.set("x", 760);
    chorusNode->properties.set("y", 100);
    auto reverbNode = graph.addNode(std::make_unique<ReverbModule>());
    reverbNode->properties.set("x", 40);
    reverbNode->properties.set("y", 100);
    setDualIOParam(*reverbNode->getProcessor(), true); // dual OUTPUT: separate Left/Right jacks
    editor.updateComponents();
    sizeModuleComponents(editor);

    const auto reverbId = reverbNode->nodeID;
    const auto chorusId = chorusNode->nodeID;

    auto* reverbComp = findModuleComp(editor, reverbNode->getProcessor());
    ASSERT_NE(reverbComp, nullptr);
    editor.beginDragPreview(reverbComp->getWidth(), reverbComp->getHeight(), reverbId);
    editor.updateDragPreview({440, 100});
    ASSERT_GT(editor.getSmartSuggestionCount(), 0);
    editor.finalizeModuleDrag(reverbComp);
    editor.endDragPreview();

    EXPECT_TRUE(graph.isConnected({{reverbId, 0}, {chorusId, 0}})) << "Left must reach the collapsed jack's raw0";
    EXPECT_TRUE(graph.isConnected({{reverbId, 1}, {chorusId, 1}})) << "Right must reach the collapsed jack's raw1";
    EXPECT_FALSE(graph.isConnected({{reverbId, 0}, {chorusId, 1}}))
        << "Left must not be duplicated onto the right leg as well";
}

TEST_F(GraphEditorTest, SmartConnectionCtrlLibraryDropInsertsIntoAnOccupiedModule) {
    // Item 1's canvas half: a LIBRARY drag with Ctrl held must offer the insert, not just a
    // canvas-move drag. The library row's own press guard is covered by
    // ModuleLibraryRowPress.CtrlLeftClickDoesNotSuppressTheDrag.
    AudioEngine engine;
    AppUndoManager undoMgr;
    GraphEditor editor(engine, &undoMgr);
    editor.setSize(1200, 700);
    editor.setSmartConnectionMode(GraphEditor::SmartConnectionMode::NewAndUnwired);
    editor.setInsertModifierOverrideForTests(true); // Ctrl held for the whole library drag

    auto& graph = engine.getGraph();
    auto f = makeWiredChain(engine, editor);
    ASSERT_GT(countAudioConnectionsBetween(graph, f.upstreamId, f.targetId), 0);

    DummyDragSource dummySource;
    juce::DragAndDropTarget::SourceDetails details(juce::var("Chorus"), &dummySource,
                                                   libraryCursorForGhostTopLeft("Chorus", {440, 100}));
    editor.itemDragEnter(details);
    editor.itemDragMove(details);

    ASSERT_GT(editor.getSmartSuggestionCount(), 0) << "a Ctrl-held library drag must offer the insert";
    for (const auto& s : editor.getSmartSuggestions()) {
        EXPECT_TRUE(s.isInsert);
        EXPECT_EQ(s.upstreamId, f.upstreamId);
    }
    editor.itemDropped(details);

    const auto chorusId = findNodeIdByName(graph, "Chorus");
    ASSERT_NE(chorusId.uid, 0u);
    EXPECT_EQ(countAudioConnectionsBetween(graph, f.upstreamId, f.targetId), 0);
    EXPECT_GT(countAudioConnectionsBetween(graph, f.upstreamId, chorusId), 0);
    EXPECT_GT(countAudioConnectionsBetween(graph, chorusId, f.targetId), 0);
}

TEST_F(GraphEditorTest, ResolvePolyLinkPairsADualSourceOntoACollapsedDestination) {
    // The pure-function half of the fix, and the mirror direction that already worked.
    ReverbModule dualSource;
    setDualIOParam(dualSource, true);
    ChorusModule collapsedDest; // Dual I/O off

    const auto fromDual = GraphEditor::resolvePolyLink(&dualSource, 0, &collapsedDest, 0);
    EXPECT_EQ(fromDual.voiceCount, 2) << "a dual source's Left jack must carry the whole pair";
    EXPECT_EQ(fromDual.sourceRawChannel, 0);
    EXPECT_EQ(fromDual.destRawChannel, 0);
    EXPECT_EQ(fromDual.sourceStride, 1) << "stride must step to the module's own right leg, not 0";

    // Mirror: collapsed source into a dual destination still fans L->L / R->R.
    ReverbModule collapsedSource;
    ChorusModule dualDest;
    setDualIOParam(dualDest, true);
    const auto toDual = GraphEditor::resolvePolyLink(&collapsedSource, 0, &dualDest, 0);
    EXPECT_EQ(toDual.voiceCount, 2);
    EXPECT_EQ(toDual.sourceStride, 1);
}

// --- Double-click module title to rename (custom card titles) ----------------
//
// The custom title is the node property "displayName". It is deliberately NOT the processor's own
// name: ModuleBase::getName() is the auto-numbered "Chorus 2" that AudioEngine::updateModuleNames()
// recomputes wholesale on every graph change, so a title written there is clobbered by the next node
// added. These tests pin both halves — the custom title sticks, and the numbering still works.

TEST_F(GraphEditorTest, ModuleTitleRenameCommitsAndFallsBackToTheNumberedName) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(900, 600);

    auto& graph = engine.getGraph();
    auto node = graph.addNode(std::make_unique<ChorusModule>());
    engine.updateModuleNames(); // assigns "Chorus 1"
    editor.updateComponents();
    sizeModuleComponents(editor);

    auto* comp = findModuleComp(editor, node->getProcessor());
    ASSERT_NE(comp, nullptr);
    const auto numbered = node->getProcessor()->getName();
    ASSERT_TRUE(numbered.isNotEmpty());
    EXPECT_EQ(comp->cardTitle(), numbered) << "with no custom title the card shows the numbered name";

    comp->beginTitleRename();
    ASSERT_TRUE(comp->isRenamingTitle());
    auto* ed = dynamic_cast<juce::TextEditor*>(comp->findChildWithID("moduleTitleRenameEditor"));
    ASSERT_NE(ed, nullptr);
    EXPECT_EQ(ed->getText(), numbered) << "the editor is seeded with what the header shows";

    ed->setText("Shimmer Bus", juce::dontSendNotification);
    comp->finishTitleRename(true);

    EXPECT_FALSE(comp->isRenamingTitle());
    EXPECT_EQ(editor.getModuleDisplayName(node->nodeID), "Shimmer Bus");
    EXPECT_EQ(comp->cardTitle(), "Shimmer Bus");
    EXPECT_EQ(node->getProcessor()->getName(), numbered) << "the processor's numbered name is untouched";
}

TEST_F(GraphEditorTest, ModuleTitleRenameEscapeCancelsAndWhitespaceReverts) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(900, 600);

    auto& graph = engine.getGraph();
    auto node = graph.addNode(std::make_unique<ChorusModule>());
    engine.updateModuleNames();
    editor.updateComponents();
    sizeModuleComponents(editor);
    auto* comp = findModuleComp(editor, node->getProcessor());
    ASSERT_NE(comp, nullptr);
    const auto numbered = node->getProcessor()->getName();

    // Escape discards.
    comp->beginTitleRename();
    auto* ed = dynamic_cast<juce::TextEditor*>(comp->findChildWithID("moduleTitleRenameEditor"));
    ASSERT_NE(ed, nullptr);
    ed->setText("Discard Me", juce::dontSendNotification);
    comp->finishTitleRename(false);
    EXPECT_EQ(editor.getModuleDisplayName(node->nodeID), "") << "Escape must not store anything";
    EXPECT_EQ(comp->cardTitle(), numbered);

    // Commit a real name, then blank it out: whitespace reverts to the numbered default.
    editor.setModuleDisplayName(node->nodeID, "Shimmer Bus");
    ASSERT_EQ(comp->cardTitle(), "Shimmer Bus");

    comp->beginTitleRename();
    ed = dynamic_cast<juce::TextEditor*>(comp->findChildWithID("moduleTitleRenameEditor"));
    ASSERT_NE(ed, nullptr);
    ed->setText("    ", juce::dontSendNotification);
    comp->finishTitleRename(true);
    EXPECT_EQ(editor.getModuleDisplayName(node->nodeID), "") << "whitespace-only clears the custom title";
    EXPECT_EQ(comp->cardTitle(), numbered);
}

TEST_F(GraphEditorTest, ModuleTitleRenameIsUndoable) {
    AudioEngine engine;
    AppUndoManager undoMgr;
    GraphEditor editor(engine, &undoMgr);
    editor.setSize(900, 600);

    auto& graph = engine.getGraph();
    auto node = graph.addNode(std::make_unique<ChorusModule>());
    engine.updateModuleNames();
    editor.updateComponents();
    sizeModuleComponents(editor);
    const auto nodeId = node->nodeID;

    editor.setModuleDisplayName(nodeId, "First Name");
    ASSERT_EQ(editor.getModuleDisplayName(nodeId), "First Name");
    editor.setModuleDisplayName(nodeId, "Second Name");
    ASSERT_EQ(editor.getModuleDisplayName(nodeId), "Second Name");

    ASSERT_TRUE(undoMgr.undo());
    const auto afterUndo = findNodeIdByName(graph, "Chorus 1");
    // The node id survives a structural restore here, but resolve by whatever is live to be safe.
    const auto liveId = afterUndo.uid != 0 ? afterUndo : nodeId;
    EXPECT_EQ(editor.getModuleDisplayName(liveId), "First Name") << "undo restores the previous title";
}

TEST_F(GraphEditorTest, ModuleTitleRenameDoesNotArmABodyDrag) {
    // The double-click must be intercepted BEFORE the drag is armed, or bodyDragActive stays set
    // under an open editor and the next stray mouseDrag walks the card off under the cursor.
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(900, 600);

    auto& graph = engine.getGraph();
    auto node = graph.addNode(std::make_unique<ChorusModule>());
    engine.updateModuleNames();
    editor.updateComponents();
    sizeModuleComponents(editor);
    auto* comp = findModuleComp(editor, node->getProcessor());
    ASSERT_NE(comp, nullptr);

    const auto before = comp->getPosition();
    const juce::Point<int> titlePoint{comp->getWidth() / 2, 8}; // inside the header band
    comp->mouseDown(makeModuleClickWithMods(*comp, titlePoint, plainLeftClick(), /*clicks=*/2));

    ASSERT_TRUE(comp->isRenamingTitle()) << "a double-click on the header opens the editor";

    // A drag arriving now must not move the card: no dragger was armed.
    comp->mouseDrag(makeModuleClickWithMods(*comp, titlePoint + juce::Point<int>(80, 40), plainLeftClick()));
    EXPECT_EQ(comp->getPosition(), before) << "the rename must not have armed a body drag";

    comp->finishTitleRename(false);
}

TEST_F(GraphEditorTest, ModuleTitleRoundTripsThroughGraphJSON) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(900, 600);

    auto& graph = engine.getGraph();
    auto node = graph.addNode(std::make_unique<ChorusModule>());
    engine.updateModuleNames();
    editor.updateComponents();
    editor.setModuleDisplayName(node->nodeID, "Shimmer Bus");

    const auto json = synth::AIStateMapper::graphToJSON(graph);
    const auto text = juce::JSON::toString(json);
    EXPECT_TRUE(text.contains("Shimmer Bus")) << "the custom title must be serialized";

    // Trusted reload: the title comes back.
    AudioEngine reloaded;
    ASSERT_TRUE(synth::AIStateMapper::applyJSONToGraph(json, reloaded.getGraph(), /*clearExisting=*/true,
                                                       /*trusted=*/true));
    bool found = false;
    for (auto* n : reloaded.getGraph().getNodes()) {
        if (n->getProcessor() != nullptr && n->getProcessor()->getName().startsWith("Chorus")) {
            EXPECT_EQ(n->properties["displayName"].toString(), "Shimmer Bus");
            found = true;
        }
    }
    EXPECT_TRUE(found) << "the renamed Chorus survived the round trip";
}

TEST_F(GraphEditorTest, UntrustedPatchDisplayNameIsCappedAndDisplayOnly) {
    // Display-only and length-capped on the untrusted path: it may relabel a card and nothing else.
    // In particular it must never influence which module type gets created — that is "type".
    AudioEngine engine;
    const juce::String oversized = juce::String::repeatedString("A", synth::kMaxModuleDisplayNameChars * 4);

    juce::String patch = R"({"nodes":[{"id":1,"type":"Chorus","displayName":")" + oversized + R"("}]})";
    const auto json = juce::JSON::parse(patch);
    ASSERT_TRUE(synth::AIStateMapper::applyJSONToGraph(json, engine.getGraph(), /*clearExisting=*/true,
                                                       /*trusted=*/false));

    int chorusCount = 0;
    for (auto* n : engine.getGraph().getNodes()) {
        if (n->getProcessor() == nullptr)
            continue;
        if (dynamic_cast<ChorusModule*>(n->getProcessor()) != nullptr) {
            ++chorusCount;
            const auto stored = n->properties["displayName"].toString();
            EXPECT_LE(stored.length(), synth::kMaxModuleDisplayNameChars) << "an untrusted title must be capped";
            EXPECT_TRUE(stored.isNotEmpty());
        }
    }
    EXPECT_EQ(chorusCount, 1) << "the title must not have changed which module type was created";
}

TEST_F(GraphEditorTest, AutoNumberingStillAppliesAlongsideCustomTitles) {
    // Renaming one instance must not disturb the numbering of the others, and a renamed card keeps
    // its own title while updateModuleNames renumbers the processors underneath.
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1200, 800);

    auto& graph = engine.getGraph();
    auto first = graph.addNode(std::make_unique<ChorusModule>());
    auto second = graph.addNode(std::make_unique<ChorusModule>());
    engine.updateModuleNames();
    editor.updateComponents();

    EXPECT_EQ(first->getProcessor()->getName(), "Chorus 1");
    EXPECT_EQ(second->getProcessor()->getName(), "Chorus 2");

    editor.setModuleDisplayName(first->nodeID, "Shimmer Bus");

    // A third instance arrives and everything renumbers.
    auto third = graph.addNode(std::make_unique<ChorusModule>());
    engine.updateModuleNames();
    editor.updateComponents();

    EXPECT_EQ(third->getProcessor()->getName(), "Chorus 3") << "auto-numbering still counts every instance";
    EXPECT_EQ(editor.getModuleDisplayName(first->nodeID), "Shimmer Bus")
        << "renumbering must not clobber a custom title";
    EXPECT_EQ(editor.getModuleTitle(first->nodeID, first->getProcessor()), "Shimmer Bus");
    EXPECT_EQ(editor.getModuleTitle(second->nodeID, second->getProcessor()), "Chorus 2")
        << "an un-renamed sibling still follows the numbering";
}

TEST_F(GraphEditorTest, ModuleTitleRenameIsDismissedByAPressOutsideTheEditor) {
    // The editor's own onFocusLost is not enough: almost nothing on this canvas wants keyboard
    // focus, so clicking a card body or the background never takes focus off the editor and the
    // callback never fires. Every press path has to commit it from the presser's side. All three
    // surfaces the user can plausibly click are covered here.
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1200, 800);

    auto& graph = engine.getGraph();
    auto a = graph.addNode(std::make_unique<ChorusModule>());
    a->properties.set("x", 40);
    a->properties.set("y", 40);
    auto b = graph.addNode(std::make_unique<DelayModule>());
    b->properties.set("x", 500);
    b->properties.set("y", 40);
    engine.updateModuleNames();
    editor.updateComponents();
    sizeModuleComponents(editor);

    auto* compA = findModuleComp(editor, a->getProcessor());
    auto* compB = findModuleComp(editor, b->getProcessor());
    ASSERT_NE(compA, nullptr);
    ASSERT_NE(compB, nullptr);

    const juce::Point<int> bodyA{compA->getWidth() / 2, compA->getHeight() / 2};
    const juce::Point<int> bodyB{compB->getWidth() / 2, compB->getHeight() / 2};

    auto openEditorWith = [&](const juce::String& text) {
        compA->beginTitleRename();
        auto* ed = dynamic_cast<juce::TextEditor*>(compA->findChildWithID("moduleTitleRenameEditor"));
        ASSERT_NE(ed, nullptr);
        ed->setText(text, juce::dontSendNotification);
    };

    // 1. A press on the SAME card's body.
    openEditorWith("Named By Body Click");
    ASSERT_TRUE(compA->isRenamingTitle());
    compA->mouseDown(makeModuleClickWithMods(*compA, bodyA, plainLeftClick()));
    EXPECT_FALSE(compA->isRenamingTitle()) << "a press on the card body must dismiss the editor";
    EXPECT_EQ(editor.getModuleDisplayName(a->nodeID), "Named By Body Click") << "clicking away commits";

    // 2. A press on ANOTHER card.
    openEditorWith("Named By Other Card");
    ASSERT_TRUE(compA->isRenamingTitle());
    compB->mouseDown(makeModuleClickWithMods(*compB, bodyB, plainLeftClick()));
    EXPECT_FALSE(compA->isRenamingTitle()) << "a press on a different card must dismiss it too";
    EXPECT_EQ(editor.getModuleDisplayName(a->nodeID), "Named By Other Card");

    // 3. A press on empty canvas.
    openEditorWith("Named By Canvas Click");
    ASSERT_TRUE(compA->isRenamingTitle());
    editor.mouseDown(makeModuleClickWithMods(editor, {1000, 700}, plainLeftClick()));
    EXPECT_FALSE(compA->isRenamingTitle()) << "a press on empty canvas must dismiss it";
    EXPECT_EQ(editor.getModuleDisplayName(a->nodeID), "Named By Canvas Click");

    // Escape is still the only cancel, and it must survive all of the above wiring.
    openEditorWith("Should Not Stick");
    compA->finishTitleRename(false);
    EXPECT_EQ(editor.getModuleDisplayName(a->nodeID), "Named By Canvas Click") << "Escape still discards";
}

// --- Every FX must be insertable, aiming AT THE GAP --------------------------
//
// The ghost is centred on the cursor for a library drag, and the insert path relaxes the jack-level
// left-to-right rule, so the natural aim works: put the cursor in the gap between two wired cards
// (or over the cable itself) and the insert is offered. This used to require aiming roughly one
// card-width to the LEFT of the destination, which nobody does — it read as "this module doesn't
// support insert", and a report of exactly that shape turned out to have no per-module cause.
//
// Fixture: upstream at 40 (280 wide, so 40..320), destination at 460 (460..740), leaving a 140px
// visible gap at 320..460. The cursor goes in the middle of that gap.

class SmartConnectionFxInsertTest : public ::testing::TestWithParam<const char*> {};

namespace {
constexpr int kFxUpstreamX = 40;
constexpr int kFxTargetX = 460;
constexpr int kFxGapCentreX = 390; // middle of the 320..460 gap
constexpr int kFxLaneY = 100;
} // namespace

/** Two wired cards with a visible gap, sized as the app sizes them. */
struct FxChainFixture {
    juce::AudioProcessorGraph::NodeID upstreamId, targetId;
};
static FxChainFixture makeFxChain(AudioEngine& engine, GraphEditor& editor) {
    FxChainFixture f;
    auto& graph = engine.getGraph();
    auto upstream = graph.addNode(std::make_unique<ReverbModule>());
    upstream->properties.set("x", kFxUpstreamX);
    upstream->properties.set("y", kFxLaneY);
    auto target = graph.addNode(std::make_unique<DelayModule>());
    target->properties.set("x", kFxTargetX);
    target->properties.set("y", kFxLaneY);
    editor.updateComponents();
    sizeModuleComponents(editor);
    f.upstreamId = upstream->nodeID;
    f.targetId = target->nodeID;
    editor.connectPorts(f.upstreamId, 0, f.targetId, 0, false, false);
    return f;
}

TEST_P(SmartConnectionFxInsertTest, CtrlDragAtTheGapInsertsThisFx) {
    const juce::String fxName(GetParam());

    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 900);
    editor.setSmartConnectionMode(GraphEditor::SmartConnectionMode::NewAndUnwired);
    editor.setInsertModifierOverrideForTests(true); // Ctrl held

    auto& graph = engine.getGraph();
    auto f = makeFxChain(engine, editor);
    ASSERT_GT(countAudioConnectionsBetween(graph, f.upstreamId, f.targetId), 0);

    std::set<juce::uint32> before;
    for (auto* n : graph.getNodes())
        before.insert(n->nodeID.uid);

    // Cursor IN THE GAP — the aim a user actually takes.
    DummyDragSource dummySource;
    juce::DragAndDropTarget::SourceDetails details(juce::var(fxName), &dummySource,
                                                   juce::Point<int>(kFxGapCentreX, kFxLaneY));
    editor.itemDragEnter(details);
    editor.itemDragMove(details);

    ASSERT_GT(editor.getSmartSuggestionCount(), 0) << fxName << " offered nothing with the cursor over the gap";
    for (const auto& s : editor.getSmartSuggestions()) {
        EXPECT_TRUE(s.isInsert) << fxName << " offered a plain connect instead of an insert";
        EXPECT_EQ(s.neighborId, f.targetId) << fxName << " aimed at the wrong neighbour";
        EXPECT_EQ(s.upstreamId, f.upstreamId) << fxName << " picked the wrong upstream";
    }

    editor.itemDropped(details);

    juce::AudioProcessorGraph::NodeID ghostId{};
    for (auto* n : graph.getNodes())
        if (before.find(n->nodeID.uid) == before.end())
            ghostId = n->nodeID;
    ASSERT_NE(ghostId.uid, 0u) << fxName << " was not created on drop";

    EXPECT_EQ(countAudioConnectionsBetween(graph, f.upstreamId, f.targetId), 0)
        << fxName << " left the original cable in place";
    EXPECT_GT(countAudioConnectionsBetween(graph, f.upstreamId, ghostId), 0) << fxName << " is not fed by the upstream";
    EXPECT_GT(countAudioConnectionsBetween(graph, ghostId, f.targetId), 0) << fxName << " does not feed the target";
}

INSTANTIATE_TEST_SUITE_P(AllFxTypes, SmartConnectionFxInsertTest,
                         ::testing::Values("Distortion", "Delay", "Reverb", "Chorus", "Phaser", "Flanger", "Compressor",
                                           "Limiter", "Bitcrusher", "Pitch Shifter", "Parametric EQ",
                                           "Ring Modulator"));

TEST_F(GraphEditorTest, SmartConnectionInsertAimWindowSpansTheWholeGap) {
    // Every cursor position across the visible gap must offer the insert, and so must a cursor over
    // the destination's own left half (aiming at the cable that ends there). Dragged clean PAST the
    // destination must not.
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 900);
    editor.setSmartConnectionMode(GraphEditor::SmartConnectionMode::NewAndUnwired);
    editor.setInsertModifierOverrideForTests(true);

    auto f = makeFxChain(engine, editor);
    DummyDragSource dummySource;

    auto insertsAt = [&](int cursorX) {
        juce::DragAndDropTarget::SourceDetails d(juce::var("Chorus"), &dummySource,
                                                 juce::Point<int>(cursorX, kFxLaneY));
        editor.itemDragEnter(d);
        editor.itemDragMove(d);
        int n = 0;
        for (const auto& s : editor.getSmartSuggestions())
            if (s.isInsert)
                ++n;
        editor.endDragPreview();
        return n;
    };

    // The whole visible gap, end to end.
    for (int x = 320; x <= 460; x += 10)
        EXPECT_GT(insertsAt(x), 0) << "no insert with the cursor at x=" << x << ", inside the gap";

    // Over the destination card itself — aiming at the cable's far end.
    EXPECT_GT(insertsAt(520), 0) << "no insert with the cursor over the destination's left half";

    // Dragged clean past the destination: its centre is beyond the card's right edge, so this is not
    // "insert into it" any more.
    EXPECT_EQ(insertsAt(900), 0) << "an insert was offered for a ghost dragged well past the destination";
}

TEST_F(GraphEditorTest, SmartConnectionPlainSuggestionKeepsTheLeftToRightFlowRule) {
    // The relaxation is scoped to the insert path. WITHOUT the modifier, a ghost sitting to the right
    // of a module must still not have that module's outputs wrapped back into it.
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 900);
    editor.setSmartConnectionMode(GraphEditor::SmartConnectionMode::NewAndUnwired);
    editor.setInsertModifierOverrideForTests(false); // no Ctrl

    auto& graph = engine.getGraph();
    auto delayNode = graph.addNode(std::make_unique<DelayModule>());
    delayNode->properties.set("x", 400);
    delayNode->properties.set("y", kFxLaneY);
    editor.updateComponents();
    sizeModuleComponents(editor);

    // Ghost centred to the RIGHT of the Delay: its own output must not be proposed backwards into
    // the Delay's input, and the Delay must not be proposed as a source into the ghost's left inputs
    // from a position the ghost has already passed.
    DummyDragSource dummySource;
    juce::DragAndDropTarget::SourceDetails d(juce::var("Chorus"), &dummySource, juce::Point<int>(760, kFxLaneY));
    editor.itemDragEnter(d);
    editor.itemDragMove(d);
    for (const auto& s : editor.getSmartSuggestions()) {
        EXPECT_FALSE(s.isInsert) << "no modifier, so nothing may be rerouted";
        EXPECT_FALSE(s.ghostIsSource) << "a ghost to the right must not feed the module on its left";
    }
    editor.endDragPreview();
}

// --- Vertical aim: the whole destination card, both axes ---------------------
//
// Reported as "it doesn't always recognize the audio output, only when I dragged it a bit below".
// The vertical acceptance region was never the problem: it already spanned the card plus a generous
// margin. What actually happened is that only ONE neighbour's candidate group survives selection,
// and a ghost being spliced into a cable sits between TWO valid neighbours — the upstream it is
// being inserted after is a perfectly good plain "feed the new module" candidate. It was winning on
// proximity and discarding the insert, and which of the two won flipped with small cursor moves, so
// nudging down appeared to fix it. Inserts now outrank plain candidates.

/** Insert offered at each cursor Y over a vertical span, for a destination at destY. */
struct VerticalAimProbe {
    int firstHit = -1, lastHit = -1, hits = 0, gaps = 0;
};
static VerticalAimProbe probeVerticalAim(GraphEditor& editor, juce::AudioProcessorGraph::NodeID destId, int cursorX,
                                         int fromY, int toY, int step) {
    VerticalAimProbe p;
    DummyDragSource ds;
    bool wasHit = false;
    for (int cy = fromY; cy <= toY; cy += step) {
        juce::DragAndDropTarget::SourceDetails d(juce::var("Chorus"), &ds, juce::Point<int>(cursorX, cy));
        editor.itemDragEnter(d);
        editor.itemDragMove(d);
        bool hit = false;
        for (const auto& s : editor.getSmartSuggestions())
            if (s.isInsert && s.neighborId == destId)
                hit = true;
        editor.endDragPreview();

        if (hit) {
            if (p.firstHit < 0)
                p.firstHit = cy;
            p.lastHit = cy;
            ++p.hits;
        } else if (wasHit && cy < p.lastHit) {
            ++p.gaps;
        }
        wasHit = hit;
    }
    // A gap is a miss strictly between two hits.
    return p;
}

/** Sizes every card the way the app does, from the footprint table, rather than one uniform size.
 *  Load-bearing here: the Audio Output card is only ~100px tall, so a uniform 300px would not
 *  reproduce anything about it. */
static void sizeModuleComponentsRealistically(GraphEditor& editor) {
    auto* content = editor.getChildComponent(0);
    ASSERT_NE(content, nullptr);
    for (auto* child : content->getChildren())
        if (auto* mc = dynamic_cast<ModuleComponent*>(child)) {
            const auto size = GraphEditor::estimateModuleSize(mc->getModule()->getName());
            mc->setSize(size.x, size.y);
        }
}

class SmartConnectionVerticalAimTest : public ::testing::TestWithParam<bool> {};

TEST_P(SmartConnectionVerticalAimTest, InsertIsOfferedAcrossTheWholeDestinationCardHeight) {
    const bool destIsAudioOutput = GetParam();
    const bool nearUpstream = true; // the real shape: last FX sits right beside the destination

    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(2000, 1600);
    editor.setSmartConnectionMode(GraphEditor::SmartConnectionMode::NewAndUnwired);
    editor.setInsertModifierOverrideForTests(true);

    auto& graph = engine.getGraph();
    constexpr int kDestX = 460;
    constexpr int kLaneY = 400;

    juce::AudioProcessorGraph::NodeID destId;
    if (destIsAudioOutput) {
        destId = addAudioOutputNode(graph, kDestX, kLaneY)->nodeID;
    } else {
        auto d = graph.addNode(std::make_unique<DelayModule>());
        d->properties.set("x", kDestX);
        d->properties.set("y", kLaneY);
        destId = d->nodeID;
    }
    auto up = graph.addNode(std::make_unique<ReverbModule>());
    up->properties.set("x", nearUpstream ? 120 : 40);
    up->properties.set("y", kLaneY);
    editor.updateComponents();
    sizeModuleComponentsRealistically(editor);
    editor.connectPorts(up->nodeID, 0, destId, 0, false, false);

    int destTop = 0, destBottom = 0;
    for (auto* child : editor.getChildComponent(0)->getChildren())
        if (auto* mc = dynamic_cast<ModuleComponent*>(child))
            if (mc->getNodeId() == destId) {
                destTop = mc->getY();
                destBottom = mc->getBottom();
            }
    ASSERT_GT(destBottom, destTop);

    const auto probe = probeVerticalAim(editor, destId, /*cursorX=*/390, /*fromY=*/destTop - 300,
                                        /*toY=*/destBottom + 300, /*step=*/10);

    ASSERT_GT(probe.hits, 0) << "no insert offered at ANY cursor height over the destination";
    EXPECT_EQ(probe.gaps, 0) << "the vertical window has holes in it, which is what 'flaky' means";

    // Aiming anywhere over the card body must work, same rule as the horizontal axis.
    EXPECT_LE(probe.firstHit, destTop) << "aiming at the card's top edge is refused";
    EXPECT_GE(probe.lastHit, destBottom) << "aiming at the card's bottom edge is refused";
}

INSTANTIATE_TEST_SUITE_P(DestinationKinds, SmartConnectionVerticalAimTest, ::testing::Values(true, false),
                         [](const testing::TestParamInfo<bool>& i) {
                             return i.param ? "AudioOutput" : "OrdinaryModule";
                         });

TEST_F(GraphEditorTest, SmartConnectionInsertOutranksAPlainSuggestionFromTheUpstream) {
    // The arbitration bug in isolation: the upstream is a valid plain neighbour AND the destination
    // is a valid insert target. Only one group survives, and it must be the insert.
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(2000, 1200);
    editor.setSmartConnectionMode(GraphEditor::SmartConnectionMode::NewAndUnwired);
    editor.setInsertModifierOverrideForTests(true);

    auto& graph = engine.getGraph();
    auto outNode = addAudioOutputNode(graph, 460, 400);
    auto up = graph.addNode(std::make_unique<ReverbModule>());
    up->properties.set("x", 120); // close enough to be its own candidate
    up->properties.set("y", 400);
    editor.updateComponents();
    sizeModuleComponentsRealistically(editor);
    editor.connectPorts(up->nodeID, 0, outNode->nodeID, 0, false, false);

    DummyDragSource ds;
    juce::DragAndDropTarget::SourceDetails d(juce::var("Chorus"), &ds, juce::Point<int>(390, 450));
    editor.itemDragEnter(d);
    editor.itemDragMove(d);

    ASSERT_GT(editor.getSmartSuggestionCount(), 0);
    for (const auto& s : editor.getSmartSuggestions()) {
        EXPECT_TRUE(s.isInsert) << "a plain suggestion from the upstream hid the insert";
        EXPECT_EQ(s.neighborId, outNode->nodeID);
        EXPECT_EQ(s.upstreamId, up->nodeID);
    }
    editor.endDragPreview();
}

// --- Double-click port disconnect (issue #216) -------------------------------

static ModuleComponent* findModuleComp(GraphEditor& editor, juce::AudioProcessor* proc) {
    auto* content = editor.getChildComponent(0);
    if (content == nullptr)
        return nullptr;
    for (auto* child : content->getChildren())
        if (auto* mc = dynamic_cast<ModuleComponent*>(child))
            if (mc->getModule() == proc)
                return mc;
    return nullptr;
}

static juce::MouseEvent makeModuleClick(juce::Component& comp, juce::Point<int> position, int clicks) {
    const auto pos = position.toFloat();
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), pos, juce::ModifierKeys(), 0.0f, 0.0f,
                            0.0f, 0.0f, 0.0f, &comp, &comp, juce::Time::getCurrentTime(), pos,
                            juce::Time::getCurrentTime(), clicks, false);
}

TEST_F(GraphEditorTest, DoubleClickConnectedPortDisconnectsByDefault) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    auto oscNode = engine.getGraph().addNode(std::make_unique<OscillatorModule>());
    auto vcaNode = engine.getGraph().addNode(std::make_unique<VCAModule>());
    editor.updateComponents();
    sizeModuleComponents(editor);

    auto* oscComp = findModuleComp(editor, oscNode->getProcessor());
    auto* vcaComp = findModuleComp(editor, vcaNode->getProcessor());
    ASSERT_NE(oscComp, nullptr);
    ASSERT_NE(vcaComp, nullptr);

    editor.connectPorts(oscNode->nodeID, 0, vcaNode->nodeID, 0, false, false);
    ASSERT_EQ(countAudioConnectionsBetween(engine.getGraph(), oscNode->nodeID, vcaNode->nodeID), 1);
    ASSERT_TRUE(editor.isPortConnected(oscComp, 0, false, false));
    ASSERT_TRUE(editor.getDoubleClickPortDisconnectEnabled());

    oscComp->mouseDown(makeModuleClick(*oscComp, oscComp->getPortCenter(0, false), 2));

    EXPECT_EQ(countAudioConnectionsBetween(engine.getGraph(), oscNode->nodeID, vcaNode->nodeID), 0);
    EXPECT_FALSE(editor.isPortConnected(oscComp, 0, false, false));
    EXPECT_FALSE(editor.isPortConnected(vcaComp, 0, true, false));
}

TEST_F(GraphEditorTest, DoubleClickDoesNotDisconnectWhenPreferenceOff) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);
    editor.setDoubleClickPortDisconnectEnabled(false);

    auto oscNode = engine.getGraph().addNode(std::make_unique<OscillatorModule>());
    auto vcaNode = engine.getGraph().addNode(std::make_unique<VCAModule>());
    editor.updateComponents();
    sizeModuleComponents(editor);

    auto* oscComp = findModuleComp(editor, oscNode->getProcessor());
    ASSERT_NE(oscComp, nullptr);

    editor.connectPorts(oscNode->nodeID, 0, vcaNode->nodeID, 0, false, false);
    ASSERT_EQ(countAudioConnectionsBetween(engine.getGraph(), oscNode->nodeID, vcaNode->nodeID), 1);

    oscComp->mouseDown(makeModuleClick(*oscComp, oscComp->getPortCenter(0, false), 2));

    EXPECT_EQ(countAudioConnectionsBetween(engine.getGraph(), oscNode->nodeID, vcaNode->nodeID), 1);
}

TEST_F(GraphEditorTest, DoubleClickUnconnectedPortIsANoOp) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    auto oscNode = engine.getGraph().addNode(std::make_unique<OscillatorModule>());
    editor.updateComponents();
    sizeModuleComponents(editor);

    auto* oscComp = findModuleComp(editor, oscNode->getProcessor());
    ASSERT_NE(oscComp, nullptr);
    ASSERT_FALSE(editor.isPortConnected(oscComp, 0, false, false));

    oscComp->mouseDown(makeModuleClick(*oscComp, oscComp->getPortCenter(0, false), 2));

    EXPECT_EQ(engine.getGraph().getConnections().size(), 0u);
    EXPECT_FALSE(editor.isPortConnected(oscComp, 0, false, false));
}

// --- Output-card identity treatment (module chrome) --------------------------
// GraphEditor::setOutputDeviceInfoProvider / refreshOutputDeviceInfo: MainComponent -> GraphEditor
// -> the Audio Output ModuleComponent, refreshed only when told to (no polling — see
// ModuleComponent::setOutputDeviceInfoText and docs/layout.md's module chrome section).

TEST_F(GraphEditorTest, RefreshOutputDeviceInfoPushesProviderTextToTheOutputCard) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    auto outNode = addAudioOutputNode(engine.getGraph(), 400, 100);
    editor.updateComponents();
    sizeModuleComponents(editor);

    auto* outComp = findModuleComp(editor, outNode->getProcessor());
    ASSERT_NE(outComp, nullptr);
    EXPECT_TRUE(outComp->getOutputDeviceInfoTextForTest().isEmpty()) << "nothing pushed in yet";

    editor.setOutputDeviceInfoProvider([] { return juce::String("Test Device \xc2\xb7 48 kHz \xc2\xb7 2ch"); });
    editor.refreshOutputDeviceInfo();

    EXPECT_EQ(outComp->getOutputDeviceInfoTextForTest(), juce::String("Test Device \xc2\xb7 48 kHz \xc2\xb7 2ch"));
}

TEST_F(GraphEditorTest, RefreshOutputDeviceInfoLeavesOtherCardsUntouched) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    auto outNode = addAudioOutputNode(engine.getGraph(), 400, 100);
    auto oscNode = engine.getGraph().addNode(std::make_unique<OscillatorModule>());
    editor.updateComponents();
    sizeModuleComponents(editor);

    auto* oscComp = findModuleComp(editor, oscNode->getProcessor());
    ASSERT_NE(oscComp, nullptr);
    juce::ignoreUnused(outNode);

    editor.setOutputDeviceInfoProvider([] { return juce::String("Test Device \xc2\xb7 48 kHz \xc2\xb7 2ch"); });
    editor.refreshOutputDeviceInfo();

    EXPECT_TRUE(oscComp->getOutputDeviceInfoTextForTest().isEmpty())
        << "the identity treatment (and the text it carries) is Audio-Output-only";
}

TEST_F(GraphEditorTest, RefreshOutputDeviceInfoWithNoProviderIsANoOp) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    auto outNode = addAudioOutputNode(engine.getGraph(), 400, 100);
    editor.updateComponents();
    sizeModuleComponents(editor);

    auto* outComp = findModuleComp(editor, outNode->getProcessor());
    ASSERT_NE(outComp, nullptr);

    // No provider installed (e.g. before MainComponent wires one up) — must not crash.
    EXPECT_NO_THROW(editor.refreshOutputDeviceInfo());
    EXPECT_TRUE(outComp->getOutputDeviceInfoTextForTest().isEmpty());
}

// Hosted mode (or a device that just closed) degrades to an empty provider result, which must
// clear a previously-shown line rather than leaving it stale.
TEST_F(GraphEditorTest, RefreshOutputDeviceInfoClearsTextWhenProviderReturnsEmpty) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    auto outNode = addAudioOutputNode(engine.getGraph(), 400, 100);
    editor.updateComponents();
    sizeModuleComponents(editor);

    auto* outComp = findModuleComp(editor, outNode->getProcessor());
    ASSERT_NE(outComp, nullptr);

    editor.setOutputDeviceInfoProvider([] { return juce::String("Test Device \xc2\xb7 48 kHz \xc2\xb7 2ch"); });
    editor.refreshOutputDeviceInfo();
    ASSERT_FALSE(outComp->getOutputDeviceInfoTextForTest().isEmpty());

    editor.setOutputDeviceInfoProvider([] { return juce::String(); }); // e.g. HostMode::Hosted
    editor.refreshOutputDeviceInfo();
    EXPECT_TRUE(outComp->getOutputDeviceInfoTextForTest().isEmpty());
}
