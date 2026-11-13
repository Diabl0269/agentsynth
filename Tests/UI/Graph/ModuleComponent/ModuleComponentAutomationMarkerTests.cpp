// The automation-lane marker on a module card (docs/layout/module-card.md#automated-marker): a knob whose parameter
// has a lane shows a glyph at its top-left corner, its tooltip names the lane, a screen reader hears "automated",
// and all of it goes when the lane does. The card asks the host once per gated tick, like the MIDI badge.

#include "AudioEngine/AudioEngine.h"
#include "ModuleComponentTestFixture.h"

#include "Modules/FilterModule.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Layout/AutomatedMarker.h"
#include "UI/MidiRemote/MidiLearnMenu.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <set>

namespace {

juce::Slider* cutoffKnob(ModuleComponent& card) {
    for (auto* child : card.getChildren())
        if (child->getComponentID() == "Cutoff")
            return dynamic_cast<juce::Slider*>(child);
    return nullptr;
}

// The card painted onto a software image; the clock is pinned past the fade so a marker is at full strength.
juce::Image paintCard(ModuleComponent& card) {
    juce::Image image(juce::Image::ARGB, card.getWidth(), card.getHeight(), true, juce::SoftwareImageType());
    juce::Graphics g(image);
    card.paintEntireComponent(g, false);
    return image;
}

int differingPixels(const juce::Image& a, const juce::Image& b, juce::Rectangle<int> area) {
    int n = 0;
    for (int y = area.getY(); y < area.getBottom(); ++y)
        for (int x = area.getX(); x < area.getRight(); ++x)
            if (a.getPixelAt(x, y) != b.getPixelAt(x, y))
                ++n;
    return n;
}

struct ClockPin {
    explicit ClockPin(double ms) { synth::ui::AutomatedMarkerFade::setClockForTest(ms); }
    ~ClockPin() { synth::ui::AutomatedMarkerFade::setClockForTest(std::nullopt); }
    void set(double ms) { synth::ui::AutomatedMarkerFade::setClockForTest(ms); }
};

struct Fixture {
    AudioEngine engine;
    GraphEditor editor{engine};
    FilterModule filter;
    ModuleComponent card{&filter, juce::AudioProcessorGraph::NodeID(1), editor};
    std::set<juce::String> automated;
    juce::Slider* cutoff = nullptr;

    Fixture() {
        card.setSize(280, 500);
        editor.onQueryAutomatedParamsForNode = [this](juce::AudioProcessorGraph::NodeID) { return automated; };
        cutoff = cutoffKnob(card);
    }
};

} // namespace

TEST_F(ModuleComponentTest, AutomatedKnobGainsTheMarkerTooltipLineAndDescriptionThenLosesThem) {
    ClockPin clock(1000.0);
    Fixture f;
    ASSERT_NE(f.cutoff, nullptr);
    const auto before = f.cutoff->getTooltip();

    f.card.timerCallback();
    EXPECT_FALSE(f.card.isAutomatedMarkerShownForTest(f.cutoff)) << "no lane yet";

    f.automated = {"cutoff"};
    f.card.timerCallback();
    EXPECT_TRUE(f.card.isAutomatedMarkerShownForTest(f.cutoff));
    EXPECT_TRUE(f.cutoff->getTooltip().contains("Automated: Cutoff")) << f.cutoff->getTooltip();
    EXPECT_TRUE(f.cutoff->getTooltip().startsWith(before));
    EXPECT_TRUE(f.cutoff->getDescription().containsIgnoreCase("automated"));

    f.automated.clear();
    f.card.timerCallback();
    EXPECT_FALSE(f.card.isAutomatedMarkerShownForTest(f.cutoff));
    EXPECT_EQ(f.cutoff->getTooltip(), before);
    EXPECT_FALSE(f.cutoff->getDescription().containsIgnoreCase("automated"));
}

TEST_F(ModuleComponentTest, AutomatedMarkerPaintsAtTheTopLeftOfTheKnobOnlyWhileAutomated) {
    ClockPin clock(1000.0);
    Fixture f;
    ASSERT_NE(f.cutoff, nullptr);
    const auto cell = f.cutoff->getBounds();
    const auto markerBox = synth::ui::automatedMarkerRect(cell);

    f.card.timerCallback();
    const auto plain = paintCard(f.card);

    f.automated = {"cutoff"};
    f.card.timerCallback();
    clock.set(1500.0); // well past the 160 ms fade
    const auto marked = paintCard(f.card);
    EXPECT_GT(differingPixels(plain, marked, markerBox), 0) << "the glyph is drawn in the knob's top-left corner";
    EXPECT_EQ(differingPixels(plain, marked, juce::Rectangle<int>(cell.getCentreX() - 4, cell.getCentreY() - 4, 8, 8)),
              0)
        << "and nowhere else on the knob";
    EXPECT_EQ(differingPixels(plain, marked, synth::ui::midilearn::midiMappedDotRect(cell)), 0)
        << "the top-right stays the MIDI dot's";

    f.automated.clear();
    f.card.timerCallback();
    clock.set(2500.0);
    EXPECT_EQ(differingPixels(plain, paintCard(f.card), markerBox), 0) << "the lane is gone, so is the glyph";
}

TEST_F(ModuleComponentTest, AutomatedMarkerFadesInInsteadOfAppearing) {
    ClockPin clock(1000.0);
    Fixture f;
    ASSERT_NE(f.cutoff, nullptr);
    f.automated = {"cutoff"};
    f.card.timerCallback();
    EXPECT_EQ(f.card.automatedMarkerLevelForTest(f.cutoff), 0.0f) << "starts invisible";
    clock.set(1070.0);
    const float mid = f.card.automatedMarkerLevelForTest(f.cutoff);
    EXPECT_GT(mid, 0.0f);
    EXPECT_LT(mid, 1.0f);
    clock.set(1200.0);
    EXPECT_EQ(f.card.automatedMarkerLevelForTest(f.cutoff), 1.0f);
}

TEST_F(ModuleComponentTest, TimerTickWithNoAutomationHostWiredIsHarmless) {
    AudioEngine engine;
    GraphEditor editor(engine);
    FilterModule filter;
    ModuleComponent card(&filter, juce::AudioProcessorGraph::NodeID(1), editor);
    card.setSize(280, 500);
    card.timerCallback();
    EXPECT_FALSE(card.isAutomatedMarkerShownForTest(cutoffKnob(card)));
}
