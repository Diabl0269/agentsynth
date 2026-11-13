// The Parametric EQ card's band strip and selected-band row (docs/layout/module-card-layout.md): three real knobs and
// a toggle that follow the one selection the curve, the strip and the knobs share; the card holds the same few widgets
// at 4 and at 24 bands, and its height is what the layout computes from the strip's wrapped rows.

#include "../../Accessibility/AccessibilityAudit.h"
#include "../../Accessibility/TabOrderHelpers.h"
#include "AudioEngine/AudioEngine.h"
#include "ModuleComponentTestFixture.h"

#include "Modules/FX/ParametricEQModule.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/ModuleViews/EQBandStrip.h"
#include "UI/ModuleViews/EQCurveComponent.h"

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
