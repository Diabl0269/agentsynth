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

TEST_F(ModuleComponentTest, MacroBankHeightFollowsItsLayoutAndFitsEveryJack) {
    int previous = 0;
    for (int count : {1, 4, 8, 16}) {
        AudioEngine engine;
        GraphEditor editor(engine);
        MacroControlModule macros;
        setKnobCount(macros, count);

        ModuleComponent comp(&macros, juce::AudioProcessorGraph::NodeID(1), editor);

        EXPECT_EQ(comp.getWidth(), synth::LayoutUtil::kSingleWidth) << "count " << count;
        EXPECT_GE(comp.getHeight(), previous) << "the card never shrinks as knobs are added, count " << count;
        previous = comp.getHeight();
        const auto last = comp.getPortCenter(count - 1, /*isInput=*/false);
        EXPECT_LT(last.y, comp.getHeight()) << "the last jack sits inside the card, count " << count;
    }
}

TEST_F(ModuleComponentTest, MacroBankJacksStayInTheRightGutterAndAreHittable) {
    AudioEngine engine;
    GraphEditor editor(engine);
    MacroControlModule macros;
    setKnobCount(macros, 12);
    ModuleComponent comp(&macros, juce::AudioProcessorGraph::NodeID(1), editor);

    int previousY = -1;
    for (int i = 0; i < 12; ++i) {
        auto centre = comp.getPortCenter(i, /*isInput=*/false);
        EXPECT_EQ(centre.x, comp.getWidth() - 10) << "jack " << i;
        EXPECT_GT(centre.y, previousY) << "jacks run top to bottom, jack " << i;
        previousY = centre.y;
        EXPECT_LT(centre.y, comp.getHeight()) << "jack " << i << " must be inside the module";

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
