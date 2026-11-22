// AIChatComponentFoldTests.cpp -- chat message text: long messages fold to six lines behind a "Show more" link
// instead of being cut off with an ellipsis (docs/ai/chat-component.md, "Long messages fold"); and the edit-plan card
// re-colouring on a live theme switch and reading in every built-in theme.
#include "AIChatComponentTestFixture.h"
#include "PlanRig.h"
#include "UI/Assistant/AIChatComponent/SelectableChatText.h"
#include "UI/Layout/FocusRing.h"
#include "UI/Layout/FocusStepWithin.h"
#include "UI/Layout/TextLinkButton.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Theme/BuiltInThemes.h"
#include "UI/Theme/ReadableText.h"

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

    // The view holding the message text; its parent is the clip that decides how much of it shows.
    synth::SelectableChatText* label() {
        auto* bubble = errorBubble();
        if (bubble == nullptr)
            return nullptr;
        for (auto* child : bubble->getChildren())
            if (child->getNumChildComponents() == 1)
                if (auto* l = dynamic_cast<synth::SelectableChatText*>(child->getChildComponent(0)))
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

int wrappedHeight(const synth::SelectableChatText& label) {
    juce::GlyphArrangement ga;
    ga.addJustifiedText(label.getFont(), label.getText(), 0.0f, 0.0f, (float)label.getWidth(),
                        juce::Justification::left);
    return (int)std::ceil(ga.getBoundingBox(0, -1, true).getHeight());
}

// A headless Linux runner has no system clipboard: a copy reads back empty, so the copy tests skip there.
bool clipboardRoundTrips() {
    juce::SystemClipboard::copyTextToClipboard("probe");
    return juce::SystemClipboard::getTextFromClipboard() == "probe";
}

struct SelectRig {
    explicit SelectRig(const juce::String& errorText) {
        service.setProvider(std::make_unique<ErrorProvider>(synth::AIProvider::AIErrorKind::Network, errorText));
        juce::PropertiesFile::Options options;
        options.applicationName = "AIChatSelectTest";
        options.filenameSuffix = "test";
        options.storageFormat = juce::PropertiesFile::storeAsXML;
        props.setStorageParameters(options);
        chat = std::make_unique<synth::AIChatComponent>(service, props);
        chat->setLocalHistoryDirectoryForTesting(juce::File::getSpecialLocation(juce::File::tempDirectory)
                                                     .getChildFile("chat-select-" + juce::Uuid().toString()));
        chat->setSize(400, 600);
        chat->setVisible(true); // the chat's own hit test only finds children of a visible component
        for (auto* child : chat->getChildren())
            if (auto* editor = dynamic_cast<juce::TextEditor*>(child))
                editor->setText("hi");
        chat->triggerSend();
        juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
    }

    // The text view of the bubble whose title starts with `prefix`.
    synth::SelectableChatText* view(const juce::String& prefix) {
        for (auto* bubble : findMessageList(*chat)->getChildren())
            if (bubble->getTitle().startsWith(prefix))
                for (auto* child : bubble->getChildren())
                    if (child->getNumChildComponents() == 1)
                        if (auto* v = dynamic_cast<synth::SelectableChatText*>(child->getChildComponent(0)))
                            return v;
        return nullptr;
    }

    // Press at (x0, y) and release at (x1, y), both in the view's own coordinates, on whatever the chat finds there.
    void dragAcross(synth::SelectableChatText& v, float x0, float x1, float y) {
        const auto source = juce::Desktop::getInstance().getMainMouseSource();
        const juce::ModifierKeys left(juce::ModifierKeys::leftButtonModifier);
        auto send = [&](juce::Point<float> local, bool dragged, juce::Point<float> down) {
            const auto inChat = chat->getLocalPoint(&v, local.roundToInt());
            auto* target = chat->getComponentAt(inChat);
            ASSERT_NE(target, nullptr);
            const auto pos = target->getLocalPoint(&v, local);
            const auto downPos = target->getLocalPoint(&v, down);
            juce::MouseEvent e(source, pos, left, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, target, target,
                               juce::Time::getCurrentTime(), downPos, juce::Time::getCurrentTime(), 1, dragged);
            if (!dragged && !pressed)
                target->mouseDown(e), pressed = true;
            else if (dragged)
                target->mouseDrag(e);
            else
                target->mouseUp(e), pressed = false;
        };
        const juce::Point<float> a(x0, y), b(x1, y);
        send(a, false, a);
        send(b, true, a);
        send(b, false, a);
    }

    bool pressed = false;
    AudioEngine engine;
    synth::AIIntegrationService service{engine.getGraph()};
    juce::ApplicationProperties props;
    std::unique_ptr<synth::AIChatComponent> chat;
};

bool pressCmd(juce::Component& c, juce::juce_wchar key) {
    return c.keyPressed(juce::KeyPress(key, juce::ModifierKeys::commandModifier, 0));
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
    EXPECT_EQ(label->getBorder().getTopAndBottom(), 0);
    EXPECT_EQ(label->getBorder().getLeftAndRight(), 0);
    // Folded or not, the whole text is at most one click away.
    if (rig.link() != nullptr)
        rig.link()->onClick();
    EXPECT_GE(rig.label()->getParentComponent()->getHeight(), wrappedHeight(*rig.label()));
}

// Selecting and copying message text (docs/ai/chat-component.md, "Selecting and copying message text"). The mouse
// tests send the press and drag to whatever component the chat's own hit test finds under the pointer, so a bubble that
// swallowed the click would fail.
TEST_F(AIChatComponentTest, DraggingAcrossAMessageSelectsItsTextAndCmdCCopiesIt) {
    SelectRig rig("The server did not answer in time.");
    auto* v = rig.view("Assistant: Error");
    ASSERT_NE(v, nullptr);

    rig.dragAcross(*v, 1.0f, (float)v->getWidth() - 1.0f, 4.0f);
    const auto selected = v->getHighlightedText();
    EXPECT_FALSE(selected.isEmpty());
    EXPECT_TRUE(v->getText().contains(selected));

    if (!clipboardRoundTrips())
        GTEST_SKIP() << "no system clipboard on this runner";
    juce::SystemClipboard::copyTextToClipboard("untouched");
    EXPECT_TRUE(pressCmd(*v, 'c'));
    EXPECT_EQ(juce::SystemClipboard::getTextFromClipboard(), selected);
}

TEST_F(AIChatComponentTest, CmdASelectsTheWholeMessageAndCmdCWithoutASelectionCopiesIt) {
    SelectRig rig(numberedLines(20));
    auto* v = rig.view("Assistant: Error");
    ASSERT_NE(v, nullptr);

    EXPECT_TRUE(pressCmd(*v, 'a'));
    EXPECT_EQ(v->getHighlightedText(), v->getText());

    v->setHighlightedRegion({0, 0});
    if (!clipboardRoundTrips())
        GTEST_SKIP() << "no system clipboard on this runner";
    juce::SystemClipboard::copyTextToClipboard("untouched");
    EXPECT_TRUE(pressCmd(*v, 'c'));
    EXPECT_EQ(juce::SystemClipboard::getTextFromClipboard(), v->getText());
    EXPECT_TRUE(v->getText().contains("line 20"));
}

TEST_F(AIChatComponentTest, TheTextViewIsAReadOnlyNamedTabStopWithACopyMenu) {
    SelectRig rig("Something failed.");
    auto* v = rig.view("Assistant: Error");
    ASSERT_NE(v, nullptr);
    EXPECT_TRUE(v->isReadOnly());
    EXPECT_TRUE(v->getWantsKeyboardFocus());
    EXPECT_TRUE(v->isPopupMenuEnabled());
    EXPECT_EQ(v->getTitle(), "Message text");
    EXPECT_FALSE(v->getTooltip().isEmpty());
    const auto before = v->getText();
    juce::SystemClipboard::copyTextToClipboard("pasted");
    pressCmd(*v, 'v');
    EXPECT_EQ(v->getText(), before) << "a message is never edited";
}

TEST_F(AIChatComponentTest, SelectingKeepsTheFoldLinkAndTheWholeTextWorking) {
    SelectRig rig(numberedLines(20));
    auto* v = rig.view("Assistant: Error");
    ASSERT_NE(v, nullptr);
    rig.dragAcross(*v, 1.0f, 60.0f, 4.0f);
    EXPECT_FALSE(v->getHighlightedText().isEmpty());

    synth::ui::TextLinkButton* link = nullptr;
    for (auto* bubble : findMessageList(*rig.chat)->getChildren())
        if (bubble->getTitle().startsWith("Assistant: Error"))
            for (auto* child : bubble->getChildren())
                if (auto* l = dynamic_cast<synth::ui::TextLinkButton*>(child))
                    link = l;
    ASSERT_NE(link, nullptr);
    link->onClick();
    EXPECT_EQ(link->getButtonText(), "Show less");
    EXPECT_TRUE(v->getText().contains("line 20"));
}

TEST_F(AIChatComponentTest, TheMessageTextFollowsTheThemeWhenItChanges) {
    SelectRig rig("Something failed.");
    auto* v = rig.view("Assistant: Error");
    ASSERT_NE(v, nullptr);
    synth::theme::AppLookAndFeel lf;
    lf.applyTheme(synth::theme::makeDaylight());
    rig.chat->setLookAndFeel(&lf);
    EXPECT_EQ(v->findColour(juce::TextEditor::textColourId), synth::theme::makeDaylight().colors.textPrimary);
    lf.applyTheme(synth::theme::makeObsidian());
    rig.chat->sendLookAndFeelChange();
    EXPECT_EQ(v->findColour(juce::TextEditor::textColourId), synth::theme::makeObsidian().colors.textPrimary);
    rig.chat->setLookAndFeel(nullptr);
}

// ---- The edit-plan card follows the theme -------------------------------------------------------------------------

using namespace plan_rig;

namespace plan_card_theme {

using synth::theme::contrastBetween;
constexpr double kPlanCardTextContrast = synth::theme::kReadableTextContrast;

// What the card's text sits on: a faint tint of the bubble colour over the panel, so the worst case is the panel
// surfaces themselves (the same set the chat palette is held to).
std::vector<juce::Colour> planCardSurfaces(const synth::theme::Colors& c) { return {c.bg0, c.bg1, c.surface}; }

struct PlanCardParts {
    juce::Component* card = nullptr;
    juce::Label* header = nullptr;
    juce::Label* preview = nullptr;
    juce::Button* apply = nullptr;
    juce::Button* up = nullptr;
    juce::Button* down = nullptr;
};

PlanCardParts planCardPartsOf(PlanRig& rig) {
    PlanCardParts parts;
    const auto cards = rig.findTitled("Edit plan");
    if (cards.empty())
        return parts;
    parts.card = cards.front();
    for (auto* child : parts.card->getChildren())
        if (auto* label = dynamic_cast<juce::Label*>(child)) {
            if (label->getText() == "Edit plan")
                parts.header = label;
            else if (label->getTitle() == "What this plan changes")
                parts.preview = label;
        }
    parts.apply = rig.applyButton();
    parts.up = findButtonTitled(rig, "This answer was helpful");
    parts.down = findButtonTitled(rig, "This answer missed the mark");
    return parts;
}

} // namespace plan_card_theme

using namespace plan_card_theme;

// Regression test for the plan card keeping its old theme colours after a live switch: the colours are set values, so
// the card must re-apply them when the look and feel changes.
TEST_F(AIChatComponentTest, ThePlanCardRecoloursWhenTheThemeSwitchesLive) {
    synth::theme::AppLookAndFeel lf;
    lf.applyTheme(synth::theme::makeObsidian());
    PlanRig rig;
    rig.chat->setLookAndFeel(&lf);
    rig.provider->answer = kPatchOnly;
    rig.send("make me a simple oscillator patch");
    auto parts = planCardPartsOf(rig);
    ASSERT_NE(parts.card, nullptr);
    ASSERT_NE(parts.preview, nullptr);
    ASSERT_NE(parts.header, nullptr);
    ASSERT_NE(parts.apply, nullptr);
    const auto obsidian = synth::theme::makeObsidian().colors;
    EXPECT_EQ(parts.preview->findColour(juce::Label::textColourId), obsidian.textPrimary);

    lf.applyTheme(synth::theme::makeDaylight());
    rig.chat->sendLookAndFeelChange();
    const auto daylight = synth::theme::makeDaylight().colors;
    EXPECT_EQ(parts.preview->findColour(juce::Label::textColourId), daylight.textPrimary);
    EXPECT_EQ(parts.header->findColour(juce::Label::textColourId),
              synth::theme::nudgeToReadable(daylight.accent2, planCardSurfaces(daylight)));
    EXPECT_EQ(parts.apply->findColour(juce::TextButton::buttonColourId), daylight.accent);
    EXPECT_EQ(parts.apply->findColour(juce::TextButton::textColourOffId),
              synth::theme::readableTextOn(daylight.accent, daylight.textPrimary));
    EXPECT_EQ(parts.up->findColour(juce::TextButton::buttonColourId), daylight.surfaceHi);
    for (const auto& surface : planCardSurfaces(daylight))
        EXPECT_GE(contrastBetween(parts.preview->findColour(juce::Label::textColourId), surface),
                  kPlanCardTextContrast);
    rig.chat->setLookAndFeel(nullptr);
}

// Every built-in theme: the card's text over its surfaces, Apply's text over its fill, in both its states.
TEST_F(AIChatComponentTest, ThePlanCardReadsInEveryBuiltInTheme) {
    for (const auto& theme : synth::theme::builtInThemes()) {
        SCOPED_TRACE(theme.id.toStdString());
        synth::theme::AppLookAndFeel lf;
        lf.applyTheme(theme);
        PlanRig rig;
        rig.chat->setLookAndFeel(&lf);
        rig.provider->answer = kPatchOnly;
        rig.send("make me a simple oscillator patch");
        auto parts = planCardPartsOf(rig);
        ASSERT_NE(parts.card, nullptr);
        ASSERT_NE(parts.apply, nullptr);
        ASSERT_NE(parts.up, nullptr);
        ASSERT_NE(parts.down, nullptr);
        const auto& c = theme.colors;
        for (const auto& surface : planCardSurfaces(c)) {
            EXPECT_GE(contrastBetween(parts.preview->findColour(juce::Label::textColourId), surface),
                      kPlanCardTextContrast)
                << "preview text";
            EXPECT_GE(contrastBetween(parts.header->findColour(juce::Label::textColourId), surface),
                      kPlanCardTextContrast)
                << "header";
        }
        EXPECT_GE(contrastBetween(parts.apply->findColour(juce::TextButton::textColourOffId),
                                  parts.apply->findColour(juce::TextButton::buttonColourId)),
                  kPlanCardTextContrast)
            << "Apply";
        for (auto* thumb : {parts.up, parts.down})
            EXPECT_GE(contrastBetween(thumb->findColour(juce::TextButton::textColourOffId),
                                      thumb->findColour(juce::TextButton::buttonColourId)),
                      kPlanCardTextContrast)
                << thumb->getTitle().toStdString();

        // Applied: the button turns to muted text on the raised surface.
        parts.apply->onClick();
        parts = planCardPartsOf(rig);
        if (parts.apply != nullptr && !parts.apply->isEnabled())
            EXPECT_GE(contrastBetween(parts.apply->findColour(juce::TextButton::textColourOffId),
                                      parts.apply->findColour(juce::TextButton::buttonColourId)),
                      kPlanCardTextContrast)
                << "Applied";
        rig.chat->setLookAndFeel(nullptr);
    }
}

// Tab and Down both go from Apply to Save as preset; its ring stands inside the button, the button inside the card.
TEST_F(AIChatComponentTest, SaveAsPresetIsReachedByDownFromApplyAndItsFocusRingStaysInsideTheButton) {
    synth::theme::AppLookAndFeel lf;
    lf.applyTheme(synth::theme::makeObsidian());
    PlanRig rig;
    rig.chat->setLookAndFeel(&lf);
    rig.provider->answer = kPatchOnly;
    rig.send("make me a simple oscillator patch");
    auto* save = findButtonTitled(rig, "Save patch as preset");
    auto* apply = rig.applyButton();
    ASSERT_NE(save, nullptr);
    ASSERT_NE(apply, nullptr);

    EXPECT_EQ(synth::ui::neighbourStop(*rig.chat, apply, +1), save) << "Down from Apply";
    EXPECT_EQ(synth::ui::neighbourStop(*rig.chat, save, -1), apply) << "Up from Save as preset";

    juce::Component* card = save->getParentComponent();
    ASSERT_NE(card, nullptr);
    EXPECT_TRUE(card->getLocalBounds().contains(save->getBounds())) << "the button sits inside the card";

    synth::ui::setFocusRingsShownForTest(true);
    const auto image = save->createComponentSnapshot(save->getLocalBounds());
    synth::ui::setFocusRingsShownForTest(false);
    const auto accent = synth::theme::makeObsidian().colors.accent;
    auto isRing = [&](int x, int y) {
        const auto px = image.getPixelAt(x, y);
        return px.getAlpha() > 200 && std::abs(px.getRed() - accent.getRed()) < 24 &&
               std::abs(px.getGreen() - accent.getGreen()) < 24 && std::abs(px.getBlue() - accent.getBlue()) < 24;
    };
    const int band = (int)std::ceil(synth::ui::focusRingThickness(synth::theme::makeObsidian())) + 1;
    const int midX = image.getWidth() / 2, midY = image.getHeight() / 2;
    auto ringWithin = [&](bool horizontalEdge, bool farSide) {
        for (int d = 0; d <= band; ++d) {
            const int x = horizontalEdge ? midX : (farSide ? image.getWidth() - 1 - d : d);
            const int y = horizontalEdge ? (farSide ? image.getHeight() - 1 - d : d) : midY;
            if (isRing(x, y))
                return true;
        }
        return false;
    };
    EXPECT_TRUE(ringWithin(true, false)) << "ring at the top edge, not clipped";
    EXPECT_TRUE(ringWithin(true, true)) << "ring at the bottom edge";
    EXPECT_TRUE(ringWithin(false, false)) << "ring at the left edge";
    EXPECT_TRUE(ringWithin(false, true)) << "ring at the right edge";
    rig.chat->setLookAndFeel(nullptr);
}
