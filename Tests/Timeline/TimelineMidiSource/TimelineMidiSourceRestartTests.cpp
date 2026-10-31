// A re-prepare while the song plays (a device restart, or a host preparing the plugin again): the module owes a
// note-off for everything it holds, and brings back once what the position is still inside.
#include "Modules/MidiKeyboardModule.h"
#include "TimelineMidiSourceTestHelpers.h"

namespace {

int countEvents(const std::vector<Event>& events, int pitch, bool on) {
    return (int)std::count_if(events.begin(), events.end(),
                              [&](const Event& e) { return e.pitch == pitch && (on ? e.isNoteOn : e.isNoteOff); });
}

void playToBeat(Harness& h, const TimelineSnapshot* snapshot, double beat) {
    const int blocks = (int)std::ceil(beat * kSamplesPerBeat / kBlock);
    for (int i = 0; i < blocks; ++i)
        h.renderBlock(snapshot);
}

} // namespace

// Regression test for FRO1222: prepareToPlay emptied the held table, so a held note never got its note-off.
TEST(TimelineMidiSourceRestart, AHeldNoteIsReleasedThenResumedOnceAtSampleZero) {
    Harness h;
    Doc d;
    ASSERT_TRUE(d.doc.addNote(d.clipId, makeNote(0.0, 60, 4.0, 77)).isValid());
    const auto snapshot = d.snapshot();
    ASSERT_TRUE(h.transport.play());
    playToBeat(h, snapshot.get(), 1.0);
    ASSERT_EQ(h.module.getActiveNoteCount(), 1);

    h.module.releaseResources();
    h.transport.prepare(44100.0, 256);
    h.module.prepareToPlay(44100.0, 256);

    const auto events = h.renderBlock(snapshot.get(), 256);
    ASSERT_EQ(events.size(), 2u);
    EXPECT_TRUE(events[0].isNoteOff);
    EXPECT_TRUE(events[1].isNoteOn);
    EXPECT_EQ(events[1].pitch, 60);
    EXPECT_EQ(events[1].velocity, 77) << "it comes back as it was played";
    EXPECT_EQ(events[0].sample, 0);
    EXPECT_EQ(events[1].sample, 0);
    EXPECT_EQ(h.module.getActiveNoteCount(), 1);

    // It still ends where the clip says, exactly once.
    int offs = 0;
    for (int i = 0; i < 400; ++i)
        offs += countEvents(h.renderBlock(snapshot.get(), 256), 60, false);
    EXPECT_EQ(offs, 1);
    EXPECT_EQ(h.module.getActiveNoteCount(), 0);
}

// Regression test for FRO1222: a Stop landing while the stream is down still has to release what was held.
TEST(TimelineMidiSourceRestart, StoppingWhileTheStreamIsDownOnlyReleases) {
    Harness h;
    Doc d;
    ASSERT_TRUE(d.doc.addNote(d.clipId, makeNote(0.0, 60, 4.0)).isValid());
    const auto snapshot = d.snapshot();
    ASSERT_TRUE(h.transport.play());
    playToBeat(h, snapshot.get(), 1.0);

    ASSERT_TRUE(h.transport.stop());
    h.module.prepareToPlay(kSampleRate, kBlock);
    const auto events = h.renderBlock(snapshot.get());
    EXPECT_EQ(countEvents(events, 60, false), 1);
    EXPECT_EQ(countEvents(events, 60, true), 0);
    EXPECT_EQ(h.module.getActiveNoteCount(), 0);
}

// Regression test for FRO1222: the transport can move while the stream is down, and a note it left must not come back.
TEST(TimelineMidiSourceRestart, ANoteThePositionLeftWhileDownIsReleasedAndNotResumed) {
    Harness h;
    Doc d;
    ASSERT_TRUE(d.doc.addNote(d.clipId, makeNote(0.0, 60, 2.0)).isValid());
    const auto snapshot = d.snapshot();
    ASSERT_TRUE(h.transport.play());
    playToBeat(h, snapshot.get(), 1.0);

    ASSERT_TRUE(h.transport.locateBeat(3.0));
    h.transport.tick(0);
    h.module.prepareToPlay(kSampleRate, kBlock);
    const auto events = h.renderBlock(snapshot.get());
    EXPECT_EQ(countEvents(events, 60, false), 1);
    EXPECT_EQ(countEvents(events, 60, true), 0);
    EXPECT_EQ(h.module.getActiveNoteCount(), 0);
}

// Regression test for FRO1222: the pass counter reset on a restart, so chance and conditions rolled again mid-song.
TEST(TimelineMidiSourceRestart, DoesNotRestartThePassCount) {
    const auto render = [](bool restart) {
        Harness h;
        Doc d;
        for (int beat = 0; beat < 4; ++beat) {
            auto note = makeNote((double)beat, 60 + beat, 0.25);
            note.chance = 50;
            EXPECT_TRUE(d.doc.addNote(d.clipId, note).isValid());
        }
        const auto snapshot = d.snapshot();
        EXPECT_TRUE(h.transport.setLoop(0.0, 4.0, true));
        EXPECT_TRUE(h.transport.play());
        std::vector<int> pitches;
        const int total = 16 * kSamplesPerBeat / kBlock;
        for (int i = 0; i < total; ++i) {
            if (restart && i == total / 2 + 11)
                h.module.prepareToPlay(kSampleRate, kBlock);
            for (const auto& e : h.renderBlock(snapshot.get()))
                if (e.isNoteOn)
                    pitches.push_back(e.pitch);
        }
        return pitches;
    };
    EXPECT_EQ(render(false), render(true));
}

// Regression test for FRO1222: a previewed note lost its note-off on a restart and rang on in a hosted synth.
TEST(TimelineMidiSourceRestart, AnAuditionedNoteIsReleasedAndDoesNotResume) {
    Harness h;
    Doc d;
    const auto snapshot = d.snapshot();
    h.module.pushAuditionNote(64, 100, true);
    ASSERT_EQ(h.renderBlock(snapshot.get()).size(), 1u);

    h.module.prepareToPlay(kSampleRate, kBlock);
    const auto events = h.renderBlock(snapshot.get());
    ASSERT_EQ(events.size(), 1u);
    EXPECT_TRUE(events[0].isNoteOff);
    EXPECT_EQ(h.module.getAuditionNoteCount(), 0);
}

// Regression test for FRO1222: the on-screen keys forgot a held key with no note-off.
TEST(MidiKeyboardRestart, AHeldKeyIsReleasedOnTheFirstBlock) {
    auto keyboard = std::make_unique<MidiKeyboardModule>();
    keyboard->prepareToPlay(kSampleRate, kBlock);
    keyboard->getKeyboardState().noteOn(1, 60, 0.8f);
    juce::AudioBuffer<float> buffer(1, kBlock);
    juce::MidiBuffer midi;
    keyboard->processBlock(buffer, midi);
    ASSERT_EQ(midi.getNumEvents(), 1);

    keyboard->prepareToPlay(kSampleRate, kBlock);
    midi.clear();
    keyboard->processBlock(buffer, midi);
    const auto events = eventsOf(midi);
    EXPECT_EQ(countEvents(events, 60, false), 1);
}
