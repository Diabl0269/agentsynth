// TrackRemovalSetTests.cpp: which modules deleting a track takes along (synth::modulesOnlyUsedBy), pure ids and cables.
#include "Timeline/TrackRemovalSet.h"
#include <gtest/gtest.h>

namespace {

using Cables = std::vector<std::pair<std::uint32_t, std::uint32_t>>;
using Ids = std::set<std::uint32_t>;

constexpr std::uint32_t kTrack = 1, kLfo = 2, kLfo2 = 3, kAtten = 4, kMaster = 5, kOther = 6;

TEST(TrackRemovalSet, ModuleWiredOnlyIntoTheTrackGoes) {
    EXPECT_EQ(synth::modulesOnlyUsedBy({{kLfo, kTrack}}, {kTrack}, {}), Ids{kLfo});
}

TEST(TrackRemovalSet, ChainOfModulatorsGoesToAFixedPoint) {
    const Cables cables{{kLfo, kLfo2}, {kLfo2, kTrack}};
    EXPECT_EQ(synth::modulesOnlyUsedBy(cables, {kTrack}, {}), (Ids{kLfo, kLfo2}));
}

TEST(TrackRemovalSet, AttenuverterBetweenAnLfoAndTheTrackLeavesWithBoth) {
    const Cables cables{{kLfo, kAtten}, {kAtten, kTrack}};
    EXPECT_EQ(synth::modulesOnlyUsedBy(cables, {kTrack}, {}), (Ids{kLfo, kAtten}));
}

TEST(TrackRemovalSet, SharedModulatorStaysAndSoDoesAnythingOnlyItFeeds) {
    const Cables cables{{kLfo, kTrack}, {kLfo, kOther}, {kLfo2, kLfo}};
    EXPECT_TRUE(synth::modulesOnlyUsedBy(cables, {kTrack}, {kOther}).empty());
}

TEST(TrackRemovalSet, ModuleWithNoCableAtAllIsNotUsedByTheTrack) {
    // A module with no cable never appears in the cable list; a pair wired only to each other stays too.
    const Cables cables{{kLfo, kTrack}, {kLfo2, kAtten}, {kAtten, kLfo2}};
    EXPECT_EQ(synth::modulesOnlyUsedBy(cables, {kTrack}, {}), Ids{kLfo});
}

TEST(TrackRemovalSet, KeptNodesStayAndTheirNeighboursWithThem) {
    // Master (kept) is wired to the track and to the LFO: the LFO has a cable that stays outside, so it stays.
    const Cables cables{{kTrack, kMaster}, {kLfo, kMaster}, {kLfo, kTrack}};
    EXPECT_TRUE(synth::modulesOnlyUsedBy(cables, {kTrack}, {kMaster}).empty());
}

TEST(TrackRemovalSet, SeedMembersNeverAppearInTheAnswer) {
    EXPECT_EQ(synth::modulesOnlyUsedBy({{kLfo, kTrack}, {kTrack, kLfo2}}, {kTrack, kLfo2}, {}), Ids{kLfo});
}

TEST(TrackRemovalSet, RelayIntoTheTrackGoesWhileItsSharedSourceStays) {
    // kLfo drives kTrack through relay kAtten and kOther through relay kAtten2 (outside, kept).
    constexpr std::uint32_t kAtten2 = 8;
    const Cables cables{{kLfo, kAtten}, {kAtten, kTrack}, {kLfo, kAtten2}, {kAtten2, kOther}};
    EXPECT_EQ(synth::modulesOnlyUsedBy(cables, {kTrack}, {kOther}, {kAtten, kAtten2}), Ids{kAtten});
}

TEST(TrackRemovalSet, RelayWithNothingElseToFeedTakesItsOnlySourceToo) {
    const Cables cables{{kLfo, kAtten}, {kAtten, kTrack}};
    EXPECT_EQ(synth::modulesOnlyUsedBy(cables, {kTrack}, {}, {kAtten}), (Ids{kLfo, kAtten}));
}

} // namespace
