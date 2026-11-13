// MacroAutoPortNestedTests.cpp
// Auto ports across two macro boundaries (docs/macros/auto-ports.md#nested-macros): a cable into or out of a member of
// a child macro nested in a parent gets one port per boundary, wired in a chain. No UI creates a nested macro yet, so
// the fixture groups two flat macros and links them with MacroSet::setParent. Shared modules/helpers live in
// MacroAutoPortTestHelpers.h.

#include "AudioEngine/AudioEngine.h"
#include "MacroAutoPortTestHelpers.h"

#include "Modules/MacroInletModule.h"
#include "Modules/MacroOutletModule.h"
#include "UI/Macros/MacroCardComponent/MacroCardComponent.h"

namespace {
ModuleComponent* compOf(GraphEditor& editor, NodeID id) {
    for (auto* c : editor.getModuleComponents())
        if (c != nullptr && c->getNodeId() == id)
            return c;
    return nullptr;
}

// Child macro {c1, cSpare} nested in a parent macro whose own direct members are {p1, pSpare}, both expanded, plus a
// module outside every macro. c1 is a CV probe and ext a constant source so a rendered block proves the signal path.
struct NestedPortFixture {
    static constexpr float kValue = 0.37f;
    AudioEngine engine;
    GraphEditor editor{engine};
    NodeID ext, c1, cSpare, p1, pSpare;
    juce::String childId, parentId;

    NestedPortFixture() {
        editor.setSize(2000, 1600);
        ext = addModuleAt(editor, engine, std::make_unique<TestConstantModule>(kValue), "Ext", 1500, 300);
        c1 = addModuleAt(editor, engine, std::make_unique<TestCvProbeModule>(), "C1", 400, 300);
        cSpare = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "CSpare", 400, 500);
        p1 = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "P1", 300, 1000);
        pSpare = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "PSpare", 300, 1200);
        editor.setSelectedNodes({c1, cSpare});
        childId = ctl().groupSelectionIntoMacro();
        editor.setSelectedNodes({p1, pSpare});
        parentId = ctl().groupSelectionIntoMacro();
        linked = editor.getMacros().setParent(childId, parentId);
        editor.clearSelection();
        ctl().setMacroCollapsed(childId, false);
        ctl().setMacroCollapsed(parentId, false);
        editor.updateComponents();
    }

    bool linked = false;
    MacroGroupController& ctl() { return editor.getMacroController(); }
    const synth::Macro& child() { return *editor.getMacros().find(childId); }
    const synth::Macro& parent() { return *editor.getMacros().find(parentId); }
    NodeID nodeOf(const juce::String& uuid) { return nodeIdForUuid(engine, uuid); }

    void dragOutputToInput(NodeID from, NodeID to) {
        auto* fromComp = compOf(editor, from);
        auto* toComp = compOf(editor, to);
        ASSERT_NE(fromComp, nullptr);
        ASSERT_NE(toComp, nullptr);
        editor.beginConnectionDrag(fromComp, 0, /*isInput=*/false, /*isMidi=*/false, {0, 0});
        editor.endConnectionDrag(toComp->getBounds().getPosition() + toComp->getPortCenter(0, /*isInput=*/true));
    }

    float renderProbe() {
        constexpr double kSampleRate = 44100.0;
        constexpr int kBlockSize = 64;
        auto& graph = engine.getGraph();
        graph.setPlayConfigDetails(0, 0, kSampleRate, kBlockSize);
        graph.prepareToPlay(kSampleRate, kBlockSize);
        juce::MidiBuffer midi;
        for (int i = 0; i < 4; ++i) {
            juce::AudioBuffer<float> buf(1, kBlockSize);
            buf.clear();
            graph.processBlock(buf, midi);
        }
        graph.releaseResources();
        auto* p = dynamic_cast<TestCvProbeModule*>(graph.getNodeForId(c1)->getProcessor());
        return p != nullptr ? p->lastSample() : -1.0f;
    }
};

const synth::MacroPort* onlyPort(const synth::Macro& macro, bool isInput) {
    const synth::MacroPort* found = nullptr;
    for (const auto& p : macro.ports)
        if (p.isInput == isInput) {
            EXPECT_EQ(found, nullptr) << "more than one port on this side";
            found = &p;
        }
    return found;
}
} // namespace

TEST(MacroAutoPortNested, DraggingFromOutsideIntoANestedMemberMintsAParentInletAndAChildInletWiredInAChain) {
    NestedPortFixture f;
    ASSERT_TRUE(f.linked);
    f.dragOutputToInput(f.ext, f.c1);

    const auto* parentIn = onlyPort(f.parent(), true);
    const auto* childIn = onlyPort(f.child(), true);
    ASSERT_NE(parentIn, nullptr);
    ASSERT_NE(childIn, nullptr);
    EXPECT_EQ(f.parent().ports.size(), 1u);
    EXPECT_EQ(f.child().ports.size(), 1u);
    EXPECT_TRUE(f.parent().hasMember(parentIn->nodeUuid)) << "a parent's port is a direct member of the parent";
    EXPECT_TRUE(f.child().hasMember(childIn->nodeUuid)) << "a child's port is a direct member of the child";

    const auto pIn = f.nodeOf(parentIn->nodeUuid);
    const auto cIn = f.nodeOf(childIn->nodeUuid);
    EXPECT_TRUE(hasConnection(f.engine, f.ext, 0, pIn, 0));
    EXPECT_TRUE(hasConnection(f.engine, pIn, 0, cIn, 0));
    EXPECT_TRUE(hasConnection(f.engine, cIn, 0, f.c1, 0));
    EXPECT_FALSE(hasConnection(f.engine, f.ext, 0, f.c1, 0));
    EXPECT_NEAR(f.renderProbe(), NestedPortFixture::kValue, 0.001f) << "audio flows through both boundaries";
}

TEST(MacroAutoPortNested, DraggingFromANestedMemberToOutsideMintsAChildOutletAndAParentOutletWiredInAChain) {
    NestedPortFixture f;
    auto sink = addModuleAt(f.editor, f.engine, std::make_unique<TestMonoModule>(), "Sink", 1500, 700);
    f.dragOutputToInput(f.cSpare, sink);

    const auto* parentOut = onlyPort(f.parent(), false);
    const auto* childOut = onlyPort(f.child(), false);
    ASSERT_NE(parentOut, nullptr);
    ASSERT_NE(childOut, nullptr);
    EXPECT_TRUE(f.parent().memberIsPort(parentOut->nodeUuid));
    EXPECT_TRUE(f.child().memberIsPort(childOut->nodeUuid));
    const auto pOut = f.nodeOf(parentOut->nodeUuid);
    const auto cOut = f.nodeOf(childOut->nodeUuid);
    EXPECT_TRUE(hasConnection(f.engine, f.cSpare, 0, cOut, 0));
    EXPECT_TRUE(hasConnection(f.engine, cOut, 0, pOut, 0));
    EXPECT_TRUE(hasConnection(f.engine, pOut, 0, sink, 0));
}

TEST(MacroAutoPortNested, AnEdgeBetweenAParentMemberAndAChildMemberMintsOnlyTheChildPort) {
    NestedPortFixture f;
    f.dragOutputToInput(f.p1, f.c1);

    EXPECT_TRUE(f.parent().ports.empty()) << "the cable never leaves the parent";
    const auto* childIn = onlyPort(f.child(), true);
    ASSERT_NE(childIn, nullptr);
    const auto cIn = f.nodeOf(childIn->nodeUuid);
    EXPECT_TRUE(hasConnection(f.engine, f.p1, 0, cIn, 0));
    EXPECT_TRUE(hasConnection(f.engine, cIn, 0, f.c1, 0));
}

TEST(MacroAutoPortNested, ProgrammaticConnectionIntoANestedMemberRoutesThroughBothBoundaries) {
    NestedPortFixture f;
    f.ctl().applyProgrammaticConnectionChange(
        true, [&] { return f.engine.getGraph().addConnection({{f.ext, 0}, {f.c1, 0}}); });

    const auto* parentIn = onlyPort(f.parent(), true);
    const auto* childIn = onlyPort(f.child(), true);
    ASSERT_NE(parentIn, nullptr);
    ASSERT_NE(childIn, nullptr);
    const auto pIn = f.nodeOf(parentIn->nodeUuid);
    const auto cIn = f.nodeOf(childIn->nodeUuid);
    EXPECT_TRUE(hasConnection(f.engine, f.ext, 0, pIn, 0));
    EXPECT_TRUE(hasConnection(f.engine, pIn, 0, cIn, 0));
    EXPECT_TRUE(hasConnection(f.engine, cIn, 0, f.c1, 0));
    EXPECT_NEAR(f.renderProbe(), NestedPortFixture::kValue, 0.001f);
}

TEST(MacroAutoPortNested, ProgrammaticEdgeBetweenParentAndChildMembersLeavesTheParentWithoutPorts) {
    NestedPortFixture f;
    f.ctl().applyProgrammaticConnectionChange(
        true, [&] { return f.engine.getGraph().addConnection({{f.p1, 0}, {f.c1, 0}}); });
    EXPECT_TRUE(f.parent().ports.empty());
    EXPECT_EQ(f.child().ports.size(), 1u);
}

TEST(MacroAutoPortNested, RemovingTheOuterCableSweepsTheWholePortChain) {
    NestedPortFixture f;
    f.ctl().applyProgrammaticConnectionChange(
        true, [&] { return f.engine.getGraph().addConnection({{f.ext, 0}, {f.c1, 0}}); });
    const auto* parentIn = onlyPort(f.parent(), true);
    ASSERT_NE(parentIn, nullptr);
    const auto pIn = f.nodeOf(parentIn->nodeUuid);

    f.ctl().applyProgrammaticConnectionChange(
        false, [&] { return f.engine.getGraph().removeConnection({{f.ext, 0}, {pIn, 0}}); });

    EXPECT_TRUE(f.parent().ports.empty());
    EXPECT_TRUE(f.child().ports.empty()) << "the child inlet lost its feed with the parent's, so it goes too";
    EXPECT_NE(f.editor.getMacros().find(f.childId), nullptr);
    EXPECT_NE(f.editor.getMacros().find(f.parentId), nullptr);
}

TEST(MacroAutoPortNested, SweepingTheLastPortOfAParentThatHoldsOnlyItsChildKeepsTheParent) {
    NestedPortFixture f;
    // Leave the parent with no modules of its own: only its child and (soon) its ports.
    f.editor.getMacros().removeMemberEverywhere(f.editor.getMacroController().nodeUuidFor(f.p1));
    f.editor.getMacros().removeMemberEverywhere(f.editor.getMacroController().nodeUuidFor(f.pSpare));
    ASSERT_NE(f.editor.getMacros().find(f.parentId), nullptr);

    f.ctl().applyProgrammaticConnectionChange(
        true, [&] { return f.engine.getGraph().addConnection({{f.ext, 0}, {f.c1, 0}}); });
    const auto* parentIn = onlyPort(f.parent(), true);
    ASSERT_NE(parentIn, nullptr);
    const auto pIn = f.nodeOf(parentIn->nodeUuid);
    f.ctl().applyProgrammaticConnectionChange(
        false, [&] { return f.engine.getGraph().removeConnection({{f.ext, 0}, {pIn, 0}}); });

    ASSERT_NE(f.editor.getMacros().find(f.parentId), nullptr) << "a macro with children is not empty";
    EXPECT_EQ(f.editor.getMacros().parentOf(f.childId), f.parentId);
}
