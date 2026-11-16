// JSON round-trip and rejection coverage for the osc MessageSpec fields and Control::inRange
// (docs/control/midi-remote-osc.md#9-persistence, #7-trust-boundary). Suite name contains "MidiRemote" per the
// ship-task --gtest_filter convention.
#include "MidiRemote/RemoteModel.h"

#include <gtest/gtest.h>

using namespace synth;

namespace {

MessageSpec oscSpec(const juce::String& address, int argIndex = 0) {
    MessageSpec spec;
    spec.type = MessageType::osc;
    spec.address = address;
    spec.argIndex = argIndex;
    return spec;
}

juce::var specVar(const juce::String& json) { return juce::JSON::parse(json); }

ControllerProfile oscProfile() {
    ControllerProfile p;
    p.id = "osc-profile";
    p.name = "Tablet";
    p.input.identifier = "osc:9000";
    p.input.name = "OSC 9000";
    Control c;
    c.id = "c1";
    c.name = "/1/fader3";
    c.message = oscSpec("/1/fader3", 1);
    c.inRange.min = 0.0;
    c.inRange.max = 127.0;
    p.controls.push_back(c);
    return p;
}

} // namespace

TEST(MidiRemoteModelOscTest, OscSpecRoundTripsAddressAndArgIndex) {
    MessageSpec parsed;
    ASSERT_TRUE(MessageSpec::fromVar(oscSpec("/synth/filter/cutoff", 2).toVar(), parsed));
    EXPECT_EQ(parsed.type, MessageType::osc);
    EXPECT_EQ(parsed.address, "/synth/filter/cutoff");
    EXPECT_EQ(parsed.argIndex, 2);
    EXPECT_EQ(parsed.channel, 0);
    EXPECT_EQ(parsed.number, 0);
}

TEST(MidiRemoteModelOscTest, NonOscSpecsKeepTheirV1ShapeAndEquality) {
    MessageSpec cc;
    cc.type = MessageType::cc;
    cc.channel = 1;
    cc.number = 21;
    const auto var = cc.toVar();
    EXPECT_FALSE(var.getDynamicObject()->hasProperty("address"));
    EXPECT_FALSE(var.getDynamicObject()->hasProperty("argIndex"));
    EXPECT_TRUE(cc.address.isEmpty());
    EXPECT_EQ(cc.argIndex, 0);

    EXPECT_NE(oscSpec("/a"), oscSpec("/b"));
    EXPECT_NE(oscSpec("/a", 0), oscSpec("/a", 1));
    EXPECT_EQ(oscSpec("/a", 1), oscSpec("/a", 1));
}

TEST(MidiRemoteModelOscTest, MissingArgIndexIsZeroAndInvalidAddressesRejectTheSpec) {
    MessageSpec parsed;
    ASSERT_TRUE(MessageSpec::fromVar(specVar(R"({"type":"osc","channel":0,"number":0,"address":"/x"})"), parsed));
    EXPECT_EQ(parsed.argIndex, 0);

    const char* const bad[] = {
        R"({"type":"osc","channel":0,"number":0})",                               // missing address
        R"({"type":"osc","channel":0,"number":0,"address":""})",                  // empty
        R"({"type":"osc","channel":0,"number":0,"address":"x"})",                 // no leading slash
        R"({"type":"osc","channel":0,"number":0,"address":"/a/*"})",              // pattern char
        R"({"type":"osc","channel":0,"number":0,"address":"/a b"})",              // space
        R"({"type":"osc","channel":0,"number":0,"address":"/a\u0001"})",          // control char
        R"({"type":"osc","channel":0,"number":0,"address":7})",                   // wrong type
        R"({"type":"osc","channel":0,"number":0,"address":"/x","argIndex":-1})",  // below range
        R"({"type":"osc","channel":0,"number":0,"address":"/x","argIndex":64})",  // above range
        R"({"type":"osc","channel":0,"number":0,"address":"/x","argIndex":"0"})", // wrong type
        R"({"type":"osc","channel":3,"number":0,"address":"/x"})",                // channel must be 0
        R"({"type":"osc","channel":0,"number":5,"address":"/x"})",                // number must be 0
    };
    for (const char* json : bad) {
        MessageSpec target = oscSpec("/untouched");
        EXPECT_FALSE(MessageSpec::fromVar(specVar(json), target)) << json;
        EXPECT_EQ(target.address, "/untouched") << json;
    }

    const juce::String tooLong = "/" + juce::String::repeatedString("a", 128);
    MessageSpec target;
    EXPECT_FALSE(MessageSpec::fromVar(oscSpec(tooLong).toVar(), target)) << "129 bytes";
    EXPECT_TRUE(MessageSpec::fromVar(oscSpec("/" + juce::String::repeatedString("a", 127)).toVar(), target));
}

TEST(MidiRemoteModelOscTest, OscAddressRulesMatchTheNetworkValidation) {
    EXPECT_TRUE(isValidOscAddress("/1/fader12"));
    EXPECT_FALSE(isValidOscAddress("/1/fader*"));
    EXPECT_FALSE(isValidOscAddress("/a{b}"));
    EXPECT_FALSE(isValidOscAddress("/a[1]"));
    EXPECT_FALSE(isValidOscAddress("/a?"));
    EXPECT_FALSE(isValidOscAddress(juce::String(juce::CharPointer_UTF8("/caf\xc3\xa9")))) << "printable ASCII only";
}

TEST(MidiRemoteModelOscTest, OscProfileWithInRangeRoundTripsAndNonOscControlsOmitIt) {
    const auto profile = oscProfile();
    ControllerProfile loaded;
    ASSERT_TRUE(loaded.fromVar(profile.toVar()));
    ASSERT_EQ(loaded.controls.size(), 1u);
    EXPECT_EQ(loaded.controls[0].message, profile.controls[0].message);
    EXPECT_DOUBLE_EQ(loaded.controls[0].inRange.min, 0.0);
    EXPECT_DOUBLE_EQ(loaded.controls[0].inRange.max, 127.0);

    Control cc;
    cc.id = "cc";
    cc.message.type = MessageType::cc;
    EXPECT_FALSE(cc.toVar().getDynamicObject()->hasProperty("inRange"));
}

TEST(MidiRemoteModelOscTest, AMissingInRangeDefaultsToZeroToOneAndAMalformedOneRejectsTheProfile) {
    auto var = oscProfile().toVar();
    auto* control = var.getProperty("controls", {}).getArray()->getReference(0).getDynamicObject();
    control->removeProperty("inRange");
    ControllerProfile loaded;
    ASSERT_TRUE(loaded.fromVar(var));
    EXPECT_DOUBLE_EQ(loaded.controls[0].inRange.max, 1.0);

    control->setProperty("inRange", specVar(R"({"min":"x","max":1})"));
    ControllerProfile rejected;
    rejected.name = "untouched";
    EXPECT_FALSE(rejected.fromVar(var));
    EXPECT_EQ(rejected.name, "untouched");
}

TEST(MidiRemoteModelOscTest, AnOscControlMustUseTheAbsoluteEncodingAndAProfileRejectsADuplicateAddress) {
    auto var = oscProfile().toVar();
    auto* control = var.getProperty("controls", {}).getArray()->getReference(0).getDynamicObject();
    control->setProperty("encoding", "relTwos");
    ControllerProfile loaded;
    EXPECT_FALSE(loaded.fromVar(var));

    auto dup = oscProfile();
    auto second = dup.controls[0];
    second.id = "c2";
    dup.controls.push_back(second);
    EXPECT_FALSE(loaded.fromVar(dup.toVar())) << "two controls on one (address, argIndex)";

    dup.controls[1].message.argIndex = 2;
    EXPECT_TRUE(loaded.fromVar(dup.toVar()));
}

TEST(MidiRemoteModelOscTest, AnAssignmentCarriesTheOscSpec) {
    Assignment a;
    a.id = "a1";
    a.control.profileId = "p";
    a.control.controlId = "c";
    a.spec = oscSpec("/1/fader3", 1);
    a.target.kind = Target::Kind::parameter;
    a.target.parameter.nodeUuid = "n";
    a.target.parameter.paramId = "cutoff";

    Assignment loaded;
    ASSERT_TRUE(Assignment::fromVar(a.toVar(), loaded));
    EXPECT_EQ(loaded.spec, a.spec);
}
