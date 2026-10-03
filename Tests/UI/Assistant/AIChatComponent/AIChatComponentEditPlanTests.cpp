// AIChatComponentEditPlanTests.cpp
// One input, one answer, one card: no Patch/Arrange selector; with a timeline a send asks for an
// edit plan through sendProjectMessage (hosted project.generate, local with the combined schema);
// a plan renders one card with one Apply that runs applyProjectEdit; a refused plan shows its reason
// and no Apply; a patch-only answer is the same card; a local conversational answer stays text; and
// the Apply button's name, tooltip and Tab position. Shared mocks live in AIChatComponentTestFixture.h.

#include "../../../AI/AIIntegrationService/PlanFakeHost.h"
#include "../../Accessibility/TabOrderHelpers.h"
#include "AIChatComponentTestFixture.h"

namespace {

// Answers both transports with `answer` and records which one each request used and what it carried.
class PlanProvider : public synth::AIProvider {
public:
    juce::String getProviderName() const override { return "PlanProvider"; }
    void fetchAvailableModels(std::function<void(const juce::StringArray&, bool)> callback) override {
        callback({}, true);
    }
    RequestId sendPrompt(const std::vector<Message>& conversation, CompletionCallback callback, const juce::var& schema,
                         std::function<void(const juce::String&)> = {}) override {
        ++sendPromptCalls;
        lastConversation = conversation;
        lastSchema = schema;
        respond(callback);
        return {};
    }
    RequestId sendCapabilityRequest(const juce::String& capability, const juce::var&,
                                    CompletionCallback callback) override {
        ++capabilityCalls;
        lastCapability = capability;
        respond(callback);
        return {};
    }
    void cancel(RequestId) override {}
    void setModel(const juce::String&) override {}
    juce::String getCurrentModel() const override { return {}; }
    void setRequestTimeoutMs(int) override {}
    int getRequestTimeoutMs() const override { return 240000; }
    bool isHosted() const override { return hosted; }

    bool hosted = true;
    juce::String answer = "plain answer";
    int sendPromptCalls = 0, capabilityCalls = 0;
    juce::String lastCapability;
    juce::var lastSchema;
    std::vector<Message> lastConversation;

private:
    void respond(const CompletionCallback& callback) const {
        AIResponse response;
        response.success = true;
        response.content = answer;
        callback(response);
    }
};

// A patch (an LFO modulating the new track's Filter insert by name) plus two timeline ops: an
// instrument track with that insert, and a lane on it - the engine's own small plan.
constexpr const char* kPlan = R"({"mode": "merge",
    "nodes": [{"id": 7003, "type": "LFO"}], "connections": [],
    "modulations": [{"source": 7003, "dest": 7002, "destParam": "cutoff", "amount": 0.5}],
    "timelineOps": [
        {"op": "addInstrumentTrack", "name": "Bass", "instrument": "Oscillator", "instrumentId": 7001,
         "inserts": [{"type": "Filter", "id": 7002, "params": {"cutoff": 600}}]},
        {"op": "writeLane", "nodeId": 7002, "paramId": "cutoff",
         "points": [{"beat": 0, "value": 400, "curve": 1}, {"beat": 8, "value": 3000, "curve": 1}]}]})";

constexpr const char* kPatchOnly = R"({"nodes":[{"id":1,"type":"Oscillator"},{"id":2,"type":"Audio Output"}],)"
                                   R"("connections":[{"src":1,"srcPort":0,"dst":2,"dstPort":0}]})";

// The chat over a service with a live timeline and an app-shaped host, as MainComponent wires it.
struct PlanRig {
    explicit PlanRig(bool hosted = true, bool withTimeline = true) {
        auto owned = std::make_unique<PlanProvider>();
        provider = owned.get();
        provider->hosted = hosted;
        service.setProvider(std::move(owned));
        if (withTimeline) {
            service.setTimelineContext(&doc, &transport);
            service.setTimelineToolsEnabled(true);
            service.setTimelineOpsHost(&host);
        }
        juce::PropertiesFile::Options options;
        options.applicationName = "AIChatEditPlanTest";
        options.filenameSuffix = "test";
        options.storageFormat = juce::PropertiesFile::storeAsXML;
        props.setStorageParameters(options);
        chat = std::make_unique<synth::AIChatComponent>(service, props);
        historyDir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                         .getChildFile("chat-plan-" + juce::Uuid().toString());
        chat->setLocalHistoryDirectoryForTesting(historyDir);
        chat->setSize(400, 700);
        chat->refreshModels();
    }

    void send(const juce::String& text) {
        for (auto* child : chat->getChildren())
            if (auto* editor = dynamic_cast<juce::TextEditor*>(child); editor != nullptr && editor->isVisible())
                editor->setText(text);
        chat->triggerSend();
        juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
    }

    std::vector<juce::Component*> findTitled(const juce::String& title) {
        std::vector<juce::Component*> found;
        std::function<void(juce::Component*)> walk = [&](juce::Component* c) {
            for (auto* child : c->getChildren()) {
                if (child->getTitle() == title)
                    found.push_back(child);
                walk(child);
            }
        };
        if (auto* list = findMessageList(*chat))
            walk(list);
        return found;
    }

    juce::Button* applyButton() {
        const auto buttons = findTitled("Apply edit plan");
        return buttons.empty() ? nullptr : dynamic_cast<juce::Button*>(buttons.front());
    }

    AudioEngine engine;
    synth::AIIntegrationService service{engine.getGraph()};
    synth::TimelineDoc doc;
    synth::TransportService transport;
    synth::PlanFakeHost host{doc, engine.getGraph()};
    juce::ApplicationProperties props;
    juce::File historyDir;
    PlanProvider* provider = nullptr;
    std::unique_ptr<synth::AIChatComponent> chat;
};

bool hasTrackNamed(const synth::TimelineDoc& doc, const juce::String& name) {
    for (const auto& track : doc.getTracks())
        if (track.name == name)
            return true;
    return false;
}

int countNodesOfType(juce::AudioProcessorGraph& graph, const juce::String& type) {
    int count = 0;
    const juce::var patch = synth::AIStateMapper::graphToJSON(graph); // keep alive: getArray() points into it
    if (auto* nodes = patch.getProperty("nodes", {}).getArray())
        for (const auto& node : *nodes)
            count += node.getProperty("type", {}).toString() == type ? 1 : 0;
    return count;
}

} // namespace

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
    // A replace that would delete the instrument the same plan builds: the engine refuses it.
    rig.provider->answer = R"({"mode": "replace", "nodes": [], "connections": [],
        "timelineOps": [{"op": "addInstrumentTrack", "name": "Bass", "instrument": "Oscillator"}]})";
    rig.send("replace everything with a bass track");

    EXPECT_EQ(rig.findTitled("Edit plan").size(), 1u);
    EXPECT_EQ(rig.applyButton(), nullptr);
    EXPECT_TRUE(rig.chat->getLastPlanPreviewForTesting().startsWith("This plan was rejected and was not applied: "))
        << rig.chat->getLastPlanPreviewForTesting();
    EXPECT_TRUE(rig.chat->getLastPlanPreviewForTesting().contains("merge"));
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
    EXPECT_NE(findDescendantWithText<juce::Label>(findMessageList(*rig.chat),
                                                  "Subtractive starts bright and filters; FM builds overtones."),
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
