#pragma once

// DualIOGlideRig.h -- an Oscillator (Dual I/O on) wired Left and Right into a Channel Strip (Dual I/O off), the
// Oscillator track's shape, with the real AppUndoManager. The strip card is painted once, as it is on screen before a
// click, so its jacks are known where they were drawn. Shared by DualIOJackGlideTests and the undo-glide registry.

#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "Modules/ChannelStripModule.h"
#include "Modules/OscillatorModule.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/DualIOJackGlide.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include <optional>

namespace dual_io_glide_test {

using NodeID = juce::AudioProcessorGraph::NodeID;

inline juce::AudioProcessorParameter* dualParamOf(juce::AudioProcessor& proc) {
    for (auto* p : proc.getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(p); withId && withId->paramID == "dualIO")
            return p;
    return nullptr;
}

struct Rig {
    static constexpr int kStripRight = ChannelStripModule::kRightBase;
    static constexpr int kOscRight = OscillatorModule::kRightBase;

    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor{engine, &undo};
    NodeID osc, strip;
    ModuleComponent* card = nullptr;

    /** `oscDual` false: the Oscillator is one Audio jack too (a new track's shape), so a collapsed strip draws no Right
     *  cable at rest. */
    explicit Rig(bool oscDual = true) {
        undo.setGraphEditor(&editor);
        editor.setSize(1400, 900);
        DualIOJackGlide::setAnimateOffScreenForTest(true);
        auto& graph = engine.getGraph();
        auto oscNode = graph.addNode(std::make_unique<OscillatorModule>());
        oscNode->properties.set("uuid", juce::Uuid().toDashedString());
        oscNode->properties.set("x", 40);
        oscNode->properties.set("y", 100);
        auto stripNode = graph.addNode(std::make_unique<ChannelStripModule>());
        stripNode->properties.set("uuid", juce::Uuid().toDashedString());
        stripNode->properties.set("x", 600);
        stripNode->properties.set("y", 100);
        osc = oscNode->nodeID;
        strip = stripNode->nodeID;
        setDual(*oscNode->getProcessor(), oscDual);
        setDual(*stripNode->getProcessor(), false);
        editor.updateComponents();
        graph.addConnection({{osc, 0}, {strip, 0}});
        graph.addConnection({{osc, kOscRight}, {strip, kStripRight}});
        editor.updateComponents();
        for (auto* mc : editor.getModuleComponents())
            if (mc != nullptr && mc->getNodeId() == strip)
                card = mc;
        paintCard();
    }
    ~Rig() { DualIOJackGlide::setAnimateOffScreenForTest(false); }

    static void setDual(juce::AudioProcessor& proc, bool on) {
        if (auto* p = dualParamOf(proc))
            p->setValueNotifyingHost(on ? 1.0f : 0.0f);
    }

    /** The Dual I/O button's click: one parameter gesture, one undo step. */
    void clickDualIO() {
        auto* p = dualParamOf(*engine.getGraph().getNodeForId(strip)->getProcessor());
        const bool on = p->getValue() < 0.5f;
        p->beginChangeGesture();
        p->setValueNotifyingHost(on ? 1.0f : 0.0f);
        p->endChangeGesture();
    }

    /** What the screen shows: the card painted into an image (the card remembers the jacks it drew). */
    void paintCard() {
        juce::Image img(juce::Image::ARGB, std::max(1, card->getWidth()), std::max(1, card->getHeight()), true,
                        juce::SoftwareImageType());
        juce::Graphics g(img);
        card->paintEntireComponent(g, false);
    }

    DualIOJackGlide* glide() const { return card->getDualIOGlide(); }
    bool gliding() const { return glide() != nullptr && glide()->isLive(); }
    void stepTo(double ms) {
        if (auto* g = glide())
            g->stepForTest(ms);
    }

    /** Card-local centre of visible input jack `visible` as drawn now. */
    juce::Point<int> drawnInput(int visible) const {
        const auto laidOut = card->getPortCenter(visible, true);
        return glide() != nullptr ? glide()->drawnJack(visible, true, laidOut) : laidOut;
    }
    juce::Point<int> drawnOutput(int visible) const {
        const auto laidOut = card->getPortCenter(visible, false);
        return glide() != nullptr ? glide()->drawnJack(visible, false, laidOut) : laidOut;
    }

    /** Card-local end of the cable from the Oscillator's `oscChannel`, as drawn now. */
    std::optional<juce::Point<float>> cableEndFrom(int oscChannel) {
        for (const auto& c : editor.buildVisibleCables())
            if (c.id.srcUid == osc.uid && c.id.srcPort == oscChannel && c.id.dstUid == strip.uid)
                return c.p2 - card->getBounds().getPosition().toFloat();
        return std::nullopt;
    }
    /** The Right cable's end height on the card; -1 when it is not drawn. */
    double rightCableY() {
        const auto end = cableEndFrom(kOscRight);
        return end.has_value() ? (double)end->y : -1.0;
    }
    /** The strip's first body control below its jacks (the Gain knob's row), and its drawn top on the card. */
    juce::Component* bodyControl() const {
        juce::Component* lowest = nullptr;
        for (auto* c : card->getChildren())
            if (c->isVisible() && c->getY() > card->getPortCenter(0, true).y + 8 &&
                (lowest == nullptr || c->getY() < lowest->getY()))
                lowest = c;
        return lowest;
    }
    static double drawnTop(const juce::Component& c) { return c.getY() + c.getTransform().getTranslationY(); }
};

} // namespace dual_io_glide_test
