#include "AIChatComponent.h"

namespace synth {

// Concern: exporting the conversation as a JSON file -- the per-turn record the sending, song and apply paths fill in
// (a ChatTranscript turn), the turns restored from history that carry only their text, the Export chat button and the
// file it writes (docs/ai/chat-component.md).

namespace {

const char* const kExportTooltip = "Save this conversation as a JSON file you can inspect or attach to a bug report";
const char* const kExportEmptyTooltip = "Nothing to export yet: send a message first";

juce::String exportFileName() {
    return "agent-synth-chat-" + juce::Time::getCurrentTime().formatted("%Y-%m-%d-%H%M") + ".json";
}

juce::String runningAppVersion() {
    if (auto* app = juce::JUCEApplicationBase::getInstance())
        return app->getApplicationVersion();
    return "unknown";
}

} // namespace

size_t AIChatComponent::beginTurn(const juce::String& text, ChatTurn::Kind kind) {
    ChatTurn turn;
    turn.at = juce::Time::getCurrentTime();
    turn.request = text;
    turn.kind = kind;
    turn.error = "No answer yet.";
    if (kind == ChatTurn::Kind::edit) {
        turn.context = aiService.buildProjectRequestBody(text);
        turn.context.getDynamicObject()->removeProperty("userPrompt"); // already the turn's "request"
    } else if (kind == ChatTurn::Kind::patch) {
        turn.context = juce::JSON::parse(aiService.getPatchContext());
    }
    activeTurn_ = transcript.addTurn(std::move(turn));
    if (!messages.empty())
        messages.back().turnIndex = (int)activeTurn_;
    return activeTurn_;
}

// A plan is not applied until its card's Apply says so, so an answer with a plan starts as "not applied" (or as
// the reason the engine refused it); a text answer has nothing to apply.
void AIChatComponent::recordAnswer(size_t turn, const juce::String& content, const juce::var& plan,
                                   const MessageData& answer) {
    transcript.updateTurn(turn, [&](ChatTurn& t) {
        t.error = {};
        if (plan.isVoid()) {
            t.response = content;
            t.appliedOk = true;
            return;
        }
        t.envelope = plan;
        t.appliedOk = false;
        t.notes = answer.planNotes;
        t.error = answer.planOk ? juce::String("Not applied yet.") : answer.planPreviewLines.joinIntoString(" ");
    });
}

void AIChatComponent::recordFailure(size_t turn, const juce::String& message) {
    transcript.updateTurn(turn, [&](ChatTurn& t) {
        t.appliedOk = false;
        t.error = message;
    });
}

void AIChatComponent::recordApplied(const MessageData& data, const ProjectEditResult& result) {
    if (data.turnIndex < 0)
        return;
    transcript.updateTurn((size_t)data.turnIndex, [&](ChatTurn& t) {
        t.appliedOk = result.ok;
        t.error = result.ok ? juce::String() : (result.message.isNotEmpty() ? result.message : "Unknown error.");
        if (result.ok)
            t.notes = result.notes.isEmpty() ? data.planNotes : result.notes;
    });
}

void AIChatComponent::recordSongPlan(const std::shared_ptr<SongSession>& session, const songsteps::PlanReply& reply) {
    if (session->turnIndex < 0)
        return;
    transcript.updateTurn((size_t)session->turnIndex, [&](ChatTurn& t) {
        t.plan = reply.plan.raw;
        t.steps = reply.steps;
        t.error = "Not built yet.";
    });
}

void AIChatComponent::recordSongEvent(const SongSession& session, const songsteps::StreamEvent& event) {
    if (session.turnIndex >= 0)
        transcript.updateTurn((size_t)session.turnIndex, [&](ChatTurn& t) { t.events.push_back(event.raw); });
}

void AIChatComponent::recordSongOutcome(const SongSession& session,
                                        const AIIntegrationService::SongBuildOutcome& outcome) {
    using Kind = AIIntegrationService::SongBuildOutcome::Kind;
    if (session.turnIndex < 0)
        return;
    transcript.updateTurn((size_t)session.turnIndex, [&](ChatTurn& t) {
        t.appliedOk = outcome.kind == Kind::applied;
        t.error = {};
        t.notes.clear();
        if (outcome.kind == Kind::applied || outcome.kind == Kind::rejected)
            t.envelope = outcome.done.envelope;
        if (outcome.kind == Kind::applied) {
            t.notes = outcome.edit.notes;
            t.notes.addArray(outcome.done.notes);
        } else if (outcome.kind == Kind::cancelled) {
            t.error = "Cancelled.";
        } else {
            t.error = outcome.message.isNotEmpty() ? outcome.message : juce::String("The build could not finish.");
        }
    });
}

// A message restored from history has no transcript turn, so its turn is read back from the bubbles: the request, and
// the answer's text or its edit plan.
std::vector<ChatTurn> AIChatComponent::collectExportTurns() const {
    std::vector<ChatTurn> turns;
    const auto restoredAt = juce::Time::fromISO8601(currentLocalConversationCreatedAt);
    for (size_t i = 0; i < messages.size(); ++i) {
        const auto& message = messages[i];
        if (message.role != "user")
            continue;
        if (const auto* live = message.turnIndex >= 0 ? transcript.turn((size_t)message.turnIndex) : nullptr) {
            turns.push_back(*live);
            continue;
        }
        ChatTurn turn;
        turn.at = restoredAt;
        turn.request = message.text;
        const MessageData* answer = nullptr;
        for (size_t j = i + 1; j < messages.size() && messages[j].role != "user"; ++j)
            if (messages[j].role == "assistant") {
                answer = &messages[j];
                break;
            }
        if (answer == nullptr) {
            turn.error = "No answer.";
        } else if (answer->planJson.isNotEmpty()) {
            turn.kind = ChatTurn::Kind::edit;
            turn.envelope = juce::JSON::parse(answer->planJson);
            turn.appliedOk = answer->planApplied;
            turn.notes = answer->planNotes;
            if (!answer->planApplied)
                turn.error = "Not applied.";
        } else if (answer->text.startsWith("Error: ")) {
            turn.error = answer->text;
        } else {
            turn.response = answer->text;
            turn.appliedOk = true;
        }
        turns.push_back(std::move(turn));
    }
    return turns;
}

juce::var AIChatComponent::buildChatExport() const {
    ChatExportInfo info;
    info.exportedAt = juce::Time::getCurrentTime();
    info.appVersion = runningAppVersion();
    info.providerKind = aiService.isCurrentProviderHosted() ? "remote" : "ollama";
    info.model = aiService.getCurrentModel();
    return ChatTranscript::toJson(collectExportTurns(), info);
}

bool AIChatComponent::writeChatExport(const juce::File& file) const {
    return file.replaceWithText(juce::JSON::toString(buildChatExport()));
}

void AIChatComponent::exportChatClicked() {
    if (!exportButton.isEnabled())
        return;
    const auto start =
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile(exportFileName());
    exportChooser = std::make_unique<juce::FileChooser>("Export chat", start, "*.json");
    juce::Component::SafePointer<AIChatComponent> safeThis(this);
    exportChooser->launchAsync(
        juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles |
            juce::FileBrowserComponent::warnAboutOverwriting,
        [safeThis](const juce::FileChooser& chooser) {
            auto* self = safeThis.getComponent();
            auto file = chooser.getResult();
            if (self == nullptr || file == juce::File())
                return;
            if (file.getFileExtension().isEmpty())
                file = file.withFileExtension("json");
            if (!self->writeChatExport(file))
                juce::AlertWindow::showAsync(
                    juce::MessageBoxOptions()
                        .withIconType(juce::MessageBoxIconType::WarningIcon)
                        .withTitle("Export chat")
                        .withMessage("The chat could not be saved to " + file.getFullPathName() + ".")
                        .withButton("OK"),
                    nullptr);
        });
}

void AIChatComponent::setUpExportButton() {
    addAndMakeVisible(exportButton);
    exportButton.setButtonText("Export chat");
    exportButton.setTitle("Export chat");
    exportButton.onClick = [this]() { exportChatClicked(); };
    updateExportButton();
}

void AIChatComponent::updateExportButton() {
    const bool hasTurns = std::any_of(messages.begin(), messages.end(),
                                      [](const MessageData& message) { return message.role == "user"; });
    exportButton.setEnabled(hasTurns);
    exportButton.setTooltip(hasTurns ? kExportTooltip : kExportEmptyTooltip);
}

} // namespace synth
