// Right-click MIDI Learn on a module-card control (FRO130,
// docs/control/midi-remote-ui.md#right-click-midi-learn--coverage /
// docs/control/midi-remote-ui.md#the-learn-interaction). Every test drives a REAL right-click
// through the control's own mouseDown()/mouseUp() (never a direct menu-builder call) and captures
// whatever PopupMenu ModuleComponent builds via setShowContextMenuHookForTest() -- the same
// repo-wide idiom TimelineTrackHeaderContextMenuTests.cpp documents (this file keeps its own copy,
// per that convention).

#include "ModuleComponentTestFixture.h"

#include "Modules/FX/ParametricEQModule.h"
#include "Modules/FilterModule.h"
#include "Modules/LFOModule.h"
#include "Modules/OscillatorModule.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"

#include <algorithm>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <map>
#include <vector>

namespace {

juce::ModifierKeys rightClickMods() { return juce::ModifierKeys(juce::ModifierKeys::rightButtonModifier); }

juce::MouseEvent realChildMouseEvent(juce::Component& child, juce::ModifierKeys mods) {
    const auto pos = child.getLocalBounds().getCentre().toFloat();
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), pos, mods, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                            &child, &child, juce::Time::getCurrentTime(), pos, juce::Time::getCurrentTime(), 1, false);
}

// Right-clicks `child` and captures whatever PopupMenu the module card builds via its
// setShowContextMenuHookForTest() seam. Calling child.mouseDown()/mouseUp() directly invokes only
// the CHILD's own override (proving e.g. RightClickSafeButton's popup guard works) -- it does NOT
// run registered MouseListeners the way a real OS click does (that only happens inside JUCE's
// internal dispatch, Component::internalMouseDown, which a direct virtual call bypasses). Since
// juce::Component IS its own MouseListener implementation, ModuleComponent::mouseDown() is exactly
// what a real listener notification would invoke, with e.eventComponent already pointing at
// `child` (realChildMouseEvent sets it) -- so calling it directly on `card` reproduces the
// listener half of dispatch that createControls()'s addMouseListener(this) wires up.
juce::PopupMenu rightClickChild(ModuleComponent& card, juce::Component& child) {
    juce::PopupMenu captured;
    card.setShowContextMenuHookForTest([&](juce::PopupMenu& menu) { captured = menu; });
    const auto downEvent = realChildMouseEvent(child, rightClickMods());
    child.mouseDown(downEvent); // the control's own handler
    card.mouseDown(downEvent);  // the registered MouseListener path -- builds the menu
    child.mouseUp(realChildMouseEvent(child, rightClickMods()));
    card.setShowContextMenuHookForTest(nullptr);
    return captured;
}

juce::Component* findChildByComponentID(ModuleComponent& card, const juce::String& id) {
    for (auto* child : card.getChildren())
        if (child->getComponentID() == id)
            return child;
    return nullptr;
}

juce::Slider* findSlider(ModuleComponent& card, const juce::String& id) {
    return dynamic_cast<juce::Slider*>(findChildByComponentID(card, id));
}

juce::ToggleButton* findToggle(ModuleComponent& card, const juce::String& id) {
    return dynamic_cast<juce::ToggleButton*>(findChildByComponentID(card, id));
}

// ComboBoxes never get a componentID (only sliders/toggles do -- createControls() has no
// combo->setComponentID call), so this finds the first one, which is fine for a module with
// exactly one choice parameter like OscillatorModule's Waveform.
juce::ComboBox* findFirstCombo(ModuleComponent& card) {
    for (auto* child : card.getChildren())
        if (auto* combo = dynamic_cast<juce::ComboBox*>(child))
            return combo;
    return nullptr;
}

juce::DrawableButton* findDrawableButtonByName(ModuleComponent& card, const juce::String& name) {
    for (auto* child : card.getChildren())
        if (auto* db = dynamic_cast<juce::DrawableButton*>(child); db != nullptr && db->getName() == name)
            return db;
    return nullptr;
}

// Every menu item's text, in order -- including disabled rows (isTicked/isEnabled don't affect
// getItemText). Separators have empty text and are skipped, matching how a human reads the menu.
std::vector<juce::String> menuItemTexts(const juce::PopupMenu& menu) {
    std::vector<juce::String> texts;
    juce::PopupMenu::MenuItemIterator it(menu);
    while (it.next()) {
        const auto& item = it.getItem();
        if (item.text.isNotEmpty())
            texts.push_back(item.text);
    }
    return texts;
}

bool menuContains(const juce::PopupMenu& menu, const juce::String& text) {
    const auto texts = menuItemTexts(menu);
    return std::find(texts.begin(), texts.end(), text) != texts.end();
}

} // namespace

// ============================================================================
// Coverage: right-click shows the MIDI block; a mapped assignment shows the mapped variant.
// ============================================================================

TEST_F(ModuleComponentTest, RightClickFloatSliderShowsAutomateThenUnmappedMidiLearnItem) {
    AudioEngine engine;
    GraphEditor editor(engine);
    FilterModule filter;
    ModuleComponent card(&filter, juce::AudioProcessorGraph::NodeID(1), editor);
    card.setSize(280, 500);

    editor.onMidiLearnRequested = [](juce::AudioProcessorGraph::NodeID, const juce::String&) {};

    auto* slider = findSlider(card, "Cutoff");
    ASSERT_NE(slider, nullptr);

    const auto menu = rightClickChild(card, *slider);
    const auto texts = menuItemTexts(menu);
    ASSERT_GE(texts.size(), 2u);
    EXPECT_EQ(texts[0], "Automate 'Cutoff'");
    EXPECT_EQ(texts[1], "MIDI Learn 'Cutoff'...");
}

TEST_F(ModuleComponentTest, NoMidiLearnItemsWhenTheHostNeverWiredTheCallback) {
    AudioEngine engine;
    GraphEditor editor(engine); // onMidiLearnRequested left unset -- e.g. headless/plugin build
    FilterModule filter;
    ModuleComponent card(&filter, juce::AudioProcessorGraph::NodeID(1), editor);
    card.setSize(280, 500);

    auto* slider = findSlider(card, "Cutoff");
    ASSERT_NE(slider, nullptr);

    const auto menu = rightClickChild(card, *slider);
    EXPECT_EQ(menuItemTexts(menu).size(), 1u) << "just the pre-existing Automate item";
}

TEST_F(ModuleComponentTest, RightClickBoolToggleShowsMidiLearnAndDoesNotToggleTheParameter) {
    AudioEngine engine;
    GraphEditor editor(engine);
    LFOModule lfo;
    ModuleComponent card(&lfo, juce::AudioProcessorGraph::NodeID(1), editor);
    card.setSize(280, 500);
    editor.onMidiLearnRequested = [](juce::AudioProcessorGraph::NodeID, const juce::String&) {};

    auto* bipolarParam = dynamic_cast<juce::AudioParameterBool*>(findParameterByID(&lfo, "bipolar"));
    ASSERT_NE(bipolarParam, nullptr);
    const bool before = bipolarParam->get();

    auto* toggle = findToggle(card, "Bipolar");
    ASSERT_NE(toggle, nullptr);

    const auto menu = rightClickChild(card, *toggle);
    EXPECT_TRUE(menuContains(menu, "MIDI Learn 'Bipolar'..."));
    EXPECT_EQ(bipolarParam->get(), before) << "a right click must never fire the button's own click";
}

TEST_F(ModuleComponentTest, RightClickComboDoesNotOpenItsPopupAndShowsMidiLearn) {
    AudioEngine engine;
    GraphEditor editor(engine);
    OscillatorModule osc;
    ModuleComponent card(&osc, juce::AudioProcessorGraph::NodeID(1), editor);
    card.setSize(280, 500);
    editor.onMidiLearnRequested = [](juce::AudioProcessorGraph::NodeID, const juce::String&) {};

    auto* combo = findFirstCombo(card);
    ASSERT_NE(combo, nullptr);

    const auto menu = rightClickChild(card, *combo);
    EXPECT_FALSE(combo->isPopupActive()) << "right-click must never open the choice popup";
    EXPECT_TRUE(menuContains(menu, "MIDI Learn 'Waveform'..."));
}

TEST_F(ModuleComponentTest, RightClickBypassShowsMidiLearnAndDoesNotToggleBypass) {
    AudioEngine engine;
    GraphEditor editor(engine);
    FilterModule filter;
    ModuleComponent card(&filter, juce::AudioProcessorGraph::NodeID(1), editor);
    card.setSize(280, 500);
    editor.onMidiLearnRequested = [](juce::AudioProcessorGraph::NodeID, const juce::String&) {};

    auto* bypassedParam = dynamic_cast<juce::AudioParameterBool*>(findParameterByID(&filter, "bypassed"));
    ASSERT_NE(bypassedParam, nullptr);
    const bool before = bypassedParam->get();

    auto* bypass = findDrawableButtonByName(card, "Bypass");
    ASSERT_NE(bypass, nullptr);

    const auto menu = rightClickChild(card, *bypass);
    EXPECT_TRUE(menuContains(menu, "MIDI Learn 'Bypassed'..."));
    EXPECT_EQ(bypassedParam->get(), before) << "a right click must never toggle Bypass";
}

TEST_F(ModuleComponentTest, EqCardBandToggleIsLearnableThroughTheGenericRegistryNoCardSpecificWiring) {
    AudioEngine engine;
    GraphEditor editor(engine);
    ParametricEQModule eq;
    ModuleComponent card(&eq, juce::AudioProcessorGraph::NodeID(1), editor);
    card.setSize(340, 600);
    editor.onMidiLearnRequested = [](juce::AudioProcessorGraph::NodeID, const juce::String&) {};

    // band 0's "On" toggle -- ModuleComponentEQCard.cpp positions it, but createControls() built it
    // as an ordinary generic toggle, so it must already be registered (docs/control/midi-remote-ui.md's
    // "each card's own control creation registers its controls with the same registry the generic
    // path uses ... mouseDown needs no card-specific branches").
    auto* onParam = dynamic_cast<juce::AudioParameterBool*>(findParameterByID(&eq, "band1On"));
    ASSERT_NE(onParam, nullptr);
    EXPECT_EQ(card.findMidiLearnableParamForTest(findToggle(card, onParam->getName(100))), onParam);
}

// ============================================================================
// The learn/forget actions themselves, and the mapped-vs-unmapped menu shape.
// ============================================================================

TEST_F(ModuleComponentTest, LearnMenuItemFiresOnMidiLearnRequestedWithTheNodeAndParamId) {
    AudioEngine engine;
    GraphEditor editor(engine);
    FilterModule filter;
    const juce::AudioProcessorGraph::NodeID kNodeId(7);
    ModuleComponent card(&filter, kNodeId, editor);
    card.setSize(280, 500);

    juce::AudioProcessorGraph::NodeID requestedNode;
    juce::String requestedParamId;
    int callCount = 0;
    editor.onMidiLearnRequested = [&](juce::AudioProcessorGraph::NodeID n, const juce::String& p) {
        requestedNode = n;
        requestedParamId = p;
        ++callCount;
    };

    auto* slider = findSlider(card, "Cutoff");
    ASSERT_NE(slider, nullptr);
    auto menu = rightClickChild(card, *slider);

    juce::PopupMenu::MenuItemIterator it(menu);
    bool invoked = false;
    while (it.next()) {
        if (it.getItem().text == "MIDI Learn 'Cutoff'...") {
            it.getItem().action();
            invoked = true;
        }
    }
    ASSERT_TRUE(invoked);
    EXPECT_EQ(callCount, 1);
    EXPECT_EQ(requestedNode, kNodeId);
    EXPECT_EQ(requestedParamId, "cutoff");
}

TEST_F(ModuleComponentTest, MappedControlShowsDisabledTitleLearnAgainAndForgetButNoEditItemByDefault) {
    AudioEngine engine;
    GraphEditor editor(engine);
    FilterModule filter;
    ModuleComponent card(&filter, juce::AudioProcessorGraph::NodeID(1), editor);
    card.setSize(280, 500);

    editor.onMidiLearnRequested = [](juce::AudioProcessorGraph::NodeID, const juce::String&) {};
    editor.onQueryMidiMappingsForNode = [](juce::AudioProcessorGraph::NodeID) -> std::map<juce::String, juce::String> {
        return {{"cutoff", "Knob 1 on Launchkey Mini"}};
    };
    // onEditMidiAssignmentRequested deliberately left unset -- FRO131 (the panel) wires it.

    auto* slider = findSlider(card, "Cutoff");
    ASSERT_NE(slider, nullptr);
    const auto menu = rightClickChild(card, *slider);
    const auto texts = menuItemTexts(menu);

    EXPECT_TRUE(menuContains(menu, "MIDI: Knob 1 on Launchkey Mini"));
    EXPECT_TRUE(menuContains(menu, "MIDI Learn again..."));
    EXPECT_TRUE(menuContains(menu, "Forget MIDI"));
    EXPECT_FALSE(menuContains(menu, "Edit MIDI assignment...")) << "no dead menu item before FRO131 wires it";
    juce::ignoreUnused(texts);
}

TEST_F(ModuleComponentTest, MappedControlShowsEditItemWhenTheHostWiresIt) {
    AudioEngine engine;
    GraphEditor editor(engine);
    FilterModule filter;
    ModuleComponent card(&filter, juce::AudioProcessorGraph::NodeID(1), editor);
    card.setSize(280, 500);

    editor.onMidiLearnRequested = [](juce::AudioProcessorGraph::NodeID, const juce::String&) {};
    editor.onQueryMidiMappingsForNode = [](juce::AudioProcessorGraph::NodeID) -> std::map<juce::String, juce::String> {
        return {{"cutoff", "Knob 1 on Launchkey Mini"}};
    };
    editor.onEditMidiAssignmentRequested = [](juce::AudioProcessorGraph::NodeID, const juce::String&) {};

    auto* slider = findSlider(card, "Cutoff");
    ASSERT_NE(slider, nullptr);
    const auto menu = rightClickChild(card, *slider);
    EXPECT_TRUE(menuContains(menu, "Edit MIDI assignment..."));
}

TEST_F(ModuleComponentTest, ForgetMenuItemFiresOnMidiForgetRequested) {
    AudioEngine engine;
    GraphEditor editor(engine);
    FilterModule filter;
    ModuleComponent card(&filter, juce::AudioProcessorGraph::NodeID(3), editor);
    card.setSize(280, 500);

    editor.onMidiLearnRequested = [](juce::AudioProcessorGraph::NodeID, const juce::String&) {};
    editor.onQueryMidiMappingsForNode = [](juce::AudioProcessorGraph::NodeID) -> std::map<juce::String, juce::String> {
        return {{"cutoff", "Knob 1 on Launchkey Mini"}};
    };
    int forgetCount = 0;
    editor.onMidiForgetRequested = [&](juce::AudioProcessorGraph::NodeID, const juce::String& p) {
        ++forgetCount;
        EXPECT_EQ(p, "cutoff");
    };

    auto* slider = findSlider(card, "Cutoff");
    ASSERT_NE(slider, nullptr);
    auto menu = rightClickChild(card, *slider);

    juce::PopupMenu::MenuItemIterator it(menu);
    while (it.next())
        if (it.getItem().text == "Forget MIDI")
            it.getItem().action();
    EXPECT_EQ(forgetCount, 1);
}

// ============================================================================
// Badge: painted only when mapped, refreshed from the existing gated timerCallback.
// ============================================================================

TEST_F(ModuleComponentTest, BadgePaintsOnlyAfterATimerTickObservesAMapping) {
    AudioEngine engine;
    GraphEditor editor(engine);
    FilterModule filter;
    ModuleComponent card(&filter, juce::AudioProcessorGraph::NodeID(1), editor);
    card.setSize(280, 500);

    auto* slider = findSlider(card, "Cutoff");
    ASSERT_NE(slider, nullptr);

    EXPECT_FALSE(card.isMidiLearnBadgeMappedForTest(slider)) << "unmapped and no query wired yet";

    editor.onQueryMidiMappingsForNode = [](juce::AudioProcessorGraph::NodeID) -> std::map<juce::String, juce::String> {
        return {{"cutoff", "Knob 1 on Launchkey Mini"}};
    };
    EXPECT_FALSE(card.isMidiLearnBadgeMappedForTest(slider)) << "wiring the callback alone must not paint anything";

    card.timerCallback();
    EXPECT_TRUE(card.isMidiLearnBadgeMappedForTest(slider));
}

TEST_F(ModuleComponentTest, TimerCallbackNeverCrashesWithNoMidiLearnHostWired) {
    AudioEngine engine;
    GraphEditor editor(engine);
    FilterModule filter;
    ModuleComponent card(&filter, juce::AudioProcessorGraph::NodeID(1), editor);
    EXPECT_NO_THROW(card.timerCallback());
}
