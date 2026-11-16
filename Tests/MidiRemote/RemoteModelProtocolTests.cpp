// ControllerProfile::protocol round-trip and rejection (docs/control/midi-remote.md#protocol-surfaces): absent means
// generic and is never written, "mcu" round-trips, anything else rejects the whole load.

#include "MidiRemote/RemoteModel.h"

#include <gtest/gtest.h>

using namespace synth;

namespace {

ControllerProfile makeProfile() {
    ControllerProfile p;
    p.id = "profile-1";
    p.name = "Some Controller";
    p.input = {"in-id", "in-name"};
    return p;
}

} // namespace

TEST(MidiRemoteModelProtocolTest, GenericIsTheDefaultAndIsNeverWritten) {
    const auto p = makeProfile();
    EXPECT_EQ(p.protocol, ControllerProtocol::generic);
    EXPECT_FALSE(p.isProtocolSurface());
    EXPECT_FALSE(p.toVar().getDynamicObject()->hasProperty("protocol"))
        << "an older profile round-trips byte-identical";
    ControllerProfile loaded;
    ASSERT_TRUE(loaded.fromVar(p.toVar()));
    EXPECT_EQ(loaded.protocol, ControllerProtocol::generic);
}

TEST(MidiRemoteModelProtocolTest, MackieControlRoundTripsWithItsOutput) {
    auto p = makeProfile();
    p.protocol = ControllerProtocol::mcu;
    p.hasOutput = true;
    p.output = {"out-id", "out-name"};
    const auto json = juce::JSON::toString(p.toVar());
    EXPECT_TRUE(json.contains("\"protocol\": \"mcu\"")) << json;
    ControllerProfile loaded;
    ASSERT_TRUE(loaded.fromVar(juce::JSON::parse(json)));
    EXPECT_EQ(loaded.protocol, ControllerProtocol::mcu);
    EXPECT_TRUE(loaded.isProtocolSurface());
    EXPECT_EQ(loaded.output.identifier, "out-id");
    EXPECT_EQ(juce::JSON::toString(loaded.toVar()), json);
}

TEST(MidiRemoteModelProtocolTest, AnUnknownOrMistypedProtocolRejectsTheLoad) {
    for (const juce::var bad : {juce::var("hui-later"), juce::var(""), juce::var(1)}) {
        auto v = makeProfile().toVar();
        v.getDynamicObject()->setProperty("protocol", bad);
        ControllerProfile loaded;
        loaded.name = "untouched";
        EXPECT_FALSE(loaded.fromVar(v)) << bad.toString();
        EXPECT_EQ(loaded.name, "untouched");
    }
}

TEST(MidiRemoteModelProtocolTest, AProtocolProfileWithControlsIsRejected) {
    auto p = makeProfile();
    p.protocol = ControllerProtocol::mcu;
    Control c;
    c.id = "k1";
    c.name = "Knob";
    c.message.type = MessageType::cc;
    c.message.channel = 1;
    c.message.number = 20;
    p.controls.push_back(c);
    ControllerProfile loaded;
    EXPECT_FALSE(loaded.fromVar(p.toVar())) << "a Mackie Control's layout is the protocol's, never learned";
}
