// The Filter's morph map sliders as landing widgets for Morph X / Y cables: the landing dot clear of the bar, a mod dot
// that opens and drags like a knob's, the modulation bar, and the amount drag on that bar -- all driven by real mouse
// events delivered to the real map inside a real card. Also the focus-ring guard for every mod dot (knob and map
// slider): the ring is centred on the dot at every zoom.

#include "ModDotTestFixture.h"
#include "Modules/FilterModule.h"
#include "UI/Graph/ModDot/ModDotButton.h"
#include "UI/Graph/ModuleComponent/ModuleComponentModBand.h"
#include "UI/Graph/ModuleComponent/MorphShapesCard/MorphShapesCard.h"
#include "UI/Layout/FocusRing.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <cmath>
#include <functional>

using namespace synth::ui;

namespace {

struct MorphRig {
    AudioEngine engine;
    AppUndoManager undo;
    std::unique_ptr<GraphEditor> editor;
    juce::AudioProcessorGraph::NodeID lfoId, filterId, attenId;
    ModuleComponent* card = nullptr;
    std::unique_ptr<juce::Component> held;
    int clicks = 0;

    explicit MorphRig(bool routed = true) {
        engine.initialise();
        engine.getGraph().clear();
        editor = std::make_unique<GraphEditor>(engine, &undo);
        undo.setGraphEditor(editor.get());
        auto& graph = engine.getGraph();
        auto lfoNode = graph.addNode(std::make_unique<LFOModule>());
        auto* filter = new FilterModule();
        *dynamic_cast<juce::AudioParameterChoice*>(findParameterByID(filter, "filterType")) =
            FilterModule::kCustomTypeIndex;
        auto filterNode = graph.addNode(std::unique_ptr<juce::AudioProcessor>(filter));
        lfoId = lfoNode->nodeID;
        filterId = filterNode->nodeID;
        if (routed) {
            attenId = engine.addModRouting(lfoId, 0, filterId, FilterModule::kMorphXCVChannel);
            setAmount(graph, attenId, 0.25f);
        }
        editor->getModDot().popoverLauncher = [this](std::unique_ptr<juce::Component> content, juce::Component&) {
            held = std::move(content);
        };
        editor->getModDot().onModDotClicked = [this](juce::AudioProcessorGraph::NodeID, int, juce::Component&) {
            ++clicks;
        };
        refresh();
        card = findModuleComp(*editor, filter);
        refresh(); // the card's own tick reads the routing
    }

    ~MorphRig() {
        held.reset();
        editor.reset();
        engine.shutdown();
    }

    void refresh() {
        editor->updateComponents();
        sizeModuleComponents(*editor);
        editor->timerCallback();
        if (card != nullptr)
            card->timerCallback();
    }

    MorphMap& map() { return card->getMorphCard()->getMap(); }
    float amount() { return amountParam(engine.getGraph(), attenId)->get(); }

    juce::MouseEvent onMap(juce::Point<float> p, juce::ModifierKeys mods = kPlain) {
        return makeModuleClickWithMods(map(), p.roundToInt(), mods);
    }
    void pressDragRelease(juce::Point<float> from, juce::Point<float> to) {
        map().mouseDown(onMap(from));
        map().mouseDrag(onMap(to));
        map().mouseUp(onMap(to));
    }
    juce::Point<float> barCentre(MorphMap::Axis axis) { return map().getModBarTrack(axis).getCentre(); }
};

struct Centroid {
    double x = 0.0, y = 0.0, mass = 0.0;
};

Centroid centroidOf(const juce::Image& image) {
    Centroid c;
    for (int y = 0; y < image.getHeight(); ++y)
        for (int x = 0; x < image.getWidth(); ++x) {
            const double a = image.getPixelAt(x, y).getAlpha();
            c.x += a * (x + 0.5);
            c.y += a * (y + 0.5);
            c.mass += a;
        }
    if (c.mass > 0.0) {
        c.x /= c.mass;
        c.y /= c.mass;
    }
    return c;
}

// A canvas for the card as the canvas shows it at `zoom`: children keep their integer bounds, the whole card is scaled.
// Big enough for a button that sits below or right of the card's own bounds.
juce::Image cardImage(const juce::Rectangle<int>& extent, float zoom) {
    return juce::Image(juce::Image::ARGB, (int)std::ceil(extent.getRight() * zoom) + 2,
                       (int)std::ceil(extent.getBottom() * zoom) + 2, true, juce::SoftwareImageType());
}

Centroid dotCentroid(const juce::Rectangle<int>& extent, juce::Point<float> anchor, float zoom) {
    auto image = cardImage(extent, zoom);
    {
        juce::Graphics g(image);
        g.addTransform(juce::AffineTransform::scale(zoom));
        g.setColour(juce::Colours::white);
        const float d = ModuleComponent::kKnobLandingDotDiameter;
        g.fillEllipse(anchor.x - d * 0.5f, anchor.y - d * 0.5f, d, d);
    }
    return centroidOf(image);
}

Centroid ringCentroid(ModDotButton& button, float zoom) {
    auto image = cardImage(button.getBounds(), zoom);
    {
        juce::Graphics g(image);
        g.addTransform(juce::AffineTransform::scale(zoom));
        g.addTransform(juce::AffineTransform::translation(button.getPosition()));
        button.paintEntireComponent(g, true);
    }
    return centroidOf(image);
}

struct RingsShown {
    RingsShown() { setFocusRingsShownForTest(true); }
    ~RingsShown() { setFocusRingsShownForTest(false); }
};

// One dot-with-ring surface: its card, its mod dot channel, and a way to relayout the card so the anchor lands on a new
// fractional spot.
struct Surface {
    juce::String name;
    ModuleComponent* card = nullptr;
    int channel = -1;
    std::function<void(int step)> moveAnchor;
    std::function<void()> refresh;
};

void expectRingCentredOnDot(const Surface& s) {
    RingsShown shown;
    for (int step = 0; step < 6; ++step) {
        s.moveAnchor(step);
        s.refresh();
        auto* button = s.card->getModDotButton(s.channel);
        ASSERT_NE(button, nullptr) << s.name << " step " << step;
        const auto anchor = s.card->getModTargetKnobAnchor(s.channel);
        ASSERT_TRUE(anchor.has_value());
        for (const bool open : {false, true}) {
            button->setMenuOpen(open);
            for (const float zoom : {1.0f, 1.5f, 2.0f}) {
                const auto dot = dotCentroid(button->getBounds(), *anchor, zoom);
                const auto ring = ringCentroid(*button, zoom);
                ASSERT_GT(ring.mass, 0.0)
                    << s.name << ": the ring paints, step " << step << " open " << open << " zoom " << zoom
                    << " button " << button->getBounds().toString() << " card " << s.card->getBounds().toString();
                EXPECT_NEAR(ring.x, dot.x, 0.5) << s.name << " step " << step << " zoom " << zoom << " open " << open
                                                << " anchor " << anchor->x << "," << anchor->y;
                EXPECT_NEAR(ring.y, dot.y, 0.5) << s.name << " step " << step << " zoom " << zoom << " open " << open
                                                << " anchor " << anchor->x << "," << anchor->y;
            }
        }
        button->setMenuOpen(false);
    }
}

float landingDot() { return ModuleComponent::kKnobLandingDotDiameter; }

} // namespace

// Regression test for FRO955: the landing dot sat 3 px from the slider and covered its bar.
TEST_F(ModuleComponentTest, TheMorphSliderLandingDotSitsClearOfTheBarAndInsideTheMap) {
    MorphRig rig(false);
    ASSERT_NE(rig.card, nullptr);
    const float radius = landingDot() * 0.5f;
    const auto inner = rig.map().getLocalBounds().toFloat();
    for (const auto axis : {MorphMap::Axis::X, MorphMap::Axis::Y}) {
        const auto strip = rig.map().getStripBounds(axis);
        const auto anchor = rig.map().stripAnchor(axis);
        const float gap = axis == MorphMap::Axis::X ? anchor.y - strip.getBottom() : strip.getX() - anchor.x;
        EXPECT_GE(gap, radius + 1.0f + MorphMap::kModBarThickness) << "dot centre clear of the slider and its bar";
        const auto dot = juce::Rectangle<float>(landingDot(), landingDot()).withCentre(anchor);
        EXPECT_TRUE(inner.contains(dot)) << "the dot is drawn inside the map";
        EXPECT_EQ(rig.map().stripAt(anchor), MorphMap::Axis::None) << "a plain press on the dot is not on the slider";
        EXPECT_TRUE(rig.map().getModBarTrack(axis).getIntersection(dot).isEmpty()) << "the dot does not cover the bar";
    }
}

TEST_F(ModuleComponentTest, AMorphSliderWithACableGetsAModDotButtonAtItsAnchor) {
    MorphRig rig;
    ASSERT_NE(rig.card, nullptr);
    auto* button = rig.card->getModDotButton(FilterModule::kMorphXCVChannel);
    ASSERT_NE(button, nullptr);
    EXPECT_TRUE(button->isVisible());
    const auto anchor = rig.card->getModTargetKnobAnchor(FilterModule::kMorphXCVChannel);
    ASSERT_TRUE(anchor.has_value());
    EXPECT_EQ(button->getBounds().getCentre(), anchor->roundToInt());
    EXPECT_EQ(button->getTitle(), "Morph X modulation, 1 source");
    EXPECT_EQ(button->getKnob(), rig.card->getMorphCard()->stripControlFor(FilterModule::kMorphXCVChannel));
    EXPECT_EQ(rig.card->getModDotButton(FilterModule::kMorphYCVChannel), nullptr) << "no cable, no dot";

    rig.engine.addModRouting(rig.lfoId, 0, rig.filterId, FilterModule::kMorphYCVChannel);
    rig.refresh();
    ASSERT_NE(rig.card->getModDotButton(FilterModule::kMorphYCVChannel), nullptr);

    rig.card->getMorphCard()->getToggle().setToggleState(false, juce::sendNotificationSync);
    rig.refresh();
    auto* back = rig.card->getModDotButton(FilterModule::kMorphXCVChannel);
    ASSERT_NE(back, nullptr) << "hiding the map hands the dot back to the Morph X knob";
    EXPECT_NE(back->getKnob(), rig.card->getMorphCard()->stripControlFor(FilterModule::kMorphXCVChannel));
}

TEST_F(ModuleComponentTest, APressOnAMorphSliderDotOpensTheDotAndDoesNotMoveTheCursor) {
    MorphRig rig;
    ASSERT_NE(rig.card, nullptr);
    const auto cursor = rig.map().getCursor();
    int moved = 0;
    int gestures = 0;
    rig.map().onCursorMoved = [&](float, float) { ++moved; };
    rig.map().onGestureStart = [&](MorphMap::Gesture) { ++gestures; };

    const auto dot = rig.map().stripAnchor(MorphMap::Axis::X);
    rig.pressDragRelease(dot, dot);

    EXPECT_EQ(rig.clicks, 1) << "a click on the dot opens what a knob's dot opens";
    EXPECT_EQ(moved, 0);
    EXPECT_EQ(gestures, 0);
    EXPECT_EQ(rig.map().getCursor(), cursor);
}

TEST_F(ModuleComponentTest, DraggingTheMorphSliderDotChangesTheAmountInOneUndoStep) {
    MorphRig rig;
    ASSERT_NE(rig.card, nullptr);
    const auto cursor = rig.map().getCursor();
    const auto dot = rig.map().stripAnchor(MorphMap::Axis::X);

    rig.pressDragRelease(dot, dot + juce::Point<float>(0.0f, 20.0f));

    EXPECT_NEAR(rig.amount(), 0.25f - 0.20f, 0.005f);
    EXPECT_EQ(rig.map().getCursor(), cursor);
    ASSERT_TRUE(rig.undo.undo());
    EXPECT_NEAR(rig.amount(), 0.25f, 1e-4f);
    EXPECT_FALSE(rig.undo.canUndo());
}

TEST_F(ModuleComponentTest, DraggingTheMorphSliderBarChangesTheAmountLikeAFadersBarAndLeavesTheCursor) {
    MorphRig rig;
    ASSERT_NE(rig.card, nullptr);
    const auto cursor = rig.map().getCursor();
    int gestures = 0;
    rig.map().onGestureStart = [&](MorphMap::Gesture) { ++gestures; };
    const auto bar = rig.barCentre(MorphMap::Axis::X);

    rig.pressDragRelease(bar, bar + juce::Point<float>(0.0f, -30.0f)); // up: more

    EXPECT_NEAR(rig.amount(), 0.25f + 0.30f, 0.005f);
    EXPECT_EQ(rig.map().getCursor(), cursor);
    EXPECT_EQ(gestures, 0) << "the cursor never starts a gesture";
    ASSERT_TRUE(rig.undo.undo());
    EXPECT_NEAR(rig.amount(), 0.25f, 1e-4f);
    EXPECT_FALSE(rig.undo.canUndo()) << "the whole drag is one undo step";
}

TEST_F(ModuleComponentTest, APressOnTheSliderItselfStillMovesTheCursor) {
    MorphRig rig;
    ASSERT_NE(rig.card, nullptr);
    const auto strip = rig.map().getStripBounds(MorphMap::Axis::X);
    rig.pressDragRelease({strip.getX() + strip.getWidth() * 0.9f, strip.getCentreY()},
                         {strip.getX() + strip.getWidth() * 0.9f, strip.getCentreY()});
    EXPECT_NEAR(rig.map().getCursor().x, 0.9f, 0.02f);
    EXPECT_NEAR(rig.amount(), 0.25f, 1e-5f);
}

TEST_F(ModuleComponentTest, TheMorphSliderBarShowsTheReachableRangeAndTheLiveModulation) {
    MorphRig rig;
    ASSERT_NE(rig.card, nullptr);
    const auto& shown = rig.map().getStripModulation(MorphMap::Axis::X);
    ASSERT_EQ(shown.size(), 1u);
    const auto expected = synth::ui::modDepthBandRange(0.5f, 0.25f, true);
    EXPECT_FLOAT_EQ(shown[0].bandStart, expected.startNorm);
    EXPECT_FLOAT_EQ(shown[0].bandEnd, expected.endNorm);
    EXPECT_FLOAT_EQ(shown[0].baseNorm, 0.5f);
    EXPECT_TRUE(rig.map().getStripModulation(MorphMap::Axis::Y).empty());

    // The band is painted: a pixel in the middle of the track differs from the same pixel with no cable.
    const auto probe = rig.barCentre(MorphMap::Axis::X).roundToInt();
    const auto paintedColour =
        rig.map().createComponentSnapshot(rig.map().getLocalBounds()).getPixelAt(probe.x, probe.y);
    rig.map().setStripModulation(MorphMap::Axis::X, {});
    const auto bareColour = rig.map().createComponentSnapshot(rig.map().getLocalBounds()).getPixelAt(probe.x, probe.y);
    EXPECT_NE(paintedColour, bareColour);

    rig.engine.getGraph().removeNode(rig.attenId);
    rig.refresh();
    EXPECT_TRUE(rig.map().getStripModulation(MorphMap::Axis::X).empty()) << "no cable, no bar";
}

// Opt-in visual check of the Custom Filter card with a cable on Morph X: the footer pill and the
// slider's landing dot and amount bar. Set FILTER_CUSTOM_CARD_PNG=<path> to write it.
TEST_F(ModuleComponentTest, CustomFilterCardRendersToPngForVisualInspection) {
    const char* pngPath = std::getenv("FILTER_CUSTOM_CARD_PNG");
    if (pngPath == nullptr || juce::String(pngPath).isEmpty())
        GTEST_SKIP() << "set FILTER_CUSTOM_CARD_PNG=<path> to write the rendered card for visual inspection";
    MorphRig rig;
    synth::theme::AppLookAndFeel lf;
    rig.editor->setLookAndFeel(&lf);
    rig.editor->setSize(2400, 2000);
    rig.refresh();
    rig.card->setSize(rig.card->getWidth(), rig.map().getBottom() + 60); // the test helper's fixed height cuts it
    rig.editor->timerCallback();
    const auto area = rig.card->getBounds().expanded(60).getIntersection(rig.editor->getLocalBounds());
    const auto img = rig.editor->createComponentSnapshot(area, true, 2.0f);
    rig.editor->setLookAndFeel(nullptr);
    juce::File outFile(pngPath);
    outFile.deleteFile();
    juce::FileOutputStream stream(outFile);
    ASSERT_TRUE(stream.openedOk());
    ASSERT_TRUE(juce::PNGImageFormat().writeImageToStream(img, stream));
}

// Regression test for FRO1663: the mod dot's focus ring sat up to half a pixel (more under zoom) off the dot.
TEST_F(ModuleComponentTest, EveryDotWithRingPaintsItsRingCentredOnTheDot) {
    // A knob's mod dot, the card resized so the knob centre lands on fractions.
    {
        Fixture f;
        ASSERT_NE(f.gainKnob, nullptr);
        const auto base = f.vcaCard->getBounds();
        Surface s{
            "knob dot", f.vcaCard, f.gainChannel,
            [&](int step) { f.vcaCard->setSize(base.getWidth() + step * 5 % 11, base.getHeight() + step * 3 % 7); },
            [&] { f.editor->timerCallback(); }};
        expectRingCentredOnDot(s);
    }
    // A morph map slider's mod dot, the card resized so the strip anchor lands on fractions.
    {
        MorphRig rig;
        ASSERT_NE(rig.card, nullptr);
        const auto base = rig.card->getBounds();
        Surface s{
            "morph map dot", rig.card, FilterModule::kMorphXCVChannel,
            [&](int step) { rig.card->setSize(base.getWidth() + step * 7 % 13, base.getHeight() + step * 3 % 9); },
            [&] { rig.editor->timerCallback(); }};
        expectRingCentredOnDot(s);
    }
    // Port jacks carry no keyboard focus ring (they are not Tab stops), so no jack surface applies.
}
