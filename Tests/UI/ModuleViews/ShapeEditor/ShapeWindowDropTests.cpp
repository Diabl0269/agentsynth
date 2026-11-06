// ShapeWindowDropTests.cpp -- the shape editor window is not modal, so a shape dragged from the Sounds view can land
// on its morph map: over a dot it stamps that dot, over empty space it adds a shape there. Also every way the window
// closes (Esc, Done, its close button, the card going away, quit) deletes it with nothing left pointing at it.
#include "../../../TestWait.h"
#include "../../Layout/NativeWindowTestGuard.h"
#include "ShapeEditorTestHelpers.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Graph/ModuleComponent/MorphShapesCard/MorphShapesCard.h"
#include "UI/Layout/PopupMotion.h"
#include "UI/Library/SoundBrowser/ShapeDragPayload.h"
#include "UI/ModuleViews/ShapeEditor/ShapeEditorWindow.h"
#include "UI/ModuleViews/ShapeEditor/ShapeListEditor.h"

using namespace synth::ui;
using namespace synth::ui::test;
using synth::shapes::ShapeKind;
using synth::shapes::ShapeLibrary;

namespace {

juce::DragAndDropTarget::SourceDetails dragAt(const juce::String& name, juce::Point<float> local,
                                              ShapeKind kind = ShapeKind::Filter) {
    return {makeShapePayload({kind, true, name}), nullptr, local.roundToInt()};
}

synth::LfoCustomWave factoryCurve(const juce::String& name) {
    return ShapeLibrary::shared().find(ShapeKind::Filter, true, name)->curve;
}

/** The whole drag a real pointer makes over the map: enter, move to the point, drop. */
void dragAndDrop(ShapeWindowMap& map, const juce::String& name, juce::Point<float> local) {
    const auto details = dragAt(name, local);
    ASSERT_TRUE(map.isInterestedInDragSource(details));
    map.itemDragEnter(dragAt(name, local + juce::Point<float>(1.0f, 1.0f)));
    map.itemDragMove(details);
    map.itemDropped(details);
}

struct EditorRig {
    ShapeRig rig;
    ShapeListEditor editor{rig.adapter, 1};
    ShapeWindowMap& map() { return editor.getMap(); }
};

} // namespace

TEST(ShapeWindowDrop, HoveringADotSaysItWillBeStampedAndEmptySpaceSaysAShapeWillBeAdded) {
    EditorRig r;
    auto& map = r.map();
    map.itemDragEnter(dragAt("eh", map.getDotCentre(0)));
    map.itemDragMove(dragAt("eh", map.getDotCentre(0)));
    EXPECT_TRUE(map.isDropOver());
    EXPECT_EQ(map.getDropDot(), 0);
    EXPECT_EQ(map.getDropHint(), "Drop to stamp eh onto ah");

    map.itemDragMove(dragAt("eh", map.toPixels(0.9f, 0.9f)));
    EXPECT_EQ(map.getDropDot(), -1);
    EXPECT_EQ(map.getDropHint(), "Drop to add eh here");

    map.itemDragExit(dragAt("eh", map.toPixels(0.9f, 0.9f)));
    EXPECT_FALSE(map.isDropOver());
    EXPECT_TRUE(map.getDropHint().isEmpty());
}

// Regression test for FRO1198: the window's map took no drop at all
TEST(ShapeWindowDrop, ADropOnADotStampsItsCurveKeepsItsNameAndPlaceAndIsOneUndoStep) {
    EditorRig r;
    const auto before = r.rig.adapter->getShapes();
    const int serial = r.rig.undo.getEditSerial();

    dragAndDrop(r.map(), "eh", r.map().getDotCentre(0));

    const auto after = r.rig.adapter->getShapes();
    ASSERT_EQ(after.size(), before.size());
    EXPECT_EQ(after[0].curve, factoryCurve("eh"));
    EXPECT_EQ(after[0].name, "ah") << "the dot keeps its name";
    EXPECT_EQ(after[0].position, before[0].position) << "and its place";
    EXPECT_EQ(after[1], before[1]);
    EXPECT_EQ(r.editor.getSelected(), 0) << "the stamped shape is the one shown";
    EXPECT_FALSE(r.map().isDropOver());
    EXPECT_EQ(r.rig.undo.getEditSerial(), serial + 1);

    r.rig.undo.undo();
    EXPECT_EQ(r.rig.adapter->getShapes(), before) << "one undo puts the curve back";
}

TEST(ShapeWindowDrop, ADropOnEmptySpaceAddsAShapeThereWithTheCurveSelectedAsOneUndoStep) {
    EditorRig r;
    const auto before = r.rig.adapter->getShapes();
    const int serial = r.rig.undo.getEditSerial();

    dragAndDrop(r.map(), "ih", r.map().toPixels(0.9f, 0.9f));

    const auto after = r.rig.adapter->getShapes();
    ASSERT_EQ(after.size(), before.size() + 1);
    const auto& added = after.back();
    EXPECT_EQ(added.name, "ih");
    EXPECT_EQ(added.curve, factoryCurve("ih"));
    ASSERT_EQ(added.position.size(), 2u);
    EXPECT_NEAR(added.position[0], 0.9f, 0.01f);
    EXPECT_NEAR(added.position[1], 0.9f, 0.01f);
    EXPECT_EQ(r.editor.getSelected(), (int)after.size() - 1);
    EXPECT_EQ(r.map().getSelected(), (int)after.size() - 1);
    EXPECT_EQ(r.rig.undo.getEditSerial(), serial + 1);

    r.rig.undo.undo();
    EXPECT_EQ(r.rig.adapter->getShapes(), before) << "one undo removes the new shape";
}

TEST(ShapeWindowDrop, AtTheShapeCapADropOnEmptySpaceAddsNothingAndSaysSo) {
    EditorRig r;
    auto full = r.rig.filter->getCustomShapes();
    while ((int)full.shapes.size() < r.rig.adapter->getMaxShapes()) {
        auto copy = full.shapes[0];
        copy.name = "s" + juce::String((int)full.shapes.size());
        copy.position = {0.05f, 0.05f};
        full.shapes.push_back(copy);
    }
    r.rig.filter->setCustomShapes(full);
    r.editor.refresh();
    const int serial = r.rig.undo.getEditSerial();

    dragAndDrop(r.map(), "ih", r.map().toPixels(0.9f, 0.9f));

    EXPECT_EQ((int)r.rig.adapter->getShapes().size(), r.rig.adapter->getMaxShapes());
    EXPECT_EQ(r.editor.getStatusText(), "No room for another shape");
    EXPECT_EQ(r.rig.undo.getEditSerial(), serial);
}

TEST(ShapeWindowDrop, AnLfoShapeIsRefusedByTheFilterWindowsMap) {
    EditorRig r;
    EXPECT_FALSE(r.map().isInterestedInDragSource(dragAt("ramp up", r.map().getDotCentre(0), ShapeKind::Lfo)));
}

// ---------------------------------------------------------------------------------------------------------------------
// The real window
// ---------------------------------------------------------------------------------------------------------------------

namespace {

/** A main window to stand behind the shape window. */
struct MainStandIn : juce::DocumentWindow {
    MainStandIn()
        : juce::DocumentWindow("Main", juce::Colours::black, 0) {
        content.setSize(600, 400);
        setContentNonOwned(&content, true);
        setVisible(true);
    }
    juce::Component content;
};

class ShapeWindowLifecycle : public ::testing::Test {
protected:
    void SetUp() override {
        if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() == nullptr)
            GTEST_SKIP() << "needs a display";
        if (nativeWindowsAbortHere())
            GTEST_SKIP() << "native windows abort the process under the ASAN job's Xvfb";
        juce::MessageManager::getInstance();
        wasEnabled_ = PopupMotion::isEnabled();
        PopupMotion::setEnabled(false); // a close takes effect at once
    }
    void TearDown() override {
        deleteOpenModalDialogs();
        PopupMotion::setEnabled(wasEnabled_);
    }
    bool wasEnabled_ = true;
};

struct DialogsDeletedFirst {
    ~DialogsDeletedFirst() { deleteOpenModalDialogs(); }
};

} // namespace

// Regression test for FRO1198: the window was launched modal, which blocked every press on the main window
TEST_F(ShapeWindowLifecycle, TheWindowIsNotModalAndStaysAboveTheMainWindow) {
    ShapeRig rig;
    MainStandIn main;
    const DialogsDeletedFirst dialogsFirst;
    juce::Component::SafePointer<juce::DialogWindow> window(openShapeEditorWindow(rig.adapter, 0, &main.content));
    ASSERT_NE(window.getComponent(), nullptr);

    EXPECT_TRUE(window->isVisible());
    EXPECT_FALSE(window->isCurrentlyModal(false));
    EXPECT_EQ(juce::ModalComponentManager::getInstance()->getNumModalComponents(), 0);
    EXPECT_FALSE(main.content.isCurrentlyBlockedByAnotherModalComponent()) << "the main window still takes presses";

    main.toFront(true);
    EXPECT_TRUE(PopupMotion::isAboveForTest(*window, main)) << "a press on the main window does not bury it";
}

TEST_F(ShapeWindowLifecycle, EscDoneAndTheCloseButtonEachCloseItAndItDeletesItself) {
    ShapeRig rig;
    const DialogsDeletedFirst dialogsFirst;
    const std::vector<std::pair<const char*, std::function<void(juce::DialogWindow&)>>> closes{
        {"Esc",
         [](juce::DialogWindow& w) {
             EXPECT_TRUE(PopupMotion::offerKeyToDialogForTest(w, juce::KeyPress(juce::KeyPress::escapeKey)));
         }},
        {"Done",
         [](juce::DialogWindow& w) {
             auto* editor = dynamic_cast<ShapeListEditor*>(w.getContentComponent());
             ASSERT_NE(editor, nullptr);
             editor->getDoneButton().onClick();
         }},
        {"close button", [](juce::DialogWindow& w) { w.closeButtonPressed(); }},
    };
    for (const auto& [name, close] : closes) {
        juce::Component::SafePointer<juce::DialogWindow> window(openShapeEditorWindow(rig.adapter, 0, nullptr));
        ASSERT_NE(window.getComponent(), nullptr) << name;
        close(*window);
        ASSERT_NE(window.getComponent(), nullptr) << name << ": deleted a turn later, not under its own handler";
        EXPECT_FALSE(window->isVisible()) << name;
        EXPECT_TRUE(synth::test::waitUntil([&] { return window == nullptr; })) << name << " left the window alive";
    }
}

TEST_F(ShapeWindowLifecycle, QuitDeletesAWindowThatIsStillOpen) {
    ShapeRig rig;
    juce::Component::SafePointer<juce::DialogWindow> window(openShapeEditorWindow(rig.adapter, 0, nullptr));
    ASSERT_NE(window.getComponent(), nullptr);
    deleteOpenModalDialogs(); // what shutdown() runs before the main window goes
    EXPECT_EQ(window.getComponent(), nullptr);
}

namespace {
struct FilterCardRig {
    AudioEngine engine;
    FilterModule* filter = nullptr;
    juce::AudioProcessorGraph::Node::Ptr node;
    GraphEditor editor{engine};
    AppUndoManager undo;
    std::unique_ptr<ModuleComponent> card;

    FilterCardRig() {
        filter = new FilterModule();
        node = engine.getGraph().addNode(std::unique_ptr<juce::AudioProcessor>(filter));
        *dynamic_cast<juce::AudioParameterChoice*>(findParameterByID(filter, "filterType")) =
            FilterModule::kCustomTypeIndex;
        card = std::make_unique<ModuleComponent>(filter, node->nodeID, editor, &undo);
        card->timerCallback();
    }
};
} // namespace

TEST_F(ShapeWindowLifecycle, DeletingTheCardWhileItsWindowIsOpenDeletesTheWindow) {
    FilterCardRig rig;
    ASSERT_NE(rig.card->getMorphCard(), nullptr);
    juce::Component::SafePointer<juce::DialogWindow> window(rig.card->getMorphCard()->openEditor(0));
    ASSERT_NE(window.getComponent(), nullptr);

    rig.card.reset();
    EXPECT_EQ(window.getComponent(), nullptr) << "a window left open would edit a module that is gone";
    EXPECT_TRUE(synth::test::flushMessageQueue()) << "its pending focus call finds nothing";
}

TEST_F(ShapeWindowLifecycle, AWindowClosedButNotYetDeletedIsReplacedWhenTheCardOpensItAgainOrGoes) {
    FilterCardRig rig;
    auto& morph = *rig.card->getMorphCard();
    juce::Component::SafePointer<juce::DialogWindow> first(morph.openEditor(0));
    ASSERT_NE(first.getComponent(), nullptr);
    first->closeButtonPressed();
    ASSERT_NE(first.getComponent(), nullptr);

    juce::Component::SafePointer<juce::DialogWindow> second(morph.openEditor(1));
    EXPECT_EQ(first.getComponent(), nullptr);
    ASSERT_NE(second.getComponent(), nullptr);
    EXPECT_TRUE(second->isVisible());

    second->closeButtonPressed();
    rig.card.reset(); // before the window's own deletion turn
    EXPECT_EQ(second.getComponent(), nullptr);
    EXPECT_TRUE(synth::test::flushMessageQueue());
}
