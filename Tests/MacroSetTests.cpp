// MacroSetTests.cpp
// Pure struct-level unit tests for synth::MacroPort and its interaction with synth::MacroSet
// (P8-15 Macro I/O, docs/macros.md §5.2, §7 item 2) — no GraphEditor, no AudioEngine. The
// GraphEditor-level Macro behaviour (wrap/unwrap, persistence, cables, ...) lives in
// Tests/MacroContainerTests.cpp; this file covers only:
//
//   • MacroPort::toVar/fromVar   — round trip, both kinds, and rejection of malformed input
//   • MacroSet::toVar/fromVar    — "ports" round trips, is optional (pre-P8-15 compatibility),
//                                  and rejects a port whose nodeUuid isn't one of its own members
//   • MacroSet::retainOnly       — drops a port whose node died, dissolves the macro when its
//                                  last member (a port node) dies, keeps a port whose node is
//                                  still alive untouched
//   • MacroSet::removeMemberEverywhere — drops a port when its fronting member is removed singly

#include "MacroSet.h"
#include <gtest/gtest.h>

using synth::Macro;
using synth::MacroPort;
using synth::MacroPortKind;
using synth::MacroSet;

namespace {

MacroPort makePort(const juce::String& nodeUuid, bool isInput, const juce::String& name, int order,
                   MacroPortKind kind = MacroPortKind::AudioCV) {
    MacroPort p;
    p.nodeUuid = nodeUuid;
    p.isInput = isInput;
    p.name = name;
    p.order = order;
    p.kind = kind;
    return p;
}

} // namespace

// ---------------------------------------------------------------------------------------------
// MacroPort::toVar / fromVar
// ---------------------------------------------------------------------------------------------

TEST(MacroPortSerialization, ToVarFromVarRoundTripsAudioCVPort) {
    const MacroPort original = makePort("node-1", true, "Pitch In", 2, MacroPortKind::AudioCV);

    const juce::var v = original.toVar();
    MacroPort parsed;
    ASSERT_TRUE(MacroPort::fromVar(v, parsed));

    EXPECT_EQ(parsed.nodeUuid, original.nodeUuid);
    EXPECT_EQ(parsed.isInput, original.isInput);
    EXPECT_EQ(parsed.name, original.name);
    EXPECT_EQ(parsed.order, original.order);
    EXPECT_EQ(parsed.kind, MacroPortKind::AudioCV);
}

TEST(MacroPortSerialization, ToVarFromVarRoundTripsMidiPort) {
    const MacroPort original = makePort("node-2", false, "Gate Out", 0, MacroPortKind::Midi);

    const juce::var v = original.toVar();
    MacroPort parsed;
    ASSERT_TRUE(MacroPort::fromVar(v, parsed));

    EXPECT_EQ(parsed.nodeUuid, original.nodeUuid);
    EXPECT_FALSE(parsed.isInput);
    EXPECT_EQ(parsed.name, original.name);
    EXPECT_EQ(parsed.kind, MacroPortKind::Midi);
}

TEST(MacroPortSerialization, FromVarRejectsNonObject) {
    MacroPort out;
    EXPECT_FALSE(MacroPort::fromVar(juce::var(42), out));
    EXPECT_FALSE(MacroPort::fromVar(juce::var("not an object"), out));
}

TEST(MacroPortSerialization, FromVarRejectsEmptyNodeUuid) {
    auto* obj = new juce::DynamicObject();
    obj->setProperty("nodeUuid", "");
    obj->setProperty("kind", "audioCV");
    MacroPort out;
    EXPECT_FALSE(MacroPort::fromVar(juce::var(obj), out));
}

TEST(MacroPortSerialization, FromVarRejectsMissingOrUnknownKind) {
    {
        auto* obj = new juce::DynamicObject();
        obj->setProperty("nodeUuid", "node-1");
        // no "kind" property at all
        MacroPort out;
        EXPECT_FALSE(MacroPort::fromVar(juce::var(obj), out));
    }
    {
        auto* obj = new juce::DynamicObject();
        obj->setProperty("nodeUuid", "node-1");
        obj->setProperty("kind", "somethingElse");
        MacroPort out;
        EXPECT_FALSE(MacroPort::fromVar(juce::var(obj), out));
    }
}

TEST(MacroPortSerialization, FromVarLeavesOutUntouchedOnRejection) {
    MacroPort out = makePort("untouched", true, "keep me", 7);
    auto* obj = new juce::DynamicObject();
    obj->setProperty("nodeUuid", ""); // malformed
    EXPECT_FALSE(MacroPort::fromVar(juce::var(obj), out));

    EXPECT_EQ(out.nodeUuid, "untouched");
    EXPECT_EQ(out.name, "keep me");
    EXPECT_EQ(out.order, 7);
}

// ---------------------------------------------------------------------------------------------
// MacroSet::toVar / fromVar — "ports" wiring
// ---------------------------------------------------------------------------------------------

TEST(MacroSetPortPersistence, RoundTripsPortsThroughToVarFromVar) {
    MacroSet set;
    Macro m;
    m.id = "macro-1";
    m.name = "Filter Bank";
    m.members = {"member-1", "inlet-uuid", "outlet-uuid"};
    m.ports = {makePort("inlet-uuid", true, "Cutoff In", 0, MacroPortKind::AudioCV),
               makePort("outlet-uuid", false, "MIDI Thru", 1, MacroPortKind::Midi)};
    set.add(m);

    const juce::var saved = set.toVar();

    MacroSet reloaded;
    ASSERT_TRUE(reloaded.fromVar(saved));
    ASSERT_EQ(reloaded.size(), 1);

    const Macro& r = reloaded.getAll()[0];
    ASSERT_EQ((int)r.ports.size(), 2);
    EXPECT_EQ(r.ports[0].nodeUuid, "inlet-uuid");
    EXPECT_TRUE(r.ports[0].isInput);
    EXPECT_EQ(r.ports[0].name, "Cutoff In");
    EXPECT_EQ(r.ports[0].kind, MacroPortKind::AudioCV);
    EXPECT_EQ(r.ports[1].nodeUuid, "outlet-uuid");
    EXPECT_FALSE(r.ports[1].isInput);
    EXPECT_EQ(r.ports[1].kind, MacroPortKind::Midi);
}

// Every macro saved by P8-12 (before this key existed) has no "ports" property at all — that
// must load as zero ports, not reject the whole macro.
TEST(MacroSetPortPersistence, AbsentPortsKeyParsesAsEmptyPortList) {
    auto* macroObj = new juce::DynamicObject();
    macroObj->setProperty("id", "old-macro");
    macroObj->setProperty("name", "Pre-P8-15 Macro");
    macroObj->setProperty("colour", juce::Colour(0xff5a7dff).toString());
    macroObj->setProperty("collapsed", false);

    auto* boundsObj = new juce::DynamicObject();
    boundsObj->setProperty("x", 0);
    boundsObj->setProperty("y", 0);
    boundsObj->setProperty("w", 100);
    boundsObj->setProperty("h", 100);
    macroObj->setProperty("bounds", juce::var(boundsObj));

    juce::Array<juce::var> members;
    members.add("member-1");
    members.add("member-2");
    macroObj->setProperty("members", members);
    // Deliberately no "ports" property — this is the exact shape a pre-P8-15 project.json has.

    juce::Array<juce::var> arr;
    arr.add(juce::var(macroObj));

    MacroSet set;
    ASSERT_TRUE(set.fromVar(juce::var(arr)));
    ASSERT_EQ(set.size(), 1);
    EXPECT_TRUE(set.getAll()[0].ports.empty());
}

TEST(MacroSetPortPersistence, FromVarRejectsWholeMacroWhenAPortIsMalformed) {
    auto* macroObj = new juce::DynamicObject();
    macroObj->setProperty("id", "m1");
    macroObj->setProperty("name", "Broken");
    macroObj->setProperty("colour", juce::Colour(0xff5a7dff).toString());
    macroObj->setProperty("collapsed", false);
    juce::Array<juce::var> members;
    members.add("member-1");
    macroObj->setProperty("members", members);

    juce::Array<juce::var> ports;
    auto* badPort = new juce::DynamicObject();
    badPort->setProperty("nodeUuid", ""); // malformed: empty uuid
    ports.add(juce::var(badPort));
    macroObj->setProperty("ports", ports);

    juce::Array<juce::var> arr;
    arr.add(juce::var(macroObj));

    MacroSet set;
    EXPECT_FALSE(set.fromVar(juce::var(arr)));
    EXPECT_TRUE(set.empty()) << "a rejected load must leave the set completely untouched";
}

// A port's nodeUuid must be one of the SAME macro's own members (Macro::ports' invariant,
// mirroring "it is a member of that macro's members list like any other node", docs/macros.md
// §5.1) — a port naming a uuid outside `members` is rejected, not silently accepted.
TEST(MacroSetPortPersistence, FromVarRejectsPortWhoseNodeUuidIsNotAMember) {
    auto* macroObj = new juce::DynamicObject();
    macroObj->setProperty("id", "m1");
    macroObj->setProperty("name", "Orphan Port");
    macroObj->setProperty("colour", juce::Colour(0xff5a7dff).toString());
    macroObj->setProperty("collapsed", false);
    juce::Array<juce::var> members;
    members.add("member-1"); // note: "inlet-uuid" is NOT in this list
    macroObj->setProperty("members", members);

    juce::Array<juce::var> ports;
    auto* port = new juce::DynamicObject();
    port->setProperty("nodeUuid", "inlet-uuid");
    port->setProperty("isInput", true);
    port->setProperty("name", "In");
    port->setProperty("order", 0);
    port->setProperty("kind", "audioCV");
    ports.add(juce::var(port));
    macroObj->setProperty("ports", ports);

    juce::Array<juce::var> arr;
    arr.add(juce::var(macroObj));

    MacroSet set;
    EXPECT_FALSE(set.fromVar(juce::var(arr)));
    EXPECT_TRUE(set.empty());
}

// ---------------------------------------------------------------------------------------------
// MacroSet::retainOnly — port reconciliation
// ---------------------------------------------------------------------------------------------

TEST(MacroSetPortReconciliation, RetainOnlyDropsAPortWhoseNodeDiedButKeepsTheMacro) {
    MacroSet set;
    Macro m;
    m.id = "m1";
    m.members = {"member-1", "inlet-uuid"};
    m.ports = {makePort("inlet-uuid", true, "In", 0)};
    set.add(m);

    // "inlet-uuid" no longer exists in the live graph; "member-1" still does.
    const bool changed = set.retainOnly({"member-1"});

    EXPECT_TRUE(changed);
    ASSERT_EQ(set.size(), 1);
    const Macro& r = set.getAll()[0];
    EXPECT_EQ((int)r.members.size(), 1);
    EXPECT_TRUE(r.members[0] == "member-1");
    EXPECT_TRUE(r.ports.empty()) << "the port fronting the dead node must be dropped too";
}

TEST(MacroSetPortReconciliation, RetainOnlyDissolvesTheMacroWhenThePortNodeWasTheLastMember) {
    MacroSet set;
    Macro m;
    m.id = "m1";
    m.members = {"inlet-uuid"}; // the port node IS the only member
    m.ports = {makePort("inlet-uuid", true, "In", 0)};
    set.add(m);

    const bool changed = set.retainOnly({}); // nothing survives

    EXPECT_TRUE(changed);
    EXPECT_TRUE(set.empty()) << "a macro with zero members dissolves, taking its ports with it";
}

TEST(MacroSetPortReconciliation, RetainOnlyLeavesALivePortUntouched) {
    MacroSet set;
    Macro m;
    m.id = "m1";
    m.members = {"member-1", "inlet-uuid"};
    m.ports = {makePort("inlet-uuid", true, "In", 0)};
    set.add(m);

    const bool changed = set.retainOnly({"member-1", "inlet-uuid"}); // both still alive

    EXPECT_FALSE(changed);
    ASSERT_EQ(set.size(), 1);
    EXPECT_EQ((int)set.getAll()[0].ports.size(), 1);
}

// ---------------------------------------------------------------------------------------------
// MacroSet::removeMemberEverywhere — single-member removal also drops its port
// ---------------------------------------------------------------------------------------------

TEST(MacroSetPortReconciliation, RemoveMemberEverywhereDropsThePortItFronted) {
    MacroSet set;
    Macro m;
    m.id = "m1";
    m.members = {"member-1", "inlet-uuid"};
    m.ports = {makePort("inlet-uuid", true, "In", 0)};
    set.add(m);

    const juce::String touchedId = set.removeMemberEverywhere("inlet-uuid");

    EXPECT_EQ(touchedId, "m1");
    ASSERT_EQ(set.size(), 1);
    const Macro& r = set.getAll()[0];
    EXPECT_EQ((int)r.members.size(), 1);
    EXPECT_TRUE(r.ports.empty());
}
