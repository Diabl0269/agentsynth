// SequencerKeyFieldTests.cpp -- the Sequencer and Poly Sequencer card's Key as the user meets it: the Key cell holds
// the follow-or-own field (no Scale or follow widget of its own), it reads the song key from the app's transport on
// the card's tick, a pick is one undo step that writes Key, Scale and the switch, a song key change moves and glows
// only the followers, the grid plays in the resolved key, and the Randomize range starts at the key's root.
#include "../StepGrid/StepGridCardFixture.h"
#include "Music/SongKey.h"
#include "UI/Graph/CardBody/DefaultLayouts/DefaultCardLayoutsFamilies.h"
#include "UI/Layout/KeyPicker/KeyPicker.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/ModuleViews/SequencerKey/SequencerKeyField.h"
#include "UI/ModuleViews/StepGrid/StepGridRandomizePanel.h"

using stepgrid_test::SeqCard;
using synth::makeScale;
using synth::scalePresetIndex;
using synth::ui::KeyPicker;
using synth::ui::SequencerKeyField;

namespace {

const juce::String kDot = juce::String::fromUTF8(" \xc2\xb7 ");

SequencerKeyField* keyFieldOf(SeqCard& c) {
    return c.card != nullptr && c.card->getCardBody() != nullptr
               ? dynamic_cast<SequencerKeyField*>(c.card->getCardBody()->findWidget("scaleRoot"))
               : nullptr;
}

void tick(SeqCard& c) { c.card->timerCallback(); }

synth::TransportService& transportOf(SeqCard& c) { return c.canvas.engine.getTransport(); }

struct MotionOn {
    MotionOn() { synth::ui::setReducedMotionForTest(false); }
    ~MotionOn() { synth::ui::setReducedMotionForTest(std::nullopt); }
};

} // namespace

TEST(SequencerKeyField, TheKeyCellHoldsTheFieldAndScaleAndFollowGetNoWidget) {
    for (const char* type : {"Sequencer", "Poly Sequencer"}) {
        SCOPED_TRACE(type);
        SeqCard c(type);
        auto* field = keyFieldOf(c);
        ASSERT_NE(field, nullptr);
        EXPECT_TRUE(field->isVisible());
        EXPECT_GE(field->getWidth(), 150) << "Song key and a long scale name fit";
        EXPECT_EQ(c.card->getCardBody()->findWidget("scale"), nullptr);
        EXPECT_EQ(c.card->getCardBody()->findWidget("followSongKey"), nullptr);
        tick(c);
        EXPECT_EQ(field->getText(), "Song key" + kDot + "No key");
    }
}

TEST(SequencerKeyField, ASavedLayoutThatNamesKeyAndScaleStillGetsTheField) {
    synth::CardLayout layout;
    layout.sections = {synth::cardlayout::section(
        "notes", juce::String("Notes"), {synth::cardlayout::param("scaleRoot"), synth::cardlayout::param("scale")})};
    CardCanvas canvas;
    const auto id = canvas.add(synth::AIStateMapper::createModule("Sequencer"), 40, 40, layout);
    canvas.editor.updateComponents();
    auto* card = canvas.card(id);
    ASSERT_NE(card, nullptr);
    EXPECT_NE(dynamic_cast<SequencerKeyField*>(card->getCardBody()->findWidget("scaleRoot")), nullptr);
    EXPECT_EQ(card->getCardBody()->findWidget("scale"), nullptr);
}

TEST(SequencerKeyField, AFollowerReadsTheSongKeyAndTheGridPlaysInIt) {
    SeqCard c;
    transportOf(c).setSongKey(makeScale(2, scalePresetIndex("Major")));
    tick(c);
    auto* field = keyFieldOf(c);
    ASSERT_NE(field, nullptr);
    EXPECT_EQ(field->getText(), "Song key" + kDot + "D Major");
    auto pattern = c.module->getPattern();
    pattern.steps[0].pitch = 65; // F, out of D major
    c.module->setPattern(pattern);
    tick(c);
    EXPECT_EQ(c.grid->playedPitchOf(0), 64);
}

TEST(SequencerKeyField, PickingAnOwnKeyIsOneUndoStepAndUndoFollowsAgain) {
    SeqCard c;
    transportOf(c).setSongKey(makeScale(2, scalePresetIndex("Dorian")));
    tick(c);
    auto* field = keyFieldOf(c);
    ASSERT_NE(field, nullptr);
    std::unique_ptr<juce::Component> held;
    field->pickerLauncher = [&](std::unique_ptr<juce::Component> picker, juce::Rectangle<int>) {
        held = std::move(picker);
    };
    field->openPicker();
    auto* picker = dynamic_cast<KeyPicker*>(held.get());
    ASSERT_NE(picker, nullptr);
    picker->setQuery("a phr");
    picker->pickHighlighted();

    EXPECT_FALSE(c.module->followsSongKey());
    ASSERT_TRUE(c.module->getOwnKey().has_value());
    EXPECT_EQ(synth::keyDisplayName(*c.module->getOwnKey()), "A Phrygian");
    EXPECT_EQ(field->getText(), "A Phrygian" + kDot + "own");

    ASSERT_TRUE(c.canvas.undo.undo());
    synth::SequencerModuleBase* seq = nullptr;
    for (auto* node : c.canvas.engine.getGraph().getNodes())
        if (auto* found = dynamic_cast<synth::SequencerModuleBase*>(node->getProcessor()))
            seq = found;
    ASSERT_NE(seq, nullptr);
    EXPECT_TRUE(seq->followsSongKey()) << "one undo puts following back";
    EXPECT_FALSE(seq->getOwnKey().has_value()) << "and the Key and Scale with it, in the same step";
}

TEST(SequencerKeyField, AUserScaleIsStoredAsCustomWithItsMask) {
    SeqCard c;
    tick(c);
    synth::MusicalScale odd;
    odd.rootPitchClass = 2;
    odd.mask = 0x05B3 ^ 0x0002;
    keyFieldOf(c)->pickOwn(odd);
    EXPECT_EQ(c.module->getPattern().customMask, odd.mask);
    ASSERT_TRUE(c.module->getOwnKey().has_value());
    EXPECT_EQ(c.module->getOwnKey()->mask, odd.mask);
    EXPECT_EQ(c.module->getOwnKey()->rootPitchClass, 2);
}

TEST(SequencerKeyField, ASongKeyChangeMovesAndGlowsOnlyTheFollowers) {
    MotionOn motion;
    SeqCard follower;
    SeqCard own;
    for (auto* c : {&follower, &own})
        transportOf(*c).setSongKey(makeScale(2, scalePresetIndex("Natural Minor")));
    tick(follower);
    tick(own);
    keyFieldOf(own)->pickOwn(makeScale(9, scalePresetIndex("Phrygian")));
    EXPECT_EQ(keyFieldOf(follower)->getGlow(), 0.0f) << "the first look never glows";

    for (auto* c : {&follower, &own})
        transportOf(*c).setSongKey(makeScale(4, scalePresetIndex("Natural Minor")));
    tick(follower);
    tick(own);
    EXPECT_EQ(keyFieldOf(follower)->getText(), "Song key" + kDot + "E Natural Minor");
    EXPECT_GT(keyFieldOf(follower)->getGlow(), 0.0f);
    EXPECT_EQ(keyFieldOf(own)->getText(), "A Phrygian" + kDot + "own");
    EXPECT_EQ(keyFieldOf(own)->getGlow(), 0.0f);
}

TEST(SequencerKeyField, FollowAgainKeepsTheOwnKeyForLater) {
    SeqCard c;
    tick(c);
    auto* field = keyFieldOf(c);
    field->pickOwn(makeScale(9, scalePresetIndex("Phrygian")));
    field->follow();
    EXPECT_TRUE(c.module->followsSongKey());
    ASSERT_TRUE(c.module->getOwnKey().has_value());
    EXPECT_EQ(c.module->getOwnKey()->rootPitchClass, 9);
}

TEST(SequencerKeyField, TheRandomizeRangeStartsAtTheKeysRoot) {
    SeqCard c;
    auto pattern = c.module->getPattern();
    for (auto& step : pattern.steps)
        step.pitch = 55; // inside the range either way, so only the seed decides
    c.module->setPattern(pattern);
    transportOf(c).setSongKey(makeScale(2, scalePresetIndex("Natural Minor")));
    tick(c);
    std::unique_ptr<synth::ui::StepGridRandomizePanel> panel;
    synth::ui::test_hooks::randomizePanelHookForTest() = [&](auto p) { panel = std::move(p); };
    c.grid->getTools().getButton(synth::ui::StepGridTools::Tool::Randomize).onClick();
    synth::ui::test_hooks::randomizePanelHookForTest() = nullptr;
    ASSERT_NE(panel, nullptr);
    EXPECT_EQ(panel->getRangePicker().getLow(), 50) << "D2 in the grid's numbering";
    EXPECT_EQ(panel->getRangePicker().getHigh(), 74) << "D4";
}
