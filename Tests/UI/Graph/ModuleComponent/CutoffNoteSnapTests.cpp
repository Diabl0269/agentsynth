// CutoffNoteSnapTests.cpp -- the Filter cutoff knob's Snap to notes: the snapping and steps of Notes and By key, the
// "A3 220 Hz" readout, the knob menu's choices (Custom type only) and the choice saved in the card's view state.
#include "Modules/CardViewState.h"
#include "Modules/FilterModule.h"
#include "UI/Graph/ModuleComponent/CardKnobSlider.h"
#include "UI/Graph/ModuleComponent/CutoffNoteSnap.h"
#include <gtest/gtest.h>

using namespace synth::ui;
using cutoffsnap::Mode;

namespace {

const synth::MusicalScale kCMajor{0, 0xAB5, "Major"};

juce::RangedAudioParameter* paramOf(FilterModule& filter, const juce::String& id) {
    for (auto* p : filter.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(p); ranged != nullptr && ranged->paramID == id)
            return ranged;
    return nullptr;
}

void setCustomType(FilterModule& filter, bool custom) {
    auto* type = dynamic_cast<juce::AudioParameterChoice*>(paramOf(filter, "filterType"));
    ASSERT_NE(type, nullptr);
    *type = custom ? FilterModule::kCustomTypeIndex : 0;
}

struct KnobRig {
    FilterModule filter;
    CardKnobSlider knob;
    std::optional<synth::MusicalScale> songKey;

    KnobRig() {
        knob.setRange(20.0, 20000.0);
        knob.setValue(1000.0, juce::dontSendNotification);
        cutoffsnap::wire(knob, filter, [this] { return songKey; });
    }

    void setMode(Mode mode) {
        auto state = filter.getCardViewState();
        cutoffsnap::setMode(state, mode);
        filter.setCardViewState(state);
    }
};

} // namespace

TEST(CutoffNoteSnap, NotesLandOnTheNearestSemitoneAndByKeyOnTheNearestNoteOfTheKey) {
    EXPECT_NEAR(cutoffsnap::snapHz(225.0, Mode::notes, std::nullopt), 220.0, 1e-9);
    EXPECT_NEAR(cutoffsnap::snapHz(233.0, Mode::notes, std::nullopt), 233.08, 0.01) << "A#3";
    EXPECT_NEAR(cutoffsnap::snapHz(233.0, Mode::key, kCMajor), 220.0, 1e-9) << "A#3 is not in C major: a tie goes down";
    EXPECT_NEAR(cutoffsnap::snapHz(240.0, Mode::key, kCMajor), 246.94, 0.01) << "B3";
    EXPECT_DOUBLE_EQ(cutoffsnap::snapHz(225.0, Mode::free, std::nullopt), 225.0);
    EXPECT_NEAR(cutoffsnap::snapHz(233.0, Mode::key, std::nullopt), 233.08, 0.01) << "no song key: every note";
}

TEST(CutoffNoteSnap, StepsMoveOneNoteOrOneNoteOfTheKey) {
    EXPECT_NEAR(cutoffsnap::stepHz(220.0, 1, Mode::notes, std::nullopt), 233.08, 0.01);
    EXPECT_NEAR(cutoffsnap::stepHz(220.0, -1, Mode::notes, std::nullopt), 207.65, 0.01);
    EXPECT_NEAR(cutoffsnap::stepHz(220.0, 1, Mode::key, kCMajor), 246.94, 0.01);
    EXPECT_NEAR(cutoffsnap::stepHz(261.63, -1, Mode::key, kCMajor), 246.94, 0.01) << "C4 down to B3";
    EXPECT_EQ(cutoffsnap::readout(220.0), "A3 220 Hz");
}

TEST(CutoffNoteSnap, TheKnobDragsStepsAndReadsByNoteOnlyOnTheCustomType) {
    KnobRig rig;
    setCustomType(rig.filter, true);
    rig.setMode(Mode::notes);
    EXPECT_NEAR(rig.knob.snapValue(225.0, juce::Slider::absoluteDrag), 220.0, 1e-9);
    EXPECT_EQ(rig.knob.getTextFromValue(220.0), "A3 220 Hz");
    rig.knob.setValue(220.0, juce::dontSendNotification);
    EXPECT_TRUE(rig.knob.keyPressed(juce::KeyPress(juce::KeyPress::upKey)));
    EXPECT_NEAR(rig.knob.getValue(), 233.08, 0.01) << "Up is a semitone";

    rig.songKey = kCMajor;
    rig.setMode(Mode::key);
    rig.knob.setValue(220.0, juce::dontSendNotification);
    EXPECT_TRUE(rig.knob.keyPressed(juce::KeyPress(juce::KeyPress::upKey)));
    EXPECT_NEAR(rig.knob.getValue(), 246.94, 0.01) << "Up is the next note of the song key";

    setCustomType(rig.filter, false);
    EXPECT_DOUBLE_EQ(rig.knob.snapValue(225.0, juce::Slider::absoluteDrag), 225.0) << "other types stay free";
    EXPECT_NE(rig.knob.getTextFromValue(220.0), "A3 220 Hz");
}

TEST(CutoffNoteSnap, TheKnobMenuOffersTheThreeChoicesOnTheCustomTypeAndRemembersThePick) {
    KnobRig rig;
    auto* cutoff = paramOf(rig.filter, "cutoff");
    ASSERT_NE(cutoff, nullptr);
    juce::PopupMenu plain;
    cutoffsnap::appendMenuItems(plain, &rig.filter, cutoff, &rig.knob);
    EXPECT_EQ(plain.getNumItems(), 0) << "not on a non-Custom type";

    setCustomType(rig.filter, true);
    juce::PopupMenu other;
    cutoffsnap::appendMenuItems(other, &rig.filter, paramOf(rig.filter, "resonance"), &rig.knob);
    EXPECT_EQ(other.getNumItems(), 0) << "only on the cutoff";

    juce::PopupMenu menu;
    rig.knob.setValue(225.0, juce::dontSendNotification);
    cutoffsnap::appendMenuItems(menu, &rig.filter, cutoff, &rig.knob);
    juce::PopupMenu::MenuItemIterator top(menu);
    ASSERT_TRUE(top.next());
    EXPECT_EQ(top.getItem().text, "Snap to notes");
    ASSERT_NE(top.getItem().subMenu, nullptr);
    std::vector<juce::String> names;
    juce::PopupMenu::MenuItemIterator it(*top.getItem().subMenu);
    std::function<void()> pickNotes;
    while (it.next()) {
        names.push_back(it.getItem().text);
        if (it.getItem().text == "Notes")
            pickNotes = it.getItem().action;
        if (it.getItem().text == "Free")
            EXPECT_TRUE(it.getItem().isTicked) << "Free is the default";
    }
    EXPECT_EQ(names, (std::vector<juce::String>{"Free", "Notes", "By key"}));
    ASSERT_TRUE(pickNotes);
    pickNotes();
    EXPECT_EQ(cutoffsnap::modeOf(rig.filter.getCardViewState()), Mode::notes);
    EXPECT_NEAR(rig.knob.getValue(), 220.0, 1e-9) << "the pick snaps the cutoff at once";

    const auto saved = synth::CardViewState::fromVar(rig.filter.getCardViewState().toVar());
    EXPECT_EQ(saved.cutoffSnap, "notes") << "saved with the project";
    EXPECT_TRUE(synth::CardViewState::fromVar(juce::var()).cutoffSnap.isEmpty());
}
