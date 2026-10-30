// Concern: KeyAuditioner, the scale a key picker plays while the user hovers a key: which notes, where they go, that
// every note-on gets its note-off at the target it went to, the default sound when nothing names a target, and the
// setting that silences it. The new API fails on code that lacks it by not compiling.
#include "Music/SongKey.h"
#include "UI/Layout/KeyPicker/KeyAuditioner.h"
#include <gtest/gtest.h>
#include <juce_data_structures/juce_data_structures.h>

using synth::ui::KeyAuditioner;

namespace {

struct Note {
    int target;
    int pitch;
    bool on;
};

struct Rig {
    std::vector<Note> notes;
    std::vector<std::vector<int>> defaultRuns;
    int defaultStops = 0;
    int target = 1;
    KeyAuditioner auditioner; // last: its destructor ends a held note into the members above

    Rig() {
        auditioner.resolveTarget = [this]() -> KeyAuditioner::Target {
            const int t = target;
            return {[this, t](int pitch, int, bool on) { notes.push_back({t, pitch, on}); }};
        };
        auditioner.playDefaultSound = [this](const std::vector<int>& pitches) { defaultRuns.push_back(pitches); };
        auditioner.stopDefaultSound = [this] { ++defaultStops; };
    }

    void runToEnd() {
        for (int i = 0; i < 30 && auditioner.isAuditioning(); ++i)
            auditioner.advanceForTest();
    }
    int open() const {
        int n = 0;
        for (const auto& note : notes)
            n += note.on ? 1 : -1;
        return n;
    }
};

synth::MusicalScale major(int root = 0) { return synth::makeScale(root, synth::scalePresetIndex("Major")); }

struct Settings {
    juce::ApplicationProperties app;
    juce::PropertiesFile& props;
    Settings()
        : props(*(configure(app), app.getUserSettings())) {
        props.clear();
    }
    ~Settings() { props.clear(); }
    static void configure(juce::ApplicationProperties& app) {
        juce::PropertiesFile::Options options;
        options.applicationName = "KeyAuditionerTest";
        options.filenameSuffix = "test";
        options.storageFormat = juce::PropertiesFile::storeAsXML;
        app.setStorageParameters(options);
    }
};

} // namespace

TEST(KeyAuditioner, PlaysTheRootAtMiddleCUpToTheRootAnOctaveHigh) {
    Rig rig;
    rig.auditioner.start(major());
    rig.runToEnd();
    std::vector<int> ons;
    for (const auto& n : rig.notes)
        if (n.on)
            ons.push_back(n.pitch);
    EXPECT_EQ(ons, (std::vector<int>{60, 62, 64, 65, 67, 69, 71, 72}));
    EXPECT_EQ(rig.open(), 0) << "every note-on got its note-off";
    EXPECT_FALSE(rig.auditioner.isAuditioning());
    EXPECT_EQ(KeyAuditioner::pitchesFor(major(2)).front(), 62) << "the root's own pitch class, at middle C's octave";
}

TEST(KeyAuditioner, StoppingSendsTheNoteOffItStillOwes) {
    Rig rig;
    rig.auditioner.start(major());
    rig.auditioner.advanceForTest();
    ASSERT_EQ(rig.open(), 1) << "a note is sounding";
    rig.auditioner.stop();
    EXPECT_EQ(rig.open(), 0);
    EXPECT_FALSE(rig.auditioner.isAuditioning());
}

TEST(KeyAuditioner, TheNoteOffGoesWhereTheNoteOnWentEvenIfTheTargetChangesMidRun) {
    Rig rig;
    rig.auditioner.start(major());
    rig.auditioner.advanceForTest();
    rig.target = 2; // the selection moved, a new clip opened
    rig.auditioner.stop();
    ASSERT_FALSE(rig.notes.empty());
    for (const auto& n : rig.notes)
        EXPECT_EQ(n.target, 1);
    EXPECT_EQ(rig.open(), 0);
}

TEST(KeyAuditioner, DestroyingItEndsTheNoteItWasHolding) {
    std::vector<Note> notes;
    {
        KeyAuditioner auditioner;
        auditioner.resolveTarget = [&notes]() -> KeyAuditioner::Target {
            return {[&notes](int pitch, int, bool on) { notes.push_back({0, pitch, on}); }};
        };
        auditioner.start(major());
        auditioner.advanceForTest();
    }
    int open = 0;
    for (const auto& n : notes)
        open += n.on ? 1 : -1;
    EXPECT_EQ(open, 0);
}

TEST(KeyAuditioner, NothingPlaysWhileTheSettingIsOffAndItIsReadAtEachStart) {
    Settings settings;
    Rig rig;
    rig.auditioner.setSettings(&settings.props);
    EXPECT_TRUE(KeyAuditioner::isEnabledIn(&settings.props)) << "on until the user turns it off";
    settings.props.setValue(synth::kPlayScaleOnBrowseSettingsKey, "0");
    rig.auditioner.start(major());
    EXPECT_FALSE(rig.auditioner.isAuditioning());
    EXPECT_TRUE(rig.notes.empty());
    EXPECT_TRUE(rig.defaultRuns.empty());

    settings.props.setValue(synth::kPlayScaleOnBrowseSettingsKey, "1");
    rig.auditioner.start(major());
    EXPECT_TRUE(rig.auditioner.isAuditioning());
}

TEST(KeyAuditioner, WithNoTargetItPlaysTheWholeRunWithTheDefaultSoundAndCutsItOnStop) {
    Rig rig;
    rig.auditioner.resolveTarget = [] { return KeyAuditioner::Target{}; };
    rig.auditioner.start(major());
    ASSERT_EQ(rig.defaultRuns.size(), 1u);
    EXPECT_EQ(rig.defaultRuns[0], (std::vector<int>{60, 62, 64, 65, 67, 69, 71, 72}));
    EXPECT_TRUE(rig.notes.empty());
    EXPECT_TRUE(rig.auditioner.isUsingDefaultSound());
    rig.auditioner.stop();
    EXPECT_EQ(rig.defaultStops, 1) << "a pick or a close while it rings cuts it";
    EXPECT_FALSE(rig.auditioner.isAuditioning());
}

TEST(KeyAuditioner, AnUnsetResolverMeansTheDefaultSound) {
    KeyAuditioner auditioner;
    int runs = 0;
    auditioner.playDefaultSound = [&runs](const std::vector<int>&) { ++runs; };
    auditioner.start(major());
    EXPECT_EQ(runs, 1);
}
