#pragma once

// Shared fixtures for the MacroPortFlow*Tests.cpp split: graph-building and lookup helpers used
// across every topic file.

#include "AI/AIStateMapper/AIStateMapper.h"
#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "Modules/FilterModule.h"
#include "Modules/LFOModule.h"
#include "Modules/MacroInletModule.h"
#include "Modules/MacroMidiOutletModule.h"
#include "Modules/MacroOutletModule.h"
#include "Modules/MidiKeyboardModule.h"
#include "Modules/OscillatorModule.h"
#include "Modules/WavetableOscillatorModule/WavetableOscillatorModule.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Library/ModuleLibraryComponent/ModuleLibraryComponent.h"
#include "UI/Macros/MacroCardComponent/MacroCardComponent.h"
#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>

using NodeID = juce::AudioProcessorGraph::NodeID;

namespace {

NodeID addModuleAt(GraphEditor& editor, AudioEngine& engine, std::unique_ptr<juce::AudioProcessor> processor, int x,
                   int y) {
    auto node = engine.getGraph().addNode(std::move(processor));
    node->properties.set("x", x);
    node->properties.set("y", y);
    node->properties.set("uuid", juce::Uuid().toDashedString());
    editor.updateComponents();
    return node->nodeID;
}

juce::String uuidOf(AudioEngine& engine, NodeID id) {
    auto* node = engine.getGraph().getNodeForId(id);
    return node != nullptr ? node->properties["uuid"].toString() : juce::String();
}

NodeID nodeIdForUuid(AudioEngine& engine, const juce::String& uuid) {
    for (auto* node : engine.getGraph().getNodes())
        if (node->properties["uuid"].toString() == uuid)
            return node->nodeID;
    return {};
}

bool hasConnection(AudioEngine& engine, NodeID srcId, int srcCh, NodeID dstId, int dstCh) {
    for (const auto& c : engine.getGraph().getConnections())
        if (c.source.nodeID == srcId && c.source.channelIndex == srcCh && c.destination.nodeID == dstId &&
            c.destination.channelIndex == dstCh)
            return true;
    return false;
}

/** Groups two fresh Oscillator/Filter modules into a new collapsed macro (the min-2 rule) and
 *  returns its id. */
juce::String makeTwoMemberMacro(GraphEditor& editor, AudioEngine& engine) {
    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 500, 100);
    editor.setSelectedNodes({a, b});
    return editor.getMacroController().groupSelectionIntoMacro();
}

/** The live ModuleComponent for `id`, or nullptr — the same lookup every drag-based test below
 *  needs to resolve a NodeID to something endConnectionDrag's own jack hit-test loop can see. */
ModuleComponent* compFor(GraphEditor& editor, NodeID id) {
    for (auto* c : editor.getModuleComponents())
        if (c != nullptr && c->getNodeId() == id)
            return c;
    return nullptr;
}

// Shared with the shape-inference and card-drop-routing topics.
// A collapsed-stereo source: ONE visible jack fronting two raw legs (span 2, Audio role) — the
// same shape a Dual-I/O-off FX module (Delay, Reverb) presents, isolated here from any of a real
// FX module's other quirks, mirroring TestPolyCVModule's own isolation rationale in
// MacroAutoPortTestHelpers.h.
class TestStereoCollapsedModule : public ModuleBase {
public:
    TestStereoCollapsedModule()
        : ModuleBase("TestStereoCollapsed", 2, 2) {}
    void prepareToPlay(double, int) override {}
    void processModuleBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
    ModuleType getModuleType() const override { return ModuleType::Math; }
    int getVisibleInputPortCount() const override { return 1; }
    int getVisibleOutputPortCount() const override { return 1; }
    LogicalPort mapInputChannel(int raw) const override { return fan(raw); }
    LogicalPort mapOutputChannel(int raw) const override { return fan(raw); }

private:
    static LogicalPort fan(int raw) {
        LogicalPort p;
        if (raw == 0 || raw == 1) {
            p.visibleJackIndex = 0;
            p.role = PortRole::Audio;
            p.isPolyGroupHead = (raw == 0);
            p.polyVoiceSpan = 2;
        }
        return p;
    }
};

// A single Poly-8 ModCV bus — mirrors MacroAutoPortTestHelpers.h's TestPolyCVModule (kept as its
// own local copy: that header is scoped to the MacroAutoPort test suite, not shared across areas).
class TestPolyCVModule : public ModuleBase {
public:
    TestPolyCVModule()
        : ModuleBase("TestPolyCV", 8, 8) {}
    void prepareToPlay(double, int) override {}
    void processModuleBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
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

} // namespace
