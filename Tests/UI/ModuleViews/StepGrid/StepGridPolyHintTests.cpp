// StepGridPolyHintTests.cpp -- the line a Poly Sequencer card shows when its Midi output feeds a module that plays one
// note at a time: it appears when the cable is made, goes when that module's Poly switch turns on (and returns when it
// turns off), the plain Sequencer never shows it, its link turns Poly on as ONE undo step, and the chord picker's
// footer says the same.
#include "Mixer/ChannelFlows/ChannelFlows.h"
#include "StepGridCardFixture.h"
#include "UI/ModuleViews/StepGrid/StepGridChordPicker.h"
#include "UI/ModuleViews/StepGrid/StepGridPolyHint.h"

using stepgrid_test::SeqCard;
using synth::ui::StepGridPolyHint;

namespace {

constexpr int kMidi = juce::AudioProcessorGraph::midiChannelIndex;

/** A Poly Sequencer card with an Oscillator beside it, not yet cabled. */
struct Chain {
    SeqCard seq{"Poly Sequencer"};
    NodeID osc;

    Chain() {
        osc = seq.canvas.add(synth::AIStateMapper::createModule("Oscillator"), 600, 40);
        seq.canvas.editor.updateComponents();
    }
    juce::AudioProcessor& oscillator() { return *seq.canvas.processor(osc); }
    void connect() { seq.canvas.engine.getGraph().addConnection({{seq.id, kMidi}, {osc, kMidi}}); }
    void tick() { seq.card->timerCallback(); }
    StepGridPolyHint& hint() { return seq.grid->getPolyHint(); }
};

} // namespace

TEST(StepGridPolyHint, AppearsWhenTheCableIsMadeAndFollowsTheOscillatorsPolySwitch) {
    Chain c;
    c.tick();
    EXPECT_FALSE(c.hint().isVisible()) << "nothing is cabled yet";

    c.connect();
    c.tick();
    ASSERT_TRUE(c.hint().isVisible());
    EXPECT_EQ(c.hint().getModule(), "Oscillator");
    EXPECT_EQ(StepGridPolyHint::lineFor("Oscillator"), "Next: Oscillator plays one note at a time");
    EXPECT_FALSE(c.hint().getLink().getTitle().isEmpty()) << "the link has a screen-reader name";
    EXPECT_TRUE(c.hint().getLink().getWantsKeyboardFocus());

    synth::setProcessorPoly(&c.oscillator(), true);
    c.tick();
    EXPECT_FALSE(c.hint().isVisible()) << "a poly Oscillator plays every note";

    synth::setProcessorPoly(&c.oscillator(), false);
    c.tick();
    EXPECT_TRUE(c.hint().isVisible());
}

TEST(StepGridPolyHint, ThePlainSequencerAndACardThatFeedsNothingShowNone) {
    SeqCard plain("Sequencer");
    auto osc = plain.canvas.add(synth::AIStateMapper::createModule("Oscillator"), 600, 40);
    plain.canvas.engine.getGraph().addConnection({{plain.id, kMidi}, {osc, kMidi}});
    plain.card->timerCallback();
    EXPECT_FALSE(plain.grid->getPolyHint().isVisible()) << "the plain Sequencer has no chords to lose";
    EXPECT_TRUE(plain.grid->getMonoDestination().isEmpty());
}

TEST(StepGridPolyHint, TheLinkTurnsPolyOnAsOneUndoStepAndUndoBringsTheHintBack) {
    Chain c;
    c.connect();
    c.tick();
    ASSERT_TRUE(c.hint().isVisible());
    c.seq.canvas.undo.getUndoManager().clearUndoHistory();

    c.hint().getLink().onClick();
    c.tick();
    EXPECT_TRUE(synth::isProcessorPoly(&c.oscillator()));
    EXPECT_FALSE(c.hint().isVisible());
    ASSERT_TRUE(c.seq.canvas.undo.canUndo());
    c.seq.canvas.undo.undo();
    EXPECT_FALSE(c.seq.canvas.undo.canUndo()) << "turning Poly on is one step";
    EXPECT_FALSE(synth::isProcessorPoly(&c.oscillator()));
    c.tick();
    EXPECT_TRUE(c.hint().isVisible());
}

TEST(StepGridPolyHint, TheChordPickerFooterSaysTheSameLineOnlyWhileTheNextModuleIsMono) {
    Chain c;
    c.connect();
    c.tick();
    std::unique_ptr<synth::ui::StepGridChordPicker> picker;
    synth::ui::test_hooks::chordPickerHookForTest() = [&](auto p) { picker = std::move(p); };
    c.seq.grid->openChordPicker(0);
    ASSERT_NE(picker, nullptr);
    EXPECT_EQ(picker->getFooterText(), "Next: Oscillator plays one note at a time");
    const int withFooter = picker->getHeight();

    synth::setProcessorPoly(&c.oscillator(), true);
    c.tick();
    picker.reset();
    c.seq.grid->openChordPicker(0);
    synth::ui::test_hooks::chordPickerHookForTest() = nullptr;
    ASSERT_NE(picker, nullptr);
    EXPECT_TRUE(picker->getFooterText().isEmpty());
    EXPECT_LT(picker->getHeight(), withFooter);
}
