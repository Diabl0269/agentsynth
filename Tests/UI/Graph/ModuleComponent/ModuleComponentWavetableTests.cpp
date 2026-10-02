// ModuleComponent Wavetable card tests: display/load chrome, jack columns, mod-drop targets, tab paging.
// The tabs themselves are the card body's (layout data); their layout tests are
// Tests/UI/Graph/CardBody/DefaultLayouts/WavetableDefaultLayoutTests.cpp.

#include "AudioEngine/AudioEngine.h"
#include "ModuleComponentTestFixture.h"

#include "Modules/WavetableOscillatorModule/WavetableOscillatorModule.h"
#include "UI/Graph/CardWidgets/CardSegmentedSwitch.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Layout/LayoutUtil.h"
#include <algorithm>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <set>
#include <vector>

namespace {
constexpr int kTabCount = 5; // Tune, Unison, Phase, Sub, File

// The card body's tab strip: a direct child of the card.
synth::ui::CardSegmentedSwitch* findTabStrip(ModuleComponent& card) {
    for (auto* child : card.getChildren())
        if (auto* strip = dynamic_cast<synth::ui::CardSegmentedSwitch*>(child))
            if (strip->getComponentID() == "cardTabStrip")
                return strip;
    return nullptr;
}

// A real left click (mouseDown + mouseUp) at `local` on the strip: its segments take no clicks, the
// strip picks the segment under the press.
void clickStripAt(juce::Component& strip, juce::Point<int> local) {
    const auto at = local.toFloat();
    const auto event =
        juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), at,
                         juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                         &strip, &strip, juce::Time::getCurrentTime(), at, juce::Time::getCurrentTime(), 1, false);
    strip.mouseDown(event);
    strip.mouseUp(event);
}

void selectTab(ModuleComponent& card, int tab) {
    findTabStrip(card)->setSelectedIndex(tab, juce::sendNotificationSync);
}
} // namespace

// A point on a tab, hit-tested from the card, must land on the strip (nothing above it may swallow
// it), and a real click there must switch the tab without resizing the card.
TEST_F(ModuleComponentTest, WavetableTabRealClickThroughTheCardSwitchesThePage) {
    AudioEngine engine;
    GraphEditor editor(engine);
    WavetableOscillatorModule processor;
    ModuleComponent moduleComponent(&processor, juce::AudioProcessorGraph::NodeID(1), editor);
    moduleComponent.setVisible(true);

    auto* strip = findTabStrip(moduleComponent);
    ASSERT_NE(strip, nullptr);
    ASSERT_EQ(strip->getNumSegments(), kTabCount);
    const int height = moduleComponent.getHeight();

    const auto isVisibleSlider = [&](const juce::String& id) {
        for (auto* child : moduleComponent.getChildren())
            if (auto* s = dynamic_cast<juce::Slider*>(child))
                if (s->getComponentID() == id)
                    return s->isVisible();
        return false;
    };
    ASSERT_TRUE(isVisibleSlider("Octave")) << "the Tune tab should be showing first";

    for (int tab = kTabCount - 1; tab >= 0; --tab) {
        const auto segmentCentre = strip->getSegment(tab)->getBounds().getCentre();
        const auto pointOnCard = moduleComponent.getLocalPoint(strip, segmentCentre);
        EXPECT_EQ(moduleComponent.getComponentAt(pointOnCard), strip)
            << "a click on tab " << tab << " misses the strip";

        clickStripAt(*strip, segmentCentre);
        EXPECT_EQ(strip->getSelectedIndex(), tab) << "tab " << tab << " is not selected after a real click";
        EXPECT_TRUE(strip->getSegment(tab)->getToggleState());
        EXPECT_EQ(moduleComponent.getHeight(), height) << "the card resized on tab " << tab;
    }

    // Back on Tune (tab 0) after the loop, and Unison-tab knobs are hidden again.
    EXPECT_TRUE(isVisibleSlider("Octave"));
    EXPECT_FALSE(isVisibleSlider("Detune"));
}

TEST_F(ModuleComponentTest, WavetableCardBuildsDisplayAndLoadButton) {
    AudioEngine engine;
    GraphEditor editor(engine);
    WavetableOscillatorModule processor;
    ModuleComponent moduleComponent(&processor, juce::AudioProcessorGraph::NodeID(1), editor);

    WavetableDisplayComponent* display = nullptr;
    juce::TextButton* loadButton = nullptr;
    int comboCount = 0, sliderCount = 0, toggleCount = 0;

    for (auto* child : moduleComponent.getChildren()) {
        if (auto* d = dynamic_cast<WavetableDisplayComponent*>(child))
            display = d;
        else if (auto* b = dynamic_cast<juce::TextButton*>(child)) {
            if (b->getButtonText() == "Load Wavetable...")
                loadButton = b;
        } else if (dynamic_cast<juce::ComboBox*>(child) != nullptr)
            ++comboCount;
        else if (dynamic_cast<juce::Slider*>(child) != nullptr)
            ++sliderCount;
        else if (dynamic_cast<juce::ToggleButton*>(child) != nullptr)
            ++toggleCount;
    }

    ASSERT_NE(display, nullptr) << "Wavetable cards must own a WavetableDisplayComponent";
    ASSERT_NE(loadButton, nullptr) << "Wavetable cards must own a \"Load Wavetable...\" button";

    // Table/Warp/Stack/Sub Oct/Sub Wave/Sync In/Import/Interp -> 8 combos;
    // Position/Octave/Coarse/Fine/Level/Unison/Detune/Warp Amt/Phase/Rand Phase/Spread/
    // Width/Blend/Sub/Pan -> 15 sliders; Poly -> 1 toggle, plus the always-present
    // "Show Scope" toggle.
    EXPECT_EQ(comboCount, 8);
    EXPECT_EQ(sliderCount, 15);
    EXPECT_EQ(toggleCount, 2);

    // Both bespoke children must be laid out inside the card.
    EXPECT_FALSE(display->getBounds().isEmpty());
    EXPECT_FALSE(loadButton->getBounds().isEmpty());
    EXPECT_TRUE(moduleComponent.getLocalBounds().contains(display->getBounds()));
    EXPECT_TRUE(moduleComponent.getLocalBounds().contains(loadButton->getBounds()));

    // Height is deliberately not asserted here: EstimatedModuleSizesMatchTheRealComponents
    // already pins the real card against GraphEditor::estimateModuleSize for every offered
    // type, so duplicating the number would just be a second thing to update by hand.
    // Width IS asserted: the card is double-width, and the chrome beside the
    // two-column jack gutter and the body's wide-card branches (doubled knob columns, paired combos)
    // hang off that.
    EXPECT_EQ(moduleComponent.getWidth(), synth::LayoutUtil::kDoubleWidth);
    EXPECT_GT(moduleComponent.getHeight(), 100);
}

// The folder browser is the module's, but its chrome is the card's — a Wavetable card without
// the prev/next buttons leaves the browser unreachable.
TEST_F(ModuleComponentTest, WavetableCardBuildsFolderBrowserChrome) {
    AudioEngine engine;
    GraphEditor editor(engine);
    WavetableOscillatorModule processor;
    ModuleComponent moduleComponent(&processor, juce::AudioProcessorGraph::NodeID(1), editor);

    juce::TextButton* folderButton = nullptr;
    juce::TextButton* prevButton = nullptr;
    juce::TextButton* nextButton = nullptr;

    for (auto* child : moduleComponent.getChildren()) {
        if (auto* b = dynamic_cast<juce::TextButton*>(child)) {
            if (b->getButtonText() == "Folder...")
                folderButton = b;
            else if (b->getButtonText() == "<")
                prevButton = b;
            else if (b->getButtonText() == ">")
                nextButton = b;
        }
    }

    ASSERT_NE(folderButton, nullptr) << "Wavetable cards must own a \"Folder...\" button";
    ASSERT_NE(prevButton, nullptr) << "Wavetable cards must own a previous-table button";
    ASSERT_NE(nextButton, nullptr) << "Wavetable cards must own a next-table button";

    for (auto* b : {folderButton, prevButton, nextButton}) {
        EXPECT_FALSE(b->getBounds().isEmpty());
        EXPECT_TRUE(moduleComponent.getLocalBounds().contains(b->getBounds()));
    }

    // Clicking next with no folder selected must not crash or throw — it just reports back.
    EXPECT_NO_THROW(nextButton->triggerClick());
}

// The CV jacks run in two columns so the gutter stops dictating the card height -- but BOTH stay on
// the left. Inputs-left / outputs-right is what makes signal flow read left to right, and splitting
// inputs across both edges costs more in comprehension than the height saves.
//
// This only holds for the DRAWN (non-knob-bound) jacks. A CV jack whose target resolves to a bound,
// VISIBLE knob outside the tabs (only the pinned Position and Warp Amt knobs --
// docs/modules/modulation.md#drag-to-knob-modulation) draws no gutter dot at all; getPortCenter(i, true)
// for that `i` returns the knob's own landing anchor instead, which legitimately sits wherever that knob
// is in the body. A knob on a tab never binds its jack, so the gutter is the same on every tab.
// `drawnInputJackIndices()` is the one list of which indices are still real gutter jacks.
TEST_F(ModuleComponentTest, WavetableCardKeepsEveryInputJackOnTheLeft) {
    AudioEngine engine;
    GraphEditor editor(engine);
    WavetableOscillatorModule processor;
    ModuleComponent moduleComponent(&processor, juce::AudioProcessorGraph::NodeID(1), editor);

    const int numJacks = processor.getVisibleInputPortCount();
    const int numOuts = processor.getVisibleOutputPortCount();
    ASSERT_EQ(numJacks, WavetableOscillatorModule::kNumJacks);

    const auto drawn = moduleComponent.drawnInputJackIndices();
    ASSERT_EQ((int)drawn.size(), numJacks - 2) << "exactly the pinned Position and Warp Amt CV jacks are knob-bound";

    std::set<std::pair<int, int>> seen;
    std::set<int> columns;
    for (int i : drawn) {
        const auto p = moduleComponent.getPortCenter(i, true);
        EXPECT_TRUE(seen.insert({p.x, p.y}).second) << "jack " << i << " overlaps another jack";
        EXPECT_TRUE(moduleComponent.getLocalBounds().contains(p)) << "jack " << i << " sits outside the card";
        EXPECT_LT(p.x, moduleComponent.getWidth() / 2) << "input jack " << i << " must stay on the left half";
        columns.insert(p.x);
    }
    EXPECT_LE(columns.size(), 2u) << "expected at most two jack columns among the drawn jacks";

    // Every knob-bound jack (excluded above) must still resolve to SOME point on the card -- it
    // simply is not a gutter jack any more.
    for (int i = 0; i < numJacks; ++i) {
        if (std::find(drawn.begin(), drawn.end(), i) != drawn.end())
            continue;
        const auto p = moduleComponent.getPortCenter(i, true);
        EXPECT_TRUE(moduleComponent.getLocalBounds().contains(p)) << "knob-bound jack " << i << " landed off-card";
    }

    // Outputs keep the right edge to themselves.
    for (int o = 0; o < numOuts; ++o)
        EXPECT_GT(moduleComponent.getPortCenter(o, false).x, moduleComponent.getWidth() / 2);

    // Column-major: the first half of the DRAWN jacks runs down column 0, so the first and the
    // midpoint-of-drawn jack share a row (only meaningful once there are enough drawn jacks to
    // actually fill two columns).
    if (drawn.size() > 10) {
        const int mid = (int)drawn.size() / 2;
        EXPECT_EQ(moduleComponent.getPortCenter(drawn[0], true).y, moduleComponent.getPortCenter(drawn[mid], true).y);
        EXPECT_LT(moduleComponent.getPortCenter(drawn[0], true).x, moduleComponent.getPortCenter(drawn[mid], true).x);
    }

    // The body must clear the LOWEST drawn jack, which is not necessarily the last one -- a
    // knob-bound jack's anchor sits inside the body itself, so it must not be folded into this max.
    int lowest = 0;
    for (int i : drawn)
        lowest = std::max(lowest, moduleComponent.getPortCenter(i, true).y);
    for (auto* child : moduleComponent.getChildren())
        if (child->isVisible() && dynamic_cast<juce::Slider*>(child) != nullptr)
            EXPECT_GT(child->getY(), lowest) << "a knob overlaps the jack gutter";
}

// Serum-style modulation drop: releasing a cable on a KNOB resolves to that parameter's CV jack,
// so a 16-jack module can be patched without aiming at the gutter at all.
TEST_F(ModuleComponentTest, KnobsResolveToTheirCVJackAsModulationDropTargets) {
    AudioEngine engine;
    GraphEditor editor(engine);
    WavetableOscillatorModule processor;
    ModuleComponent moduleComponent(&processor, juce::AudioProcessorGraph::NodeID(1), editor);

    juce::Slider* position = nullptr;
    for (auto* child : moduleComponent.getChildren())
        if (auto* s = dynamic_cast<juce::Slider*>(child))
            if (s->getComponentID() == "Position")
                position = s;
    ASSERT_NE(position, nullptr);
    ASSERT_TRUE(position->isVisible()) << "Position is pinned, so it shows on every page";

    const auto hit = moduleComponent.getModTargetPortForPoint(position->getBounds().getCentre());
    ASSERT_TRUE(hit.has_value()) << "a visible knob must be a modulation drop target";
    EXPECT_TRUE(hit->isInput) << "a knob resolves to an INPUT port";
    EXPECT_FALSE(hit->isMidi);
    EXPECT_EQ(hit->index, WavetableOscillatorModule::kJackPosition)
        << "the drop must land on the parameter's own CV channel";

    // Empty card is not a drop target.
    EXPECT_FALSE(moduleComponent.getModTargetPortForPoint({moduleComponent.getWidth() - 4, 4}).has_value());

    // A knob on a hidden page keeps its bounds, so it must not swallow a drop aimed elsewhere.
    juce::Slider* octave = nullptr;
    for (auto* child : moduleComponent.getChildren())
        if (auto* s = dynamic_cast<juce::Slider*>(child))
            if (s->getComponentID() == "Octave")
                octave = s;
    ASSERT_NE(octave, nullptr);
    const auto octaveCentre = octave->getBounds().getCentre();
    ASSERT_TRUE(moduleComponent.getModTargetPortForPoint(octaveCentre).has_value());

    selectTab(moduleComponent, 1);

    EXPECT_FALSE(moduleComponent.getModTargetPortForPoint(octaveCentre).has_value())
        << "a knob whose tab is not selected must not accept a modulation drop";
}

// The highlight is what makes the drop aimed rather than guessed at.
TEST_F(ModuleComponentTest, ModulationDropTargetHighlightTracksTheHoveredKnob) {
    AudioEngine engine;
    GraphEditor editor(engine);
    WavetableOscillatorModule processor;
    ModuleComponent moduleComponent(&processor, juce::AudioProcessorGraph::NodeID(1), editor);

    EXPECT_EQ(moduleComponent.getModDropTargetChannel(), -1);

    EXPECT_TRUE(moduleComponent.setModDropTargetChannel(WavetableOscillatorModule::kJackPosition));
    EXPECT_EQ(moduleComponent.getModDropTargetChannel(), WavetableOscillatorModule::kJackPosition);

    // Setting the same target again is not a change, so the caller can skip the repaint.
    EXPECT_FALSE(moduleComponent.setModDropTargetChannel(WavetableOscillatorModule::kJackPosition));

    EXPECT_TRUE(moduleComponent.setModDropTargetChannel(-1));
    EXPECT_EQ(moduleComponent.getModDropTargetChannel(), -1);

    // Painting with a highlight set must not crash (headless: no themed LookAndFeel).
    moduleComponent.setModDropTargetChannel(WavetableOscillatorModule::kJackPosition);
    juce::Image img(juce::Image::ARGB, moduleComponent.getWidth(), moduleComponent.getHeight(), true);
    juce::Graphics g(img);
    EXPECT_NO_THROW(moduleComponent.paint(g));
}

// Controls are on tabs. Switching tabs must not resize the card -- a card that grew and shrank would
// shove its neighbours around the canvas on every tab click.
TEST_F(ModuleComponentTest, WavetableTabsSwitchContentWithoutResizingTheCard) {
    AudioEngine engine;
    GraphEditor editor(engine);
    WavetableOscillatorModule processor;
    ModuleComponent moduleComponent(&processor, juce::AudioProcessorGraph::NodeID(1), editor);

    ASSERT_NE(findTabStrip(moduleComponent), nullptr);

    const auto visibleSliderNames = [&] {
        std::set<juce::String> names;
        for (auto* child : moduleComponent.getChildren())
            if (auto* s = dynamic_cast<juce::Slider*>(child))
                if (s->isVisible())
                    names.insert(s->getComponentID());
        return names;
    };

    const int height = moduleComponent.getHeight();
    const auto firstPage = visibleSliderNames();

    // Position and Warp Amt are pinned above the strip, so they survive every tab switch.
    EXPECT_TRUE(firstPage.count("Position")) << "Position must stay pinned";
    EXPECT_TRUE(firstPage.count("Warp Amt")) << "Warp Amt must stay pinned";

    for (int i = 1; i < kTabCount; ++i) {
        selectTab(moduleComponent, i);
        EXPECT_EQ(moduleComponent.getHeight(), height) << "the card resized on tab " << i;

        const auto page = visibleSliderNames();
        EXPECT_TRUE(page.count("Position")) << "Position vanished on tab " << i;
        EXPECT_TRUE(page.count("Warp Amt")) << "Warp Amt vanished on tab " << i;
        EXPECT_NE(page, firstPage) << "tab " << i << " shows the same controls as the first tab";

        // Whatever is showing must be laid out inside the card.
        for (auto* child : moduleComponent.getChildren())
            if (child->isVisible() && dynamic_cast<juce::Slider*>(child) != nullptr)
                EXPECT_TRUE(moduleComponent.getLocalBounds().contains(child->getBounds()))
                    << child->getComponentID() << " is outside the card on tab " << i;
    }

    // Every knob must be reachable from some tab -- a control on no tab is unusable.
    std::set<juce::String> everSeen;
    for (int i = 0; i < kTabCount; ++i) {
        selectTab(moduleComponent, i);
        for (const auto& n : visibleSliderNames())
            everSeen.insert(n);
    }
    int totalSliders = 0;
    for (auto* child : moduleComponent.getChildren())
        if (dynamic_cast<juce::Slider*>(child) != nullptr)
            ++totalSliders;
    EXPECT_EQ((int)everSeen.size(), totalSliders) << "some knob is not reachable from any tab";
}

// A modulation ring is drawn from its knob's bounds. A knob on an unselected tab keeps the bounds it
// had when its tab was last laid out, so without this guard the ring would keep painting on empty
// card after a tab switch -- an orange arc floating with no knob under it.
TEST_F(ModuleComponentTest, ModulationRingsSkipKnobsOnInactiveTabPages) {
    AudioEngine engine;
    GraphEditor editor(engine);
    WavetableOscillatorModule processor;
    ModuleComponent moduleComponent(&processor, juce::AudioProcessorGraph::NodeID(1), editor);

    ASSERT_NE(findTabStrip(moduleComponent), nullptr);

    // Tab 0 (Tune) owns Octave; Position is pinned above the strip.
    EXPECT_GE(moduleComponent.getModRingSliderIndex("Octave"), 0);
    EXPECT_GE(moduleComponent.getModRingSliderIndex("Position"), 0);

    selectTab(moduleComponent, 1);

    EXPECT_EQ(moduleComponent.getModRingSliderIndex("Octave"), -1)
        << "a ring must not be drawn for a knob whose tab is not selected";
    EXPECT_GE(moduleComponent.getModRingSliderIndex("Position"), 0) << "a pinned knob keeps its ring on every tab";

    // A parameter with no knob at all never gets a ring.
    EXPECT_EQ(moduleComponent.getModRingSliderIndex("Not A Parameter"), -1);
}
