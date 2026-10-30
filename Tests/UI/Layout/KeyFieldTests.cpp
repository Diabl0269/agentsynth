// Concern: KeyField, the closed box of a key choice: its text, and that its keys open the picker once and are consumed
// (so a focused field's Space never also toggles playback).
#include "../../TestWait.h"
#include "../ModuleViews/StepGrid/StepGridCardFixture.h"
#include "../SourceScan.h"
#include "Music/SongKey.h"
#include "UI/Layout/FollowKeyField.h"
#include "UI/Layout/KeyField.h"
#include "UI/ModuleViews/SequencerKey/SequencerKeyField.h"
#include "UI/PianoRoll/ScaleAssistPanel.h"
#include "UI/Timeline/SongKeyField.h"
#include <gtest/gtest.h>
#include <regex>

using synth::ui::KeyField;

TEST(KeyFieldTest, ShowsTheKeyOrTheNoKeyText) {
    KeyField field;
    EXPECT_EQ(field.getText(), "No key");
    field.setKey(synth::makeScale(9, synth::scalePresetIndex("Natural Minor")));
    EXPECT_EQ(field.getText(), "A Natural Minor");
    field.setKey(std::nullopt);
    field.setNoKeyText("None");
    EXPECT_EQ(field.getText(), "None");
}

TEST(KeyFieldTest, SpaceAndReturnOpenOnceAndAreConsumedButUpAndDownAreNot) {
    KeyField field;
    int opened = 0;
    field.onOpen = [&] { ++opened; };
    for (const auto& key : {juce::KeyPress(juce::KeyPress::spaceKey), juce::KeyPress(juce::KeyPress::returnKey)}) {
        const int before = opened;
        EXPECT_TRUE(field.keyPressed(key)) << key.getTextDescription();
        EXPECT_EQ(opened, before + 1);
    }
    for (const auto& key : {juce::KeyPress(juce::KeyPress::downKey), juce::KeyPress(juce::KeyPress::upKey)}) {
        EXPECT_FALSE(field.keyPressed(key)) << key.getTextDescription() << " bubbles so the region can move on";
        EXPECT_EQ(opened, 2);
    }
}

TEST(KeyFieldTest, AClickDoesNotTakeKeyboardFocusButTabReachesIt) {
    KeyField field;
    EXPECT_FALSE(field.getMouseClickGrabsKeyboardFocus());
    EXPECT_TRUE(field.getWantsKeyboardFocus());
}

TEST(KeyFieldTest, ADisabledFieldDoesNotOpen) {
    KeyField field;
    int opened = 0;
    field.onOpen = [&] { ++opened; };
    field.setEnabled(false);
    field.showPopup();
    EXPECT_EQ(opened, 0);
}

TEST(KeyFieldTest, FollowingReadsSongKeyBeforeTheKeyAndSaysSoToTheUser) {
    KeyField field;
    field.setKey(synth::makeScale(2, synth::scalePresetIndex("Dorian")));
    field.setFollowing(true);
    EXPECT_TRUE(field.isFollowing());
    EXPECT_EQ(field.getText(), juce::String::fromUTF8("Song key \xc2\xb7 D Dorian"));
    EXPECT_EQ(field.getTitle(), "Key, following song key");
    EXPECT_EQ(field.getTooltip(), "Following the song key. Click to give this its own key.");

    field.setFollowing(false);
    EXPECT_FALSE(field.isFollowing());
    EXPECT_EQ(field.getText(), juce::String::fromUTF8("D Dorian \xc2\xb7 own"));
    EXPECT_EQ(field.getTitle(), "Key, own key");
}

namespace {

juce::MouseEvent clickEvent(juce::Component& comp, juce::Point<int> pos) {
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), pos.toFloat(), juce::ModifierKeys(),
                            0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &comp, &comp, juce::Time::getCurrentTime(), pos.toFloat(),
                            juce::Time::getCurrentTime(), 1, false);
}

/** A real click: mouseDown and mouseUp, then the message loop runs so ComboBox's asynchronous open lands. */
void clickAndSettle(juce::Component& comp) {
    const auto pos = comp.getLocalBounds().getCentre();
    comp.mouseDown(clickEvent(comp, pos));
    comp.mouseUp(clickEvent(comp, pos));
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
}

/** Clicks `field` twice, closing whatever it opened in between, and returns how many times it opened. `opens` counts
 *  the opens the owner reaches; `closeOpened` closes the picker the first click opened. */
int opensAfterTwoClicks(juce::Component& field, const int& opens, const std::function<void()>& closeOpened) {
    clickAndSettle(field);
    closeOpened();
    clickAndSettle(field);
    return opens;
}

} // namespace

// Every owner of a KeyField: a second click, after the first picker closed, opens the picker again.
TEST(KeyFieldTest, EveryKeyFieldOpensItsPickerOnASecondClick) {
    int opens = 0;
    std::unique_ptr<juce::Component> held;
    const auto launcher = [&](std::unique_ptr<juce::Component> picker, juce::Rectangle<int>) {
        ++opens;
        held = std::move(picker);
    };
    const auto close = [&] { held.reset(); };

    {
        KeyField field;
        field.setBounds(0, 0, 120, 24);
        field.onOpen = [&] { ++opens; };
        EXPECT_EQ(opensAfterTwoClicks(field, opens, [] {}), 2) << "KeyField";
    }
    opens = 0;
    {
        synth::ui::SongKeyField field;
        field.setBounds(0, 0, 120, 24);
        field.pickerLauncher = launcher;
        EXPECT_EQ(opensAfterTwoClicks(field, opens, close), 2) << "SongKeyField";
        held.reset();
    }
    opens = 0;
    {
        synth::ui::FollowKeyField field;
        field.setBounds(0, 0, 120, 24);
        field.pickerLauncher = launcher;
        EXPECT_EQ(opensAfterTwoClicks(field, opens, close), 2) << "FollowKeyField";
        held.reset();
    }
    opens = 0;
    {
        stepgrid_test::SeqCard card;
        auto* field =
            card.card != nullptr && card.card->getCardBody() != nullptr
                ? dynamic_cast<synth::ui::SequencerKeyField*>(card.card->getCardBody()->findWidget("scaleRoot"))
                : nullptr;
        ASSERT_NE(field, nullptr);
        field->setSize(120, 24);
        field->pickerLauncher = launcher;
        EXPECT_EQ(opensAfterTwoClicks(*field, opens, close), 2) << "SequencerKeyField";
        held.reset();
    }
    opens = 0;
    {
        synth::ui::ScaleAssistPanel panel;
        panel.pickerLauncher = launcher;
        auto& field = panel.getKeyField();
        field.setSize(120, 24);
        EXPECT_EQ(opensAfterTwoClicks(field, opens, close), 2) << "ScaleAssistPanel key field";
        held.reset();
    }
}

TEST(KeyFieldTest, ASecondClickWhileAPickerIsOpenStillShowsOnePicker) {
    int launched = 0;
    std::unique_ptr<juce::Component> held;
    synth::ui::SongKeyField field;
    field.setBounds(0, 0, 120, 24);
    field.pickerLauncher = [&](std::unique_ptr<juce::Component> picker, juce::Rectangle<int>) {
        ++launched;
        held = std::move(picker);
    };
    clickAndSettle(field);
    clickAndSettle(field);
    EXPECT_EQ(launched, 1);
    EXPECT_NE(field.getOpenPicker(), nullptr);
}

// A popup opener that never shows the stock menu leaves ComboBox's "menu active" latch raised, and every later click
// is ignored. Each `void showPopup() override` under Source/ must lower it with hidePopup(), in its own file or its
// sibling .h/.cpp (NoteLengthBox lowers it when its popup closes).
TEST(KeyFieldTest, EveryShowPopupOverrideLowersTheComboMenuLatch) {
    static const std::regex opener(R"(void\s+showPopup\s*\(\s*\)\s*override)");
    static const std::regex lowers(R"(hidePopup\s*\()");
    const auto texts = synth::test::source_scan::sourceTexts();
    int found = 0;
    for (const auto& [path, text] : texts) {
        if (!std::regex_search(text, opener))
            continue;
        ++found;
        const auto stem = path.substr(0, path.find_last_of('.'));
        bool lowered = false;
        for (const auto* ext : {".h", ".cpp"}) {
            const auto sibling = texts.find(stem + ext);
            lowered = lowered || (sibling != texts.end() && std::regex_search(sibling->second, lowers));
        }
        EXPECT_TRUE(lowered) << path
                             << " overrides showPopup() but never calls hidePopup(), so its combo opens only once";
    }
    EXPECT_GE(found, 3);
}
