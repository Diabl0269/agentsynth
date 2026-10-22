// A click on a knob's mod dot opens the dot's own panel and nothing else. The real event path of a card control: the
// press goes to the knob first, then to the card as the knob's registered mouse listener (ModuleComponent::mouseDown /
// mouseUp), exactly the order Component::internalMouseDown/Up use. docs/modules/modulation.md#the-mod-dot-menu.

#include "../GraphEditor/GraphEditorTestHelpers.h"
#include "../ModuleComponent/ModuleComponentTestFixture.h"
#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "Modules/FX/DelayModule.h"
#include "Modules/FilterModule.h"
#include "Modules/LFOModule.h"
#include "Modules/ModuleBase.h"
#include "UI/Graph/ModDot/ModDotController.h"
#include "UI/Graph/ModDot/ModDotPopover.h"
#include "UI/Graph/PortPanel/PortConnectionsPanel.h"
#include "UI/Graph/PortPanel/PortPanelController.h"
#include <gtest/gtest.h>

namespace {

const auto kLeft = juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier);

// An LFO routed onto one modulation target of a card of type `Module`, with both panels captured by launchers (a real
// window needs a display).
template <typename Module>
struct DotCard {
    AudioEngine engine;
    AppUndoManager undo;
    std::unique_ptr<GraphEditor> editor;
    juce::AudioProcessorGraph::NodeID cardId;
    ModuleComponent* card = nullptr;
    juce::Slider* knob = nullptr;
    int channel = -1;
    std::unique_ptr<juce::Component> heldDotPanel, heldPortPanel;
    int portPanelLaunches = 0;

    DotCard(const juce::String& paramId, const juce::String& knobId) {
        engine.initialise();
        engine.getGraph().clear();
        editor = std::make_unique<GraphEditor>(engine, &undo);
        undo.setGraphEditor(editor.get());
        editor->setSize(1200, 900);
        auto& graph = engine.getGraph();
        auto lfo = graph.addNode(std::make_unique<LFOModule>());
        auto target = graph.addNode(std::make_unique<Module>());
        lfo->properties.set("x", 40);
        lfo->properties.set("y", 40);
        target->properties.set("x", 500);
        target->properties.set("y", 40);
        cardId = target->nodeID;
        for (const auto& t : dynamic_cast<ModuleBase*>(target->getProcessor())->getModulationTargets())
            if (t.paramId == paramId)
                channel = t.channelIndex;
        engine.addModRouting(lfo->nodeID, 0, cardId, channel);
        editor->getModDot().popoverLauncher = [this](std::unique_ptr<juce::Component> content, juce::Component&) {
            heldDotPanel = std::move(content);
        };
        editor->getPortPanel().panelLauncher = [this](std::unique_ptr<juce::Component> content, juce::Component&,
                                                      juce::Rectangle<int>) {
            heldPortPanel = std::move(content);
            ++portPanelLaunches;
        };
        editor->updateComponents();
        sizeModuleComponents(*editor);
        editor->timerCallback();
        card = findModuleComp(*editor, target->getProcessor());
        if (card == nullptr)
            return;
        for (auto* child : card->getChildren())
            if (auto* s = dynamic_cast<juce::Slider*>(child); s != nullptr && s->getComponentID() == knobId)
                knob = s;
    }

    ~DotCard() {
        heldDotPanel.reset();
        heldPortPanel.reset();
        editor.reset();
        engine.shutdown();
    }

    juce::Point<int> dot() const {
        return knob->getLocalPoint(card, card->getModTargetKnobAnchor(channel)->roundToInt());
    }

    // A plain click on the dot: the knob's own handler, then the card as its listener, for the press and the release.
    void clickDot() {
        const auto p = dot();
        const auto down = makeModuleClickWithMods(*knob, p, kLeft);
        knob->mouseDown(down);
        card->mouseDown(down);
        const auto up = makeModuleClickWithMods(*knob, p, kLeft);
        knob->mouseUp(up);
        card->mouseUp(up);
    }

    bool dotPanelOpen() const { return dynamic_cast<synth::ui::ModDotPopover*>(heldDotPanel.get()) != nullptr; }
};

} // namespace

// Regression test for FRO791: the card, listening to the knob, read the press position (in the knob's own space) as a
// point on the card, so a dot press that landed within reach of a jack there also opened that jack's connections.
TEST_F(ModuleComponentTest, ClickingTheFilterCutoffDotOpensOnlyItsOwnPanel) {
    DotCard<FilterModule> f("cutoff", "Cutoff");
    ASSERT_NE(f.knob, nullptr);
    f.clickDot();
    EXPECT_TRUE(f.dotPanelOpen());
    EXPECT_EQ(f.portPanelLaunches, 0);
    EXPECT_FALSE(f.editor->getPortPanel().hasPendingOpenForTest());
    EXPECT_EQ(f.editor->getPortPanel().getPanel(), nullptr);
}

TEST_F(ModuleComponentTest, ClickingTheDelayTimeDotOpensOnlyItsOwnPanel) {
    DotCard<DelayModule> f("time", "Time (ms)");
    ASSERT_NE(f.knob, nullptr);
    f.clickDot();
    EXPECT_TRUE(f.dotPanelOpen());
    EXPECT_EQ(f.portPanelLaunches, 0);
    EXPECT_FALSE(f.editor->getPortPanel().hasPendingOpenForTest());
    EXPECT_EQ(f.editor->getPortPanel().getPanel(), nullptr);
}
