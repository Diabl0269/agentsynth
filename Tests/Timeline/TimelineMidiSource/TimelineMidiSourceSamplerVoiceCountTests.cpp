// Loop-wrap guard for the status bar voice count: a looping MIDI track plays a Sampler in Instrument mode (its
// default 0.6 s Release, so a fade-out always runs into the next pass) for several passes, with a note that ends
// before, exactly at and past the loop end, at every block size. After every block the module's displayVoiceCount
// must never exceed the keys held, and it must be 0 at the quiet point of each pass (the wrap has flushed the
// held note and its fade-out is not counted).

#include "../../Modules/Sampler/SamplerTestWavs.h"
#include "Modules/SamplerModule.h"
#include "TimelineMidiSourceTestHelpers.h"

namespace {

constexpr int kVoiceCountPasses = 4;
constexpr double kVoiceCountLoopBeats = 4.0;

enum class VoiceCountNoteEnd { BeforeLoopEnd, ExactlyAtLoopEnd, PastLoopEnd };

double voiceCountEndBeat(VoiceCountNoteEnd end) {
    switch (end) {
    case VoiceCountNoteEnd::BeforeLoopEnd:
        return kVoiceCountLoopBeats - 0.01;
    case VoiceCountNoteEnd::ExactlyAtLoopEnd:
        return kVoiceCountLoopBeats;
    case VoiceCountNoteEnd::PastLoopEnd:
        return kVoiceCountLoopBeats + 1.0;
    }
    return kVoiceCountLoopBeats;
}

} // namespace

TEST(TimelineMidiSourceTest, SamplerStatusBarCountNeverExceedsTheKeysHeldAcrossLoopPasses) {
    sampler_test::TempDir dir;
    sampler_test::writeWav(dir.file("dc.wav"), 88200, 1, kSampleRate, [](int, int) { return 0.2f; });
    dir.file("dc.sfz").replaceWithText("<region> sample=dc.wav lokey=0 hikey=127 pitch_keycenter=60 "
                                       "loop_mode=loop_continuous\n");

    for (const int blockSize : {512, 480, 256, 441}) {
        for (const auto end :
             {VoiceCountNoteEnd::BeforeLoopEnd, VoiceCountNoteEnd::ExactlyAtLoopEnd, VoiceCountNoteEnd::PastLoopEnd}) {
            SCOPED_TRACE("block " + std::to_string(blockSize) + ", note end " + std::to_string((int)end));

            Doc doc;
            ASSERT_TRUE(doc.doc.addNote(doc.clipId, makeNote(0.0, 62, 0.25)).isValid());
            ASSERT_TRUE(doc.doc.addNote(doc.clipId, makeNote(1.0, 60, voiceCountEndBeat(end) - 1.0)).isValid());
            auto snapshot = doc.snapshot();

            Harness h;
            SamplerModule sampler;
            sampler.prepareToPlay(kSampleRate, blockSize);
            ASSERT_TRUE(sampler.loadInstrumentFromSfz(dir.file("dc.sfz")));
            ASSERT_TRUE(h.transport.setLoop(0.0, kVoiceCountLoopBeats, true));
            ASSERT_TRUE(h.transport.play());

            HeldKeys model;
            std::int64_t previousStart = -1;
            int pass = -1, probedPasses = 0, sawHeld = 0;
            while (pass < kVoiceCountPasses) {
                const auto events = h.renderBlock(snapshot.get(), blockSize);
                const auto start = h.blockStartSample();
                if (start < previousStart || pass < 0)
                    ++pass;
                previousStart = start;
                if (pass >= kVoiceCountPasses)
                    break;

                juce::MidiBuffer midi;
                for (const auto& e : events) {
                    model.apply(e, "pass " + juce::String(pass));
                    midi.addEvent(e.isNoteOn ? juce::MidiMessage::noteOn(e.channel, e.pitch, (juce::uint8)e.velocity)
                                             : juce::MidiMessage::noteOff(e.channel, e.pitch),
                                  e.sample);
                }
                juce::AudioBuffer<float> audio(SamplerModule::kNumChannels, blockSize);
                audio.clear();
                sampler.processBlock(audio, midi);

                const int shown = sampler.displayVoiceCount();
                EXPECT_LE(shown, model.numDown()) << "pass " << pass << " block start " << start;
                EXPECT_LE(shown, 1) << "pass " << pass << ": the notes never overlap";
                sawHeld += shown > 0 ? 1 : 0;

                // beat ~0.5: the previous pass's note is off and fading, the next has not started.
                const double beat = (double)start / kSamplesPerBeat;
                if (beat >= 0.5 && beat < 0.5 + (double)blockSize / kSamplesPerBeat && probedPasses == pass) {
                    EXPECT_EQ(shown, 0) << "pass " << pass << ": a fading note is counted after the wrap";
                    ++probedPasses;
                }
            }
            EXPECT_EQ(probedPasses, kVoiceCountPasses);
            EXPECT_GT(sawHeld, 0) << "the held notes are shown";
            EXPECT_TRUE(model.firstFailure.isEmpty()) << model.firstFailure;
        }
    }
}
