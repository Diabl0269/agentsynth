// AIChatComponentFoldTests.cpp -- long chat messages fold to six lines behind a "Show more" link
// instead of being cut off with an ellipsis (docs/ai/chat-component.md, "Long messages fold").
#include "AIChatComponentTestFixture.h"
#include "UI/Layout/TextLinkButton.h"

namespace {

juce::String numberedLines(int count) {
    juce::StringArray lines;
    for (int i = 1; i <= count; ++i)
        lines.add("line " + juce::String(i));
    return lines.joinIntoString("\n");
}

// A chat whose provider fails every send with `errorText`, so the failure bubble is the message under test.
struct FoldRig {
    explicit FoldRig(const juce::String& errorText) {
        service.setProvider(std::make_unique<ErrorProvider>(synth::AIProvider::AIErrorKind::Network, errorText));
        juce::PropertiesFile::Options options;
        options.applicationName = "AIChatFoldTest";
        options.filenameSuffix = "test";
        options.storageFormat = juce::PropertiesFile::storeAsXML;
        props.setStorageParameters(options);
        chat = std::make_unique<synth::AIChatComponent>(service, props);
        chat->setLocalHistoryDirectoryForTesting(juce::File::getSpecialLocation(juce::File::tempDirectory)
                                                     .getChildFile("chat-fold-" + juce::Uuid().toString()));
        chat->setSize(400, 600);
    }

    void send(const juce::String& text) {
        for (auto* child : chat->getChildren())
            if (auto* editor = dynamic_cast<juce::TextEditor*>(child))
                editor->setText(text);
        chat->triggerSend();
        juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
    }

    juce::Component* list() { return findMessageList(*chat); }

    // The assistant error bubble: the list item whose title starts "Assistant: Error".
    juce::Component* errorBubble() {
        for (auto* child : list()->getChildren())
            if (child->getTitle().startsWith("Assistant: Error"))
                return child;
        return nullptr;
    }

    synth::ui::TextLinkButton* link() {
        auto* bubble = errorBubble();
        if (bubble == nullptr)
            return nullptr;
        for (auto* child : bubble->getChildren())
            if (auto* l = dynamic_cast<synth::ui::TextLinkButton*>(child))
                return l->isVisible() ? l : nullptr;
        return nullptr;
    }

    // The label holding the message text; its parent is the clip that decides how much of it shows.
    juce::Label* label() {
        auto* bubble = errorBubble();
        if (bubble == nullptr)
            return nullptr;
        for (auto* child : bubble->getChildren())
            if (child->getNumChildComponents() == 1)
                if (auto* l = dynamic_cast<juce::Label*>(child->getChildComponent(0)))
                    return l;
        return nullptr;
    }

    int visibleLines() {
        auto* l = label();
        return (int)std::lround((float)l->getParentComponent()->getHeight() / l->getFont().getHeight());
    }

    AudioEngine engine;
    synth::AIIntegrationService service{engine.getGraph()};
    juce::ApplicationProperties props;
    std::unique_ptr<synth::AIChatComponent> chat;
};

int wrappedHeight(const juce::Label& label) {
    juce::GlyphArrangement ga;
    ga.addJustifiedText(label.getFont(), label.getText(), 0.0f, 0.0f, (float)label.getWidth(),
                        juce::Justification::left);
    return (int)std::ceil(ga.getBoundingBox(0, -1, true).getHeight());
}

} // namespace

TEST_F(AIChatComponentTest, ShortMessageHasNoFoldLink) {
    FoldRig rig(numberedLines(3));
    rig.send("hi");
    ASSERT_NE(rig.errorBubble(), nullptr);
    EXPECT_EQ(rig.link(), nullptr);
    ASSERT_NE(rig.label(), nullptr);
    // The clip is exactly as tall as the text: no extra space for a link that is not there.
    EXPECT_EQ(rig.label()->getParentComponent()->getHeight(),
              synth::AIChatComponent::computeWrappedTextHeight(rig.label()->getFont(), rig.label()->getText(),
                                                               rig.label()->getWidth()));
}

TEST_F(AIChatComponentTest, LongMessageFoldsToSixLinesAndUnfoldsOnClick) {
    FoldRig rig(numberedLines(20));
    rig.send("hi");

    auto* link = rig.link();
    ASSERT_NE(link, nullptr);
    EXPECT_EQ(link->getButtonText(), "Show more");
    EXPECT_EQ(rig.visibleLines(), 6);
    // The label itself keeps the whole text; the clip is what hides lines 7 to 20.
    EXPECT_TRUE(rig.label()->getText().contains("line 20"));

    link->onClick();
    link = rig.link();
    ASSERT_NE(link, nullptr);
    EXPECT_EQ(link->getButtonText(), "Show less");
    EXPECT_EQ(rig.visibleLines(), 20);

    link->onClick();
    EXPECT_EQ(rig.link()->getButtonText(), "Show more");
    EXPECT_EQ(rig.visibleLines(), 6);
}

TEST_F(AIChatComponentTest, FoldStateSurvivesTheListBeingRebuilt) {
    FoldRig rig(numberedLines(20));
    rig.send("hi");
    rig.link()->onClick();

    rig.send("again"); // a second exchange rebuilds every bubble
    ASSERT_NE(rig.link(), nullptr);
    EXPECT_EQ(rig.link()->getButtonText(), "Show less");
    EXPECT_EQ(rig.visibleLines(), 20);
}

TEST_F(AIChatComponentTest, FoldLinkIsAKeyboardReachableNamedControl) {
    FoldRig rig(numberedLines(20));
    rig.send("hi");
    auto* link = rig.link();
    ASSERT_NE(link, nullptr);
    EXPECT_TRUE(link->getWantsKeyboardFocus());
    EXPECT_EQ(link->getTitle(), "Show the whole message");
    EXPECT_EQ(link->getTooltip(), "Show the whole message");

    link->onClick();
    EXPECT_EQ(rig.link()->getTitle(), "Show less of this message");
    EXPECT_EQ(rig.link()->getTooltip(), "Show less of this message");
}

TEST_F(AIChatComponentTest, ScreenReaderHearsTheWholeFoldedMessage) {
    FoldRig rig(numberedLines(20));
    rig.send("hi");
    ASSERT_NE(rig.link(), nullptr);
    EXPECT_EQ(rig.link()->getButtonText(), "Show more"); // folded
    const auto title = rig.errorBubble()->getTitle();
    EXPECT_TRUE(title.contains("line 1\n"));
    EXPECT_TRUE(title.contains("line 20"));
}

// RemoteProvider's HTTP failures already start with "Error: "; the bubble must not say it twice.
TEST_F(AIChatComponentTest, AnErrorThatAlreadySaysErrorIsNotPrefixedTwice) {
    const juce::String error = "Error: Remote provider at http://localhost:8787 failed the request (HTTP 502).";
    FoldRig rig(error);
    rig.send("hi");
    auto* label = rig.label();
    ASSERT_NE(label, nullptr);
    EXPECT_EQ(label->getText(), error);
}

TEST_F(AIChatComponentTest, FailedSendNeverClipsALongErrorWithAnEllipsis) {
    const juce::String error =
        "Could not reach the Remote provider at http://localhost:11434/v1/chat/completions. "
        "Check that the server is running, that the address is right and that no firewall blocks it, "
        "then try again. The model did not respond within the time allowed and the connection was closed "
        "by the remote end before a complete answer arrived.";
    FoldRig rig(error);
    rig.send("hi");

    auto* label = rig.label();
    ASSERT_NE(label, nullptr);
    // Everything is in the label (the clip, not the label, hides lines when folded) ...
    EXPECT_EQ(label->getText(), "Error: " + error);
    // ... which has room for every line it wraps to and no border to narrow it, so it never ellipsizes.
    EXPECT_GE(label->getHeight(), wrappedHeight(*label));
    EXPECT_EQ(label->getBorderSize().getTopAndBottom(), 0);
    EXPECT_EQ(label->getBorderSize().getLeftAndRight(), 0);
    // Folded or not, the whole text is at most one click away.
    if (rig.link() != nullptr)
        rig.link()->onClick();
    EXPECT_GE(rig.label()->getParentComponent()->getHeight(), wrappedHeight(*rig.label()));
}
