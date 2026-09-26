// ModuleComponent Macro Control bank layout tests: knob-count sizing, jack rows, gutter, paging.

#include "AudioEngine/AudioEngine.h"
#include "ModuleComponentTestFixture.h"

#include "Modules/MacroControlModule.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Layout/LayoutUtil.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace {
// Sets the Macro bank's "Knobs" parameter before the component is built, so createControls()
// lays the component out for that count.
void setKnobCount(MacroControlModule& macros, int count) {
    for (auto* p : macros.getParameters())
        if (auto* i = dynamic_cast<juce::AudioParameterInt*>(p))
            if (i->paramID == "macroCount")
                i->setValueNotifyingHost(i->convertTo0to1(count));
}
} // namespace

TEST_F(ModuleComponentTest, MacroBankHeightTracksItsKnobCount) {
    using namespace synth::LayoutUtil;

    for (int count : {1, 4, 8, 16}) {
        AudioEngine engine;
        GraphEditor editor(engine);
        MacroControlModule macros;
        setKnobCount(macros, count);

        ModuleComponent comp(&macros, juce::AudioProcessorGraph::NodeID(1), editor);

        EXPECT_EQ(comp.getWidth(), kSingleWidth) << "count " << count;
        EXPECT_EQ(comp.getHeight(), macroBankHeight(count)) << "count " << count;
    }
}

TEST_F(ModuleComponentTest, MacroBankJacksSitOnTheirOwnKnobRow) {
    using namespace synth::LayoutUtil;

    AudioEngine engine;
    GraphEditor editor(engine);
    MacroControlModule macros;
    setKnobCount(macros, 12);
    ModuleComponent comp(&macros, juce::AudioProcessorGraph::NodeID(1), editor);

    for (int i = 0; i < 12; ++i) {
        auto centre = comp.getPortCenter(i, /*isInput=*/false);
        EXPECT_EQ(centre.x, comp.getWidth() - 10) << "jack " << i;
        EXPECT_EQ(centre.y, macroRowCentreY(i)) << "jack " << i;
        EXPECT_LT(centre.y, comp.getHeight()) << "jack " << i << " must be inside the module";

        // Clicking the jack must resolve to that macro's output port, not a neighbour's.
        auto hit = comp.getPortForPoint(centre);
        ASSERT_TRUE(hit.has_value()) << "jack " << i << " is not hit-testable";
        EXPECT_EQ(hit->index, i);
        EXPECT_FALSE(hit->isInput);
        EXPECT_FALSE(hit->isMidi);
    }
}

TEST_F(ModuleComponentTest, MacroBankHasNoInputOrMidiJacks) {
    AudioEngine engine;
    GraphEditor editor(engine);
    MacroControlModule macros;
    ModuleComponent comp(&macros, juce::AudioProcessorGraph::NodeID(1), editor);

    // Top-left / top-right are where paint() would put MIDI In / MIDI Out on any module that
    // has them. The bank declares neither, so nothing may be grabbable there.
    auto midiIn = comp.getPortForPoint({10, 30});
    if (midiIn.has_value())
        EXPECT_FALSE(midiIn->isMidi);

    auto midiOut = comp.getPortForPoint({comp.getWidth() - 10, 30});
    if (midiOut.has_value())
        EXPECT_FALSE(midiOut->isMidi);
}

TEST_F(ModuleComponentTest, MacroBankKnobsClearTheJackLabelGutter) {
    using namespace synth::LayoutUtil;

    AudioEngine engine;
    GraphEditor editor(engine);
    MacroControlModule macros;
    setKnobCount(macros, MacroControlModule::kMaxMacros);
    ModuleComponent comp(&macros, juce::AudioProcessorGraph::NodeID(1), editor);

    // paint() draws each output label in the 60px strip ending 20px short of the right edge.
    const int labelLeft = comp.getWidth() - 80;

    int visibleMacroKnobs = 0;
    for (auto* child : comp.getChildren()) {
        auto* slider = dynamic_cast<juce::Slider*>(child);
        if (slider == nullptr || !slider->isVisible())
            continue;
        if (!slider->getComponentID().startsWith("M"))
            continue; // skip the "Knobs" count slider

        ++visibleMacroKnobs;
        EXPECT_LE(slider->getRight(), labelLeft) << slider->getComponentID() << " overlaps the output-label gutter";
        EXPECT_LT(slider->getBottom(), comp.getHeight()) << slider->getComponentID() << " overflows the module";
    }

    EXPECT_EQ(visibleMacroKnobs, MacroControlModule::kMaxMacros);
}

TEST_F(ModuleComponentTest, MacroBankHidesKnobRowsAboveTheCount) {
    AudioEngine engine;
    GraphEditor editor(engine);
    MacroControlModule macros;
    setKnobCount(macros, 4);
    ModuleComponent comp(&macros, juce::AudioProcessorGraph::NodeID(1), editor);

    int visible = 0;
    for (auto* child : comp.getChildren())
        if (auto* slider = dynamic_cast<juce::Slider*>(child))
            if (slider->isVisible() && slider->getComponentID().startsWith("M"))
                ++visible;

    EXPECT_EQ(visible, 4);
}
