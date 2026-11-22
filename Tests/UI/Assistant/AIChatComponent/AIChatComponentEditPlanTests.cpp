// AIChatComponentEditPlanTests.cpp
// One input, one answer, one card: no Patch/Arrange selector; with a timeline a send asks for an
// edit plan through sendProjectMessage (hosted project.generate, local with the combined schema);
// a plan renders one card with one Apply that runs applyProjectEdit; a refused plan shows its reason
// and no Apply; a patch-only answer is the same card; a local conversational answer stays text; and
// the Apply button's name, tooltip and Tab position. Shared mocks live in AIChatComponentTestFixture.h.

#include "../../../TestWait.h"
#include "../../Accessibility/TabOrderHelpers.h"
#include "../../Layout/FadeVisibilityTestGuard.h"
#include "AI/PlanPreset.h"
#include "AIChatComponentTestFixture.h"
#include "PlanRig.h"
#include "TestPaths.h"
#include "UI/Assistant/AIChatComponent/EditPlanDetailsText.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"

using namespace plan_rig;

TEST_F(AIChatComponentTest, ThereIsNoRequestModeSelector) {
    PlanRig rig;
    int combos = 0;
    for (auto* child : rig.chat->getChildren())
        if (dynamic_cast<juce::ComboBox*>(child) != nullptr) {
            ++combos;
            EXPECT_EQ(child->getTitle(), "AI model") << "the model picker is the only combo box left";
        }
    EXPECT_EQ(combos, 1);
}

TEST_F(AIChatComponentTest, HostedSendWithATimelineAsksProjectGenerate) {
    PlanRig rig(/*hosted=*/true);
    rig.send("write a bass line with a filter sweep");
    EXPECT_EQ(rig.provider->capabilityCalls, 1);
    EXPECT_EQ(rig.provider->lastCapability, juce::String("project.generate"));
    EXPECT_EQ(rig.provider->sendPromptCalls, 0);
}

TEST_F(AIChatComponentTest, LocalSendWithATimelineCarriesTheCombinedSchema) {
    PlanRig rig(/*hosted=*/false);
    rig.send("add a bass track and automate its cutoff");
    ASSERT_EQ(rig.provider->sendPromptCalls, 1);
    EXPECT_EQ(rig.provider->capabilityCalls, 0);
    auto* properties = rig.provider->lastSchema.getProperty("properties", {}).getDynamicObject();
    ASSERT_NE(properties, nullptr) << "a structured request carries a schema";
    EXPECT_TRUE(properties->hasProperty("timelineOps"));
    EXPECT_TRUE(properties->hasProperty("nodes"));
    ASSERT_FALSE(rig.provider->lastConversation.empty());
    EXPECT_TRUE(rig.provider->lastConversation.back().content.contains("Project tracks:"))
        << "the local message is composed from the project.generate fields";
}

// Every local request gets the app's own small prompt; only a song request adds the song-layout rules and the worked
// song, and none gets the vendored production prompt.
TEST_F(AIChatComponentTest, LocalRequestsGetTheSmallAppPromptAndOnlyASongTheSongRules) {
    for (const char* request : {"add a bass track and automate its cutoff", "write a song about rain"}) {
        const bool song = juce::String(request).contains("song");
        PlanRig rig(/*hosted=*/false);
        rig.send(request);
        ASSERT_FALSE(rig.provider->lastConversation.empty()) << request;
        const juce::String system = rig.provider->lastConversation.front().content;
        EXPECT_TRUE(system.contains("addInstrumentTrack")) << request;
        EXPECT_EQ(system.contains("\"sections\""), song) << request;
        EXPECT_FALSE(system.contains("### Patch rules")) << request;
        EXPECT_FALSE(system.contains("### Full songs")) << request;
        EXPECT_LT(system.getNumBytesAsUTF8(), 24000) << request;
    }
}

TEST_F(AIChatComponentTest, APatchWithTimelineOpsRendersOneCardWithOneApply) {
    PlanRig rig;
    rig.provider->answer = kPlan;
    rig.send("a bass track with a wobbling filter");

    EXPECT_EQ(rig.findTitled("Edit plan").size(), 1u);
    EXPECT_EQ(rig.findTitled("Apply edit plan").size(), 1u);
    const auto preview = juce::StringArray::fromLines(rig.chat->getLastPlanPreviewForTesting());
    ASSERT_EQ(preview.size(), 2) << rig.chat->getLastPlanPreviewForTesting();
    EXPECT_TRUE(preview[0].startsWith("Merges a patch")) << preview[0];
    EXPECT_TRUE(preview[1].contains("Bass")) << preview[1];
    EXPECT_EQ(rig.host.builds, 0) << "rendering the card previews; it builds nothing";
}

// The card's preview is sized from the text at the label's full width; a label border would wrap
// it narrower than measured and clip the last line under the buttons.
TEST_F(AIChatComponentTest, ThePlanPreviewIsNeverClipped) {
    PlanRig rig;
    rig.provider->answer = kPlan;
    rig.send("a bass track with a wobbling filter");
    const auto found = rig.findTitled("What this plan changes");
    ASSERT_EQ(found.size(), 1u);
    auto* label = dynamic_cast<juce::Label*>(found.front());
    ASSERT_NE(label, nullptr);
    EXPECT_EQ(label->getBorderSize().getLeftAndRight(), 0);
    EXPECT_EQ(label->getBorderSize().getTopAndBottom(), 0);
    juce::GlyphArrangement ga;
    ga.addJustifiedText(label->getFont(), label->getText(), 0.0f, 0.0f, (float)label->getWidth(),
                        juce::Justification::left);
    EXPECT_GE(label->getHeight(), (int)std::floor(ga.getBoundingBox(0, -1, true).getHeight()));
}

// A plan with one bad detail still gets its card; the card says so in one short line, with every note in the
// tooltip and the accessible description, and a clean plan shows no such line.
TEST_F(AIChatComponentTest, APlanThatWasRepairedShowsOneShortLineWithATooltip) {
    PlanRig rig;
    rig.provider->answer = R"({"mode": "merge", "nodes": [], "connections": [], "timelineOps": [
        {"op": "addInstrumentTrack", "name": "Bass", "instrument": "Oscillator",
         "inserts": [{"type": "Filter", "id": 7002}]},
        {"op": "placeClips", "track": "Bass", "clips": [{"startBeat": 0, "lengthBeats": 4, "notes": [
            {"startBeat": 0, "lengthBeats": 1, "pitch": 36, "velocity": 100}]}]},
        {"op": "writeLane", "nodeId": 7002, "paramId": "wobble", "points": [{"beat": 0, "value": 1}]}]})";
    rig.send("a bass track");

    const auto found = rig.findTitled("Parts of this plan that were left out or adjusted");
    ASSERT_EQ(found.size(), 1u);
    auto* label = dynamic_cast<juce::Label*>(found.front());
    ASSERT_NE(label, nullptr);
    EXPECT_EQ(label->getText(), "1 detail skipped: an automation lane on a setting the Filter doesn't have");
    EXPECT_NE(label->getText(), label->getText().toUpperCase()) << "no ALL-CAPS text";
    EXPECT_TRUE(label->getTooltip().contains("setting the Filter doesn't have")) << label->getTooltip();
    EXPECT_TRUE(label->getDescription().contains("setting the Filter doesn't have"));
    EXPECT_NE(rig.applyButton(), nullptr) << "a repaired plan can still be applied";
}

TEST_F(AIChatComponentTest, ACleanPlanShowsNoRepairLine) {
    PlanRig rig;
    rig.provider->answer = kPlan;
    rig.send("a bass track with a wobbling filter");
    EXPECT_TRUE(rig.findTitled("Parts of this plan that were left out or adjusted").empty());
}

TEST_F(AIChatComponentTest, ApplyRunsTheWholePlanOnceAsOneUndoStep) {
    PlanRig rig;
    rig.provider->answer = kPlan;
    rig.send("a bass track with a wobbling filter");
    const int nodesBefore = rig.engine.getGraph().getNumNodes();

    auto* apply = rig.applyButton();
    ASSERT_NE(apply, nullptr);
    apply->onClick();

    EXPECT_EQ(rig.host.batches, 1) << "one applyProjectEdit, one recorded batch";
    EXPECT_EQ(rig.host.builds, 1);
    EXPECT_TRUE(hasTrackNamed(rig.doc, "Bass"));
    EXPECT_EQ(countNodesOfType(rig.engine.getGraph(), "LFO"), 1);
    EXPECT_EQ(countNodesOfType(rig.engine.getGraph(), "Filter"), 1);
    ASSERT_TRUE(rig.host.undo.undo());
    EXPECT_FALSE(hasTrackNamed(rig.doc, "Bass"));
    EXPECT_EQ(rig.engine.getGraph().getNumNodes(), nodesBefore);
}

TEST_F(AIChatComponentTest, ARefusedPlanShowsTheReasonAndNoApply) {
    PlanRig rig;
    // An instrument that does not exist: the engine refuses it.
    rig.provider->answer = R"({"mode": "merge", "nodes": [], "connections": [],
        "timelineOps": [{"op": "addInstrumentTrack", "name": "Bass", "instrument": "Theremin"}]})";
    rig.send("add a bass track");

    EXPECT_EQ(rig.findTitled("Edit plan").size(), 1u);
    EXPECT_EQ(rig.applyButton(), nullptr);
    EXPECT_TRUE(rig.chat->getLastPlanPreviewForTesting().startsWith("This plan was rejected and was not applied: "))
        << rig.chat->getLastPlanPreviewForTesting();
    EXPECT_TRUE(rig.chat->getLastPlanPreviewForTesting().contains("Theremin"))
        << rig.chat->getLastPlanPreviewForTesting();
}

TEST_F(AIChatComponentTest, APatchOnlyAnswerIsTheSameCardAndApplies) {
    PlanRig rig;
    rig.provider->answer = juce::String("Here you go:\n```json\n") + kPatchOnly + "\n```";
    rig.send("make me a simple oscillator patch");

    EXPECT_EQ(rig.findTitled("Edit plan").size(), 1u);
    auto* apply = rig.applyButton();
    ASSERT_NE(apply, nullptr);
    apply->onClick();
    EXPECT_EQ(rig.host.batches, 1);
    EXPECT_EQ(countNodesOfType(rig.engine.getGraph(), "Oscillator"), 1);
}

TEST_F(AIChatComponentTest, APatchOnlyAnswerAppliesWithoutATimeline) {
    PlanRig rig(/*hosted=*/false, /*withTimeline=*/false);
    rig.provider->answer = juce::String("```json\n") + kPatchOnly + "\n```";
    rig.send("make me a simple oscillator patch");

    EXPECT_EQ(rig.provider->sendPromptCalls, 1) << "no timeline: the patch request it always was";
    auto* apply = rig.applyButton();
    ASSERT_NE(apply, nullptr);
    apply->onClick();
    EXPECT_EQ(countNodesOfType(rig.engine.getGraph(), "Oscillator"), 1);
    EXPECT_EQ(rig.host.batches, 0);
}

TEST_F(AIChatComponentTest, ALocalConversationalAnswerStaysText) {
    PlanRig rig(/*hosted=*/false);
    rig.provider->answer = "Subtractive starts bright and filters; FM builds overtones.";
    rig.send("How does subtractive synthesis differ from FM synthesis?");

    EXPECT_EQ(rig.provider->sendPromptCalls, 1);
    EXPECT_FALSE(rig.provider->lastSchema.isObject()) << "a conversational question asks for free text";
    EXPECT_TRUE(rig.findTitled("Edit plan").empty());
    EXPECT_NE(findDescendantWithText<synth::SelectableChatText>(
                  findMessageList(*rig.chat), "Subtractive starts bright and filters; FM builds overtones."),
              nullptr);
}

TEST_F(AIChatComponentTest, ApplyIsNamedTippedAndReachedByTabAfterTheInputRow) {
    PlanRig rig;
    rig.provider->answer = kPlan;
    rig.send("a bass track with a wobbling filter");

    auto* apply = rig.applyButton();
    ASSERT_NE(apply, nullptr);
    EXPECT_EQ(apply->getTooltip(), "Apply this answer to the project (one undo step)");
    EXPECT_TRUE(apply->getWantsKeyboardFocus());

    const auto names = synth::test::walkTabOrder(*rig.chat).names();
    ASSERT_GE(names.size(), 4) << names.joinIntoString(" | ").toStdString();
    EXPECT_EQ(names[0], "Message to the AI assistant") << names.joinIntoString(" | ").toStdString();
    EXPECT_EQ(names[1], "Send");
    EXPECT_EQ(names[2], "Chat messages");
    const int applyIndex = names.indexOf("Apply edit plan");
    EXPECT_GT(applyIndex, 2) << names.joinIntoString(" | ").toStdString();
    EXPECT_GT(applyIndex, names.indexOf("Show details")) << "inside the card, in reading order";
}

TEST_F(AIChatComponentTest, ApplyOnceThenTheCardReadsAppliedAndSecondClickDoesNothing) {
    PlanRig rig;
    rig.provider->answer = kPlan;
    rig.send("a bass track with a wobbling filter");

    auto* apply = rig.applyButton();
    ASSERT_NE(apply, nullptr);
    apply->onClick();
    EXPECT_EQ(rig.host.batches, 1);

    EXPECT_FALSE(apply->isEnabled());
    EXPECT_EQ(apply->getButtonText(), "Applied");
    EXPECT_EQ(apply->getTitle(), "Edit plan applied");
    EXPECT_EQ(apply->getTooltip(), "This plan was applied; undo with Cmd+Z");
    EXPECT_TRUE(rig.findTitled("Apply edit plan").empty());

    apply->onClick();
    EXPECT_EQ(rig.host.batches, 1) << "an applied plan is never applied twice";
    EXPECT_EQ(countNodesOfType(rig.engine.getGraph(), "LFO"), 1);

    // A redraw rebuilds every bubble; the card must come back applied.
    rig.provider->answer = "plain answer";
    rig.send("thanks");
    const auto rebuilt = rig.findTitled("Edit plan applied");
    ASSERT_EQ(rebuilt.size(), 1u);
    EXPECT_FALSE(rebuilt.front()->isEnabled());
    EXPECT_TRUE(rig.findTitled("Apply edit plan").empty());
}

TEST_F(AIChatComponentTest, AppliedStateSurvivesTheSavedHistoryRoundTrip) {
    PlanRig rig;
    rig.provider->answer = kPlan;
    rig.send("a bass track with a wobbling filter");
    ASSERT_NE(rig.applyButton(), nullptr);
    rig.applyButton()->onClick();

    const auto saved = synth::LocalHistoryStore::list(rig.historyDir);
    ASSERT_EQ(saved.size(), 1u);
    synth::LocalConversation conversation;
    ASSERT_TRUE(synth::LocalHistoryStore::get(rig.historyDir, saved[0].id, conversation));
    ASSERT_EQ(conversation.messages.size(), 2u);
    EXPECT_TRUE(conversation.messages[1].content.contains("edit-plan-applied"));

    // A fresh chat restoring that conversation shows the card applied, not offering Apply again.
    PlanRig restored;
    restored.chat->setLocalHistoryDirectoryForTesting(rig.historyDir);
    restored.chat->simulateRestoreConversationForTesting(saved[0].id, /*isCloud=*/false);
    for (int i = 0; i < 40 && restored.findTitled("Edit plan").empty(); ++i)
        juce::MessageManager::getInstance()->runDispatchLoopUntil(50);

    EXPECT_EQ(restored.findTitled("Edit plan").size(), 1u);
    EXPECT_TRUE(restored.findTitled("Apply edit plan").empty());
    const auto appliedButtons = restored.findTitled("Edit plan applied");
    ASSERT_EQ(appliedButtons.size(), 1u);
    EXPECT_FALSE(appliedButtons.front()->isEnabled());
    EXPECT_TRUE(restored.chat->getLastPlanJsonForTesting().isNotEmpty());
    EXPECT_EQ(restored.host.batches, 0) << "restoring applies nothing";
}

namespace {
juce::Label* findHostedNotice(synth::AIChatComponent& chat) {
    for (auto* child : chat.getChildren())
        if (auto* label = dynamic_cast<juce::Label*>(child); label != nullptr && label->getText().startsWith("Hosted"))
            return label;
    return nullptr;
}
} // namespace

// The hosted-mode notice strip fades and its height follows; it is hidden only after the fade.
TEST_F(AIChatComponentTest, HostedNoticeFadesWithItsStripHeight) {
    PlanRig rig(/*hosted=*/true);
    auto* notice = findHostedNotice(*rig.chat);
    ASSERT_NE(notice, nullptr);
    ASSERT_TRUE(notice->isVisible());
    const int fullHeight = notice->getHeight();
    ASSERT_GT(fullHeight, 1);

    FadeAnimateGuard guard;
    rig.provider->hosted = false;
    rig.chat->refreshModels();
    EXPECT_TRUE(notice->isVisible()) << "still on screen while it fades out";
    synth::ui::FadeVisibility::stepAllForTest(0.5f);
    EXPECT_TRUE(notice->isVisible());
    EXPECT_LT(notice->getHeight(), fullHeight) << "the strip closes over the fade";
    EXPECT_NEAR(notice->getAlpha(), 0.5f, 0.01f);
    synth::ui::FadeVisibility::stepAllForTest(1.0f);
    EXPECT_FALSE(notice->isVisible());

    rig.provider->hosted = true;
    rig.chat->refreshModels();
    EXPECT_TRUE(notice->isVisible());
    EXPECT_FLOAT_EQ(notice->getAlpha(), 0.0f);
    synth::ui::FadeVisibility::stepAllForTest(1.0f);
    EXPECT_EQ(notice->getHeight(), fullHeight);
    EXPECT_FLOAT_EQ(notice->getAlpha(), 1.0f);
}

// A rating opens the comment row and Show details opens the panel, each by growing the card while it fades;
// both land at once headless.
TEST_F(AIChatComponentTest, EditPlanCardCommentRowAndDetailsFadeAndGrow) {
    PlanRig rig;
    rig.provider->answer = kPlan;
    rig.send("a bass track with a wobbling filter");
    const auto cards = rig.findTitled("Edit plan");
    ASSERT_EQ(cards.size(), 1u);
    auto* card = cards.front();
    auto* thumbsUp = dynamic_cast<juce::Button*>(rig.findTitled("This answer was helpful").front());
    ASSERT_NE(thumbsUp, nullptr);
    auto* comment = rig.findTitled("Feedback comment").front();
    auto* details = rig.findTitled("Edit plan details").front();
    juce::Button* detailsButton = nullptr;
    for (auto* child : card->getChildren())
        if (auto* b = dynamic_cast<juce::Button*>(child); b != nullptr && b->getButtonText() == "Show details")
            detailsButton = b;
    ASSERT_NE(detailsButton, nullptr);
    ASSERT_FALSE(comment->isVisible());
    ASSERT_FALSE(details->isVisible());
    const int base = card->getHeight();
    // The thumbs row is the card's last row here, so what sits below it is the card's bottom padding.
    // Widths and fonts change how the preview wraps (and so every y above), never this margin.
    const int bottomPadding = base - (thumbsUp->getBottom() + 2);
    ASSERT_GT(bottomPadding, 0);

    FadeAnimateGuard guard;
    thumbsUp->onClick();
    EXPECT_TRUE(comment->isVisible());
    EXPECT_FLOAT_EQ(comment->getAlpha(), 0.0f);
    EXPECT_EQ(card->getHeight(), base) << "frame 0 adds no height";
    synth::ui::FadeVisibility::stepAllForTest(0.5f);
    EXPECT_GT(card->getHeight(), base);
    synth::ui::FadeVisibility::stepAllForTest(1.0f);
    const int withComment = card->getHeight();
    EXPECT_EQ(withComment, base + 8 + 24);
    EXPECT_FLOAT_EQ(comment->getAlpha(), 1.0f);

    detailsButton->onClick();
    EXPECT_TRUE(details->isVisible());
    synth::ui::FadeVisibility::stepAllForTest(0.5f);
    const int midFade = card->getHeight();
    EXPECT_GT(midFade, withComment);
    synth::ui::FadeVisibility::stepAllForTest(1.0f);
    // Fully open, the card ends exactly one gap and the panel below the comment row and one bottom
    // padding under the panel, wherever the preview text wrapped.
    EXPECT_LT(midFade, card->getHeight());
    EXPECT_GT(details->getHeight(), 0);
    EXPECT_EQ(details->getY(), comment->getBottom() + 2 + 8);
    EXPECT_EQ(card->getHeight(), details->getBottom() + bottomPadding);

    detailsButton->onClick();
    EXPECT_TRUE(details->isVisible()) << "hidden only after the fade";
    synth::ui::FadeVisibility::stepAllForTest(1.0f);
    EXPECT_FALSE(details->isVisible());
    EXPECT_EQ(card->getHeight(), withComment);
}

TEST_F(AIChatComponentTest, EditPlanCardRatingAndDetailsLandAtOnceOffScreen) {
    PlanRig rig;
    rig.provider->answer = kPlan;
    rig.send("a bass track with a wobbling filter");
    auto* card = rig.findTitled("Edit plan").front();
    const int base = card->getHeight();
    dynamic_cast<juce::Button*>(rig.findTitled("This answer was helpful").front())->onClick();
    EXPECT_TRUE(rig.findTitled("Feedback comment").front()->isVisible());
    EXPECT_EQ(card->getHeight(), base + 8 + 24);
}

// A full song's plan is megabytes of JSON. The card used to lay all of it out in the details panel
// when it was built (and so again on every reopen of the conversation), freezing the app. Built from
// a real recorded full-song answer, repeated up to ~2 MB.
TEST_F(AIChatComponentTest, EditPlanCardOfAFullSongBuildsFastAndCapsItsDetails) {
    const juce::File fixture = juce::File(TESTS_ROOT_DIR).getChildFile("fixtures/ai-plans/full-song-answer.json");
    ASSERT_TRUE(fixture.existsAsFile());
    juce::var plan = juce::JSON::parse(fixture.loadFileAsString());
    ASSERT_TRUE(plan.getProperty("timelineOps", {}).isArray());
    auto* ops = plan.getProperty("timelineOps", {}).getArray();
    const juce::Array<juce::var> original = *ops;
    juce::String json = juce::JSON::toString(plan);
    while (json.length() < 2'000'000) {
        for (const auto& op : original)
            ops->add(op);
        json = juce::JSON::toString(plan);
    }

    PlanRig rig;
    synth::LocalConversation conversation;
    conversation.id = "full-song";
    conversation.title = "Full song";
    conversation.createdAt = conversation.updatedAt = "2026-10-10T00:00:00.000Z";
    conversation.messages = {{"user", "write a full song", conversation.createdAt},
                             {"assistant", json, conversation.createdAt}};
    ASSERT_TRUE(synth::LocalHistoryStore::save(rig.historyDir, conversation, synth::LocalHistoryStore::kRetainForever));

    const auto start = juce::Time::getMillisecondCounterHiRes();
    rig.chat->simulateRestoreConversationForTesting("full-song", /*isCloud=*/false);
    while (rig.findTitled("Edit plan").empty() && juce::Time::getMillisecondCounterHiRes() - start < 10000.0)
        juce::MessageManager::getInstance()->runDispatchLoopUntil(5);
    const auto restoreMs = juce::Time::getMillisecondCounterHiRes() - start;
    EXPECT_LT(restoreMs, 1500.0) << "restore of a 2 MB plan took " << restoreMs << " ms";

    const auto cards = rig.findTitled("Edit plan");
    ASSERT_EQ(cards.size(), 1u);
    auto* details = dynamic_cast<juce::TextEditor*>(rig.findTitled("Edit plan details").front());
    ASSERT_NE(details, nullptr);
    EXPECT_TRUE(details->isEmpty()) << "nothing is laid out until the panel opens";

    juce::Button* detailsButton = nullptr;
    for (auto* child : cards.front()->getChildren())
        if (auto* b = dynamic_cast<juce::Button*>(child); b != nullptr && b->getButtonText() == "Show details")
            detailsButton = b;
    ASSERT_NE(detailsButton, nullptr);
    const auto openStart = juce::Time::getMillisecondCounterHiRes();
    detailsButton->onClick();
    const auto openMs = juce::Time::getMillisecondCounterHiRes() - openStart;
    EXPECT_LT(openMs, 1500.0) << "opening the details took " << openMs << " ms";

    const juce::String shown = details->getText();
    EXPECT_LE(shown.length(), 20100);
    EXPECT_FALSE(shown.contains("timelineOps")) << "the raw plan JSON is not shown";
    EXPECT_FALSE(shown.contains("\"pitch\""));
}

// The summary itself is bounded too, and no run of characters without whitespace is long enough for
// TextEditor to chunk glyph by glyph.
TEST_F(AIChatComponentTest, EditPlanCardCapsAHugeSummaryAndBreaksLongTokens) {
    juce::String huge;
    for (int i = 0; i < 4000; ++i)
        huge << "+ Note " << i << " on a track with a longish description\n";
    huge << juce::String::repeatedString("x", 50000);
    const juce::String shown = synth::buildEditPlanDetailsText(huge);
    EXPECT_LE(shown.length(), 20100);
    EXPECT_TRUE(shown.contains("KB more not shown"));
    for (const auto& token : juce::StringArray::fromTokens(shown, " \t\r\n", ""))
        EXPECT_LE(token.length(), 120);
}

// ---- Save as preset (docs/ai/plan-preset.md) ----------------------------------------------------------------------

// The button is on for a patch that stands on its own. A plan with an Apply that is not one (a merge that points at a
// module the plan's own track ops build) shows it off with the reason as its tooltip, never silently missing; a
// refused plan has no Apply and no button. A plan that cannot be saved never has an enabled button.
TEST_F(AIChatComponentTest, SaveAsPresetIsEnabledForAPatchOfItsOwnAndOffWithAReasonForAnyOtherPlan) {
    {
        PlanRig rig;
        rig.provider->answer = kPatchOnly;
        rig.send("make me a simple oscillator patch");
        auto* save = findButtonTitled(rig, "Save patch as preset");
        ASSERT_NE(save, nullptr);
        EXPECT_TRUE(save->isEnabled());
    }
    {
        PlanRig rig; // kPlan's modulation points at a Filter its timeline op builds: not a patch of its own
        rig.provider->answer = kPlan;
        rig.send("a bass track with a wobbling filter");
        EXPECT_NE(rig.applyButton(), nullptr);
        auto* save = findButtonTitled(rig, "Save patch as preset");
        ASSERT_NE(save, nullptr) << "the button is never silently missing";
        EXPECT_FALSE(save->isEnabled());
        EXPECT_EQ(save->getButtonText(), "Save as preset");
        EXPECT_TRUE(save->getTooltip().contains("complete patch")) << save->getTooltip().toStdString();
        EXPECT_TRUE(save->getTooltip().contains("can only be applied"));
        EXPECT_FALSE(save->getWantsKeyboardFocus()) << "a disabled button is no Tab stop";
        const auto names = synth::test::walkTabOrder(*rig.chat).names();
        EXPECT_EQ(names.indexOf("Save patch as preset"), -1) << names.joinIntoString(" | ").toStdString();
        // The card is tall enough for its row: the button sits inside the card, under Apply.
        juce::Component* card = rig.findTitled("Edit plan").front();
        EXPECT_TRUE(card->getLocalBounds().contains(save->getBounds()));
        EXPECT_GE(save->getY(), rig.applyButton()->getBottom());
        // A click on it does nothing (a script can call onClick on a disabled button).
        PresetSink sink;
        sink.attach(*rig.chat);
        clickLikeAUser(*save);
        EXPECT_EQ(sink.prompts, 0);
        EXPECT_FALSE(sink.folder.exists());
    }
    {
        PlanRig rig; // the engine refused it: no Apply, no Save as preset
        rig.provider->answer = R"({"mode": "merge", "nodes": [], "connections": [],
            "timelineOps": [{"op": "addInstrumentTrack", "name": "Bass", "instrument": "Theremin"}]})";
        rig.send("add a bass track");
        EXPECT_EQ(rig.applyButton(), nullptr);
        EXPECT_EQ(findButtonTitled(rig, "Save patch as preset"), nullptr);
    }
    {
        PlanRig rig; // a patch the untrusted check refuses (a module that does not exist)
        rig.provider->answer = R"({"nodes":[{"id":1,"type":"NoSuchModule"}],"connections":[]})";
        rig.send("make a patch");
        if (auto* save = findButtonTitled(rig, "Save patch as preset"))
            EXPECT_FALSE(save->isEnabled()) << "a plan that cannot be saved never has an enabled button";
    }
}

// A real click asks for a name, writes a preset file and leaves the project exactly as it was.
TEST_F(AIChatComponentTest, SaveAsPresetWritesAPresetFileAndLeavesTheLiveGraphAlone) {
    PlanRig rig;
    PresetSink sink;
    sink.attach(*rig.chat);
    rig.provider->answer = kPatchWithState;
    rig.send("an oscillator through a filter");
    // The project already holds something, so "untouched" is more than "still empty".
    synth::AIStateMapper::applyJSONToGraph(juce::JSON::parse(R"({"nodes":[{"id":9,"type":"Reverb"}]})"),
                                           rig.engine.getGraph(), /*clearExisting=*/true, /*trusted=*/true);
    const auto before = juce::JSON::toString(synth::AIStateMapper::graphToJSON(rig.engine.getGraph()));
    const int undoBefore = rig.host.batches;

    auto* save = findButtonTitled(rig, "Save patch as preset");
    ASSERT_NE(save, nullptr);
    EXPECT_EQ(save->getButtonText(), "Save as preset");
    clickLikeAUser(*save);

    EXPECT_EQ(sink.prompts, 1);
    const auto file = sink.folder.getChildFile("My Lead.json");
    ASSERT_TRUE(file.existsAsFile());
    EXPECT_EQ(sink.savedName, "My Lead");

    const auto preset = juce::JSON::parse(file);
    ASSERT_TRUE(preset.isObject());
    juce::StringArray types;
    if (auto* nodes = preset.getProperty("nodes", {}).getArray())
        for (const auto& node : *nodes) {
            types.add(node.getProperty("type", {}).toString());
            EXPECT_FALSE(node.hasProperty("uuid")) << "a node uuid is never written";
            EXPECT_FALSE(node.hasProperty("state")) << "the untrusted state is dropped, as on Apply";
        }
    types.sort(false);
    EXPECT_EQ(types.joinIntoString(","), "Audio Output,Filter,Oscillator");
    EXPECT_FALSE(preset.hasProperty("timelineOps"));

    // The file is an ordinary preset: it loads on the path a saved preset loads on.
    AudioEngine other;
    EXPECT_TRUE(synth::AIStateMapper::applyJSONToGraph(preset, other.getGraph(), true, /*trusted=*/true));
    EXPECT_EQ(countNodesOfType(other.getGraph(), "Oscillator"), 1);

    EXPECT_EQ(juce::JSON::toString(synth::AIStateMapper::graphToJSON(rig.engine.getGraph())), before)
        << "saving a preset never applies the plan";
    EXPECT_EQ(rig.host.batches, undoBefore);
    EXPECT_EQ(save->getButtonText(), "Saved");
    EXPECT_FALSE(save->isEnabled());
    EXPECT_NE(rig.applyButton(), nullptr);
    EXPECT_TRUE(rig.applyButton()->isEnabled()) << "the plan can still be applied afterwards";
}

TEST_F(AIChatComponentTest, SaveAsPresetCancelledWritesNothingAndKeepsTheButton) {
    PlanRig rig;
    PresetSink sink;
    sink.name = {}; // the window was cancelled: the chat is never given a name
    sink.attach(*rig.chat);
    rig.provider->answer = kPatchOnly;
    rig.send("make me a simple oscillator patch");
    auto* save = findButtonTitled(rig, "Save patch as preset");
    ASSERT_NE(save, nullptr);
    clickLikeAUser(*save);
    EXPECT_EQ(sink.prompts, 1);
    EXPECT_FALSE(sink.folder.exists());
    EXPECT_TRUE(save->isEnabled());
    EXPECT_EQ(save->getButtonText(), "Save as preset");
}

// Tab reaches it straight after Apply, and Enter on it saves.
TEST_F(AIChatComponentTest, SaveAsPresetIsReachedByTabAfterApplyAndEnterPresses) {
    PlanRig rig;
    PresetSink sink;
    sink.attach(*rig.chat);
    rig.provider->answer = kPatchOnly;
    rig.send("make me a simple oscillator patch");
    const auto names = synth::test::walkTabOrder(*rig.chat).names();
    const int apply = names.indexOf("Apply edit plan");
    ASSERT_GE(apply, 0) << names.joinIntoString(" | ").toStdString();
    EXPECT_EQ(names[apply + 1], "Save patch as preset") << names.joinIntoString(" | ").toStdString();

    auto* save = findButtonTitled(rig, "Save patch as preset");
    ASSERT_NE(save, nullptr);
    EXPECT_TRUE(save->getWantsKeyboardFocus());
    EXPECT_TRUE(save->getTooltip().contains("without applying"));
    EXPECT_TRUE(static_cast<juce::Component*>(save)->keyPressed(juce::KeyPress(juce::KeyPress::returnKey)));
    synth::test::waitUntil([&] { return sink.folder.getChildFile("My Lead.json").existsAsFile(); });
    EXPECT_EQ(sink.prompts, 1);
    EXPECT_TRUE(sink.folder.getChildFile("My Lead.json").existsAsFile());
}

// The button has a row of its own under Apply, so it keeps its full width at the narrowest chat.
TEST_F(AIChatComponentTest, SaveAsPresetSitsUnderApplyAtItsFullWidth) {
    PlanRig rig;
    rig.provider->answer = kPatchOnly;
    rig.send("make me a simple oscillator patch");
    auto* save = findButtonTitled(rig, "Save patch as preset");
    auto* apply = rig.applyButton();
    ASSERT_NE(save, nullptr);
    ASSERT_NE(apply, nullptr);
    juce::Component* details = rig.findTitled("Edit plan").front();
    juce::Button* detailsButton = nullptr;
    for (auto* child : details->getChildren())
        if (auto* b = dynamic_cast<juce::Button*>(child); b != nullptr && b->getButtonText() == "Show details")
            detailsButton = b;
    ASSERT_NE(detailsButton, nullptr);
    EXPECT_GE(save->getY(), apply->getBottom()) << "its own row under Apply";
    EXPECT_EQ(save->getHeight(), apply->getHeight());
    EXPECT_EQ(save->getRight(), apply->getRight()) << "right-aligned with Apply";
    EXPECT_GT(save->getWidth(), 60);
    EXPECT_GE(save->getX(), detailsButton->getX());
}

// A redraw of the conversation (any new message rebuilds every card) keeps a saved card saved.
TEST_F(AIChatComponentTest, SaveAsPresetStaysSavedWhenTheConversationRedraws) {
    PlanRig rig;
    PresetSink sink;
    sink.attach(*rig.chat);
    rig.provider->answer = kPatchOnly;
    rig.send("make me a simple oscillator patch");
    clickLikeAUser(*findButtonTitled(rig, "Save patch as preset"));
    rig.provider->answer = "plain answer";
    rig.send("thanks");
    auto* saved = findButtonTitled(rig, "Preset saved");
    ASSERT_NE(saved, nullptr);
    EXPECT_FALSE(saved->isEnabled());
}

TEST(PlanPresetTest, ANameIsSanitisedAndNeverOverwritesAnExistingPreset) {
    const auto folder = juce::File::getSpecialLocation(juce::File::tempDirectory)
                            .getChildFile("plan-preset-names-" + juce::Uuid().toString());
    folder.createDirectory();
    const juce::String plan = kPatchOnly;
    juce::File first, second, third;
    ASSERT_TRUE(synth::planpreset::saveAs(folder, "  Warm/Pad  ", plan, first).ok);
    ASSERT_TRUE(synth::planpreset::saveAs(folder, "Warm/Pad", plan, second).ok);
    EXPECT_EQ(first.getParentDirectory(), folder) << "a path separator never leaves the folder";
    EXPECT_NE(first, second);
    EXPECT_TRUE(first.existsAsFile());
    EXPECT_TRUE(second.existsAsFile());
    EXPECT_TRUE(second.getFileName().endsWith(" 2.json"));
    const auto empty = synth::planpreset::saveAs(folder, "...", plan, third);
    EXPECT_FALSE(empty.ok);
    EXPECT_EQ(third, juce::File());
    folder.deleteRecursively();
}

// A plan that gave no positions is laid out, so a loaded preset does not stack every module on one spot.
TEST(PlanPresetTest, ModulesWithoutPositionsOpenLaidOutInColumns) {
    const auto result = synth::planpreset::buildPreset(kPatchWithState);
    ASSERT_TRUE(result.ok) << result.message;
    std::set<std::pair<int, int>> spots;
    if (auto* nodes = result.preset.getProperty("nodes", {}).getArray())
        for (const auto& node : *nodes)
            spots.insert({(int)node["position"]["x"], (int)node["position"]["y"]});
    EXPECT_EQ(spots.size(), 3u) << "three modules, three different places";
}

// Cards wider and taller than 280 x 600 (the lane-grid Oscillator, Pluck and Drum Kit) never overlap in a laid-out
// plan: each column is as wide as its widest card and each row as tall as its card.
TEST(PlanPresetTest, WideAndTallCardsWithoutPositionsDoNotOverlap) {
    const juce::String plan = R"({"nodes":[
        {"id":1,"type":"Oscillator"},{"id":2,"type":"Drum Kit"},{"id":3,"type":"Pluck"},{"id":4,"type":"Filter"},
        {"id":5,"type":"Audio Output"}],
        "connections":[{"src":1,"srcPort":0,"dst":4,"dstPort":0},{"src":2,"srcPort":0,"dst":4,"dstPort":0},
        {"src":3,"srcPort":0,"dst":4,"dstPort":0},{"src":4,"srcPort":0,"dst":5,"dstPort":0}]})";
    const auto result = synth::planpreset::buildPreset(plan);
    ASSERT_TRUE(result.ok) << result.message;
    std::vector<std::pair<juce::String, juce::Rectangle<int>>> cards;
    for (const auto& node : *result.preset.getProperty("nodes", {}).getArray()) {
        const auto type = node["type"].toString();
        const auto size = GraphEditor::estimateModuleSize(type);
        cards.push_back({type, {(int)node["position"]["x"], (int)node["position"]["y"], size.x, size.y}});
    }
    ASSERT_EQ(cards.size(), 5u);
    for (size_t i = 0; i < cards.size(); ++i)
        for (size_t j = i + 1; j < cards.size(); ++j)
            EXPECT_FALSE(cards[i].second.intersects(cards[j].second))
                << cards[i].first << " " << cards[i].second.toString() << " overlaps " << cards[j].first << " "
                << cards[j].second.toString();
}

TEST(PlanPresetTest, APlanWithOnlyTimelineOpsOrAMergeOfLiveModulesHasNoPreset) {
    EXPECT_FALSE(synth::planpreset::patchValidates(
        R"({"nodes":[],"connections":[],"timelineOps":[{"op":"addTrack","name":"Drums","kind":"instrument"}]})"));
    EXPECT_FALSE(synth::planpreset::patchValidates(
        R"({"mode":"merge","nodes":[{"id":5,"type":"Filter","params":{"cutoff":500}}],"connections":[{"src":5,"srcPort":0,"dst":999,"dstPort":0}]})"));
    EXPECT_FALSE(synth::planpreset::buildPreset("not json").ok);
}

// Enter on the focused button asks for a name once and saves once.
TEST_F(AIChatComponentTest, EnterOnSaveAsPresetFiresExactlyOnce) {
    PlanRig rig;
    PresetSink sink;
    sink.attach(*rig.chat);
    rig.provider->answer = kPatchOnly;
    rig.send("make me a simple oscillator patch");
    auto* save = findButtonTitled(rig, "Save patch as preset");
    ASSERT_NE(save, nullptr);
    EXPECT_TRUE(static_cast<juce::Component*>(save)->keyPressed(juce::KeyPress(juce::KeyPress::returnKey)));
    synth::test::waitUntil([&] { return sink.folder.getChildFile("My Lead.json").existsAsFile(); });
    synth::test::flushMessageQueue();
    EXPECT_EQ(sink.prompts, 1) << "one Enter, one name window";
    EXPECT_FALSE(save->isEnabled()) << "saved, so a second press has nothing to do";
}
