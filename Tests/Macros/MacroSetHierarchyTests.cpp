// MacroSetHierarchyTests.cpp
// Pure struct-level tests for nested macros' data model: Macro::parentId, the MacroSet hierarchy
// queries, parent-aware removal/reconciliation, and the optional "parent" key in toVar/fromVar.
// No UI here; the canvas side is in Tests/Macros/MacroContainer/MacroNested*Tests.cpp.

#include "MacroSet.h"
#include <gtest/gtest.h>

using synth::Macro;
using synth::MacroSet;

namespace {

Macro makeMacro(const juce::String& id, std::vector<juce::String> members, const juce::String& parent = {},
                bool collapsed = false) {
    Macro m;
    m.id = id;
    m.name = id;
    m.members = std::move(members);
    m.parentId = parent;
    m.collapsed = collapsed;
    return m;
}

// outer{a} > mid{b} > inner{c}, plus a separate top-level lone{d}.
MacroSet makeChain(bool outerCollapsed = false, bool midCollapsed = false, bool innerCollapsed = false) {
    MacroSet set;
    set.add(makeMacro("outer", {"a"}, {}, outerCollapsed));
    set.add(makeMacro("mid", {"b"}, "outer", midCollapsed));
    set.add(makeMacro("inner", {"c"}, "mid", innerCollapsed));
    set.add(makeMacro("lone", {"d"}));
    return set;
}

juce::var parseJson(const juce::String& text) { return juce::JSON::parse(text); }

} // namespace

TEST(MacroSetHierarchy, ParentLinkRoundTripsThroughToVarFromVar) {
    const MacroSet src = makeChain();
    MacroSet loaded;
    ASSERT_TRUE(loaded.fromVar(src.toVar()));
    EXPECT_EQ(loaded.parentOf("inner"), juce::String("mid"));
    EXPECT_EQ(loaded.parentOf("mid"), juce::String("outer"));
    EXPECT_TRUE(loaded.parentOf("outer").isEmpty());
    EXPECT_EQ(juce::JSON::toString(loaded.toVar()), juce::JSON::toString(src.toVar()));
}

TEST(MacroSetHierarchy, FlatOldFileLoadsAndResavesByteIdentically) {
    const juce::String oldFile =
        R"([{"id":"m1","name":"A","colour":"ff5a7dff","collapsed":false,"bounds":{"x":1,"y":2,"w":3,"h":4},)"
        R"("members":["u1","u2"],"ports":[]},)"
        R"({"id":"m2","name":"B","colour":"ff5a7dff","collapsed":true,"bounds":{"x":0,"y":0,"w":0,"h":0},)"
        R"("members":["u3"],"ports":[]}])";
    MacroSet set;
    ASSERT_TRUE(set.fromVar(parseJson(oldFile)));
    EXPECT_TRUE(set.parentOf("m1").isEmpty());
    const juce::String resaved = juce::JSON::toString(set.toVar(), true);
    EXPECT_FALSE(resaved.contains("parent"));
    EXPECT_EQ(resaved, juce::JSON::toString(parseJson(oldFile), true));
}

TEST(MacroSetHierarchy, FromVarRejectsCycleDanglingAndSelfParent) {
    MacroSet set;
    set.add(makeMacro("keep", {"k"}));
    const juce::String before = juce::JSON::toString(set.toVar());

    MacroSet cyc = makeChain();
    juce::var v = cyc.toVar();
    v.getArray()->getReference(0).getDynamicObject()->setProperty("parent", "inner"); // outer <- inner
    EXPECT_FALSE(set.fromVar(v));

    MacroSet dangling = makeChain();
    v = dangling.toVar();
    v.getArray()->getReference(1).getDynamicObject()->setProperty("parent", "ghost");
    EXPECT_FALSE(set.fromVar(v));

    v = makeChain().toVar();
    v.getArray()->getReference(0).getDynamicObject()->setProperty("parent", "outer");
    EXPECT_FALSE(set.fromVar(v));

    EXPECT_EQ(juce::JSON::toString(set.toVar()), before) << "a rejected load leaves the set untouched";
}

TEST(MacroSetHierarchy, ContainerOnlyParentIsAcceptedButEmptyLeafIsNot) {
    MacroSet src;
    src.add(makeMacro("parent", {}));
    src.add(makeMacro("child", {"x"}, "parent"));
    MacroSet loaded;
    EXPECT_TRUE(loaded.fromVar(src.toVar()));
    EXPECT_EQ(loaded.size(), 2);

    MacroSet bad;
    bad.add(makeMacro("parent", {"p"}));
    bad.add(makeMacro("empty", {}, "parent"));
    EXPECT_FALSE(loaded.fromVar(bad.toVar()));
}

TEST(MacroSetHierarchy, RetainOnlyCascadesDissolveUpTheChain) {
    MacroSet set = makeChain();
    EXPECT_TRUE(set.retainOnly({"a", "b", "d"})); // c dies: inner dissolves, mid/outer keep members
    EXPECT_EQ(set.find("inner"), nullptr);
    EXPECT_TRUE(set.childrenOf("mid").empty());

    MacroSet container;
    container.add(makeMacro("outer", {}));
    container.add(makeMacro("inner", {"c"}, "outer"));
    EXPECT_TRUE(container.retainOnly({})); // inner dissolves, then the container-only outer too
    EXPECT_TRUE(container.empty());
}

TEST(MacroSetHierarchy, RemoveMemberEverywhereCascadesAndReportsTouchedMacro) {
    MacroSet set;
    set.add(makeMacro("outer", {}));
    set.add(makeMacro("mid", {}, "outer"));
    set.add(makeMacro("inner", {"c"}, "mid"));
    set.add(makeMacro("other", {"z"}));
    EXPECT_EQ(set.removeMemberEverywhere("c"), juce::String("inner"));
    EXPECT_EQ(set.size(), 1);
    EXPECT_NE(set.find("other"), nullptr);
}

TEST(MacroSetHierarchy, RemovePromotesChildrenToGrandparentOrTopLevel) {
    MacroSet set = makeChain();
    EXPECT_TRUE(set.remove("mid"));
    EXPECT_EQ(set.parentOf("inner"), juce::String("outer"));
    EXPECT_TRUE(set.remove("outer"));
    EXPECT_TRUE(set.parentOf("inner").isEmpty());
    EXPECT_FALSE(set.remove("nope"));
}

TEST(MacroSetHierarchy, SetParentRejectsCyclesSelfAndUnknown) {
    MacroSet set = makeChain();
    EXPECT_FALSE(set.setParent("outer", "inner"));
    EXPECT_FALSE(set.setParent("outer", "outer"));
    EXPECT_FALSE(set.setParent("outer", "ghost"));
    EXPECT_FALSE(set.setParent("ghost", "outer"));
    EXPECT_TRUE(set.setParent("lone", "inner"));
    EXPECT_EQ(set.depth("lone"), 3);
    EXPECT_TRUE(set.setParent("lone", {}));
    EXPECT_EQ(set.depth("lone"), 0);
}

TEST(MacroSetHierarchy, StructuralQueries) {
    const MacroSet set = makeChain();
    EXPECT_EQ(set.depth("outer"), 0);
    EXPECT_EQ(set.depth("inner"), 2);
    EXPECT_EQ(set.childrenOf("outer"), std::vector<juce::String>{"mid"});
    EXPECT_TRUE(set.childrenOf("inner").empty());
    EXPECT_EQ(set.ancestorChain("inner"), (std::vector<juce::String>{"mid", "outer"}));
    EXPECT_TRUE(set.ancestorChain("outer").empty());
    EXPECT_EQ(set.descendantMembers("outer"), (std::set<juce::String>{"a", "b", "c"}));
    EXPECT_EQ(set.descendantMembers("inner"), (std::set<juce::String>{"c"}));
    EXPECT_EQ(set.findByMember("c")->id, juce::String("inner"));
    EXPECT_EQ(set.outermostOf("c"), juce::String("outer"));
    EXPECT_EQ(set.outermostOf("d"), juce::String("lone"));
    EXPECT_TRUE(set.outermostOf("nobody").isEmpty());
}

TEST(MacroSetHierarchy, CollapseQueries) {
    const MacroSet open = makeChain();
    EXPECT_TRUE(open.outermostCollapsedAncestorOf("c").isEmpty());
    EXPECT_FALSE(open.isEffectivelyCollapsed("inner"));
    EXPECT_TRUE(open.isVisible("inner"));

    const MacroSet midShut = makeChain(false, true, false);
    EXPECT_EQ(midShut.outermostCollapsedAncestorOf("c"), juce::String("mid"));
    EXPECT_EQ(midShut.outermostCollapsedAncestorOf("b"), juce::String("mid"));
    EXPECT_TRUE(midShut.outermostCollapsedAncestorOf("a").isEmpty());
    EXPECT_TRUE(midShut.isEffectivelyCollapsed("mid"));
    EXPECT_TRUE(midShut.isEffectivelyCollapsed("inner"));
    EXPECT_FALSE(midShut.isEffectivelyCollapsed("outer"));
    EXPECT_TRUE(midShut.isVisible("mid")) << "a collapsed macro is itself still drawn (as a card)";
    EXPECT_FALSE(midShut.isVisible("inner"));

    const MacroSet allShut = makeChain(true, true, true);
    EXPECT_EQ(allShut.outermostCollapsedAncestorOf("c"), juce::String("outer"));
    EXPECT_FALSE(allShut.isVisible("mid"));
    EXPECT_TRUE(allShut.isVisible("outer"));
}
