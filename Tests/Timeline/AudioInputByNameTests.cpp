// AudioInputByNameTests.cpp: a track's input picked by the name the user gave it. The pure resolution
// (synth::resolveAudioInput), the project-file form, the snapshot a named track monitors from, and the words the
// routing pane shows for it.
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "Timeline/TimelineSnapshot.h"
#include "Timeline/TrackAudioInput.h"
#include "UI/Timeline/TimelineRoutingPane/AudioInputMenu.h"
#include <gtest/gtest.h>

namespace {
using synth::AudioInputChoice;
using synth::AudioInputStatus;
using synth::InputSlot;

AudioInputStatus interfaceWith(const juce::String& name, std::vector<InputSlot> layout,
                               std::uint32_t activeMask = 0xff) {
    AudioInputStatus status;
    status.deviceName = name;
    status.deviceInputs = 8;
    status.activeMask = activeMask;
    status.layout = std::move(layout);
    return status;
}

AudioInputChoice named(const juce::String& inputName, int first, int count, const juce::String& device) {
    AudioInputChoice choice;
    choice.kind = AudioInputChoice::Kind::Device;
    choice.firstChannel = first;
    choice.numChannels = count;
    choice.deviceName = device;
    choice.inputName = inputName;
    return choice;
}
} // namespace

TEST(AudioInputByNameTest, ANameFoundOnAnotherInterfaceMapsToThatInterfacesJacksAndChannelCount) {
    const auto choice = named("Vocal mic", 0, 1, "Volt 2"); // picked as mono In 1 on the Volt
    const auto scarlett = interfaceWith("Scarlett 4i4", {{0, 1, "Guitar"}, {1, 1, ""}, {2, 2, "Vocal mic"}});
    const auto resolved = synth::resolveAudioInput(choice, scarlett);
    EXPECT_TRUE(resolved.connected);
    EXPECT_FALSE(resolved.nameMissing);
    EXPECT_EQ(resolved.firstChannel, 2);
    EXPECT_EQ(resolved.numChannels, 2) << "the input there is a stereo pair";
}

TEST(AudioInputByNameTest, ANameTheInterfaceLacksIsNotConnectedWhateverJackItWasPickedOn) {
    const auto choice = named("Vocal mic", 0, 1, "Volt 2");
    const auto status = interfaceWith("Volt 2", {{0, 1, "Guitar"}});
    const auto resolved = synth::resolveAudioInput(choice, status);
    EXPECT_FALSE(resolved.connected) << "same device, same jack, but no input of that name";
    EXPECT_TRUE(resolved.nameMissing);
    EXPECT_FALSE(synth::isAudioInputConnected(choice, status));
}

TEST(AudioInputByNameTest, ANamedInputWhoseJackIsOffIsNotConnectedButNotMissing) {
    const auto choice = named("Vocal mic", 0, 1, "Volt 2");
    const auto resolved =
        synth::resolveAudioInput(choice, interfaceWith("Volt 2", {{0, 1, "Vocal mic"}}, /*activeMask=*/0b10));
    EXPECT_FALSE(resolved.connected);
    EXPECT_FALSE(resolved.nameMissing);
}

TEST(AudioInputByNameTest, WithNoNameTheJackBehaviourIsUnchanged) {
    auto choice = named({}, 2, 1, "Volt 2");
    const auto volt = interfaceWith("Volt 2", {{2, 1, "Vocal mic"}});
    auto resolved = synth::resolveAudioInput(choice, volt);
    EXPECT_TRUE(resolved.connected);
    EXPECT_EQ(resolved.firstChannel, 2) << "the layout's names are not consulted for an unnamed choice";
    EXPECT_FALSE(synth::resolveAudioInput(choice, interfaceWith("Scarlett", {})).connected) << "another device";
}

TEST(AudioInputByNameTest, HostedInputsCarryNoNamesSoTheJacksAreUsed) {
    auto status = interfaceWith({}, {});
    status.hosted = true;
    status.activeMask = 0b11;
    const auto resolved = synth::resolveAudioInput(named("Vocal mic", 1, 1, "Volt 2"), status);
    EXPECT_TRUE(resolved.connected);
    EXPECT_EQ(resolved.firstChannel, 1);
}

TEST(AudioInputByNameTest, TheRowReadsTheNameOrNotOnThisInterface) {
    const auto choice = named("Vocal mic", 0, 1, "Volt 2");
    auto found = synth::ui::describeAudioInput(choice, interfaceWith("Scarlett", {{2, 1, "Vocal mic"}}));
    EXPECT_EQ(found.text, "Vocal mic (In 3)");
    EXPECT_FALSE(found.warning);

    auto missing = synth::ui::describeAudioInput(choice, interfaceWith("Scarlett", {{0, 1, "Guitar"}}));
    EXPECT_EQ(missing.text, "Vocal mic (not on this interface)");
    EXPECT_TRUE(missing.warning);
    EXPECT_TRUE(missing.note.isNotEmpty());
}

TEST(AudioInputByNameTest, PickingAnyInputFromTheMenuRemapsTheNamedChoice) {
    const auto status = interfaceWith("Scarlett", {{0, 1, "Guitar"}, {1, 1, ""}});
    const auto action = synth::ui::resolveAudioInputMenuChoice(synth::ui::AudioInputMenuIds::kFirstMono + 1, status);
    ASSERT_EQ(action.kind, synth::ui::AudioInputMenuAction::Kind::Pick);
    EXPECT_EQ(action.choice.firstChannel, 1);
    EXPECT_TRUE(action.choice.inputName.isEmpty()) << "an unnamed input stores no name";

    const auto guitar = synth::ui::resolveAudioInputMenuChoice(synth::ui::AudioInputMenuIds::kFirstMono, status);
    EXPECT_EQ(guitar.choice.inputName, "Guitar") << "the pick stores the name of that input on this interface";
}

TEST(AudioInputByNameTest, TheNameSurvivesTheProjectFileAndAnOldFileLoadsUnchanged) {
    synth::TimelineDoc doc;
    const auto id = doc.addTrack(synth::TrackKind::Audio, "Vox");
    ASSERT_TRUE(doc.setTrackAudioInput(id, named("Vocal mic", 0, 2, "Volt 2")));
    const auto text = juce::JSON::toString(doc.toVar());
    EXPECT_TRUE(text.contains("\"name\": \"Vocal mic\"")) << text;

    synth::TimelineDoc loaded;
    ASSERT_TRUE(loaded.fromVar(juce::JSON::parse(text)));
    EXPECT_EQ(loaded.getTrack(id)->audioInput, named("Vocal mic", 0, 2, "Volt 2"));

    ASSERT_TRUE(doc.setTrackAudioInput(id, named({}, 0, 2, "Volt 2")));
    EXPECT_FALSE(juce::JSON::toString(doc.toVar()).contains("Vocal mic")) << "an empty name is left out";
    EXPECT_FALSE(juce::JSON::toString(doc.toVar()).contains("\"name\": \"\"")) << "no empty name key is written";

    synth::TimelineDoc old;
    ASSERT_TRUE(old.fromVar(juce::JSON::parse(
        R"({"version":1,"tracks":[{"id":1,"kind":1,"audioInput":{"kind":"device","channel":3,"channels":2,"device":"Volt 2"}}]})")));
    EXPECT_EQ(old.getTracks().front().audioInput, named({}, 2, 2, "Volt 2"));

    synth::TimelineDoc bad;
    EXPECT_FALSE(bad.fromVar(juce::JSON::parse(
        R"({"version":1,"tracks":[{"id":1,"kind":1,"audioInput":{"kind":"device","channel":1,"name":4}}]})")));
}

TEST(AudioInputByNameTest, ASnapshotMonitorsTheJacksTheNameResolvesToAndNothingWhenItIsMissing) {
    synth::TimelineDoc doc;
    const auto vox = doc.addTrack(synth::TrackKind::Audio, "Vox");
    ASSERT_TRUE(doc.setTrackAudioInput(vox, named("Vocal mic", 0, 1, "Volt 2")));

    int asked = 0;
    auto build = [&](const AudioInputStatus& status) {
        return synth::TimelineSnapshot::buildFrom(doc, 4.0, [&, status] {
            ++asked;
            return status;
        });
    };
    auto snapshot = build(interfaceWith("Scarlett", {{2, 2, "Vocal mic"}}));
    EXPECT_EQ(asked, 1);
    EXPECT_EQ(snapshot->tracks[0].inputFirstChannel, 2);
    EXPECT_EQ(snapshot->tracks[0].inputNumChannels, 2);
    EXPECT_EQ(snapshot->tracks[0].inputDeviceHash, 0) << "found by name on whatever is open";

    snapshot = build(interfaceWith("Scarlett", {{0, 1, "Guitar"}}));
    EXPECT_EQ(snapshot->tracks[0].inputNumChannels, 0) << "no such input here: nothing is monitored";

    ASSERT_TRUE(doc.setTrackAudioInput(vox, named({}, 0, 1, "Volt 2")));
    asked = 0;
    snapshot = build(interfaceWith("Scarlett", {}));
    EXPECT_EQ(asked, 0) << "an unnamed choice never asks the driver";
    EXPECT_EQ(snapshot->tracks[0].inputNumChannels, 1);
}
