#include "AI/ChatTranscript.h"

namespace synth {

// Concern: the in-memory record of a chat's turns and its export document. Optional fields are left out of the JSON
// rather than written empty, so a reader can tell "none" from "empty".

namespace {

juce::var makeObject() { return juce::var(new juce::DynamicObject()); }

juce::var stepsToJson(const std::vector<songsteps::StepInfo>& steps) {
    juce::Array<juce::var> out;
    for (const auto& step : steps) {
        auto object = makeObject();
        object.getDynamicObject()->setProperty("id", step.id);
        object.getDynamicObject()->setProperty("kind", songsteps::stepKindToString(step.kind));
        object.getDynamicObject()->setProperty("label", step.label);
        out.add(object);
    }
    return out;
}

juce::var appliedToJson(const ChatTurn& turn) {
    auto applied = makeObject();
    auto* object = applied.getDynamicObject();
    object->setProperty("ok", turn.appliedOk);
    if (turn.error.isNotEmpty())
        object->setProperty("error", turn.error);
    if (!turn.notes.isEmpty()) {
        juce::Array<juce::var> notes;
        for (const auto& note : turn.notes)
            notes.add(note);
        object->setProperty("notes", notes);
    }
    return applied;
}

juce::var turnToJson(const ChatTurn& turn) {
    auto result = makeObject();
    auto* object = result.getDynamicObject();
    if (turn.at != juce::Time())
        object->setProperty("at", turn.at.toISO8601(true));
    object->setProperty("request", turn.request);
    object->setProperty("kind", chatTurnKindName(turn.kind));
    if (!turn.plan.isVoid())
        object->setProperty("plan", turn.plan);
    if (!turn.steps.empty())
        object->setProperty("steps", stepsToJson(turn.steps));
    if (!turn.events.empty()) {
        juce::Array<juce::var> events;
        for (const auto& event : turn.events)
            events.add(event);
        object->setProperty("events", events);
    }
    if (!turn.envelope.isVoid())
        object->setProperty("envelope", turn.envelope);
    if (turn.response.isNotEmpty())
        object->setProperty("response", turn.response);
    object->setProperty("applied", appliedToJson(turn));
    if (!turn.context.isVoid())
        object->setProperty("context", turn.context);
    return result;
}

} // namespace

const char* chatTurnKindName(ChatTurn::Kind kind) {
    switch (kind) {
    case ChatTurn::Kind::song:
        return "song";
    case ChatTurn::Kind::edit:
        return "edit";
    case ChatTurn::Kind::patch:
        return "patch";
    case ChatTurn::Kind::chat:
        break;
    }
    return "chat";
}

size_t ChatTranscript::addTurn(ChatTurn turn) {
    turnList.push_back(std::move(turn));
    return turnList.size() - 1;
}

bool ChatTranscript::updateTurn(size_t index, const std::function<void(ChatTurn&)>& change) {
    if (index >= turnList.size())
        return false;
    change(turnList[index]);
    return true;
}

const ChatTurn* ChatTranscript::turn(size_t index) const {
    return index < turnList.size() ? &turnList[index] : nullptr;
}

juce::var ChatTranscript::toJson(const std::vector<ChatTurn>& turns, const ChatExportInfo& info) {
    auto root = makeObject();
    auto* object = root.getDynamicObject();
    object->setProperty("format", "agentsynth-chat");
    object->setProperty("version", 1);
    object->setProperty("exportedAt", info.exportedAt.toISO8601(true));

    auto app = makeObject();
    app.getDynamicObject()->setProperty("version", info.appVersion);
    object->setProperty("app", app);

    auto provider = makeObject();
    provider.getDynamicObject()->setProperty("kind", info.providerKind);
    if (info.model.isNotEmpty())
        provider.getDynamicObject()->setProperty("model", info.model);
    object->setProperty("provider", provider);

    juce::Array<juce::var> turnArray;
    for (const auto& turn : turns)
        turnArray.add(turnToJson(turn));
    object->setProperty("turns", turnArray);
    return root;
}

} // namespace synth
