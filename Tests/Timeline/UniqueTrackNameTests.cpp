// UniqueTrackNameTests.cpp -- synth::uniqueNameAmong, the rule that names a plugin track "Diva", then "Diva 2".
#include "Timeline/UniqueTrackName.h"
#include <gtest/gtest.h>

TEST(UniqueTrackName, ABaseNobodyUsesIsReturnedBare) {
    EXPECT_EQ(synth::uniqueNameAmong("Diva", {}), "Diva");
    EXPECT_EQ(synth::uniqueNameAmong("Diva", {"Serum", "Track 1"}), "Diva");
}

TEST(UniqueTrackName, ATakenBaseGetsTheLowestFreeSuffixFromTwo) {
    EXPECT_EQ(synth::uniqueNameAmong("Diva", {"Diva"}), "Diva 2");
    EXPECT_EQ(synth::uniqueNameAmong("Diva", {"Diva", "Diva 2"}), "Diva 3");
    EXPECT_EQ(synth::uniqueNameAmong("Diva", {"Diva", "Diva 3"}), "Diva 2") << "a gap is reused, not skipped";
}

TEST(UniqueTrackName, ASuffixedNameAloneDoesNotTakeTheBareBase) {
    EXPECT_EQ(synth::uniqueNameAmong("Diva", {"Diva 2"}), "Diva");
}
