// The shape library inside the shape window and on the canvas: Save shape, the Start from menu that lists the library,
// and a dragged library shape stamped onto a chip or a card in one undo step.
#include "../../Accessibility/TabOrderHelpers.h"
#include "../../Graph/CardBody/CardBodyTestHelpers.h"
#include "Modules/LFOModule.h"
#include "ShapeEditorTestHelpers.h"
#include "UI/Graph/ModuleComponent/MorphShapesCard/MorphShapesCard.h"
#include "UI/Library/SoundBrowser/ShapeDragPayload.h"
#include "UI/Library/SoundBrowser/SoundDragState.h"
#include "UI/ModuleViews/ShapeEditor/ShapeListEditor.h"
#include "UI/ModuleViews/ShapeEditor/ShapeSavePrompts.h"

using namespace synth::ui;
using namespace synth::ui::test;
using synth::LfoCustomWave;
using synth::shapes::ShapeKind;
using synth::shapes::ShapeLibrary;

namespace {

/** Points the shared library at a temp folder for one test and puts it back after. */
struct TempShapes {
    juce::File dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                         .getChildFile("shape-window-" + juce::Uuid().toDashedString());
    TempShapes() { ShapeLibrary::shared().setRootForTest(dir); }
    ~TempShapes() {
        ShapeLibrary::shared().setRootForTest({});
        test_hooks::shapeNamePromptHookForTest() = nullptr;
        test_hooks::shapeReplacePromptHookForTest() = nullptr;
        SoundDragState::end();
        dir.deleteRecursively();
    }
};

void click(juce::Button& button) { button.onClick(); }

LfoCustomWave rampWave() { return LfoCustomWave::preset(LfoCustomWave::Preset::RampUp); }

juce::DragAndDropTarget::SourceDetails dragOf(ShapeKind kind, bool factory, const juce::String& name) {
    return {makeShapePayload({kind, factory, name}), nullptr, {5, 5}};
}

} // namespace

TEST(ShapeLibraryWindow, SaveShapeIsANamedTooltippedTabStopThatWritesTheSelectedCurve) {
    TempShapes shapes;
    ShapeRig rig;
    ShapeListEditor editor(rig.adapter, 1);
    auto& save = editor.getSaveShapeButton();
    EXPECT_EQ(save.getTitle(), "Save shape");
    EXPECT_FALSE(save.getTooltip().isEmpty());
    EXPECT_TRUE(save.getWantsKeyboardFocus());
    EXPECT_TRUE(save.isVisible());
    EXPECT_GT(save.getWidth(), 0);
    // Nothing overlaps it in the button row.
    for (auto* other : {&editor.getDrawButton(), &editor.getDeleteButton()})
        EXPECT_FALSE(save.getBounds().intersects(other->getBounds())) << other->getTitle();

    juce::String suggested;
    test_hooks::shapeNamePromptHookForTest() = [&](const juce::String& s, const juce::String&,
                                                   std::function<void(juce::String)> done) {
        suggested = s;
        done("my ee");
    };
    click(save);
    EXPECT_EQ(suggested, "ee") << "the prompt starts from the selected shape's name";
    const auto saved = ShapeLibrary::shared().find(ShapeKind::Filter, false, "my ee");
    ASSERT_TRUE(saved.has_value());
    EXPECT_EQ(saved->curve, rig.filter->getCustomShapes().shapes[1].curve);
    EXPECT_TRUE(shapes.dir.getChildFile("Filter/my ee.json").existsAsFile());
}

TEST(ShapeLibraryWindow, AnEmptyNameIsRefusedAndAnExistingOneAsksBeforeReplacing) {
    TempShapes shapes;
    ShapeRig rig;
    ShapeListEditor editor(rig.adapter, 0);

    std::vector<juce::String> problems;
    int asked = 0;
    test_hooks::shapeNamePromptHookForTest() = [&](const juce::String&, const juce::String& problem,
                                                   std::function<void(juce::String)> done) {
        problems.push_back(problem);
        done(asked++ == 0 ? "   " : "again");
    };
    click(editor.getSaveShapeButton());
    ASSERT_EQ(problems.size(), 2u);
    EXPECT_TRUE(problems[0].isEmpty());
    EXPECT_FALSE(problems[1].isEmpty()) << "the second prompt says why it asks again";
    EXPECT_EQ(ShapeLibrary::shared().userShapes(ShapeKind::Filter).size(), 1u);

    // The same name again: Cancel keeps the old curve, Replace takes the new one.
    ASSERT_TRUE(ShapeLibrary::shared().save(ShapeKind::Filter, "again", rampWave(), true) ==
                ShapeLibrary::SaveResult::Replaced);
    test_hooks::shapeNamePromptHookForTest() = [&](const juce::String&, const juce::String&,
                                                   std::function<void(juce::String)> done) { done("again"); };
    bool replaceAsked = false;
    test_hooks::shapeReplacePromptHookForTest() = [&](const juce::String& name, std::function<void(bool)> done) {
        replaceAsked = true;
        EXPECT_EQ(name, "again");
        done(false);
    };
    click(editor.getSaveShapeButton());
    EXPECT_TRUE(replaceAsked);
    EXPECT_EQ(ShapeLibrary::shared().find(ShapeKind::Filter, false, "again")->curve, rampWave());
    test_hooks::shapeReplacePromptHookForTest() = [&](const juce::String&, std::function<void(bool)> done) {
        done(true);
    };
    click(editor.getSaveShapeButton());
    EXPECT_EQ(ShapeLibrary::shared().find(ShapeKind::Filter, false, "again")->curve,
              rig.filter->getCustomShapes().shapes[0].curve);
}

TEST(ShapeLibraryWindow, StartFromListsTheFactoryShapesAndTheUsersSavedShapeOfThatKind) {
    TempShapes shapes;
    ShapeRig rig;
    ASSERT_EQ(ShapeLibrary::shared().save(ShapeKind::Filter, "mine", rampWave()), ShapeLibrary::SaveResult::Saved);
    ASSERT_EQ(ShapeLibrary::shared().save(ShapeKind::Lfo, "lfo only", rampWave()), ShapeLibrary::SaveResult::Saved);
    ShapeListEditor editor(rig.adapter, 2);
    CapturedMenu captured;
    editor.showMenu = [&](juce::PopupMenu& m) { captured.capture(m); };
    click(editor.getStartFromButton());

    const auto has = [&](const char* name) {
        return std::find(captured.items.begin(), captured.items.end(), name) != captured.items.end();
    };
    for (const char* name : {"ah", "ee", "oo", "eh", "ih", "wolf", "telephone band", "comb", "tilt", "mine"})
        EXPECT_TRUE(has(name)) << name;
    EXPECT_FALSE(has("lfo only"));
    EXPECT_FALSE(has("ramp up"));

    const int serial = rig.undo.getEditSerial();
    ASSERT_TRUE(captured.run("mine"));
    EXPECT_EQ(rig.filter->getCustomShapes().shapes[2].curve, rampWave());
    EXPECT_EQ(rig.undo.getEditSerial(), serial + 1);
    ASSERT_TRUE(captured.run("telephone band"));
    EXPECT_EQ(rig.filter->getCustomShapes().shapes[2].curve, ShapeLibrary::factoryShapes(ShapeKind::Filter)[5].curve);
}

TEST(ShapeLibraryWindow, ADroppedFilterShapeChangesTheChipsCurveInOneUndoStepAndAnLfoShapeIsRefused) {
    TempShapes shapes;
    ShapeRig rig;
    ShapeListEditor editor(rig.adapter, 0);
    auto* chip = dynamic_cast<juce::DragAndDropTarget*>(editor.getChip(1));
    ASSERT_NE(chip, nullptr);
    const auto before = rig.filter->getCustomShapes();

    EXPECT_FALSE(chip->isInterestedInDragSource(dragOf(ShapeKind::Lfo, true, "ramp up")));
    EXPECT_FALSE(chip->isInterestedInDragSource({"Oscillator", nullptr, {1, 1}})) << "a module name is not a shape";

    const auto details = dragOf(ShapeKind::Filter, true, "telephone band");
    ASSERT_TRUE(chip->isInterestedInDragSource(details));
    const int serial = rig.undo.getEditSerial();
    chip->itemDropped(details);
    const auto phone = ShapeLibrary::factoryShapes(ShapeKind::Filter)[5].curve;
    EXPECT_EQ(rig.filter->getCustomShapes().shapes[1].curve, phone);
    EXPECT_EQ(rig.filter->getCustomShapes().shapes[0].curve, before.shapes[0].curve) << "only the chip dropped on";
    EXPECT_EQ(rig.filter->getCustomShapes().shapes[1].name, "ee");
    EXPECT_EQ(rig.undo.getEditSerial(), serial + 1);
    EXPECT_EQ(editor.getSelected(), 1);

    ASSERT_TRUE(rig.undo.undo());
    EXPECT_EQ(rig.filter->getCustomShapes(), before) << "Cmd+Z restores it";
}

TEST(ShapeLibraryWindow, ADroppedUserShapeStampsFromTheSavedFile) {
    TempShapes shapes;
    ShapeRig rig;
    ASSERT_EQ(ShapeLibrary::shared().save(ShapeKind::Filter, "mine", rampWave()), ShapeLibrary::SaveResult::Saved);
    ShapeListEditor editor(rig.adapter, 0);
    auto* chip = dynamic_cast<juce::DragAndDropTarget*>(editor.getChip(2));
    ASSERT_NE(chip, nullptr);
    chip->itemDropped(dragOf(ShapeKind::Filter, false, "mine"));
    EXPECT_EQ(rig.filter->getCustomShapes().shapes[2].curve, rampWave());
    // A shape deleted during the drag drops nothing.
    ShapeLibrary::shared().remove(ShapeKind::Filter, "mine");
    chip->itemDropped(dragOf(ShapeKind::Filter, false, "mine"));
    EXPECT_EQ(rig.filter->getCustomShapes().shapes[2].curve, rampWave());
}

namespace {

void setChoice(juce::AudioProcessor& proc, const char* id, int index) {
    auto* p = dynamic_cast<juce::AudioParameterChoice*>(findParameterByID(&proc, id));
    ASSERT_NE(p, nullptr);
    p->setValueNotifyingHost(p->convertTo0to1((float)index));
}

} // namespace

TEST(ShapeLibraryWindow, ACardTakesOnlyAKindMatchingShapeWhileItsCurveIsCustom) {
    TempShapes shapes;
    cardbody_test::CardCanvas canvas;
    const auto lfoId = canvas.add(std::make_unique<LFOModule>(), 0, 0);
    const auto filterId = canvas.add(std::make_unique<FilterModule>(), 400, 0);
    canvas.editor.updateComponents();
    auto* lfoCard = canvas.card(lfoId);
    auto* filterCard = canvas.card(filterId);
    ASSERT_NE(lfoCard, nullptr);
    ASSERT_NE(filterCard, nullptr);
    const auto filterDrag = dragOf(ShapeKind::Filter, true, "tilt");
    const auto lfoDrag = dragOf(ShapeKind::Lfo, true, "bounce");

    // Not on the Custom type or shape: neither card takes anything.
    EXPECT_FALSE(lfoCard->isInterestedInDragSource(lfoDrag));
    EXPECT_FALSE(filterCard->isInterestedInDragSource(filterDrag));

    setChoice(*canvas.processor(lfoId), "shape", LFOModule::kCustomShapeIndex);
    setChoice(*canvas.processor(filterId), "filterType", FilterModule::kCustomTypeIndex);
    EXPECT_TRUE(lfoCard->isInterestedInDragSource(lfoDrag));
    EXPECT_FALSE(lfoCard->isInterestedInDragSource(filterDrag)) << "a filter shape does not stamp an LFO";
    EXPECT_TRUE(filterCard->isInterestedInDragSource(filterDrag));
    EXPECT_FALSE(filterCard->isInterestedInDragSource(lfoDrag)) << "an LFO shape does not stamp a Filter";

    // The outline shows on the cards that would take the shape, and only while one is dragged.
    SoundDragState::beginShape(ShapeKind::Filter, "tilt");
    EXPECT_TRUE(filterCard->isShowingSoundOutline());
    EXPECT_FALSE(lfoCard->isShowingSoundOutline());
    SoundDragState::end();
    EXPECT_FALSE(filterCard->isShowingSoundOutline());
}

TEST(ShapeLibraryWindow, ADroppedShapeReplacesTheCardsCurveInOneUndoStep) {
    TempShapes shapes;
    cardbody_test::CardCanvas canvas;
    const auto filterId = canvas.add(std::make_unique<FilterModule>(), 0, 0);
    canvas.editor.updateComponents();
    setChoice(*canvas.processor(filterId), "filterType", FilterModule::kCustomTypeIndex);
    auto* card = canvas.card(filterId);
    ASSERT_NE(card, nullptr);
    const auto readShapes = [&] { return dynamic_cast<FilterModule*>(canvas.processor(filterId))->getCustomShapes(); };
    const auto before = readShapes();
    const auto drag = dragOf(ShapeKind::Filter, true, "comb");
    card->itemDragEnter(drag);
    ASSERT_NE(card->getMorphCard(), nullptr);
    EXPECT_TRUE(card->getMorphCard()->isShapeDropOver());

    const int serial = canvas.undo.getEditSerial();
    card->itemDropped(drag);
    EXPECT_FALSE(card->getMorphCard()->isShapeDropOver());
    const auto after = readShapes();
    const int stamped = synth::morph::nearestShapeIndex(after.shapes, 0.5f, 0.5f);
    EXPECT_EQ(after.shapes[(size_t)stamped].curve, ShapeLibrary::factoryShapes(ShapeKind::Filter)[6].curve);
    EXPECT_EQ(after.shapes[(size_t)stamped].name, before.shapes[(size_t)stamped].name);
    EXPECT_EQ(canvas.undo.getEditSerial(), serial + 1);

    ASSERT_TRUE(canvas.undo.undo());
    EXPECT_EQ(readShapes(), before);
}

TEST(ShapeLibraryWindow, TheCanvasRefusesAShapeSoNoModuleIsMadeFromItsPayload) {
    cardbody_test::CardCanvas canvas;
    EXPECT_FALSE(canvas.editor.isInterestedInDragSource(dragOf(ShapeKind::Filter, true, "tilt")));
}
