// The Shapes place of the Sounds view: both groups, user shapes first, thumbnails that paint, the spoken row and the
// drag that starts from a row.
#include "SoundBrowserTestKit.h"
#include "UI/Library/SoundBrowser/ShapeDragPayload.h"
#include "UI/Library/SoundBrowser/SoundBrowserComponent.h"
#include "UI/Library/SoundBrowser/SoundBrowserText.h"
#include "UI/Library/SoundBrowser/SoundDragState.h"
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
