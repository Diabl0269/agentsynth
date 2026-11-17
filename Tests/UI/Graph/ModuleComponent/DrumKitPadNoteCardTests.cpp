// DrumKitPadNoteCardTests.cpp
// The picked pad's "Plays on" note field (the key that triggers the pad, editable by keys and typing, one undo step,
// saved) and the Tune scale row (the Tune knob landing on a key's notes, the key following the song or its own, every
// built-in scale), with the keyboard path to both.

#include "../CardBody/CardCanvas.h"
#include "../GraphEditor/GraphEditorTestHelpers.h"
#include "AppUndoManager.h"
#include "Modules/DrumKit/DrumKitModule.h"
#include "Modules/Sequencer/SequencerScaleSnap.h"
#include "UI/Graph/ModuleComponent/SamplerChrome/DrumKitChrome.h"
#include <algorithm>
#include <gtest/gtest.h>

using namespace cardbody_test;
using synth::ui::DrumKitChrome;
using synth::ui::DrumKitPadsSection;
using PadParam = DrumKitModule::PadParam;

namespace {

struct Scene {
    CardCanvas canvas;
    NodeID id;

    Scene() {
        id = canvas.add(std::make_unique<DrumKitModule>(), 0, 0);
        canvas.editor.updateComponents();
        pads().grid().pick(1, false); // the snare, note D1 (38)
        pads().controls().showPad(1, false);
    }
    DrumKitModule& kit() { return *dynamic_cast<DrumKitModule*>(canvas.processor(id)); }
    ModuleComponent& card() { return *canvas.card(id); }
    DrumKitPadsSection& pads() { return dynamic_cast<DrumKitChrome*>(card().getInstrumentChrome())->getPads(); }
    synth::ui::DrumPadControls& controls() { return pads().controls(); }

    // The pad a hit on `note` strikes, or -1.
    int padStruckBy(int note) {
        auto& k = kit();
        k.prepareToPlay(44100.0, 256);
        juce::AudioBuffer<float> buffer(2, 256);
        buffer.clear();
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(10, note, 1.0f), 0);
        const int before = k.lastPlayedPad();
        k.processBlock(buffer, midi);
        return k.lastPlayedPad() != before ? k.lastPlayedPad() : -1;
    }
    bool tabbable(juce::Component* c) {
        const auto all = card().createKeyboardFocusTraverser()->getAllComponents(&card());
        return std::find(all.begin(), all.end(), c) != all.end();
    }
};

const synth::MusicalScale kCMajor = synth::makeScaleFromMask(0, 0x0AB5, "Major");

} // namespace

TEST(DrumKitPadNoteCard, TheNoteIsLabelledAsTheKeyThatPlaysThePad) {
    Scene scene;
    auto& controls = scene.controls();
    EXPECT_EQ(controls.noteCaption().getText(), "Plays on");
    EXPECT_EQ(controls.noteStepper().getValueText(), "D1 (38)");
    EXPECT_EQ(controls.noteStepper().getTitle(), "Plays on key for Snare");
    EXPECT_TRUE(controls.noteStepper().getTooltip().contains("plays when this note arrives"));
    EXPECT_TRUE(controls.noteStepper().getTooltip().contains("not the drum's pitch"));
    EXPECT_TRUE(controls.noteStepper().isVisible());
}

TEST(DrumKitPadNoteCard, TheNoteIsEditedByKeysAndTypingAsOneUndoStepAndMovesTheTrigger) {
    Scene scene;
    auto& stepper = scene.controls().noteStepper();
    EXPECT_EQ(scene.padStruckBy(38), 1);

    EXPECT_TRUE(stepper.keyPressed(juce::KeyPress(juce::KeyPress::upKey)));
    EXPECT_EQ(scene.kit().pad(1).note, 39);
    EXPECT_EQ(stepper.getValueText(), "D#1 (39)");
    EXPECT_EQ(scene.kit().pad(1).name, "Snare") << "only the trigger moves";

    stepper.onTypedValue("f#5");
    EXPECT_EQ(scene.kit().pad(1).note, 90);
    EXPECT_EQ(scene.padStruckBy(90), 1) << "the new note plays the pad";
    EXPECT_NE(scene.padStruckBy(38), 1) << "the old note no longer does";

    stepper.onTypedValue("nonsense");
    EXPECT_EQ(scene.kit().pad(1).note, 48) << "text that is no note changes nothing";
    EXPECT_EQ(stepper.getValueText(), "F#5 (90)");

    ASSERT_TRUE(scene.canvas.undo.undo());
    scene.pads().poll();
    EXPECT_EQ(scene.kit().pad(1).note, 39) << "the typed note was one undo step";
    EXPECT_EQ(stepper.getValueText(), "D#1 (39)");
}

TEST(DrumKitPadNoteCard, TheNoteSurvivesASaveAndAReload) {
    Scene scene;
    scene.controls().noteStepper().onTypedValue("36");
    Scene other;
    other.kit().setExtraState(scene.kit().getExtraState());
    EXPECT_EQ(other.kit().pad(1).note, 36);
    other.pads().poll();
    EXPECT_EQ(other.controls().noteStepper().getValueText(), "C1 (36)");
}

TEST(DrumKitPadNoteCard, TheNoteFieldAndTheTuneScaleAreReachedByTab) {
    Scene scene;
    EXPECT_TRUE(scene.tabbable(&scene.controls().noteStepper()));
    EXPECT_TRUE(scene.tabbable(&scene.controls().tuneKeyField()));
    EXPECT_TRUE(scene.controls().noteStepper().getWantsKeyboardFocus());
    EXPECT_TRUE(scene.controls().tuneKeyField().getWantsKeyboardFocus());
    EXPECT_FALSE(scene.controls().tuneKeyField().getTitle().isEmpty());
}

TEST(DrumKitPadNoteCard, TuneLandsOnTheNotesOfTheChosenKeyAndIsFreeWithoutOne) {
    Scene scene;
    auto& knob = scene.controls().knob(PadParam::Tune);
    EXPECT_DOUBLE_EQ(knob.snapValue(1.0, juce::Slider::absoluteDrag), 1.0) << "no scale: every semitone";

    scene.pads().setTuneScaleUndoably(false, kCMajor);
    EXPECT_DOUBLE_EQ(knob.snapValue(1.0, juce::Slider::absoluteDrag), 0.0) << "C#: a tie goes down to C";
    EXPECT_DOUBLE_EQ(knob.snapValue(3.6, juce::Slider::absoluteDrag), 4.0);
    EXPECT_DOUBLE_EQ(knob.snapValue(-6.0, juce::Slider::absoluteDrag), -7.0)
        << "below the root works too, a tie going down";

    knob.setValue(0.0, juce::dontSendNotification);
    EXPECT_TRUE(knob.keyPressed(juce::KeyPress(juce::KeyPress::upKey)));
    EXPECT_NEAR(knob.getValue(), 2.0, 1e-6) << "Up is the next note of the key";
    EXPECT_TRUE(knob.keyPressed(juce::KeyPress(juce::KeyPress::downKey)));
    EXPECT_NEAR(knob.getValue(), 0.0, 1e-6);
}

TEST(DrumKitPadNoteCard, TheTuneKeyFollowsTheSongKeyOrItsOwnAndIsOneUndoStepAndSaved) {
    Scene scene;
    auto& controls = scene.controls();
    auto& field = controls.tuneKeyField();
    EXPECT_FALSE(field.isFollowing());
    EXPECT_EQ(field.getTitle(), "Tune scale, own key");

    std::optional<synth::MusicalScale> songKey = synth::makeScaleFromMask(2, 0x06AD, "Dorian");
    controls.songKey = [&] { return songKey; };
    field.onFollow();
    EXPECT_TRUE(scene.kit().padTable().tuneFollowsSong);
    EXPECT_TRUE(field.isFollowing());
    EXPECT_EQ(field.getTitle(), "Tune scale, following song key");
    ASSERT_TRUE(controls.activeTuneKey().has_value());
    EXPECT_EQ(controls.activeTuneKey()->rootPitchClass, 2);
    EXPECT_DOUBLE_EQ(controls.knob(PadParam::Tune).snapValue(1.0, juce::Slider::absoluteDrag), 0.0)
        << "Dorian has no minor second: C# lands on the root";

    songKey = std::nullopt;
    EXPECT_FALSE(controls.activeTuneKey().has_value()) << "no song key: every semitone";

    field.onPickOwn(kCMajor);
    EXPECT_FALSE(scene.kit().padTable().tuneFollowsSong);
    ASSERT_TRUE(scene.kit().padTable().tuneKey.has_value());
    EXPECT_EQ(*scene.kit().padTable().tuneKey, kCMajor);

    Scene other;
    other.kit().setExtraState(scene.kit().getExtraState());
    EXPECT_EQ(other.kit().padTable().tuneKey, scene.kit().padTable().tuneKey);

    ASSERT_TRUE(scene.canvas.undo.undo());
    scene.pads().poll();
    EXPECT_TRUE(scene.kit().padTable().tuneFollowsSong) << "the pick was one undo step";
}

TEST(DrumKitPadNoteCard, EveryBuiltInScaleAtEveryRootLandsOnItsOwnNotes) {
    for (const auto& preset : synth::builtInScalePresets()) {
        if ((preset.mask & 0xFFF) == 0xFFF)
            continue; // chromatic: every semitone, nothing to snap
        for (int root = 0; root < 12; ++root) {
            const auto key =
                std::optional<synth::MusicalScale>(synth::makeScaleFromMask(root, preset.mask, preset.name));
            for (double tune = -12.0; tune <= 12.0; tune += 0.25) {
                const double snapped = synth::snapTuneToKey(tune, key, -12.0, 12.0);
                ASSERT_TRUE(key->contains(root + (int)snapped)) << preset.name << " root " << root << " tune " << tune;
                ASSERT_GE(snapped, -12.0);
                ASSERT_LE(snapped, 12.0);
                ASSERT_LE(std::abs(snapped - std::round(tune)), 3.0) << "the nearest note, never a far one";
            }
            const auto up = synth::stepTuneInKey(0.0, 1, key, -12.0, 12.0);
            ASSERT_TRUE(up.has_value());
            ASSERT_GT(*up, 0.0) << preset.name;
            ASSERT_TRUE(key->contains(root + (int)*up)) << preset.name;
        }
    }
    EXPECT_FALSE(synth::stepTuneInKey(0.0, 1, std::nullopt, -12.0, 12.0).has_value());
    EXPECT_DOUBLE_EQ(synth::snapTuneToKey(1.0, synth::makeScaleFromMask(0, 0xFFF), -12.0, 12.0), 1.0) << "chromatic";
}
