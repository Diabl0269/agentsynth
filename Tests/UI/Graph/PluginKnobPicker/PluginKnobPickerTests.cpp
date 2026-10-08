// PluginKnobPickerTests.cpp (docs/control/plugin-card-layout.md#choosing-knobs): the
// "Edit Layout..." popover. Everything runs against Tests/StubPluginInstance.h, the same fake
// hosted instance the other picker tests use, and a PickerRig modelled on HostedPluginCardTests.cpp's
// own Rig (real engine graph node + real PluginCardLayoutStore on a temp directory).
//
// Groups:
//   1. Search, tick/untick, reorder, label -- the row list itself.
//   2. Apply-to scope: This instance vs. All instances (clears the override, broadcasts).
//   3. Presets: save/load/delete, reset to automatic.
//   4. Touch-to-add: via a real gesture, and the off-thread -> message-thread hop.
//   5. Missing parameters.
//   6. Entry points: the card's "Edit Layout..." button and its context-menu item, both driven by a
//      real synthesized gesture through the real handler (a virtual seam stubs the actual
//      juce::CallOutBox, which would otherwise crash a display-less runner).
//   7. "Add to card" in the plugin window's parameter context menu, and the card menu's "Add control from
//      plugin window..." (touch capture pre-armed). The real juce popup and a real VST3 plugin cannot run
//      here: the menu is built through HostedPluginModule::buildParameterContextMenu, the very function the
//      patched JUCE host calls, and the item's action is invoked directly.

#include "../../../StubPluginInstance.h"
#include "../../Timeline/TimelinePanel/TimelinePanelTestEvents.h"
#include "../GraphEditor/GraphEditorTestHelpers.h"
#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "Modules/CardLayout.h"
#include "Modules/OscillatorModule.h"
#include "Plugin/Hosting/HostedPluginEditorWindow.h"
#include "Plugin/Hosting/HostedPluginModule.h"
#include "Plugin/Hosting/HostedPluginWindowManager.h"
#include "Plugin/Hosting/PluginCardLayoutStore.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Graph/PluginKnobPicker/HostedParameterCardMenu.h"
#include "UI/Graph/PluginKnobPicker/PluginKnobPickerComponent.h"
#include "UI/Graph/PluginKnobPicker/PluginKnobPickerTouchCapture.h"
#include "UI/Layout/DragCursor.h"
#include "UI/Layout/SplitButton.h"
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

// ---- Reordering with the real mouse path: lift, glide aside, ONE commit on release, Esc cancels ----
// Headless, so the picker is not showing and the animator lands every glide instantly.

namespace {
constexpr float kRowStride = 26.0f; // CardLayoutEditorRow::kRowHeight

// Hand-built events on a row's grab handle. The list position is turned into handle-local coordinates
// at every event, because the handle moves with its row while the row is dragged.
struct GripDrag {
    PluginKnobPickerComponent& picker;
    int row;
    float pressY;

    juce::Component& handle() const { return *picker.getRowDragHandleForTest(row); }
    juce::Point<float> local(float listY) const {
        auto& content = picker.getRowsContentForTest();
        const float x = content.getLocalPoint(&handle(), handle().getLocalBounds().toFloat().getCentre()).x;
        return handle().getLocalPoint(&content, juce::Point<float>(x, listY));
    }
    void down() { handle().mouseDown(makeClickEvent(handle(), local(pressY))); }
    void dragBy(float dy) { handle().mouseDrag(makeDragEvent(handle(), local(pressY + dy), local(pressY))); }
    void up(float dy) { handle().mouseUp(makeClickEvent(handle(), local(pressY + dy))); }
};

GripDrag gripDragOf(PluginKnobPickerComponent& picker, int row) {
    return {picker, row, static_cast<float>(picker.getRowBoundsForTest(row).getCentreY())};
}

std::vector<juce::String> committedOrder(const PickerRig::Node& node) {
    std::vector<juce::String> ids;
    for (const auto& slot : CardLayout::fromVar(node.module->getCardLayoutOverride()).layout.slots)
        ids.push_back(slot.paramId);
    return ids;
}

/** Ticks the first `count` parameters, leaving them checked in order a, b, c, ... */
void checkFirst(PluginKnobPickerComponent& picker, int count) {
    for (int i = 0; i < count; ++i)
        picker.triggerRowToggleForTest(i);
}
} // namespace

TEST(PluginKnobPickerTest, ADragAcrossTwoRowsCommitsOnceOnReleaseAsOneUndoStep) {
    PickerRig rig;
    auto node = rig.addPlugin({knobSpec("a", "Alpha"), knobSpec("b", "Beta"), knobSpec("c", "Gamma")});
    AppUndoManager undo;
    auto picker = rig.makePicker(node, &undo);
    checkFirst(*picker, 3);
    ASSERT_EQ(committedOrder(node), (std::vector<juce::String>{"a", "b", "c"}));
    const int serialBefore = undo.getEditSerial();

    auto drag = gripDragOf(*picker, 0);
    drag.down();
    drag.dragBy(kRowStride);
    drag.dragBy(2.0f * kRowStride + 4.0f);
    EXPECT_EQ(undo.getEditSerial(), serialBefore) << "nothing is committed while the row is still held";
    EXPECT_EQ(committedOrder(node), (std::vector<juce::String>{"a", "b", "c"}));
    drag.up(2.0f * kRowStride + 4.0f);

    EXPECT_EQ(committedOrder(node), (std::vector<juce::String>{"b", "c", "a"})) << "same final order as before";
    EXPECT_EQ(undo.getEditSerial(), serialBefore + 1) << "a multi-row drag is ONE undo step, not one per row passed";
    EXPECT_FALSE(picker->isRowDragActiveForTest());
}

TEST(PluginKnobPickerTest, TheDraggedRowFollowsThePointerWhileItsNeighboursGlideAside) {
    PickerRig rig;
    auto node = rig.addPlugin({knobSpec("a", "Alpha"), knobSpec("b", "Beta"), knobSpec("c", "Gamma")});
    auto picker = rig.makePicker(node);
    checkFirst(*picker, 3);
    const auto a = picker->getRowBoundsForTest(0);
    const auto c = picker->getRowBoundsForTest(2);

    auto drag = gripDragOf(*picker, 0);
    drag.down();
    drag.dragBy(30.0f); // past Beta's midpoint, short of Gamma's

    EXPECT_TRUE(picker->isRowDragActiveForTest());
    EXPECT_EQ(picker->getRowBoundsForTest(0).getY(), a.getY() + 30) << "the lifted row sits under the pointer";
    EXPECT_EQ(picker->getRowBoundsForTest(1).getY(), a.getY()) << "Beta moved up into the vacated slot";
    EXPECT_EQ(picker->getRowBoundsForTest(2).getY(), c.getY()) << "Gamma stays put";
    EXPECT_EQ(picker->getVisibleRowParamIdForTest(0), "a") << "the rows are not rebuilt during the drag";

    drag.dragBy(2.0f * kRowStride + 4.0f);
    EXPECT_EQ(picker->getRowBoundsForTest(0).getY(), c.getY()) << "held inside the list, at its last slot";
    EXPECT_EQ(picker->getRowBoundsForTest(2).getY(), picker->getRowBoundsForTest(1).getY() + 26) << "Gamma moved up";
    drag.up(2.0f * kRowStride + 4.0f);
}

TEST(PluginKnobPickerTest, EscapeMidDragCommitsNothingAndPutsEveryRowBack) {
    PickerRig rig;
    auto node = rig.addPlugin({knobSpec("a", "Alpha"), knobSpec("b", "Beta"), knobSpec("c", "Gamma")});
    AppUndoManager undo;
    auto picker = rig.makePicker(node, &undo);
    checkFirst(*picker, 3);
    const int serialBefore = undo.getEditSerial();
    std::vector<int> before;
    for (int i = 0; i < 3; ++i)
        before.push_back(picker->getRowBoundsForTest(i).getY());

    auto drag = gripDragOf(*picker, 0);
    drag.down();
    drag.dragBy(2.0f * kRowStride + 4.0f);
    ASSERT_TRUE(picker->isRowDragActiveForTest());
    EXPECT_TRUE(picker->sendEscapeToRowDragForTest());
    drag.up(2.0f * kRowStride + 4.0f);

    EXPECT_EQ(undo.getEditSerial(), serialBefore) << "no undo step";
    EXPECT_EQ(committedOrder(node), (std::vector<juce::String>{"a", "b", "c"}));
    EXPECT_FALSE(picker->isRowDragActiveForTest());
    for (int i = 0; i < 3; ++i)
        EXPECT_EQ(picker->getRowBoundsForTest(i).getY(), before[static_cast<size_t>(i)]) << "row " << i;
}

TEST(PluginKnobPickerTest, TheDropLandsRightWhenASearchHidesSomeCheckedRows) {
    PickerRig rig;
    auto node = rig.addPlugin({knobSpec("a", "Alpha"), knobSpec("b", "Bravo"), knobSpec("c", "Alpha Two")});
    auto picker = rig.makePicker(node);
    checkFirst(*picker, 3);
    picker->setSearchTextForTest("alpha"); // hides the checked "Bravo"
    ASSERT_EQ(picker->getVisibleRowCountForTest(), 2);

    auto drag = gripDragOf(*picker, 1); // "Alpha Two" (c)
    drag.down();
    drag.dragBy(-kRowStride - 4.0f);
    drag.up(-kRowStride - 4.0f);

    EXPECT_EQ(committedOrder(node), (std::vector<juce::String>{"c", "a", "b"}));
}

TEST(PluginKnobPickerTest, TheGrabHandleShowsTheGrabCursor) {
    PickerRig rig;
    auto node = rig.addPlugin({knobSpec("a", "Alpha")});
    auto picker = rig.makePicker(node);
    checkFirst(*picker, 1);
    ASSERT_NE(picker->getRowDragHandleForTest(0), nullptr);
    EXPECT_TRUE(picker->getRowDragHandleForTest(0)->getMouseCursor() == synth::ui::dragGrabCursor());
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
// 4. Touch capture
// ============================================================================

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
    std::unique_ptr<juce::Component> lastEditor; // what the call-out box would have shown

protected:
    void launchCardLayoutEditorCallOutBox(std::unique_ptr<juce::Component> editor, juce::Rectangle<int>) override {
        ++callOutBoxLaunches;
        lastEditor = std::move(editor);
    }
};

juce::MouseEvent rightClickAt(juce::Component& comp, juce::Point<int> position) {
    const auto pos = position.toFloat();
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), pos,
                            juce::ModifierKeys(juce::ModifierKeys::rightButtonModifier), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                            &comp, &comp, juce::Time::getCurrentTime(), pos, juce::Time::getCurrentTime(), 1, false);
}

// A press and release as the mouse delivers them (a button's click fires on the release).
void clickCentre(juce::Component& c) {
    const auto at = c.getLocalBounds().getCentre();
    c.mouseDown(makeModuleClickWithMods(c, at, plainLeftClick()));
    c.mouseUp(makeModuleClickWithMods(c, at, plainLeftClick()));
    pump();
}

const juce::PopupMenu::Item* findMenuItemByText(const juce::PopupMenu& menu, const juce::String& text) {
    juce::PopupMenu::MenuItemIterator it(menu, true);
    while (it.next())
        if (it.getItem().text == text)
            return &it.getItem();
    return nullptr;
}
} // namespace

TEST(PluginKnobPickerEntryPointTest, TheListHalfOpensThePickerThroughTheRealClickHandler) {
    PickerRig rig;
    auto node = rig.addPlugin({knobSpec("k", "Knob")});
    RecordingModuleComponent card(node.module, node.nodeId, rig.editor);

    auto* split = childWithId<synth::ui::SplitButton>(card, "addControls");
    ASSERT_NE(split, nullptr);
    split->setSize(80, synth::ui::SplitButton::kHeight);
    clickCentre(split->leftHalf());

    EXPECT_EQ(card.callOutBoxLaunches, 1);
    EXPECT_NE(dynamic_cast<PluginKnobPickerComponent*>(card.lastEditor.get()), nullptr);
    EXPECT_FALSE(card.isPluginTouchToAdd()) << "the list half never starts the by-moving mode";
    card.detachFromProcessor();
}

TEST(PluginKnobPickerEntryPointTest, ContextMenuEditLayoutOpensThePickerForAHostedPluginNode) {
    PickerRig rig;
    auto node = rig.addPlugin({knobSpec("k", "Knob")});
    RecordingModuleComponent card(node.module, node.nodeId, rig.editor);
    card.setSize(240, 200);

    juce::PopupMenu capturedMenu;
    card.setShowContextMenuHookForTest([&capturedMenu](juce::PopupMenu& m) { capturedMenu = m; });
    card.mouseDown(rightClickAt(card, {card.getWidth() / 2, card.getHeight() - 10}));

    const auto* item = findMenuItemByText(capturedMenu, "Edit Layout...");
    ASSERT_NE(item, nullptr);
    ASSERT_TRUE(static_cast<bool>(item->action));
    item->action();

    EXPECT_EQ(card.callOutBoxLaunches, 1);
    card.detachFromProcessor();
}

// ============================================================================
// 7. "Add to card" from the plugin window; "Add control from plugin window..." on the card
// ============================================================================

namespace {
std::vector<juce::String> overrideParamIds(const HostedPluginModule& module) {
    std::vector<juce::String> ids;
    const auto parsed = CardLayout::fromVar(module.getCardLayoutOverride());
    for (const auto& slot : parsed.layout.slots)
        ids.push_back(slot.paramId);
    return ids;
}

juce::PopupMenu windowMenuFor(HostedPluginModule& module, const juce::String& paramId) {
    juce::PopupMenu menu;
    module.buildParameterContextMenu(paramId, menu);
    return menu;
}
} // namespace

TEST(AddToCardMenuTest, AnUnshownParameterOffersAddToCardAndTheChoiceIsOneUndoStep) {
    PickerRig rig;
    auto node = rig.addPlugin({knobSpec("k", "Knob"), knobSpec("j", "Other")});
    AppUndoManager undo;
    node.module->onParameterContextMenu = [&](const juce::String& id, juce::PopupMenu& menu) {
        synth::ui::appendAddToCardMenuItem(menu, *node.module, &rig.store, rig.engine.getGraph(), node.nodeId, &undo,
                                           id);
    };

    const auto menu = windowMenuFor(*node.module, "k");
    const auto* item = findMenuItemByText(menu, "Add to card");
    ASSERT_NE(item, nullptr);
    EXPECT_TRUE(item->isEnabled);
    EXPECT_EQ(findMenuItemByText(menu, "On the card"), nullptr);
    ASSERT_TRUE(static_cast<bool>(item->action));

    const int serialBefore = undo.getEditSerial();
    item->action();

    EXPECT_EQ(overrideParamIds(*node.module), (std::vector<juce::String>{"k"}));
    EXPECT_EQ(undo.getEditSerial(), serialBefore + 1);
    ASSERT_TRUE(undo.undo());
    EXPECT_TRUE(overrideParamIds(*node.module).empty()) << "one undo takes the parameter off the card again";
}

TEST(AddToCardMenuTest, AParameterAlreadyOnTheCardShowsADisabledOnTheCardItem) {
    PickerRig rig;
    auto node = rig.addPlugin({knobSpec("k", "Knob")});
    CardLayout layout;
    layout.version = CardLayout::kCurrentVersion;
    layout.slots.push_back(slotFor("k"));
    node.module->setCardLayoutOverride(layout.toVar());
    AppUndoManager undo;

    juce::PopupMenu menu;
    synth::ui::appendAddToCardMenuItem(menu, *node.module, &rig.store, rig.engine.getGraph(), node.nodeId, &undo, "k");

    const auto* item = findMenuItemByText(menu, "On the card");
    ASSERT_NE(item, nullptr);
    EXPECT_FALSE(item->isEnabled);
    EXPECT_EQ(findMenuItemByText(menu, "Add to card"), nullptr);
    EXPECT_EQ(undo.getEditSerial(), 0);
}

TEST(AddToCardMenuTest, AnIdTheInstanceDoesNotHaveAddsNothing) {
    PickerRig rig;
    auto node = rig.addPlugin({knobSpec("k", "Knob")});
    juce::PopupMenu menu;
    synth::ui::appendAddToCardMenuItem(menu, *node.module, &rig.store, rig.engine.getGraph(), node.nodeId, nullptr,
                                       "no-such-parameter");
    EXPECT_EQ(menu.getNumItems(), 0);
}

TEST(AddToCardMenuTest, ChoosingAfterTheModuleIsGoneIsANoOp) {
    PickerRig rig;
    auto node = rig.addPlugin({knobSpec("k", "Knob")});
    juce::PopupMenu menu;
    synth::ui::appendAddToCardMenuItem(menu, *node.module, &rig.store, rig.engine.getGraph(), node.nodeId, nullptr,
                                       "k");
    const auto* item = findMenuItemByText(menu, "Add to card");
    ASSERT_NE(item, nullptr);
    node.module->unloadPlugin();
    item->action(); // must not touch the unloaded module
    EXPECT_TRUE(overrideParamIds(*node.module).empty());
}

TEST(AddToCardMenuTest, TheCardRegistersItsHookAndRebuildsWhenAParameterIsAdded) {
    PickerRig rig;
    auto node = rig.addPlugin({knobSpec("k", "Knob")});
    RecordingModuleComponent card(node.module, node.nodeId, rig.editor);
    card.setSize(240, 200);
    ASSERT_NE(node.module->onParameterContextMenu, nullptr);
    EXPECT_EQ(childWithId<juce::Slider>(card, "hostedKnob:k"), nullptr) << "precondition: not on the card";

    const auto menu = windowMenuFor(*node.module, "k");
    const auto* item = findMenuItemByText(menu, "Add to card");
    ASSERT_NE(item, nullptr);
    item->action();

    EXPECT_NE(childWithId<juce::Slider>(card, "hostedKnob:k"), nullptr) << "the card rebuilt with the new control";
    EXPECT_NE(findMenuItemByText(windowMenuFor(*node.module, "k"), "On the card"), nullptr);

    card.detachFromProcessor();
    EXPECT_EQ(windowMenuFor(*node.module, "k").getNumItems(), 0) << "a torn-down card leaves no hook behind";
}

TEST(AddToCardMenuTest, TheCardMenuOffersAddControlFromPluginWindowAndStartsTheModeWithoutAPopover) {
    PickerRig rig;
    auto node = rig.addPlugin({knobSpec("k", "Knob")});
    std::vector<juce::AudioProcessorGraph::NodeID> editorRequests;
    rig.editor.onOpenPluginEditorRequested = [&](juce::AudioProcessorGraph::NodeID id) {
        editorRequests.push_back(id);
    };
    RecordingModuleComponent card(node.module, node.nodeId, rig.editor);

    const auto menu = card.buildModuleContextMenu();
    const auto* item = findMenuItemByText(menu, "Add control from plugin window...");
    ASSERT_NE(item, nullptr);
    EXPECT_TRUE(item->isEnabled);
    ASSERT_TRUE(static_cast<bool>(item->action));
    item->action();

    EXPECT_EQ(card.callOutBoxLaunches, 0) << "the plugin's window must stay unobstructed: no popover";
    EXPECT_TRUE(card.isPluginTouchToAdd());
    ASSERT_EQ(editorRequests.size(), 1u) << "starting opens the plugin's own window";
    EXPECT_EQ(editorRequests[0], node.nodeId);

    // "Edit Layout..." still opens the picker.
    card.onChooseKnobsRequested();
    EXPECT_EQ(card.callOutBoxLaunches, 1);
    EXPECT_NE(dynamic_cast<PluginKnobPickerComponent*>(card.lastEditor.get()), nullptr);
    card.detachFromProcessor();
}

TEST(AddToCardMenuTest, ABuiltInCardMenuHasNoAddControlFromPluginWindowItem) {
    PickerRig rig;
    auto* oscillator = new OscillatorModule();
    auto node = rig.engine.getGraph().addNode(std::unique_ptr<juce::AudioProcessor>(oscillator));
    ModuleComponent card(oscillator, node->nodeID, rig.editor);
    EXPECT_EQ(findMenuItemByText(card.buildModuleContextMenu(), "Add control from plugin window..."), nullptr);
    card.detachFromProcessor();
}

// ============================================================================
// 8. Add by moving a control in the plugin
// ============================================================================

namespace {

/** A hosted card wired the way MainComponent wires it: a real (headless) window manager behind the editor and
 *  tab callbacks, and the window's Done / Esc / close reaching back to the card. */
struct AddingRig {
    explicit AddingRig(std::vector<StubParamSpec> specs) {
        node = rig.addPlugin(std::move(specs));
        rig.editor.onOpenPluginEditorRequested = [this](juce::AudioProcessorGraph::NodeID id) {
            manager.openEditorFor(node.module, id);
        };
        rig.editor.onPluginAddingControlsChanged = [this](juce::AudioProcessorGraph::NodeID id, bool on) {
            manager.setAddingControls(id, on);
        };
        manager.onAddingControlsEnded = [this](juce::AudioProcessorGraph::NodeID) { card->setPluginTouchToAdd(false); };
        card = std::make_unique<RecordingModuleComponent>(node.module, node.nodeId, rig.editor);
        card->setSize(240, 200);
        split().setSize(80, synth::ui::SplitButton::kHeight);
    }
    ~AddingRig() { card->detachFromProcessor(); }

    synth::ui::SplitButton& split() { return *childWithId<synth::ui::SplitButton>(*card, "addControls"); }
    synth::HostedPluginEditorWindow* window() { return manager.getWindowForTest(node.nodeId); }
    synth::HostedPluginEditorFrame* frame() { return window() != nullptr ? window()->getFrameForTest() : nullptr; }
    bool tabIsUp() { return frame() != nullptr && frame()->hasTabForTest(); }
    void startByClickingTheHand() { clickCentre(split().rightHalf()); }
    /** Everything that must be true once the mode is off, wherever it was ended from. */
    void expectEnded(const char* how) {
        SCOPED_TRACE(how);
        EXPECT_FALSE(card->isPluginTouchToAdd());
        EXPECT_FALSE(split().isRightLit()) << "the hand half un-lights";
        EXPECT_FALSE(split().rightHalf().getToggleState());
        EXPECT_FALSE(tabIsUp()) << "the tab leaves the window";
        if (window() != nullptr)
            EXPECT_FALSE(window()->isAddingControls());
    }

    PickerRig rig;
    PickerRig::Node node;
    synth::HostedPluginWindowManager manager;
    std::unique_ptr<RecordingModuleComponent> card;
};

} // namespace

TEST(AddByMovingTest, TheSplitButtonsHalvesHaveNamesTooltipsAndAreTabStops) {
    AddingRig a({knobSpec("k", "Knob")});
    auto& left = a.split().leftHalf();
    auto& right = a.split().rightHalf();

    EXPECT_EQ(left.getTitle(), "Add from list");
    EXPECT_EQ(left.getTooltip(), "Add from list");
    EXPECT_EQ(right.getTitle(), "Add by moving a control in the plugin");
    EXPECT_EQ(right.getTooltip(), "Add by moving a control in the plugin");
    EXPECT_TRUE(left.getWantsKeyboardFocus());
    EXPECT_TRUE(right.getWantsKeyboardFocus());
    for (const auto& text : {left.getTitle(), left.getTooltip(), right.getTitle(), right.getTooltip()})
        EXPECT_NE(text, text.toUpperCase()) << "no ALL-CAPS text";

    a.startByClickingTheHand();
    EXPECT_EQ(right.getTitle(), "Add by moving a control in the plugin, on") << "a screen reader hears the state";
    EXPECT_TRUE(right.getToggleState());
}

TEST(AddByMovingTest, TheHandStartsTheModeOpensTheWindowAndShowsTheTabWithoutAPopover) {
    AddingRig a({knobSpec("k", "Knob"), knobSpec("j", "Other")});
    EXPECT_FALSE(a.card->isPluginTouchToAdd());
    EXPECT_EQ(a.window(), nullptr);

    a.startByClickingTheHand();

    EXPECT_EQ(a.card->callOutBoxLaunches, 0) << "no popover over the plugin's controls";
    EXPECT_TRUE(a.card->isPluginTouchToAdd());
    EXPECT_TRUE(a.split().isRightLit()) << "the hand stays lit while the mode is on";
    ASSERT_NE(a.window(), nullptr) << "starting opened the plugin's own window";
    EXPECT_TRUE(a.window()->isAddingControls());
    EXPECT_TRUE(a.tabIsUp());
    EXPECT_EQ(a.frame()->getStripHeight(), synth::HostedPluginEditorFrame::kTabHeight);
    EXPECT_EQ(a.frame()->inner().getY(), a.frame()->getStripHeight())
        << "the strip sits above the plugin: nothing is covered";
}

TEST(AddByMovingTest, EachControlMovedInThePluginIsAddedToTheCardAndTheModeStaysOn) {
    AddingRig a({knobSpec("k", "Knob"), knobSpec("j", "Other"), knobSpec("m", "Third")});
    a.startByClickingTheHand();
    ASSERT_TRUE(a.card->isPluginTouchToAdd());

    auto* instance = a.node.module->getActiveInstanceForEditor();
    ASSERT_NE(instance, nullptr);
    auto& params = instance->getParameters();
    params[1]->beginChangeGesture(); // the user grabs "j" in the plugin's own window
    params[1]->endChangeGesture();
    pump();

    EXPECT_NE(childWithId<juce::Slider>(*a.card, "hostedKnob:j"), nullptr) << "the touched control is on the card";
    EXPECT_EQ(childWithId<juce::Slider>(*a.card, "hostedKnob:k"), nullptr);
    EXPECT_TRUE(a.card->isPluginTouchToAdd()) << "the mode stays on until it is turned off";

    params[2]->beginChangeGesture();
    params[2]->endChangeGesture();
    pump();
    EXPECT_NE(childWithId<juce::Slider>(*a.card, "hostedKnob:m"), nullptr) << "and the next one is added too";
    EXPECT_NE(childWithId<juce::Slider>(*a.card, "hostedKnob:j"), nullptr);
    EXPECT_TRUE(a.card->isPluginTouchToAdd());
}

TEST(AddByMovingTest, DoneOnTheTabEndsTheMode) {
    AddingRig a({knobSpec("k", "Knob")});
    a.startByClickingTheHand();
    ASSERT_TRUE(a.tabIsUp());
    auto& done = a.frame()->doneButton();
    EXPECT_EQ(done.getTitle(), "Done adding controls");
    EXPECT_TRUE(done.getTooltip().contains("Esc"));
    EXPECT_TRUE(done.getWantsKeyboardFocus());

    clickCentre(done);

    a.expectEnded("Done");
    EXPECT_NE(a.window(), nullptr) << "Done leaves the plugin window open";
}

TEST(AddByMovingTest, EscInThePluginWindowEndsTheModeInsteadOfClosingTheWindow) {
    AddingRig a({knobSpec("k", "Knob")});
    a.startByClickingTheHand();
    ASSERT_NE(a.window(), nullptr);

    EXPECT_TRUE(a.window()->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));

    a.expectEnded("Esc");
    EXPECT_NE(a.window(), nullptr) << "the first Esc only ends the mode";
    EXPECT_TRUE(a.window()->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
    EXPECT_EQ(a.window(), nullptr) << "with the mode off Esc closes the window as before";
}

TEST(AddByMovingTest, ClickingTheHandAgainEndsTheMode) {
    AddingRig a({knobSpec("k", "Knob")});
    a.startByClickingTheHand();
    ASSERT_TRUE(a.card->isPluginTouchToAdd());

    clickCentre(a.split().rightHalf());

    a.expectEnded("the hand again");
}

TEST(AddByMovingTest, ClosingThePluginWindowEndsTheMode) {
    AddingRig a({knobSpec("k", "Knob")});
    a.startByClickingTheHand();
    ASSERT_NE(a.window(), nullptr);

    a.window()->closeButtonPressed();

    EXPECT_EQ(a.window(), nullptr);
    a.expectEnded("closing the window");
}

TEST(AddByMovingTest, UnloadingThePluginEndsTheMode) {
    AddingRig a({knobSpec("k", "Knob")});
    a.startByClickingTheHand();

    a.node.module->unloadPlugin();
    pump();

    a.expectEnded("unload");
}

TEST(AddByMovingTest, TheModeCanBeStartedAgainAfterItEnded) {
    AddingRig a({knobSpec("k", "Knob"), knobSpec("j", "Other")});
    a.startByClickingTheHand();
    clickCentre(a.split().rightHalf());
    a.expectEnded("first run");

    a.startByClickingTheHand();

    EXPECT_TRUE(a.card->isPluginTouchToAdd());
    EXPECT_TRUE(a.split().isRightLit());
    EXPECT_TRUE(a.tabIsUp());
}
