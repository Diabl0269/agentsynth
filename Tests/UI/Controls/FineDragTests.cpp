// FineDragTests.cpp -- Shift while dragging slows every value drag to FineDrag::kRate (docs/layout/fine-drag.md).
//
// FineDrag: the shared scaling, and that a Shift press or release mid-drag never makes the pointer, and so the value,
// jump. FineDragEveryControl: the guard for the sibling-gap kind. A table of real controls is dragged with and without
// Shift and the Shift travel must be about a tenth; a source scan then fails on a juce::Slider that is not a
// FineDragSlider and on a class that overrides mouseDrag without saying whether it drags a value (and so slows with
// Shift, with the test that proves it) or something else (an exemption with its reason). kNotAValue may only shrink.
#include "../SourceScan.h"
#include "TestPaths.h"
#include "UI/Chrome/ColourPickerFields.h"
#include "UI/Chrome/ExpandingRangeSlider.h"
#include "UI/Controls/FineDrag/FineDragSlider.h"
#include "UI/Controls/RangeSlider/RangeSlider.h"
#include "UI/Graph/CardWidgets/CardFader.h"
#include "UI/Graph/ModDot/ModDotAmountBar.h"
#include "UI/Graph/ModuleComponent/CardKnobSlider.h"
#include <cmath>
#include <functional>
#include <gtest/gtest.h>
#include <map>
#include <regex>
#include <set>
#include <string>
#include <vector>

using synth::ui::FineDrag;
using synth::ui::FineDragSlider;

namespace {

const juce::ModifierKeys kLeft(juce::ModifierKeys::leftButtonModifier);
const juce::ModifierKeys kShift(juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::shiftModifier);

juce::MouseEvent eventAt(juce::Component& comp, juce::Point<float> pos, juce::Point<float> down,
                         juce::ModifierKeys mods, bool dragged) {
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), pos, mods, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                            &comp, &comp, juce::Time::getCurrentTime(), down, juce::Time::getCurrentTime(), 1, dragged);
}

// What a drag of `pixels` along `axis` from `start` moves the control's value by (in its own 0..1 proportion), with
// `mods` held for the whole drag.
struct Probe {
    std::string name;
    // press at the start, drag by (dx, dy), return the proportion moved since the press
    std::function<double(juce::ModifierKeys, float dx, float dy)> run;
    float dx = 0.0f, dy = 0.0f;
    double maxRatio = 0.15; // an angle drag is not linear in the pointer: it only has to be well under the plain one
};

double slide(juce::Slider& s, juce::ModifierKeys mods, juce::Point<float> start, float dx, float dy) {
    s.mouseDown(eventAt(s, start, start, mods, false));
    const double before = s.valueToProportionOfLength(s.getValue());
    s.mouseDrag(eventAt(s, start + juce::Point<float>(dx, dy), start, mods, true));
    const double moved = s.valueToProportionOfLength(s.getValue()) - before;
    s.mouseUp(eventAt(s, start + juce::Point<float>(dx, dy), start, mods, true));
    return moved;
}

template <typename S>
Probe sliderProbe(const std::string& name, juce::Slider::SliderStyle style, float dx, float dy, double maxRatio = 0.15,
                  juce::Point<float> start = {100.0f, 100.0f}) {
    return {name,
            [style, start](juce::ModifierKeys mods, float ddx, float ddy) {
                S s;
                s.setSliderStyle(style);
                s.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
                s.setRange(0.0, 1.0, 0.0);
                s.setSize(200, 200);
                s.setValue(0.5, juce::dontSendNotification);
                return slide(s, mods, start, ddx, ddy);
            },
            dx, dy, maxRatio};
}

std::vector<Probe> probes() {
    std::vector<Probe> all;
    all.push_back(
        sliderProbe<FineDragSlider>("FineDragSlider linear horizontal", juce::Slider::LinearHorizontal, 60, 0));
    all.push_back(sliderProbe<FineDragSlider>("FineDragSlider linear vertical", juce::Slider::LinearVertical, 0, -60));
    all.push_back(
        sliderProbe<FineDragSlider>("FineDragSlider rotary drag", juce::Slider::RotaryHorizontalVerticalDrag, 40, -40));
    all.push_back(sliderProbe<FineDragSlider>("FineDragSlider rotary angle", juce::Slider::Rotary, 60, 0, 0.3,
                                              {100.0f, 40.0f})); // off the centre: an angle drag needs a radius
    all.push_back(
        sliderProbe<synth::ui::CardKnobSlider>("CardKnobSlider", juce::Slider::RotaryHorizontalVerticalDrag, 40, -40));
    all.push_back(
        sliderProbe<synth::ui::ExpandingRangeSlider>("ExpandingRangeSlider", juce::Slider::LinearHorizontal, 60, 0));
    all.push_back({"CardFader vertical",
                   [](juce::ModifierKeys mods, float, float ddy) {
                       synth::ui::CardFader f(synth::ui::CardFader::Orientation::Vertical);
                       f.setRange(0.0, 1.0);
                       f.setSize(synth::ui::CardFader::kVerticalWidth, 120);
                       f.setValue(0.5, juce::dontSendNotification);
                       return slide(f, mods, {f.travelBounds().getCentreX(), 60.0f}, 0.0f, ddy);
                   },
                   0, -40});
    all.push_back({"CardFader horizontal",
                   [](juce::ModifierKeys mods, float ddx, float) {
                       synth::ui::CardFader f(synth::ui::CardFader::Orientation::Horizontal);
                       f.setRange(0.0, 1.0);
                       f.setSize(240, 24);
                       f.setValue(0.5, juce::dontSendNotification);
                       return slide(f, mods, {f.travelBounds().getCentreX(), f.travelBounds().getCentreY()}, ddx, 0.0f);
                   },
                   60, 0});
    all.push_back({"ModDotAmountBar",
                   [](juce::ModifierKeys mods, float ddx, float) {
                       synth::ui::ModDotAmountBar bar;
                       bar.setSize(200, 20);
                       const juce::Point<float> start(bar.xForValue(0.0f), 10.0f);
                       bar.mouseDown(eventAt(bar, start, start, mods, false));
                       const double before = bar.amount();
                       bar.mouseDrag(eventAt(bar, start + juce::Point<float>(ddx, 0.0f), start, mods, true));
                       const double moved = bar.amount() - before;
                       bar.mouseUp(eventAt(bar, start, start, mods, true));
                       return moved / 2.0; // the amount spans -1..1
                   },
                   80, 0, 0.2}); // whole percent steps: a tenth of 80 px is 8 px, a few percent
    all.push_back({"HueBar",
                   [](juce::ModifierKeys mods, float, float ddy) {
                       synth::ui::HueBar bar;
                       bar.setSize(20, 200);
                       synth::ui::Hsb start{0.0f, 1.0f, 1.0f};
                       bar.setHsb(start);
                       const juce::Point<float> from(10.0f, 3.0f);
                       bar.mouseDown(eventAt(bar, from, from, mods, false));
                       const double before = bar.hsb().hue;
                       bar.mouseDrag(eventAt(bar, from + juce::Point<float>(0.0f, ddy), from, mods, true));
                       return (double)bar.hsb().hue - before;
                   },
                   0, 100});
    all.push_back({"RangeSlider handle",
                   [](juce::ModifierKeys mods, float ddx, float) {
                       synth::ui::RangeSlider s;
                       s.setBounds(0, 0, 240, synth::ui::RangeSlider::kPreferredHeight);
                       s.setLimits(0, 1000, 1);
                       s.setValues(100, 900);
                       s.finishMotionForTest();
                       auto& handle = s.getHandle(synth::ui::RangeSlider::Handle::Lower);
                       const auto start = handle.getLocalBounds().getCentre().toFloat();
                       const double before = s.getLower();
                       handle.mouseDown(eventAt(handle, start, start, mods, false));
                       handle.mouseDrag(eventAt(handle, start + juce::Point<float>(ddx, 0.0f), start, mods, true));
                       const double moved = s.getLower() - before;
                       handle.mouseUp(eventAt(handle, start, start, mods, true));
                       return moved / 1000.0;
                   },
                   60, 0});
    return all;
}

} // namespace

TEST(FineDrag, ShiftScalesOnlyTheTravelAfterItIsPressedAndNeverMakesThePointerJump) {
    FineDrag fine;
    juce::Component c;
    c.setSize(200, 200);
    fine.begin({10.0f, 10.0f});
    EXPECT_FLOAT_EQ(fine.advance(eventAt(c, {30.0f, 10.0f}, {10.0f, 10.0f}, kLeft, true)).x, 30.0f);
    // Shift pressed with the pointer held still: the same place.
    EXPECT_FLOAT_EQ(fine.advance(eventAt(c, {30.0f, 10.0f}, {10.0f, 10.0f}, kShift, true)).x, 30.0f);
    // 50 px more with Shift is a tenth of that.
    EXPECT_NEAR(fine.advance(eventAt(c, {80.0f, 10.0f}, {10.0f, 10.0f}, kShift, true)).x, 35.0f, 1e-4);
    // Shift released, pointer still: the same place, then full speed again.
    EXPECT_NEAR(fine.advance(eventAt(c, {80.0f, 10.0f}, {10.0f, 10.0f}, kLeft, true)).x, 35.0f, 1e-4);
    EXPECT_NEAR(fine.advance(eventAt(c, {90.0f, 10.0f}, {10.0f, 10.0f}, kLeft, true)).x, 45.0f, 1e-4);
    EXPECT_DOUBLE_EQ(FineDrag::rateFor(kLeft), 1.0);
    EXPECT_DOUBLE_EQ(FineDrag::rateFor(kShift), FineDrag::kRate);
}

TEST(FineDrag, ASliderDragNeverJumpsWhenShiftTogglesMidDrag) {
    FineDragSlider s(juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox);
    s.setRange(0.0, 1.0, 0.0);
    s.setSize(100, 100);
    s.setValue(0.2, juce::dontSendNotification);
    const juce::Point<float> start(50.0f, 50.0f);
    s.mouseDown(eventAt(s, start, start, kLeft, false));
    s.mouseDrag(eventAt(s, start + juce::Point<float>(0.0f, -50.0f), start, kLeft, true));
    const double afterPlain = s.getValue();
    EXPECT_GT(afterPlain, 0.3);
    s.mouseDrag(eventAt(s, start + juce::Point<float>(0.0f, -50.0f), start, kShift, true));
    EXPECT_DOUBLE_EQ(s.getValue(), afterPlain) << "Shift pressed, pointer still: no jump";
    s.mouseDrag(eventAt(s, start + juce::Point<float>(0.0f, -100.0f), start, kShift, true));
    EXPECT_NEAR(s.getValue() - afterPlain, 50.0 / 250.0 * FineDrag::kRate, 0.003) << "then a tenth of the rate";
    const double afterFine = s.getValue();
    s.mouseDrag(eventAt(s, start + juce::Point<float>(0.0f, -100.0f), start, kLeft, true));
    EXPECT_DOUBLE_EQ(s.getValue(), afterFine) << "Shift released, pointer still: no jump";
    s.mouseUp(eventAt(s, start, start, kLeft, true));
}

TEST(FineDragEveryControl, ShiftDragMovesAboutATenthAsFarAsAPlainDragOnEveryValueControl) {
    for (const auto& probe : probes()) {
        const double plain = std::abs(probe.run(kLeft, probe.dx, probe.dy));
        const double fine = std::abs(probe.run(kShift, probe.dx, probe.dy));
        ASSERT_GT(plain, 0.02) << probe.name << ": the plain drag barely moved it, the probe is wrong";
        EXPECT_GT(fine, 0.0) << probe.name << ": Shift must slow the drag, not stop it";
        EXPECT_LT(fine / plain, probe.maxRatio) << probe.name << ": Shift moved " << fine << " against " << plain;
    }
}

namespace {

// Classes that drag a value. `file` must use FineDrag (or FineDragSlider), and `guard` names the registered test that
// drags it with Shift (this file's table, or the class's own).
struct ValueDrag {
    const char* cls;
    const char* file;
    const char* guard;
};

const char* const kTable = "FineDragEveryControl.ShiftDragMovesAboutATenthAsFarAsAPlainDragOnEveryValueControl";

const std::vector<ValueDrag> kValueDrags = {
    {"FineDragSlider", "Source/UI/Controls/FineDrag/FineDragSlider.h", kTable},
    {"CardKnobSlider", "Source/UI/Graph/ModuleComponent/CardKnobSlider.h", kTable},
    {"CardFader", "Source/UI/Graph/CardWidgets/CardFader.cpp", kTable},
    {"ModDotAmountBar", "Source/UI/Graph/ModDot/ModDotAmountBar.cpp", kTable},
    {"HueBar", "Source/UI/Chrome/ColourPickerFields.h", kTable},
    {"SaturationBrightnessField", "Source/UI/Chrome/ColourPickerFields.h", kTable},
    {"RangeSlider", "Source/UI/Controls/RangeSlider/RangeSlider.cpp", kTable},
    {"MixerFaderSlider", "Source/UI/Mixer/MixerFaderSlider.cpp",
     "MixerFaderDragTest.ShiftDragMovesAboutOneTenthAsFarAsPlainDrag"},
    {"CardStepper", "Source/UI/Graph/CardWidgets/CardStepper.cpp",
     "CardStepper.ShiftDragIsATenthOfTheSpeedAndNeverJumpsWhenShiftChanges"},
    {"BpmDragLabel", "Source/UI/Timeline/TimelineTransportBar.cpp",
     "FineDrag.ShiftScalesOnlyTheTravelAfterItIsPressedAndNeverMakesThePointerJump"},
    {"ModCurveRangeHandle", "Source/UI/ModuleViews/ModCurve/ModCurveRangeHandle.cpp",
     "FineDrag.ShiftScalesOnlyTheTravelAfterItIsPressedAndNeverMakesThePointerJump"},
    {"ModulatorBand", "Source/UI/Timeline/AutomationLanes/Modulators/ModulatorBand.cpp",
     "FineDrag.ShiftScalesOnlyTheTravelAfterItIsPressedAndNeverMakesThePointerJump"},
    {"MeterColourStopsEditor", "Source/UI/Settings/MeterColourStopsEditor.cpp",
     "FineDrag.ShiftScalesOnlyTheTravelAfterItIsPressedAndNeverMakesThePointerJump"},
    // The modulation-amount drags: a card knob's ring or Alt-drag (ModuleComponent) and the cable's midpoint knob
    // (GraphEditor) scale each move by rateFor.
    {"ModuleComponent", "Source/UI/Graph/ModuleComponent/ModuleComponent.cpp",
     "FineDrag.ShiftScalesOnlyTheTravelAfterItIsPressedAndNeverMakesThePointerJump"},
    {"GraphEditor", "Source/UI/Graph/GraphEditor/GraphEditorCanvas.cpp",
     "FineDrag.ShiftScalesOnlyTheTravelAfterItIsPressedAndNeverMakesThePointerJump"},
};

// Drags that move something other than a value, each with why Shift stays what it was. May only shrink: a drag that
// turns out to move a value leaves this list for kValueDrags.
const char* const kPoints =
    "moves points or notes, not a value: Shift there is axis lock or extend selection (docs/layout/fine-drag.md)";
const char* const kReorder = "reorders or moves rows, panels or cards, nothing carries a value";
const char* const kResize = "resizes a panel or pane to a width or height, not a control value";
const char* const kForward = "forwards the drag to its parent, whose entry decides";

const std::map<std::string, std::string> kNotAValue = {
    {"TimelineClipLaneArea", kPoints},
    {"TimelineSectionStrip", kReorder},
    {"CurveEditorComponent", kPoints},
    {"MorphMap", kPoints},
    {"AutomationLaneEditor", kPoints},
    {"PianoRollComponent", kPoints},
    {"PianoRollVelocityLane", kPoints},
    {"NoteRangePicker", kPoints},
    {"MixerZonesRow", kReorder},
    {"TimelineRulerComponent", "moves a loop or marker position on the ruler, a position and not a value"},
    {"CardLayoutOutline", kReorder},
    {"CardLayoutAddRow", kReorder},
    {"CardLayoutGroupHandle", kReorder},
    {"DrumMachineComponent", kPoints},
    {"DrumHitLane", kPoints},
    {"MixerInsertList", kReorder},
    {"CardLayoutEditorGrip", kReorder},
    {"DockTabButton", kReorder},
    {"MixerColumnHeader", kReorder},
    {"MixerSendList", kReorder},
    {"TimelineTrackHeaderComponent", kReorder},
    {"AutomationLaneHeaderComponent", kReorder},
    {"LaneControlChip", kReorder},
    {"ModulatorRow", kReorder},
    {"DragHandle", kReorder},
    {"ControllerSurfaceCell", "moves a cell of the controller layout, its slider shows a value and takes no mouse"},
    {"EQCurveComponent", kPoints},
    {"StepGridComponent", "paints cells, nothing carries a value"},
    {"EdgeResizeHandle", kResize},
    {"PanelResizeHandle", kResize},
    {"GrabEdge", kResize},
    {"MixerSectionDivider", kResize},
    {"MinimapComponent", "scrolls the view"},
    {"CanvasEdgeScrollDriver", "listens to the pointer while another drag runs, it has no drag of its own"},
    {"ControllerSurfaceComponent", "a selection marquee"},
    {"TopBar", "moves the help window"},
    {"CommandPaletteComponent", "moves the search bar"},
    {"MacroCardComponent", kReorder},
    {"ModuleLibraryComponent", "starts a drag-and-drop"},
    {"SoundBrowserComponent", "starts a drag-and-drop"},
    {"DrawPenButton", "a drag only cancels the press-and-hold timer"},
    {"MixerColourDot", kForward},
    {"SwatchButton", kForward},
    {"ContextMenuForwardingLabel", kForward},
};

// A guard is registered when a test of that name exists.
bool testExists(const std::string& fullName) {
    const auto* unit = ::testing::UnitTest::GetInstance();
    for (int s = 0; s < unit->total_test_suite_count(); ++s) {
        const auto* suite = unit->GetTestSuite(s);
        for (int t = 0; t < suite->total_test_count(); ++t)
            if (std::string(suite->name()) + "." + suite->GetTestInfo(t)->name() == fullName)
                return true;
    }
    return false;
}

} // namespace

TEST(FineDragEveryControl, EveryClassThatOverridesMouseDragSaysWhetherShiftSlowsIt) {
    const auto found = synth::test::source_scan::classesOverridingMouseDrag();
    ASSERT_GT(found.size(), 40u) << "the scan found almost nothing: has the mouseDrag signature changed?";
    const auto texts = synth::test::source_scan::sourceTexts();

    std::set<std::string> named;
    for (const auto& entry : kValueDrags) {
        EXPECT_TRUE(named.insert(entry.cls).second) << entry.cls << " is listed twice";
        EXPECT_TRUE(testExists(entry.guard)) << entry.cls << ": no test is named '" << entry.guard << "'";
        const auto it = texts.find(entry.file);
        if (it == texts.end()) {
            ADD_FAILURE() << entry.cls << ": " << entry.file << " is gone";
            continue;
        }
        // The include sits in the header when the drag is in the .cpp.
        std::string header = entry.file;
        if (header.size() > 4 && header.compare(header.size() - 4, 4, ".cpp") == 0)
            header.replace(header.size() - 4, 4, ".h");
        const auto sibling = texts.find(header);
        const bool usesFineDrag = it->second.find("FineDrag") != std::string::npos ||
                                  (sibling != texts.end() && sibling->second.find("FineDrag") != std::string::npos);
        EXPECT_TRUE(usesFineDrag) << entry.cls << ": " << entry.file
                                  << " does not use FineDrag, so Shift does not slow its drag";
        if (!found.count(entry.cls))
            ADD_FAILURE() << "stale: " << entry.cls << " no longer overrides mouseDrag - remove it from kValueDrags";
    }
    for (const auto& [cls, why] : kNotAValue) {
        EXPECT_TRUE(named.insert(cls).second) << cls << " is in both lists";
        EXPECT_GT(why.size(), 12u) << cls << ": an exemption needs a reason";
        if (!found.count(cls))
            ADD_FAILURE() << "stale: " << cls << " no longer overrides mouseDrag - remove it from kNotAValue";
    }
    for (const auto& [cls, file] : found)
        if (!named.count(cls))
            ADD_FAILURE() << "not listed: " << cls << " (" << file
                          << ") overrides mouseDrag - if it drags a value, make Shift slow it through FineDrag and add "
                             "it to kValueDrags in FineDragTests.cpp; if it drags something else, add it to "
                             "kNotAValue with the reason";
}

TEST(FineDragEveryControl, EveryJuceSliderIsAFineDragSlider) {
    // A juce::Slider declared, made or derived from anywhere in Source other than FineDragSlider itself. The three
    // faders that do their own drag maths (and never call juce::Slider's mouse handlers) are in kValueDrags above.
    static const std::regex made(
        R"(make_unique<\s*juce::Slider\s*>|\bjuce::Slider\s+\w+\s*[;{(]|new\s+juce::Slider\b)");
    static const std::regex derived(R"(class\s+(\w+)\s*(?:final\s*)?:\s*(?:public\s+)?juce::Slider\b)");
    static const std::set<std::string> ownDrag = {"MixerFaderSlider", "CardFader", "ModDotAmountBar"};
    for (const auto& [file, text] : synth::test::source_scan::sourceTexts()) {
        if (file == "Source/UI/Controls/FineDrag/FineDragSlider.h")
            continue;
        std::smatch m;
        if (std::regex_search(text, m, made))
            ADD_FAILURE() << file << ": a plain juce::Slider (" << m.str(0)
                          << ") ignores Shift - use synth::ui::FineDragSlider";
        for (std::sregex_iterator it(text.begin(), text.end(), derived), end; it != end; ++it)
            if (!ownDrag.count((*it)[1]))
                ADD_FAILURE() << file << ": " << (*it)[1]
                              << " derives from juce::Slider directly - derive from FineDragSlider";
    }
}
