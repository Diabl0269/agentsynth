// NoteLengthMenuTests.cpp -- the shared note-length menu (Layout/NoteLengthMenu): how a list of names splits into the
// popup's groups, the popup's chips, keys and mouse path, the closed box and its parameter attachment.
//
// A headless component cannot hold real keyboard focus, so keys are delivered the way the native window delivers them
// (the focused control first, then up its parents) to the box or to the popup; the popup tracks its own keyboard
// cursor for exactly this reason.

#include "Modules/Envelope/EnvelopeTempoSync.h"
#include "Modules/LFOModule.h"
#include "Modules/LfoRateDivisions.h"
#include "UI/Layout/NoteLengthMenu/NoteLengthBox.h"
#include "UI/Layout/NoteLengthMenu/NoteLengthMenuModel.h"
#include "UI/Layout/NoteLengthMenu/NoteLengthPopup.h"
#include "UI/Layout/PopupMotion.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

using synth::NoteKind;
using synth::ui::NoteLengthBox;
using synth::ui::NoteLengthMenuModel;
using synth::ui::NoteLengthPopup;

namespace {

juce::MouseEvent mouseEvent(juce::Component& comp) {
    const auto at = comp.getLocalBounds().toFloat().getCentre();
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), at,
                            juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                            &comp, &comp, juce::Time::getCurrentTime(), at, juce::Time::getCurrentTime(), 1, false);
}

// A press and release on the middle of `button`, as the live mouse delivers them.
void click(juce::Button& button) {
    auto& comp = static_cast<juce::Component&>(button);
    comp.mouseEnter(mouseEvent(comp));
    comp.mouseDown(mouseEvent(comp));
    comp.mouseUp(mouseEvent(comp));
}

juce::KeyPress letter(char c) { return juce::KeyPress((int)c, juce::ModifierKeys(), (juce::juce_wchar)c); }

// Every test keeps the popup off the desktop: no native window is needed to exercise it.
struct SkipDesktop {
    SkipDesktop() { NoteLengthPopup::setSkipDesktopForTest(true); }
    ~SkipDesktop() { NoteLengthPopup::setSkipDesktopForTest(false); }
};

juce::StringArray extrasThenNotes() {
    juce::StringArray names{"Off", "Bar"};
    names.addArray(synth::envelopeNoteDivisions());
    return names;
}

} // namespace

// ---- the model ---------------------------------------------------------------------------------------------------

TEST(NoteLengthMenuModel, SplitsALfoListIntoNumbersPerKindAndTheBarLengths) {
    const NoteLengthMenuModel model(synth::lfoRateDivisions());
    EXPECT_TRUE(model.leading().empty());
    for (const auto kind : {NoteKind::straight, NoteKind::triplet, NoteKind::dotted}) {
        ASSERT_TRUE(model.hasKind(kind));
        const auto& chips = model.numbers(kind);
        ASSERT_EQ(chips.size(), 8u);
        const char* labels[] = {"1", "2", "4", "8", "16", "32", "64", "128"};
        for (size_t i = 0; i < 8; ++i)
            EXPECT_EQ(chips[i].label, labels[i]) << "the chips write only the bottom number, longest note first";
    }
    EXPECT_EQ(model.numbers(NoteKind::triplet)[3].tooltip, "1/8T");
    EXPECT_EQ(model.numbers(NoteKind::dotted)[2].name, "1/4D");
    ASSERT_EQ(model.trailing().size(), 3u);
    EXPECT_EQ(model.trailing()[0].label, "2");
    EXPECT_EQ(model.trailing()[0].name, "2/1");
    EXPECT_EQ(model.trailing()[0].tooltip, "2/1 (2 bars)");
    EXPECT_EQ(model.trailing()[2].label, "8");
}

TEST(NoteLengthMenuModel, ExtrasBeforeTheNotesLeadAndAfterThemTrail) {
    juce::StringArray names = extrasThenNotes();
    names.add("Free");
    const NoteLengthMenuModel model(names);
    ASSERT_EQ(model.leading().size(), 2u);
    EXPECT_EQ(model.leading()[0].label, "Off");
    EXPECT_EQ(model.leading()[1].label, "Bar");
    ASSERT_EQ(model.trailing().size(), 1u);
    EXPECT_EQ(model.trailing()[0].name, "Free");
    EXPECT_EQ(model.kindOf("Off"), NoteKind::straight);
    EXPECT_EQ(model.kindOf("1/16T"), NoteKind::triplet);
    EXPECT_EQ(model.kindOf("1/16D"), NoteKind::dotted);
    EXPECT_EQ(model.kindOf("2/1"), NoteKind::straight);
}

TEST(NoteLengthMenuModel, AKindWithNoNameInTheListIsUnavailable) {
    const NoteLengthMenuModel model({"Off", "1/4", "1/8", "1/4T"});
    EXPECT_TRUE(model.hasKind(NoteKind::straight));
    EXPECT_TRUE(model.hasKind(NoteKind::triplet));
    EXPECT_FALSE(model.hasKind(NoteKind::dotted));
    EXPECT_EQ(model.numbers(NoteKind::straight).size(), 2u) << "only the values the list has";
}

TEST(NoteLengthMenuModel, ScreenReaderNamesSpellOutTheKind) {
    EXPECT_EQ(NoteLengthMenuModel::accessibleNameFor("1/8T"), "1/8 triplet");
    EXPECT_EQ(NoteLengthMenuModel::accessibleNameFor("1/4D"), "1/4 dotted");
    EXPECT_EQ(NoteLengthMenuModel::accessibleNameFor("1/16"), "1/16");
    EXPECT_EQ(NoteLengthMenuModel::accessibleNameFor("4/1"), "4 bars");
    EXPECT_EQ(NoteLengthMenuModel::accessibleNameFor("Off"), "Off");
}

// ---- the popup ---------------------------------------------------------------------------------------------------

namespace {
struct PopupRig {
    SkipDesktop skip;
    juce::Component anchor;
    std::unique_ptr<NoteLengthPopup> popup;
    juce::String picked;
    bool closed = false;

    explicit PopupRig(const juce::StringArray& choices, const juce::String& current) {
        anchor.setBounds(100, 100, 80, 22);
        NoteLengthPopup::Options options;
        options.choices = choices;
        options.current = current;
        options.title = "Test note length";
        options.unavailableTip[(size_t)NoteKind::dotted] = "No dotted values here";
        popup = std::make_unique<NoteLengthPopup>(anchor, options);
        popup->onPick = [this](const juce::String& name) { picked = name; };
        popup->onClosed = [this] { closed = true; };
        popup->showOnDesktop();
    }
};
} // namespace

TEST(NoteLengthPopup, OpensOnTheKindOfTheCurrentValueWithOneChipPerNumber) {
    PopupRig rig(synth::lfoRateDivisions(), "1/8T");
    EXPECT_EQ(rig.popup->getKind(), NoteKind::triplet);
    EXPECT_TRUE(rig.popup->kindButton(NoteKind::triplet).getToggleState());
    EXPECT_FALSE(rig.popup->kindButton(NoteKind::straight).getToggleState());
    const auto chips = rig.popup->shownChips();
    ASSERT_EQ(chips.size(), 8u + 3u) << "eight numbers and the three bar lengths";
    EXPECT_EQ(chips[0]->getName(), "1");
    EXPECT_EQ(chips[7]->getName(), "128");
    EXPECT_EQ(chips[8]->getName(), "2") << "the bar lengths follow the numbers";
    EXPECT_EQ(chips[3]->getTooltip(), "1/8T") << "hover shows the full name";
    EXPECT_TRUE(chips[3]->getToggleState()) << "the current value is lit";
    EXPECT_EQ(rig.popup->activeItem(), chips[3]) << "the keyboard cursor starts on it";
}

TEST(NoteLengthPopup, EveryControlHasAScreenReaderNameAndATooltip) {
    PopupRig rig(extrasThenNotes(), "1/4");
    EXPECT_EQ(rig.popup->getTitle(), "Test note length");
    EXPECT_EQ(rig.popup->kindButton(NoteKind::straight).getTooltip(), "Straight");
    EXPECT_EQ(rig.popup->kindButton(NoteKind::triplet).getTooltip(), "Triplet");
    EXPECT_EQ(rig.popup->kindButton(NoteKind::dotted).getTooltip(), "Dotted");
    EXPECT_EQ(rig.popup->kindButton(NoteKind::dotted).getTitle(), "Dotted");
    for (auto* chip : rig.popup->shownChips()) {
        EXPECT_TRUE(chip->getTitle().isNotEmpty());
        EXPECT_TRUE(chip->getTooltip().isNotEmpty());
        EXPECT_TRUE(chip->getWantsKeyboardFocus());
    }
}

TEST(NoteLengthPopup, SwitchKindFromTheKeyboardRewritesTheChipNames) {
    PopupRig rig(synth::envelopeNoteDivisions(), "1/4");
    EXPECT_EQ(rig.popup->getKind(), NoteKind::straight);
    EXPECT_TRUE(rig.popup->keyPressed(letter('t')));
    EXPECT_EQ(rig.popup->getKind(), NoteKind::triplet);
    EXPECT_EQ(rig.popup->shownChips()[2]->getTooltip(), "1/4T");
    EXPECT_TRUE(rig.popup->keyPressed(letter('D')));
    EXPECT_EQ(rig.popup->getKind(), NoteKind::dotted);
    EXPECT_EQ(rig.popup->shownChips()[2]->getTooltip(), "1/4D");
    EXPECT_TRUE(rig.popup->keyPressed(letter('s')));
    EXPECT_EQ(rig.popup->getKind(), NoteKind::straight);
    EXPECT_EQ(rig.popup->shownChips()[2]->getTooltip(), "1/4");
    EXPECT_FALSE(rig.closed) << "switching the kind picks nothing";
    EXPECT_TRUE(rig.picked.isEmpty());
}

TEST(NoteLengthPopup, ClickingALetterSwitchesTheKind) {
    PopupRig rig(synth::envelopeNoteDivisions(), "1/4");
    click(rig.popup->kindButton(NoteKind::dotted));
    EXPECT_EQ(rig.popup->getKind(), NoteKind::dotted);
    EXPECT_TRUE(rig.popup->kindButton(NoteKind::dotted).getToggleState());
    EXPECT_FALSE(rig.popup->kindButton(NoteKind::straight).getToggleState());
}

TEST(NoteLengthPopup, PickingAChipWithTheMousePicksItsFullNameAndCloses) {
    PopupRig rig(synth::envelopeNoteDivisions(), "1/4");
    click(rig.popup->kindButton(NoteKind::triplet));
    click(*rig.popup->shownChips()[3]); // the "8" chip
    EXPECT_EQ(rig.picked, "1/8T");
    EXPECT_TRUE(rig.popup->isClosing());
    EXPECT_TRUE(rig.closed);
}

TEST(NoteLengthPopup, ArrowsMoveBetweenChipsAndEnterPicks) {
    PopupRig rig(synth::envelopeNoteDivisions(), "1/4"); // the cursor starts on the "4" chip
    ASSERT_NE(rig.popup->activeItem(), nullptr);
    EXPECT_EQ(rig.popup->activeItem()->getName(), "4");
    EXPECT_TRUE(rig.popup->keyPressed(juce::KeyPress(juce::KeyPress::rightKey)));
    EXPECT_EQ(rig.popup->activeItem()->getName(), "8");
    EXPECT_TRUE(rig.popup->keyPressed(juce::KeyPress(juce::KeyPress::leftKey)));
    EXPECT_TRUE(rig.popup->keyPressed(juce::KeyPress(juce::KeyPress::leftKey)));
    EXPECT_EQ(rig.popup->activeItem()->getName(), "2");
    EXPECT_TRUE(rig.popup->keyPressed(letter('d')));
    EXPECT_EQ(rig.popup->activeItem()->getName(), "2") << "the cursor stays on its chip when the kind changes";
    EXPECT_TRUE(rig.popup->keyPressed(juce::KeyPress(juce::KeyPress::returnKey)));
    EXPECT_EQ(rig.picked, "1/2D");
    EXPECT_TRUE(rig.closed);
}

TEST(NoteLengthPopup, ArrowsStopAtTheEnds) {
    PopupRig rig(synth::envelopeNoteDivisions(), "1/1");
    for (int i = 0; i < 20; ++i)
        rig.popup->keyPressed(juce::KeyPress(juce::KeyPress::leftKey));
    ASSERT_NE(rig.popup->activeItem(), nullptr);
    EXPECT_EQ(rig.popup->activeItem()->getTitle(), "Straight") << "Left walks back through the letters, then stops";
    for (int i = 0; i < 30; ++i)
        rig.popup->keyPressed(juce::KeyPress(juce::KeyPress::rightKey));
    EXPECT_EQ(rig.popup->activeItem(), rig.popup->shownChips().back());
}

TEST(NoteLengthPopup, EscapeClosesWithoutPicking) {
    PopupRig rig(synth::envelopeNoteDivisions(), "1/4");
    EXPECT_TRUE(rig.popup->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
    EXPECT_TRUE(rig.closed);
    EXPECT_TRUE(rig.picked.isEmpty());
    EXPECT_TRUE(rig.popup->restoresFocus()) << "a keyboard close hands the focus back to the box";
}

TEST(NoteLengthPopup, ADottedStepIsDisabledWithItsReasonWhenTheListHasNone) {
    PopupRig rig({"Off", "1/4", "1/8", "1/4T", "1/8T"}, "1/8");
    auto& dotted = rig.popup->kindButton(NoteKind::dotted);
    EXPECT_FALSE(dotted.isEnabled());
    EXPECT_EQ(dotted.getTooltip(), "No dotted values here");
    EXPECT_TRUE(rig.popup->kindButton(NoteKind::triplet).isEnabled());
    rig.popup->keyPressed(letter('d'));
    EXPECT_EQ(rig.popup->getKind(), NoteKind::straight) << "the key does nothing for a kind the list lacks";
    EXPECT_EQ(rig.popup->shownChips().size(), 1u + 2u) << "Off and the two numbers the list has";
}

TEST(NoteLengthPopup, ExtraChipsSitInFrontOfTheNumbers) {
    PopupRig rig(extrasThenNotes(), "Bar");
    const auto chips = rig.popup->shownChips();
    ASSERT_EQ(chips.size(), 2u + 8u);
    EXPECT_EQ(chips[0]->getName(), "Off");
    EXPECT_EQ(chips[1]->getName(), "Bar");
    EXPECT_EQ(chips[2]->getName(), "1");
    EXPECT_TRUE(chips[1]->getToggleState());
    EXPECT_EQ(rig.popup->getKind(), NoteKind::straight);
    click(*chips[0]);
    EXPECT_EQ(rig.picked, "Off");
}

TEST(NoteLengthPopup, APressOutsideClosesItAndOneInsideDoesNot) {
    PopupRig rig(synth::envelopeNoteDivisions(), "1/4");
    juce::Component outside;
    outside.setBounds(500, 500, 40, 40);
    rig.popup->mouseDown(mouseEvent(*rig.popup->shownChips().front()));
    EXPECT_FALSE(rig.popup->isClosing());
    rig.popup->mouseDown(mouseEvent(outside));
    EXPECT_TRUE(rig.popup->isClosing());
    EXPECT_TRUE(rig.closed);
    EXPECT_TRUE(rig.picked.isEmpty());
    EXPECT_FALSE(rig.popup->restoresFocus()) << "a click elsewhere has already moved the focus";
}

TEST(NoteLengthPopup, GrowsOutOfItsBoxAndReturnsIntoIt) {
    PopupRig rig(synth::envelopeNoteDivisions(), "1/4");
    const auto style = rig.popup->motionStyle();
    EXPECT_LT(style.inStartScale, 1.0f) << "it arrives scaled up from a smaller picture";
    EXPECT_LT(style.outEndScale, 1.0f) << "and leaves back down toward the box";
    ASSERT_TRUE(style.anchor);
    EXPECT_EQ(style.anchor(), rig.anchor.getScreenBounds().getCentre());
    ASSERT_TRUE(style.body);
    EXPECT_NE(style.body(), nullptr);
    ASSERT_TRUE(style.bodyPivot);
    const auto pivot = style.bodyPivot();
    const auto boxCentreInPopup = rig.anchor.getScreenBounds().getCentre().x - rig.popup->getX();
    EXPECT_NEAR(pivot.x, (float)boxCentreInPopup, 1.0f) << "it pivots under the middle of the box";
    EXPECT_LT(pivot.y, (float)rig.popup->getHeight() / 2.0f) << "on the top edge, as it hangs below the box";
}

// ---- the closed box ----------------------------------------------------------------------------------------------

TEST(NoteLengthBox, ShowsTheFullNameOfTheChoice) {
    NoteLengthBox box;
    box.setChoices(synth::envelopeNoteDivisions());
    box.setCurrentName("1/8T", juce::dontSendNotification);
    EXPECT_EQ(box.getText(), "1/8T");
    EXPECT_EQ(box.getCurrentName(), "1/8T");
    EXPECT_EQ(box.getSelectedId(), synth::envelopeNoteDivisionIndex("1/8T") + 1) << "item id = list index + 1";
    box.setCurrentName("nope", juce::dontSendNotification);
    EXPECT_EQ(box.getCurrentName(), "1/8T") << "an unknown name changes nothing";
}

TEST(NoteLengthBox, ATextPrefixIsWrittenBeforeEveryName) {
    NoteLengthBox box;
    box.setChoices({"Off", "1/4", "1/8"}, "Grid ");
    box.setCurrentName("1/8", juce::dontSendNotification);
    EXPECT_EQ(box.getText(), "Grid 1/8");
    EXPECT_EQ(box.getItemText(0), "Grid Off");
    EXPECT_EQ(box.getCurrentName(), "1/8") << "the name itself carries no prefix";
}

TEST(NoteLengthBox, KeepsTheSelectionWhenTheListIsReplaced) {
    NoteLengthBox box;
    box.setChoices(synth::envelopeNoteDivisions());
    box.setCurrentName("1/16", juce::dontSendNotification);
    box.setChoices(synth::lfoRateDivisions());
    EXPECT_EQ(box.getCurrentName(), "1/16");
}

TEST(NoteLengthBox, TabsToTheBoxAndKeysOpenPickAndWriteTheParameter) {
    SkipDesktop skip;
    LFOModule lfo;
    auto* param = dynamic_cast<juce::AudioParameterChoice*>(lfo.getParameters()[5]);
    ASSERT_NE(param, nullptr);
    ASSERT_EQ(param->paramID, "rateSync");

    juce::Component host;
    juce::TextButton before("before");
    NoteLengthBox box;
    host.addAndMakeVisible(before);
    host.addAndMakeVisible(box);
    before.setBounds(0, 0, 60, 22);
    box.setBounds(70, 0, 80, 22);
    host.setSize(200, 40);
    box.setChoices(param->choices);
    juce::ComboBoxParameterAttachment attachment(*param, box);

    // Tab from the previous control lands on the box.
    juce::KeyboardFocusTraverser traverser;
    EXPECT_TRUE(box.getWantsKeyboardFocus());
    EXPECT_EQ(traverser.getNextComponent(&before), &box);

    EXPECT_EQ(box.getCurrentName(), "1/4") << "the box opens showing the parameter's value";
    EXPECT_FALSE(box.keyPressed(juce::KeyPress(juce::KeyPress::downKey))) << "Down is the region's, not the box's";
    EXPECT_EQ(box.getPopup(), nullptr);
    ASSERT_TRUE(box.keyPressed(juce::KeyPress(juce::KeyPress::returnKey)));
    auto* popup = box.getPopup();
    ASSERT_NE(popup, nullptr) << "Enter opens the popup";
    ASSERT_TRUE(popup->keyPressed(letter('t')));
    ASSERT_TRUE(popup->keyPressed(juce::KeyPress(juce::KeyPress::rightKey)));
    ASSERT_TRUE(popup->keyPressed(juce::KeyPress(juce::KeyPress::returnKey)));

    EXPECT_EQ(param->getCurrentChoiceName(), "1/8T") << "the attachment wrote the parameter";
    EXPECT_EQ(box.getText(), "1/8T");
    EXPECT_EQ(box.getPopup(), nullptr) << "the popup is gone once it closed";
}

TEST(NoteLengthBox, SpaceAndEnterOpenThePopupAndEscapeLeavesTheValue) {
    SkipDesktop skip;
    NoteLengthBox box;
    box.setBounds(0, 0, 80, 22);
    box.setChoices(synth::envelopeNoteDivisions());
    box.setCurrentName("1/2", juce::dontSendNotification);
    for (const int key : {juce::KeyPress::spaceKey, juce::KeyPress::returnKey}) {
        ASSERT_TRUE(box.keyPressed(juce::KeyPress(key))) << key;
        ASSERT_NE(box.getPopup(), nullptr) << key;
        EXPECT_EQ(box.getPopup()->activeItem()->getTooltip(), "1/2") << "it opens on the current value";
        box.getPopup()->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
        EXPECT_EQ(box.getPopup(), nullptr);
        EXPECT_EQ(box.getCurrentName(), "1/2");
    }
}

TEST(NoteLengthBox, AParameterChangeFromElsewhereMovesTheBox) {
    LFOModule lfo;
    auto* param = dynamic_cast<juce::AudioParameterChoice*>(lfo.getParameters()[5]);
    ASSERT_NE(param, nullptr);
    NoteLengthBox box;
    box.setChoices(param->choices);
    juce::ComboBoxParameterAttachment attachment(*param, box);
    *param = synth::lfoRateDivisions().indexOf("1/8D");
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    EXPECT_EQ(box.getCurrentName(), "1/8D");
}
