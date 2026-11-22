// ModuleComponent Wavetable card tests: display/load chrome, jack columns, mod-drop targets, lane knobs.
// The lanes themselves are the card body's (layout data); their layout tests are
// Tests/UI/Graph/CardBody/DefaultLayouts/WavetableDefaultLayoutTests.cpp.

#include "AudioEngine/AudioEngine.h"
#include "ModuleComponentTestFixture.h"

#include "Modules/WavetableOscillatorModule/WavetableOscillatorModule.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Layout/LayoutUtil.h"
#include <algorithm>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <set>
#include <vector>

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
    EXPECT_GE(moduleComponent.getWidth(), synth::LayoutUtil::kDoubleWidth) << "the lane grid may widen it";
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
    ASSERT_GT((int)drawn.size(), 0);
    ASSERT_LT((int)drawn.size(), numJacks) << "the knobs on the lanes bind their CV jacks";

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
        const int mid = ((int)drawn.size() + 1) / 2; // the first column takes the extra jack of an odd count
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
    ASSERT_TRUE(position->isVisible());

    const auto hit = moduleComponent.getModTargetPortForPoint(position->getBounds().getCentre());
    ASSERT_TRUE(hit.has_value()) << "a visible knob must be a modulation drop target";
    EXPECT_TRUE(hit->isInput) << "a knob resolves to an INPUT port";
    EXPECT_FALSE(hit->isMidi);
    EXPECT_EQ(hit->index, WavetableOscillatorModule::kJackPosition)
        << "the drop must land on the parameter's own CV channel";

    // Empty card is not a drop target.
    EXPECT_FALSE(moduleComponent.getModTargetPortForPoint({moduleComponent.getWidth() - 4, 4}).has_value());

    // Every lane knob is a drop target too (no knob hides on a page any more).
    juce::Slider* octave = nullptr;
    for (auto* child : moduleComponent.getChildren())
        if (auto* s = dynamic_cast<juce::Slider*>(child))
            if (s->getComponentID() == "Octave")
                octave = s;
    ASSERT_NE(octave, nullptr);
    ASSERT_TRUE(octave->isVisible());
    EXPECT_TRUE(moduleComponent.getModTargetPortForPoint(octave->getBounds().getCentre()).has_value());
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

// The lanes show every knob at once (no tabs): each sits inside the card and has its modulation ring.
TEST_F(ModuleComponentTest, WavetableLanesShowEveryKnobInsideTheCard) {
    AudioEngine engine;
    GraphEditor editor(engine);
    WavetableOscillatorModule processor;
    ModuleComponent moduleComponent(&processor, juce::AudioProcessorGraph::NodeID(1), editor);

    int shown = 0;
    for (auto* child : moduleComponent.getChildren())
        if (auto* slider = dynamic_cast<juce::Slider*>(child); slider != nullptr && slider->isVisible()) {
            ++shown;
            EXPECT_TRUE(moduleComponent.getLocalBounds().contains(slider->getBounds()))
                << slider->getComponentID() << " is outside the card";
        }
    EXPECT_GE(shown, 15) << "Position, Warp Amt, Octave, Coarse, Fine, Phase, the Unison knobs, Sub Level, Pan, Level";
    for (const char* id : {"Position", "Octave", "Detune", "Sub"})
        EXPECT_GE(moduleComponent.getModRingSliderIndex(id), 0) << id << " keeps its modulation ring";
}
