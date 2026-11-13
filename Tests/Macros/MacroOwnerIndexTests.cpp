// synth::MacroOwnerIndex answers MacroSet::findByMember and outermostCollapsedAncestorOf exactly as the set itself
// does, for every member of nested, collapsed and plain macros (docs/architecture/graph-queries.md).
#include "MacroOwnerIndex.h"
#include <gtest/gtest.h>

namespace {

synth::Macro macro(const juce::String& id, std::vector<juce::String> members, bool collapsed = false) {
    synth::Macro m;
    m.id = id;
    m.members = std::move(members);
    m.collapsed = collapsed;
    return m;
}

void expectSameAnswers(const synth::MacroSet& macros, const std::vector<juce::String>& uuids) {
    const synth::MacroOwnerIndex index(macros);
    for (const auto& uuid : uuids) {
        EXPECT_EQ(index.ownerOf(uuid), macros.findByMember(uuid)) << uuid;
        EXPECT_EQ(index.hiddenByCollapse(uuid), macros.outermostCollapsedAncestorOf(uuid).isNotEmpty()) << uuid;
    }
}

} // namespace

TEST(MacroOwnerIndexTest, AgreesWithTheMacroSetForNestedAndCollapsedMacros) {
    synth::MacroSet macros;
    macros.add(macro("outer", {"a", "b"}));
    macros.add(macro("inner", {"c"}));
    macros.add(macro("deep", {"d"}));
    macros.add(macro("shut", {"e"}, true));
    macros.add(macro("underShut", {"f"}));
    macros.setParent("inner", "outer");
    macros.setParent("deep", "inner");
    macros.setParent("underShut", "shut");
    const std::vector<juce::String> uuids{"a", "b", "c", "d", "e", "f", "loose", ""};
    expectSameAnswers(macros, uuids);

    macros.find("outer")->collapsed = true; // now everything under outer is hidden too
    expectSameAnswers(macros, uuids);
}

TEST(MacroOwnerIndexTest, AUuidInTwoMacrosBelongsToTheFirst) {
    synth::MacroSet macros;
    macros.add(macro("first", {"x"}));
    macros.add(macro("second", {"x"}, true));
    expectSameAnswers(macros, {"x"});
    EXPECT_EQ(synth::MacroOwnerIndex(macros).ownerOf("x")->id, "first");
}
