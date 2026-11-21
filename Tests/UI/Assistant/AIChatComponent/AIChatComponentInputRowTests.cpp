// AIChatComponentInputRowTests.cpp
// The request field wraps its text and the input row follows it: 1 to 5 lines, then the box scrolls; Enter sends,
// Shift+Enter is a new line; the height eases (or lands at once under Animations Off) and returns after a send.

#include "../../../TestWait.h"
#include "AIChatComponentTestFixture.h"
#include "UI/Layout/FadeVisibility.h"

namespace {
struct ChatRig {
    AudioEngine engine;
    synth::AIIntegrationService service{engine.getGraph()};
    juce::ApplicationProperties props;
    std::unique_ptr<synth::AIChatComponent> chat;

    ChatRig() {
        service.setProvider(std::make_unique<MockChatProvider>());
        juce::PropertiesFile::Options options;
        options.applicationName = "Test";
        options.filenameSuffix = "test";
        options.storageFormat = juce::PropertiesFile::storeAsXML;
        props.setStorageParameters(options);
        chat = std::make_unique<synth::AIChatComponent>(service, props);
        chat->setSize(400, 700);
    }

    juce::TextEditor& field() { return chat->getInputFieldForTesting(); }
    int rowHeight() const { return chat->getInputRowHeightForTesting(); }

    // setText posts its change message; the panel reacts to that, so pump until the row has settled.
    void type(const juce::String& text) {
        field().setText(text);
        synth::test::flushMessageQueue();
    }
};

juce::String lines(int n) {
    juce::StringArray a;
    for (int i = 0; i < n; ++i)
        a.add("line " + juce::String(i + 1));
    return a.joinIntoString("\n");
}
} // namespace

TEST_F(AIChatComponentTest, InputRowIsFortyPixelsForOneLine) {
    ChatRig rig;
    EXPECT_EQ(rig.rowHeight(), 40);
    rig.type("short");
    EXPECT_EQ(rig.rowHeight(), 40);
    EXPECT_EQ(rig.field().getHeight(), 40);
    EXPECT_TRUE(rig.field().isMultiLine());
}

TEST_F(AIChatComponentTest, LongTextWrapsAndGrowsTheRow) {
    ChatRig rig;
    const int yOneLine = rig.field().getY();
    rig.type("word word word word word word word word word word word word word word word word word word word word "
             "word word word word word word word word word word word word word word word word word word word word");
    EXPECT_GT(rig.rowHeight(), 40);
    EXPECT_EQ(rig.field().getHeight(), rig.rowHeight());
    // The room comes out of the message list above, which ends where the row begins.
    EXPECT_EQ(yOneLine - rig.field().getY(), rig.rowHeight() - 40);
    rig.type(lines(2));
    const int two = rig.rowHeight();
    rig.type(lines(3));
    const int three = rig.rowHeight();
    EXPECT_GT(two, 40);
    EXPECT_GT(three, two);
    EXPECT_EQ(three - two, two - 40);
    std::cout << "ROWPX 1=40 2=" << two << " 3=" << three << std::endl;
}

TEST_F(AIChatComponentTest, InputRowStopsGrowingAtFiveLines) {
    ChatRig rig;
    rig.type(lines(5));
    const int five = rig.rowHeight();
    rig.type(lines(9));
    EXPECT_EQ(rig.rowHeight(), five);
    EXPECT_GT(rig.field().getTextHeight(), rig.field().getHeight()); // more text than the box shows: it scrolls
}

TEST_F(AIChatComponentTest, SendKeepsItsSizeAtTheBottomOfAGrownRow) {
    ChatRig rig;
    rig.type(lines(3));
    ASSERT_GT(rig.rowHeight(), 40);
    for (auto* child : rig.chat->getChildren())
        if (auto* b = dynamic_cast<juce::TextButton*>(child); b != nullptr && b->getButtonText() == "Send") {
            EXPECT_EQ(b->getHeight(), 40);
            EXPECT_EQ(b->getBottom(), rig.field().getBottom());
        }
}

TEST_F(AIChatComponentTest, RowReturnsToOneLineAfterSend) {
    ChatRig rig;
    rig.type(lines(4));
    ASSERT_GT(rig.rowHeight(), 40);
    rig.chat->triggerSend();
    EXPECT_TRUE(rig.field().getText().isEmpty());
    EXPECT_EQ(rig.rowHeight(), 40);
}

TEST_F(AIChatComponentTest, EnterSendsMultiLineText) {
    ChatRig rig;
    rig.type("first line\nsecond line");
    const auto before = rig.chat->getMessageCountForTesting();
    EXPECT_TRUE(rig.chat->pressKeyInInputForTesting(juce::KeyPress(juce::KeyPress::returnKey)));
    synth::test::flushMessageQueue(); // the editor reports Return through a posted message
    EXPECT_GT(rig.chat->getMessageCountForTesting(), before);
    EXPECT_EQ(rig.chat->getMessageTextForTesting(before), "first line\nsecond line");
    EXPECT_TRUE(rig.field().getText().isEmpty());
}

TEST_F(AIChatComponentTest, ShiftEnterInsertsANewLine) {
    ChatRig rig;
    rig.type("abc");
    rig.field().setCaretPosition(3);
    const auto before = rig.chat->getMessageCountForTesting();
    EXPECT_TRUE(rig.chat->pressKeyInInputForTesting(
        juce::KeyPress(juce::KeyPress::returnKey, juce::ModifierKeys::shiftModifier, 0)));
    EXPECT_EQ(rig.chat->getMessageCountForTesting(), before);
    EXPECT_EQ(rig.field().getText(), "abc\n");
    synth::test::flushMessageQueue();
    EXPECT_GT(rig.rowHeight(), 40);
}

TEST_F(AIChatComponentTest, RowHeightEasesWhenOnScreen) {
    ChatRig rig;
    synth::ui::FadeVisibility::setAnimateOffScreenForTest(true);
    rig.type(lines(3));
    const int mid0 = rig.rowHeight();
    EXPECT_EQ(mid0, 40); // the ease has not started its first frame yet
    rig.chat->stepInputRowEaseForTesting(0.0);
    rig.chat->stepInputRowEaseForTesting(40.0);
    const int mid = rig.rowHeight();
    rig.chat->stepInputRowEaseForTesting(1000.0);
    const int end = rig.rowHeight();
    synth::ui::FadeVisibility::setAnimateOffScreenForTest(false);
    EXPECT_GT(mid, 40);
    EXPECT_LT(mid, end);
    EXPECT_GT(end, 40);
}

TEST_F(AIChatComponentTest, InputKeepsItsTitleAndTooltipNamesShiftEnter) {
    ChatRig rig;
    EXPECT_EQ(rig.field().getTitle(), "Message to the AI assistant");
    EXPECT_TRUE(rig.field().getTooltip().contains("Shift+Enter"));
}
