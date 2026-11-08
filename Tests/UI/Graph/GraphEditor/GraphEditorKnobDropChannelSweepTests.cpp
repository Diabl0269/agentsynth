// A cable dropped on any knob, through the real drop path, lands on that knob's own channel. Every module type the app
// can create is built, every modulation target whose knob is shown on the default card gets a cable dropped on the
// centre of that knob, and the new connection must reach the target's raw channel. This is the guard for the bug kind
// "a drop resolves through a lossy index mapping and collapses onto one target": the drop maps raw channel -> visible
// jack -> raw, so a module whose mapping hands every channel the same jack sends every knob to one channel.

#include "AI/AIStateMapper/AIStateMapper.h"
#include "AudioEngine/AudioEngine.h"
#include "GraphEditorTestHelpers.h"
#include "Modules/ModuleBase.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <set>
#include <vector>

namespace {

// Module types with no card to drop on, or no continuous input to modulate.
const std::set<juce::String> kNoKnobs = {"Macros",         "Macro In",    "Macro Out",     "Macro Midi In",
                                         "Macro Midi Out", "Audio Input", "Audio Output",  "Midi Input",
                                         "MIDI Keyboard",  "Poly MIDI",   "External MIDI", "Track In",
                                         "Track Audio",    "Rec Tap",     "Hosted Plugin"};

juce::Slider* sliderWithId(juce::Component& parent, const juce::String& id) {
    for (auto* child : parent.getChildren()) {
        if (auto* slider = dynamic_cast<juce::Slider*>(child); slider != nullptr && slider->getComponentID() == id)
            return slider;
        if (auto* nested = sliderWithId(*child, id))
            return nested;
    }
    return nullptr;
}

bool shownOnCard(const juce::Component* c, const juce::Component& card) {
    for (; c != nullptr && c != &card; c = c->getParentComponent())
        if (!c->isVisible())
            return false;
    return c == &card;
}

// A fresh engine with an LFO and one module of `typeName`, so one drop never sees the cable of another.
struct KnobDrop {
    AudioEngine engine;
    // Declared before the editor so the nodes outlive its cards: a card detaches from its processor when the editor
    // is destroyed, after the destructor body has shut the engine down.
    juce::AudioProcessorGraph::Node::Ptr lfo, node;
    GraphEditor editor{engine};
    ModuleBase* module = nullptr;
    ModuleComponent* lfoCard = nullptr;
    ModuleComponent* card = nullptr;
    juce::String status;

    explicit KnobDrop(const juce::String& typeName) {
        engine.initialise();
        engine.getGraph().clear();
        editor.setSize(3000, 2400);
        lfo = engine.getGraph().addNode(synth::AIStateMapper::createModule("LFO"));
        node = engine.getGraph().addNode(synth::AIStateMapper::createModule(typeName));
        module = node != nullptr ? dynamic_cast<ModuleBase*>(node->getProcessor()) : nullptr;
        editor.updateComponents();
        lfoCard = findModuleComp(editor, lfo->getProcessor());
        card = module != nullptr ? findModuleComp(editor, module) : nullptr;
        if (lfoCard != nullptr && card != nullptr) {
            lfoCard->setTopLeftPosition(0, 0);
            card->setTopLeftPosition(600, 0);
        }
        editor.onStatusMessage = [this](const juce::String& m) { status = m; };
        engine.onBeforeShutdown = [this] { editor.detachAllModuleComponents(); };
    }
    ~KnobDrop() {
        engine.onBeforeShutdown = nullptr;
        engine.shutdown();
    }

    // Drops the LFO's output on the centre of `knob`; the destination channels of the cables it made.
    std::vector<int> dropOn(juce::Component& knob) {
        const auto point =
            card->getBounds().getPosition() + card->getLocalArea(&knob, knob.getLocalBounds()).getCentre();
        editor.beginConnectionDrag(lfoCard, 0, /*isInput*/ false, /*isMidi*/ false, {0, 0});
        status.clear(); // the one-time "drop it on any knob" hint is not a refusal
        editor.dragConnection(point);
        editor.endConnectionDrag(point);
        std::vector<int> landed;
        for (const auto& c : engine.getGraph().getConnections())
            if (c.destination.nodeID == node->nodeID)
                landed.push_back(c.destination.channelIndex);
        return landed;
    }
};

} // namespace

// Regression test for FRO1622: a cable dropped on a Pluck or Mallet knob landed on Level
TEST_F(GraphEditorTest, ACableDroppedOnAnyKnobOfAnyModuleLandsOnThatKnobsOwnChannel) {
    int dropped = 0;
    for (const auto& typeName : synth::AIStateMapper::moduleFactoryTypeNames()) {
        if (kNoKnobs.count(typeName) > 0)
            continue;
        std::vector<ModulationTarget> targets;
        {
            KnobDrop probe(typeName);
            if (probe.module == nullptr || probe.card == nullptr)
                continue;
            targets = probe.module->getModulationTargets();
        }
        for (const auto& target : targets) {
            SCOPED_TRACE(typeName + " / " + target.name + " (channel " + juce::String(target.channelIndex) + ")");
            KnobDrop drop(typeName);
            ASSERT_NE(drop.card, nullptr);
            auto* knob = sliderWithId(*drop.card, target.name);
            if (knob == nullptr)
                knob = sliderWithId(*drop.card, target.paramId);
            if (knob == nullptr || !shownOnCard(knob, *drop.card) || knob->getBounds().isEmpty())
                continue; // a folded row, a hidden page or a control the card does not show
            const auto landed = drop.dropOn(*knob);
            if (landed.empty() && drop.status.isNotEmpty())
                continue; // refused with a status message saying why (a whole-number control)
            ++dropped;
            ASSERT_EQ(landed.size(), 1u) << "one drop makes one cable";
            EXPECT_EQ(landed[0], target.channelIndex) << "the cable reaches this knob's own channel";
        }
    }
    EXPECT_GT(dropped, 100) << "the sweep reaches the cards' knobs";
}
