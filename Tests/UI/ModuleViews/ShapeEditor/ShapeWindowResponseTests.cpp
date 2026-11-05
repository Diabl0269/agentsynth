// ShapeWindowResponseTests.cpp -- the Filter's live frequency response over the shape editor while the morph cursor is
// the selection (and only then, and never in the LFO's window).
#include "../../../TestWait.h"
#include "../CurveEditor/CurveEditorTestHelpers.h"
#include "ShapeEditorTestHelpers.h"
#include "UI/ModuleViews/ShapeEditor/LfoShapesAdapter.h"
#include "UI/ModuleViews/ShapeEditor/ShapeListEditor.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

using namespace synth::ui;
using namespace synth::ui::test;

namespace {

juce::Image render(juce::Component& c) {
    juce::Image image(juce::Image::ARGB, c.getWidth(), c.getHeight(), true, juce::SoftwareImageType());
    juce::Graphics g(image);
    c.paintEntireComponent(g, false);
    return image;
}

bool sameImage(const juce::Image& a, const juce::Image& b) {
    if (a.getBounds() != b.getBounds())
        return false;
    for (int y = 0; y < a.getHeight(); ++y)
        for (int x = 0; x < a.getWidth(); ++x)
            if (a.getPixelAt(x, y) != b.getPixelAt(x, y))
                return false;
    return true;
}

/** Runs one block so the module publishes its modulated Cutoff, the value the response is drawn from. */
void runBlock(FilterModule& filter) {
    filter.prepareToPlay(44100.0, 512);
    juce::AudioBuffer<float> audio(filter.getTotalNumInputChannels() + 8, 512);
    audio.clear();
    juce::MidiBuffer midi;
    filter.processBlock(audio, midi);
}

void click(MorphMap& map, juce::Point<float> at) {
    map.mouseDown(curveLeftClick(map, at));
    map.mouseUp(curveLeftClick(map, at));
}

/** How many pixels are exactly `colour` (the opaque core of a 2 px line). */
int countPixelsOf(const juce::Image& image, juce::Colour colour) {
    int n = 0;
    for (int y = 0; y < image.getHeight(); ++y)
        for (int x = 0; x < image.getWidth(); ++x)
            if (image.getPixelAt(x, y).getARGB() == colour.withAlpha(1.0f).getARGB())
                ++n;
    return n;
}

} // namespace

TEST(ShapeWindowResponse, TheResponseAppearsWhenTheCursorIsSelectedAndGoesWhenADotIs) {
    ShapeRig rig;
    ShapeListEditor editor(rig.adapter, 0);
    auto& curve = editor.getCurveEditor();
    const auto dotSelected = render(curve);
    EXPECT_FALSE(editor.getMap().isCursorSelected());

    editor.getMap().getCursorStop()->grabKeyboardFocus();
    editor.getMap().getCursorStop()->focusGained(juce::Component::focusChangedDirectly);
    ASSERT_TRUE(editor.getMap().isCursorSelected());
    EXPECT_FALSE(sameImage(dotSelected, render(curve))) << "the response is drawn over the editor";
    const auto accent = synth::theme::themeOf(editor).colors.accent;
    EXPECT_GT(countPixelsOf(render(curve), accent), countPixelsOf(dotSelected, accent) + 100)
        << "a 2 px line in the card's response colour (the accent), not a second shape in another colour";

    editor.getMap().getDotStop(0)->focusGained(juce::Component::focusChangedDirectly);
    EXPECT_FALSE(editor.getMap().isCursorSelected());
    EXPECT_TRUE(sameImage(dotSelected, render(curve))) << "a dot hides the response again";
}

TEST(ShapeWindowResponse, ClickingTheCursorSelectsItAndClickingTheSelectedDotAgainClearsIt) {
    ShapeRig rig;
    ShapeListEditor editor(rig.adapter, 0);
    auto& map = editor.getMap();
    click(map, map.getCursorCentre());
    EXPECT_TRUE(map.isCursorSelected());
    map.getDotStop(0)->focusGained(juce::Component::focusChangedDirectly);
    EXPECT_FALSE(map.isCursorSelected()) << "the dot was already the selected shape: it still takes the selection back";
}

TEST(ShapeWindowResponse, ASelectedDotLeavesTheEditorAsItWasBeforeTheCursorWasSelected) {
    ShapeRig rig;
    ShapeListEditor editor(rig.adapter, 0);
    auto& curve = editor.getCurveEditor();
    const auto before = render(curve);
    editor.getMap().getCursorStop()->focusGained(juce::Component::focusChangedDirectly);
    editor.getMap().getDotStop(0)->focusGained(juce::Component::focusChangedDirectly);
    EXPECT_TRUE(sameImage(before, render(curve)));
}

TEST(ShapeWindowResponse, TheLfoWindowNeverDrawsIt) {
    AudioEngine engine;
    auto* lfo = new LFOModule();
    auto node = engine.getGraph().addNode(std::unique_ptr<juce::AudioProcessor>(lfo));
    AppUndoManager undo;
    auto adapter = std::make_shared<LfoShapesAdapter>(*lfo, &undo, engine.getGraph(), node->nodeID, "LFO");
    EXPECT_FALSE(adapter->hasResponse());
    ShapeListEditor editor(adapter, 0);
    auto& curve = editor.getCurveEditor();
    const auto before = render(curve);
    editor.getMap().getCursorStop()->focusGained(juce::Component::focusChangedDirectly);
    EXPECT_TRUE(sameImage(before, render(curve)));
}

TEST(ShapeWindowResponse, TheResponseFollowsACutoffChangeWhileTheCursorIsSelected) {
    ShapeRig rig;
    ShapeListEditor editor(rig.adapter, 0);
    editor.setVisible(true);
    auto& curve = editor.getCurveEditor();
    runBlock(*rig.filter);
    editor.getMap().getCursorStop()->focusGained(juce::Component::focusChangedDirectly);
    const auto first = render(curve);
    const auto beforeDb = rig.adapter->responseDb();

    auto* cutoff = dynamic_cast<juce::AudioParameterFloat*>(rig.filter->getParameters()[0]);
    for (auto* param : rig.filter->getParameters())
        if (param->getName(64) == "Cutoff")
            cutoff = dynamic_cast<juce::AudioParameterFloat*>(param);
    ASSERT_NE(cutoff, nullptr);
    *cutoff = 4000.0f;
    runBlock(*rig.filter);
    EXPECT_NE(rig.adapter->responseDb(), beforeDb);

    EXPECT_TRUE(synth::test::waitUntil([&] { return !sameImage(first, render(curve)); }))
        << "the editor repaints the new response on its own tick";
}
