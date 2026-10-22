// SoundNameGuessTests.cpp -- tags, tempo, key and loop hints read from file and folder names.
#include "SoundBrowser/SoundNameGuess.h"
#include <gtest/gtest.h>

using synth::soundbrowser::guessFromName;

namespace {

struct Row {
    const char* folder;
    const char* file;
    const char* tags; // comma-joined, in order
    double tempo;
    const char* key;
    bool loop;
};

} // namespace

TEST(SoundNameGuessTest, NamesAndFoldersYieldTagsTempoKeyAndLoopHint) {
    const Row rows[] = {
        {"", "kick-808-long", "Drums", 0, "", false},
        {"Drums/Snares", "acoustic-snare-center", "Drums", 0, "", false},
        {"Kicks", "01", "Drums", 0, "", false},
        {"", "closed_hat_03", "Drums", 0, "", false},
        {"Percussion", "shaker", "Drums", 0, "", false},
        {"", "sub_bass_hit", "Bass", 0, "", false},
        {"Keys", "rhodes_chord", "Keys", 0, "", false},
        {"", "Grand Piano C4", "Keys", 0, "", false},
        {"Vox", "ahh", "Vocals", 0, "", false},
        {"", "vocal_chop", "Vocals", 0, "", false},
        {"", "riser_long", "FX", 0, "", false},
        {"", "string_pad", "Strings", 0, "", false},
        {"", "dusty-keys-loop", "Keys", 0, "", true},
        {"", "dusty_96bpm", "", 96, "", false},
        {"", "break 120 BPM", "", 120, "", false},
        {"", "melody_120_Am", "", 120, "A minor", false},
        {"", "pad_C#min", "", 0, "C# minor", false},
        {"", "lead F major", "", 0, "F major", false},
        {"", "bass_Ebm_90bpm", "Bass", 90, "Eb minor", false},
        {"Loops/110 BPM", "groove", "", 110, "", true},
        // A trailing sample number is not a tempo, a number outside 60..200 is not either.
        {"", "kick_100", "Drums", 0, "", false},
        {"", "loop_250_x", "", 0, "", true},
        {"", "take_30_x", "", 0, "", false},
        // A lone note letter or a plain word is not a key.
        {"", "a_bass_b", "Bass", 0, "", false},
        {"", "bassline", "", 0, "", false},
    };
    for (const auto& r : rows) {
        const auto g = guessFromName(r.folder, r.file);
        EXPECT_EQ(g.tags.joinIntoString(","), juce::String(r.tags)) << r.file;
        EXPECT_EQ(g.tempo, r.tempo) << r.file;
        EXPECT_EQ(g.key, juce::String(r.key)) << r.file;
        EXPECT_EQ(g.loopHint, r.loop) << r.file;
    }
}

TEST(SoundNameGuessTest, NoteNamesIgnoreTheOctave) {
    EXPECT_EQ(synth::soundbrowser::noteNameOf(60), "C");
    EXPECT_EQ(synth::soundbrowser::noteNameOf(57), "A");
    EXPECT_EQ(synth::soundbrowser::noteNameOf(61), "C#");
}
