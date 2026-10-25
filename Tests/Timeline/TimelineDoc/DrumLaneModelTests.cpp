// The drum machine's per-hit settings (chance, repeat, flam, nudge, condition) and the clip's humanise and per-drum
// length/swing: the setters, the file format (round trip, defaults omitted, invalid rejected), linked sync, and every
// path that copies a clip or a note.

#include "TimelineDocTestHelpers.h"

namespace {

struct LaneFixture {
    TimelineDoc doc;
    TrackId track = doc.addTrack(TrackKind::Midi, "Drums");
    ClipId clip = doc.addClip(track, 0.0, 4.0, "Beat");
    NoteId note = doc.addNote(clip, makeNote(1.0, 42, 0.25));

    const MidiNote& n() const { return doc.getClip(clip)->notes.front(); }
    juce::String json() const { return juce::JSON::toString(doc.toVar()); }
    static bool loads(const juce::String& text) {
        juce::var parsed;
        TimelineDoc loaded;
        return juce::JSON::parse(text, parsed).wasOk() && loaded.fromVar(parsed);
    }
};

void setAllNoteFields(LaneFixture& f) {
    ASSERT_TRUE(f.doc.setNoteChance(f.note, 40));
    ASSERT_TRUE(f.doc.setNoteRepeat(f.note, 3, true));
    ASSERT_TRUE(f.doc.setNoteNudge(f.note, -0.25));
    ASSERT_TRUE(f.doc.setNoteCondition(f.note, 2, 4));
}

void expectAllNoteFields(const MidiNote& n) {
    EXPECT_EQ(n.chance, 40);
    EXPECT_EQ(n.repeat, 3);
    EXPECT_TRUE(n.flam);
    EXPECT_DOUBLE_EQ(n.nudge, -0.25);
    EXPECT_EQ(n.condHit, 2);
    EXPECT_EQ(n.condOf, 4);
}

} // namespace

TEST(DrumLaneModelTest, DefaultsAreAlwaysPlayAndAreOmittedFromTheFile) {
    LaneFixture f;
    EXPECT_EQ(f.n().chance, 100);
    EXPECT_EQ(f.n().repeat, 1);
    EXPECT_FALSE(f.n().flam);
    EXPECT_DOUBLE_EQ(f.n().nudge, 0.0);
    EXPECT_EQ(f.n().condHit, 1);
    EXPECT_EQ(f.n().condOf, 1);
    const auto text = f.json();
    for (const char* key : {"chance", "repeat", "flam", "nudge", "condition", "humanise", "drumRows"})
        EXPECT_FALSE(text.contains(key)) << key;
}

TEST(DrumLaneModelTest, NoteSettingsRoundTripAndWriteTheSequencersKeys) {
    LaneFixture f;
    setAllNoteFields(f);
    EXPECT_TRUE(f.json().contains("\"condition\": \"2/4\""));
    TimelineDoc loaded;
    ASSERT_TRUE(loaded.fromVar(f.doc.toVar()));
    expectAllNoteFields(loaded.getClip(f.clip)->notes.front());

    ASSERT_TRUE(f.doc.setNoteRepeat(f.note, 1, false));
    ASSERT_TRUE(f.doc.setNoteChance(f.note, 100));
    EXPECT_FALSE(f.json().contains("repeat"));
    EXPECT_FALSE(f.json().contains("chance"));
}

TEST(DrumLaneModelTest, ClipSettingsRoundTrip) {
    LaneFixture f;
    ASSERT_TRUE(f.doc.setClipHumanise(f.clip, 0.4));
    ASSERT_TRUE(f.doc.setClipRowLength(f.clip, 42, 12));
    ASSERT_TRUE(f.doc.setClipRowSwing(f.clip, 42, 0.6));
    ASSERT_TRUE(f.doc.setClipRowLength(f.clip, 36, 6));
    TimelineDoc loaded;
    ASSERT_TRUE(loaded.fromVar(f.doc.toVar()));
    const auto* c = loaded.getClip(f.clip);
    EXPECT_DOUBLE_EQ(c->humanise, 0.4);
    ASSERT_EQ(c->drumRows.size(), 2u);
    EXPECT_EQ(c->drumRows[0].pitch, 36) << "sorted by pitch";
    EXPECT_EQ(c->drumRows[0].lengthSteps, 6);
    EXPECT_LT(c->drumRows[0].swing, 0.0);
    EXPECT_EQ(c->drumRows[1].lengthSteps, 12);
    EXPECT_DOUBLE_EQ(c->drumRows[1].swing, 0.6);

    const auto text = f.json();
    EXPECT_TRUE(text.contains("\"length\": 6"));
    EXPECT_EQ(text.indexOf("\"swing\""), text.lastIndexOf("\"swing\"")) << "row 36 omits swing";
}

TEST(DrumLaneModelTest, ARowBackAtDefaultsIsRemoved) {
    LaneFixture f;
    ASSERT_TRUE(f.doc.setClipRowLength(f.clip, 42, 12));
    ASSERT_TRUE(f.doc.setClipRowSwing(f.clip, 42, 0.6));
    ASSERT_TRUE(f.doc.setClipRowLength(f.clip, 42, 0));
    ASSERT_EQ(f.doc.getClip(f.clip)->drumRows.size(), 1u) << "swing still set";
    ASSERT_TRUE(f.doc.setClipRowSwing(f.clip, 42, -1.0));
    EXPECT_TRUE(f.doc.getClip(f.clip)->drumRows.empty());
    EXPECT_FALSE(f.json().contains("drumRows"));
    ASSERT_TRUE(f.doc.setClipHumanise(f.clip, 0.4));
    ASSERT_TRUE(f.doc.setClipHumanise(f.clip, 0.0));
    EXPECT_FALSE(f.json().contains("humanise"));
}

TEST(DrumLaneModelTest, SettersRejectOutOfRangeAndNoOpsDoNotNotify) {
    LaneFixture f;
    EXPECT_FALSE(f.doc.setNoteChance(f.note, -1));
    EXPECT_FALSE(f.doc.setNoteChance(f.note, 101));
    EXPECT_FALSE(f.doc.setNoteRepeat(f.note, 0, false));
    EXPECT_FALSE(f.doc.setNoteRepeat(f.note, 9, false));
    EXPECT_FALSE(f.doc.setNoteNudge(f.note, 0.51));
    EXPECT_FALSE(f.doc.setNoteNudge(f.note, std::nan("")));
    EXPECT_FALSE(f.doc.setNoteCondition(f.note, 0, 4));
    EXPECT_FALSE(f.doc.setNoteCondition(f.note, 5, 4));
    EXPECT_FALSE(f.doc.setNoteCondition(f.note, 1, 9));
    EXPECT_FALSE(f.doc.setNoteChance(NoteId{9999}, 50));
    EXPECT_FALSE(f.doc.setClipHumanise(f.clip, 1.1));
    EXPECT_FALSE(f.doc.setClipHumanise(f.clip, std::nan("")));
    EXPECT_FALSE(f.doc.setClipRowLength(f.clip, 42, -1));
    EXPECT_FALSE(f.doc.setClipRowLength(f.clip, 42, Clip::kMaxRowLengthSteps + 1));
    EXPECT_FALSE(f.doc.setClipRowLength(f.clip, 128, 4));
    EXPECT_FALSE(f.doc.setClipRowSwing(f.clip, 42, 0.76));
    EXPECT_FALSE(f.doc.setClipRowSwing(f.clip, 42, std::nan("")));
    EXPECT_EQ(f.n().chance, 100);
    EXPECT_TRUE(f.doc.getClip(f.clip)->drumRows.empty());

    CountingListener listener;
    f.doc.addListener(&listener);
    EXPECT_TRUE(f.doc.setNoteChance(f.note, 100));
    EXPECT_TRUE(f.doc.setNoteRepeat(f.note, 1, false));
    EXPECT_TRUE(f.doc.setNoteCondition(f.note, 1, 1));
    EXPECT_TRUE(f.doc.setClipRowLength(f.clip, 42, 0));
    EXPECT_TRUE(f.doc.setClipRowSwing(f.clip, 42, -3.0));
    EXPECT_EQ(listener.calls, 0);
    EXPECT_TRUE(f.doc.setNoteChance(f.note, 50));
    EXPECT_EQ(listener.calls, 1);
    f.doc.removeListener(&listener);
}

TEST(DrumLaneModelTest, TheLoaderRejectsInvalidValues) {
    LaneFixture f;
    setAllNoteFields(f);
    ASSERT_TRUE(f.doc.setClipHumanise(f.clip, 0.4));
    ASSERT_TRUE(f.doc.setClipRowLength(f.clip, 42, 12));
    const auto text = f.json();
    ASSERT_TRUE(LaneFixture::loads(text));

    // Each pair is {text in the saved file, an invalid replacement}; the replacement must be found, or the test is
    // not testing anything.
    const std::pair<const char*, const char*> bad[] = {
        {"\"chance\": 40", "\"chance\": 101"},
        {"\"repeat\": 3", "\"repeat\": 9"},
        {"\"repeat\": 3", "\"repeat\": 0"},
        {"\"flam\": true", "\"flam\": 1"},
        {"-0.25", "0.75"},
        {"\"2/4\"", "\"5/4\""},
        {"\"2/4\"", "\"0/4\""},
        {"\"2/4\"", "\"2/9\""},
        {"\"2/4\"", "\"two\""},
        {"\"2/4\"", "\"2/\""},
        {"\"2/4\"", "3"},
        {"\"humanise\": 0.4", "\"humanise\": 1.5"},
        {"\"length\": 12", "\"length\": 300"},
        {"\"length\": 12", "\"length\": 12, \"swing\": 0.9"},
        {"\"length\": 12", "\"length\": \"x\""},
    };
    for (const auto& [from, to] : bad) {
        ASSERT_TRUE(text.contains(from)) << from;
        EXPECT_FALSE(LaneFixture::loads(text.replace(from, to))) << from << " -> " << to;
    }
}

TEST(DrumLaneModelTest, TheLoaderRejectsDuplicateAndEmptyRows) {
    LaneFixture f;
    ASSERT_TRUE(f.doc.setClipRowLength(f.clip, 42, 12));
    ASSERT_TRUE(f.doc.setClipRowLength(f.clip, 36, 6));
    const auto text = f.json();
    ASSERT_TRUE(LaneFixture::loads(text));
    ASSERT_TRUE(text.contains("\"pitch\": 36"));
    EXPECT_FALSE(LaneFixture::loads(text.replace("\"pitch\": 36", "\"pitch\": 42")));
    EXPECT_FALSE(LaneFixture::loads(text.replace("\"length\": 6", "\"length\": 0")));
}

TEST(DrumLaneModelTest, LinkedClipsShareTheSettingsAndTheNoteFields) {
    LaneFixture f;
    const auto twin = f.doc.duplicateClipLinked(f.clip);
    ASSERT_TRUE(twin.isValid());
    ASSERT_TRUE(f.doc.setClipHumanise(f.clip, 0.3));
    ASSERT_TRUE(f.doc.setClipRowLength(f.clip, 42, 12));
    ASSERT_TRUE(f.doc.setClipRowSwing(twin, 42, 0.7));
    for (const auto id : {f.clip, twin}) {
        const auto* c = f.doc.getClip(id);
        EXPECT_DOUBLE_EQ(c->humanise, 0.3);
        ASSERT_EQ(c->drumRows.size(), 1u);
        EXPECT_EQ(c->drumRows[0].lengthSteps, 12);
        EXPECT_DOUBLE_EQ(c->drumRows[0].swing, 0.7);
    }

    f.note = f.n().id; // linking may re-id the source's notes
    setAllNoteFields(f);
    expectAllNoteFields(f.doc.getClip(twin)->notes.front());
    ASSERT_TRUE(f.doc.setClipHumanise(twin, 0.0));
    EXPECT_DOUBLE_EQ(f.doc.getClip(f.clip)->humanise, 0.0) << "an edit on either copy reaches the whole group";
}

TEST(DrumLaneModelTest, SplitDuplicateAndRangeCopiesCarryEveryField) {
    LaneFixture f;
    setAllNoteFields(f);
    ASSERT_TRUE(f.doc.setClipHumanise(f.clip, 0.3));
    ASSERT_TRUE(f.doc.setClipRowLength(f.clip, 42, 12));
    ASSERT_TRUE(f.doc.setClipRowSwing(f.clip, 42, 0.7));

    const auto check = [&](ClipId id) {
        const auto* c = f.doc.getClip(id);
        ASSERT_NE(c, nullptr);
        EXPECT_DOUBLE_EQ(c->humanise, 0.3);
        ASSERT_EQ(c->drumRows.size(), 1u);
        EXPECT_EQ(c->drumRows[0].lengthSteps, 12);
        EXPECT_DOUBLE_EQ(c->drumRows[0].swing, 0.7);
    };

    const auto copy = f.doc.duplicateClip(f.clip);
    check(copy);
    expectAllNoteFields(f.doc.getClip(copy)->notes.front());

    const auto halves = f.doc.splitClip(f.clip, 2.0);
    ASSERT_TRUE(halves.second.isValid());
    check(halves.second);
    check(f.clip);
    expectAllNoteFields(f.doc.getClip(f.clip)->notes.front());

    const auto fragment = TimelineDoc::clipToRange(*f.doc.getClip(copy), f.doc.getClip(copy)->startBeat,
                                                   f.doc.getClip(copy)->startBeat + 2.0, 0.5);
    ASSERT_TRUE(fragment.has_value());
    EXPECT_DOUBLE_EQ(fragment->humanise, 0.3);
    EXPECT_EQ(fragment->drumRows.size(), 1u);
    expectAllNoteFields(fragment->notes.front());
}

TEST(DrumLaneModelTest, QuantiseKeepsTheNoteFields) {
    LaneFixture f;
    setAllNoteFields(f);
    ASSERT_TRUE(f.doc.quantiseNotes(f.clip, 0.5, 1.0));
    expectAllNoteFields(f.doc.getClip(f.clip)->notes.front());
}
