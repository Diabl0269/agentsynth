// ShapeWindowResponseTests.cpp -- the Filter's live frequency response over the shape editor while its Show Response
// button is on (whichever dot or the cursor is selected), and never in the LFO's window.
#include "../../../TestSettingsHelpers.h"
#include "../../../TestWait.h"
#include "../CurveEditor/CurveEditorTestHelpers.h"
#include "ShapeEditorTestHelpers.h"
#include "UI/Graph/CardBody/CardBodyGeometry.h"
#include "UI/Layout/ActionTexts.h"
#include "UI/ModuleViews/ShapeEditor/LfoShapesAdapter.h"
#include "UI/ModuleViews/ShapeEditor/PitchGridRows.h"
#include "UI/ModuleViews/ShapeEditor/ShapeListEditor.h"
#include "UI/ModuleViews/ShapeEditor/ShapeWindowPrefs.h"
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

struct ResponseKeysGuard {
    synth::test::PersistedKeysGuard guard{juce::StringArray{shapewindowprefs::responseKey("filter")}};
};

} // namespace

TEST(ShapeWindowResponse, TheToggleShowsTheResponseWhicheverDotOrTheCursorIsSelected) {
    ResponseKeysGuard keys;
    ShapeRig rig;
    ShapeListEditor editor(rig.adapter, 0);
    auto& curve = editor.getCurveEditor();
    auto& map = editor.getMap();
    const auto off = render(curve);
    const auto accent = synth::theme::themeOf(editor).colors.accent;
    ASSERT_NE(editor.getResponseToggle(), nullptr);
    EXPECT_FALSE(editor.getResponseToggle()->getToggleState());

    map.getCursorStop()->focusGained(juce::Component::focusChangedDirectly);
    ASSERT_TRUE(map.isCursorSelected());
    EXPECT_TRUE(sameImage(off, render(curve))) << "selecting the cursor no longer shows the response";

    editor.getResponseToggle()->setToggleState(true, juce::dontSendNotification);
    editor.getResponseToggle()->onClick();
    const auto withCursor = render(curve);
    EXPECT_GT(countPixelsOf(withCursor, accent), countPixelsOf(off, accent) + 100)
        << "a 2 px line in the card's response colour (the accent)";

    map.getDotStop(1)->focusGained(juce::Component::focusChangedDirectly);
    EXPECT_FALSE(map.isCursorSelected());
    EXPECT_GT(countPixelsOf(render(curve), accent), countPixelsOf(off, accent) + 100)
        << "a selected dot keeps the response";

    map.getDotStop(0)->focusGained(juce::Component::focusChangedDirectly);
    editor.getResponseToggle()->setToggleState(false, juce::dontSendNotification);
    editor.getResponseToggle()->onClick();
    EXPECT_TRUE(sameImage(off, render(curve))) << "off hides it, whatever is selected";
}

TEST(ShapeWindowResponse, TheChoiceIsRememberedAcrossWindows) {
    ResponseKeysGuard keys;
    juce::ApplicationProperties props;
    props.setStorageParameters(synth::test::userSettingsTestOptions());
    shapewindowprefs::saveFlag(&props, shapewindowprefs::responseKey("filter"), false);
    ShapeRig rig;
    rig.adapter->setApplicationProperties(&props);
    {
        ShapeListEditor editor(rig.adapter, 0);
        EXPECT_FALSE(editor.getResponseToggle()->getToggleState());
        editor.getResponseToggle()->setToggleState(true, juce::dontSendNotification);
        editor.getResponseToggle()->onClick();
    }
    ShapeListEditor again(rig.adapter, 0);
    EXPECT_TRUE(again.getResponseToggle()->getToggleState());
}

TEST(ShapeWindowResponse, TheButtonReadsLikeTheCardsAndSitsLeftOfShowSpectrumWithoutOverlap) {
    ShapeRig rig;
    ShapeListEditor editor(rig.adapter, 0);
    auto* response = editor.getResponseToggle();
    auto* spectrum = editor.getSpectrumToggle();
    ASSERT_NE(response, nullptr);
    EXPECT_EQ(response->getTitle(), juce::String(synth::cardbody::kShowResponseText));
    EXPECT_EQ(response->getTooltip(), juce::String(synth::ui::actiontext::kShowResponseTip));
    EXPECT_TRUE(response->getWantsKeyboardFocus());
    EXPECT_LT(response->getRight(), spectrum->getX());
    EXPECT_FALSE(response->getBoundsInParent().intersects(spectrum->getBoundsInParent()));
    EXPECT_FALSE(response->getBoundsInParent().intersects(editor.getGuidesToggle()->getBoundsInParent()));
    EXPECT_GE(response->getX(), editor.getPitchRows()->getX() + PitchGridRows::kSecondRowWidth)
        << "right of the grid rows' second row";
    EXPECT_TRUE(editor.getLocalBounds().contains(response->getBoundsInParent()));
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
    EXPECT_EQ(editor.getResponseToggle(), nullptr) << "the LFO window has no Show Response button";
}

TEST(ShapeWindowResponse, TheResponseFollowsACutoffChangeWhileTheToggleIsOn) {
    ResponseKeysGuard keys;
    ShapeRig rig;
    ShapeListEditor editor(rig.adapter, 0);
    editor.setVisible(true);
    auto& curve = editor.getCurveEditor();
    runBlock(*rig.filter);
    editor.getResponseToggle()->setToggleState(true, juce::dontSendNotification);
    editor.getResponseToggle()->onClick();
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
