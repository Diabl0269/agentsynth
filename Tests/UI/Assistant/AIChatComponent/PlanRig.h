#pragma once

// PlanRig.h -- the shared rig of the edit-plan card tests: a provider that answers with a fixed string, the chat over a
// service with a live timeline and an app-shaped host, and the Save as preset helpers. Included by the edit-plan test
// files; one translation unit each, so the helpers are inline in a named namespace.

#include "../../../AI/AIIntegrationService/PlanFakeHost.h"
#include "../../../TestWait.h"
#include "AIChatComponentTestFixture.h"
#include "TestPaths.h"

namespace plan_rig {

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

inline bool hasTrackNamed(const synth::TimelineDoc& doc, const juce::String& name) {
    for (const auto& track : doc.getTracks())
        if (track.name == name)
            return true;
    return false;
}

inline int countNodesOfType(juce::AudioProcessorGraph& graph, const juce::String& type) {
    int count = 0;
    const juce::var patch = synth::AIStateMapper::graphToJSON(graph); // keep alive: getArray() points into it
    if (auto* nodes = patch.getProperty("nodes", {}).getArray())
        for (const auto& node : *nodes)
            count += node.getProperty("type", {}).toString() == type ? 1 : 0;
    return count;
}

// A temp folder for the presets a test saves, removed with the test; the card's name window is replaced by a name the
// test types, so what runs is the card, the chat's save and the file write.
struct PresetSink {
    juce::File folder = juce::File::getSpecialLocation(juce::File::tempDirectory)
                            .getChildFile("plan-preset-" + juce::Uuid().toString());
    juce::String name = "My Lead";
    int prompts = 0;
    juce::String savedName;

    void attach(synth::AIChatComponent& chat) {
        chat.presetFolder = [this] { return folder; };
        chat.presetNamePrompt = [this](std::function<void(const juce::String&)> onName) {
            ++prompts;
            if (name.isNotEmpty())
                onName(name);
        };
        chat.onPresetSaved = [this](const juce::String& saved) { savedName = saved; };
    }
    ~PresetSink() { folder.deleteRecursively(); }
};

inline juce::Button* findButtonTitled(PlanRig& rig, const juce::String& title) {
    const auto found = rig.findTitled(title);
    auto* button = found.empty() ? nullptr : dynamic_cast<juce::Button*>(found.front());
    return button != nullptr && button->isVisible() ? button : nullptr; // a hidden button is no button
}

// A click as the button's own handler runs it. (The real pointer path is the Tools/ui-verify scenario's.)
inline void clickLikeAUser(juce::Button& button) {
    button.onClick(); // the handler a click reaches; the real pointer path is the Tools/ui-verify scenario's
}

constexpr const char* kPatchWithState =
    R"({"nodes":[{"id":1,"type":"Oscillator","state":{"evil":"/etc/passwd"},"params":{"level":0.5}},)"
    R"({"id":2,"type":"Filter"},{"id":3,"type":"Audio Output"}],)"
    R"("connections":[{"src":1,"srcPort":0,"dst":2,"dstPort":0},{"src":2,"srcPort":0,"dst":3,"dstPort":0}]})";

} // namespace plan_rig
