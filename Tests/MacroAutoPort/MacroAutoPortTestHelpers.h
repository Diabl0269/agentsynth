#pragma once

// Shared test modules and helpers for the MacroAutoPort test suite
// (Tests/MacroAutoPort/MacroAutoPort*Tests.cpp). Header-only; not compiled on its own and not
// registered in Tests/CMakeLists.txt.

#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include <atomic>
#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>

using NodeID = juce::AudioProcessorGraph::NodeID;

namespace {

// A plain mono module: 1 audio in, 1 audio out, MIDI accepted/produced (ModuleBase's own
// defaults) — deliberately dumb, so a test's crossing connections exercise only the shape/kind
// derivation under test, never a real module's own quirks.
class TestMonoModule : public ModuleBase {
public:
    TestMonoModule()
        : ModuleBase("TestMono", 1, 1) {}
    void prepareToPlay(double, int) override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
    ModuleType getModuleType() const override { return ModuleType::Math; }
};

// A single Poly-8 ModCV bus, both directions — the shape §7 item 6.1 calls "a poly-bus crossing".
// Mirrors PolyMidiModule's own pitch-fan mapping (raw 0-7, head at 0, span 8, ModCV role) but
// isolated from that module's other quirks (its own getVisibleOutputPortCount() override, a
// second Gate fan sharing jack 0) which would make a test depend on code this fix does not own.
class TestPolyCVModule : public ModuleBase {
public:
    TestPolyCVModule()
        : ModuleBase("TestPolyCV", 8, 8) {}
    void prepareToPlay(double, int) override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
    ModuleType getModuleType() const override { return ModuleType::Math; }
    int getVisibleInputPortCount() const override { return 1; }
    int getVisibleOutputPortCount() const override { return 1; }
    LogicalPort mapInputChannel(int raw) const override { return fan(raw); }
    LogicalPort mapOutputChannel(int raw) const override { return fan(raw); }

private:
    static LogicalPort fan(int raw) {
        LogicalPort p;
        if (raw >= 0 && raw < 8) {
            p.visibleJackIndex = 0;
            p.role = PortRole::ModCV;
            p.isPolyGroupHead = (raw == 0);
            p.polyVoiceSpan = 8;
        }
        return p;
    }
};

// A constant-output source for the mod-routing behavioural test below: outputs `value_` on
// channel 0 regardless of input, so the test's expected value at the far end of the spliced chain
// is a fixed number rather than an LFO's time-varying phase.
class TestConstantModule : public ModuleBase {
public:
    explicit TestConstantModule(float value)
        : ModuleBase("TestConstant", 1, 1)
        , value_(value) {}
    void prepareToPlay(double, int) override {}
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override {
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch) {
            buffer.clear(ch, 0, buffer.getNumSamples());
            if (ch == 0)
                for (int i = 0; i < buffer.getNumSamples(); ++i)
                    buffer.setSample(0, i, value_);
        }
    }
    ModuleType getModuleType() const override { return ModuleType::Math; }

private:
    float value_;
};

// A CV probe: records the last sample it saw on its own channel 0 input, so a test can confirm a
// modulation signal ACTUALLY reached the far side of a spliced port, not just that the port node
// exists and the graph edges look right.
class TestCvProbeModule : public ModuleBase {
public:
    TestCvProbeModule()
        : ModuleBase("TestProbe", 1, 1) {}
    void prepareToPlay(double, int) override {}
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override {
        if (buffer.getNumChannels() > 0 && buffer.getNumSamples() > 0)
            lastSample_.store(buffer.getReadPointer(0)[buffer.getNumSamples() - 1], std::memory_order_relaxed);
    }
    ModuleType getModuleType() const override { return ModuleType::Math; }
    float lastSample() const { return lastSample_.load(std::memory_order_relaxed); }

private:
    std::atomic<float> lastSample_{0.0f};
};

NodeID addModuleAt(GraphEditor& editor, AudioEngine& engine, std::unique_ptr<juce::AudioProcessor> processor,
                   const juce::String& name, int x, int y) {
    if (auto* mb = dynamic_cast<ModuleBase*>(processor.get()))
        if (name.isNotEmpty())
            mb->setModuleName(name);
    auto node = engine.getGraph().addNode(std::move(processor));
    node->properties.set("x", x);
    node->properties.set("y", y);
    node->properties.set("uuid", juce::Uuid().toDashedString());
    editor.updateComponents();
    return node->nodeID;
}

bool hasConnection(AudioEngine& engine, NodeID srcId, int srcCh, NodeID dstId, int dstCh) {
    for (const auto& c : engine.getGraph().getConnections())
        if (c.source.nodeID == srcId && c.source.channelIndex == srcCh && c.destination.nodeID == dstId &&
            c.destination.channelIndex == dstCh)
            return true;
    return false;
}

bool hasMidiConnection(AudioEngine& engine, NodeID srcId, NodeID dstId) {
    return hasConnection(engine, srcId, juce::AudioProcessorGraph::midiChannelIndex, dstId,
                         juce::AudioProcessorGraph::midiChannelIndex);
}

// Hand-built MouseEvent, same pattern as GraphEditorViewportTests.cpp's makeGraphEditorMouseEvent
// (Tests/GraphEditor/) — no OS mouse source exists headlessly, but MouseInputSource is copyable
// and Desktop always exposes one.
// `position` is in GraphEditor-LOCAL (screen) coordinates, the same space GraphEditor::mouseDown
// converts via content.getLocalPoint() before doing any canvas-space hit-testing.
juce::MouseEvent makeEditorMouseEvent(juce::Component& comp, juce::Point<float> position, int clicks = 1) {
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), position,
                            juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                            &comp, &comp, juce::Time::getCurrentTime(), position, juce::Time::getCurrentTime(), clicks,
                            false);
}

// Inverse of GraphEditorViewportTests.cpp's screenToCanvas (Tests/GraphEditor/): a cable's p1/p2
// (and therefore its painted knob midpoint) are in CANVAS coordinates, but a MouseEvent fed into GraphEditor itself
// must be in GraphEditor-local (screen) coordinates -- getVisibleCanvasRect() is the one linear map between the two
// (zoom + pan), so a mouse-driven test has to go through it rather than assume 1:1.
juce::Point<float> canvasToEditorLocal(const GraphEditor& editor, juce::Point<float> canvasPt) {
    const auto rect = editor.getVisibleCanvasRect();
    const auto w = static_cast<float>(editor.getWidth());
    const auto h = static_cast<float>(editor.getHeight());
    return {(canvasPt.x - rect.getX()) / rect.getWidth() * w, (canvasPt.y - rect.getY()) / rect.getHeight() * h};
}

NodeID nodeIdForUuid(AudioEngine& engine, const juce::String& uuid) {
    for (auto* node : engine.getGraph().getNodes())
        if (node->properties["uuid"].toString() == uuid)
            return node->nodeID;
    return {};
}

// The one new-port node a group produced, asserting there is exactly one. Most tests below add
// exactly one crossing group, so this is the common case; tests that expect more resolve nodes by
// direction/kind explicitly instead.
NodeID theOneNewPortNode(AudioEngine& engine, const std::vector<NodeID>& before) {
    NodeID found;
    int count = 0;
    for (auto* node : engine.getGraph().getNodes()) {
        if (std::find(before.begin(), before.end(), node->nodeID) == before.end()) {
            found = node->nodeID;
            ++count;
        }
    }
    EXPECT_EQ(count, 1) << "expected exactly one new node";
    return found;
}

std::vector<NodeID> allNodeIds(AudioEngine& engine) {
    std::vector<NodeID> ids;
    for (auto* node : engine.getGraph().getNodes())
        ids.push_back(node->nodeID);
    return ids;
}

} // namespace
