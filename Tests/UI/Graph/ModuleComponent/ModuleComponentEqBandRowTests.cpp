// The Parametric EQ card's band strip and selected-band row (docs/layout/module-card-layout.md): three real knobs and
// a toggle that follow the one selection the curve, the strip and the knobs share; the card holds the same few widgets
// at 4 and at 24 bands, and its height is what the layout computes from the strip's wrapped rows.

#include "../../Accessibility/AccessibilityAudit.h"
#include "../../Accessibility/TabOrderHelpers.h"
#include "../../Layout/NativeWindowTestGuard.h"
#include "AudioEngine/AudioEngine.h"
#include "ModuleComponentTestFixture.h"

#include "Modules/FX/ParametricEQModule.h"
#include "UI/Graph/CardBody/CardBodyLanes.h"
#include "UI/Graph/CardWidgets/CardIconToggle.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/ModuleViews/EQBandStrip.h"
#include "UI/ModuleViews/EQCurveComponent.h"
#include "UI/ModuleViews/EQSoloButton.h"
#include "UI/ModuleViews/EQWindow.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

namespace {

std::vector<juce::Slider*> bandSliders(ModuleComponent& card) {
    std::vector<juce::Slider*> out;
    for (auto* child : card.getChildren())
        if (auto* s = dynamic_cast<juce::Slider*>(child))
            if (card.findMidiLearnableParamForTest(s) != nullptr &&
                card.findMidiLearnableParamForTest(s)->paramID.startsWith("band"))
                out.push_back(s);
    return out;
}

std::vector<juce::ToggleButton*> bandToggles(ModuleComponent& card) {
    std::vector<juce::ToggleButton*> out;
    for (auto* child : card.getChildren())
        if (auto* t = dynamic_cast<juce::ToggleButton*>(child))
            if (card.findMidiLearnableParamForTest(t) != nullptr &&
                card.findMidiLearnableParamForTest(t)->paramID.startsWith("band"))
                out.push_back(t);
    return out;
}

template <class T>
T* firstOfType(ModuleComponent& card) {
    for (auto* child : card.getChildren())
        if (auto* t = dynamic_cast<T*>(child))
            return t;
    return nullptr;
}

synth::ui::EQBandStrip* stripOf(ModuleComponent& card) { return firstOfType<synth::ui::EQBandStrip>(card); }

void turnBandsOn(ParametricEQModule& eq, int count) {
    for (int b = 0; b < count; ++b) {
        eq.setBandEnabled(b, true);
        eq.setBandFreq(b, 40.0f * std::pow(1.25f, (float)b));
    }
}

} // namespace

TEST_F(ModuleComponentTest, TheEqCardHoldsOneSelectedBandRowAtAnyBandCount) {
    AudioEngine engine;
    GraphEditor editor(engine);
    ParametricEQModule eq;
    turnBandsOn(eq, 24);
    ModuleComponent card(&eq, juce::AudioProcessorGraph::NodeID(1), editor);
    card.setSize(card.getWidth(), card.getHeight());

    EXPECT_EQ(bandSliders(card).size(), 3u);
    EXPECT_EQ(bandToggles(card).size(), 1u);
    ASSERT_NE(stripOf(card), nullptr);
    EXPECT_EQ(stripOf(card)->getComponentID(), "eqCardStrip");
}

TEST_F(ModuleComponentTest, ClickingAStripChipRebindsTheKnobsToThatBand) {
    AudioEngine engine;
    GraphEditor editor(engine);
    ParametricEQModule eq;
    turnBandsOn(eq, 10);
    ModuleComponent card(&eq, juce::AudioProcessorGraph::NodeID(1), editor);
    auto* curve = firstOfType<EQCurveComponent>(card);
    auto* strip = stripOf(card);
    ASSERT_NE(curve, nullptr);
    ASSERT_NE(strip, nullptr);
    curve->timerCallback();

    ASSERT_NE(strip->getChip(8), nullptr);
    strip->getChip(8)->onClick();
    EXPECT_EQ(curve->getSelectedBand(), 8);

    eq.setBandFreq(8, 3000.0f);
    const auto sliders = bandSliders(card);
    ASSERT_EQ(sliders.size(), 3u);
    juce::StringArray ids;
    for (auto* s : sliders)
        ids.add(card.findMidiLearnableParamForTest(s)->paramID);
    EXPECT_TRUE(ids.contains("band9Freq"));
    EXPECT_TRUE(ids.contains("band9Gain"));
    EXPECT_TRUE(ids.contains("band9Q"));
    const auto toggles = bandToggles(card);
    ASSERT_EQ(toggles.size(), 1u);
    EXPECT_EQ(card.findMidiLearnableParamForTest(toggles[0])->paramID, "band9On");
}

// Regression test for FRO1694: pressing a chip wrote the new band's frequency, gain and Q into the band the knobs had
// just left (the old attachment was still alive when the slider took the new value).
TEST_F(ModuleComponentTest, SelectingAnotherBandLeavesTheBandTheKnobsLeftUntouched) {
    AudioEngine engine;
    GraphEditor editor(engine);
    ParametricEQModule eq;
    turnBandsOn(eq, 10);
    eq.setBandGain(0, 9.0f);
    eq.setBandGain(8, -9.0f);
    eq.setBandQ(8, 3.0f);
    ModuleComponent card(&eq, juce::AudioProcessorGraph::NodeID(1), editor);
    auto* curve = firstOfType<EQCurveComponent>(card);
    auto* strip = stripOf(card);
    curve->timerCallback();
    curve->setSelectedBand(0);
    const auto before = eq.getBandSnapshots()[0];

    strip->getChip(8)->onClick();
    card.driveEqKnobGlideForTest(true, 1000.0);
    const auto after = eq.getBandSnapshots()[0];
    EXPECT_FLOAT_EQ(after.freqHz, before.freqHz);
    EXPECT_FLOAT_EQ(after.gainDb, before.gainDb);
    EXPECT_FLOAT_EQ(after.q, before.q);
    EXPECT_FLOAT_EQ(eq.getBandSnapshots()[8].gainDb, -9.0f);
}

// The card's strip chips, the selected band's knobs, toggle and pill are named, have tooltips and are Tab stops.
TEST_F(ModuleComponentTest, TheEqCardsBandControlsAreNamedHaveTooltipsAndAreTabStops) {
    AudioEngine engine;
    GraphEditor editor(engine);
    ParametricEQModule eq;
    turnBandsOn(eq, 9);
    ModuleComponent card(&eq, juce::AudioProcessorGraph::NodeID(1), editor);
    card.setSize(card.getWidth(), card.getHeight());
    auto* curve = firstOfType<EQCurveComponent>(card);
    curve->timerCallback();
    curve->setSelectedBand(8);

    int gaps = 0;
    juce::String listing;
    for (const auto& gap : synth::test::auditAccessibility(card))
        if (gap.path.contains("EQBandStrip") || gap.path.contains("EQBandTypePill") || gap.path.contains("B9") ||
            gap.path.contains("Band 9")) {
            ++gaps;
            listing << gap.path << "\n";
        }
    EXPECT_EQ(gaps, 0) << listing;

    const auto names = synth::test::walkTabOrder(card).names();
    EXPECT_TRUE(names.contains("Band 9, Bell, 213 Hz") || names.joinIntoString("|").contains("Band 9, Bell"))
        << names.joinIntoString(" | ");
    EXPECT_TRUE(names.joinIntoString("|").contains("Band 9 type, Bell")) << names.joinIntoString(" | ");
    EXPECT_TRUE(names.contains("B9 Freq")) << names.joinIntoString(" | ");
}

TEST_F(ModuleComponentTest, TheDrawnJacksDoNotChangeWithTheSelectedBand) {
    AudioEngine engine;
    GraphEditor editor(engine);
    ParametricEQModule eq;
    turnBandsOn(eq, 10);
    ModuleComponent card(&eq, juce::AudioProcessorGraph::NodeID(1), editor);
    auto* curve = firstOfType<EQCurveComponent>(card);
    auto* strip = stripOf(card);
    ASSERT_NE(curve, nullptr);
    curve->timerCallback();
    const auto before = card.drawnInputJackIndices();
    strip->getChip(8)->onClick();
    EXPECT_EQ(card.drawnInputJackIndices(), before);
}

TEST_F(ModuleComponentTest, ChangingTheSelectedBandDoesNotJumpTheKnobs) {
    AudioEngine engine;
    GraphEditor editor(engine);
    ParametricEQModule eq;
    turnBandsOn(eq, 10);
    eq.setBandGain(0, 9.0f);
    eq.setBandGain(8, -9.0f);
    ModuleComponent card(&eq, juce::AudioProcessorGraph::NodeID(1), editor);
    auto* curve = firstOfType<EQCurveComponent>(card);
    auto* strip = stripOf(card);
    curve->timerCallback();
    curve->setSelectedBand(0);

    juce::Slider* gain = nullptr;
    for (auto* s : bandSliders(card))
        if (card.findMidiLearnableParamForTest(s)->paramID.endsWith("Gain"))
            gain = s;
    ASSERT_NE(gain, nullptr);
    const double start = gain->getValue();
    card.driveEqKnobGlideForTest(true, 0.0);
    strip->getChip(8)->onClick();
    EXPECT_NEAR(gain->getValue(), start, 1.5) << "the knob eases rather than jumping to the new band's gain";
    card.driveEqKnobGlideForTest(true, 1000.0);
    EXPECT_NEAR(gain->getValue(), -9.0, 0.5) << "and lands on the new band's gain";
}

TEST_F(ModuleComponentTest, TheEqCardHeightMatchesItsLayoutAtFourAndTwentyFourBands) {
    for (const int count : {4, 24}) {
        SCOPED_TRACE(count);
        AudioEngine engine;
        GraphEditor editor(engine);
        ParametricEQModule eq;
        turnBandsOn(eq, count);
        ModuleComponent card(&eq, juce::AudioProcessorGraph::NodeID(1), editor);
        auto* strip = stripOf(card);
        ASSERT_NE(strip, nullptr);
        card.setSize(card.getWidth(), card.getHeight());
        card.resized();
        EXPECT_GT(strip->getHeight(), 0);
        EXPECT_GE(strip->getHeight(), strip->heightForWidth(strip->getWidth()));
        for (auto* child : card.getChildren())
            if (child->isVisible() && child != strip)
                EXPECT_FALSE(child->getBounds().intersects(strip->getBounds()) && child->getHeight() > 0 &&
                             child->getComponentID() != "")
                    << child->getComponentID();
        for (auto* child : card.getChildren())
            if (child->isVisible())
                EXPECT_LE(child->getBottom(), card.getHeight()) << child->getComponentID();
    }
}

// The card has its own solo button, beside the band toggle, on the card's curve.
TEST_F(ModuleComponentTest, TheEqCardsSoloButtonSolosTheSelectedBandAndShowsPressed) {
    AudioEngine engine;
    GraphEditor editor(engine);
    ParametricEQModule eq;
    turnBandsOn(eq, 3);
    ModuleComponent card(&eq, juce::AudioProcessorGraph::NodeID(1), editor);
    card.setSize(card.getWidth(), card.getHeight());
    auto* curve = firstOfType<EQCurveComponent>(card);
    auto* solo = firstOfType<synth::ui::EQSoloButton>(card);
    ASSERT_NE(solo, nullptr);
    EXPECT_EQ(solo->getComponentID(), "eqCardSolo");
    EXPECT_EQ(solo->getTitle(), "Solo band");
    EXPECT_TRUE(solo->getWantsKeyboardFocus());
    EXPECT_TRUE(solo->isVisible());
    EXPECT_FALSE(solo->getBounds().isEmpty());
    for (auto* t : bandToggles(card))
        EXPECT_FALSE(solo->getBounds().intersects(t->getBounds())) << "beside the toggle, not over it";
    curve->timerCallback();
    curve->setSelectedBand(1);

    EXPECT_FALSE(solo->getToggleState());
    solo->onClick();
    EXPECT_EQ(eq.getSoloBand(), 1);
    EXPECT_TRUE(solo->getToggleState());
    solo->onClick();
    EXPECT_EQ(eq.getSoloBand(), -1);
    EXPECT_FALSE(solo->getToggleState());
}

TEST_F(ModuleComponentTest, ASoloStartedByAnotherCurveOnTheEqShowsPressedOnTheCardButton) {
    AudioEngine engine;
    GraphEditor editor(engine);
    ParametricEQModule eq;
    turnBandsOn(eq, 3);
    ModuleComponent card(&eq, juce::AudioProcessorGraph::NodeID(1), editor);
    auto* curve = firstOfType<EQCurveComponent>(card);
    auto* solo = firstOfType<synth::ui::EQSoloButton>(card);
    ASSERT_NE(solo, nullptr);
    curve->timerCallback();
    curve->setSelectedBand(2);

    EQCurveComponent windowCurve(eq); // the EQ window's own curve
    windowCurve.setSelectedBand(2);
    ASSERT_TRUE(windowCurve.toggleSoloSelectedBand());
    curve->timerCallback(); // the card's 30 Hz poll
    EXPECT_TRUE(solo->getToggleState());

    solo->onClick(); // the card ends a solo it did not start
    EXPECT_EQ(eq.getSoloBand(), -1);
    EXPECT_FALSE(solo->getToggleState());
}

namespace {

juce::Component* childById(ModuleComponent& card, const juce::String& id) {
    for (auto* child : card.getChildren())
        if (child->getComponentID() == id)
            return child;
    return nullptr;
}

juce::Slider* sliderById(ModuleComponent& card, const juce::String& id) {
    for (auto* child : card.getChildren())
        if (auto* slider = dynamic_cast<juce::Slider*>(child); slider != nullptr && slider->getComponentID() == id)
            return slider;
    return nullptr;
}

juce::Component* titleLabel(ModuleComponent& card, const juce::String& text) {
    for (auto* child : card.getChildren())
        if (auto* label = dynamic_cast<juce::Label*>(child);
            label != nullptr && label->getComponentID() == "cardSectionHeader" && label->getText().startsWith(text))
            return label;
    return nullptr;
}

} // namespace

// The card never changes size when the selection changes (the lanes show one band, whichever it is), and grows only
// by the strip's chip rows: 12 bands wrap to a second row.
TEST_F(ModuleComponentTest, TheEqCardKeepsItsSizeWhenTheSelectionChangesAndGrowsOnlyByChipRows) {
    AudioEngine engine;
    GraphEditor editor(engine);
    ParametricEQModule eq;
    turnBandsOn(eq, 4);
    ModuleComponent card(&eq, juce::AudioProcessorGraph::NodeID(1), editor);
    auto* curve = firstOfType<EQCurveComponent>(card);
    curve->timerCallback();
    const int strip4 = stripOf(card)->heightForWidth(card.getWidth() - 24);
    const auto size = card.getBounds();
    const auto freqBounds =
        sliderById(card, "B1 Freq") != nullptr ? sliderById(card, "B1 Freq")->getBounds() : juce::Rectangle<int>();
    for (int band = 0; band < 4; ++band) {
        curve->setSelectedBand(band);
        card.driveEqKnobGlideForTest(true, 1000.0);
        EXPECT_EQ(card.getBounds(), size) << "band " << band + 1;
    }
    EXPECT_FALSE(freqBounds.isEmpty());

    turnBandsOn(eq, 12);
    curve->timerCallback();
    const int strip12 = stripOf(card)->heightForWidth(card.getWidth() - 24);
    EXPECT_GT(strip12, strip4) << "12 bands wrap to a second chip row";
    EXPECT_EQ(card.getHeight() - size.getHeight(), strip12 - strip4) << "the card grows by the chip rows, no more";
    EXPECT_EQ(card.getWidth(), size.getWidth());
}

// The lanes' size is fixed: what sits under the strip is at the same distance from it whichever band is selected.
TEST_F(ModuleComponentTest, TheEqCardsMainLaneIsTitledAfterTheSelectedBandAndHoldsItsControlsEvenly) {
    AudioEngine engine;
    GraphEditor editor(engine);
    ParametricEQModule eq;
    turnBandsOn(eq, 4);
    eq.setBandType(2, ParametricEQModule::BandType::Notch);
    ModuleComponent card(&eq, juce::AudioProcessorGraph::NodeID(1), editor);
    auto* curve = firstOfType<EQCurveComponent>(card);
    curve->timerCallback();
    curve->setSelectedBand(1);
    ASSERT_NE(titleLabel(card, "Band 2"), nullptr);
    EXPECT_EQ(static_cast<juce::Label*>(titleLabel(card, "Band 2"))->getText(), "Band 2");
    curve->setSelectedBand(2);
    EXPECT_EQ(static_cast<juce::Label*>(titleLabel(card, "Band 3"))->getText(), "Band 3");

    auto* freq = sliderById(card, "B3 Freq");
    auto* gain = sliderById(card, "B3 Gain");
    auto* q = sliderById(card, "B3 Q");
    ASSERT_TRUE(freq != nullptr && gain != nullptr && q != nullptr);
    const int gapA = gain->getX() - freq->getRight();
    const int gapB = q->getX() - gain->getRight();
    EXPECT_LE(std::abs(gapA - gapB), 1) << "three even knobs";
    EXPECT_GT(freq->getWidth(), 70) << "larger than the Out lane's dials";
    EXPECT_EQ(freq->getBottom(), gain->getBottom());
}

TEST_F(ModuleComponentTest, TheEqCardsSpectrumAndWindowAreIconTogglesOnTheCurvesTopRightWithTitlesAndTooltips) {
    AudioEngine engine;
    GraphEditor editor(engine);
    ParametricEQModule eq;
    turnBandsOn(eq, 4);
    ModuleComponent card(&eq, juce::AudioProcessorGraph::NodeID(1), editor);
    auto* curve = firstOfType<EQCurveComponent>(card);
    auto* spectrum = dynamic_cast<synth::ui::CardIconToggle*>([&]() -> juce::Component* {
        for (auto* child : card.getChildren())
            if (child->getTitle() == "Show Spectrum")
                return child;
        return nullptr;
    }());
    auto* window = dynamic_cast<synth::ui::CardIconToggle*>(childById(card, "eqPopOut"));
    ASSERT_NE(spectrum, nullptr);
    ASSERT_NE(window, nullptr);
    EXPECT_EQ(window->getTitle(), "Open EQ Window");
    for (auto* icon : {spectrum, window}) {
        EXPECT_TRUE(icon->isVisible());
        EXPECT_TRUE(icon->getWantsKeyboardFocus()) << icon->getTitle();
        EXPECT_TRUE(icon->getTooltip().startsWith(icon->getTitle())) << icon->getTooltip();
        const auto box = icon->getBounds();
        EXPECT_TRUE(curve->getBounds().contains(box)) << "on the curve";
        EXPECT_LT(box.getY() - curve->getY(), 8) << "in its top corner";
    }
    EXPECT_LT(curve->getRight() - window->getBounds().getRight(), 8) << "the window icon is in the right corner";
    EXPECT_LT(curve->getRight() - spectrum->getBounds().getRight(), 8 + 2 * window->getWidth());
    EXPECT_LT(spectrum->getX(), window->getX());
    EXPECT_GT(window->getX(), spectrum->getRight());

    const bool shown = curve->getShowSpectrum();
    spectrum->setToggleState(!shown, juce::dontSendNotification);
    spectrum->onClick();
    EXPECT_NE(curve->getShowSpectrum(), shown) << "the toggle turns the spectrum on and off";
    EXPECT_EQ(spectrum->getToggleState(), curve->getShowSpectrum());

    EXPECT_TRUE(window->onClick != nullptr) << "the window icon is wired to open the EQ window";
}

// Opening the window is a native window, so it needs a display (a headless runner has none and the card then does
// nothing; the card does not crash it).
TEST_F(ModuleComponentTest, TheEqCardsWindowIconOpensTheEqWindowAndOnAHeadlessHostDoesNothing) {
    AudioEngine engine;
    GraphEditor editor(engine);
    ParametricEQModule eq;
    ModuleComponent card(&eq, juce::AudioProcessorGraph::NodeID(1), editor);
    auto* window = dynamic_cast<synth::ui::CardIconToggle*>(childById(card, "eqPopOut"));
    ASSERT_NE(window, nullptr);
    if (nativeWindowsAbortHere())
        GTEST_SKIP() << "native windows abort the process under the ASAN job's Xvfb";
    window->onClick();
    if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() == nullptr) {
        SUCCEED() << "no display: the icon opened nothing and the process is alive";
        return;
    }
    EQWindow* opened = nullptr;
    for (int i = 0; i < juce::TopLevelWindow::getNumTopLevelWindows() && opened == nullptr; ++i)
        if (auto* top = dynamic_cast<juce::DialogWindow*>(juce::TopLevelWindow::getTopLevelWindow(i)))
            opened = dynamic_cast<EQWindow*>(top->getContentComponent());
    ASSERT_NE(opened, nullptr) << "the icon opens the EQ window";
    delete opened->getParentComponent();
}

// The Out lane: Mix, Pan, Output stacked bottom right as small dials, a title and a rule over them and a hairline
// between it and the Main lane that keep the shared clear space.
TEST_F(ModuleComponentTest, TheEqCardsOutLaneHoldsMixPanAndOutputStackedBottomRight) {
    namespace cb = synth::cardbody;
    AudioEngine engine;
    GraphEditor editor(engine);
    ParametricEQModule eq;
    turnBandsOn(eq, 4);
    ModuleComponent card(&eq, juce::AudioProcessorGraph::NodeID(1), editor);
    auto* mix = sliderById(card, "Mix");
    auto* pan = sliderById(card, "Pan");
    auto* output = sliderById(card, "Output");
    ASSERT_TRUE(mix != nullptr && pan != nullptr && output != nullptr);
    EXPECT_LT(mix->getY(), pan->getY());
    EXPECT_LT(pan->getY(), output->getY());
    EXPECT_EQ(mix->getX(), pan->getX());
    EXPECT_EQ(pan->getX(), output->getX());
    EXPECT_EQ(output->getWidth(), cb::kSmallKnobCellWidth);
    EXPECT_GT(mix->getX(), card.getWidth() / 2) << "on the right";
    EXPECT_LE(output->getBottom(), card.getHeight() - 8) << "at the bottom";
    EXPECT_GT(output->getBottom(), card.getHeight() - 30);
    EXPECT_TRUE(output->getTooltip().endsWith(cb::kOutputControlTip)) << output->getTooltip();
    EXPECT_EQ(pan->getTextFromValue(0.0), "C") << "Pan reads L, C, R like every Pan knob";
    EXPECT_EQ(pan->getTextFromValue(pan->getMinimum()).substring(0, 1), "L");

    auto* outTitle = titleLabel(card, "Out");
    ASSERT_NE(outTitle, nullptr);
    EXPECT_LE(outTitle->getBottom(), mix->getY()) << "the title is above the first dial";
    // Nothing of the Out lane's controls comes near the hairline between the lanes.
    juce::Component* divider = nullptr;
    for (auto* child : card.getChildren())
        if (child->getWidth() == cb::kLaneRuleWidth && child->getHeight() == cb::kLaneTitleBand + cb::kLaneBandHeight)
            divider = child;
    ASSERT_NE(divider, nullptr);
    EXPECT_GE(mix->getX() - divider->getRight(), cb::kLaneClear);
    EXPECT_GE(divider->getX() - sliderById(card, "B1 Q")->getRight(), cb::kLaneClear);
}

// The card's own Tab order: the curve's icons, the strip, the Main lane column first (on, solo, type, Freq, Gain, Q),
// then Out (Mix, Pan, Output).
TEST_F(ModuleComponentTest, TheEqCardsTabOrderIsIconsStripMainLaneThenOut) {
    AudioEngine engine;
    GraphEditor editor(engine);
    ParametricEQModule eq;
    turnBandsOn(eq, 4);
    ModuleComponent card(&eq, juce::AudioProcessorGraph::NodeID(1), editor);
    auto* curve = firstOfType<EQCurveComponent>(card);
    curve->timerCallback();
    curve->setSelectedBand(0);
    std::vector<juce::Component*> stops = card.getKeyboardControls();
    const auto indexOf = [&](const juce::String& title) {
        for (size_t i = 0; i < stops.size(); ++i)
            if (stops[i]->getTitle() == title || stops[i]->getComponentID() == title)
                return (int)i;
        return -1;
    };
    const int spectrum = indexOf("Show Spectrum");
    const int window = indexOf("Open EQ Window");
    const int on = indexOf("Band 1 on");
    const int solo = indexOf("eqCardSolo");
    const int freq = indexOf("B1 Freq");
    const int gain = indexOf("B1 Gain");
    const int q = indexOf("B1 Q");
    const int mix = indexOf("Mix");
    const int pan = indexOf("Pan");
    const int output = indexOf("Output");
    for (int stop : {spectrum, window, on, solo, freq, gain, q, mix, pan, output})
        ASSERT_GE(stop, 0);
    EXPECT_LT(spectrum, window);
    EXPECT_LT(window, on);
    EXPECT_LT(on, solo);
    EXPECT_LT(solo, freq);
    EXPECT_LT(freq, gain);
    EXPECT_LT(gain, q);
    EXPECT_LT(q, mix) << "Out comes after the whole Main lane";
    EXPECT_LT(mix, pan);
    EXPECT_LT(pan, output);
    // The strip's chips come between the curve's icons and the Main lane.
    bool chipBetween = false;
    for (int i = window + 1; i < on; ++i)
        chipBetween =
            chipBetween || dynamic_cast<synth::ui::EQBandStrip*>(stops[(size_t)i]->getParentComponent()) != nullptr;
    EXPECT_TRUE(chipBetween);
}
