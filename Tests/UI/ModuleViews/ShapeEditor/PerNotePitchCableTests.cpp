// Choosing By hand or By pitch in the shape window cables the Filter's Pitch input from the voice chain's Pitch source
// in the same undo step, so each note can sit at its own shape; the hint line shows while that cannot be done.
// Regression test for FRO952: a poly synth never cabled the Filter's Pitch, so every note fell back to the shared
// cursor and per-note morph sounded like it was off.
#include "../../Graph/GraphEditor/GraphEditorTestHelpers.h"
#include "../../Graph/ModuleComponent/ModuleComponentTestFixture.h"
#include "AI/AIStateMapper/AIStateMapper.h"
#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "Mixer/ChannelFlows/ChannelFlows.h"
#include "Modules/FilterModule.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/ModuleViews/ShapeEditor/ShapeEditorWindow.h"
#include "UI/ModuleViews/ShapeEditor/ShapeListEditor.h"

using namespace synth::ui;
using Mode = synth::CustomFilterPerNote::Mode;
using NodeID = juce::AudioProcessorGraph::NodeID;

namespace {

struct PitchWindowRig {
    AudioEngine engine{AudioEngine::HostMode::Hosted};
    AppUndoManager undo;
    GraphEditor editor{engine, &undo};
    NodeID polyMidi, osc, filterId;
    FilterModule* filter = nullptr;
    std::unique_ptr<ShapeListEditor> window;

    NodeID add(const juce::String& type) {
        auto node = engine.getGraph().addNode(synth::AIStateMapper::createModule(type));
        node->properties.set("x", x += 320);
        node->properties.set("y", 100);
        return node->nodeID;
    }
    static int jackOf(juce::AudioProcessor* p, PortRole role, bool input) {
        auto* module = dynamic_cast<ModuleBase*>(p);
        for (int ch = 0; ch < 40; ++ch) {
            const auto port = input ? module->mapInputChannel(ch) : module->mapOutputChannel(ch);
            if (port.role == role && port.isPolyGroupHead)
                return port.visibleJackIndex;
        }
        return -1;
    }
    juce::AudioProcessor* proc(NodeID id) { return engine.getGraph().getNodeForId(id)->getProcessor(); }

    /** Poly MIDI (-> Oscillator pitch when `cabled`) -> Oscillator audio -> Filter, all Poly. */
    explicit PitchWindowRig(bool cabled = true) {
        engine.getGraph().setPlayConfigDetails(2, 2, 44100.0, 512);
        undo.setGraphEditor(&editor);
        editor.setSize(3000, 1600);
        polyMidi = add("Poly MIDI");
        osc = add("Oscillator");
        filterId = add("Filter");
        synth::setProcessorPoly(proc(osc), true);
        synth::setProcessorPoly(proc(filterId), true);
        filter = dynamic_cast<FilterModule*>(proc(filterId));
        editor.updateComponents();
        if (cabled)
            editor.connectPorts(polyMidi, 0, osc, jackOf(proc(osc), PortRole::Pitch, true), false, false);
        editor.connectPorts(osc, jackOf(proc(osc), PortRole::Audio, false), filterId, 0, false, false);
    }

    ShapeListEditor& open() {
        test_hooks::shapeEditorWindowHookForTest() = [this](std::unique_ptr<ShapeListEditor> e) {
            window = std::move(e);
        };
        findModuleComp(editor, filter)->openShapesWindow(0);
        test_hooks::shapeEditorWindowHookForTest() = nullptr;
        return *window;
    }

    int pitchEdges() {
        int n = 0;
        for (const auto& c : engine.getGraph().getConnections())
            if (c.destination.nodeID == filterId && c.destination.channelIndex >= FilterModule::kPitchBase &&
                c.destination.channelIndex < FilterModule::kPitchBase + 8)
                ++n;
        return n;
    }

private:
    int x = 0;
};

void pick(ShapeListEditor& editor, Mode mode) {
    editor.getPerNoteBar()->getModeSwitch().setSelectedIndex((int)mode, juce::sendNotification);
}

} // namespace

TEST_F(ModuleComponentTest, ChoosingByPitchCablesTheFiltersPitchAndOneUndoTakesBothBack) {
    PitchWindowRig rig;
    auto& editor = rig.open();
    ASSERT_EQ(rig.pitchEdges(), 0);
    const int serial = rig.undo.getEditSerial();

    pick(editor, Mode::ByPitch);
    EXPECT_EQ(rig.filter->getCustomPerNote().mode, Mode::ByPitch);
    EXPECT_EQ(rig.pitchEdges(), 8) << "the whole poly fan";
    EXPECT_EQ(rig.undo.getEditSerial(), serial + 1);
    EXPECT_FALSE(editor.getPerNoteBar()->isHintShown());

    rig.undo.undo();
    EXPECT_EQ(rig.pitchEdges(), 0);
    EXPECT_EQ(rig.filter->getCustomPerNote().mode, Mode::Off);
}

TEST_F(ModuleComponentTest, ChoosingByHandAlsoCablesAndAnAlreadyCabledPitchGetsNoSecondCable) {
    PitchWindowRig rig;
    auto& editor = rig.open();
    pick(editor, Mode::ByHand);
    EXPECT_EQ(rig.pitchEdges(), 8);
    pick(editor, Mode::ByPitch);
    EXPECT_EQ(rig.pitchEdges(), 8) << "no second cable";
    EXPECT_EQ(rig.filter->getCustomPerNote().mode, Mode::ByPitch);
}

TEST_F(ModuleComponentTest, RandomNeedsNoPitchCable) {
    PitchWindowRig rig;
    auto& editor = rig.open();
    pick(editor, Mode::Random);
    EXPECT_EQ(rig.pitchEdges(), 0);
    EXPECT_FALSE(editor.getPerNoteBar()->isHintShown());
}

TEST_F(ModuleComponentTest, WithNoPitchSourceUpstreamTheModeIsSetNothingIsCabledAndTheHintShows) {
    PitchWindowRig rig(/*cabled*/ false);
    auto& editor = rig.open();
    EXPECT_EQ(rig.filter->getCustomPerNote().mode, Mode::Off);
    pick(editor, Mode::ByPitch);
    EXPECT_EQ(rig.filter->getCustomPerNote().mode, Mode::ByPitch);
    EXPECT_EQ(rig.pitchEdges(), 0);
    auto& bar = *editor.getPerNoteBar();
    EXPECT_TRUE(bar.isHintShown());
    EXPECT_TRUE(bar.getHint().getText().startsWith("Needs a Pitch cable"));
    EXPECT_EQ(bar.getHint().getTitle(), bar.getHint().getText());
    pick(editor, Mode::Off);
    EXPECT_FALSE(bar.isHintShown());
}

TEST_F(ModuleComponentTest, AMonoFilterShowsThePolyHint) {
    PitchWindowRig rig;
    synth::setProcessorPoly(rig.proc(rig.filterId), false);
    auto& editor = rig.open();
    pick(editor, Mode::Random);
    EXPECT_TRUE(editor.getPerNoteBar()->isHintShown());
    EXPECT_TRUE(editor.getPerNoteBar()->getHint().getText().startsWith("Per-note works in Poly"));
}
