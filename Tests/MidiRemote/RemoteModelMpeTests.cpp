// JSON round-trip and rejection coverage for ControllerProfile::mpe
// (docs/control/midi-remote-mpe.md#data-model-changes). Suite name contains "MidiRemote" per the ship-task
// --gtest_filter convention.
#include "MidiRemote/RemoteModel.h"

#include <gtest/gtest.h>

using namespace synth;

namespace {

ControllerProfile makeProfile() {
    ControllerProfile p;
    p.id = "profile-1";
    p.name = "Seaboard";
    p.input.identifier = "dev";
    p.input.name = "Dev";
    return p;
}

// The profile's own JSON with `mpe` replaced by `mpeJson` (a JSON snippet), parsed back as a var.
juce::var profileWithMpe(const juce::String& mpeJson) {
    auto var = makeProfile().toVar();
    var.getDynamicObject()->setProperty("mpe", juce::JSON::parse(mpeJson));
    return var;
}

} // namespace

TEST(MidiRemoteModelMpeTest, DefaultMpeIsOmittedSoAnOlderProfileSavesByteIdentical) {
    const auto var = makeProfile().toVar();
    EXPECT_FALSE(var.getDynamicObject()->hasProperty("mpe"));

    ControllerProfile loaded;
    ASSERT_TRUE(loaded.fromVar(var));
    EXPECT_TRUE(loaded.mpe.isDefault());
    EXPECT_EQ(loaded.mpe.mode, MpeSettings::Mode::automatic);
    EXPECT_EQ(juce::JSON::toString(loaded.toVar()), juce::JSON::toString(var));
}

TEST(MidiRemoteModelMpeTest, ForcedLayoutsRoundTrip) {
    for (const auto mode :
         {MpeSettings::Mode::off, MpeSettings::Mode::lower, MpeSettings::Mode::upper, MpeSettings::Mode::both}) {
        auto profile = makeProfile();
        profile.mpe.mode = mode;
        profile.mpe.lowerMembers = 7;
        profile.mpe.upperMembers = 7;

        ControllerProfile loaded;
        ASSERT_TRUE(loaded.fromVar(profile.toVar()));
        EXPECT_EQ(loaded.mpe, profile.mpe);
        EXPECT_EQ(loaded.version, 1);
    }
}

TEST(MidiRemoteModelMpeTest, MissingCountsDefaultToFifteenAndAnExplicitAutoLoads) {
    ControllerProfile loaded;
    ASSERT_TRUE(loaded.fromVar(profileWithMpe(R"({"mode":"lower"})")));
    EXPECT_EQ(loaded.mpe.lowerMembers, 15);
    EXPECT_EQ(loaded.mpe.upperMembers, 15);

    ControllerProfile viaAuto;
    ASSERT_TRUE(viaAuto.fromVar(profileWithMpe(R"({"mode":"auto"})")));
    EXPECT_TRUE(viaAuto.mpe.isDefault());
}

TEST(MidiRemoteModelMpeTest, MalformedMpeRejectsTheWholeProfileAndLeavesItUntouched) {
    const char* const bad[] = {
        R"([1])",                                               // wrong type
        R"({"mode":"sideways"})",                               // unknown mode
        R"({"mode":7})",                                        // mode wrong type
        R"({"mode":"lower","lowerMembers":0})",                 // below range
        R"({"mode":"lower","lowerMembers":16})",                // above range
        R"({"mode":"upper","upperMembers":-1})",                // negative
        R"({"mode":"lower","lowerMembers":"7"})",               // count wrong type
        R"({"mode":"lower","lowerMembers":7.5})",               // count not an int
        R"({"mode":"both"})",                                   // 15 + 15 overlaps
        R"({"mode":"both","lowerMembers":8,"upperMembers":7})", // 15 > 14 overlaps
    };
    for (const char* json : bad) {
        ControllerProfile target = makeProfile();
        target.name = "untouched";
        EXPECT_FALSE(target.fromVar(profileWithMpe(json))) << json;
        EXPECT_EQ(target.name, "untouched") << json;
    }
}

TEST(MidiRemoteModelMpeTest, ABareStringInsteadOfAnObjectIsRejected) {
    auto var = makeProfile().toVar();
    var.getDynamicObject()->setProperty("mpe", juce::var("lower"));
    ControllerProfile target;
    EXPECT_FALSE(target.fromVar(var));
}

TEST(MidiRemoteModelMpeTest, BothZonesAcceptExactlyFourteenMembers) {
    ControllerProfile loaded;
    EXPECT_TRUE(loaded.fromVar(profileWithMpe(R"({"mode":"both","lowerMembers":8,"upperMembers":6})")));
    EXPECT_TRUE(loaded.fromVar(profileWithMpe(R"({"mode":"both","lowerMembers":1,"upperMembers":1})")));
}
