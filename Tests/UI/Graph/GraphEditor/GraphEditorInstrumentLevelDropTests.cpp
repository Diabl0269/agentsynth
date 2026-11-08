// A cable dropped on a knob, through the real drop path: the Level of Drum Kit, Pluck and Mallet takes it (a
// modulation into the module's Level channel), and the Wavetable's Unison refuses it with a status message saying why
// and connects nothing.

#include "AudioEngine/AudioEngine.h"
#include "GraphEditorTestHelpers.h"
#include "Modules/DrumKit/DrumKitModule.h"
#include "Modules/LFOModule.h"
#include "Modules/Mallet/MalletModule.h"
#include "Modules/Pluck/PluckModule.h"
#include "Modules/WavetableOscillatorModule/WavetableOscillatorModule.h"
#include "UI/Graph/CardBody/CardBody.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include <functional>
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace {

ModuleComponent* cardOf(GraphEditor& editor, juce::AudioProcessor* processor) {
    for (auto* card : editor.getModuleComponents())
        if (card->getModule() == processor)
            return card;
    return nullptr;
}

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

struct Drop {
    AudioEngine engine;
    // Declared before the editor so the nodes outlive its cards.
    juce::AudioProcessorGraph::Node::Ptr lfo, target;
    GraphEditor editor{engine};
    ModuleComponent* lfoCard = nullptr;
    ModuleComponent* targetCard = nullptr;
    juce::String status;

    explicit Drop(std::unique_ptr<juce::AudioProcessor> module) {
        editor.setSize(1600, 1200);
        lfo = engine.getGraph().addNode(std::make_unique<LFOModule>());
        target = engine.getGraph().addNode(std::move(module));
        editor.updateComponents();
        lfoCard = cardOf(editor, lfo->getProcessor());
        targetCard = cardOf(editor, target->getProcessor());
        if (lfoCard != nullptr)
            lfoCard->setTopLeftPosition(0, 0);
        if (targetCard != nullptr)
            targetCard->setTopLeftPosition(400, 0);
        editor.onStatusMessage = [this](const juce::String& m) { status = m; };
        // The contract of AudioEngine::shutdown(): the owner of the cards lets go of their parameters first.
        engine.onBeforeShutdown = [this] { editor.detachAllModuleComponents(); };
    }

    // The hook points at the editor, which dies before the engine's own shutdown runs.
    ~Drop() { engine.onBeforeShutdown = nullptr; }

    // Drags the LFO's output and releases it on the centre of `knob`.
    void onto(juce::Component& knob) {
        const auto point =
            targetCard->getBounds().getPosition() + targetCard->getLocalArea(&knob, knob.getLocalBounds()).getCentre();
        editor.beginConnectionDrag(lfoCard, 0, /*isInput*/ false, /*isMidi*/ false, {0, 0});
        status.clear(); // the one-time "drop it on any knob" hint is not what is checked
        editor.dragConnection(point);
        editor.endConnectionDrag(point);
    }

    int connectionsInto(int channel) {
        int count = 0;
        for (const auto& c : engine.getGraph().getConnections())
            if (c.destination.nodeID == target->nodeID && c.destination.channelIndex == channel)
                ++count;
        return count;
    }
    int connectionsIntoTarget() {
        int count = 0;
        for (const auto& c : engine.getGraph().getConnections())
            if (c.destination.nodeID == target->nodeID)
                ++count;
        return count;
    }
};

} // namespace

// Regression test for FRO1359: a cable dropped on an instrument's Level knob connected nothing.
TEST_F(GraphEditorTest, ACableDroppedOnAnInstrumentsLevelModulatesIt) {
    const std::vector<std::pair<const char*, std::function<std::unique_ptr<juce::AudioProcessor>()>>> instruments = {
        {"Drum Kit", [] { return std::make_unique<DrumKitModule>(); }},
        {"Pluck", [] { return std::make_unique<PluckModule>(); }},
        {"Mallet", [] { return std::make_unique<MalletModule>(); }},
    };
    for (const auto& [name, make] : instruments) {
        SCOPED_TRACE(name);
        Drop drop(make());
        ASSERT_NE(drop.lfoCard, nullptr);
        ASSERT_NE(drop.targetCard, nullptr);
        auto* level = sliderWithId(*drop.targetCard, "Level");
        ASSERT_NE(level, nullptr);
        ASSERT_TRUE(shownOnCard(level, *drop.targetCard));
        drop.onto(*level);
        EXPECT_EQ(drop.connectionsInto(0), 1) << "the cable reaches the Level channel";
        EXPECT_TRUE(drop.status.isEmpty()) << drop.status;
        drop.engine.shutdown();
    }
}

TEST_F(GraphEditorTest, ACableDroppedOnTheWavetablesUnisonIsRefusedOutLoud) {
    Drop drop(std::make_unique<WavetableOscillatorModule>());
    ASSERT_NE(drop.targetCard, nullptr);
    auto* unison = sliderWithId(*drop.targetCard, "Unison");
    ASSERT_NE(unison, nullptr);
    if (auto* body = drop.targetCard->getCardBody(); body != nullptr && !shownOnCard(unison, *drop.targetCard)) {
        const auto& plan = body->getPlan();
        for (int g = 0; g < (int)plan.tabGroups.size(); ++g)
            for (int t = 0; t < (int)plan.tabGroups[(size_t)g].sections.size(); ++t)
                if (plan.tabTitle(plan.tabGroups[(size_t)g].sections[(size_t)t]) == "Unison")
                    body->selectTab(g, t);
    }
    ASSERT_TRUE(shownOnCard(unison, *drop.targetCard)) << "the Unison knob must be on the card to be dropped on";
    const int before = drop.connectionsIntoTarget();
    drop.onto(*unison);
    EXPECT_EQ(drop.connectionsIntoTarget(), before) << "nothing connects";
    EXPECT_EQ(drop.status, "Unison takes no mod cable: it sets a whole number of voices");
    drop.engine.shutdown();
}
