// FRO312: a dangling parameter listener. ModuleComponent's ctor registers itself as an
// AudioProcessorParameter::Listener on every one of the module's parameters
// (module->getParameters(), see ModuleComponent.cpp's "Register as parameter listener" block).
// ~ModuleComponent (via detachFromProcessor()) tries to undo that, but historically decided
// whether the processor was still safe to touch by searching for it among the CURRENT nodes of
// owner.getAudioEngine().getGraph() -- which is right for a card built the normal way (GraphEditor
// only ever builds a card for a node that already lives in its graph, and a later single-node
// removal frees the processor before the card's destructor runs, so skipping the touch there is
// correct) but wrong for a card built directly on a bare processor that was NEVER added to any
// graph, exactly what ModuleComponentKnobCoverageTests.cpp does (a fresh processor per card,
// never routed through AudioEngine). There the "not found in the graph" search always reports
// false, so the destructor never calls removeListener -- the processor stays alive (it's owned by
// the test, not the graph) but the listener registration outlives the component, and the very
// next parameter write on that processor calls back into the freed ModuleComponent.
//
// This test is the direct, minimal repro: build a card on a bare, never-graphed processor,
// destroy the card, then set a parameter on the still-alive processor. Before the fix this is a
// heap-use-after-free (parameterValueChanged dispatching into freed memory); ASAN/a debug heap
// catches it reliably, and a plain release build often "gets away with it", which is exactly why
// ModuleComponentKnobCoverageTests.cpp had to route around it with a fresh processor per card
// instead of asserting on it directly.

#include "ModuleComponentTestFixture.h"

#include "AI/AIStateMapper/AIStateMapper.h"
#include "AudioEngine/AudioEngine.h"
#include "Modules/ModuleBase.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"

#include <gtest/gtest.h>
#include <memory>

TEST_F(ModuleComponentTest, DestroyingCardOnUngraphedProcessorDoesNotLeaveADanglingListener) {
    // Oscillator: a plain, always-available module with several float/int parameters -- nothing
    // special about the choice, any module in the factory would reproduce the same bug.
    auto processor = synth::AIStateMapper::createModule("Oscillator");
    auto* mb = dynamic_cast<ModuleBase*>(processor.get());
    ASSERT_NE(mb, nullptr);

    AudioEngine engine;
    GraphEditor editor(engine);

    {
        // Deliberately NOT added to engine's graph (mirrors the coverage test's construction) --
        // this is the exact case the graph-membership liveness check used to get wrong.
        ModuleComponent card(processor.get(), juce::AudioProcessorGraph::NodeID(1), editor);
        // card destructs here, at end of scope.
    }

    // The processor is still fully alive (owned by `processor` above, never touched by the
    // graph). If the destructor left `card` registered as a listener on every one of these
    // parameters, this write dispatches into freed memory.
    for (auto* param : mb->getParameters())
        param->setValueNotifyingHost(param->getValue());
    // Reaching here without crashing/ASAN-failing is the assertion.
    SUCCEED();
}
