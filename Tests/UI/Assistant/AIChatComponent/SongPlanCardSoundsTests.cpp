// SongPlanCardSoundsTests.cpp -- the plan card's offer to download the core instruments before building a song that
// names them: made only when needed, Download first builds after the install, Continue with synths builds at once,
// and a failed download keeps both choices. The installer is the app's shared one, faked at the network.
#include "../../../SoundPacks/SoundPackTestKit.h"
#include "../../Accessibility/AccessibilityAudit.h"
#include "UI/Assistant/AIChatComponent/SongPlanCard.h"
#include <gtest/gtest.h>

using namespace sound_pack_test;
using synth::SongPlanCard;
using synth::SongSession;

namespace {

constexpr const char* kSoundsPlan =
    R"({"plan":{"title":"Sunday jazz","bpm":96,"key":"C major","special":"",
    "sections":[{"name":"Head","bars":16,"energy":3,"chords":"C","description":"theme"}],
    "tracks":[{"name":"Piano","role":"keys","instrument":"Mallet","sound":"x","coreSound":"core/upright-piano","sections":["Head"]},
              {"name":"Bass","role":"bass","instrument":"Pluck","sound":"y","coreSound":"core/double-bass-plucked","sections":["Head"]},
              {"name":"Kit","role":"drums","instrument":"Drum Kit","sound":"z","coreSound":"core/acoustic-kit","sections":["Head"]}],
    "lanes":[]},"price":{"units":5,"estimatedUsd":0.02},
    "steps":[{"id":"sections","kind":"sections","label":"Laying out 1 sections"}],"token":"tok"})";

constexpr const char* kPlainPlan =
    R"({"plan":{"title":"Plain","bpm":96,"key":"C major","special":"",
    "sections":[{"name":"Head","bars":16,"energy":3,"chords":"C","description":"theme"}],
    "tracks":[{"name":"Lead","role":"lead","instrument":"Oscillator","sound":"x","sections":["Head"]}],"lanes":[]},
    "price":{"units":5,"estimatedUsd":0.02},"steps":[{"id":"sections","kind":"sections","label":"Laying out"}],"token":"tok"})";

SongSession sessionFor(const char* planJson) {
    SongSession session;
    juce::String error;
    EXPECT_TRUE(synth::songsteps::parsePlanReply(planJson, session.reply, error)) << error;
    session.adoptPlan(session.reply, true);
    return session;
}

// The shared installer pointed at a scratch folder and a fake server, with the core pack published but not installed.
struct OfferRig {
    juce::SharedResourcePointer<SoundPackInstaller> shared;
    TempDir dir;
    FakeServer server;
    juce::MemoryBlock zip = goodPackZip("core", "1.0.0");

    OfferRig() {
        synth::sampler::SoundPackLibrary::setRootsForTests(std::vector<juce::File>{dir.path()});
        shared->setInstallRootForTesting(dir.path());
        shared->setIndexUrlForTesting(kIndexUrl);
        shared->setFetcherForTesting(server.fetcher());
        shared->setRunInlineForTesting(true);
        const auto json = indexJson("core", "1.0.0", zip);
        server.bodies[kIndexUrl] = juce::MemoryBlock(json.toRawUTF8(), json.getNumBytesAsUTF8());
        server.bodies["https://example.test/core-1.0.0.zip"] = zip;
        shared->refreshIndex(); // a fresh list for every test, whatever the last one left
    }
    ~OfferRig() {
        synth::sampler::SoundPackLibrary::setRootsForTests(std::nullopt);
        shared->setFetcherForTesting(synth::soundpacks::makeHttpFetcher());
    }

    void installAlready() {
        const auto pack = dir.path().getChildFile("core");
        pack.createDirectory();
        pack.getChildFile("pack.json").replaceWithText(manifestJson("core", "1.0.0"));
        pack.getChildFile("piano").createDirectory();
        pack.getChildFile("piano/piano.sfz").replaceWithText("<region> sample=x.wav");
        shared->refreshIndex();
    }

    juce::String sizeText() const { return synth::soundpacks::formatPackSize((juce::int64)zip.getSize()); }
};

struct Counter {
    int builds = 0;
    SongPlanCard::Actions actions() {
        SongPlanCard::Actions a;
        a.onBuild = [this] { ++builds; };
        return a;
    }
};

void size(SongPlanCard& card) { card.setSize(300, card.getRequiredHeight(300)); }

} // namespace

TEST(SongPlanCardSounds, NoOfferWhenThePlanNamesNoCoreInstruments) {
    OfferRig rig;
    auto session = sessionFor(kPlainPlan);
    SongPlanCard card(session, {});
    size(card);
    EXPECT_FALSE(card.soundsOfferShownForTesting());
    EXPECT_EQ(card.buildButtonText(), juce::String::fromUTF8("Build song \xC2\xB7 5 uses"));
}

TEST(SongPlanCardSounds, NoOfferWhenThePackIsAlreadyInstalled) {
    OfferRig rig;
    rig.installAlready();
    auto session = sessionFor(kSoundsPlan);
    SongPlanCard card(session, {});
    size(card);
    EXPECT_FALSE(card.soundsOfferShownForTesting());
    EXPECT_EQ(card.buildButtonText(), juce::String::fromUTF8("Build song \xC2\xB7 5 uses"));
}

TEST(SongPlanCardSounds, OffersTheDownloadNamingTheInstrumentsAndTheSize) {
    OfferRig rig;
    auto session = sessionFor(kSoundsPlan);
    SongPlanCard card(session, {});
    size(card);
    EXPECT_TRUE(card.soundsOfferShownForTesting());
    EXPECT_EQ(card.soundsLineForTesting(),
              "Uses real instruments: Upright piano, Plucked double bass, Acoustic drum kit. Download the core "
              "instruments (" +
                  rig.sizeText() + ") first?");
    EXPECT_EQ(card.buildButtonText(), "Download first");
    EXPECT_TRUE(card.continueWithSynthsForTesting().isVisible());
}

TEST(SongPlanCardSounds, TheSizeIsLeftOutWhenThePackListDoesNotKnowTheCorePack) {
    OfferRig rig;
    const auto other = indexJson("other", "1.0.0", rig.zip);
    rig.server.bodies[kIndexUrl] = juce::MemoryBlock(other.toRawUTF8(), other.getNumBytesAsUTF8());
    rig.shared->refreshIndex();
    auto session = sessionFor(kSoundsPlan);
    SongPlanCard card(session, {});
    size(card);
    EXPECT_EQ(card.soundsLineForTesting(),
              "Uses real instruments: Upright piano, Plucked double bass, Acoustic drum kit. Download the core "
              "instruments first?");
}

TEST(SongPlanCardSounds, DownloadFirstInstallsThePackThenBuilds) {
    OfferRig rig;
    Counter counter;
    auto session = sessionFor(kSoundsPlan);
    SongPlanCard card(session, counter.actions());
    size(card);
    juce::String during;
    rig.server.onChunk = [&] { during = card.soundsLineForTesting(); };

    card.pressBuildForTesting();

    EXPECT_TRUE(during.startsWith("Downloading the core instruments ")) << during;
    EXPECT_EQ(counter.builds, 1) << "built once the pack was installed";
    EXPECT_TRUE(rig.shared->row("core")->state == SoundPackInstaller::State::Installed);
    EXPECT_FALSE(session.buildAfterDownload);
}

TEST(SongPlanCardSounds, ContinueWithSynthsBuildsAtOnceWithoutDownloading) {
    OfferRig rig;
    Counter counter;
    auto session = sessionFor(kSoundsPlan);
    SongPlanCard card(session, counter.actions());
    size(card);

    card.continueWithSynthsForTesting().onClick();

    EXPECT_EQ(counter.builds, 1);
    EXPECT_TRUE(rig.shared->row("core")->state == SoundPackInstaller::State::NotInstalled);
}

TEST(SongPlanCardSounds, AFailedDownloadSaysSoAndKeepsBothChoices) {
    OfferRig rig;
    Counter counter;
    auto session = sessionFor(kSoundsPlan);
    SongPlanCard card(session, counter.actions());
    size(card);
    rig.server.offline = true;

    card.pressBuildForTesting();

    EXPECT_EQ(card.soundsLineForTesting(), "Couldn't download the instruments.");
    EXPECT_EQ(counter.builds, 0);
    EXPECT_EQ(card.buildButtonText(), "Download first");
    EXPECT_TRUE(card.continueWithSynthsForTesting().isEnabled());

    rig.server.offline = false;
    card.pressBuildForTesting();
    EXPECT_EQ(counter.builds, 1) << "trying again works";
}

TEST(SongPlanCardSounds, EveryControlOfTheOfferHasANameAndATooltip) {
    OfferRig rig;
    auto session = sessionFor(kSoundsPlan);
    SongPlanCard card(session, {});
    size(card);
    EXPECT_EQ(card.continueWithSynthsForTesting().getTitle(), "Continue with synths");
    EXPECT_TRUE(card.continueWithSynthsForTesting().getTooltip().isNotEmpty());
    EXPECT_TRUE(card.buildButtonText() == "Download first");
    for (const auto& gap : synth::test::auditAccessibility(card))
        ADD_FAILURE() << (gap.kind == synth::test::Gap::Kind::MissingName ? "missing name " : "missing tooltip ")
                      << gap.path;
}
