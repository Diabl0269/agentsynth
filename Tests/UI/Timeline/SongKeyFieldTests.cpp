// Concern: the transport bar's song key field: what it shows, the picker it opens, picking a key, and the scale
// audition's note-ons and note-offs.
#include "Music/SongKey.h"
#include "Transport/TransportService.h"
#include "UI/Layout/KeyPicker/KeyPicker.h"
#include "UI/Timeline/SongKeyField.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

using namespace synth;
using synth::ui::KeyPicker;
using synth::ui::SongKeyField;

namespace {

struct NoteEvent {
    TrackId track;
    int pitch;
    bool on;
};

class SongKeyFieldTest : public ::testing::Test {
protected:
    TransportService transport;
    TimelineDoc doc;
    TrackId track = doc.addTrack(TrackKind::Midi, "Keys");
    std::vector<NoteEvent> events;
    std::unique_ptr<juce::Component> launched;

    void wire(SongKeyField& field) {
        field.setTransport(&transport);
        field.resolveAuditionTrack = [this]() -> std::optional<TrackId> { return track; };
        field.auditionNote = [this](TrackId t, int pitch, int, bool on) { events.push_back({t, pitch, on}); };
        field.pickerLauncher = [this](std::unique_ptr<juce::Component> picker, juce::Rectangle<int>) {
            launched = std::move(picker);
        };
    }

    int onCount() const {
        int n = 0;
        for (const auto& e : events)
            n += e.on ? 1 : -1;
        return n;
    }
};

} // namespace

TEST_F(SongKeyFieldTest, ShowsNoKeyUntilTheTransportHasOne) {
    SongKeyField field;
    wire(field);
    field.refreshFromTransport();
    EXPECT_EQ(field.getText(), "No key");
    transport.setSongKey(makeScale(2, scalePresetIndex("Dorian")));
    field.refreshFromTransport();
    EXPECT_EQ(field.getText(), "D Dorian");
    EXPECT_EQ(field.getTitle(), "Song key");
    EXPECT_TRUE(field.getTooltip().isNotEmpty());
}

TEST_F(SongKeyFieldTest, PickingFromThePickerSetsTheSongKey) {
    SongKeyField field;
    wire(field);
    field.openPicker();
    auto* picker = dynamic_cast<KeyPicker*>(launched.get());
    ASSERT_NE(picker, nullptr);
    EXPECT_EQ(field.getOpenPicker(), picker);

    picker->setQuery("d dor");
    picker->pickHighlighted();
    const auto& key = transport.getDocumentState().songKey;
    ASSERT_TRUE(key.has_value());
    EXPECT_EQ(keyDisplayName(*key), "D Dorian");
    EXPECT_EQ(field.getText(), "D Dorian");
}

TEST_F(SongKeyFieldTest, NoKeyClearsTheSongKey) {
    transport.setSongKey(makeScale(0, 0));
    SongKeyField field;
    wire(field);
    field.openPicker();
    auto* picker = dynamic_cast<KeyPicker*>(launched.get());
    ASSERT_NE(picker, nullptr);
    picker->clickNoKey();
    EXPECT_FALSE(transport.getDocumentState().songKey.has_value());
    EXPECT_EQ(field.getText(), "No key");
}

TEST_F(SongKeyFieldTest, DetectIsOfferedOnlyWhenTheSongCanBeRead) {
    {
        SongKeyField field;
        wire(field);
        field.openPicker();
        auto* picker = dynamic_cast<KeyPicker*>(launched.get());
        ASSERT_NE(picker, nullptr);
        EXPECT_FALSE((bool)picker->detect);
        launched.reset();
    }
    SongKeyField field;
    wire(field);
    field.pitchClassWeights = [] {
        std::array<double, 12> w{};
        for (int pc : {9, 11, 0, 2, 4, 5, 7})
            w[(size_t)pc] = 1.0;
        w[9] = 4.0;
        w[4] = 2.0;
        return w;
    };
    field.openPicker();
    auto* picker = dynamic_cast<KeyPicker*>(launched.get());
    ASSERT_NE(picker, nullptr);
    ASSERT_TRUE((bool)picker->detect);
    // The field wires detect after the picker is built; attaching it the way a CallOutBox does must show the button.
    juce::Component box;
    box.addAndMakeVisible(*picker);
    EXPECT_TRUE(picker->isDetectShown());
    const auto detected = picker->detect();
    ASSERT_TRUE(detected.has_value());
    EXPECT_EQ(keyDisplayName(*detected), "A Natural Minor");
}

TEST_F(SongKeyFieldTest, AuditionPlaysTheScaleUpTheOctaveAndEndsEveryNote) {
    SongKeyField field;
    wire(field);
    field.startAudition(makeScale(0, scalePresetIndex("Major")));
    ASSERT_TRUE(field.isAuditioning());
    for (int i = 0; i < 20; ++i)
        field.advanceAuditionForTest();
    EXPECT_FALSE(field.isAuditioning());
    std::vector<int> ons;
    for (const auto& e : events)
        if (e.on)
            ons.push_back(e.pitch);
    EXPECT_EQ(ons, (std::vector<int>{60, 62, 64, 65, 67, 69, 71, 72}));
    EXPECT_EQ(onCount(), 0) << "every note-on got its note-off";
}

TEST_F(SongKeyFieldTest, AStoppedOrDestroyedAuditionSendsItsNoteOffToTheLatchedTrack) {
    const TrackId other = doc.addTrack(TrackKind::Midi, "Bass");
    {
        SongKeyField field;
        wire(field);
        field.startAudition(makeScale(2, scalePresetIndex("Dorian")));
        field.advanceAuditionForTest();
        track = other; // the audition track changes mid-run: the note-off must still go where the note-on went
        field.stopAudition();
        EXPECT_EQ(onCount(), 0);
        ASSERT_FALSE(events.empty());
        EXPECT_EQ(events.back().track, events.front().track);
        EXPECT_NE(events.back().track, other);

        field.startAudition(makeScale(2, scalePresetIndex("Dorian")));
    }
    EXPECT_EQ(onCount(), 0) << "destroying the field ends the note it was holding";
}

TEST_F(SongKeyFieldTest, ThePickersHoverDrivesTheAudition) {
    SongKeyField field;
    wire(field);
    field.openPicker();
    auto* picker = dynamic_cast<KeyPicker*>(launched.get());
    ASSERT_NE(picker, nullptr);
    ASSERT_TRUE((bool)picker->onAudition);
    picker->onAudition(makeScale(9, scalePresetIndex("Natural Minor")));
    EXPECT_TRUE(field.isAuditioning());
    picker->onAudition(std::nullopt);
    EXPECT_FALSE(field.isAuditioning());
    EXPECT_EQ(onCount(), 0);
}
