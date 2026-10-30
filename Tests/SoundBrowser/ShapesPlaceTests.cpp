// The Shapes place of the Sounds view: both groups, user shapes first, thumbnails that paint, the spoken row and the
// drag that starts from a row.
#include "../TestWait.h"
#include "SoundBrowserTestKit.h"
#include "UI/Library/SoundBrowser/ShapeDragPayload.h"
#include "UI/Library/SoundBrowser/SoundBrowserComponent.h"
#include "UI/Library/SoundBrowser/SoundBrowserText.h"
#include "UI/Library/SoundBrowser/SoundDragState.h"
#include "UI/ModuleViews/ShapeEditor/ShapeSavePrompts.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

using namespace sound_browser_test;
using synth::shapes::ShapeKind;
using synth::shapes::ShapeLibrary;
using synth::ui::SoundBrowserComponent;

namespace {

struct ShapesFixture {
    TempTree tree;
    juce::File shapeDir = tree.dir.getChildFile("shapes");
    ShapeLibrary library{shapeDir};
    synth::soundbrowser::SoundLists lists;
    std::unique_ptr<synth::soundbrowser::SoundIndex> index;
    std::unique_ptr<SoundBrowserComponent> view;

    ShapesFixture() {
        tree.fill();
        index = makeIndex(tree);
        scanAndWait(*index);
        synth::LfoCustomWave wave;
        wave.points = {{0.0f, 0.1f, 0.0f}, {0.5f, 0.9f, 0.0f}, {1.0f, 0.1f, 0.0f}};
        library.save(ShapeKind::Filter, "zz mine", wave);
        library.save(ShapeKind::Lfo, "my wobble", wave);
        view = std::make_unique<SoundBrowserComponent>(*index, lists);
        view->setSize(300, 900);
        view->setShapeLibrary(&library);
    }
    ~ShapesFixture() { synth::ui::SoundDragState::end(); }
    void openShapes() {
        view->showLocation(SoundBrowserComponent::kShapesId, {});
        view->completeListAnimationForTest();
    }
    int rowOf(const juce::String& text) const {
        for (size_t i = 0; i < view->getRows().size(); ++i)
            if (view->getRows()[i].text == text)
                return (int)i;
        return -1;
    }
};

} // namespace

TEST(ShapesPlaceTest, ThePlacesListHasAShapesPlaceNextToFavouritesAndUsedRecentlyOnlyWithALibrary) {
    ShapesFixture f;
    ASSERT_GE(f.view->getRows().size(), 3u);
    EXPECT_EQ(f.view->getRows()[0].text, "Favourites");
    EXPECT_EQ(f.view->getRows()[1].text, "Used recently");
    EXPECT_EQ(f.view->getRows()[2].text, "Shapes");
    EXPECT_EQ(f.view->getRows()[2].detail, "18") << "16 factory shapes and the 2 saved ones";

    f.view->setShapeLibrary(nullptr);
    EXPECT_EQ(f.rowOf("Shapes"), -1);
}

TEST(ShapesPlaceTest, ItListsFilterShapesThenLfoShapesWithTheUsersFirstInEach) {
    ShapesFixture f;
    f.openShapes();
    std::vector<juce::String> heads, filterNames, lfoNames;
    bool inLfo = false;
    for (const auto& row : f.view->getRows()) {
        if (row.kind == SoundBrowserComponent::RowKind::Hint) {
            heads.push_back(row.text);
            inLfo = row.text == "LFO shapes";
        } else if (row.kind == SoundBrowserComponent::RowKind::Shape) {
            (inLfo ? lfoNames : filterNames).push_back(row.text);
            EXPECT_EQ(row.shape->kind, inLfo ? ShapeKind::Lfo : ShapeKind::Filter);
        }
    }
    ASSERT_EQ(heads.size(), 2u);
    EXPECT_EQ(heads[0], "Filter shapes");
    EXPECT_EQ(heads[1], "LFO shapes");
    ASSERT_EQ(filterNames.size(), 9u);
    ASSERT_EQ(lfoNames.size(), 9u);
    EXPECT_EQ(filterNames[0], "zz mine") << "a saved shape comes before the factory set, whatever its name";
    EXPECT_EQ(filterNames[1], "ah");
    EXPECT_EQ(filterNames.back(), "tilt");
    EXPECT_EQ(lfoNames[0], "my wobble");
    EXPECT_EQ(lfoNames[1], "ramp up");
    EXPECT_EQ(f.view->getPathText(), "Places > Shapes");
}

TEST(ShapesPlaceTest, EveryShapeRowPaintsAThumbnailOfItsCurve) {
    ShapesFixture f;
    f.openShapes();
    synth::theme::AppLookAndFeel laf;
    f.view->setLookAndFeel(&laf);
    const int row = f.rowOf("zz mine");
    ASSERT_GE(row, 0);
    const auto image = f.view->createComponentSnapshot(f.view->getLocalBounds());
    const auto bounds = f.view->getRowBounds(row);
    const auto& colors = synth::theme::themeOf(*f.view).colors;
    // Thumbnail box: the accent-coloured line is inside it, so some pixel there is near the accent.
    int accentPixels = 0;
    for (int y = bounds.getY(); y < bounds.getBottom(); ++y)
        for (int x = bounds.getX() + 12; x < bounds.getX() + 12 + 44; ++x) {
            const auto p = image.getPixelAt(x, y);
            if (std::abs(p.getRed() - colors.accent.getRed()) < 40 &&
                std::abs(p.getGreen() - colors.accent.getGreen()) < 40 &&
                std::abs(p.getBlue() - colors.accent.getBlue()) < 40)
                ++accentPixels;
        }
    EXPECT_GT(accentPixels, 10);
    f.view->setLookAndFeel(nullptr);
}

TEST(ShapesPlaceTest, ASavedShapeAppearsInTheOpenListAtOnce) {
    ShapesFixture f;
    f.openShapes();
    EXPECT_EQ(f.rowOf("fresh"), -1);
    f.library.save(ShapeKind::Lfo, "fresh", synth::LfoCustomWave::preset(synth::LfoCustomWave::Preset::RampUp));
    EXPECT_EQ(f.rowOf("fresh") + 1, f.rowOf("my wobble")) << "the user's LFO shapes are listed in name order";
}

TEST(ShapesPlaceTest, TheSpokenRowSaysWhichKindAndWhereItCameFrom) {
    EXPECT_EQ(synth::ui::describeShapeForAccessibility("ah", "filter", true, 3, 20),
              "ah, filter shape, built in, 3 of 20");
    EXPECT_EQ(synth::ui::describeShapeForAccessibility("mine", "lfo", false, 1, 9),
              "mine, lfo shape, saved by you, 1 of 9");
    ShapesFixture f;
    f.openShapes();
    f.view->selectRow(f.rowOf("ah"));
    EXPECT_NE(f.view->getAccessibilityValueText().indexOf("ah, filter shape, built in"), -1);
}

TEST(ShapesPlaceTest, DraggingAShapeRowStartsAShapeDragAndEndsItOnRelease) {
    ShapesFixture f;
    f.openShapes();
    const int row = f.rowOf("ramp up");
    ASSERT_GE(row, 0);
    EXPECT_TRUE(f.view->beginShapeDrag(row));
    EXPECT_TRUE(synth::ui::SoundDragState::isShapeActive());
    EXPECT_FALSE(synth::ui::SoundDragState::isActive()) << "a shape is not a sound";
    EXPECT_EQ(synth::ui::SoundDragState::shapeKind(), ShapeKind::Lfo);
    EXPECT_EQ(synth::ui::SoundDragState::shapeName(), "ramp up");
    EXPECT_FALSE(f.view->beginShapeDrag(f.rowOf("LFO shapes"))) << "a heading does not drag";
    f.view->mouseUp(juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), {}, {}, 0, 0, 0, 0, 0,
                                     f.view.get(), f.view.get(), juce::Time::getCurrentTime(), {},
                                     juce::Time::getCurrentTime(), 1, false));
    EXPECT_FALSE(synth::ui::SoundDragState::isShapeActive());
}

TEST(ShapesPlaceTest, AShapePayloadRoundTripsAndOtherPayloadsAreNotShapes) {
    using namespace synth::ui;
    const ShapeRef ref{ShapeKind::Lfo, false, "name: with colons"};
    const auto parsed = shapeFromPayload(makeShapePayload(ref));
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->kind, ShapeKind::Lfo);
    EXPECT_FALSE(parsed->factory);
    EXPECT_EQ(parsed->name, ref.name);
    EXPECT_FALSE(shapeFromPayload("shape:nonsense").has_value());
    EXPECT_FALSE(shapeFromPayload("shape:filter:elsewhere:x").has_value());
    EXPECT_FALSE(isShapePayload("sound:/tmp/a.wav"));
    EXPECT_FALSE(isShapePayload("Oscillator"));
}

namespace {

void pump() { juce::MessageManager::getInstance()->runDispatchLoopUntil(60); }

// Plays the part of the Delete confirmation: remembers what was asked, answers on demand.
struct DeletePrompt {
    juce::StringArray asked;
    std::function<void(bool)> answer;
    DeletePrompt() {
        synth::ui::test_hooks::shapeDeletePromptHookForTest() = [this](const juce::String& name,
                                                                       std::function<void(bool)> done) {
            asked.add(name);
            answer = std::move(done);
        };
    }
    ~DeletePrompt() { synth::ui::test_hooks::shapeDeletePromptHookForTest() = nullptr; }
};

juce::KeyPress keyOf(int code) { return juce::KeyPress(code); }

} // namespace

// Regression test for FRO1118: a shape row showed the sound footer (0 sounds, empty waveform, Stop, Auto-play, volume).
TEST(ShapesPlaceTest, TheFooterOfAShapeRowSaysKindAndSourceAndHidesTheTransportButASoundKeepsIt) {
    ShapesFixture f;
    f.openShapes();
    auto& footer = f.view->getPreviewFooter();
    f.view->selectRow(f.rowOf("zz mine"));
    footer.finishShapeMotionForTest();
    EXPECT_EQ(f.view->getShapeFooterText(), "Filter shape, yours");
    EXPECT_TRUE(footer.isShowingShape());
    EXPECT_TRUE(footer.getShapeCurve().isVisible());
    EXPECT_FALSE(footer.getStopButton().isVisible());
    EXPECT_FALSE(footer.getAutoPlay().isVisible());
    EXPECT_FALSE(footer.getVolume().isVisible());
    EXPECT_FALSE(footer.getWaveform().isVisible());

    f.view->selectRow(f.rowOf("ramp up"));
    EXPECT_EQ(f.view->getShapeFooterText(), "LFO shape, factory");

    f.view->showLocation("core", "Drums");
    f.view->completeListAnimationForTest();
    int sound = -1;
    for (size_t i = 0; i < f.view->getRows().size(); ++i)
        if (f.view->getRows()[i].kind == SoundBrowserComponent::RowKind::Sound)
            sound = (int)i;
    ASSERT_GE(sound, 0);
    f.view->selectRow(sound);
    footer.finishShapeMotionForTest();
    EXPECT_FALSE(footer.isShowingShape());
    EXPECT_TRUE(footer.getStopButton().isVisible());
    EXPECT_TRUE(footer.getAutoPlay().isVisible());
    EXPECT_TRUE(footer.getVolume().isVisible());
    EXPECT_FALSE(footer.getShapeCurve().isVisible());
}

TEST(ShapesPlaceTest, DeletingASavedShapeAsksFirstThenRemovesTheFileAndTheRow) {
    ShapesFixture f;
    DeletePrompt prompt;
    f.openShapes();
    const int row = f.rowOf("zz mine");
    f.view->selectRow(row);
    const auto file = f.library.fileOf(ShapeKind::Filter, "zz mine");
    ASSERT_TRUE(file.existsAsFile());

    EXPECT_TRUE(f.view->keyPressed(keyOf(juce::KeyPress::deleteKey)));
    ASSERT_EQ(prompt.asked.size(), 1);
    EXPECT_EQ(prompt.asked[0], "zz mine");
    EXPECT_TRUE(file.existsAsFile()) << "nothing goes before the answer";
    prompt.answer(false);
    EXPECT_TRUE(file.existsAsFile());
    EXPECT_GE(f.rowOf("zz mine"), 0);

    EXPECT_TRUE(f.view->keyPressed(keyOf(juce::KeyPress::backspaceKey)));
    ASSERT_EQ(prompt.asked.size(), 2);
    prompt.answer(true);
    EXPECT_FALSE(file.existsAsFile());
    EXPECT_EQ(f.rowOf("zz mine"), -1);
    f.view->completeListAnimationForTest();
    EXPECT_GE(f.view->getSelectedIndex(), 0) << "the selection lands on a neighbour";
}

TEST(ShapesPlaceTest, FactoryShapesOfferNoMenuAndNoDelete) {
    ShapesFixture f;
    DeletePrompt prompt;
    f.openShapes();
    const int factory = f.rowOf("ah");
    EXPECT_EQ(f.view->buildShapeMenu(factory).getNumItems(), 0);
    f.view->selectRow(factory);
    EXPECT_FALSE(f.view->keyPressed(keyOf(juce::KeyPress::deleteKey)));
    EXPECT_FALSE(f.view->applyShapeAction(factory, SoundBrowserComponent::ShapeAction::Delete));
    EXPECT_TRUE(prompt.asked.isEmpty());

    juce::PopupMenu menu = f.view->buildShapeMenu(f.rowOf("zz mine"));
    juce::StringArray labels;
    for (juce::PopupMenu::MenuItemIterator it(menu); it.next();)
        if (it.getItem().itemID != 0)
            labels.add(it.getItem().text);
    EXPECT_EQ(labels.joinIntoString("|"), "Rename|Show in " + synth::ui::fileManagerName() + "|Delete");
}

TEST(ShapesPlaceTest, ShowInFinderRevealsTheShapesFile) {
    ShapesFixture f;
    f.openShapes();
    juce::File revealed;
    f.view->setRevealForTest([&](const juce::File& file) { revealed = file; });
    EXPECT_TRUE(f.view->applyShapeAction(f.rowOf("zz mine"), SoundBrowserComponent::ShapeAction::ShowInFinder));
    EXPECT_EQ(revealed, f.library.fileOf(ShapeKind::Filter, "zz mine"));
}

TEST(ShapesPlaceTest, RenamingASavedShapeMovesItsFileAndKeepsItSelectedAndFavourite) {
    ShapesFixture f;
    f.openShapes();
    f.view->selectRow(f.rowOf("zz mine"));
    f.view->keyPressed(juce::KeyPress('f', juce::ModifierKeys(), 'f'));
    ASSERT_TRUE(f.view->applyShapeAction(f.rowOf("zz mine"), SoundBrowserComponent::ShapeAction::Rename));
    auto& label = f.view->getRenameLabel();
    EXPECT_TRUE(label.isVisible());
    EXPECT_EQ(label.getText(), "zz mine");
    label.setText("renamed", juce::sendNotification);
    pump();
    EXPECT_EQ(f.rowOf("zz mine"), -1);
    ASSERT_GE(f.rowOf("renamed"), 0);
    EXPECT_FALSE(f.library.exists(ShapeKind::Filter, "zz mine"));
    EXPECT_TRUE(f.library.exists(ShapeKind::Filter, "renamed"));
    EXPECT_EQ(f.view->getSelectedIndex(), f.rowOf("renamed"));
    EXPECT_TRUE(f.lists.isFavourite(synth::shapes::shapeId(ShapeKind::Filter, false, "renamed")));
    EXPECT_FALSE(f.lists.isFavourite(synth::shapes::shapeId(ShapeKind::Filter, false, "zz mine")));
}

TEST(ShapesPlaceTest, RenamingOverATakenNameLeavesTheShapeAsItWas) {
    ShapesFixture f;
    f.library.save(ShapeKind::Filter, "taken", synth::LfoCustomWave::preset(synth::LfoCustomWave::Preset::RampUp));
    f.openShapes();
    ASSERT_TRUE(f.view->applyShapeAction(f.rowOf("zz mine"), SoundBrowserComponent::ShapeAction::Rename));
    f.view->getRenameLabel().setText("taken", juce::sendNotification);
    synth::test::waitUntil([&] { return (f.rowOf("zz mine")) >= (0); });
    EXPECT_GE(f.rowOf("zz mine"), 0);
    EXPECT_TRUE(f.library.exists(ShapeKind::Filter, "zz mine"));
}

TEST(ShapesPlaceTest, AFavouritedShapeShowsUnderFavouritesAndAMissingOneDoesNot) {
    ShapesFixture f;
    f.openShapes();
    f.view->selectRow(f.rowOf("ah"));
    EXPECT_TRUE(f.view->keyPressed(juce::KeyPress('f', juce::ModifierKeys(), 'f')));
    f.view->selectRow(f.rowOf("zz mine"));
    f.view->keyPressed(juce::KeyPress('f', juce::ModifierKeys(), 'f'));
    EXPECT_TRUE(f.lists.isFavourite("shape:filter:factory:ah")) << "the id is the drag payload's";
    EXPECT_TRUE(f.lists.isFavourite("shape:filter:user:zz mine"));

    f.view->showLocation(SoundBrowserComponent::kFavouritesId, {});
    f.view->completeListAnimationForTest();
    EXPECT_GE(f.rowOf("ah"), 0);
    EXPECT_GE(f.rowOf("zz mine"), 0);
    EXPECT_EQ(f.view->getRows()[(size_t)f.rowOf("zz mine")].kind, SoundBrowserComponent::RowKind::Shape);

    f.library.remove(ShapeKind::Filter, "zz mine");
    EXPECT_EQ(f.rowOf("zz mine"), -1) << "a deleted shape leaves the list";
    f.library.save(ShapeKind::Filter, "zz mine", synth::LfoCustomWave::preset(synth::LfoCustomWave::Preset::RampUp));
    f.view->setShapeLibrary(nullptr);
    EXPECT_EQ(f.rowOf("ah"), -1) << "without a library no shape row is listed";
}

TEST(ShapesPlaceTest, AStampedShapeIsUsedRecentlyAndReturnStampsOnlyWhenACardTakesIt) {
    ShapesFixture f;
    f.openShapes();
    int calls = 0;
    bool lands = false;
    f.view->onShapeActivated = [&](const synth::shapes::LibraryShape&) {
        ++calls;
        return lands;
    };
    f.view->selectRow(f.rowOf("ah"));
    EXPECT_TRUE(f.view->keyPressed(keyOf(juce::KeyPress::returnKey))) << "the key is taken either way";
    EXPECT_EQ(calls, 1);
    EXPECT_TRUE(f.lists.recent().isEmpty()) << "no fitting card: nothing is stamped, nothing is recent";
    lands = true;
    EXPECT_TRUE(f.view->keyPressed(keyOf(juce::KeyPress::returnKey)));
    ASSERT_EQ(f.lists.recent().size(), 1);
    EXPECT_EQ(f.lists.recent()[0], "shape:filter:factory:ah");

    f.library.noteUsed(*f.library.find(ShapeKind::Lfo, true, "ramp up")); // a drop or Start from
    EXPECT_EQ(f.lists.recent()[0], "shape:lfo:factory:ramp up");
    f.view->showLocation(SoundBrowserComponent::kRecentId, {});
    f.view->completeListAnimationForTest();
    EXPECT_EQ(f.rowOf("ramp up"), 0);
    EXPECT_EQ(f.rowOf("ah"), 1);
}

TEST(ShapesPlaceTest, TheTooltipOfAShapeRowSaysWhatReturnNeeds) {
    ShapesFixture f;
    f.openShapes();
    const auto& row = f.view->getRows()[(size_t)f.rowOf("ah")];
    f.view->selectRow(f.rowOf("ah"));
    (void)row;
    f.view->mouseMove(juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(),
                                       f.view->getRowBounds(f.rowOf("ah")).getCentre().toFloat(), {}, 0, 0, 0, 0, 0,
                                       f.view.get(), f.view.get(), juce::Time::getCurrentTime(), {},
                                       juce::Time::getCurrentTime(), 1, false));
    const auto tip = f.view->getTooltip();
    EXPECT_NE(tip.indexOf("Return"), -1);
    EXPECT_NE(tip.indexOf("Custom"), -1);
}
