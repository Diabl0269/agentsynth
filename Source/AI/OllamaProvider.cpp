#include "OllamaProvider.h"

namespace synth {

// Default constructor implementation
OllamaProvider::OllamaProvider(const juce::String& host)
    : Thread("OllamaProviderThread")
    , ollamaHost(host)
    , createStream([](const juce::URL& url, const juce::URL::InputStreamOptions& options) {
        return url.createInputStream(options);
    }) {}

void OllamaProvider::setModel(const juce::String& name) { currentModel = name; }
juce::String OllamaProvider::getCurrentModel() const { return currentModel; }

void OllamaProvider::fetchAvailableModels(std::function<void(const juce::StringArray& models, bool success)> callback) {
    // Run on a separate thread to avoid blocking the caller (the message thread).
    //
    // CRITICAL: we must NOT join() on the caller's thread. The old code joined the
    // previous discovery thread here, which blocked the UI for up to the connection
    // timeout (was 120s) when Ollama was unreachable. Instead we guard with an
    // atomic in-flight flag: if a discovery is already running, this call returns
    // immediately (and reports failure asynchronously) without ever blocking.
    bool expected = false;
    if (!discoveryInFlight.compare_exchange_strong(expected, true)) {
        // A discovery worker is already running. Do not block the caller and do not
        // launch a second worker. Report "no result yet" asynchronously so callers
        // relying on the callback still get one without us touching the UI synchronously.
        if (callback) {
            if (isTestMode) {
                callback(juce::StringArray{}, false);
            } else {
                juce::MessageManager::callAsync([callback]() { callback(juce::StringArray{}, false); });
            }
        }
        return;
    }

    // No worker was in flight. A previous worker may have finished but not yet been
    // joined; joining a finished thread returns immediately (no blocking). We only
    // ever reach here when discoveryInFlight just transitioned false->true, so there
    // is no concurrent worker to wait on.
    if (discoveryThread.joinable())
        discoveryThread.join();

    discoveryThread = std::thread([this, callback]() {
        // Clear the in-flight flag when this worker exits, no matter which path it
        // takes, so a subsequent fetch can run.
        struct InFlightGuard {
            std::atomic<bool>& flag;
            ~InFlightGuard() { flag.store(false); }
        } inFlightGuard{discoveryInFlight};

        DBG("AI Discovery STARTED: " + ollamaHost + "/api/tags");
        juce::URL url(ollamaHost + "/api/tags");
        juce::StringArray models;
        bool success = false;

        if (auto stream = createStream(
                url,
                juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress).withConnectionTimeoutMs(4000))) {
            juce::String responseText = stream->readEntireStreamAsString();
            // DBG("AI Discovery Response (before JSON parse): [" + responseText + "]"); // Potentially expensive
            // DBG("AI Discovery Response: " + responseText); // Redundant

            juce::var jsonResponse = juce::JSON::parse(responseText);

            DBG("AI Discovery Debug: jsonResponse.isObject() = " +
                juce::String(jsonResponse.isObject() ? "true" : "false"));
            if (jsonResponse.isObject()) {
                DBG("AI Discovery Debug: hasProperty(\"models\") = " +
                    juce::String(jsonResponse.getDynamicObject()->hasProperty("models") ? "true" : "false"));
                if (jsonResponse.getDynamicObject()->hasProperty("models")) {
                    auto modelsArray = jsonResponse["models"];
                    DBG("AI Discovery Debug: modelsArray.isArray() = " +
                        juce::String(modelsArray.isArray() ? "true" : "false"));
                    if (modelsArray.isArray()) {
                        for (int i = 0; i < modelsArray.size(); ++i) {
                            models.add(modelsArray[i]["name"].toString());
                        }
                        success = true;
                        DBG("AI Discovery: Found " + juce::String(models.size()) + " " +
                            (models.size() == 1 ? "model" : "models"));
                    } else {
                        DBG("AI Discovery Error: 'models' property is not an array");
                    }
                } else {
                    DBG("AI Discovery Error: JSON response does not have 'models' property");
                }
            } else {
                DBG("AI Discovery Error: JSON response is not an object");
            }
        } else {
            DBG("AI Discovery Error: Failed to open input stream for " + url.toString(true));
        }

        if (isTestMode) {
            if (callback)
                callback(models, success);
        } else {
            juce::MessageManager::callAsync([callback, models, success]() {
                if (callback)
                    callback(models, success);
            });
        }
    });
}

void OllamaProvider::run() {
    while (!threadShouldExit()) {
        Request currentRequest;

        {
            const juce::ScopedLock sl(queueLock);
            if (pendingRequests.empty()) {
                break;
            }
            currentRequest = pendingRequests.front();
            pendingRequests.erase(pendingRequests.begin());
        }

        processRequest(currentRequest);
    }
}

void OllamaProvider::processRequest(const Request& req) {
    // Delivers (responseText, success) through the same test-mode-aware channel used by
    // every exit path below, so the fail-fast path and the network paths stay identical.
    auto deliver = [this, &req](const juce::String& responseText, bool success) {
        if (isTestMode) {
            if (req.callback)
                req.callback(responseText, success);
        } else {
            juce::MessageManager::callAsync([req, responseText, success]() {
                if (req.callback)
                    req.callback(responseText, success);
            });
        }
    };

    // Fail fast: never hit the network with an empty model. Ollama rejects such requests
    // with HTTP 400 "model is required", which previously surfaced as a misleading
    // "Could not connect to Ollama" error even when the server was reachable.
    if (currentModel.isEmpty()) {
        deliver("Error: No Ollama model selected. Check that Ollama is running and that a model is available "
                "(ollama list).",
                false);
        return;
    }

    juce::URL url(ollamaHost + "/api/chat");

    // Build JSON body
    juce::DynamicObject::Ptr body = new juce::DynamicObject();
    body->setProperty("model", currentModel);
    body->setProperty("stream", false);

    juce::Array<juce::var> messages;
    for (const auto& msg : req.conversation) {
        juce::DynamicObject::Ptr m = new juce::DynamicObject();
        m->setProperty("role", msg.role);
        m->setProperty("content", msg.content);
        messages.add(juce::var(m.get()));
    }
    body->setProperty("messages", messages);

    if (!req.responseSchema.isVoid())
        body->setProperty("format", req.responseSchema);

    juce::String jsonString = juce::JSON::toString(juce::var(body.get()));

    juce::String responseText;
    bool success = false;
    int httpStatus = 0;

    if (auto stream = createStream(url.withPOSTData(jsonString),
                                   juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inPostData)
                                       .withConnectionTimeoutMs(120000)
                                       .withStatusCode(&httpStatus))) {
        responseText = stream->readEntireStreamAsString();
        // DBG("AI Chat Response (before JSON parse): [" + responseText + "]"); // Potentially expensive

        juce::var jsonResponse = juce::JSON::parse(responseText);
        if (jsonResponse.isObject()) {
            if (auto* obj = jsonResponse.getDynamicObject()) {
                if (obj->hasProperty("message")) {
                    auto msgObj = obj->getProperty("message");
                    if (msgObj.getDynamicObject()) {
                        responseText = msgObj.getDynamicObject()->getProperty("content").toString();
                        success = true;
                    }
                }
            }
        }
    } else if (httpStatus != 0) {
        responseText =
            "Error: Ollama at " + ollamaHost + " rejected the request (HTTP " + juce::String(httpStatus) + ").";
    } else {
        responseText = "Error: Could not connect to Ollama at " + ollamaHost;
    }

    deliver(responseText, success);
}

} // namespace synth