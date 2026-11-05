#pragma once

// The headless MIDI Remote rig the workflow tests share: a Hosted engine, a real GraphEditor and MidiLearnController
// wired the way MainComponent wires them, a fake controller on a fake clock, and the right-click helpers that drive a
// card control's own mouseDown plus the card's listener half. See MidiRemoteWorkflowE2ETests.cpp's header.

#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "MidiRemote/MidiLearnController.h"
#include "Modules/FilterModule.h"
#include "Modules/ModuleBase.h"
#include "ShortcutManager/AppCommands.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "UI/Chrome/StatusBarComponent.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"

#include <functional>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

namespace midi_remote_rig {

using namespace synth;
using namespace synth::midi;

constexpr const char* kDevice = "fake-controller-id";
constexpr const char* kFilterUuid = "workflow-filter-uuid";
constexpr int kCc = 20;
constexpr int kPadNote = 36;
constexpr double kSampleRate = 48000.0;
constexpr int kBlock = 512;

class CountingInvoker : public RemoteActionInvoker {
public:
    void invokeRemoteCommand(juce::CommandID commandId) override { invoked.push_back(commandId); }
    void invokeNodeCommand(juce::AudioProcessorGraph::NodeID, NodeCommandKind) override {}
    // This suite doesn't exercise continuous targets -- stub, never called.
    double getContinuousValue(ContinuousTargetKind) override { return 0.0; }
    void setContinuousValue(ContinuousTargetKind, double) override {}
    bool getContinuousWindow(ContinuousTargetKind, double&, double&) override { return false; }
    std::vector<juce::CommandID> invoked;
};

class CountingGestureListener : public juce::AudioProcessorParameter::Listener {
public:
    void parameterValueChanged(int, float) override {}
    void parameterGestureChanged(int, bool starting) override { (starting ? starts : ends)++; }
    int starts = 0;
    int ends = 0;
};

// One app session's worth of MIDI Remote plumbing: a Hosted engine (never opens hardware -- see Source/CLAUDE.md), a
// real GraphEditor sharing the undo manager, and the real MidiLearnController over a caller-supplied profile directory
// (never the user's settings folder). Declaration order is load-bearing (members die in reverse): the engine and
// everything that reads the graph die before the undo stack that references it.
struct Rig {
    double nowMs = 0.0;
    RemoteEngine remote;
    synth::MidiRemoteProjectDoc doc;
    synth::TimelineDoc timeline;
    AppUndoManager undo;
    StatusBarComponent statusBar;
    AudioEngine engine{AudioEngine::HostMode::Hosted};
    GraphEditor editor{engine, &undo};
    CountingInvoker invoker;
    MidiLearnController controller;
    juce::AudioProcessorGraph::Node::Ptr filter;

    explicit Rig(const juce::File& profileDir)
        : controller(engine, editor, remote, doc, undo, statusBar, synth::ControllerProfileStore(profileDir)) {
        remote.setClock([this] { return nowMs; });
        remote.setActionInvoker(&invoker);
        remote.setActionCommandLookup(
            [](const juce::String& actionId) { return AppCommands::getCommandForAction(actionId); });
        remote.setDefaultTakeover(Takeover::jump); // exact follow: the value maps straight onto the parameter
        // The same three callbacks MainComponent::wireGraphEditorCallbacks() wires.
        editor.onQueryMidiMappingsForNode = [this](juce::AudioProcessorGraph::NodeID id) {
            return controller.queryMappings(id);
        };
        editor.onMidiLearnRequested = [this](juce::AudioProcessorGraph::NodeID id, const juce::String& paramId) {
            controller.arm(id, paramId);
        };
        editor.onMidiForgetRequested = [this](juce::AudioProcessorGraph::NodeID id, const juce::String& paramId) {
            controller.forget(id, paramId);
        };
        engine.setRemoteMessageSink(&remote);
    }

    // `remote` outlives `engine` (declared first), so end any gesture a test left inside its idle
    // window while the parameter still exists -- RemoteEngine::endAllGestures()'s lifetime contract.
    ~Rig() {
        engine.setRemoteMessageSink(nullptr);
        remote.endAllGestures();
    }

    juce::AudioProcessorGraph::Node::Ptr addFilter(const juce::String& uuid) {
        return addModule(std::make_unique<FilterModule>(), uuid);
    }
    juce::AudioProcessorGraph::Node::Ptr addModule(std::unique_ptr<ModuleBase> processor, const juce::String& uuid) {
        auto node = engine.getGraph().addNode(std::move(processor));
        node->properties.set("uuid", uuid);
        if (auto* module = dynamic_cast<ModuleBase*>(node->getProcessor()))
            module->setNodeUuid(uuid);
        editor.updateComponents();
        return node;
    }

    juce::RangedAudioParameter* cutoff() { return findParameterByID(filter->getProcessor(), "cutoff"); }

    ModuleComponent* card() { return cardOf(*filter); }
    ModuleComponent* cardOf(juce::AudioProcessorGraph::Node& node) {
        for (auto* c : editor.getModuleComponents())
            if (c->getModule() == node.getProcessor())
                return c;
        return nullptr;
    }

    void advance(double ms) { nowMs += ms; }
    void hearDevice() { remote.setSources({juce::String(kDevice)}); }

    void send(const juce::MidiMessage& message) {
        engine.handleIncomingMidiMessageFromSource(kDevice, message);
        remote.drain();
    }
    void sendCc(int value) { send(juce::MidiMessage::controllerEvent(1, kCc, value)); }

    // Sweeps `values` a fifth of the idle window apart, so the whole run is one gesture.
    void sweep(std::initializer_list<int> values, const std::function<void(int)>& afterStep = {}) {
        for (const int v : values) {
            sendCc(v);
            advance(kGestureIdleMs / 5.0);
            if (afterStep)
                afterStep(v);
        }
    }
    void letGestureExpire() {
        advance(kGestureIdleMs + 1.0);
        remote.drain();
    }
};

inline juce::MouseEvent childMouseEvent(juce::Component& child, juce::ModifierKeys mods) {
    const auto pos = child.getLocalBounds().getCentre().toFloat();
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), pos, mods, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                            &child, &child, juce::Time::getCurrentTime(), pos, juce::Time::getCurrentTime(), 1, false);
}

// Right-clicks `child` and captures the PopupMenu the card builds via its setShowContextMenuHookForTest() seam. The
// control's own handler and the card's registered-MouseListener half are both driven -- see
// ModuleComponentMidiLearnTests.cpp's rightClickChild for why calling only the child would prove nothing.
inline juce::PopupMenu rightClick(ModuleComponent& card, juce::Component& child) {
    juce::PopupMenu captured;
    card.setShowContextMenuHookForTest([&](juce::PopupMenu& menu) { captured = menu; });
    const juce::ModifierKeys mods(juce::ModifierKeys::rightButtonModifier);
    const auto down = childMouseEvent(child, mods);
    child.mouseDown(down);
    card.mouseDown(down);
    child.mouseUp(childMouseEvent(child, mods));
    card.setShowContextMenuHookForTest(nullptr);
    return captured;
}

inline juce::Component* findByComponentID(ModuleComponent& card, const juce::String& id) {
    for (auto* child : card.getChildren())
        if (child->getComponentID() == id)
            return child;
    return nullptr;
}

inline bool menuContains(const juce::PopupMenu& menu, const juce::String& text) {
    juce::PopupMenu::MenuItemIterator it(menu);
    while (it.next())
        if (it.getItem().text == text)
            return true;
    return false;
}

inline bool invokeMenuItem(const juce::PopupMenu& menu, const juce::String& text) {
    juce::PopupMenu::MenuItemIterator it(menu);
    while (it.next()) {
        if (it.getItem().text == text && it.getItem().action) {
            it.getItem().action();
            return true;
        }
    }
    return false;
}

inline const RemoteMappingSnapshot::Slot* findSlot(const RemoteEngine& engine, const juce::String& assignmentId) {
    const auto* snapshot = engine.publisher().live();
    if (snapshot == nullptr)
        return nullptr;
    for (const auto& slot : snapshot->slots)
        if (slot.assignmentId == assignmentId)
            return &slot;
    return nullptr;
}

} // namespace midi_remote_rig
