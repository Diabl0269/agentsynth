// PluginKnobPickerTests.cpp -- FRO132 (docs/control/plugin-card-layout.md#choosing-knobs): the
// "Choose knobs..." popover. Everything runs against Tests/StubPluginInstance.h, the same fake
// hosted instance FRO126/FRO128's tests use, and a PickerRig modelled on HostedPluginCardTests.cpp's
// own Rig (real engine graph node + real PluginCardLayoutStore on a temp directory).
//
// Groups:
//   1. Search, tick/untick, reorder, label -- the row list itself.
//   2. Apply-to scope: This instance vs. All instances (clears the override, broadcasts).
//   3. Presets: save/load/delete, reset to automatic.
//   4. Touch-to-add: via a real gesture, and the off-thread -> message-thread hop.
//   5. Missing parameters.
//   6. Entry points: the card's "Choose knobs..." button and its context-menu item, both driven by a
//      real synthesized gesture through the real handler (a virtual seam stubs the actual
//      juce::CallOutBox, which would otherwise crash a display-less runner).

#include "../../../StubPluginInstance.h"
#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "Modules/CardLayout.h"
#include "Plugin/Hosting/HostedPluginModule.h"
#include "Plugin/Hosting/PluginCardLayoutStore.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Graph/PluginKnobPicker/PluginKnobPickerComponent.h"
#include "UI/Graph/PluginKnobPicker/PluginKnobPickerTouchCapture.h"
#include <chrono>
#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <thread>

using synth::CardLayout;
using synth::CardSlot;
using synth::HostedPluginModule;
using synth::PluginCardLayoutStore;
using synth::PluginIdentity;
using synth::test::StubBackend;
using synth::test::StubParamSpec;
using synth::test::StubPluginInstance;
using synth::ui::PluginKnobPickerComponent;
using synth::ui::PluginKnobPickerTouchCapture;

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

void pump() { juce::MessageManager::getInstance()->runDispatchLoopUntil(30); }

juce::PluginDescription stubDescription() {
    juce::PluginDescription description;
    description.name = "Card Plugin";
    description.pluginFormatName = "VST3";
    description.uniqueId = 0xC0DE01;
    description.deprecatedUid = 0xC0DE01;
    description.fileOrIdentifier = "/nonexistent/test/path/CardPlugin.vst3";
    return description;
}

StubParamSpec knobSpec(const juce::String& id, const juce::String& name) { return {id, name, 0.0f, {}, false}; }

CardSlot slotFor(const juce::String& paramId, int hint = -1) {
    CardSlot slot;
    slot.paramId = paramId;
    slot.indexHint = hint;
    return slot;
}

template <typename T>
T* childWithId(juce::Component& parent, const juce::String& id) {
    for (auto* child : parent.getChildren())
        if (child->getComponentID() == id)
            if (auto* typed = dynamic_cast<T*>(child))
                return typed;
    return nullptr;
}

/** A hosted plugin as a real engine-graph node, plus a store on a temp directory -- the picker's own
 *  Rig. Supports more than one node sharing the SAME plugin identity, for the "All instances" tests. */
struct PickerRig {
    PickerRig()
        : tempDir(juce::File::getSpecialLocation(juce::File::tempDirectory)
                      .getChildFile("fro132-picker-store-" + juce::Uuid().toString()))
        , store(tempDir)
        , editor(engine) {
        editor.setPluginCardLayoutStore(&store);
    }

    ~PickerRig() {
        editor.detachAllModuleComponents();
        tempDir.deleteRecursively();
    }

    struct Node {
        HostedPluginModule* module = nullptr;
        juce::AudioProcessorGraph::NodeID nodeId;
    };

    // The automatic default (docs/control/plugin-card-layout.md, "Choosing knobs as built") would
    // otherwise auto-check every automatable stub parameter the moment the
    // instance publishes, making every test's "nothing is checked yet" starting point depend on how
    // many automatable params a given test happens to declare. Setting an explicit EMPTY instance
    // override right after load gives every test the same deterministic starting point (source =
    // Instance, zero slots) regardless of that; a test that wants a specific starting layout (an
    // orphaned slot, a preset) sets its own override / preset AFTER this and before building a picker.
    Node addPlugin(std::vector<StubParamSpec> specs) {
        backend.setFactory(
            [specs] { return std::make_unique<StubPluginInstance>(2, 2, "Card Plugin", 0xC0DE01, "VST3", specs); });
        auto* hosted = new HostedPluginModule();
        auto node = engine.getGraph().addNode(std::unique_ptr<juce::AudioProcessor>(hosted));
        hosted->prepareToPlay(kSampleRate, kBlockSize);
        hosted->loadPlugin(stubDescription(), backend);
        EXPECT_TRUE(pumpUntil([hosted] { return hosted->hasInstance(); }));
        hosted->setCardLayoutOverride(CardLayout().toVar());
        return {hosted, node->nodeID};
    }

    std::unique_ptr<PluginKnobPickerComponent> makePicker(const Node& node, AppUndoManager* undo = nullptr) {
        return std::make_unique<PluginKnobPickerComponent>(*node.module, &store, engine.getGraph(), node.nodeId, undo);
    }

    juce::File tempDir;
    PluginCardLayoutStore store; // declared before engine/editor: a card/picker may hold a listener on it
    AudioEngine engine;
    GraphEditor editor;
    StubBackend backend;
};

} // namespace

// ============================================================================
// 1. Search, tick/untick, reorder, label
// ============================================================================

TEST(PluginKnobPickerTest, UncheckedRowsListEveryParameterAndCheckedOnesComeFirst) {
    PickerRig rig;
    auto node = rig.addPlugin({knobSpec("a", "Alpha"), knobSpec("b", "Beta"), knobSpec("c", "Gamma")});
    auto picker = rig.makePicker(node);

    ASSERT_EQ(picker->getVisibleRowCountForTest(), 3);
    for (int i = 0; i < 3; ++i)
        EXPECT_FALSE(picker->getVisibleRowCheckedForTest(i));

    picker->triggerRowToggleForTest(2); // ticks "Gamma" (paramId "c"), currently the last row
    ASSERT_EQ(picker->getVisibleRowCountForTest(), 3);
    EXPECT_EQ(picker->getVisibleRowParamIdForTest(0), "c") << "the checked row moves to the front";
    EXPECT_TRUE(picker->getVisibleRowCheckedForTest(0));
}

TEST(PluginKnobPickerTest, SearchFiltersBothCheckedAndUncheckedRowsByDisplayName) {
    PickerRig rig;
    auto node = rig.addPlugin(
        {knobSpec("cutoff", "Filter Cutoff"), knobSpec("res", "Filter Resonance"), knobSpec("drive", "Drive")});
    auto picker = rig.makePicker(node);
    picker->triggerRowToggleForTest(0); // check "Filter Cutoff"

    picker->setSearchTextForTest("filt");
    ASSERT_EQ(picker->getVisibleRowCountForTest(), 2);
    EXPECT_EQ(picker->getVisibleRowParamIdForTest(0), "cutoff") << "checked still comes first";
    EXPECT_EQ(picker->getVisibleRowParamIdForTest(1), "res");

    picker->setSearchTextForTest("");
    EXPECT_EQ(picker->getVisibleRowCountForTest(), 3);
}

TEST(PluginKnobPickerTest, TickingAParameterWritesTheInstanceOverrideLive) {
    PickerRig rig;
    auto node = rig.addPlugin({knobSpec("k", "Knob")});
    auto picker = rig.makePicker(node);

    EXPECT_FALSE(picker->getVisibleRowCheckedForTest(0)) << "precondition: nothing checked yet";
    picker->triggerRowToggleForTest(0);

    auto parsed = CardLayout::fromVar(node.module->getCardLayoutOverride());
    ASSERT_EQ(parsed.status, CardLayout::ParseStatus::Ok);
    ASSERT_EQ(parsed.layout.slots.size(), 1u);
    EXPECT_EQ(parsed.layout.slots[0].paramId, "k");

    picker->triggerRowToggleForTest(0); // untick
    parsed = CardLayout::fromVar(node.module->getCardLayoutOverride());
    ASSERT_EQ(parsed.status, CardLayout::ParseStatus::Ok);
    EXPECT_TRUE(parsed.layout.slots.empty());
}

TEST(PluginKnobPickerTest, LabelEditCommitsAsAPerSlotOverride) {
    PickerRig rig;
    auto node = rig.addPlugin({knobSpec("k", "Knob")});
    auto picker = rig.makePicker(node);
    picker->triggerRowToggleForTest(0);

    picker->setRowLabelForTest(0, "  Cutoff  ");
    picker->commitRowLabelForTest(0);

    auto parsed = CardLayout::fromVar(node.module->getCardLayoutOverride());
    ASSERT_EQ(parsed.status, CardLayout::ParseStatus::Ok);
    ASSERT_EQ(parsed.layout.slots.size(), 1u);
    ASSERT_TRUE(parsed.layout.slots[0].label.has_value());
    EXPECT_EQ(*parsed.layout.slots[0].label, "Cutoff") << "trimmed";

    picker->setRowLabelForTest(0, "");
    picker->commitRowLabelForTest(0);
    parsed = CardLayout::fromVar(node.module->getCardLayoutOverride());
    EXPECT_FALSE(parsed.layout.slots[0].label.has_value()) << "empty text clears the override";
}

TEST(PluginKnobPickerTest, DraggingAcheckedRowToANewIndexReordersTheLayout) {
    PickerRig rig;
    auto node = rig.addPlugin({knobSpec("a", "Alpha"), knobSpec("b", "Beta"), knobSpec("c", "Gamma")});
    auto picker = rig.makePicker(node);
    picker->triggerRowToggleForTest(0); // a
    picker->triggerRowToggleForTest(1); // rows shifted: now checks "b" (Beta), the new row 1
    // Checked order should now be [a, b].
    ASSERT_EQ(picker->getVisibleRowParamIdForTest(0), "a");
    ASSERT_EQ(picker->getVisibleRowParamIdForTest(1), "b");

    picker->dragCheckedRowToIndexForTest("a", 1); // moves "a" after "b"

    auto parsed = CardLayout::fromVar(node.module->getCardLayoutOverride());
    ASSERT_EQ(parsed.status, CardLayout::ParseStatus::Ok);
    ASSERT_EQ(parsed.layout.slots.size(), 2u);
    EXPECT_EQ(parsed.layout.slots[0].paramId, "b");
    EXPECT_EQ(parsed.layout.slots[1].paramId, "a");
}

// ============================================================================
// 2. Apply-to scope
// ============================================================================

namespace {
struct StoreBroadcastRecorder : PluginCardLayoutStore::Listener {
    void layoutChangedForPlugin(const PluginIdentity&) override { ++calls; }
    int calls = 0;
};
} // namespace

TEST(PluginKnobPickerTest, SwitchingToAllInstancesClearsTheOverrideAndBroadcasts) {
    PickerRig rig;
    auto node = rig.addPlugin({knobSpec("k", "Knob")});
    auto picker = rig.makePicker(node);
    picker->triggerRowToggleForTest(0); // "This instance" override now set

    ASSERT_FALSE(node.module->getCardLayoutOverride().isVoid());

    StoreBroadcastRecorder recorder;
    rig.store.addListener(&recorder);
    picker->setApplyToAllInstancesForTest(true);
    rig.store.removeListener(&recorder);

    EXPECT_TRUE(node.module->getCardLayoutOverride().isVoid()) << "this instance now follows the default";
    EXPECT_GE(recorder.calls, 1) << "every open instance without an override is told to rebuild";

    const auto loaded = rig.store.loadDefault(node.module->getIdentity());
    ASSERT_EQ(loaded.status, PluginCardLayoutStore::LoadStatus::Ok);
    ASSERT_EQ(loaded.layout.slots.size(), 1u);
    EXPECT_EQ(loaded.layout.slots[0].paramId, "k");
}

TEST(PluginKnobPickerTest, AllInstancesEditDoesNotTouchAnUnrelatedNodesOverride) {
    PickerRig rig;
    auto a = rig.addPlugin({knobSpec("k", "Knob")});
    auto b = rig.addPlugin({knobSpec("k", "Knob")}); // same identity (same stubDescription())

    auto pickerA = rig.makePicker(a);
    pickerA->setApplyToAllInstancesForTest(true);
    pickerA->triggerRowToggleForTest(0);

    EXPECT_TRUE(a.module->getCardLayoutOverride().isVoid());
    const auto bOverride = CardLayout::fromVar(b.module->getCardLayoutOverride());
    ASSERT_EQ(bOverride.status, CardLayout::ParseStatus::Ok);
    EXPECT_TRUE(bOverride.layout.slots.empty()) << "b's own (empty) override is untouched by a's edit";
    const auto loaded = rig.store.loadDefault(b.module->getIdentity());
    ASSERT_EQ(loaded.status, PluginCardLayoutStore::LoadStatus::Ok) << "b's identity shares the same stored default";
}

// ============================================================================
// 3. Presets
// ============================================================================

TEST(PluginKnobPickerTest, SaveLoadAndDeletePreset) {
    PickerRig rig;
    auto node = rig.addPlugin({knobSpec("k", "Knob"), knobSpec("j", "Other")});
    auto picker = rig.makePicker(node);

    // Two presets, each named after the parameter it checks -- picking one after the other is a
    // real combo SELECTION CHANGE (picking the just-saved preset again would be a no-op: JUCE's
    // own ComboBox only fires onChange when the selected id actually changes, and Save As already
    // leaves the combo pointed at what it just saved).
    picker->triggerRowToggleForTest(0); // check "k"
    picker->triggerSaveAsPresetForTest("Preset K");
    EXPECT_TRUE(picker->getPresetNamesForTest().contains("Preset K"));

    picker->triggerRowToggleForTest(0); // untick "k"
    picker->triggerRowToggleForTest(0); // check "j" (now the only unchecked row)
    picker->triggerSaveAsPresetForTest("Preset J");
    EXPECT_TRUE(picker->getPresetNamesForTest().contains("Preset J"));

    picker->selectPresetForTest("Preset K");
    auto parsed = CardLayout::fromVar(node.module->getCardLayoutOverride());
    ASSERT_EQ(parsed.status, CardLayout::ParseStatus::Ok);
    ASSERT_EQ(parsed.layout.slots.size(), 1u);
    EXPECT_EQ(parsed.layout.slots[0].paramId, "k");

    picker->triggerDeletePresetForTest("Preset K");
    EXPECT_FALSE(picker->getPresetNamesForTest().contains("Preset K"));
    EXPECT_TRUE(picker->getPresetNamesForTest().contains("Preset J"));
}

TEST(PluginKnobPickerTest, ResetToAutomaticRemovesTheOverrideAndShowsTheAutomaticSet) {
    PickerRig rig;
    auto node = rig.addPlugin({knobSpec("k", "Knob")});
    auto picker = rig.makePicker(node);
    picker->triggerRowToggleForTest(0);
    ASSERT_FALSE(node.module->getCardLayoutOverride().isVoid());

    picker->triggerResetToAutomaticForTest();

    EXPECT_TRUE(node.module->getCardLayoutOverride().isVoid());
    EXPECT_TRUE(picker->getVisibleRowCheckedForTest(0)) << "the single automatable parameter is automatically shown";
}

// ============================================================================
// 4. Touch-to-add
// ============================================================================

TEST(PluginKnobPickerTest, TouchToAddViaAGestureAppendsTheParameter) {
    PickerRig rig;
    auto node = rig.addPlugin({knobSpec("k", "Knob"), knobSpec("j", "Other")});
    auto picker = rig.makePicker(node);

    bool openRequested = false;
    picker->onOpenPluginEditorRequested = [&] { openRequested = true; };

    picker->setTouchToAddArmedForTest(true);
    EXPECT_TRUE(openRequested) << "arming opens the plugin editor if it wasn't already";
    EXPECT_TRUE(picker->isTouchToAddArmedForTest());

    picker->simulateTouchGestureForTest(1); // "j"'s live parameter index
    pump();

    auto parsed = CardLayout::fromVar(node.module->getCardLayoutOverride());
    ASSERT_EQ(parsed.status, CardLayout::ParseStatus::Ok);
    ASSERT_EQ(parsed.layout.slots.size(), 1u);
    EXPECT_EQ(parsed.layout.slots[0].paramId, "j");
}

TEST(PluginKnobPickerTouchCaptureTest, AnOffThreadGestureIsHoppedToTheMessageThread) {
    PickerRig rig;
    auto node = rig.addPlugin({knobSpec("k", "Knob")});

    PluginKnobPickerTouchCapture capture(*node.module);
    int touchedIndex = -1;
    capture.onParameterTouched = [&](int index) { touchedIndex = index; };
    capture.setArmed(true);

    auto* param = node.module->getActiveInstanceForEditor()->getParameters()[0];
    std::thread worker([param] {
        param->beginChangeGesture();
        param->endChangeGesture();
    });
    worker.join();

    EXPECT_EQ(touchedIndex, -1) << "the callback must not run on the reporting thread";
    pump();
    EXPECT_EQ(touchedIndex, 0);

    capture.setArmed(false);
}

// ============================================================================
// 5. Missing parameters
// ============================================================================

TEST(PluginKnobPickerTest, AnOverrideNamingAGoneParameterShowsAsMissingAndIsDroppedOnTheNextApply) {
    PickerRig rig;
    auto node = rig.addPlugin({knobSpec("k", "Knob")});
    CardLayout layout;
    layout.slots = {slotFor("k"), slotFor("gone-param")};
    node.module->setCardLayoutOverride(layout.toVar());

    auto picker = rig.makePicker(node);
    EXPECT_EQ(picker->getMissingParameterCountForTest(), 1);
    EXPECT_TRUE(picker->getMissingParameterLineForTest().contains("gone-param"));
    EXPECT_EQ(picker->getVisibleRowCountForTest(), 1) << "only the resolvable parameter gets a row";

    picker->triggerRowToggleForTest(0); // untick "k" -- any edit applies and drops the missing slot
    picker->triggerRowToggleForTest(0); // and re-tick it
    EXPECT_EQ(picker->getMissingParameterCountForTest(), 0);

    auto parsed = CardLayout::fromVar(node.module->getCardLayoutOverride());
    ASSERT_EQ(parsed.status, CardLayout::ParseStatus::Ok);
    ASSERT_EQ(parsed.layout.slots.size(), 1u);
    EXPECT_EQ(parsed.layout.slots[0].paramId, "k");
}

// ============================================================================
// 6. Entry points: real gestures through the real handlers
// ============================================================================

namespace {
// Stubs ONLY the real juce::CallOutBox construction (which crashes a display-less test runner --
// docs/timeline/ruler.md#opening-a-menu-is-a-protected-virtual, the same reasoning
// ModuleLibraryHelpPopupTests.cpp's RecordingCallOutBoxModuleLibraryComponent documents), leaving
// showPluginKnobPicker()'s own real liveness check and callback wiring fully real.
class RecordingModuleComponent final : public ModuleComponent {
public:
    using ModuleComponent::ModuleComponent;
    int callOutBoxLaunches = 0;

protected:
    void launchPluginKnobPickerCallOutBox(std::unique_ptr<juce::Component>, juce::Rectangle<int>) override {
        ++callOutBoxLaunches;
    }
};

juce::MouseEvent rightClickAt(juce::Component& comp, juce::Point<int> position) {
    const auto pos = position.toFloat();
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), pos,
                            juce::ModifierKeys(juce::ModifierKeys::rightButtonModifier), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                            &comp, &comp, juce::Time::getCurrentTime(), pos, juce::Time::getCurrentTime(), 1, false);
}

const juce::PopupMenu::Item* findMenuItemByText(const juce::PopupMenu& menu, const juce::String& text) {
    juce::PopupMenu::MenuItemIterator it(menu, true);
    while (it.next())
        if (it.getItem().text == text)
            return &it.getItem();
    return nullptr;
}
} // namespace

TEST(PluginKnobPickerEntryPointTest, ChooseKnobsButtonOpensThePickerThroughTheRealClickHandler) {
    PickerRig rig;
    auto node = rig.addPlugin({knobSpec("k", "Knob")});
    RecordingModuleComponent card(node.module, node.nodeId, rig.editor);

    auto* button = childWithId<juce::TextButton>(card, "chooseKnobs");
    ASSERT_NE(button, nullptr);
    ASSERT_NE(button->onClick, nullptr);
    button->onClick(); // the real handler a click invokes -- see ModuleComponentEnvelopeCardTests.cpp's
                       // own comment for why this suite calls onClick() rather than triggerClick()
                       // (which posts an async message no headless test pumps)

    EXPECT_EQ(card.callOutBoxLaunches, 1);
    card.detachFromProcessor();
}

TEST(PluginKnobPickerEntryPointTest, ContextMenuOffersChooseKnobsForAHostedPluginNodeOnly) {
    PickerRig rig;
    auto node = rig.addPlugin({knobSpec("k", "Knob")});
    RecordingModuleComponent card(node.module, node.nodeId, rig.editor);
    card.setSize(240, 200);

    juce::PopupMenu capturedMenu;
    card.setShowContextMenuHookForTest([&capturedMenu](juce::PopupMenu& m) { capturedMenu = m; });
    card.mouseDown(rightClickAt(card, {card.getWidth() / 2, card.getHeight() - 10}));

    const auto* item = findMenuItemByText(capturedMenu, "Choose knobs...");
    ASSERT_NE(item, nullptr);
    ASSERT_TRUE(static_cast<bool>(item->action));
    item->action();

    EXPECT_EQ(card.callOutBoxLaunches, 1);
    card.detachFromProcessor();
}
