// The shape library inside the shape window and on the canvas: Save shape, the Start from menu that lists the library,
// and a dragged library shape stamped onto a chip or a card in one undo step.
#include "../../../SoundBrowser/SoundBrowserTestKit.h"
#include "../../../TestWait.h"
#include "../../Accessibility/TabOrderHelpers.h"
#include "../../Graph/CardBody/CardCanvas.h"
#include "Modules/LFOModule.h"
#include "ShapeEditorTestHelpers.h"
#include "UI/Chrome/Badge/Badge.h"
#include "UI/Graph/ModuleComponent/MorphShapesCard/MorphShapesCard.h"
#include "UI/Library/SoundBrowser/ShapeDragPayload.h"
#include "UI/Library/SoundBrowser/ShapeStamp.h"
#include "UI/Library/SoundBrowser/SoundBrowserComponent.h"
#include "UI/Library/SoundBrowser/SoundDragState.h"
#include "UI/ModuleViews/ShapeEditor/ShapeListEditor.h"
#include "UI/ModuleViews/ShapeEditor/ShapePicker.h"
#include "UI/ModuleViews/ShapeEditor/ShapeSavePrompts.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Theme/BuiltInThemes.h"
#include <cmath>

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
    for (const char* name :
         {"ah, Factory", "ee, Factory", "oo, Factory", "eh, Factory", "ih, Factory", "wolf, Factory",
          "telephone band, Factory", "comb, Factory", "tilt, Factory", "double peak, Factory", "mine, Yours"})
        EXPECT_TRUE(has(name)) << name;
    EXPECT_FALSE(has("lfo only, Yours"));
    EXPECT_FALSE(has("ramp up, Factory"));

    const int serial = rig.undo.getEditSerial();
    ASSERT_TRUE(captured.run("mine, Yours"));
    EXPECT_EQ(rig.filter->getCustomShapes().shapes[2].curve, rampWave());
    EXPECT_EQ(rig.undo.getEditSerial(), serial + 1);
    ASSERT_TRUE(captured.run("telephone band, Factory"));
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

// Regression test for FRO1118: Return on a shape row had no keyboard path to a card; it stamps the selected card.
TEST(ShapeLibraryWindow, ReturnStampsTheSelectedFilterCardInOneUndoStepAndDoesNothingWithoutATarget) {
    TempShapes shapes;
    cardbody_test::CardCanvas canvas;
    const auto filterId = canvas.add(std::make_unique<FilterModule>(), 0, 0);
    const auto lfoId = canvas.add(std::make_unique<LFOModule>(), 400, 0);
    canvas.editor.updateComponents();
    setChoice(*canvas.processor(filterId), "filterType", FilterModule::kCustomTypeIndex);
    const auto shape = *ShapeLibrary::shared().find(ShapeKind::Filter, true, "comb");
    const auto readShapes = [&] { return dynamic_cast<FilterModule*>(canvas.processor(filterId))->getCustomShapes(); };
    const auto before = readShapes();
    const int serial = canvas.undo.getEditSerial();

    canvas.editor.clearSelection();
    EXPECT_TRUE(stampShapeOnSelectedCard(canvas.editor, shape).isEmpty()) << "no card selected";
    canvas.editor.setSelectedNodes({lfoId});
    EXPECT_TRUE(stampShapeOnSelectedCard(canvas.editor, shape).isEmpty()) << "an LFO does not take a filter shape";
    EXPECT_EQ(readShapes(), before);
    EXPECT_EQ(canvas.undo.getEditSerial(), serial);

    canvas.editor.setSelectedNodes({filterId});
    EXPECT_EQ(stampShapeOnSelectedCard(canvas.editor, shape), "Filter");
    EXPECT_EQ(canvas.undo.getEditSerial(), serial + 1);
    EXPECT_NE(readShapes(), before);
    ASSERT_TRUE(canvas.undo.undo());
    EXPECT_EQ(readShapes(), before);

    setChoice(*canvas.processor(filterId), "filterType", 0);
    EXPECT_TRUE(stampShapeOnSelectedCard(canvas.editor, shape).isEmpty()) << "the filter is not on Custom";
}

using namespace synth::ui;
using namespace synth::ui::test;
using synth::shapes::ShapeKind;
using synth::shapes::ShapeLibrary;

namespace {

/** How many pixels of `area` sit close to `target`. */
int pixelsNear(const juce::Image& image, juce::Rectangle<int> area, juce::Colour target, int tolerance) {
    int count = 0;
    for (int y = area.getY(); y < area.getBottom(); ++y)
        for (int x = area.getX(); x < area.getRight(); ++x) {
            const auto p = image.getPixelAt(x, y);
            if (std::abs(p.getRed() - target.getRed()) <= tolerance &&
                std::abs(p.getGreen() - target.getGreen()) <= tolerance &&
                std::abs(p.getBlue() - target.getBlue()) <= tolerance)
                ++count;
        }
    return count;
}

} // namespace

TEST(Badge, EachKindHasItsOwnTextAndWidthAndTheComponentIsNamedByIt) {
    EXPECT_STREQ(badgeText(BadgeKind::Yours), "Yours");
    EXPECT_STREQ(badgeText(BadgeKind::Factory), "Factory");
    EXPECT_STREQ(badgeText(BadgeKind::Pack), "Pack");
    EXPECT_STREQ(badgeText(BadgeKind::Marketplace), "Marketplace");
    EXPECT_GT(badgeWidth(BadgeKind::Marketplace), badgeWidth(BadgeKind::Pack));
    Badge badge(BadgeKind::Yours);
    EXPECT_EQ(badge.getTitle(), "Yours");
    badge.setKind(BadgeKind::Factory);
    EXPECT_EQ(badge.getTitle(), "Factory") << "a screen reader hears the kind it shows";
    EXPECT_EQ(badge.getIdealBounds().getHeight(), kBadgeHeight);
}

TEST(Badge, TheKindsAreToldApartByColourInEveryTheme) {
    for (const auto& theme : synth::theme::builtInThemes()) {
        const auto yours = badgeColours(theme, BadgeKind::Yours);
        const auto factory = badgeColours(theme, BadgeKind::Factory);
        EXPECT_NE(yours.text, factory.text) << theme.id;
        EXPECT_NE(yours.border, factory.border) << theme.id;
    }
}

TEST(ShapePicker, ListsTheSavedShapesFirstBadgedYoursThenTheFactorySetBadgedFactory) {
    TempShapes shapes;
    ASSERT_EQ(ShapeLibrary::shared().save(ShapeKind::Filter, "mine", rampWave()), ShapeLibrary::SaveResult::Saved);
    const auto entries = libraryPickerEntries(ShapeLibrary::shared(), ShapeKind::Filter);
    ASSERT_GE(entries.size(), 2u);
    EXPECT_EQ(entries.front().name, "mine");
    EXPECT_EQ(entries.front().badge, BadgeKind::Yours);
    EXPECT_EQ(pickerEntryTitle(entries.front()), "mine, Yours");
    EXPECT_EQ(entries.back().badge, BadgeKind::Factory);
    EXPECT_EQ(entries.size(), 1u + ShapeLibrary::factoryShapes(ShapeKind::Filter).size());
}

TEST(ShapePicker, TheShapeWindowsStartFromRowsAreThumbnailRowsWithTheirBadgeInTheirName) {
    TempShapes shapes;
    ShapeRig rig;
    ASSERT_EQ(ShapeLibrary::shared().save(ShapeKind::Filter, "mine", rampWave()), ShapeLibrary::SaveResult::Saved);
    ShapeListEditor editor(rig.adapter, 2);
    CapturedMenu captured;
    editor.showMenu = [&](juce::PopupMenu& m) { captured.capture(m); };
    editor.getStartFromButton().onClick();

    bool sawMine = false;
    bool sawFactory = false;
    juce::PopupMenu::MenuItemIterator it(captured.menu, false);
    while (it.next()) {
        const auto& item = it.getItem();
        if (item.isSeparator || item.isSectionHeader)
            continue;
        EXPECT_NE(item.customComponent, nullptr) << item.text;
        sawMine = sawMine || item.text == "mine, Yours";
        sawFactory = sawFactory || item.text == "double peak, Factory";
    }
    EXPECT_TRUE(sawMine);
    EXPECT_TRUE(sawFactory);
}

// Regression test for FRO1172: after Save shape the Sounds view's new row has to read as Yours the moment it appears.
TEST(ShapePicker, AShapeSavedFromTheWindowShowsAsYoursInTheOpenShapesListAtOnce) {
    TempShapes shapes;
    sound_browser_test::TempTree tree;
    tree.fill();
    auto index = sound_browser_test::makeIndex(tree);
    sound_browser_test::scanAndWait(*index);
    synth::soundbrowser::SoundLists lists;
    SoundBrowserComponent view(*index, lists);
    view.setSize(420, 900);
    synth::theme::AppLookAndFeel laf;
    view.setLookAndFeel(&laf);
    view.setShapeLibrary(&ShapeLibrary::shared());
    view.showLocation(SoundBrowserComponent::kShapesId, {});
    view.completeListAnimationForTest();

    ShapeRig rig;
    ShapeListEditor editor(rig.adapter, 1);
    test_hooks::shapeNamePromptHookForTest() = [](const juce::String&, const juce::String&,
                                                  std::function<void(juce::String)> done) { done("fresh"); };
    editor.getSaveShapeButton().onClick();

    int row = -1;
    for (size_t i = 0; i < view.getRows().size(); ++i)
        if (view.getRows()[i].text == "fresh")
            row = (int)i;
    ASSERT_GE(row, 0) << "the saved shape is listed at once";
    ASSERT_TRUE(view.getRows()[(size_t)row].shape.has_value());
    EXPECT_FALSE(view.getRows()[(size_t)row].shape->factory) << "and it is the user's";

    // The row paints the Yours badge, not the Factory one (the badge sits at the right edge of the row).
    view.completeListAnimationForTest();
    const auto image = view.createComponentSnapshot(view.getLocalBounds());
    auto bounds = view.getRowBounds(row);
    const auto badgeArea = bounds.removeFromRight(100);
    const auto& theme = synth::theme::themeOf(view);
    const auto yoursBorder = theme.colors.bg0.overlaidWith(badgeColours(theme, BadgeKind::Yours).border);
    EXPECT_GT(pixelsNear(image, badgeArea, yoursBorder, 30), 10) << "the Yours badge is drawn on the new row";
    view.setLookAndFeel(nullptr);
}

TEST(ShapePicker, EveryRowOfThePickerMenuPaintsItsThumbnailAndItsBadgeAndTheSheetIsWrittenToLookAt) {
    TempShapes shapes;
    ASSERT_EQ(ShapeLibrary::shared().save(ShapeKind::Filter, "mine", rampWave()), ShapeLibrary::SaveResult::Saved);
    synth::theme::AppLookAndFeel laf;
    auto menu = makeShapePickerMenu(libraryPickerEntries(ShapeLibrary::shared(), ShapeKind::Filter), [](auto&) {});
    std::vector<juce::PopupMenu::CustomComponent*> rows;
    juce::PopupMenu::MenuItemIterator it(menu, false);
    while (it.next())
        if (auto* c = it.getItem().customComponent.get())
            rows.push_back(c);
    ASSERT_GE(rows.size(), 17u);

    const int rowH = 30;
    const int rowW = 260;
    juce::Image sheet(juce::Image::ARGB, rowW * 2, rowH * 9, true, juce::SoftwareImageType());
    juce::Graphics g(sheet);
    g.fillAll(juce::Colour(0xff161a22));
    for (size_t i = 0; i < rows.size(); ++i) {
        int w = 0;
        int h = 0;
        rows[i]->getIdealSize(w, h);
        rows[i]->setBounds(0, 0, rowW, h);
        juce::Image one(juce::Image::ARGB, rowW, h, true, juce::SoftwareImageType());
        juce::Graphics og(one);
        og.fillAll(juce::Colour(0xff161a22));
        rows[i]->paintEntireComponent(og, false);
        const auto col = (int)(i / 9);
        g.drawImageAt(one, col * rowW, (int)(i % 9) * rowH);
        // The thumbnail box (left) holds drawn curve pixels and the badge (right) holds its pill.
        int lit = 0;
        for (int y = 0; y < h; ++y)
            for (int x = 4; x < 60; ++x)
                lit += one.getPixelAt(x, y).getBrightness() > 0.5f ? 1 : 0;
        EXPECT_GT(lit, 8) << "row " << i << " has a curve drawn in its thumbnail";
        int badge = 0;
        for (int y = 0; y < h; ++y)
            for (int x = rowW - 70; x < rowW; ++x)
                badge += one.getPixelAt(x, y) != juce::Colour(0xff161a22) ? 1 : 0;
        EXPECT_GT(badge, 40) << "row " << i << " has its badge pill";
    }
    if (const auto dir = juce::SystemStats::getEnvironmentVariable("AGENTSYNTH_EVIDENCE_DIR", {}); dir.isNotEmpty()) {
        const auto file = juce::File(dir).getChildFile("shape-picker-rows.png");
        file.deleteFile();
        juce::FileOutputStream out(file);
        if (out.openedOk())
            juce::PNGImageFormat().writeImageToStream(sheet, out);
    }
}

// Regression test for FRO1172: the Start from menu worked out "is this row a saved shape" from a variable that
// shadowed the saved-shape count, so the origin it passed on was stale. A saved shape picked there is noted as Yours.
TEST(ShapePicker, AShapePickedFromStartFromIsNotedAsYoursAndAFactoryOneAsFactory) {
    struct Heard : ShapeLibrary::Listener {
        std::vector<synth::shapes::LibraryShape> used;
        void shapeLibraryChanged() override {}
        void shapeLibraryUsed(const synth::shapes::LibraryShape& s) override { used.push_back(s); }
    } heard;
    TempShapes shapes;
    ShapeRig rig;
    ASSERT_EQ(ShapeLibrary::shared().save(ShapeKind::Filter, "mine", rampWave()), ShapeLibrary::SaveResult::Saved);
    ShapeLibrary::shared().addListener(&heard);
    ShapeListEditor editor(rig.adapter, 2);
    CapturedMenu captured;
    editor.showMenu = [&](juce::PopupMenu& m) { captured.capture(m); };
    editor.getStartFromButton().onClick();
    ASSERT_TRUE(captured.run("mine, Yours"));
    ASSERT_TRUE(captured.run("double peak, Factory"));
    ShapeLibrary::shared().removeListener(&heard);
    ASSERT_EQ(heard.used.size(), 2u);
    EXPECT_EQ(heard.used[0].name, "mine");
    EXPECT_FALSE(heard.used[0].factory);
    EXPECT_EQ(heard.used[1].name, "double peak");
    EXPECT_TRUE(heard.used[1].factory);
}
