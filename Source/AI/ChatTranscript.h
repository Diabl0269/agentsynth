#pragma once

#include "AI/SongSteps/SongSteps.h"
#include <functional>
#include <juce_core/juce_core.h>
#include <vector>

// What the AI chat keeps of each exchange so the conversation can be exported as a JSON file: the raw data a turn
// already received, in memory only. No UI and no network, and the serializer is a pure function
// (docs/ai/chat-component.md).
namespace synth {

struct ChatTurn {
    enum class Kind { song, edit, patch, chat };

    juce::Time at; // Time() when unknown (a turn restored from history): the export then leaves "at" out
    juce::String request;
    Kind kind = Kind::chat;
    juce::var plan; // song: the plan object as received
    std::vector<songsteps::StepInfo> steps;
    std::vector<juce::var> events; // song: every build stream event, as received, in order
    juce::var envelope;            // the final edit plan or patch, as received
    juce::String response;         // the model's text, when there is no envelope
    juce::var context;             // the project summary sent with the request, when the app sends one

    // The outcome of applying the answer; a text answer has nothing to apply and counts as ok.
    bool appliedOk = false;
    juce::String error;
    juce::StringArray notes;
};

/** What the export header records besides the turns. */
struct ChatExportInfo {
    juce::Time exportedAt;
    juce::String appVersion;
    juce::String providerKind; // "remote" or "ollama"
    juce::String model;        // left out of the file when empty
};

/** "song", "edit", "patch" or "chat". */
const char* chatTurnKindName(ChatTurn::Kind kind);

class ChatTranscript {
public:
    /** Appends `turn` and returns its index. */
    size_t addTurn(ChatTurn turn);
    /** Runs `change` on turn `index`; false (and nothing run) when there is no such turn. */
    bool updateTurn(size_t index, const std::function<void(ChatTurn&)>& change);
    const ChatTurn* turn(size_t index) const;
    const std::vector<ChatTurn>& turns() const { return turnList; }
    bool empty() const { return turnList.empty(); }
    void clear() { turnList.clear(); }

    juce::var toJson(const ChatExportInfo& info) const { return toJson(turnList, info); }

    /** The export document: {"format":"agentsynth-chat","version":1,"exportedAt","app","provider","turns"}. */
    static juce::var toJson(const std::vector<ChatTurn>& turns, const ChatExportInfo& info);

private:
    std::vector<ChatTurn> turnList;
};

} // namespace synth
