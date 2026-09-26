// FRO142 (docs/control/midi-remote.md#pages): Assignment::page, ControllerProfile::pageCount, and
// Target::Kind::page round-trip/rejection coverage -- mirrors RemoteModelTests.cpp's style. The
// headline guarantee: every pre-FRO142 document (no "page"/"pageCount" property anywhere) loads
// with page 1 / pageCount 1 and re-serialises byte-identical to what it started as. Suite name
// contains "MidiRemote" per the ship-task --gtest_filter convention.

#include "MidiRemote/RemoteModel.h"
#include <gtest/gtest.h>

using namespace synth;

namespace {

Assignment makeV1ParamAssignment() {
    Assignment a;
    a.id = "a1";
    a.control.profileId = "profile-1";
    a.control.controlId = "control-1";
    a.spec.type = MessageType::cc;
    a.spec.channel = 1;
    a.spec.number = 21;
    a.specEncoding = Encoding::abs7;
    a.specButtonMode = ButtonMode::momentary;
    a.specControlName = "Knob 1";
    a.target.kind = Target::Kind::parameter;
    a.target.parameter.nodeUuid = "node-uuid-1";
    a.target.parameter.paramId = "cutoff";
    a.takeover = Takeover::useDefault;
    a.range.min = 0.0;
    a.range.max = 1.0;
    a.enabled = true;
    return a;
}

ControllerProfile makeV1Profile() {
    ControllerProfile p;
    p.version = 1;
    p.id = "profile-1";
    p.name = "Controller";
    p.input.identifier = "dev-1";
    p.input.name = "Device 1";
    return p;
}

} // namespace

// -- Assignment::page --------------------------------------------------------------------------

TEST(MidiRemoteModelPagesTest, AssignmentDefaultsToPageOneAndOmitsItFromJson) {
    const auto a = makeV1ParamAssignment();
    ASSERT_EQ(a.page, 1);
    const juce::var v = a.toVar();
    EXPECT_FALSE(v.getDynamicObject()->hasProperty("page")) << "a pre-FRO142 document must round-trip byte-identical";

    Assignment parsed;
    ASSERT_TRUE(Assignment::fromVar(v, parsed));
    EXPECT_EQ(parsed.page, 1);
}

TEST(MidiRemoteModelPagesTest, AssignmentPageRoundTrips) {
    auto a = makeV1ParamAssignment();
    a.page = 7;
    const juce::var v = a.toVar();
    EXPECT_TRUE(v.getDynamicObject()->getProperty("page").equals(7));

    Assignment parsed;
    ASSERT_TRUE(Assignment::fromVar(v, parsed));
    EXPECT_EQ(parsed.page, 7);
}

TEST(MidiRemoteModelPagesTest, AssignmentRejectsPageZero) {
    juce::var v = makeV1ParamAssignment().toVar();
    v.getDynamicObject()->setProperty("page", 0);
    Assignment parsed;
    EXPECT_FALSE(Assignment::fromVar(v, parsed));
}

TEST(MidiRemoteModelPagesTest, AssignmentRejectsPageAboveSixteen) {
    juce::var v = makeV1ParamAssignment().toVar();
    v.getDynamicObject()->setProperty("page", 17);
    Assignment parsed;
    EXPECT_FALSE(Assignment::fromVar(v, parsed));
}

TEST(MidiRemoteModelPagesTest, AssignmentRejectsNonIntegerPage) {
    juce::var v = makeV1ParamAssignment().toVar();
    v.getDynamicObject()->setProperty("page", "two");
    Assignment parsed;
    EXPECT_FALSE(Assignment::fromVar(v, parsed));
}

TEST(MidiRemoteModelPagesTest, AssignmentAcceptsPageSixteenBoundary) {
    juce::var v = makeV1ParamAssignment().toVar();
    v.getDynamicObject()->setProperty("page", 16);
    Assignment parsed;
    ASSERT_TRUE(Assignment::fromVar(v, parsed));
    EXPECT_EQ(parsed.page, 16);
}

// -- ControllerProfile::pageCount ----------------------------------------------------------------

TEST(MidiRemoteModelPagesTest, ControllerProfileDefaultsToPageCountOneAndOmitsItFromJson) {
    const auto p = makeV1Profile();
    ASSERT_EQ(p.pageCount, 1);
    const juce::var v = p.toVar();
    EXPECT_FALSE(v.getDynamicObject()->hasProperty("pageCount"))
        << "a pre-FRO142 profile must round-trip byte-identical";

    ControllerProfile parsed;
    ASSERT_TRUE(parsed.fromVar(v));
    EXPECT_EQ(parsed.pageCount, 1);
}

TEST(MidiRemoteModelPagesTest, ControllerProfilePageCountRoundTrips) {
    auto p = makeV1Profile();
    p.pageCount = 4;
    const juce::var v = p.toVar();
    EXPECT_TRUE(v.getDynamicObject()->getProperty("pageCount").equals(4));

    ControllerProfile parsed;
    ASSERT_TRUE(parsed.fromVar(v));
    EXPECT_EQ(parsed.pageCount, 4);
}

TEST(MidiRemoteModelPagesTest, ControllerProfileRejectsPageCountZero) {
    juce::var v = makeV1Profile().toVar();
    v.getDynamicObject()->setProperty("pageCount", 0);
    ControllerProfile parsed;
    EXPECT_FALSE(parsed.fromVar(v));
}

TEST(MidiRemoteModelPagesTest, ControllerProfileRejectsPageCountAboveSixteen) {
    juce::var v = makeV1Profile().toVar();
    v.getDynamicObject()->setProperty("pageCount", 17);
    ControllerProfile parsed;
    EXPECT_FALSE(parsed.fromVar(v));
}

// -- Target::Kind::page ------------------------------------------------------------------------

TEST(MidiRemoteModelPagesTest, TargetPageNextRoundTrips) {
    Target t;
    t.kind = Target::Kind::page;
    t.page.command = PageCommand::next;
    t.page.page = 1;
    Target parsed;
    ASSERT_TRUE(Target::fromVar(t.toVar(), parsed));
    EXPECT_TRUE(parsed.isPage());
    EXPECT_EQ(parsed.page.command, PageCommand::next);
}

TEST(MidiRemoteModelPagesTest, TargetPageGoRoundTripsItsPageNumber) {
    Target t;
    t.kind = Target::Kind::page;
    t.page.command = PageCommand::go;
    t.page.page = 9;
    Target parsed;
    ASSERT_TRUE(Target::fromVar(t.toVar(), parsed));
    EXPECT_EQ(parsed.page.command, PageCommand::go);
    EXPECT_EQ(parsed.page.page, 9);
}

TEST(MidiRemoteModelPagesTest, TargetPageSerialisesAsDocExactCamelCaseStrings) {
    for (const auto command : {PageCommand::next, PageCommand::previous, PageCommand::go}) {
        Target t;
        t.kind = Target::Kind::page;
        t.page.command = command;
        t.page.page = 1;
        const juce::var v = t.toVar(); // keep the juce::var alive: getDynamicObject() returns a raw, non-owning pointer
        const auto* payload = v.getDynamicObject()->getProperty("page").getDynamicObject();
        ASSERT_NE(payload, nullptr);
        const juce::String commandStr = payload->getProperty("command").toString();
        EXPECT_TRUE(commandStr == "next" || commandStr == "previous" || commandStr == "go")
            << "unexpected string for command=" << static_cast<int>(command);
    }
}

TEST(MidiRemoteModelPagesTest, TargetPageRejectsUnknownCommandString) {
    juce::var v = juce::JSON::parse(R"({"page":{"command":"bogus","page":1}})");
    Target parsed;
    EXPECT_FALSE(Target::fromVar(v, parsed));
}

TEST(MidiRemoteModelPagesTest, TargetPageRejectsPageZero) {
    juce::var v = juce::JSON::parse(R"({"page":{"command":"go","page":0}})");
    Target parsed;
    EXPECT_FALSE(Target::fromVar(v, parsed));
}

TEST(MidiRemoteModelPagesTest, TargetPageRejectsPageAboveSixteen) {
    juce::var v = juce::JSON::parse(R"({"page":{"command":"go","page":17}})");
    Target parsed;
    EXPECT_FALSE(Target::fromVar(v, parsed));
}

TEST(MidiRemoteModelPagesTest, TargetWithPageAndParameterIsRejected) {
    juce::var v = juce::JSON::parse(
        R"({"page":{"command":"next","page":1},"parameter":{"nodeUuid":"n","paramId":"p","paramIndexHint":-1}})");
    Target parsed;
    EXPECT_FALSE(Target::fromVar(v, parsed));
}

TEST(MidiRemoteModelPagesTest, TargetWithPageAndActionIsRejected) {
    juce::var v = juce::JSON::parse(R"({"page":{"command":"next","page":1},"action":{"actionId":"transport.play"}})");
    Target parsed;
    EXPECT_FALSE(Target::fromVar(v, parsed));
}

// -- Assignment carrying a page Target -----------------------------------------------------------

TEST(MidiRemoteModelPagesTest, AssignmentWithPageTargetRoundTrips) {
    Assignment a = makeV1ParamAssignment();
    a.target = Target{};
    a.target.kind = Target::Kind::page;
    a.target.page.command = PageCommand::previous;
    Assignment parsed;
    ASSERT_TRUE(Assignment::fromVar(a.toVar(), parsed));
    EXPECT_TRUE(parsed.target.isPage());
    EXPECT_EQ(parsed.target.page.command, PageCommand::previous);
}
