// Real-mouse-gesture regression coverage for T148's auto-create-on-drag (docs/macros.md §7
// item 9). Every T148 test in MacroPortFlowTests.cpp drives the feature through
// GraphEditor::beginConnectionDrag/endConnectionDrag called directly -- a convenient shortcut,
// but one that never touches ModuleComponent::mouseDown/mouseDrag/mouseUp at all. Those real
// entry points have their own state machine: notably, ModuleComponent::mouseUp gates on
// e.getMouseDownPosition(), which JUCE holds FIXED at the original press point for the whole
// gesture while e.getPosition() tracks the live cursor -- a synthetic MouseEvent that (wrongly)
// moves both together makes that gate miss the source jack and silently no-ops the whole gesture.
// This file drives mouseDown -> mouseDrag -> mouseUp on the real ModuleComponent callbacks, with
// a correctly-held-fixed mouseDownPosition, closing that gap (see
// docs/testing.md's "test the real mouse path" guidance).

#include "../Source/Modules/FilterModule.h"
#include "../Source/Modules/OscillatorModule.h"
#include "../Source/UI/GraphEditor.h"
#include "../Source/UI/ModuleComponent.h"
#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>

using NodeID = juce::AudioProcessorGraph::NodeID;

namespace {

NodeID addModuleAt(GraphEditor& editor, AudioEngine& engine, std::unique_ptr<juce::AudioProcessor> processor, int x,
                   int y) {
    auto node = engine.getGraph().addNode(std::move(processor));
    node->properties.set("x", x);
    node->properties.set("y", y);
    node->properties.set("uuid", juce::Uuid().toDashedString());
    editor.updateComponents();
    return node->nodeID;
}

ModuleComponent* compFor(GraphEditor& editor, NodeID id) {
    for (auto* c : editor.getModuleComponents())
        if (c != nullptr && c->getNodeId() == id)
            return c;
    return nullptr;
}

// mouseDownLocalPos is the FIXED press-point (local to eventComp), unchanged across an entire
// gesture; localPos is where the cursor is RIGHT NOW for this specific event.
juce::MouseEvent realMouseEvent(juce::Component& eventComp, juce::Point<int> localPos,
                                juce::Point<int> mouseDownLocalPos, juce::ModifierKeys mods, bool wasDragged = false) {
    const auto pos = localPos.toFloat();
    const auto downPos = mouseDownLocalPos.toFloat();
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), pos, mods, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                            &eventComp, &eventComp, juce::Time::getCurrentTime(), downPos, juce::Time::getCurrentTime(),
                            1, wasDragged);
}

/** Drives a full real mouseDown -> mouseDrag -> mouseUp connection-drag gesture from `srcComp`'s
 *  jack at `srcJackChannel`/`srcIsInput` to `dstComp`'s jack under the cursor, exactly like a real
 *  user's press-drag-release. */
void dragRealCableBetween(ModuleComponent& srcComp, int srcJackChannel, bool srcIsInput, ModuleComponent& dstComp,
                          int dstJackChannel, bool dstIsInput) {
    const juce::ModifierKeys leftClick(juce::ModifierKeys::leftButtonModifier);
    const auto srcJackLocal = srcComp.getPortCenter(srcJackChannel, srcIsInput);
    srcComp.mouseDown(realMouseEvent(srcComp, srcJackLocal, srcJackLocal, leftClick));

    const auto targetScreenPos = dstComp.localPointToGlobal(dstComp.getPortCenter(dstJackChannel, dstIsInput));
    const auto srcLocalForTarget = srcComp.getLocalPoint(nullptr, targetScreenPos);
    srcComp.mouseDrag(realMouseEvent(srcComp, srcLocalForTarget, srcJackLocal, leftClick, /*wasDragged=*/true));
    srcComp.mouseUp(realMouseEvent(srcComp, srcLocalForTarget, srcJackLocal, leftClick, /*wasDragged=*/true));
}

} // namespace

TEST(MacroPortRealMouseDrag, DraggingFromAnExpandedMacroMembersOutputJackToAnExternalModuleAutoCreatesAnOutlet) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto oscMember = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto filterMember = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 100, 400);
    editor.setSelectedNodes({oscMember, filterMember});
    const auto macroId = editor.groupSelectionIntoMacro();
    ASSERT_FALSE(macroId.isEmpty());
    editor.setMacroCollapsed(macroId, false); // expand: members become real, visible ModuleComponents

    auto* memberComp = compFor(editor, oscMember);
    ASSERT_NE(memberComp, nullptr);
    ASSERT_TRUE(memberComp->isVisible());

    auto extFilter = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 900, 100);
    auto* extComp = compFor(editor, extFilter);
    ASSERT_NE(extComp, nullptr);

    const int nodesBefore = engine.getGraph().getNodes().size();

    dragRealCableBetween(*memberComp, 0, /*srcIsInput=*/false, *extComp, 0, /*dstIsInput=*/true);

    EXPECT_EQ(engine.getGraph().getNodes().size(), nodesBefore + 1) << "expected exactly one new outlet port node";

    auto* macro = editor.getMacros().find(macroId);
    ASSERT_NE(macro, nullptr);
    ASSERT_EQ(macro->ports.size(), 1u);
    EXPECT_FALSE(macro->ports[0].isInput);
}

TEST(MacroPortRealMouseDrag, DraggingFromAnExternalModulesOutputJackToAnExpandedMacroMembersInputAutoCreatesAnInlet) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto oscMember = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto filterMember = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 100, 400);
    editor.setSelectedNodes({oscMember, filterMember});
    const auto macroId = editor.groupSelectionIntoMacro();
    ASSERT_FALSE(macroId.isEmpty());
    editor.setMacroCollapsed(macroId, false);

    auto* memberComp = compFor(editor, filterMember); // Filter's audio INPUT jack 0
    ASSERT_NE(memberComp, nullptr);

    auto extOsc = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 900, 400);
    auto* extComp = compFor(editor, extOsc); // external Oscillator's audio OUTPUT jack 0
    ASSERT_NE(extComp, nullptr);

    const int nodesBefore = engine.getGraph().getNodes().size();

    dragRealCableBetween(*extComp, 0, /*srcIsInput=*/false, *memberComp, 0, /*dstIsInput=*/true);

    EXPECT_EQ(engine.getGraph().getNodes().size(), nodesBefore + 1) << "expected exactly one new inlet port node";

    auto* macro = editor.getMacros().find(macroId);
    ASSERT_NE(macro, nullptr);
    ASSERT_EQ(macro->ports.size(), 1u);
    EXPECT_TRUE(macro->ports[0].isInput);
}
