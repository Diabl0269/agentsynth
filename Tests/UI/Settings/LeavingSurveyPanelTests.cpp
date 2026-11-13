#include "AccountTestFixture.h"
#include "UI/Settings/LeavingSurveyPanel.h"
#include <gtest/gtest.h>

using synth::LeavingSurveyPanel;

TEST(LeavingSurveyPanelTest, ListsTheEightReasonsInOrderWithTheirKeys) {
    const char* expectedKeys[] = {"too_expensive",    "unused",      "low_quality", "missing_features",
                                  "switched_service", "too_complex", "bugs",        "other"};
    const char* expectedLabels[] = {"It costs too much",
                                    "I don't use it enough",
                                    "The AI results weren't good enough",
                                    "It's missing something I need",
                                    "I switched to another tool",
                                    "It's too hard to use",
                                    "Bugs or crashes",
                                    "Something else"};
    LeavingSurveyPanel panel(LeavingSurveyPanel::Kind::cancel);
    ASSERT_EQ(LeavingSurveyPanel::reasons().size(), 8u);
    for (int i = 0; i < 8; ++i) {
        EXPECT_STREQ(LeavingSurveyPanel::reasons()[(size_t)i].key, expectedKeys[i]);
        EXPECT_EQ(panel.getReasonToggleForTest(i).getButtonText(), juce::String(expectedLabels[i]));
    }
    EXPECT_EQ(LeavingSurveyPanel::kindKey(LeavingSurveyPanel::Kind::cancel), juce::String("cancel"));
    EXPECT_EQ(LeavingSurveyPanel::kindKey(LeavingSurveyPanel::Kind::deleteAccount), juce::String("delete"));
}

TEST(LeavingSurveyPanelTest, AnswerHoldsTheTickedKeysInListOrderAndTheTrimmedComment) {
    LeavingSurveyPanel panel(LeavingSurveyPanel::Kind::deleteAccount);
    panel.getReasonToggleForTest(6).setToggleState(true, juce::dontSendNotification);
    panel.getReasonToggleForTest(0).setToggleState(true, juce::dontSendNotification);
    panel.getCommentEditorForTest().setText("  needs work \n", false);

    const auto answer = panel.currentAnswer();
    ASSERT_EQ(answer.reasons.size(), 2u);
    EXPECT_EQ(answer.reasons[0], juce::String("too_expensive"));
    EXPECT_EQ(answer.reasons[1], juce::String("bugs"));
    EXPECT_EQ(answer.comment, juce::String("needs work"));
}

TEST(LeavingSurveyPanelTest, NothingTickedAndNoCommentIsAnEmptyAnswer) {
    LeavingSurveyPanel panel(LeavingSurveyPanel::Kind::cancel);
    const auto answer = panel.currentAnswer();
    EXPECT_TRUE(answer.reasons.empty());
    EXPECT_TRUE(answer.comment.isEmpty());
}

TEST(LeavingSurveyPanelTest, CommentIsCappedAt2000Characters) {
    LeavingSurveyPanel panel(LeavingSurveyPanel::Kind::cancel);
    panel.getCommentEditorForTest().setText(juce::String::repeatedString("a", 2600), false);
    EXPECT_LE(panel.currentAnswer().comment.length(), 2000);
}

TEST(LeavingSurveyPanelTest, ContinueHandsOverTheAnswerAndSkipDoesNot) {
    LeavingSurveyPanel panel(LeavingSurveyPanel::Kind::cancel);
    int continued = 0;
    int skipped = 0;
    LeavingSurveyPanel::Answer got;
    panel.onContinue = [&](const LeavingSurveyPanel::Answer& a) {
        ++continued;
        got = a;
    };
    panel.onSkip = [&] { ++skipped; };

    panel.getReasonToggleForTest(1).setToggleState(true, juce::dontSendNotification);
    ASSERT_TRUE(account_test::realClick(panel.getSkipButtonForTest()));
    EXPECT_EQ(skipped, 1);
    EXPECT_EQ(continued, 0);

    ASSERT_TRUE(account_test::realClick(panel.getContinueButtonForTest()));
    EXPECT_EQ(continued, 1);
    ASSERT_EQ(got.reasons.size(), 1u);
    EXPECT_EQ(got.reasons[0], juce::String("unused"));
}

TEST(LeavingSurveyPanelTest, EveryControlHasANameAndATooltip) {
    LeavingSurveyPanel panel(LeavingSurveyPanel::Kind::cancel);
    for (int i = 0; i < 8; ++i) {
        EXPECT_TRUE(panel.getReasonToggleForTest(i).getTitle().isNotEmpty());
        EXPECT_TRUE(panel.getReasonToggleForTest(i).getTooltip().isNotEmpty());
    }
    EXPECT_TRUE(panel.getCommentEditorForTest().getTitle().isNotEmpty());
    EXPECT_TRUE(panel.getCommentEditorForTest().getTooltip().isNotEmpty());
    EXPECT_EQ(panel.getSkipButtonForTest().getTitle(), juce::String("Skip"));
    EXPECT_TRUE(panel.getSkipButtonForTest().getTooltip().isNotEmpty());
    EXPECT_EQ(panel.getContinueButtonForTest().getTitle(), juce::String("Continue"));
    EXPECT_TRUE(panel.getContinueButtonForTest().getTooltip().isNotEmpty());
}

TEST(LeavingSurveyPanelTest, TabWalksTheTogglesThenTheCommentThenSkipAndContinue) {
    LeavingSurveyPanel panel(LeavingSurveyPanel::Kind::cancel);
    juce::KeyboardFocusTraverser traverser;
    juce::StringArray names;
    juce::Component* current = &panel.getReasonToggleForTest(0);
    for (int guard = 0; current != nullptr && guard < 20; ++guard) {
        names.add(current->getTitle());
        current = traverser.getNextComponent(current);
        if (current == &panel.getReasonToggleForTest(0))
            break;
    }
    ASSERT_GE(names.size(), 11);
    EXPECT_EQ(names[0], juce::String("It costs too much"));
    EXPECT_EQ(names[7], juce::String("Something else"));
    EXPECT_EQ(names[8], juce::String("Anything else you'd like to tell us? (optional)"));
    EXPECT_EQ(names[9], juce::String("Skip"));
    EXPECT_EQ(names[10], juce::String("Continue"));
}
