// GraphEditor core tests: initialization/resizing, mod-matrix visibility, module drag-and-drop
// (including dual I/O default preferences and split-block collapse), audio-file drop on the canvas,
// drag-to-knob modulation routing, port-drag connections, and Replace Module (undo, position,
// audio/MIDI connection preservation).
// Shared GraphEditorTest fixture and helpers live in GraphEditorTestHelpers.h.

#include "GraphEditorTestHelpers.h"

#include "../../Source/AI/AIStateMapper/AIStateMapper.h"
#include "../../Source/AppUndoManager.h"
#include "../../Source/Mixer/MasterSplice.h"
#include "../../Source/Modules/ADSRModule.h"
#include "../../Source/Modules/AttenuverterModule.h"
#include "../../Source/Modules/FX/BitcrusherModule.h"
#include "../../Source/Modules/FX/ChorusModule.h"
#include "../../Source/Modules/FX/DelayModule.h"
#include "../../Source/Modules/FX/DistortionModule.h"
#include "../../Source/Modules/FX/ReverbModule.h"
#include "../../Source/Modules/FX/RingModulatorModule.h"
#include "../../Source/Modules/FilterModule.h"
#include "../../Source/Modules/LFOModule.h"
#include "../../Source/Modules/MasterModule.h"
#include "../../Source/Modules/MathModule.h"
#include "../../Source/Modules/MidiKeyboardModule.h"
#include "../../Source/Modules/ModuleBase.h"
#include "../../Source/Modules/OscillatorModule.h"
#include "../../Source/Modules/PolyMidiModule.h"
#include "../../Source/Modules/SamplerModule.h"
#include "../../Source/Modules/SequencerModule.h"
#include "../../Source/Modules/VCAModule.h"
#include "../../Source/Modules/WavetableOscillatorModule.h"
#include "../../Source/PresetManager.h"
#include "../../Source/UI/LayoutUtil.h"
#include "../../Source/UI/Theme/BuiltInThemes.h"

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
