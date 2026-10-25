// StepGridChordPickerTests.cpp -- the Poly Sequencer's chord picker: which chips it offers in and out of the key, the
// step's label (chord and roman numeral), picking as one undo step, and the keys.
#include "StepGridCardFixture.h"
#include "UI/ModuleViews/StepGrid/StepGridChords.h"

using stepgrid_test::SeqCard;
using namespace synth::ui::stepgrid;
using synth::SequencerChord;

namespace {

juce::StringArray chipNames(const ChordPickerContent& content, const juce::String& rowLabel) {
    juce::StringArray names;
    for (const auto& row : content.rows)
        if (row.label == rowLabel)
            for (const auto& chip : row.chips)
                names.add(chip.name);
    return names;
}

const synth::MusicalScale cMajor = synth::makeScale(0, 0);

struct Open {
    std::unique_ptr<synth::ui::StepGridChordPicker> picker;
    Open(SeqCard& c, int step) {
        synth::ui::test_hooks::chordPickerHookForTest() = [this](auto p) { picker = std::move(p); };
        c.grid->openChordPicker(step);
        synth::ui::test_hooks::chordPickerHookForTest() = nullptr;
    }
};

} // namespace

TEST(StepGridChords, TheChipsForCMajorOnC) {
    const auto content = chordPickerContent(60, cMajor);
    EXPECT_EQ(content.header, "Chords that fit C major on C");
    EXPECT_EQ(chipNames(content, "Triad").joinIntoString(","), "C");
    EXPECT_EQ(chipNames(content, "Seventh").joinIntoString(","), "Cmaj7");
    EXPECT_EQ(chipNames(content, "Sus").joinIntoString(","), "Csus2,Csus4");
    EXPECT_EQ(chipNames(content, "Add").joinIntoString(","), "Cadd9,C6");
    EXPECT_EQ(chipNames(content, "Borrowed").joinIntoString(","), "Cm,C7,Cm7,Cdim,Caug,Cm6,Cdim7,Cm7b5,CmMaj7");
    EXPECT_EQ(chipNames(content, "More").joinIntoString(","), "Unison,5ths,Octs,Random");
    EXPECT_EQ(content.firstOutsideRow, 4);
}

TEST(StepGridChords, TheChipsForCMajorOnDAndB) {
    const auto onD = chordPickerContent(62, cMajor);
    EXPECT_EQ(onD.header, "Chords that fit C major on D");
    EXPECT_EQ(chipNames(onD, "Triad").joinIntoString(","), "Dm");
    EXPECT_EQ(chipNames(onD, "Seventh").joinIntoString(","), "Dm7");
    EXPECT_EQ(chipNames(onD, "Sus").joinIntoString(","), "Dsus2,Dsus4");
    const auto borrowedOnD = chipNames(onD, "Borrowed");
    EXPECT_TRUE(borrowedOnD.contains("D")) << "major is outside the key on D";
    EXPECT_FALSE(borrowedOnD.contains("Dm")) << "Dm is already an in-key chip";

    const auto onB = chordPickerContent(71, cMajor);
    EXPECT_EQ(chipNames(onB, "Triad").joinIntoString(","), "Bdim");
    EXPECT_EQ(chipNames(onB, "Seventh").joinIntoString(","), "Bm7b5");
    EXPECT_FALSE(chipNames(onB, "Borrowed").contains("Bdim"));
}

TEST(StepGridChords, WithNoScaleTheInKeyRowsAreMajorAtTheRootAndTheHeaderSaysOn) {
    const auto content = chordPickerContent(62, synth::MusicalScale{});
    EXPECT_EQ(content.header, "Chords on D");
    EXPECT_EQ(chipNames(content, "Triad").joinIntoString(","), "D");
    EXPECT_EQ(chipNames(content, "Seventh").joinIntoString(","), "Dmaj7");
    const auto borrowed = chipNames(content, "Borrowed");
    EXPECT_FALSE(borrowed.contains("D")) << "dropped: the same notes as the Triad chip";
    EXPECT_TRUE(borrowed.contains("Dm"));
}

TEST(StepGridChords, TheLabelIsTheChordNameAndItsRomanNumeralWithAScale) {
    const auto label = chordLabelFor(SequencerChord::ScaleSeventh, 60, cMajor, 0, 0);
    EXPECT_EQ(label.name, "Cmaj7");
    EXPECT_EQ(label.numeral, "Imaj7");
    EXPECT_EQ(label.text(), "Cmaj7 Imaj7");
    const auto minor = chordLabelFor(SequencerChord::ScaleTriad, 62, cMajor, 0, 0);
    EXPECT_EQ(minor.name, "Dm");
    EXPECT_EQ(minor.numeral, "ii");
}

TEST(StepGridChords, WithNoScaleTheLabelHasOnlyTheName) {
    const auto label = chordLabelFor(SequencerChord::Dom7, 67, synth::MusicalScale{}, 0, 0);
    EXPECT_EQ(label.name, "G7");
    EXPECT_TRUE(label.numeral.isEmpty());
    EXPECT_EQ(chordLabelFor(SequencerChord::Unison, 60, cMajor, 0, 0).name, "C3");
}

TEST(StepGridChordPicker, PickingAChordIsOneUndoStepAndChangesTheStep) {
    SeqCard c("Poly Sequencer");
    c.setChoice("scaleRoot", "C");
    c.useOwnKey();
    c.setChoice("scale", "Major");
    c.grid->poll();
    Open open(c, 2);
    ASSERT_NE(open.picker, nullptr);
    EXPECT_EQ(open.picker->getWidth(), 250);

    const auto before = c.module->getPattern().steps[2].chord;
    open.picker->pickChip(1, 0); // the Seventh row
    EXPECT_EQ(c.module->getPattern().steps[2].chord, SequencerChord::ScaleSeventh);
    EXPECT_NE(before, SequencerChord::ScaleSeventh);
    EXPECT_TRUE(c.canvas.undo.canUndo());
    c.canvas.undo.getUndoManager().undo();
    EXPECT_EQ(c.module->getPattern().steps[2].chord, before);
    EXPECT_FALSE(c.canvas.undo.canUndo());
}

TEST(StepGridChordPicker, TheKeysMoveBetweenRowsAndChipsEnterPicksEscapeCloses) {
    SeqCard c("Poly Sequencer");
    Open open(c, 0);
    ASSERT_NE(open.picker, nullptr);
    auto& picker = *open.picker;
    EXPECT_EQ(picker.getFocusedRow(), 5) << "the current chord is Unison: the More row";
    const auto key = [&](int code) { return picker.keyPressed(juce::KeyPress(code)); };

    EXPECT_FALSE(key(juce::KeyPress::homeKey)) << "not a picker key";
    while (picker.getFocusedRow() > 0)
        key(juce::KeyPress::upKey);
    EXPECT_EQ(picker.getFocusedRow(), 0);
    key(juce::KeyPress::downKey);
    key(juce::KeyPress::downKey);
    EXPECT_EQ(picker.getFocusedRow(), 2);
    key(juce::KeyPress::rightKey);
    EXPECT_EQ(picker.getFocusedChip(), 1);
    key(juce::KeyPress::rightKey);
    EXPECT_EQ(picker.getFocusedChip(), 1) << "the Sus row has two chips";
    key(juce::KeyPress::leftKey);
    EXPECT_EQ(picker.getFocusedChip(), 0);

    key(juce::KeyPress::rightKey);
    EXPECT_EQ(picker.getFocusedChord(), SequencerChord::ScaleSus4);
    key(juce::KeyPress::returnKey);
    EXPECT_EQ(c.module->getPattern().steps[0].chord, SequencerChord::ScaleSus4);
    EXPECT_FALSE(key(juce::KeyPress::escapeKey)) << "headless: nothing hosts it, so nothing closes";
}

TEST(StepGridChordPicker, EveryChipIsAKeyboardTargetAndTheLayoutWrapsInside250Pixels) {
    SeqCard c("Poly Sequencer");
    Open open(c, 0);
    auto& picker = *open.picker;
    for (int r = 0; r < (int)picker.getContent().rows.size(); ++r)
        for (int i = 0; i < (int)picker.getContent().rows[(size_t)r].chips.size(); ++i) {
            const auto bounds = picker.chipBounds(r, i);
            EXPECT_FALSE(bounds.isEmpty());
            EXPECT_LE(bounds.getRight(), 250 - 10) << r << "," << i;
        }
}

TEST(StepGridChordPicker, EnterOnAFocusedPitchStepOpensItAndAClickOnTheLabelDoes) {
    SeqCard c("Poly Sequencer");
    std::unique_ptr<synth::ui::StepGridChordPicker> picker;
    synth::ui::test_hooks::chordPickerHookForTest() = [&](auto p) { picker = std::move(p); };
    c.press(juce::KeyPress::returnKey);
    ASSERT_NE(picker, nullptr);
    picker.reset();

    const auto label = c.grid->chordLabelBounds(0);
    ASSERT_FALSE(label.isEmpty());
    c.mouse("down", label.getCentre().toFloat());
    c.mouse("up", label.getCentre().toFloat());
    EXPECT_NE(picker, nullptr);
    synth::ui::test_hooks::chordPickerHookForTest() = nullptr;
    EXPECT_FALSE(c.canvas.undo.canUndo()) << "opening the picker edits nothing";
}

TEST(StepGridChordPicker, TheMonoSequencerHasNoPickerAndTheStepReadsItsChordAloud) {
    SeqCard mono;
    EXPECT_TRUE(mono.grid->chordLabelBounds(0).isEmpty());

    SeqCard poly("Poly Sequencer");
    auto pattern = poly.module->getPattern();
    pattern.steps[0].chord = SequencerChord::Dom7;
    poly.module->setPattern(pattern);
    EXPECT_NE(poly.grid->describeFocus().indexOf(", chord "), -1);
}

namespace {

struct Block {
    std::vector<int> ons;
    std::vector<int> offs;
};

// Renders one block of the module and returns the notes it started and ended.
Block renderBlock(synth::SequencerModuleBase& module) {
    juce::AudioBuffer<float> buffer(2, 256);
    juce::MidiBuffer midi;
    module.processBlock(buffer, midi);
    Block block;
    for (const auto meta : midi) {
        const auto msg = meta.getMessage();
        if (msg.isNoteOn())
            block.ons.push_back(msg.getNoteNumber());
        else if (msg.isNoteOff())
            block.offs.push_back(msg.getNoteNumber());
    }
    std::sort(block.ons.begin(), block.ons.end());
    std::sort(block.offs.begin(), block.offs.end());
    return block;
}

void moveOver(synth::ui::StepGridChordPicker& picker, juce::Point<int> p) {
    const auto e = cardbody_test::mouseAt(picker, p.toFloat(), juce::ModifierKeys());
    picker.mouseEnter(e);
    picker.mouseMove(e);
}

void settle(int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil(ms); }

// Pumps the message loop until a rendered block starts notes, so the 60 ms audition wait is not raced by a loaded
// runner (a fixed 150 ms pump missed it on macOS CI). Returns the first block that carries notes, or the last empty
// one.
Block renderOnceAuditioned(synth::SequencerModuleBase& module, int timeoutMs = 3000) {
    Block block;
    for (int waited = 0; waited < timeoutMs; waited += 20) {
        settle(20);
        block = renderBlock(module);
        if (!block.ons.empty())
            break;
    }
    return block;
}

std::vector<int> chordAt(SeqCard& c, SequencerChord chord, int step) {
    const auto notes = synth::sequencerChordNotes(chord, c.grid->playedPitchOf(step), synth::makeScale(0, 0),
                                                  c.module->getPattern().seed, step);
    std::vector<int> out(notes.notes.begin(), notes.notes.begin() + notes.count);
    std::sort(out.begin(), out.end());
    return out;
}

} // namespace

TEST(StepGridChordPicker, HoveringAChipPlaysItsChordAfterTheDebounceAndLeavingOrClosingStopsIt) {
    SeqCard c("Poly Sequencer");
    c.setChoice("scaleRoot", "C");
    c.useOwnKey();
    c.setChoice("scale", "Major");
    c.grid->poll();
    c.module->prepareToPlay(44100.0, 256);
    auto open = std::make_unique<Open>(c, 2);
    auto& picker = *open->picker;

    const auto seventh = picker.chipBounds(1, 0).getCentre();
    moveOver(picker, seventh);
    EXPECT_TRUE(renderBlock(*c.module).ons.empty()) << "nothing plays before the pointer has rested";
    const auto played = renderOnceAuditioned(*c.module);
    EXPECT_EQ(played.ons, chordAt(c, SequencerChord::ScaleSeventh, 2));
    EXPECT_FALSE(played.ons.empty());

    moveOver(picker, {1, 1}); // the gap outside every chip
    const auto stopped = renderBlock(*c.module);
    EXPECT_EQ(stopped.offs, played.ons);
    EXPECT_TRUE(stopped.ons.empty());

    moveOver(picker, seventh);
    EXPECT_EQ(renderOnceAuditioned(*c.module).ons, played.ons);
    open.reset(); // closing the picker ends the sound
    EXPECT_EQ(renderBlock(*c.module).offs, played.ons);
}

TEST(StepGridChordPicker, OnlyTheChipThePointerRestsOnPlaysAndTheKeysAuditionToo) {
    SeqCard c("Poly Sequencer");
    c.setChoice("scaleRoot", "C");
    c.useOwnKey();
    c.setChoice("scale", "Major");
    c.grid->poll();
    c.module->prepareToPlay(44100.0, 256);
    Open open(c, 0);
    auto& picker = *open.picker;

    moveOver(picker, picker.chipBounds(0, 0).getCentre());
    moveOver(picker, picker.chipBounds(1, 0).getCentre());
    moveOver(picker, picker.chipBounds(2, 1).getCentre()); // all within the wait
    EXPECT_EQ(renderOnceAuditioned(*c.module).ons, chordAt(c, SequencerChord::ScaleSus4, 0))
        << "only the last chip plays";

    picker.keyPressed(juce::KeyPress(juce::KeyPress::upKey)); // keyboard focus moves up from the current chord's row
    const auto next = renderOnceAuditioned(*c.module);
    EXPECT_EQ(next.ons, chordAt(c, picker.getFocusedChord(), 0));
    EXPECT_NE(picker.getFocusedChord(), SequencerChord::ScaleSus4);
    EXPECT_FALSE(next.offs.empty()) << "the new chord replaces the old";
}
