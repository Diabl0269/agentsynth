// Concern: the optional card panels (Show Scope, Show Response, Show Spectrum) open the way the module
// remembers them when a card is built, exactly as a click opens them, and a click is remembered on the module.
#include "AudioEngine/AudioEngine.h"
#include "MacroSet.h"
#include "ModuleComponentTestFixture.h"
#include "Modules/FX/ParametricEQModule.h"
#include "Modules/FilterModule.h"
#include "Modules/OscillatorModule.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/ModuleViews/EQCurveComponent.h"
#include "UI/ModuleViews/FrequencyResponseComponent.h"
#include "UI/ModuleViews/ScopeComponent.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace {

juce::ToggleButton* findToggle(juce::Component& card, const juce::String& text) {
    for (auto* child : card.getChildren())
        if (auto* toggle = dynamic_cast<juce::ToggleButton*>(child))
            if (toggle->getButtonText() == text)
                return toggle;
    return nullptr;
}

// Button::triggerClick() posts the click as a command message, so a click takes effect after one dispatch pass.
void click(juce::Button* button) {
    ASSERT_NE(button, nullptr);
    button->triggerClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
}

template <typename T>
T* findChildOfType(juce::Component& card) {
    for (auto* child : card.getChildren())
        if (auto* typed = dynamic_cast<T*>(child))
            return typed;
    return nullptr;
}

synth::CardViewState scopeOpen() {
    synth::CardViewState state;
    state.showScope = true;
    return state;
}

} // namespace

TEST_F(ModuleComponentTest, CardWithNoRememberedViewStartsWithEveryPanelClosed) {
    AudioEngine engine;
    GraphEditor editor(engine);
    OscillatorModule osc;
    ModuleComponent card(&osc, juce::AudioProcessorGraph::NodeID(1), editor);

    auto* toggle = findToggle(card, "Show Scope");
    auto* scope = findChildOfType<ScopeComponent>(card);
    ASSERT_NE(toggle, nullptr);
    ASSERT_NE(scope, nullptr);
    EXPECT_FALSE(toggle->getToggleState());
    EXPECT_FALSE(scope->isVisible());
    EXPECT_TRUE(osc.getCardViewState().isDefault()) << "building a card must not invent view state";
}

TEST_F(ModuleComponentTest, RememberedScopeOpensWhenTheCardIsBuiltJustAsAClickDoes) {
    AudioEngine engine;
    GraphEditor editor(engine);

    OscillatorModule clicked;
    ModuleComponent clickedCard(&clicked, juce::AudioProcessorGraph::NodeID(1), editor);
    const int closedHeight = clickedCard.getHeight();
    click(findToggle(clickedCard, "Show Scope")); // a real click path, flipping the toggle itself

    OscillatorModule restored;
    restored.setCardViewState(scopeOpen());
    ModuleComponent restoredCard(&restored, juce::AudioProcessorGraph::NodeID(2), editor);

    auto* toggle = findToggle(restoredCard, "Show Scope");
    auto* scope = findChildOfType<ScopeComponent>(restoredCard);
    ASSERT_NE(toggle, nullptr);
    ASSERT_NE(scope, nullptr);
    EXPECT_TRUE(toggle->getToggleState());
    EXPECT_TRUE(scope->isVisible());
    EXPECT_GT(restoredCard.getHeight(), closedHeight) << "the scope must make room, as it does on a click";
    EXPECT_EQ(restoredCard.getHeight(), clickedCard.getHeight());
    EXPECT_EQ(restoredCard.getBounds(), clickedCard.getBounds());
    EXPECT_EQ(scope->getBounds(), findChildOfType<ScopeComponent>(clickedCard)->getBounds());
}

TEST_F(ModuleComponentTest, ClickingTheScopeToggleIsRememberedOnTheModule) {
    AudioEngine engine;
    GraphEditor editor(engine);
    OscillatorModule osc;
    ModuleComponent card(&osc, juce::AudioProcessorGraph::NodeID(1), editor);

    auto* toggle = findToggle(card, "Show Scope");
    click(toggle);
    EXPECT_TRUE(osc.getCardViewState().showScope);
    click(toggle);
    EXPECT_FALSE(osc.getCardViewState().showScope);
    EXPECT_TRUE(osc.getCardViewState().isDefault()) << "closing it again returns to the default, which saves nothing";
}

TEST_F(ModuleComponentTest, RebuildingACardKeepsItsOpenScope) {
    AudioEngine engine;
    GraphEditor editor(engine);
    OscillatorModule osc;
    {
        ModuleComponent card(&osc, juce::AudioProcessorGraph::NodeID(1), editor);
        click(findToggle(card, "Show Scope"));
    }
    ModuleComponent rebuilt(&osc, juce::AudioProcessorGraph::NodeID(1), editor);
    EXPECT_TRUE(findChildOfType<ScopeComponent>(rebuilt)->isVisible());
}

TEST_F(ModuleComponentTest, FilterCardRestoresItsResponseAndSpectrum) {
    AudioEngine engine;
    GraphEditor editor(engine);
    FilterModule filter;
    synth::CardViewState state;
    state.showResponse = true;
    state.showSpectrum = true;
    filter.setCardViewState(state);
    ModuleComponent card(&filter, juce::AudioProcessorGraph::NodeID(1), editor);

    auto* response = findChildOfType<FrequencyResponseComponent>(card);
    auto* responseToggle = findToggle(card, "Show Response");
    auto* spectrumToggle = findToggle(card, "Show Spectrum");
    ASSERT_NE(response, nullptr);
    ASSERT_NE(responseToggle, nullptr);
    ASSERT_NE(spectrumToggle, nullptr);
    EXPECT_TRUE(responseToggle->getToggleState());
    EXPECT_TRUE(response->isVisible());
    EXPECT_TRUE(spectrumToggle->isVisible());
    EXPECT_TRUE(spectrumToggle->getToggleState());
    EXPECT_TRUE(response->getShowSpectrum());
}

TEST_F(ModuleComponentTest, FilterSpectrumIsIgnoredWhileTheResponseIsClosed) {
    AudioEngine engine;
    GraphEditor editor(engine);
    FilterModule filter;
    synth::CardViewState state;
    state.showSpectrum = true; // no response view to put it on
    filter.setCardViewState(state);
    ModuleComponent card(&filter, juce::AudioProcessorGraph::NodeID(1), editor);

    EXPECT_FALSE(findChildOfType<FrequencyResponseComponent>(card)->isVisible());
    EXPECT_FALSE(findToggle(card, "Show Spectrum")->isVisible());
    EXPECT_FALSE(findChildOfType<FrequencyResponseComponent>(card)->getShowSpectrum());
}

TEST_F(ModuleComponentTest, FilterClicksAreRememberedAndHidingTheResponseClearsTheSpectrum) {
    AudioEngine engine;
    GraphEditor editor(engine);
    FilterModule filter;
    ModuleComponent card(&filter, juce::AudioProcessorGraph::NodeID(1), editor);

    click(findToggle(card, "Show Response"));
    click(findToggle(card, "Show Spectrum"));
    EXPECT_TRUE(filter.getCardViewState().showResponse);
    EXPECT_EQ(filter.getCardViewState().showSpectrum, std::optional<bool>(true));

    click(findToggle(card, "Show Response")); // closes the response and its spectrum
    EXPECT_FALSE(filter.getCardViewState().showResponse);
    EXPECT_FALSE(filter.getCardViewState().showSpectrum.has_value());
    EXPECT_TRUE(filter.getCardViewState().isDefault());
}

TEST_F(ModuleComponentTest, ParametricEqKeepsItsSpectrumOnByDefaultAndRestoresAnOffChoice) {
    AudioEngine engine;
    GraphEditor editor(engine);

    ParametricEQModule untouched;
    ModuleComponent defaultCard(&untouched, juce::AudioProcessorGraph::NodeID(1), editor);
    EXPECT_TRUE(findChildOfType<EQCurveComponent>(defaultCard)->getShowSpectrum()) << "the card default is on";

    ParametricEQModule off;
    synth::CardViewState state;
    state.showSpectrum = false;
    off.setCardViewState(state);
    ModuleComponent offCard(&off, juce::AudioProcessorGraph::NodeID(2), editor);
    EXPECT_FALSE(findChildOfType<EQCurveComponent>(offCard)->getShowSpectrum());
    EXPECT_FALSE(findToggle(offCard, "Show Spectrum")->getToggleState());

    click(findToggle(defaultCard, "Show Spectrum"));
    EXPECT_EQ(untouched.getCardViewState().showSpectrum, std::optional<bool>(false));
}

// The load path: cards are built by GraphEditor::updateComponents() after the graph is populated. A restored
// panel changes the card's size inside its constructor, which for a macro member must not run the make-room
// pass (see MacroProjectLoadTests.cpp) or move the card off its saved position.
TEST_F(ModuleComponentTest, ProjectLoadOpensRememberedScopesWithoutMovingAnyCardInsideAMacro) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(3200, 2400);

    struct Saved {
        juce::AudioProcessorGraph::NodeID id;
        juce::String uuid;
        juce::Point<int> position;
    };
    auto addSaved = [&engine](std::unique_ptr<juce::AudioProcessor> processor, int x, int y) {
        auto node = engine.getGraph().addNode(std::move(processor));
        const auto uuid = juce::Uuid().toDashedString();
        node->properties.set("x", x);
        node->properties.set("y", y);
        node->properties.set("uuid", uuid);
        return Saved{node->nodeID, uuid, {x, y}};
    };

    auto sibling = addSaved(std::make_unique<OscillatorModule>(), 420, 340);
    auto loose = addSaved(std::make_unique<OscillatorModule>(), 700, 300);
    auto subjectProcessor = std::make_unique<OscillatorModule>();
    subjectProcessor->setCardViewState(scopeOpen());
    auto subject = addSaved(std::move(subjectProcessor), 400, 300);

    synth::Macro macro;
    macro.name = "Macro";
    macro.bounds = {subject.position.x, subject.position.y, 160, 60};
    macro.members = {subject.uuid, sibling.uuid};
    editor.getMacros().add(macro);

    editor.updateComponents();

    for (const auto& saved : {subject, sibling, loose}) {
        ModuleComponent* found = nullptr;
        for (auto* comp : editor.getModuleComponents())
            if (comp != nullptr && comp->getNodeId() == saved.id)
                found = comp;
        ASSERT_NE(found, nullptr);
        EXPECT_EQ(found->getPosition(), saved.position) << "node " << static_cast<int>(saved.id.uid) << " moved";
        if (saved.id == subject.id) {
            EXPECT_TRUE(findChildOfType<ScopeComponent>(*found)->isVisible());
            EXPECT_TRUE(findToggle(*found, "Show Scope")->getToggleState());
        }
    }
}
