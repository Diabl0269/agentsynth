#include "AI/AIStateMapper/AIStateMapper.h"
#include "AI/SongSteps/SongDetailsText.h"
#include "AIChatComponent.h"

namespace synth {

namespace {
const juce::String kSongPlanLine = "Here is a plan for your song. Nothing is spent until you press Build.";
const juce::String kSongNotBuiltLine = "The song was not built.";
} // namespace

// Concern: the hosted song plan and build (docs/ai/song-steps.md) -- when a message is a song request, the free plan,
// the plan card's re-plan, Build with its streamed steps, and how each ending leaves the chat.

namespace {

bool containsAny(const juce::String& text, std::initializer_list<const char*> words) {
    for (const auto* word : words)
        if (text.containsWholeWordIgnoreCase(word))
            return true;
    return false;
}

bool containsPhrase(const juce::String& text, std::initializer_list<const char*> phrases) {
    for (const auto* phrase : phrases)
        if (text.containsIgnoreCase(phrase))
            return true;
    return false;
}

juce::String trimmedError(const juce::String& message) {
    return message.startsWith("Error: ") ? message.substring(7) : message;
}

} // namespace

// A new song is asked for in words that name one ("a song", "an instrumental") or a style ("dark drum and bass") with
// nothing in them that edits what already exists. Edits keep the single project.generate request: "add a filter to
// the lead" names a module, "make the bass darker" is an edit verb, and "add a bass line to the song" has an edit verb
// and no creation verb.
bool AIChatComponent::shouldPlanSong(const juce::String& text, const juce::StringArray& moduleTypeNames) {
    const bool songWord = containsAny(text, {"song", "songs", "tune", "instrumental", "composition", "soundtrack"}) ||
                          containsPhrase(text, {"full track", "whole track", "from scratch", "a track", "a beat"});
    const bool genre =
        containsPhrase(text, {"drum and bass", "drum & bass", "drum n bass", "dnb", "d&b", "hip hop", "hip-hop",
                              "lo-fi", "lofi", "synthwave", "downtempo", "breakbeat"}) ||
        containsAny(text, {"house", "techno", "trance", "ambient", "trap", "dubstep", "jazz", "funk", "disco", "edm",
                           "electro", "garage", "reggae", "jungle", "hardstyle", "phonk", "idm", "acid"});
    const bool editVerb = containsAny(text, {"add",    "remove", "delete",  "change", "set",     "increase", "decrease",
                                             "swap",   "insert", "connect", "modify", "replace", "fix",      "tweak",
                                             "adjust", "mute",   "lower",   "raise",  "turn",    "between"}) ||
                          containsPhrase(text, {"make it", "make the", "make this"});
    const bool creationVerb =
        containsAny(text, {"create", "write", "compose", "build", "generate", "produce", "give"}) ||
        containsPhrase(text, {"make me", "make a", "make an", "i want a", "i need a", "i'd like a"});
    bool namesModule = false;
    for (const auto& type : moduleTypeNames)
        namesModule = namesModule || text.containsWholeWordIgnoreCase(type);

    if (songWord && (creationVerb || !editVerb))
        return true;
    return genre && !editVerb && !namesModule;
}

int AIChatComponent::pendingSongPlanIndex() const {
    for (int i = (int)messages.size() - 1; i >= 0; --i)
        if (messages[(size_t)i].song)
            return messages[(size_t)i].song->phase == SongSession::Phase::ready ? i : -1;
    return -1;
}

void AIChatComponent::refreshUsesLeft(SongSession& session) const {
    session.usesLeft = session.usesLimit = -1;
    if (accountServicePtr == nullptr)
        return;
    const auto snapshot = accountServicePtr->getSnapshot();
    if (snapshot.state == AccountState::SignedIn && snapshot.entitlementKnown && snapshot.monthlyRequestLimit > 0) {
        session.usesLimit = snapshot.monthlyRequestLimit;
        session.usesLeft = juce::jmax(0, snapshot.monthlyRequestLimit - snapshot.requestsUsed);
    }
}

// The caller (sendButtonClicked) has added the user bubble and put the chat into its waiting state. A message sent
// while a plan waits is a change request to that plan: it becomes a new plan card and the old one is marked replaced.
void AIChatComponent::startSongPlan(const juce::String& text, size_t pendingPlanIndex) {
    const bool changing = pendingPlanIndex < messages.size() && messages[pendingPlanIndex].song != nullptr;
    const auto previous = changing ? messages[pendingPlanIndex].song : nullptr;
    juce::Component::SafePointer<AIChatComponent> safeThis(this);
    const auto id =
        aiService.planSong(text, previous ? SongSession::lengthName(previous->lengthIndex) : "",
                           previous ? SongSession::detailName(previous->detailIndex) : "",
                           previous ? previous->reply.plan.raw : juce::var(),
                           [safeThis, text, pendingPlanIndex](const AIIntegrationService::SongPlanOutcome& outcome) {
                               if (auto* self = safeThis.getComponent())
                                   self->handleSongPlan(text, pendingPlanIndex, outcome);
                           });
    if (isWaitingForResponse)
        activeRequestId = id;
}

void AIChatComponent::handleSongPlan(const juce::String& text, size_t pendingPlanIndex,
                                     const AIIntegrationService::SongPlanOutcome& outcome) {
    using Kind = AIIntegrationService::SongPlanOutcome::Kind;
    if (!isWaitingForResponse)
        return;
    if (outcome.kind == Kind::unsupported) {
        // A service without the song routes: the same text goes the way it always went, after one line saying why
        // there are no steps.
        messages.push_back(
            {"assistant", "Song steps aren't available on this server, so I'm building it in one go.", ""});
        updateChatDisplay();
        dispatchRequest(text, true, true);
        return;
    }
    if (outcome.kind == Kind::failed) {
        AIProvider::AIResponse failure;
        failure.success = false;
        failure.error = outcome.error;
        handleResponse(failure, true);
        return;
    }

    activeRequestId = {};
    const int elapsed = (int)(juce::Time::getMillisecondCounter() - requestStartMs);
    cancelRequest();
    auto session = std::make_shared<SongSession>();
    session->requestText = text;
    session->adoptPlan(outcome.reply, true);
    if (pendingPlanIndex < messages.size() && messages[pendingPlanIndex].song) {
        const auto& old = messages[pendingPlanIndex].song;
        session->requestText = old->requestText;
        session->lengthIndex = old->lengthIndex;
        session->detailIndex = old->detailIndex;
        old->phase = SongSession::Phase::dismissed;
        old->notice = "Replaced by a newer plan.";
        old->fadeInPending = false;
    }
    refreshUsesLeft(*session);
    messages.push_back({"assistant", kSongPlanLine, ""});
    messages.back().responseMs = elapsed;
    messages.back().song = session;
    updateChatDisplay();
    inputField.grabKeyboardFocus();
}

void AIChatComponent::replanSong(size_t index, int lengthIndex, int detailIndex, const juce::String& changeText,
                                 const juce::String& noticeAfter) {
    if (index >= messages.size() || !messages[index].song)
        return;
    auto session = messages[index].song;
    const int oldLength = session->lengthIndex, oldDetail = session->detailIndex;
    session->lengthIndex = lengthIndex;
    session->detailIndex = detailIndex;
    session->phase = SongSession::Phase::replanning;
    session->notice = {};
    refreshSongBubble(index);

    juce::Component::SafePointer<AIChatComponent> safeThis(this);
    aiService.planSong(
        changeText.isNotEmpty() ? changeText
                                : juce::String("Keep this song as planned and only change its length and detail."),
        SongSession::lengthName(lengthIndex), SongSession::detailName(detailIndex), session->reply.plan.raw,
        [safeThis, session, index, oldLength, oldDetail, noticeAfter](const AIIntegrationService::SongPlanOutcome& o) {
            auto* self = safeThis.getComponent();
            if (self == nullptr || session->phase != SongSession::Phase::replanning)
                return;
            if (o.kind == AIIntegrationService::SongPlanOutcome::Kind::plan) {
                session->adoptPlan(o.reply, false);
                session->notice = noticeAfter;
            } else {
                session->lengthIndex = oldLength;
                session->detailIndex = oldDetail;
                session->notice = o.kind == AIIntegrationService::SongPlanOutcome::Kind::unsupported
                                      ? juce::String("The plan could not be updated.")
                                      : "The plan could not be updated: " + trimmedError(o.error.message);
            }
            session->phase = SongSession::Phase::ready;
            self->refreshSongBubble(index);
        });
}

void AIChatComponent::dismissSong(size_t index) {
    if (index >= messages.size() || !messages[index].song)
        return;
    messages[index].song->phase = SongSession::Phase::dismissed;
    refreshSongBubble(index);
}

// Build spends the plan's price. The chat goes into its waiting state (Cancel button, Escape, read-only input) but with
// the build card instead of the thinking line, and without the one-answer timeout.
void AIChatComponent::buildSongAt(size_t index) {
    if (index >= messages.size() || !messages[index].song || isWaitingForResponse)
        return;
    auto session = messages[index].song;
    if (session->phase != SongSession::Phase::ready)
        return;
    session->phase = SongSession::Phase::building;
    session->model = songsteps::BuildModel();
    session->cancelled = false;
    session->failureText = {};
    session->notice = {};
    session->buildStartMs = juce::Time::getMillisecondCounter();
    session->buildEndMs = 0;
    session->fadeInPending = true;
    messages[index].text = "Building your song.";

    if (onMessageSent)
        onMessageSent();
    setWaiting(true);
    songBuildIndex = (int)index;
    requestStartMs = session->buildStartMs;
    sendButton.setEnabled(false);
    inputField.setReadOnly(true);
    cancelFade_.setShown(true);
    spinnerFade_.setShown(true);
    spinnerDot.startPulse(vblankUpdater);
    startTimer(kWaitingStatusIntervalMs);
    updateChatDisplay();

    juce::Component::SafePointer<AIChatComponent> safeThis(this);
    AIIntegrationService::SongBuildCallbacks callbacks;
    callbacks.onEvent = [safeThis, session](const songsteps::StreamEvent& event) {
        if (auto* self = safeThis.getComponent())
            self->handleSongEvent(session, event);
    };
    callbacks.onFinished = [safeThis, session, index](const AIIntegrationService::SongBuildOutcome& outcome) {
        if (auto* self = safeThis.getComponent())
            self->handleSongFinished(index, session, outcome);
    };
    const auto id = aiService.buildSong(session->requestText, session->reply, callbacks);
    if (isWaitingForResponse && songBuildIndex == (int)index)
        activeRequestId = id;
}

void AIChatComponent::handleSongEvent(const std::shared_ptr<SongSession>& session,
                                      const songsteps::StreamEvent& event) {
    if (session->phase != SongSession::Phase::building)
        return;
    // The done and error events end the build in handleSongFinished; the model of the steps only follows the others.
    if (event.type == songsteps::EventType::done || event.type == songsteps::EventType::error)
        return;
    if (!session->model.apply(event))
        return;
    for (size_t i = 0; i < messages.size(); ++i)
        if (messages[i].song == session)
            refreshSongBubble(i);
}

void AIChatComponent::handleSongFinished(size_t index, const std::shared_ptr<SongSession>& session,
                                         const AIIntegrationService::SongBuildOutcome& outcome) {
    using Kind = AIIntegrationService::SongBuildOutcome::Kind;
    if (session->phase != SongSession::Phase::building || index >= messages.size() || messages[index].song != session)
        return;
    session->buildEndMs = juce::Time::getMillisecondCounter();
    const int elapsed = (int)(session->buildEndMs - session->buildStartMs);
    activeRequestId = {};
    cancelRequest();
    if (accountServicePtr != nullptr)
        accountServicePtr->refreshEntitlement();

    switch (outcome.kind) {
    case Kind::applied: {
        session->model.markDone(outcome.done);
        session->phase = SongSession::Phase::built;
        auto& data = messages[index];
        data.text = "Built \"" + session->reply.plan.title + "\".";
        data.responseMs = elapsed;
        data.planJson = juce::JSON::toString(outcome.done.envelope);
        session->detailsText = songsteps::buildSongDetailsText(outcome.done.envelope, session->reply.plan.sections);
        data.planOk = true;
        data.planApplied = true;
        data.planPreviewLines = outcome.edit.previewLines;
        data.planNotes = outcome.edit.notes;
        data.planSkippedCount = outcome.edit.skippedCount;
        for (const auto& note : outcome.done.notes)
            data.planNotes.add(note);
        saveCurrentConversationLocally();
        break;
    }
    case Kind::quotaExceeded:
        messages[index].text = kSongPlanLine;
        session->phase = SongSession::Phase::ready;
        session->notice = "You have no uses left this month.";
        messages.push_back({"assistant", outcome.message, "", false, /*showUpgradeAction=*/true});
        break;
    case Kind::expired:
        messages[index].text = kSongPlanLine;
        session->phase = SongSession::Phase::ready;
        updateChatDisplay();
        replanSong(index, session->lengthIndex, session->detailIndex, {},
                   "That plan had timed out, so it was planned again. Nothing was spent. Press Build to go on.");
        inputField.grabKeyboardFocus();
        return;
    case Kind::cancelled:
        messages[index].text = kSongNotBuiltLine;
        session->phase = SongSession::Phase::failed;
        session->cancelled = true;
        session->model.markFailed("Cancelled", true);
        break;
    case Kind::rejected:
        messages[index].text = kSongNotBuiltLine;
        session->phase = SongSession::Phase::failed;
        session->failureText = "The song was built but could not be applied to the project: " + outcome.message;
        session->model.markFailed(session->failureText, false);
        break;
    case Kind::failed:
        messages[index].text = kSongNotBuiltLine;
        session->phase = SongSession::Phase::failed;
        session->failureText =
            outcome.message.isNotEmpty() ? outcome.message : juce::String("The build could not finish.");
        session->model.markFailed(session->failureText, outcome.refunded);
        break;
    }
    updateChatDisplay();
    inputField.grabKeyboardFocus();
}

// The build card's Cancel, the input row's Cancel and Escape all end here: the card says so at once, the stream is
// closed (the server aborts the remaining calls and refunds), and the provider's own Cancelled answer is then ignored.
void AIChatComponent::cancelSongBuild() {
    if (songBuildIndex < 0 || (size_t)songBuildIndex >= messages.size() || !messages[(size_t)songBuildIndex].song)
        return;
    const size_t index = (size_t)songBuildIndex;
    auto session = messages[index].song;
    session->phase = SongSession::Phase::failed;
    session->cancelled = true;
    messages[index].text = kSongNotBuiltLine;
    session->buildEndMs = juce::Time::getMillisecondCounter();
    session->model.markFailed("Cancelled", true);
    cancelRequest();
    updateChatDisplay();
    inputField.grabKeyboardFocus();
}

} // namespace synth
