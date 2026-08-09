#include "AuthClient.h"

#ifndef _WIN32
#include <curl/curl.h>
#endif

namespace synth {

namespace {

// Auth endpoints are plain REST calls (device-code issuance, a token poll, a profile fetch), not
// an LLM generation call — nowhere near RemoteProvider's 240s ceiling. 15s is generous for a
// same-region service and still fails fast if the host is unreachable.
constexpr int kRequestTimeoutMs = 15000;

juce::String urlEncode(const juce::String& value) { return juce::URL::addEscapeChars(value, true); }

/** Joins `key=value` pairs with '&', URL-encoding each value (keys are always static ASCII
    literals in this file, so only values need escaping). */
juce::String formEncode(const std::vector<std::pair<juce::String, juce::String>>& params) {
    juce::String body;
    for (const auto& [key, value] : params) {
        if (body.isNotEmpty())
            body += "&";
        body += key + "=" + urlEncode(value);
    }
    return body;
}

#ifndef _WIN32
size_t curlWriteCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* out = static_cast<juce::String*>(userdata);
    *out += juce::String::fromUTF8(ptr, static_cast<int>(size * nmemb));
    return size * nmemb;
}

size_t curlHeaderCallback(char* buffer, size_t size, size_t nitems, void* userdata) {
    auto* headers = static_cast<juce::StringPairArray*>(userdata);
    const auto lineLength = size * nitems;
    juce::String line = juce::String::fromUTF8(buffer, static_cast<int>(lineLength)).trim();

    const int colon = line.indexOfChar(':');
    if (colon > 0) {
        const auto key = line.substring(0, colon).trim();
        const auto value = line.substring(colon + 1).trim();
        if (key.isNotEmpty())
            headers->set(key, value);
    }

    return lineLength;
}

int curlProgressCallback(void* clientp, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
    auto* cancelled = static_cast<const std::atomic<bool>*>(clientp);
    return (cancelled != nullptr && cancelled->load()) ? 1 : 0; // non-zero aborts the transfer
}

/** Real libcurl-backed HttpPerformer. See RemoteProvider.cpp's performHttpWithCurl for the
    identical shape this mirrors (progress callback for cancellation, header/body capture). */
AuthClient::HttpResult performHttpWithCurl(const juce::String& method, const juce::String& url,
                                           const juce::StringPairArray& requestHeaders, const juce::String& body,
                                           int timeoutMs, const std::atomic<bool>& cancelled) {
    static const bool globalInitDone = [] {
        curl_global_init(CURL_GLOBAL_DEFAULT);
        return true;
    }();
    juce::ignoreUnused(globalInitDone);

    AuthClient::HttpResult result;

    CURL* curl = curl_easy_init();
    if (curl == nullptr) {
        result.transportFailed = true;
        result.errorMessage = "Error: Could not initialize libcurl.";
        return result;
    }

    juce::String responseBody;
    juce::StringPairArray responseHeaders;

    curl_slist* headerList = nullptr;
    for (const auto& key : requestHeaders.getAllKeys()) {
        const auto headerLine = key + ": " + requestHeaders[key];
        headerList = curl_slist_append(headerList, headerLine.toRawUTF8());
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.toRawUTF8());
    if (method == "POST") {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.toRawUTF8());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.getNumBytesAsUTF8()));
    } else {
        curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    }
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headerList);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseBody);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, curlHeaderCallback);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &responseHeaders);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, static_cast<long>(timeoutMs));
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, curlProgressCallback);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &cancelled);

    const CURLcode res = curl_easy_perform(curl);

    long httpStatus = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpStatus);
    result.httpStatus = static_cast<int>(httpStatus);
    result.body = responseBody;
    result.headers = responseHeaders;

    if (res == CURLE_ABORTED_BY_CALLBACK) {
        result.transportFailed = true;
        result.errorMessage = "Request aborted (cancelled).";
    } else if (res == CURLE_OPERATION_TIMEDOUT) {
        result.timedOut = true;
        result.errorMessage = "Error: Request to " + url + " timed out.";
    } else if (res != CURLE_OK) {
        result.transportFailed = true;
        result.errorMessage = juce::String("Error: ") + curl_easy_strerror(res);
    }

    if (headerList != nullptr)
        curl_slist_free_all(headerList);
    curl_easy_cleanup(curl);

    return result;
}
#else
AuthClient::HttpResult performHttpUnavailable(const juce::String&, const juce::String&, const juce::StringPairArray&,
                                              const juce::String&, int, const std::atomic<bool>&) {
    AuthClient::HttpResult result;
    result.transportFailed = true;
    result.errorMessage = "Error: AuthClient is not available on Windows yet (no libcurl backend).";
    return result;
}
#endif

} // namespace

AuthClient::AuthClient(juce::String hostIn, juce::String clientIdIn)
    : host(std::move(hostIn))
    , clientId(std::move(clientIdIn))
#ifndef _WIN32
    , performHttp(performHttpWithCurl)
#else
    , performHttp(performHttpUnavailable)
#endif
{
}

AuthClient::AuthClient(juce::String hostIn, juce::String clientIdIn, HttpPerformer performer)
    : host(std::move(hostIn))
    , clientId(std::move(clientIdIn))
    , performHttp(std::move(performer)) {}

AuthClient::DeviceCodeResult AuthClient::requestDeviceCode(const std::atomic<bool>& cancelled) const {
    DeviceCodeResult result;

    juce::StringPairArray headers;
    headers.set("Content-Type", "application/x-www-form-urlencoded");

    const juce::String body = formEncode({{"client_id", clientId}});
    const auto http = performHttp("POST", host + "/v1/auth/device/code", headers, body, kRequestTimeoutMs, cancelled);

    if (http.transportFailed || http.timedOut) {
        result.transportError = http.errorMessage.isNotEmpty() ? http.errorMessage : "Error: request failed.";
        return result;
    }

    if (http.httpStatus != 200) {
        result.transportError = "Error: device code request failed (HTTP " + juce::String(http.httpStatus) + ").";
        return result;
    }

    // getDynamicObject() returns a raw pointer into the var's ReferenceCountedObjectPtr; the
    // parsed var must be kept alive (named, not a temporary) for as long as `obj` is used, or the
    // DynamicObject is destroyed out from under it the moment the parse expression ends.
    const auto parsed = juce::JSON::parse(http.body);
    auto* obj = parsed.getDynamicObject();
    if (obj == nullptr) {
        result.transportError = "Error: could not parse device code response.";
        return result;
    }

    result.deviceCode = obj->getProperty("device_code").toString();
    result.userCode = obj->getProperty("user_code").toString();
    result.verificationUri = obj->getProperty("verification_uri").toString();
    result.verificationUriComplete = obj->getProperty("verification_uri_complete").toString();
    result.expiresIn = static_cast<int>(obj->getProperty("expires_in"));
    result.interval = static_cast<int>(obj->getProperty("interval"));
    result.ok = true;
    return result;
}

AuthClient::TokenPollResult AuthClient::postToken(const juce::String& formBody,
                                                  const std::atomic<bool>& cancelled) const {
    TokenPollResult result;

    juce::StringPairArray headers;
    headers.set("Content-Type", "application/x-www-form-urlencoded");

    const auto http = performHttp("POST", host + "/v1/auth/token", headers, formBody, kRequestTimeoutMs, cancelled);

    if (http.transportFailed || http.timedOut) {
        result.transportError = http.errorMessage.isNotEmpty() ? http.errorMessage : "Error: request failed.";
        return result;
    }

    if (http.httpStatus == 200) {
        const auto parsed = juce::JSON::parse(http.body);
        auto* obj = parsed.getDynamicObject();
        if (obj == nullptr) {
            result.transportError = "Error: could not parse token response.";
            return result;
        }

        result.accessToken = obj->getProperty("access_token").toString();
        result.expiresIn = static_cast<int>(obj->getProperty("expires_in"));
        result.refreshToken = obj->getProperty("refresh_token").toString();
        result.ok = true;
        return result;
    }

    if (http.httpStatus == 400) {
        const auto parsed = juce::JSON::parse(http.body);
        auto* obj = parsed.getDynamicObject();
        if (obj != nullptr) {
            result.errorCode = obj->getProperty("error").toString();
            result.errorDescription = obj->getProperty("error_description").toString();
        } else {
            result.transportError = "Error: could not parse token error response.";
        }
        return result;
    }

    result.transportError = "Error: token request failed (HTTP " + juce::String(http.httpStatus) + ").";
    return result;
}

AuthClient::TokenPollResult AuthClient::pollDeviceToken(const juce::String& deviceCode,
                                                        const std::atomic<bool>& cancelled) const {
    const juce::String body = formEncode({{"grant_type", "urn:ietf:params:oauth:grant-type:device_code"},
                                          {"device_code", deviceCode},
                                          {"client_id", clientId}});
    return postToken(body, cancelled);
}

AuthClient::TokenPollResult AuthClient::refreshToken(const juce::String& refreshTokenValue,
                                                     const std::atomic<bool>& cancelled) const {
    const juce::String body =
        formEncode({{"grant_type", "refresh_token"}, {"refresh_token", refreshTokenValue}, {"client_id", clientId}});
    return postToken(body, cancelled);
}

AuthClient::MeResult AuthClient::fetchMe(const juce::String& accessToken, const std::atomic<bool>& cancelled) const {
    MeResult result;

    juce::StringPairArray headers;
    headers.set("Authorization", "Bearer " + accessToken);

    const auto http = performHttp("GET", host + "/v1/auth/me", headers, {}, kRequestTimeoutMs, cancelled);

    if (http.transportFailed || http.timedOut) {
        result.transportError = http.errorMessage.isNotEmpty() ? http.errorMessage : "Error: request failed.";
        return result;
    }

    if (http.httpStatus != 200) {
        result.transportError = "Error: /v1/auth/me failed (HTTP " + juce::String(http.httpStatus) + ").";
        return result;
    }

    const auto parsed = juce::JSON::parse(http.body);
    auto* obj = parsed.getDynamicObject();
    if (obj == nullptr) {
        result.transportError = "Error: could not parse /v1/auth/me response.";
        return result;
    }

    result.id = obj->getProperty("id").toString();
    // email/display_name may be JSON null; juce::var::toString() on a void/null var yields an
    // empty string, which is exactly the "treat as empty" behavior the spec calls for.
    result.email = obj->getProperty("email").toString();
    result.displayName = obj->getProperty("display_name").toString();
    result.ok = true;
    return result;
}

bool AuthClient::revoke(const juce::String& token, const std::atomic<bool>& cancelled) const {
    juce::StringPairArray headers;
    headers.set("Content-Type", "application/x-www-form-urlencoded");

    const juce::String body = formEncode({{"token", token}});
    const auto http = performHttp("POST", host + "/v1/auth/revoke", headers, body, kRequestTimeoutMs, cancelled);

    return !http.transportFailed && !http.timedOut && http.httpStatus == 200;
}

bool AuthClient::logout(const juce::String& accessToken, const std::atomic<bool>& cancelled) const {
    juce::StringPairArray headers;
    headers.set("Authorization", "Bearer " + accessToken);

    const auto http = performHttp("POST", host + "/v1/auth/logout", headers, {}, kRequestTimeoutMs, cancelled);

    return !http.transportFailed && !http.timedOut && http.httpStatus == 200;
}

} // namespace synth
