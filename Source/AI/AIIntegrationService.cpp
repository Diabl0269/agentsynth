#include "AIIntegrationService.h"
#include "../Branding.h"

namespace synth {

AIIntegrationService::AIIntegrationService(juce::AudioProcessorGraph& graph)
    : audioGraph(graph) {
    initSystemPrompt();
}

AIIntegrationService::~AIIntegrationService() {}

void AIIntegrationService::setProvider(std::unique_ptr<AIProvider> newProvider) { provider = std::move(newProvider); }

void AIIntegrationService::sendMessage(const juce::String& text, AIProvider::CompletionCallback callback,
                                       bool useStructuredOutput) {
    // Inject current graph state only for patch-related requests
    juce::String messageContent = text;
    if (useStructuredOutput) {
        juce::var graphJson = AIStateMapper::graphToJSON(audioGraph);
        if (auto* obj = graphJson.getDynamicObject()) {
            if (auto* nodeArr = obj->getProperty("nodes").getArray()) {
                if (!nodeArr->isEmpty()) {
                    messageContent = "Current patch state:\n```json\n" + juce::JSON::toString(graphJson) +
                                     "\n```\n\nUser request: " + text;
                }
            }
        }
    }

    chatHistory.push_back({"user", messageContent});

    if (provider) {
        auto weakThis = juce::WeakReference<AIIntegrationService>(this);
        auto schema = useStructuredOutput ? AIStateMapper::getPatchSchema() : juce::var();

        provider->sendPrompt(
            chatHistory,
            [weakThis, callback](const juce::String& response, bool success) {
                if (weakThis.get() == nullptr)
                    return; // Service was destroyed

                auto* self = weakThis.get();
                if (success) {
                    self->chatHistory.push_back({"assistant", response});
                }
                if (callback) {
                    callback(response, success);
                }
            },
            schema);
    } else {
        if (callback) {
            callback("Error: No AI provider selected.", false);
        }
    }
}

bool AIIntegrationService::applyPatch(const juce::String& jsonString, bool mergeMode) {
    juce::String extractedJson = extractJsonFromResponse(jsonString);
    juce::var json = juce::JSON::parse(extractedJson);
    // Notify listeners to detach graph-referencing UI BEFORE the graph is rebuilt (avoids a use-after-free
    // where a ScopeComponent timer reads a freed VisualBuffer once applyJSONToGraph clears old processors).
    listeners.call([](Listener& l) { l.aiPatchAboutToApply(); });
    if (AIStateMapper::applyJSONToGraph(json, audioGraph, !mergeMode)) {
        listeners.call([](Listener& l) { l.aiPatchApplied(); });
        return true;
    }
    return false;
}

juce::String AIIntegrationService::extractJsonFromResponse(const juce::String& response) {
    // 1. Try to find JSON between backticks
    int start = response.indexOf("```json");
    if (start != -1) {
        start += 7;
        int end = response.indexOf(start, "```");
        if (end != -1)
            return response.substring(start, end).trim();
    }

    // 2. Try to find JSON between any backticks
    start = response.indexOf("```");
    if (start != -1) {
        start += 3;
        int end = response.indexOf(start, "```");
        if (end != -1)
            return response.substring(start, end).trim();
    }

    // 3. Try to find first '{' and last '}'
    start = response.indexOf("{");
    int end = response.lastIndexOf("}");
    if (start != -1 && end != -1 && end > start)
        return response.substring(start, end + 1).trim();

    return response.trim();
}

juce::String AIIntegrationService::getPatchContext() {
    juce::var json = AIStateMapper::graphToJSON(audioGraph);
    return juce::JSON::toString(json);
}

void AIIntegrationService::clearHistory() {
    chatHistory.clear();
    initSystemPrompt();
}

void AIIntegrationService::initSystemPrompt() {
    juce::String schema = AIStateMapper::getModuleSchema();

    juce::String systemMsg =
        "You are " + juce::String(synth::branding::kProductName) + " AI, an expert sound designer for the " +
        juce::String(synth::branding::kProductName) +
        " modular synthesizer. "
        "Your goal is to help users create and modify patches. " +
        juce::String(synth::branding::kProductName) +
        " uses a nodes-and-connections model. "
        "\n\n" +
        schema +
        "\n"
        "### AVAILABLE PARAMETER CHOICES (USE THESE!):\n"
        "For any parameter that is a 'Choice', you MUST select from the specific options provided in the "
        "`parameterChoices` object "
        "within the schema. You MUST use the exact string value from the provided list. "
        "For example, for an LFO 'shape' parameter, if the schema says `\"LFO\": { \"shape\": [\"Sine\", \"S&H\"] }, "
        "you MUST NOT guess other shapes or use numbers. For 'rateSync', use the exact string, e.g., \"1/4\", NOT the "
        "number 0.25.\n"
        "If you do not use the exact string from the schema, the parameter will be ignored or set to default.\n"
        "\n"
        "### MODES OF OPERATION:\n"
        "1. **Conversational Mode**: When the user asks a general question, respond naturally in Markdown.\n"
        "2. **Structured Patch Mode**: When requested to create or modify a patch, you MUST provide a JSON block. "
        "If a 'format' schema is provided in the API request, your entire response MUST be the raw JSON adhering to "
        "that schema, with NO additional text or Markdown formatting.\n"
        "\n"
        "### CRITICAL SIGNAL ROUTING (CONNECTIONS VS MODULATIONS):\n"
        "1. **AUDIO/MIDI SIGNAL FLOW (Connections)**: Use the `connections` array to route audio (Oscillator output -> "
        "Filter input) and MIDI (Sequencer -> Oscillator). "
        "Audio/MIDI ports are usually 0 for audio or -1 for MIDI.\n"
        "2. **MODULATION/CONTROL SIGNAL FLOW (Modulations)**: Use the `modulations` array ONLY when you want a control "
        "signal (LFO, envelope) to modulate a parameter (e.g., Filter Cutoff, VCA Level). "
        "The system will automatically create an Attenuverter node to handle this.\n"
        "\n"
        "### SIGNAL ROUTING EXAMPLE (Adding Oscillator to existing Filter):\n"
        "User request: 'Add a new Saw oscillator connected to existing filter node 100'.\n"
        "```json\n"
        "{\n"
        "  \"mode\": \"merge\",\n"
        "  \"nodes\": [{ \"id\": 9005, \"type\": \"Oscillator\", \"params\": { \"waveform\": \"Saw\" } }],\n"
        "  \"connections\": [{ \"src\": 9005, \"srcPort\": 0, \"dst\": 100, \"dstPort\": 0 }]\n"
        "}\n"
        "```\n"
        "\n### IMPORTANT INSTRUCTIONS FOR PATCHES:\n"
        "- **Parameter IDs are Case-Sensitive**: Use the exact `Parameter ID` from the table above (e.g., use "
        "`cutoff`, not `Cutoff`).\n"
        "- **Values**: Use raw, unnormalized values within the specified ranges. Do NOT use normalized 0-1 values. "
        "For example, Oscillator `octave` range is -4 to 4 (default 0), `coarse` is -12 to 12 (default 0), "
        "`fine` is -100 to 100 (default 0).\n"
        "- **Omit default parameters**: ONLY include the parameters you specifically want to change. Do NOT send full "
        "lists of all parameters. Omitted parameters keep their default values. For example, if you are adding an "
        "Oscillator and only want to set 'octave' to 1, only include `{\"octave\": 1.0}` in the `params` object.\n"
        "- **Choice Parameters**: Use the exact string name (e.g., `\"waveform\": \"Saw\"`).\n"
        "- **Connections**: Ensure `srcPort` and `dstPort` are valid for the given module type. Most modules use port "
        "0 for their primary audio/midi signal.\n"
        "\nExample format:\n"
        "```json\n"
        "{\n"
        "  \"nodes\": [\n"
        "    { \"id\": 1, \"type\": \"Oscillator\", \"params\": { \"frequency\": 440.0, \"waveform\": \"Saw\" } },\n"
        "    { \"id\": 2, \"type\": \"Audio Output\" }\n"
        "  ],\n"
        "  \"connections\": [\n"
        "    { \"src\": 1, \"srcPort\": 0, \"dst\": 2, \"dstPort\": 0 }\n"
        "  ]\n"
        "}\n"
        "```\n"
        "\n### DELTA / MERGE MODE:\n"
        "When the user's message includes their current patch state (as JSON) and they ask to ADD, MODIFY, or REMOVE "
        "elements, respond with only the CHANGES (delta), not the entire patch. Include `\"mode\": \"merge\"` in your "
        "JSON.\n"
        "- **Adding nodes**: Include only NEW nodes in `nodes`. Use existing node IDs (from the current patch state) "
        "in "
        "`connections` to wire new nodes to existing ones.\n"
        "- **Modifying parameters**: Include the existing node (same ID, same type) with only the changed params.\n"
        "- **Removing nodes**: Use `\"remove\": [nodeId]` to delete nodes by their ID from the current patch.\n"
        "- **Full replacement**: When creating from scratch or when no current patch exists, use `\"mode\": "
        "\"replace\"` "
        "(or omit `mode`).\n"
        "\nDelta example (adding a Reverb to an existing VCA node 1003):\n"
        "```json\n"
        "{\"mode\": \"merge\", \"nodes\": [{\"id\": 9001, \"type\": \"Reverb\"}], "
        "\"connections\": [{\"src\": 1003, \"srcPort\": 0, \"dst\": 9001, \"dstPort\": 0}]}\n"
        "```\n"
        "\nRemoval example:\n"
        "```json\n"
        "{\"mode\": \"merge\", \"remove\": [1003], \"nodes\": [], \"connections\": []}\n"
        "```\n"
        "\n### MODULATION ROUTING (CRITICAL):\n"
        "**ALWAYS use the `modulations` array** when routing a modulation source (LFO, ADSR, envelope) "
        "to a parameter target (filter cutoff, VCA CV, etc.). Do NOT use `connections` for modulation — "
        "`connections` are ONLY for audio signal flow (e.g., Oscillator->Filter->VCA->Output) and MIDI. "
        "The system automatically creates the necessary Attenuverter intermediary.\n"
        "\nEach modulation entry needs:\n"
        "- `source`: node ID of the modulation source (LFO, envelope, etc.)\n"
        "- `sourcePort` (optional, default 0): output port of the source module\n"
        "- `dest`: node ID of the destination module\n"
        "- `destPort`: the input port for the target parameter (see Modulation Targets table above)\n"
        "- `amount` (optional, default 1.0): modulation depth from -1.0 (inverted) to 1.0\n"
        "- `bypass` (optional, default false): whether the modulation is bypassed\n"
        "\nModulation example (LFO modulating filter cutoff at 50% depth):\n"
        "```json\n"
        "{\"nodes\": [{\"id\": 5, \"type\": \"LFO\", \"params\": {\"rateHz\": 2.0}}, "
        "{\"id\": 3, \"type\": \"Filter\"}], "
        "\"connections\": [], "
        "\"modulations\": [{\"source\": 5, \"dest\": 3, \"destPort\": 1, \"amount\": 0.5}]}\n"
        "```\n"
        "\nMerge example (adding LFO modulation to existing filter node 1003):\n"
        "```json\n"
        "{\"mode\": \"merge\", \"nodes\": [{\"id\": 9001, \"type\": \"LFO\", \"params\": {\"rateHz\": 4.0}}], "
        "\"connections\": [], "
        "\"modulations\": [{\"source\": 9001, \"dest\": 1003, \"destPort\": 1, \"amount\": 0.8}]}\n"
        "```\n"
        "\nTo remove a modulation in merge mode, use `removeModulations`:\n"
        "```json\n"
        "{\"mode\": \"merge\", \"removeModulations\": [{\"source\": 5, \"dest\": 3, \"destPort\": 1}], "
        "\"nodes\": [], \"connections\": []}\n"
        "```";

    chatHistory.push_back({"system", systemMsg});
}

void AIIntegrationService::setModel(const juce::String& name) {
    if (provider)
        provider->setModel(name);
}

juce::String AIIntegrationService::getCurrentModel() const { return provider ? provider->getCurrentModel() : ""; }

void AIIntegrationService::fetchAvailableModels(
    std::function<void(const juce::StringArray& models, bool success)> callback) {
    if (provider)
        provider->fetchAvailableModels(callback);
    else if (callback)
        callback({}, false);
}
} // namespace synth
