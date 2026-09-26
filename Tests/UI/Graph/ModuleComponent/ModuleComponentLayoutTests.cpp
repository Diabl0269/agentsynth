// ModuleComponent layout/sizing tests: initialization, estimated-size parity, knob grid, per-module layout branches.

#include "AudioEngine/AudioEngine.h"
#include "ModuleComponentTestFixture.h"

#include "AI/AIStateMapper/AIStateMapper.h"
#include "Modules/ADSRModule.h"
#include "Modules/ModuleBase.h"
#include "Modules/OscillatorModule.h"
#include "Modules/SamplerModule.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Library/ModuleLibraryComponent/ModuleLibraryComponent.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <cstdlib>
#include <iostream>
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

// FRO112: the ADSR card no longer has its own bespoke slider-grid branch. Its five remaining
// knobs (attack/hold/decay/sustain/release — attackCurve/decayCurve/releaseCurve moved onto the
// envelope graph's bend handles, see ModuleComponentEnvelopeCardTests.cpp) flow through the
// generic layout every other module uses: the "Poly" toggle and the Threshold control (both
// generic auto-UI, laid out before the knob grid on every module that has them) sit ABOVE the
// knobs, which then wrap into two rows (3, then 2) — the envelope graph's own disclosure row
// comes last, below the knob grid.
TEST_F(ModuleComponentTest, AdsrKnobsWrapIntoTheGenericThreePerRowGrid) {
    AudioEngine engine;
    GraphEditor editor(engine);
    ADSRModule processor;
    ModuleComponent moduleComponent(&processor, juce::AudioProcessorGraph::NodeID(1), editor);

    std::vector<juce::Slider*> adsrSliders;
    for (auto* child : moduleComponent.getChildren())
        if (auto* slider = dynamic_cast<juce::Slider*>(child))
            adsrSliders.push_back(slider);

    ASSERT_EQ(adsrSliders.size(), 5u) << "attack/hold/decay/sustain/release";

    const auto moduleBounds = moduleComponent.getLocalBounds();
    int maxSliderBottom = 0;
    for (auto* slider : adsrSliders) {
        EXPECT_EQ(slider->getSliderStyle(), juce::Slider::RotaryHorizontalVerticalDrag)
            << "slider '" << slider->getComponentID() << "' must be a rotary knob, not the old vertical style";
        EXPECT_EQ(slider->getTextBoxPosition(), juce::Slider::TextBoxAbove)
            << "slider '" << slider->getComponentID() << "' readout must sit above the dial";
        EXPECT_TRUE(moduleBounds.contains(slider->getBounds()))
            << "slider '" << slider->getComponentID() << "' bounds " << slider->getBounds().toString()
            << " must sit fully inside the module's own bounds " << moduleBounds.toString()
            << " -- a slider running past the module's width is clipped and unreachable";
        maxSliderBottom = juce::jmax(maxSliderBottom, slider->getBounds().getBottom());
    }

    // Row wrap: the first three share a row (added in parameter order: attack/hold/decay), the
    // remaining two (sustain/release) start a new row back at the first column.
    EXPECT_EQ(adsrSliders[0]->getY(), adsrSliders[1]->getY());
    EXPECT_EQ(adsrSliders[1]->getY(), adsrSliders[2]->getY());
    EXPECT_GT(adsrSliders[3]->getY(), adsrSliders[2]->getY());
    EXPECT_EQ(adsrSliders[3]->getX(), adsrSliders[0]->getX());

    // "Poly" is generic auto-UI too, but it (and the Threshold control) are laid out before the
    // knob grid on every module that has them, not after — this asserts the real relationship
    // rather than assuming knobs come first.
    juce::ToggleButton* polyToggle = nullptr;
    for (auto* child : moduleComponent.getChildren())
        if (auto* toggle = dynamic_cast<juce::ToggleButton*>(child))
            if (toggle->getComponentID() == "Poly")
                polyToggle = toggle;
    ASSERT_NE(polyToggle, nullptr);
    EXPECT_LE(polyToggle->getBounds().getBottom(), adsrSliders[0]->getBounds().getY())
        << "Poly toggle must sit above the knob grid, not overlap it";
    EXPECT_TRUE(moduleBounds.contains(polyToggle->getBounds()));

    // The envelope graph's own disclosure toggle ("Show Envelope Graph") is the one generic
    // toggle that DOES sit below the knob grid.
    juce::ToggleButton* graphToggle = nullptr;
    for (auto* child : moduleComponent.getChildren())
        if (auto* toggle = dynamic_cast<juce::ToggleButton*>(child))
            if (toggle->getButtonText() == "Show Envelope Graph")
                graphToggle = toggle;
    ASSERT_NE(graphToggle, nullptr) << "ADSR must offer a way to open the envelope graph";
    EXPECT_GE(graphToggle->getBounds().getY(), maxSliderBottom)
        << "the graph disclosure toggle must sit below the knob rows";
    EXPECT_TRUE(moduleBounds.contains(graphToggle->getBounds()));

    EXPECT_GE(moduleComponent.getHeight(), maxSliderBottom)
        << "module must be tall enough to contain the last knob row";
}

namespace {
juce::Slider* findAdsrSlider(ModuleComponent& moduleComponent, const juce::String& componentId) {
    for (auto* child : moduleComponent.getChildren())
        if (auto* slider = dynamic_cast<juce::Slider*>(child))
            if (slider->getComponentID() == componentId)
                return slider;
    return nullptr;
}
} // namespace

// FRO110 fix (skew-regression): attack/hold/decay/release keep a LINEAR parameter range (see
// ADSRModule.h/docs/modules/modules.md#adsr-envelope-module) so AIStateMapper's untrusted rescale heuristic is
// unaffected, but the knob must still feel skewed at the 1 ms attack default. That skew lives on the SLIDER, applied
// AFTER its SliderParameterAttachment is built -- setting it before (or relying on NormalisableRange::skew once
// SliderParameterAttachment has installed its own lambda-based range) is a silent no-op in JUCE, so this pins the
// slider's actual runtime behaviour, not just that some setSkewFactor call happened.
TEST_F(ModuleComponentTest, AdsrTimeSlidersAreSkewedButParameterRangeStaysLinear) {
    AudioEngine engine;
    GraphEditor editor(engine);
    ADSRModule processor;
    ModuleComponent moduleComponent(&processor, juce::AudioProcessorGraph::NodeID(1), editor);

    for (const char* timeSliderName : {"Attack", "Hold", "Decay", "Release"}) {
        auto* slider = findAdsrSlider(moduleComponent, timeSliderName);
        ASSERT_NE(slider, nullptr) << timeSliderName;
        EXPECT_NEAR(slider->getSkewFactor(), 0.3, 1.0e-9) << timeSliderName << " must be skewed for knob feel";

        // Skew formula: convertFrom0to1(0.5) == start + (end-start) * exp(ln(0.5)/skew). For a
        // linear 0..5 range this would be 2.5 -- if the skew were silently dropped (the exact
        // regression this test guards against), this would read 2.5 instead of ~0.497.
        const double midpointValue = slider->proportionOfLengthToValue(0.5);
        EXPECT_NEAR(midpointValue, 0.4966, 0.01)
            << timeSliderName << " proportionOfLengthToValue(0.5) must reflect the 0.3 skew, not a linear mapping";
    }

    // Sustain must stay linear on the slider too -- only the four time params get the UI-side
    // skew. (The three curve params -- attackCurve/decayCurve/releaseCurve -- are no longer
    // sliders at all as of FRO112; they're edited via the envelope graph's bend handles.)
    auto* sustainSlider = findAdsrSlider(moduleComponent, "Sustain");
    ASSERT_NE(sustainSlider, nullptr);
    EXPECT_NEAR(sustainSlider->getSkewFactor(), 1.0, 1.0e-9) << "Sustain must stay linear";

    for (const char* curveParamName : {"Attack Curve", "Decay Curve", "Release Curve"})
        EXPECT_EQ(findAdsrSlider(moduleComponent, curveParamName), nullptr)
            << curveParamName << " must not be its own knob -- it's edited via the envelope graph";

    // The parameter's own range must stay linear regardless of the slider's skew -- this is the
    // actual FRO110 fix: AIStateMapper's untrusted rescale reads the PARAMETER's range, never the
    // slider's.
    for (const char* paramId : {"attack", "hold", "decay", "release"}) {
        auto* param = dynamic_cast<juce::AudioParameterFloat*>(findParameterByID(&processor, paramId));
        ASSERT_NE(param, nullptr) << paramId;
        EXPECT_NEAR(param->getNormalisableRange().skew, 1.0f, 1.0e-6f)
            << paramId << "'s own NormalisableRange must stay linear";
    }
}

// Renders the ADSR card to a juce::Image headlessly so a human can eyeball the row-wrapped
// slider layout (see AdsrSlidersWrapIntoRowsThatFitTheModule above) without driving the real GUI
// app -- two app instances would collide over the same bundle id in this environment. The image
// content assertion always runs; the PNG is only written to disk when ADSR_CARD_PNG is set, so a
// normal CI run never touches the filesystem for this.
TEST_F(ModuleComponentTest, AdsrCardRendersToPngForVisualInspection) {
    AudioEngine engine;
    GraphEditor editor(engine);
    ADSRModule processor;
    ModuleComponent moduleComponent(&processor, juce::AudioProcessorGraph::NodeID(1), editor);

    // Install the app's real LookAndFeel before painting -- without it the card renders as flat
    // default JUCE grey instead of the themed look the real app shows (see
    // ModuleComponentPaintTests.cpp's WavetableCardPaintsAndTicksWithoutCrashing /
    // MidiKeyboardKeysFollowThemeChange for the same pattern).
    synth::theme::AppLookAndFeel lf;
    moduleComponent.setLookAndFeel(&lf);

    const int width = moduleComponent.getWidth();
    const int height = moduleComponent.getHeight();
    ASSERT_GT(width, 0);
    ASSERT_GT(height, 0);

    // SoftwareImageType(): on Windows the default (native) image type is Direct2D-backed, and
    // painting into it then reading pixels back on a GPU-less CI runner yields an all-zero image
    // (FRO242). Force a software-backed bitmap so getPixelAt() reads what paint() actually drew.
    juce::Image img(juce::Image::ARGB, width, height, true, juce::SoftwareImageType());
    juce::Graphics g(img);
    // paintEntireComponent recurses into children (paint() + paintOverChildren() + each child's
    // own paintEntireComponent), unlike a bare paint() call -- this is the same call
    // ZoomFrozenCachedImage.h uses to flatten a component tree into an offscreen image.
    EXPECT_NO_THROW(moduleComponent.paintEntireComponent(g, true));

    // Meaningful-content assertion that always runs, regardless of whether the PNG gets written.
    bool hasOpaquePixel = false;
    for (int y = 0; y < img.getHeight() && !hasOpaquePixel; ++y)
        for (int x = 0; x < img.getWidth() && !hasOpaquePixel; ++x)
            if (img.getPixelAt(x, y).getAlpha() > 0)
                hasOpaquePixel = true;
    EXPECT_TRUE(hasOpaquePixel) << "rendered ADSR card image should have at least one opaque pixel";

    // Same slider traversal as AdsrSlidersWrapIntoRowsThatFitTheModule, so the printed bounds
    // describe exactly what that test asserts on.
    std::vector<juce::Slider*> adsrSliders;
    for (auto* child : moduleComponent.getChildren())
        if (auto* slider = dynamic_cast<juce::Slider*>(child))
            adsrSliders.push_back(slider);

    std::cout << "AdsrCardRendersToPngForVisualInspection: image " << width << "x" << height << ", "
              << adsrSliders.size() << " ADSR sliders:" << std::endl;
    for (auto* slider : adsrSliders) {
        std::cout << "  '" << slider->getComponentID() << "' bounds " << slider->getBounds().toString() << std::endl;
    }

    moduleComponent.setLookAndFeel(nullptr);

    const char* pngPath = std::getenv("ADSR_CARD_PNG");
    if (pngPath == nullptr || juce::String(pngPath).isEmpty())
        GTEST_SKIP() << "set ADSR_CARD_PNG=<path> to write the rendered card for visual inspection";

    juce::File outFile(pngPath);
    outFile.getParentDirectory().createDirectory();
    outFile.deleteFile();
    juce::FileOutputStream stream(outFile);
    ASSERT_TRUE(stream.openedOk()) << "failed to open " << pngPath << " for writing";
    juce::PNGImageFormat png;
    ASSERT_TRUE(png.writeImageToStream(img, stream)) << "failed to encode PNG to " << pngPath;
}
