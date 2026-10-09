#pragma once

// Shared fixture for the port connections panel's tests: one oscillator wired to a few VCAs on a real GraphEditor,
// helpers that deliver real mouse events to a card's jack, and a launcher that keeps the panel the click opened (a
// real window needs a display). Header-only; not compiled on its own and not registered in Tests/CMakeLists.txt.

#include "../GraphEditor/GraphEditorTestHelpers.h"

#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "Modules/ModuleBase.h"
#include "Modules/OscillatorModule.h"
#include "Modules/VCAModule.h"
#include "UI/Graph/PortPanel/PortConnectionsPanel.h"
#include "UI/Graph/PortPanel/PortPanelController.h"
#include <juce_audio_processors/juce_audio_processors.h>

namespace {

using synth::ui::PortConnectionRow;
using synth::ui::PortConnectionsPanel;

// A mouse event on `comp`: at `pos`, pressed at `downPos` (a drag when they differ), the `clicks`-th of a run.
inline juce::MouseEvent portEvent(juce::Component& comp, juce::Point<int> pos, juce::Point<int> downPos,
                                  juce::ModifierKeys mods = {}, int clicks = 1) {
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), pos.toFloat(), mods, 0.0f, 0.0f, 0.0f,
                            0.0f, 0.0f, &comp, &comp, juce::Time::getCurrentTime(), downPos.toFloat(),
                            juce::Time::getCurrentTime(), clicks, pos != downPos);
}

// Button::triggerClick() posts the click as a command message, so it takes effect after one dispatch pass.
inline void clickNow(juce::Button& button) {
    button.triggerClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(30);
}

inline void pressKey(synth::ui::ModDotGlyphButton& button, const juce::KeyPress& key) {
    button.keyPressed(key);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(30);
}

struct PortFixture {
    AudioEngine engine;
    AppUndoManager undo;
    std::unique_ptr<GraphEditor> editor;
    juce::AudioProcessorGraph::NodeID oscId;
    std::vector<juce::AudioProcessorGraph::NodeID> vcaIds;
    // The panel a click opened, and where: a real window needs a display, so the launcher keeps it instead.
    std::unique_ptr<juce::Component> held;
    juce::Rectangle<int> launchedAt;
    int launches = 0;

    // `wired` VCAs get a cable from the oscillator's output, `loose` more VCAs are left unwired.
    explicit PortFixture(int wired, int loose = 0) {
        engine.initialise();
        engine.getGraph().clear();
        editor = std::make_unique<GraphEditor>(engine, &undo);
        undo.setGraphEditor(editor.get());
        auto& graph = engine.getGraph();
        auto osc = graph.addNode(std::make_unique<OscillatorModule>());
        osc->properties.set("x", 40);
        osc->properties.set("y", 40);
        oscId = osc->nodeID;
        for (int i = 0; i < wired + loose; ++i) {
            auto vca = graph.addNode(std::make_unique<VCAModule>());
            vca->properties.set("x", 500);
            vca->properties.set("y", 40 + i * 320);
            vcaIds.push_back(vca->nodeID);
        }
        engine.updateModuleNames();
        editor->setSize(1200, 1400);
        editor->updateComponents();
        sizeModuleComponents(*editor);
        for (int i = 0; i < wired; ++i)
            editor->connectPorts(oscId, 0, vcaIds[(size_t)i], 0, false, false);
        editor->getPortPanel().panelLauncher = [this](std::unique_ptr<juce::Component> content, juce::Component&,
                                                      juce::Rectangle<int> screenAnchor) {
            held = std::move(content);
            launchedAt = screenAnchor;
            ++launches;
        };
        editor->timerCallback();
    }

    // Cards go before the engine frees their processors.
    ~PortFixture() {
        held.reset();
        editor.reset();
        engine.shutdown();
    }

    ModuleComponent* card(juce::AudioProcessorGraph::NodeID id) {
        for (auto* c : editor->getModuleComponents())
            if (c != nullptr && c->getNodeId() == id)
                return c;
        return nullptr;
    }
    ModuleComponent* oscCard() { return card(oscId); }
    ModuleBase* module(juce::AudioProcessorGraph::NodeID id) {
        auto* node = engine.getGraph().getNodeForId(id);
        return node != nullptr ? dynamic_cast<ModuleBase*>(node->getProcessor()) : nullptr;
    }
    synth::ui::PortPanelController& controller() { return editor->getPortPanel(); }
    PortConnectionsPanel* panel() { return controller().getPanel(); }
    juce::String titleOf(juce::AudioProcessorGraph::NodeID id) { return editor->getModuleTitle(id, module(id)); }
    juce::String dot() const { return juce::String::fromUTF8(" \xC2\xB7 "); }

    juce::Point<int> oscOut() { return oscCard()->getPortCenter(0, false); }
    // The text a row for the oscillator-to-VCA cable reads (from the VCA's side): "<VCA title> . <its jack>".
    juce::String vcaRowText(int i) {
        return titleOf(vcaIds[(size_t)i]) + dot() + module(vcaIds[(size_t)i])->getInputPortLabel(0);
    }

    // A plain click: press and release on the same spot.
    void click(ModuleComponent& c, juce::Point<int> p, int clicks = 1, juce::ModifierKeys mods = {}) {
        c.mouseDown(portEvent(c, p, p, mods, clicks));
        c.mouseUp(portEvent(c, p, p, mods, clicks));
    }
    void clickOscOutput(int clicks = 1) { click(*oscCard(), oscOut(), clicks); }

    int cablesBetweenOscAnd(int vca) {
        return countAudioConnectionsBetween(engine.getGraph(), oscId, vcaIds[(size_t)vca]);
    }
    int totalOscCables() {
        int n = 0;
        for (auto id : vcaIds)
            n += countAudioConnectionsBetween(engine.getGraph(), oscId, id);
        return n;
    }
    void refresh() {
        editor->updateComponents();
        editor->timerCallback();
    }
};

} // namespace
