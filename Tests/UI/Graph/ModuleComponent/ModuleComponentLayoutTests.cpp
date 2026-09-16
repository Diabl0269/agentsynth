// ModuleComponent layout/sizing tests: initialization, estimated-size parity, knob grid, per-module layout branches.

#include "ModuleComponentTestFixture.h"

#include "AI/AIStateMapper/AIStateMapper.h"
#include "Modules/ADSRModule.h"
#include "Modules/OscillatorModule.h"
#include "Modules/SamplerModule.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Library/ModuleLibraryComponent/ModuleLibraryComponent.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

TEST_F(ModuleComponentTest, InitializationAndResizing) {
    AudioEngine engine;
    GraphEditor editor(engine);
    OscillatorModule processor;
    ModuleComponent moduleComponent(&processor, juce::AudioProcessorGraph::NodeID(1), editor);

    EXPECT_NO_THROW(moduleComponent.setSize(200, 300));
}

// The drag ghost shown while dragging out of the library must match the component the drop actually
// creates, otherwise the ghost lies about where the module will land.
TEST_F(ModuleComponentTest, EstimatedModuleSizesMatchTheRealComponents) {
    // Driven off the library itself rather than a parallel hand-kept list, so a module added to the
    // library is covered automatically. (Bitcrusher and Pitch Shifter reached main without estimates
    // precisely because a hardcoded list here would not have noticed them.)
    ModuleLibraryComponent library;
    const juce::StringArray libraryTypes = library.getDraggableModuleNames();
    ASSERT_GT(libraryTypes.size(), 15) << "expected the full library list";

    AudioEngine engine;
    GraphEditor editor(engine);

    for (const auto& type : libraryTypes) {
        auto processor = synth::AIStateMapper::createModule(type);
        ASSERT_NE(processor, nullptr) << type << " is offered by the library but has no factory entry";

        ModuleComponent comp(processor.get(), juce::AudioProcessorGraph::NodeID(1), editor);
        const auto estimate = GraphEditor::estimateModuleSize(type);

        EXPECT_EQ(estimate.x, comp.getWidth()) << "estimateModuleSize(\"" << type << "\") width";
        EXPECT_EQ(estimate.y, comp.getHeight()) << "estimateModuleSize(\"" << type << "\") height";
    }
}

// Track In is deliberately absent from the library, so the loop above cannot cover it —
// but estimateModuleSize is still queried for it programmatically (the timeline's add-track flow
// places the node), and a stale estimate there misplaces the card. Same assertion, one type.
TEST_F(ModuleComponentTest, TrackInEstimatedSizeMatchesTheRealComponent) {
    ModuleLibraryComponent library;
    EXPECT_FALSE(library.getDraggableModuleNames().contains("Track In"))
        << "Track In is internal-only and must stay out of the module library";

    auto processor = synth::AIStateMapper::createModule("Track In");
    ASSERT_NE(processor, nullptr);

    AudioEngine engine;
    GraphEditor editor(engine);
    ModuleComponent comp(processor.get(), juce::AudioProcessorGraph::NodeID(1), editor);

    const auto estimate = GraphEditor::estimateModuleSize("Track In");
    EXPECT_EQ(estimate.x, comp.getWidth());
    EXPECT_EQ(estimate.y, comp.getHeight());
}

// Three knobs per row (the body sits below the ports, so it can use nearly the full card width).
TEST_F(ModuleComponentTest, KnobsAreLaidOutThreePerRow) {
    AudioEngine engine;
    GraphEditor editor(engine);
    SamplerModule processor; // 7 float/int params -> 3 rows of 3, 3, 1
    ModuleComponent moduleComponent(&processor, juce::AudioProcessorGraph::NodeID(1), editor);

    std::vector<juce::Slider*> knobs;
    for (auto* child : moduleComponent.getChildren())
        if (auto* slider = dynamic_cast<juce::Slider*>(child))
            knobs.push_back(slider);

    ASSERT_EQ(knobs.size(), 7u);

    // Children are added in parameter order, so the first three share a row.
    EXPECT_EQ(knobs[0]->getY(), knobs[1]->getY());
    EXPECT_EQ(knobs[1]->getY(), knobs[2]->getY());
    EXPECT_LT(knobs[0]->getX(), knobs[1]->getX());
    EXPECT_LT(knobs[1]->getX(), knobs[2]->getX());

    // The fourth wraps to a new row, back at the first column.
    EXPECT_GT(knobs[3]->getY(), knobs[2]->getY());
    EXPECT_EQ(knobs[3]->getX(), knobs[0]->getX());
}

// The ADSR has its own layout branch that used to position only its four sliders and return,
// leaving every auto-generated toggle at default (0,0,0,0) bounds. That made the "Poly" checkbox
// invisible and unclickable, so poly mode was unreachable on this module from the UI.
TEST_F(ModuleComponentTest, AdsrPolyToggleIsLaidOutInsideTheModule) {
    AudioEngine engine;
    GraphEditor editor(engine);
    ADSRModule processor;
    ModuleComponent moduleComponent(&processor, juce::AudioProcessorGraph::NodeID(1), editor);

    juce::ToggleButton* polyToggle = nullptr;
    for (auto* child : moduleComponent.getChildren())
        if (auto* toggle = dynamic_cast<juce::ToggleButton*>(child))
            if (toggle->getComponentID() == "Poly")
                polyToggle = toggle;

    ASSERT_NE(polyToggle, nullptr) << "ADSR exposes a 'poly' parameter, so it must render a Poly toggle";
    EXPECT_FALSE(polyToggle->getBounds().isEmpty()) << "Poly toggle must be given real bounds, not (0,0,0,0)";
    EXPECT_TRUE(moduleComponent.getLocalBounds().contains(polyToggle->getBounds()))
        << "Poly toggle must sit inside the module's bounds to be visible and clickable";
}

// FRO110 added hold/attackCurve/decayCurve/releaseCurve, taking ADSR from 4 float sliders to 8 —
// the layout used to place every slider on one row (`x = margin + 10 + i * sliderWidth`), which
// ran the last four off the right edge of the fixed 280px-wide card, making HOLD and the curve
// controls clipped and unreachable. The fix wraps sliders into rows of at most 4.
TEST_F(ModuleComponentTest, AdsrSlidersWrapIntoRowsThatFitTheModule) {
    AudioEngine engine;
    GraphEditor editor(engine);
    ADSRModule processor;
    ModuleComponent moduleComponent(&processor, juce::AudioProcessorGraph::NodeID(1), editor);

    std::vector<juce::Slider*> adsrSliders;
    for (auto* child : moduleComponent.getChildren())
        if (auto* slider = dynamic_cast<juce::Slider*>(child))
            adsrSliders.push_back(slider);

    ASSERT_EQ(adsrSliders.size(), 8u) << "attack/decay/sustain/release/hold/attackCurve/decayCurve/releaseCurve";

    const auto moduleBounds = moduleComponent.getLocalBounds();
    int maxSliderBottom = 0;
    for (auto* slider : adsrSliders) {
        EXPECT_TRUE(moduleBounds.contains(slider->getBounds()))
            << "slider '" << slider->getComponentID() << "' bounds " << slider->getBounds().toString()
            << " must sit fully inside the module's own bounds " << moduleBounds.toString()
            << " -- a slider running past the module's width is clipped and unreachable";
        maxSliderBottom = juce::jmax(maxSliderBottom, slider->getBounds().getBottom());
    }

    // Every auto-generated toggle (e.g. "Poly") must be laid out below the last slider row, not
    // overlapping it, and the module must be tall enough to contain both the last row and every
    // toggle. Skip toggles with no component ID: those are generic chrome (e.g. "Show Scope",
    // ModuleComponent.cpp's ScopeComponent toggle) positioned by the default body layout that the
    // ADSR branch never calls, not one of ADSRModule's own bool parameters.
    for (auto* child : moduleComponent.getChildren()) {
        if (auto* toggle = dynamic_cast<juce::ToggleButton*>(child)) {
            if (toggle->getComponentID().isEmpty())
                continue;
            EXPECT_GE(toggle->getBounds().getY(), maxSliderBottom)
                << "toggle '" << toggle->getComponentID() << "' must sit below the slider rows";
            EXPECT_TRUE(moduleBounds.contains(toggle->getBounds()))
                << "toggle '" << toggle->getComponentID() << "' must sit inside the module's bounds";
        }
    }

    EXPECT_GE(moduleComponent.getHeight(), maxSliderBottom)
        << "module must be tall enough to contain the last slider row";
}
