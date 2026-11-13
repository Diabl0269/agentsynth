// PerNoteBarTests.cpp -- the shape editor's per-note settings: the mode switch, Smoothness and the By hand key strip
// set the Filter's state through real mouse and key events, each change is one undo step, the key row shows only
// for By hand, and every control is named, tooltipped and reachable from the keyboard.
#include "../../Accessibility/TabOrderHelpers.h"
#include "../CurveEditor/CurveEditorTestHelpers.h"
#include "Mixer/ChannelFlows/ChannelFlows.h"
#include "ShapeEditorTestHelpers.h"
#include "UI/ModuleViews/ShapeEditor/ShapeListEditor.h"

using namespace synth::ui;
using namespace synth::ui::test;
using Mode = synth::CustomFilterPerNote::Mode;

namespace {

/** A left click at the centre of segment `index`, delivered to the switch (segments take no mouse). */
void clickSegment(CardSegmentedSwitch& sw, int index) {
    const auto at = sw.getSegment(index)->getBounds().getCentre().toFloat();
    sw.mouseDown(curveLeftClick(sw, at));
}

void clickKey(NoteKeyStrip& strip, int note) {
    const auto at = strip.getKeyBounds(note).getCentre().toFloat();
    strip.mouseDown(curveLeftClick(strip, at));
}

struct BarRig {
    ShapeRig rig;
    ShapeListEditor editor{rig.adapter, 0};
    PerNoteBar& bar = *editor.getPerNoteBar();
};

} // namespace

TEST(PerNoteBar, TheModeSwitchSetsTheModeAsOneUndoStep) {
    BarRig r;
    EXPECT_EQ(r.bar.getModeSwitch().getSelectedIndex(), (int)Mode::Off);
    const int serial = r.rig.undo.getEditSerial();
    clickSegment(r.bar.getModeSwitch(), (int)Mode::ByPitch);
    EXPECT_EQ(r.rig.filter->getCustomPerNote().mode, Mode::ByPitch);
    EXPECT_EQ(r.rig.undo.getEditSerial(), serial + 1);

    r.bar.getModeSwitch().keyPressed(key(juce::KeyPress::rightKey)); // keyboard: Random
    EXPECT_EQ(r.rig.filter->getCustomPerNote().mode, Mode::Random);

    r.rig.undo.undo();
    r.editor.refresh();
    EXPECT_EQ(r.rig.filter->getCustomPerNote().mode, Mode::ByPitch);
    EXPECT_EQ(r.bar.getModeSwitch().getSelectedIndex(), (int)Mode::ByPitch) << "undo reaches the bar";
}

TEST(PerNoteBar, DraggingSmoothnessIsHeardLiveAndIsOneUndoStep) {
    BarRig r;
    auto& slider = r.bar.getSmoothness();
    ASSERT_GT(slider.getWidth(), 20);
    const int serial = r.rig.undo.getEditSerial();
    const float y = (float)slider.getHeight() * 0.5f;
    const juce::Point<float> from{slider.getPositionOfValue(slider.getValue()), y};
    const juce::Point<float> to{(float)slider.getWidth() - 2.0f, y};
    slider.mouseDown(curveLeftClick(slider, from));
    slider.mouseDrag(curveLeftDrag(slider, {from.x + (to.x - from.x) * 0.5f, y}, from));
    const float halfway = r.rig.filter->getCustomPerNote().smoothness;
    EXPECT_GT(halfway, synth::CustomFilterPerNote::kDefaultSmoothness) << "heard while dragging";
    slider.mouseDrag(curveLeftDrag(slider, to, from));
    slider.mouseUp(curveLeftDrag(slider, to, from));
    EXPECT_GT(r.rig.filter->getCustomPerNote().smoothness, halfway);
    EXPECT_EQ(r.rig.undo.getEditSerial(), serial + 1) << "one drag, one step";

    const float dragged = r.rig.filter->getCustomPerNote().smoothness;
    EXPECT_TRUE(slider.keyPressed(key(juce::KeyPress::leftKey))); // keyboard: one step, one undo step
    EXPECT_NEAR(r.rig.filter->getCustomPerNote().smoothness, dragged - 0.01f, 1e-4f);
    EXPECT_EQ(r.rig.undo.getEditSerial(), serial + 2);
    EXPECT_EQ(slider.getTextFromValue(0.0), "Hard");
    EXPECT_EQ(slider.getTextFromValue(1.0), "500 ms glide");
}

TEST(PerNoteBar, TheKeyRowShowsOnlyForByHand) {
    BarRig r;
    EXPECT_FALSE(r.bar.isKeyRowShown());
    EXPECT_FALSE(r.bar.getKeyStrip().isVisible());
    clickSegment(r.bar.getModeSwitch(), (int)Mode::ByHand);
    EXPECT_TRUE(r.bar.isKeyRowShown());
    EXPECT_TRUE(r.bar.getKeyStrip().isVisible());
    EXPECT_TRUE(r.bar.getOctaveUp().isVisible());
    clickSegment(r.bar.getModeSwitch(), (int)Mode::Random);
    EXPECT_FALSE(r.bar.isKeyRowShown());
}

TEST(PerNoteBar, TheKeyRowFadesInAndOut) {
    FadeVisibility::setAnimateOffScreenForTest(true);
    BarRig r;
    clickSegment(r.bar.getModeSwitch(), (int)Mode::ByHand);
    EXPECT_TRUE(r.bar.getKeyStrip().isVisible());
    EXPECT_LT(r.bar.getKeyStrip().getAlpha(), 0.01f) << "frame 0 is a fade frame";
    FadeVisibility::stepAllForTest(0.5f);
    EXPECT_GT(r.bar.getKeyStrip().getAlpha(), 0.3f);
    FadeVisibility::stepAllForTest(1.0f);
    EXPECT_FLOAT_EQ(r.bar.getKeyStrip().getAlpha(), 1.0f);
    clickSegment(r.bar.getModeSwitch(), (int)Mode::Off);
    EXPECT_TRUE(r.bar.getKeyStrip().isVisible()) << "stays on screen while it fades out";
    FadeVisibility::stepAllForTest(1.0f);
    EXPECT_FALSE(r.bar.getKeyStrip().isVisible());
    FadeVisibility::setAnimateOffScreenForTest(false);
}

TEST(PerNoteBar, ClickingAKeyCyclesItsShapeAndEachClickIsOneUndoStep) {
    BarRig r;
    clickSegment(r.bar.getModeSwitch(), (int)Mode::ByHand);
    auto& strip = r.bar.getKeyStrip();
    const int c4 = 60;
    strip.setBaseNote(c4);
    const int serial = r.rig.undo.getEditSerial();
    for (int expected : {0, 1, 2, -1}) {
        clickKey(strip, c4);
        EXPECT_EQ(r.rig.filter->getCustomPerNote().keys[c4], expected);
    }
    EXPECT_EQ(r.rig.undo.getEditSerial(), serial + 4);
    clickKey(strip, c4 + 1); // a black key
    EXPECT_EQ(r.rig.filter->getCustomPerNote().keys[c4 + 1], 0);
    EXPECT_EQ(r.rig.filter->getCustomPerNote().keys[c4], -1);
    ASSERT_NE(strip.getKeyButton(c4 + 1), nullptr);
    EXPECT_EQ(strip.getKeyButton(c4 + 1)->getTitle(), "C#4: ah");
    EXPECT_EQ(strip.getKeyButton(c4)->getTitle(), "C4: no shape");
    r.rig.undo.undo();
    r.editor.refresh();
    EXPECT_EQ(strip.getShapeFor(c4 + 1), -1) << "undo reaches the strip";
}

TEST(PerNoteBar, TheKeyStripWorksFromTheKeyboard) {
    BarRig r;
    clickSegment(r.bar.getModeSwitch(), (int)Mode::ByHand);
    auto& strip = r.bar.getKeyStrip();
    strip.setBaseNote(60);
    EXPECT_EQ(strip.getFocusedNote(), 60);
    EXPECT_TRUE(strip.keyPressed(key(juce::KeyPress::rightKey)));
    EXPECT_TRUE(strip.keyPressed(key(juce::KeyPress::rightKey)));
    EXPECT_EQ(strip.getFocusedNote(), 62);
    EXPECT_TRUE(strip.keyPressed(key(juce::KeyPress::spaceKey)));
    EXPECT_TRUE(strip.keyPressed(key(juce::KeyPress::returnKey)));
    EXPECT_EQ(r.rig.filter->getCustomPerNote().keys[62], 1);
    EXPECT_TRUE(strip.keyPressed(key(juce::KeyPress::backspaceKey)));
    EXPECT_EQ(r.rig.filter->getCustomPerNote().keys[62], -1);
    EXPECT_TRUE(strip.keyPressed(key(juce::KeyPress::pageUpKey)));
    EXPECT_EQ(strip.getBaseNote(), 72);
    EXPECT_EQ(strip.getFocusedNote(), 74) << "the focused key keeps its place in the octave";
    for (int i = 0; i < 3; ++i) // walking off the left end shifts the octave down
        strip.keyPressed(key(juce::KeyPress::leftKey));
    EXPECT_EQ(strip.getFocusedNote(), 71);
    EXPECT_EQ(strip.getBaseNote(), 60);
    r.bar.getOctaveDown().onClick();
    EXPECT_EQ(strip.getBaseNote(), 48);
    r.bar.getOctaveUp().onClick();
    EXPECT_EQ(strip.getBaseNote(), 60);
}

TEST(PerNoteBar, ByHandTabOrderAndEveryControlIsNamedAndTooltipped) {
    BarRig r;
    clickSegment(r.bar.getModeSwitch(), (int)Mode::ByHand);
    const auto walk = synth::test::walkTabOrder(r.editor);
    std::vector<std::string> names;
    for (auto* c : walk.forward)
        names.push_back(c->getTitle().toStdString());
    const std::vector<std::string> tail{
        "Notes move the cursor", "Smoothness", "Octave down", "Key map", "Octave up", "Done"};
    ASSERT_GE(names.size(), tail.size());
    EXPECT_EQ(std::vector<std::string>(names.end() - (long)tail.size(), names.end()), tail);
    for (juce::Component* c : {(juce::Component*)&r.bar.getModeSwitch(), (juce::Component*)&r.bar.getSmoothness(),
                               (juce::Component*)&r.bar.getKeyStrip(), (juce::Component*)&r.bar.getOctaveDown(),
                               (juce::Component*)&r.bar.getOctaveUp(),
                               (juce::Component*)r.bar.getKeyStrip().getKeyButton(r.bar.getKeyStrip().getBaseNote())}) {
        ASSERT_NE(c, nullptr);
        EXPECT_FALSE(c->getTitle().isEmpty());
        auto* tip = dynamic_cast<juce::TooltipClient*>(c);
        ASSERT_NE(tip, nullptr) << c->getTitle();
        EXPECT_FALSE(tip->getTooltip().isEmpty()) << c->getTitle();
    }
    EXPECT_FALSE(r.bar.getKeyStrip().getKeyButton(60 + 30) != nullptr) << "only the shown keys have buttons";
}

// Regression test for FRO1097: a mono Filter showed "turn on the Filter's Poly" for every mode, because per-note
// worked in Poly only.
TEST(PerNoteBar, AMonoFilterTakesEveryModeWithNoPolyHint) {
    BarRig r;
    synth::setProcessorPoly(r.rig.filter, false);
    ASSERT_FALSE(synth::isProcessorPoly(r.rig.filter));
    EXPECT_FALSE(r.bar.getModeSwitch().getTooltip().contains("Poly"));
    for (int m = 1; m < synth::CustomFilterPerNote::kNumModes; ++m) {
        clickSegment(r.bar.getModeSwitch(), m);
        EXPECT_EQ(r.rig.filter->getCustomPerNote().mode, (Mode)m);
        EXPECT_NE(r.rig.adapter->pitchInput(), PitchInput::MissingNoSource) << "no hooks: treated as cabled";
        EXPECT_FALSE(r.bar.isHintShown()) << synth::CustomFilterPerNote::modeLabel((Mode)m);
    }
}

TEST(PerNoteBar, AnAdapterWithoutPerNoteShowsNoBar) {
    struct Plain : ShapeListAdapter {
        std::vector<ShapeItem> shapes{ShapeItem{"one", {}, {0.5f, 0.5f}}};
        std::vector<ShapeItem> getShapes() const override { return shapes; }
        void setShapes(const std::vector<ShapeItem>& s) override { shapes = s; }
        int getGeneration() const override { return 0; }
        void beginEdit() override {}
        void endEdit() override {}
        int getMaxShapes() const override { return 8; }
        ShapeItem makeShapeAt(float x, float y) const override { return {"new", {}, {x, y}}; }
        juce::String getWindowTitle() const override { return "Shapes"; }
    };
    ShapeListEditor editor(std::make_shared<Plain>(), 0);
    EXPECT_EQ(editor.getPerNoteBar(), nullptr);
    EXPECT_EQ(editor.getHeight(), ShapeListEditor::kHeight);
}
