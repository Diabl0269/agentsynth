#include "AIIntegrationService.h"
#include "../AppUndoManager.h"
#include "../Branding.h"
#include "PatchEval.h"
#include <algorithm>

namespace synth {

AIIntegrationService::AIIntegrationService(juce::AudioProcessorGraph& graph, AppUndoManager* undoManager)
    : audioGraph(graph)
    , undoManager(undoManager) {
    initSystemPrompt();
}

AIIntegrationService::~AIIntegrationService() {}

void AIIntegrationService::setProvider(std::unique_ptr<AIProvider> newProvider) {
    provider = std::move(newProvider);

    // Re-push contract (mirrors AIChatComponent::refreshModels(), see docs/AI_Engine.md "Model
    // Discovery Ordering Contract"): a caller may have called setAuthToken() before a provider
    // existed at all, so the value must be forwarded to whatever provider is installed now.
    if (provider && currentAuthToken.isNotEmpty())
        provider->setAuthToken(currentAuthToken);
}

void AIIntegrationService::setAuthToken(const juce::String& token) {
    currentAuthToken = token;
    if (provider)
        provider->setAuthToken(currentAuthToken);
}

AIProvider::RequestId AIIntegrationService::sendMessage(const juce::String& text,
                                                        AIProvider::CompletionCallback callback,
                                                        bool useStructuredOutput) {
    // The stored history always keeps the user's original text. Patch context is ephemeral:
    // it is spliced into the outgoing request only, never retained in chatHistory.
    chatHistory.push_back({"user", text});
    trimHistory();

    if (!provider) {
        if (callback) {
            AIProvider::AIResponse response;
            response.success = false;
            response.error.kind = AIProvider::AIErrorKind::Schema; // no provider configured — client precondition
            response.error.message = "Error: No AI provider selected.";
            callback(response);
        }
        return {};
    }

    std::vector<AIProvider::Message> request = chatHistory;
    if (useStructuredOutput && !request.empty())
        request.back().content = buildPatchAugmentedContent(text);

    auto weakThis = juce::WeakReference<AIIntegrationService>(this);
    auto schema = useStructuredOutput ? AIStateMapper::getPatchSchema() : juce::var();

    return provider->sendPrompt(
        request,
        [weakThis, callback](const AIProvider::AIResponse& response) {
            if (weakThis.get() == nullptr)
                return; // Service was destroyed

            auto* self = weakThis.get();

            // A cancelled request produced no assistant turn. Recording one would put words in the
            // model's mouth that the user never saw and — because chatHistory is replayed as
            // context — feed that invention back on every later message. The user's own turn
            // stays: they did say it, and it is still on screen.
            if (response.error.kind != AIProvider::AIErrorKind::Cancelled && response.success) {
                self->chatHistory.push_back({"assistant", response.content});
                self->trimHistory();
            }

            if (callback) {
                callback(response);
            }
        },
        schema);
}

void AIIntegrationService::cancelRequest(AIProvider::RequestId requestId) {
    if (provider)
        provider->cancel(requestId);
}

juce::String AIIntegrationService::buildPatchAugmentedContent(const juce::String& text) {
    juce::var graphJson = AIStateMapper::graphToJSON(audioGraph);
    if (auto* obj = graphJson.getDynamicObject()) {
        if (auto* nodeArr = obj->getProperty("nodes").getArray()) {
            if (!nodeArr->isEmpty()) {
                return "Current patch state:\n```json\n" + juce::JSON::toString(graphJson) +
                       "\n```\n\nUser request: " + text;
            }
        }
    }
    return text;
}

void AIIntegrationService::trimHistory() {
    if (chatHistory.empty())
        return;

    size_t start = (chatHistory.front().role == "system") ? 1 : 0;
    size_t conversationSize = chatHistory.size() - start;
    size_t maxConversation = static_cast<size_t>(kMaxHistoryTurns) * 2;

    if (conversationSize <= maxConversation)
        return;

    size_t excess = conversationSize - maxConversation;
    size_t pairsToRemove = (excess + 1) / 2; // round up to whole pairs
    size_t messagesToRemove = std::min(pairsToRemove * 2, conversationSize);

    chatHistory.erase(chatHistory.begin() + static_cast<long>(start),
                      chatHistory.begin() + static_cast<long>(start + messagesToRemove));
}

bool AIIntegrationService::hasExplicitMode(const juce::var& json) {
    auto* obj = json.getDynamicObject();
    if (obj == nullptr || !obj->hasProperty("mode"))
        return false;
    // An empty or non-string "mode" is no statement of intent, so it must not block the repair.
    return obj->getProperty("mode").toString().isNotEmpty();
}

bool AIIntegrationService::applyPatch(const juce::String& jsonString, bool mergeMode) {
    juce::String extractedJson = extractJsonFromResponse(jsonString);
    juce::var json = juce::JSON::parse(extractedJson);
    bool clearExisting = !mergeMode;

    // Validate BEFORE touching any listener or the undo stack — juce::JSON::parse returns a void var for
    // unparseable input, which validatePatch rejects (NotAnObject) along with any other structurally invalid
    // patch. A failure here means the graph is never mutated, so listeners must not be told a patch is about
    // to apply, and no no-op entry may be left on the undo stack.
    lastPatchError.clear();
    lastPatchErrorCode = PatchValidationError::None;

    lastPatchModeRepaired = false;

    auto validation = AIStateMapper::validatePatch(json, audioGraph, clearExisting, /*trusted=*/false);

    // Narrow, non-destructive repair: the patch is a merge delta that we were about to apply as a
    // replace. This happens when the model omits "mode" and the caller's guess (in the UI, a
    // keyword heuristic over the user's wording) goes the other way; the patch then references
    // ids that exist only in the live graph and is rejected as *UnknownNode.
    //
    // Deliberately one-directional. Re-reading a rejected patch as a *merge* can only preserve
    // nodes the user already had; the reverse — quietly turning a merge into a replace — would
    // wipe their patch, so it is never attempted. Nothing about the patch's content is altered:
    // validation itself is the arbiter, and this only runs when the model expressed no mode.
    if (!validation.ok && clearExisting && !hasExplicitMode(json)) {
        const auto asMerge = AIStateMapper::validatePatch(json, audioGraph, /*clearExisting=*/false,
                                                          /*trusted=*/false);
        if (asMerge.ok) {
            juce::Logger::writeToLog("applyPatch: reinterpreting mode-less patch as a merge (as a replace it was "
                                     "rejected: " +
                                     validation.message + ")");
            clearExisting = false;
            mergeMode = true;
            lastPatchModeRepaired = true;
            validation = asMerge;
        }
    }

    if (!validation.ok) {
        lastPatchErrorCode = validation.error;
        // One log per rejected patch — user-click frequency, so it does not violate the no-high-frequency
        // logging rule. In Debug builds this reaches AIChatComponent's console panel.
        lastPatchError = validation.message.isNotEmpty() ? validation.message : "Patch failed validation.";
        juce::Logger::writeToLog("applyPatch rejected (" + juce::String(mergeMode ? "merge" : "replace") +
                                 "): " + lastPatchError);
        return false;
    }

    // Structural gate: a patch can be schema-valid and still be useless — silent (nothing wired to
    // Audio Output) or dangling (an oscillator nobody connected). This is the same bar
    // Tools/AIEvalHarness measures every model against (PatchEval.h); enforcing it live gives the
    // model a chance to self-correct via the existing retry mechanism instead of silently handing
    // the user a patch that produces no sound. Checked on a scratch graph, never the live one, so a
    // rejection here never fires a listener notification or touches the undo stack (same
    // "validate BEFORE touching anything" contract as the schema check above).
    //
    // allParamsInRange is deliberately not gated on: PatchEval's own doc comment notes it's already
    // guaranteed by NormalisableRange clamping on every write path.
    {
        juce::AudioProcessorGraph scratch;
        synth::prepareGraphForPatchEval(scratch);

        bool beforeOk = true;
        if (!clearExisting) {
            // Merge mode: gate on REGRESSION, not absolute state. A delta is only responsible for
            // what it changes — an already-incomplete canvas (e.g. no Oscillator yet) merging in an
            // unrelated edit must not be rejected for a pre-existing gap the edit didn't cause.
            // Replaying the live graph's current state as trusted mirrors exactly what undo/redo's
            // own snapshot-restore does (see the "Preserve node identity" comment in
            // AIStateMapper::applyJSONToGraph), so ids line up with what the candidate patch
            // references.
            juce::var currentState = AIStateMapper::graphToJSON(audioGraph);
            AIStateMapper::applyJSONToGraph(currentState, scratch, /*clearExisting=*/true, /*trusted=*/true);
            const auto before = synth::evaluatePatch(scratch);
            beforeOk = before.hasAudioOutput && before.sourceReachesOutput;
        }

        AIStateMapper::applyJSONToGraph(json, scratch, clearExisting, /*trusted=*/false);
        const auto after = synth::evaluatePatch(scratch);
        const bool afterOk = after.hasAudioOutput && after.sourceReachesOutput;

        // Replace mode has no "before" to regress from — clearExisting leaves beforeOk at its
        // default true, so a from-scratch patch is always held to the unconditional bar.
        if (beforeOk && !afterOk) {
            lastPatchError = after.detail.isNotEmpty() ? after.detail : "patch produces no usable signal path";
            juce::Logger::writeToLog("applyPatch rejected (" + juce::String(mergeMode ? "merge" : "replace") +
                                     ", structural): " + lastPatchError);
            return false;
        }
    }

    // Notify listeners to detach graph-referencing UI BEFORE the graph is rebuilt (avoids a use-after-free
    // where a ScopeComponent timer reads a freed VisualBuffer once applyJSONToGraph clears old processors).
    // Undo and redo rebuild the graph exactly the same way, so the pair must fire around those too — they
    // are handed to the undoable action as its pre/post restore hooks. Skipping them on undo would leave the
    // graph editor holding stale ModuleComponents that reference freed VisualBuffers.
    juce::WeakReference<AIIntegrationService> weakThis(this);
    auto notifyAboutToApply = [weakThis] {
        if (auto* self = weakThis.get())
            self->listeners.call([](Listener& l) { l.aiPatchAboutToApply(); });
    };
    auto notifyApplied = [weakThis] {
        if (auto* self = weakThis.get())
            self->listeners.call([](Listener& l) { l.aiPatchApplied(); });
    };

    auto applyNow = [this, json, clearExisting, notifyAboutToApply, notifyApplied] {
        notifyAboutToApply();
        if (AIStateMapper::applyJSONToGraph(json, audioGraph, clearExisting)) {
            notifyApplied();
            return true;
        }
        lastPatchError = "Patch could not be applied to the graph.";
        juce::Logger::writeToLog("applyPatch failed during applyJSONToGraph: " + lastPatchError);
        return false;
    };

    // With an undo manager installed the apply is wrapped in a snapshot transaction so Cmd+Z restores the
    // user's previous patch; without one (e.g. tests that construct the service standalone) it applies directly.
    if (undoManager != nullptr) {
        return undoManager->recordAIPatch(audioGraph, mergeMode ? "AI merge" : "AI patch", applyNow, notifyAboutToApply,
                                          notifyApplied);
    }

    return applyNow();
}

void AIIntegrationService::applyPatchWithRetry(const juce::String& jsonString, bool mergeMode,
                                               PatchApplyCallback onComplete, PatchRetryCallback onRetry) {
    // Attempt 1 is the patch the caller already has in hand; retries are what follow.
    if (applyPatch(jsonString, mergeMode)) {
        if (onComplete)
            onComplete(true, {});
        return;
    }

    // Nothing to ask for a correction — report the rejection as-is rather than pretending to retry.
    if (provider == nullptr) {
        if (onComplete)
            onComplete(false, lastPatchError);
        return;
    }

    // Captured once, before any correction turn is appended to chatHistory — see
    // mostRecentUserRequest()'s doc comment for why this must not be re-derived per retry.
    requestPatchCorrection(1, mergeMode, mostRecentUserRequest(), std::move(onComplete), std::move(onRetry));
}

void AIIntegrationService::requestPatchCorrection(int failedAttempt, bool mergeMode,
                                                  const juce::String& originalRequest, PatchApplyCallback onComplete,
                                                  PatchRetryCallback onRetry) {
    const int totalAttempts = kMaxPatchRetries + 1;
    const juce::String error = lastPatchError;

    // The bound. Without it a model that keeps producing the same invalid patch would keep us
    // round-tripping forever while the user waits on a spinner.
    if (failedAttempt >= totalAttempts) {
        juce::Logger::writeToLog("applyPatch gave up after " + juce::String(failedAttempt) +
                                 " attempts, last error: " + error);
        if (onComplete)
            onComplete(false, error);
        return;
    }

    if (onRetry)
        onRetry({failedAttempt, totalAttempts, error});

    // One log per retry. Retries happen at user-click frequency, not per token or per validation
    // pass inside a loop, so this stays within the no-high-frequency-logging rule.
    juce::Logger::writeToLog("applyPatch retrying (attempt " + juce::String(failedAttempt + 1) + " of " +
                             juce::String(totalAttempts) + ") after: " + error);

    juce::WeakReference<AIIntegrationService> weakThis(this);
    sendMessage(
        buildCorrectionPrompt(originalRequest, error),
        [weakThis, failedAttempt, mergeMode, originalRequest, onComplete,
         onRetry](const AIProvider::AIResponse& response) {
            auto* self = weakThis.get();
            if (self == nullptr)
                return; // service destroyed mid-retry; nothing left to apply to

            if (!response.success) {
                if (onComplete)
                    onComplete(false, response.error.message);
                return;
            }

            if (self->applyPatch(response.content, mergeMode)) {
                if (onComplete)
                    onComplete(true, {});
                return;
            }

            self->requestPatchCorrection(failedAttempt + 1, mergeMode, originalRequest, onComplete, onRetry);
        },
        /*useStructuredOutput=*/true);
}

juce::String AIIntegrationService::buildCorrectionPrompt(const juce::String& originalRequest,
                                                         const juce::String& error) {
    // Naming the specific failure is the point of the retry. A bare "that didn't work, try again"
    // tends to reproduce the same mistake, because nothing told the model which part was wrong.
    //
    // Restating the original request matters just as much: RemoteProvider sends only this message,
    // not the conversation, so without it the model has no idea what the patch was even supposed to
    // be — verified live, it invents a generic fix referencing node ids that don't exist anywhere.
    return "Original request: " + originalRequest +
           "\n\nThe patch you returned for that request was rejected by the synthesizer and was NOT applied."
           "\n\nReason: " +
           error +
           "\n\nReturn a corrected patch for the ORIGINAL REQUEST above, as raw JSON, that fixes exactly this "
           "problem. Keep everything else about the patch the same. Use only module types and parameter choice "
           "strings that appear in the schema, and reference only node ids that exist in the current patch or "
           "that this patch itself creates.";
}

juce::String AIIntegrationService::mostRecentUserRequest() const {
    for (auto it = chatHistory.rbegin(); it != chatHistory.rend(); ++it)
        if (it->role == "user")
            return it->content;
    return {};
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
        "For any parameter listed as a 'Choice' in the module tables above, you MUST use one of the exact option "
        "strings shown there for that parameter. "
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
        "- **Oscillator has no `frequency` parameter**: pitch comes from the incoming MIDI note (or A4/440Hz if "
        "none is connected) offset by `octave`/`coarse`/`fine`. To pitch an Oscillator, set those three, not a "
        "frequency value.\n"
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
        "    { \"id\": 1, \"type\": \"Oscillator\", \"params\": { \"waveform\": \"Saw\", \"octave\": -1 } },\n"
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
        "```\n"
        "\n### WORKED EXAMPLES (COMPLETE, CORRECTLY-CONNECTED PATCHES):\n"
        "The patches below are full worked examples, not syntax fragments — study how every node is wired "
        "to something and every signal path actually reaches Audio Output. The single most common mistake "
        "is a structurally valid patch that is silent (no Audio Output) or dangling (a node nobody connected "
        "to anything); these examples show the complete, correct shape to copy.\n"
        "\n**Example 1 — from scratch, bass:**\n"
        "User request: \"Patch together a growling, punchy analog-style bass line\"\n"
        "```json\n"
        "{\n"
        "  \"nodes\": [\n"
        "    { \"id\": 101, \"type\": \"Oscillator\", \"params\": { \"waveform\": \"Saw\", \"octave\": -1 } },\n"
        "    { \"id\": 102, \"type\": \"Filter\", \"params\": { \"cutoff\": 800.0, \"resonance\": 0.4 } },\n"
        "    { \"id\": 103, \"type\": \"VCA\" },\n"
        "    { \"id\": 104, \"type\": \"Filter Env\", \"params\": { \"attack\": 0.01, \"decay\": 0.3, \"sustain\": "
        "0.2, \"release\": 0.2 } },\n"
        "    { \"id\": 105, \"type\": \"Amp Env\", \"params\": { \"attack\": 0.01, \"decay\": 0.15, \"sustain\": "
        "0.7, \"release\": 0.2 } },\n"
        "    { \"id\": 106, \"type\": \"Audio Output\" }\n"
        "  ],\n"
        "  \"connections\": [\n"
        "    { \"src\": 101, \"srcPort\": 0, \"dst\": 102, \"dstPort\": 0 },\n"
        "    { \"src\": 102, \"srcPort\": 0, \"dst\": 103, \"dstPort\": 0 },\n"
        "    { \"src\": 103, \"srcPort\": 0, \"dst\": 106, \"dstPort\": 0 }\n"
        "  ],\n"
        "  \"modulations\": [\n"
        "    { \"source\": 104, \"dest\": 102, \"destPort\": 1, \"amount\": 0.6 },\n"
        "    { \"source\": 105, \"dest\": 103, \"destPort\": 1, \"amount\": 1.0 }\n"
        "  ]\n"
        "}\n"
        "```\n"
        "Note every node is on the audio path (Oscillator -> Filter -> VCA -> Audio Output) and both "
        "envelopes drive real modulation targets (Filter's `Cutoff` is destPort 1, VCA's `CV` is destPort 1) "
        "rather than sitting unconnected.\n"
        "\n**Example 2 — from scratch, lead:**\n"
        "User request: \"Design a snappy square-wave pluck lead\"\n"
        "```json\n"
        "{\n"
        "  \"nodes\": [\n"
        "    { \"id\": 201, \"type\": \"Oscillator\", \"params\": { \"waveform\": \"Square\", \"octave\": 1 } },\n"
        "    { \"id\": 202, \"type\": \"Filter\", \"params\": { \"cutoff\": 3000.0, \"resonance\": 0.3 } },\n"
        "    { \"id\": 203, \"type\": \"VCA\" },\n"
        "    { \"id\": 204, \"type\": \"Filter Env\", \"params\": { \"attack\": 0.01, \"decay\": 0.2, \"sustain\": "
        "0.0, \"release\": 0.1 } },\n"
        "    { \"id\": 205, \"type\": \"Amp Env\", \"params\": { \"attack\": 0.01, \"decay\": 0.15, \"sustain\": "
        "0.0, \"release\": 0.1 } },\n"
        "    { \"id\": 206, \"type\": \"LFO\", \"params\": { \"mode\": false, \"rateHz\": 5.0, \"level\": 1.0 } },\n"
        "    { \"id\": 207, \"type\": \"Audio Output\" }\n"
        "  ],\n"
        "  \"connections\": [\n"
        "    { \"src\": 201, \"srcPort\": 0, \"dst\": 202, \"dstPort\": 0 },\n"
        "    { \"src\": 202, \"srcPort\": 0, \"dst\": 203, \"dstPort\": 0 },\n"
        "    { \"src\": 203, \"srcPort\": 0, \"dst\": 207, \"dstPort\": 0 }\n"
        "  ],\n"
        "  \"modulations\": [\n"
        "    { \"source\": 204, \"dest\": 202, \"destPort\": 1, \"amount\": 0.7 },\n"
        "    { \"source\": 205, \"dest\": 203, \"destPort\": 1, \"amount\": 1.0 },\n"
        "    { \"source\": 206, \"dest\": 201, \"destPort\": 4, \"amount\": 1.0 }\n"
        "  ]\n"
        "}\n"
        "```\n"
        "The LFO targets the Oscillator's `Fine` input (destPort 4) for a gentle pitch drift — a real, "
        "working CV target. Oscillator's own `Pitch` input (destPort 0) is a trap: it exists in the schema "
        "but mono oscillators ignore CV on it, so modulating it does nothing audible.\n"
        "\n**Example 3 — FX chain:**\n"
        "User request: \"Chain a saw-wave source through drive, chorus, and reverb for a wide, driven "
        "texture\"\n"
        "```json\n"
        "{\n"
        "  \"nodes\": [\n"
        "    { \"id\": 301, \"type\": \"Oscillator\", \"params\": { \"waveform\": \"Saw\" } },\n"
        "    { \"id\": 302, \"type\": \"Filter\", \"params\": { \"cutoff\": 1500.0 } },\n"
        "    { \"id\": 303, \"type\": \"VCA\" },\n"
        "    { \"id\": 304, \"type\": \"Distortion\", \"params\": { \"drive\": 3.0, \"mix\": 0.3 } },\n"
        "    { \"id\": 305, \"type\": \"Chorus\", \"params\": { \"rate\": 0.6, \"depth\": 0.4, \"mix\": 0.5 } },\n"
        "    { \"id\": 306, \"type\": \"Reverb\", \"params\": { \"roomSize\": 0.7, \"wet\": 0.4 } },\n"
        "    { \"id\": 307, \"type\": \"Audio Output\" }\n"
        "  ],\n"
        "  \"connections\": [\n"
        "    { \"src\": 301, \"srcPort\": 0, \"dst\": 302, \"dstPort\": 0 },\n"
        "    { \"src\": 302, \"srcPort\": 0, \"dst\": 303, \"dstPort\": 0 },\n"
        "    { \"src\": 303, \"srcPort\": 0, \"dst\": 304, \"dstPort\": 0 },\n"
        "    { \"src\": 304, \"srcPort\": 0, \"dst\": 305, \"dstPort\": 0 },\n"
        "    { \"src\": 305, \"srcPort\": 0, \"dst\": 306, \"dstPort\": 0 },\n"
        "    { \"src\": 306, \"srcPort\": 0, \"dst\": 307, \"dstPort\": 0 }\n"
        "  ]\n"
        "}\n"
        "```\n"
        "Every FX module sits IN LINE between the source and Audio Output — never bolted on to the side "
        "unconnected. FX modules like Delay and Reverb list CV modulation targets in the schema above, but "
        "their audio engines don't actually read that CV; automate their effect with `params` changes "
        "instead, not `modulations`.\n"
        "\n**Example 4 — merge mode, adding a node:**\n"
        "Existing patch: node 401 Oscillator (Saw) -> 402 Filter (cutoff 1200) -> 403 VCA -> 404 Audio "
        "Output.\n"
        "User request: \"Stack a second oscillator a fifth above the existing one, feeding into the same "
        "filter\"\n"
        "```json\n"
        "{\"mode\": \"merge\", \"nodes\": [{\"id\": 9401, \"type\": \"Oscillator\", \"params\": {\"waveform\": "
        "\"Saw\", \"coarse\": 7}}], "
        "\"connections\": [{\"src\": 9401, \"srcPort\": 0, \"dst\": 402, \"dstPort\": 0}]}\n"
        "```\n"
        "Only the new node and the one new connection are included — 401, 403, and 404 are untouched. Two "
        "sources connected to the same destination port sum there, which is how you mix oscillators without "
        "a dedicated mixer node.\n"
        "\n**Example 5 — merge mode, removing a node:**\n"
        "Existing patch: node 501 Oscillator (Square) -> 502 Distortion -> 503 Filter (cutoff 2500) -> 504 "
        "VCA -> 505 Audio Output.\n"
        "User request: \"Take out the distortion — it's too harsh — and connect straight through instead\"\n"
        "```json\n"
        "{\"mode\": \"merge\", \"remove\": [502], \"nodes\": [], "
        "\"connections\": [{\"src\": 501, \"srcPort\": 0, \"dst\": 503, \"dstPort\": 0}]}\n"
        "```\n"
        "Removing a node is not enough on its own — the chain must be rewired around the gap, or the patch "
        "ends up with 501 dangling and nothing reaching Audio Output.";

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
