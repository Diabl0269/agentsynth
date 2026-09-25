// HostedPluginCardMidiLearnTests.cpp -- FRO137
// (docs/control/plugin-card-layout.md#interaction-with-midi-remote-and-automation): a plugin-card knob/toggle/choice
// behaves like every other module-card control -- registered in ModuleComponent::MidiLearnableRegistry, surviving a
// layout-triggered rebuild without leaving a stale entry, and right-clickable through the exact same real mouseDown()
// path ModuleComponentMidiLearnTests.cpp already proves for built-in controls (never a direct call to the menu builder
// -- see that file's rightClickChild() comment for why).
//
// Groups:
//   1. Registry: hosted entries appear, carry hosted==true + the slot's paramId, and a layout-
//      triggered rebuild leaves the registry describing only the CURRENT widgets.
//   2. Menu: right-click on a hosted knob shows "Automate '<Param>'" + MIDI Learn; a hosted
//      toggle/choice shows MIDI Learn only (no Automate item -- that has only ever existed for a
//      slider, built-in or hosted).

#include "../../../StubPluginInstance.h"
#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "Modules/CardLayout.h"
#include "Plugin/Hosting/HostedPluginModule.h"
#include "Plugin/Hosting/PluginCardLayoutStore.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"

#include <algorithm>
#include <chrono>
#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

using synth::HostedPluginModule;
using synth::test::StubBackend;
using synth::test::StubParamSpec;
using synth::test::StubParamTraits;
using synth::test::StubPluginInstance;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 64;

template <typename Predicate>
bool pumpUntil(Predicate predicate, int timeoutMs = 2000) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    do {
        if (predicate())
            return true;
        juce::MessageManager::getInstance()->runDispatchLoopUntil(5);
    } while (std::chrono::steady_clock::now() < deadline);
    return predicate();
}

juce::PluginDescription stubDescription() {
    juce::PluginDescription description;
    description.name = "Learnable Plugin";
    description.pluginFormatName = "VST3";
    description.uniqueId = 0xC0DE02;
    description.deprecatedUid = 0xC0DE02;
    description.fileOrIdentifier = "/nonexistent/test/path/LearnablePlugin.vst3";
    return description;
}

StubParamSpec knobSpec(const juce::String& id, const juce::String& name) { return {id, name, 0.0f, {}, false}; }
StubParamSpec toggleSpec(const juce::String& id, const juce::String& name) {
    StubParamTraits traits;
    traits.boolean = true;
    return {id, name, 0.0f, traits, false};
}

/** A hosted plugin as a node of a real engine graph, plus a ModuleComponent card on it -- same
 *  shape as HostedPluginCardTests.cpp's own Rig (kept as its own copy per that file's convention:
 *  Source/UI/CLAUDE.md documents each test area keeping its own small fixture rather than sharing
 *  one across unrelated concerns). */
struct Rig {
    Rig()
        : tempDir(juce::File::getSpecialLocation(juce::File::tempDirectory)
                      .getChildFile("fro137-card-learn-store-" + juce::Uuid().toString()))
        , store(tempDir)
        , editor(engine) {
        editor.setPluginCardLayoutStore(&store);
        editor.onMidiLearnRequested = [](juce::AudioProcessorGraph::NodeID, const juce::String&) {};
        editor.onMidiForgetRequested = [](juce::AudioProcessorGraph::NodeID, const juce::String&) {};
        editor.onAutomateParameterRequested = [this](juce::AudioProcessorGraph::NodeID nodeId,
                                                     const juce::String& paramId) {
            lastAutomateNodeId = nodeId;
            lastAutomateParamId = paramId;
        };
    }

    ~Rig() {
        editor.detachAllModuleComponents();
        tempDir.deleteRecursively();
    }

    HostedPluginModule* addPlugin(std::vector<StubParamSpec> specs) {
        backend.setFactory([specs]() -> std::unique_ptr<StubPluginInstance> {
            return std::make_unique<StubPluginInstance>(2, 2, "Learnable Plugin", 0xC0DE02, "VST3", specs);
        });

        auto* hosted = new HostedPluginModule();
        auto node = engine.getGraph().addNode(std::unique_ptr<juce::AudioProcessor>(hosted));
        module = hosted;
        nodeId = node->nodeID;
        hosted->prepareToPlay(kSampleRate, kBlockSize);
        hosted->loadPlugin(stubDescription(), backend);
        EXPECT_TRUE(pumpUntil([hosted] { return hosted->hasInstance(); }));
        return hosted;
    }

    std::unique_ptr<ModuleComponent> makeCard() { return std::make_unique<ModuleComponent>(module, nodeId, editor); }

    juce::File tempDir;
    synth::PluginCardLayoutStore store;
    AudioEngine engine;
    GraphEditor editor;
    StubBackend backend;
    HostedPluginModule* module = nullptr;
    juce::AudioProcessorGraph::NodeID nodeId;
    juce::AudioProcessorGraph::NodeID lastAutomateNodeId;
    juce::String lastAutomateParamId;
};

juce::Component* findChildByComponentID(ModuleComponent& card, const juce::String& id) {
    for (auto* child : card.getChildren())
        if (child->getComponentID() == id)
            return child;
    return nullptr;
}

juce::ModifierKeys rightClickMods() { return juce::ModifierKeys(juce::ModifierKeys::rightButtonModifier); }

juce::MouseEvent realChildMouseEvent(juce::Component& child, juce::ModifierKeys mods) {
    const auto pos = child.getLocalBounds().getCentre().toFloat();
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), pos, mods, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                            &child, &child, juce::Time::getCurrentTime(), pos, juce::Time::getCurrentTime(), 1, false);
}

// Same idiom as ModuleComponentMidiLearnTests.cpp's rightClickChild(): a real mouseDown() through
// BOTH the child's own handler and the card's registered MouseListener path, never a direct call
// to a menu-builder function.
juce::PopupMenu rightClickChild(ModuleComponent& card, juce::Component& child) {
    juce::PopupMenu captured;
    card.setShowContextMenuHookForTest([&](juce::PopupMenu& menu) { captured = menu; });
    const auto downEvent = realChildMouseEvent(child, rightClickMods());
    child.mouseDown(downEvent);
    card.mouseDown(downEvent);
    child.mouseUp(realChildMouseEvent(child, rightClickMods()));
    card.setShowContextMenuHookForTest(nullptr);
    return captured;
}

std::vector<juce::String> menuItemTexts(const juce::PopupMenu& menu) {
    std::vector<juce::String> texts;
    juce::PopupMenu::MenuItemIterator it(menu);
    while (it.next())
        if (it.getItem().text.isNotEmpty())
            texts.push_back(it.getItem().text);
    return texts;
}

bool menuContains(const juce::PopupMenu& menu, const juce::String& text) {
    const auto texts = menuItemTexts(menu);
    return std::find(texts.begin(), texts.end(), text) != texts.end();
}

} // namespace

// ============================================================================
// 1. Registry
// ============================================================================

TEST(HostedPluginCardMidiLearnTest, HostedControlsAreRegisteredAsHostedWithTheirOwnParamId) {
    Rig rig;
    rig.addPlugin({knobSpec("k1", "Cutoff"), toggleSpec("t1", "Bypass FX")});
    auto card = rig.makeCard();

    auto* knob = findChildByComponentID(*card, "hostedKnob:k1");
    auto* toggle = findChildByComponentID(*card, "hostedToggle:t1");
    ASSERT_NE(knob, nullptr);
    ASSERT_NE(toggle, nullptr);

    const auto knobEntry = card->findMidiLearnableEntryForTest(knob);
    const auto toggleEntry = card->findMidiLearnableEntryForTest(toggle);
    ASSERT_TRUE(knobEntry.has_value());
    ASSERT_TRUE(toggleEntry.has_value());
    EXPECT_TRUE(knobEntry->hosted);
    EXPECT_EQ(knobEntry->paramId, "k1");
    EXPECT_TRUE(toggleEntry->hosted);
    EXPECT_EQ(toggleEntry->paramId, "t1");

    // findMidiLearnableParamForTest (the built-in-only seam) never returns a hosted parameter as a
    // RangedAudioParameter -- it isn't one.
    EXPECT_EQ(card->findMidiLearnableParamForTest(knob), nullptr);
}

TEST(HostedPluginCardMidiLearnTest, ALayoutTriggeredRebuildLeavesNoStaleRegistryEntries) {
    Rig rig;
    rig.addPlugin({knobSpec("k1", "Cutoff"), knobSpec("k2", "Resonance")});
    auto card = rig.makeCard();

    auto* firstKnob = findChildByComponentID(*card, "hostedKnob:k1");
    ASSERT_NE(firstKnob, nullptr);
    ASSERT_TRUE(card->findMidiLearnableEntryForTest(firstKnob).has_value());

    // Writing a per-instance override (the same "cardLayout" extra-state write the picker makes)
    // fires HostedPluginModule::onCardLayoutChanged, which rebuilds the card's widgets.
    synth::CardLayout layout;
    synth::CardSlot slot;
    slot.paramId = "k2";
    slot.kind = synth::CardSlotKind::Auto;
    layout.slots.push_back(slot);
    rig.module->setCardLayoutOverride(layout.toVar());

    auto* newKnob = findChildByComponentID(*card, "hostedKnob:k2");
    ASSERT_NE(newKnob, nullptr) << "the rebuild applied the new layout";
    EXPECT_EQ(findChildByComponentID(*card, "hostedKnob:k1"), nullptr) << "k1 is no longer on the card";

    // Every entry the registry now reports must point at a CURRENT child of the card -- proving no
    // entry was left over from the widget the rebuild just destroyed (firstKnob's memory may already
    // be reused, so this never dereferences it again; it only checks identity against the live tree).
    for (auto* child : card->getChildren())
        if (child == newKnob)
            ASSERT_TRUE(card->findMidiLearnableEntryForTest(child).has_value());
    EXPECT_TRUE(card->findMidiLearnableEntryForTest(newKnob).has_value());
    EXPECT_EQ(card->findMidiLearnableEntryForTest(newKnob)->paramId, "k2");
}

// ============================================================================
// 2. Menu
// ============================================================================

TEST(HostedPluginCardMidiLearnTest, RightClickOnAHostedKnobShowsAutomateThenMidiLearn) {
    Rig rig;
    rig.addPlugin({knobSpec("k1", "Cutoff")});
    auto card = rig.makeCard();

    auto* knob = findChildByComponentID(*card, "hostedKnob:k1");
    ASSERT_NE(knob, nullptr);

    const auto menu = rightClickChild(*card, *knob);
    const auto texts = menuItemTexts(menu);
    ASSERT_GE(texts.size(), 2u);
    EXPECT_EQ(texts[0], "Automate 'Cutoff'");
    EXPECT_EQ(texts[1], "MIDI Learn 'Cutoff'...");
}

TEST(HostedPluginCardMidiLearnTest, ClickingAutomateOnAHostedKnobFiresOnAutomateParameterRequested) {
    Rig rig;
    rig.addPlugin({knobSpec("k1", "Cutoff")});
    auto card = rig.makeCard();
    auto* knob = findChildByComponentID(*card, "hostedKnob:k1");
    ASSERT_NE(knob, nullptr);

    juce::PopupMenu captured;
    card->setShowContextMenuHookForTest([&](juce::PopupMenu& menu) { captured = menu; });
    const auto downEvent = realChildMouseEvent(*knob, rightClickMods());
    knob->mouseDown(downEvent);
    card->mouseDown(downEvent);
    knob->mouseUp(realChildMouseEvent(*knob, rightClickMods()));
    card->setShowContextMenuHookForTest(nullptr);

    juce::PopupMenu::MenuItemIterator it(captured);
    ASSERT_TRUE(it.next());
    EXPECT_EQ(it.getItem().text, "Automate 'Cutoff'");
    ASSERT_TRUE(it.getItem().action != nullptr);
    it.getItem().action();

    EXPECT_EQ(rig.lastAutomateNodeId, rig.nodeId);
    EXPECT_EQ(rig.lastAutomateParamId, "k1");
}

TEST(HostedPluginCardMidiLearnTest, RightClickOnAHostedToggleShowsMidiLearnOnlyNoAutomateItem) {
    Rig rig;
    rig.addPlugin({toggleSpec("t1", "Bypass FX")});
    auto card = rig.makeCard();

    auto* toggle = findChildByComponentID(*card, "hostedToggle:t1");
    ASSERT_NE(toggle, nullptr);

    const auto menu = rightClickChild(*card, *toggle);
    EXPECT_FALSE(menuContains(menu, "Automate 'Bypass FX'"));
    EXPECT_TRUE(menuContains(menu, "MIDI Learn 'Bypass FX'..."));
}

TEST(HostedPluginCardMidiLearnTest, RightClickOnAHostedChoiceShowsMidiLearnOnlyNoAutomateItem) {
    Rig rig;
    StubParamTraits choiceTraits;
    choiceTraits.choices = {"Sine", "Saw"};
    rig.addPlugin({{"c1", "Waveform", 0.0f, choiceTraits, false}});
    auto card = rig.makeCard();

    auto* choice = findChildByComponentID(*card, "hostedChoice:c1");
    ASSERT_NE(choice, nullptr);

    const auto menu = rightClickChild(*card, *choice);
    EXPECT_FALSE(menuContains(menu, "Automate 'Waveform'"));
    EXPECT_TRUE(menuContains(menu, "MIDI Learn 'Waveform'..."));
}

TEST(HostedPluginCardMidiLearnTest, NoMenuItemsWhenTheHostNeverWiredMidiLearn) {
    Rig rig;
    rig.editor.onMidiLearnRequested = nullptr; // headless/plugin build, or MainComponent never wired it
    rig.addPlugin({knobSpec("k1", "Cutoff")});
    auto card = rig.makeCard();

    auto* knob = findChildByComponentID(*card, "hostedKnob:k1");
    ASSERT_NE(knob, nullptr);

    const auto menu = rightClickChild(*card, *knob);
    EXPECT_EQ(menuItemTexts(menu).size(), 1u) << "just the Automate item -- appendMidiLearnMenuItems no-ops";
}
