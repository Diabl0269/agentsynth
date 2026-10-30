// SequencerKeyFieldTests.cpp -- the Sequencer and Poly Sequencer card's Key as the user meets it: the Key cell holds
// the follow-or-own field (no Scale or follow widget of its own), it reads the song key from the app's transport on
// the card's tick, a pick is one undo step that writes Key, Scale and the switch, a song key change moves and glows
// only the followers, the grid plays in the resolved key, and the Randomize range starts at the key's root.
#include "../../../TestWait.h"
#include "../../Layout/KeyAuditionRenderRig.h"
#include "../StepGrid/StepGridCardFixture.h"
#include "AI/AIStateMapper/AIStateMapper.h"
#include "Modules/Sequencer/SequencerModuleBase.h"
#include "Music/SongKey.h"
#include "UI/Graph/CardBody/DefaultLayouts/DefaultCardLayoutsFamilies.h"
#include "UI/Layout/KeyPicker/KeyAuditioner.h"
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

TEST(SequencerKeyField, ItsKeyFieldIsHeardThroughTheMalletItIsCabledTo) {
    cardbody_test::CardCanvas canvas;
    auto& graph = canvas.engine.getGraph();
    graph.setPlayConfigDetails(0, 2, synth::test::kFakeDeviceSampleRate, keyaudition_test::kBlock);
    const auto seqId = canvas.add(synth::AIStateMapper::createModule("Sequencer"), 40, 40);
    const auto malletId = canvas.add(synth::AIStateMapper::createModule("Mallet"), 400, 40);
    const auto out = graph.addNode(std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor>(
        juce::AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode));
    ASSERT_TRUE(graph.addConnection({{seqId, juce::AudioProcessorGraph::midiChannelIndex},
                                     {malletId, juce::AudioProcessorGraph::midiChannelIndex}}));
    ASSERT_TRUE(graph.addConnection({{malletId, 0}, {out->nodeID, 0}}));
    canvas.engine.refreshNormalling();
    canvas.editor.updateComponents();
    auto* card = canvas.card(seqId);
    ASSERT_NE(card, nullptr);
    auto* key = card->getCardBody() != nullptr
                    ? dynamic_cast<synth::ui::SequencerKeyField*>(card->getCardBody()->findWidget("scaleRoot"))
                    : nullptr;
    ASSERT_NE(key, nullptr);
    card->timerCallback(); // the card's tick wires the field's auditioner (wireSequencerKeyAudition)

    keyaudition_test::Render render(canvas.engine);
    EXPECT_LT(render.peak(20), keyaudition_test::kQuiet) << "silent before anything is hovered";

    key->getAuditioner().start(keyaudition_test::kMajor);
    ASSERT_TRUE(key->getAuditioner().isAuditioning());
    EXPECT_FALSE(key->getAuditioner().isUsingDefaultSound()) << "the cabled Mallet is the target, not the fallback";
    const float heard = render.peak(30);
    RecordProperty("sequencer_route_peak", std::to_string(heard));
    EXPECT_GT(heard, 0.001f) << "the note reaches the output through the Mallet";

    key->getAuditioner().stop();
    const int quietAfter = render.blocksUntilQuiet(keyaudition_test::kQuiet, 1500);
    EXPECT_GE(quietAfter, 0) << "the Mallet's ring dies away after the note-off";
    EXPECT_LT(render.peak(20), keyaudition_test::kQuiet) << "and stays silent";
}

// Hovering a key in a sequencer card's picker plays the scale (the default sound when nothing is cabled), and the
// "Play the scale when browsing keys" setting silences it. The other owners are in KeyAuditionOwnersTests.
TEST(SequencerKeyField, HoveringAKeyInItsPickerPlaysTheScaleUnlessTheSettingIsOff) {
    SeqCard seq;
    seq.card->timerCallback(); // wires the card's Key field
    auto* field = keyFieldOf(seq);
    ASSERT_NE(field, nullptr);
    std::unique_ptr<juce::Component> held;
    field->pickerLauncher = [&held](std::unique_ptr<juce::Component> picker, juce::Rectangle<int>) {
        held = std::move(picker);
    };
    int runs = 0;
    field->getAuditioner().playDefaultSound = [&runs](const std::vector<int>&) { ++runs; };

    const auto hover = [&](int expectFired) {
        field->openPicker();
        auto* picker = dynamic_cast<KeyPicker*>(held.get());
        EXPECT_NE(picker, nullptr);
        if (picker == nullptr)
            return;
        int fired = 0;
        const auto original = picker->onAudition;
        picker->onAudition = [&fired, original](std::optional<synth::MusicalScale> scale) {
            ++fired;
            if (original)
                original(scale);
        };
        picker->hoverVisibleKey(1);
        synth::test::waitUntil([&] { return fired >= expectFired; }, 2000);
        held.reset();
    };

    hover(1);
    EXPECT_EQ(runs, 1);

    juce::ApplicationProperties app;
    juce::PropertiesFile::Options options;
    options.applicationName = "SequencerKeyFieldAuditionTest";
    options.filenameSuffix = "test";
    options.storageFormat = juce::PropertiesFile::storeAsXML;
    app.setStorageParameters(options);
    auto& props = *app.getUserSettings();
    props.clear();
    props.setValue(synth::kPlayScaleOnBrowseSettingsKey, "0");
    field->getAuditioner().setSettings(&props);
    hover(1);
    EXPECT_EQ(runs, 1) << "the setting is off: the picker's hover raises the audition but nothing plays";
    EXPECT_FALSE(field->getAuditioner().isAuditioning());
}
