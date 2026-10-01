// Concern: a card from the keyboard -- stepping in, Tab walking its controls, Escape back to the
// canvas, a focused knob turning as one undo step -- and the names a screen reader hears on it,
// including on cards built by loading a saved patch.
#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "ModuleComponentTestFixture.h"
#include "Modules/FilterModule.h"
#include "PresetManager.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/CardKnobSlider.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"

namespace {

const juce::KeyPress kTab(juce::KeyPress::tabKey);
const juce::KeyPress kShiftTab(juce::KeyPress::tabKey, juce::ModifierKeys::shiftModifier, 0);
const juce::KeyPress kEscape(juce::KeyPress::escapeKey);
const juce::KeyPress kUp(juce::KeyPress::upKey);

struct FilterCard {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor{engine, &undo};
    juce::AudioProcessorGraph::NodeID id;
    ModuleComponent* card = nullptr;

    FilterCard() {
        undo.setGraphEditor(&editor);
        editor.setSize(1200, 900);
        auto node = engine.getGraph().addNode(std::make_unique<FilterModule>());
        node->properties.set("x", 40);
        node->properties.set("y", 40);
        id = node->nodeID;
        editor.updateComponents();
        for (auto* c : editor.getModuleComponents())
            if (c != nullptr && c->getNodeId() == id)
                card = c;
    }

    synth::ui::CardKnobSlider* knob(const juce::String& title) {
        for (auto* stop : card->getKeyboardControls())
            if (stop->getTitle() == title)
                return dynamic_cast<synth::ui::CardKnobSlider*>(stop);
        return nullptr;
    }

    juce::RangedAudioParameter* cutoff() {
        for (auto* p : card->getModule()->getParameters())
            if (auto* r = dynamic_cast<juce::RangedAudioParameter*>(p); r != nullptr && r->paramID == "cutoff")
                return r;
        return nullptr;
    }
};

} // namespace

TEST(ModuleComponentKeyboard, EnterFocusesTheFirstBodyControlAndHeaderButtonsComeLast) {
    FilterCard f;
    ASSERT_NE(f.card, nullptr);
    f.card->setRecordFocusForTest(true);
    ASSERT_TRUE(f.card->enterFromKeyboard());

    const auto stops = f.card->getKeyboardControls();
    ASSERT_GE(stops.size(), 4u);
    EXPECT_EQ(f.card->getRecordedFocusForTest(), stops.front());
    EXPECT_GE(f.card->getLocalArea(stops.front()->getParentComponent(), stops.front()->getBounds()).getY(),
              ModuleComponent::kHeaderHeight)
        << "the first stop is a body control, not Bypass";
    EXPECT_NE(dynamic_cast<juce::Button*>(stops.back()), nullptr);
    EXPECT_LT(f.card->getLocalArea(stops.back()->getParentComponent(), stops.back()->getBounds()).getY(),
              ModuleComponent::kHeaderHeight);
}

TEST(ModuleComponentKeyboard, TabWalksInteriorStopsAndExitsPastTheLastOne) {
    FilterCard f;
    f.editor.selectModule(f.id, false);
    f.card->setRecordFocusForTest(true);
    ASSERT_TRUE(f.card->enterFromKeyboard());
    const auto stops = f.card->getKeyboardControls();

    for (size_t i = 1; i < stops.size(); ++i) {
        ASSERT_TRUE(f.card->keyPressed(kTab));
        EXPECT_EQ(f.card->getRecordedFocusForTest(), stops[i]);
    }
    ASSERT_TRUE(f.card->keyPressed(kTab));
    EXPECT_EQ(f.card->getRecordedFocusForTest(), &f.editor) << "Tab on the last control leaves the card";
    EXPECT_EQ(f.editor.getSelectedNodes(), std::vector<juce::AudioProcessorGraph::NodeID>{f.id});
}

TEST(ModuleComponentKeyboard, ShiftTabWalksBackAndExitsBeforeTheFirstStop) {
    FilterCard f;
    f.editor.selectModule(f.id, false);
    f.card->setRecordFocusForTest(true);
    ASSERT_TRUE(f.card->enterFromKeyboard());
    const auto stops = f.card->getKeyboardControls();

    ASSERT_TRUE(f.card->keyPressed(kTab));
    ASSERT_TRUE(f.card->keyPressed(kShiftTab));
    EXPECT_EQ(f.card->getRecordedFocusForTest(), stops.front()) << "an interior Shift+Tab steps back";
    ASSERT_TRUE(f.card->keyPressed(kShiftTab));
    EXPECT_EQ(f.card->getRecordedFocusForTest(), &f.editor) << "Shift+Tab on the first control leaves the card";
    EXPECT_EQ(f.editor.getSelectedNodes(), std::vector<juce::AudioProcessorGraph::NodeID>{f.id});
}

// A control must leave Escape, Tab and the app's Cmd chords alone for the card (and the window) to
// see them; each stop type on a Filter card (knob, combo, toggle/header buttons) is checked, then
// the card's own handler is run for the same focus.
TEST(ModuleComponentKeyboard, NoStopSwallowsEscapeTabOrACommandChord) {
    FilterCard f;
    f.editor.selectModule(f.id, false);
    f.card->setRecordFocusForTest(true);
    const juce::KeyPress focusTimeline('t', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0);
    bool sawKnob = false, sawCombo = false, sawButton = false;

    const auto stops = f.card->getKeyboardControls();
    for (size_t i = 0; i < stops.size(); ++i) {
        auto* stop = stops[i];
        sawKnob = sawKnob || dynamic_cast<juce::Slider*>(stop) != nullptr;
        sawCombo = sawCombo || dynamic_cast<juce::ComboBox*>(stop) != nullptr;
        sawButton = sawButton || dynamic_cast<juce::Button*>(stop) != nullptr;
        for (const auto& k : {kEscape, kTab, kShiftTab, focusTimeline})
            EXPECT_FALSE(stop->keyPressed(k)) << stop->getTitle() << " swallowed a key meant for the card";

        ASSERT_TRUE(f.card->enterFromKeyboard());
        for (size_t step = 0; step < i; ++step)
            ASSERT_TRUE(f.card->keyPressed(kTab));
        ASSERT_EQ(f.card->getRecordedFocusForTest(), stop);
        EXPECT_FALSE(f.card->keyPressed(focusTimeline)) << "Cmd chords bubble on to the window";
        EXPECT_TRUE(f.card->keyPressed(kEscape));
        EXPECT_EQ(f.card->getRecordedFocusForTest(), &f.editor) << "Escape exits from " << stop->getTitle();
    }
    EXPECT_TRUE(sawKnob && sawCombo && sawButton);
}

TEST(ModuleComponentKeyboard, EscapeReturnsToTheCanvasWithTheCardStillSelected) {
    FilterCard f;
    f.editor.selectModule(f.id, false);
    f.card->setRecordFocusForTest(true);
    ASSERT_TRUE(f.card->enterFromKeyboard());

    ASSERT_TRUE(f.card->keyPressed(kEscape));
    EXPECT_EQ(f.card->getRecordedFocusForTest(), &f.editor);
    EXPECT_EQ(f.editor.getSelectedNodes(), std::vector<juce::AudioProcessorGraph::NodeID>{f.id});
    EXPECT_FALSE(f.card->keyPressed(kEscape)) << "with focus outside the card, keys are not the card's";
}

TEST(ModuleComponentKeyboard, UpOnAKnobStepsItsParameterAsExactlyOneUndoStep) {
    FilterCard f;
    auto* knob = f.knob("Cutoff");
    auto* param = f.cutoff();
    ASSERT_NE(knob, nullptr);
    ASSERT_NE(param, nullptr);
    const float before = param->convertFrom0to1(param->getValue());
    const auto expected = knob->valueForKey(kUp);
    ASSERT_TRUE(expected.has_value());
    ASSERT_FALSE(f.undo.canUndo());

    ASSERT_TRUE(knob->keyPressed(kUp));
    const float after = param->convertFrom0to1(param->getValue());
    EXPECT_GT(after, before);
    EXPECT_NEAR(after, (float)*expected, 0.02f);
    ASSERT_TRUE(f.undo.canUndo());

    f.undo.undo();
    EXPECT_FALSE(f.undo.canUndo()) << "one key press is one undo step";
}

TEST(ModuleComponentKeyboard, KnobKeysCoverFineCoarseAndEnds) {
    FilterCard f;
    auto* knob = f.knob("Cutoff");
    ASSERT_NE(knob, nullptr);
    const double v = knob->getValue();
    const double up = *knob->valueForKey(kUp) - v;
    const double fine =
        *knob->valueForKey(juce::KeyPress(juce::KeyPress::upKey, juce::ModifierKeys::shiftModifier, 0)) - v;
    const double coarse = *knob->valueForKey(juce::KeyPress(juce::KeyPress::pageUpKey)) - v;
    EXPECT_GT(up, fine);
    EXPECT_GT(fine, 0.0);
    EXPECT_GT(coarse, up);
    EXPECT_EQ(*knob->valueForKey(juce::KeyPress(juce::KeyPress::homeKey)), knob->getMinimum());
    EXPECT_EQ(*knob->valueForKey(juce::KeyPress(juce::KeyPress::endKey)), knob->getMaximum());
    EXPECT_FALSE(knob->valueForKey(kTab).has_value());
    EXPECT_FALSE(knob->keyPressed(kTab)) << "Tab goes on to the card";
}

TEST(ModuleComponentKeyboard, AKnobIsNamedAfterItsParameterAndSpeaksItsValueText) {
    FilterCard f;
    auto* knob = f.knob("Cutoff");
    ASSERT_NE(knob, nullptr);
    EXPECT_EQ(knob->getTitle(), f.cutoff()->getName(100));
    EXPECT_TRUE(knob->getTooltip().contains("Cutoff"));
    EXPECT_TRUE(knob->getWantsKeyboardFocus());
    // JUCE's slider accessibility value is getTextFromValue, which the attachment routes to the
    // parameter's own text.
    EXPECT_EQ(knob->getTextFromValue(1200.0), "1.2 kHz");
    EXPECT_TRUE(knob->getTextFromValue(knob->getValue()).contains("Hz"));
}

TEST(ModuleComponentKeyboard, JacksHaveAccessibleNames) {
    FilterCard f;
    juce::StringArray names;
    for (auto* port : f.card->getPortAccessiblesForTest()) {
        names.add(port->getTitle());
        EXPECT_FALSE(port->getWantsKeyboardFocus());
        bool clicks = true, childClicks = true;
        port->getInterceptsMouseClicks(clicks, childClicks);
        EXPECT_FALSE(clicks) << "a jack stand-in must never take a click";
    }
    ASSERT_FALSE(names.isEmpty());
    for (const auto& n : names)
        EXPECT_TRUE(n.endsWith(" input") || n.endsWith(" output")) << n;
    EXPECT_TRUE(names.joinIntoString("|").contains("output"));
}

// A patch loaded from disk builds its cards through the same constructor; every knob, combo and
// toggle on them must come out named and with a tooltip.
TEST(ModuleComponentKeyboard, CardsBuiltByLoadingASavedPatchAreNamed) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(3200, 2400);
    ASSERT_TRUE(synth::PresetManager::loadPreset(0, engine.getGraph()));
    editor.updateComponents();
    ASSERT_GT(editor.getModuleComponents().size(), 1);

    int knobs = 0;
    for (auto* card : editor.getModuleComponents()) {
        for (auto* stop : card->getKeyboardControls()) {
            EXPECT_TRUE(stop->getTitle().isNotEmpty() ||
                        (dynamic_cast<juce::Button*>(stop) != nullptr &&
                         dynamic_cast<juce::Button*>(stop)->getButtonText().isNotEmpty()))
                << card->cardTitle() << " has an unnamed control";
            if (auto* tip = dynamic_cast<juce::SettableTooltipClient*>(stop))
                EXPECT_TRUE(tip->getTooltip().isNotEmpty()) << card->cardTitle() << "/" << stop->getTitle();
            knobs += dynamic_cast<juce::Slider*>(stop) != nullptr ? 1 : 0;
        }
    }
    EXPECT_GT(knobs, 0);
}
