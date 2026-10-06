#include "WinHttpTransport.h"

#if JUCE_WINDOWS

#include "../Branding.h"
#include "RawHeaderParser.h"
#include <chrono>
#include <string>
#include <thread>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>

#pragma comment(lib, "winhttp.lib")

namespace synth {

namespace {

constexpr DWORD kReadChunkBytes = 16 * 1024;
constexpr int kWatchdogPollMs = 20;

/** Closes its HINTERNET on destruction unless already released. */
class ScopedHandle {
public:
    ScopedHandle() = default;
    explicit ScopedHandle(HINTERNET h)
        : handle(h) {}
    ScopedHandle(const ScopedHandle&) = delete;
    ScopedHandle& operator=(const ScopedHandle&) = delete;
    ~ScopedHandle() {
        if (handle != nullptr)
            WinHttpCloseHandle(handle);
    }
    HINTERNET get() const { return handle; }
    explicit operator bool() const { return handle != nullptr; }

private:
    HINTERNET handle = nullptr;
};

/** Why the watchdog aborted the request, if it did. */
enum class AbortReason { None, Cancelled, TimedOut };

/**
 * A blocking WinHTTP call cannot see `cancelled` or a total deadline, so this thread watches both
 * and, when either trips, closes the request handle: closing a handle from another thread is
 * WinHTTP's way of failing a call blocked on it. The handle is owned through an atomic so exactly
 * one side (watchdog or request thread) ever closes it.
 */
class Watchdog {
public:
    Watchdog(std::atomic<HINTERNET>& requestHandle, const std::atomic<bool>& cancelledFlag, int timeoutMs)
        : request(requestHandle)
        , cancelled(cancelledFlag)
        , deadline(std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs))
        , worker([this] { run(); }) {}

    ~Watchdog() {
        finished.store(true);
        worker.join();
    }

    AbortReason reason() const { return abortReason.load(); }

private:
    void run() {
        while (!finished.load()) {
            AbortReason why = AbortReason::None;
            if (cancelled.load())
                why = AbortReason::Cancelled;
            else if (std::chrono::steady_clock::now() >= deadline)
                why = AbortReason::TimedOut;

            if (why != AbortReason::None) {
                abortReason.store(why);
                if (HINTERNET h = request.exchange(nullptr))
                    WinHttpCloseHandle(h);
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(kWatchdogPollMs));
        }
    }

    std::atomic<HINTERNET>& request;
    const std::atomic<bool>& cancelled;
    const std::chrono::steady_clock::time_point deadline;
    std::atomic<bool> finished{false};
    std::atomic<AbortReason> abortReason{AbortReason::None};
    std::thread worker; // declared last: starts after every other member is initialised
};

WinHttpResult failure(const juce::String& message) {
    WinHttpResult r;
    r.transportFailed = true;
    r.errorMessage = message;
    return r;
}

juce::String describeError(const char* step, DWORD code) {
    return juce::String("Error: WinHTTP ") + step + " failed (error " + juce::String(static_cast<int>(code)) + ").";
}

std::wstring toWide(const juce::String& s) { return std::wstring(s.toWideCharPointer()); }

/** "Name: value\r\n" for every request header, as one wide string (empty when there are none). */
std::wstring buildHeaderBlock(const juce::StringPairArray& requestHeaders) {
    juce::String block;
    for (const auto& key : requestHeaders.getAllKeys())
        block += key + ": " + requestHeaders[key] + "\r\n";
    return toWide(block);
}

/** Reads the status code and the raw header block of a received response. */
void readStatusAndHeaders(HINTERNET request, WinHttpResult& result) {
    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    if (WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX))
        result.httpStatus = static_cast<int>(status);

    DWORD rawSize = 0;
    WinHttpQueryHeaders(request, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX, WINHTTP_NO_OUTPUT_BUFFER,
                        &rawSize, WINHTTP_NO_HEADER_INDEX);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || rawSize == 0)
        return;

    std::wstring raw(rawSize / sizeof(wchar_t) + 1, L'\0');
    if (WinHttpQueryHeaders(request, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX, raw.data(), &rawSize,
                            WINHTTP_NO_HEADER_INDEX)) {
        raw.resize(rawSize / sizeof(wchar_t));
        result.headers = parseRawHttpHeaders(juce::String(raw.c_str()));
    }
}

/** Reads the whole response body. Returns the Win32 error of the failing read, or 0 on success. */
DWORD readBody(HINTERNET request, const std::atomic<bool>& cancelled, std::string& out) {
    std::string chunk(kReadChunkBytes, '\0');
    for (;;) {
        if (cancelled.load())
            return ERROR_WINHTTP_OPERATION_CANCELLED;
        DWORD read = 0;
        if (!WinHttpReadData(request, chunk.data(), kReadChunkBytes, &read))
            return GetLastError();
        if (read == 0)
            return 0;
        out.append(chunk.data(), read);
    }
}

/** Opens the system-proxy-aware session, falling back for Windows older than 8.1. */
HINTERNET openSession() {
    const auto agent = toWide(juce::String(synth::branding::kProductName));
    HINTERNET session = WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                                    WINHTTP_NO_PROXY_BYPASS, 0);
    if (session == nullptr)
        session = WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                              WINHTTP_NO_PROXY_BYPASS, 0);
    return session;
}

/** Fills the result from a request that has been sent: the response, or why it did not arrive. */
void finishRequest(HINTERNET request, const std::atomic<bool>& cancelled, DWORD sendError, WinHttpResult& result,
                   std::string& bodyBytes, DWORD& error) {
    error = sendError;
    if (error == 0 && !WinHttpReceiveResponse(request, nullptr))
        error = GetLastError();
    if (error != 0)
        return;
    readStatusAndHeaders(request, result);
    error = readBody(request, cancelled, bodyBytes);
}

} // namespace

WinHttpResult performWinHttpRequest(const juce::String& method, const juce::String& url,
                                    const juce::StringPairArray& requestHeaders, const juce::String& body,
                                    int timeoutMs, const std::atomic<bool>& cancelled) {
    if (cancelled.load())
        return failure("Request aborted (cancelled).");

    const auto wideUrl = toWide(url);
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwSchemeLength = parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength =
        static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(wideUrl.c_str(), 0, 0, &parts) || parts.lpszHostName == nullptr ||
        (parts.nScheme != INTERNET_SCHEME_HTTP && parts.nScheme != INTERNET_SCHEME_HTTPS))
        return failure("Error: Invalid request URL: " + url);

    const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
    // The object name is the path plus any query string; an empty path means "/".
    std::wstring object(parts.lpszUrlPath != nullptr ? parts.lpszUrlPath : L"",
                        parts.dwUrlPathLength + parts.dwExtraInfoLength);
    if (object.empty())
        object = L"/";

    ScopedHandle session(openSession());
    if (!session)
        return failure(describeError("WinHttpOpen", GetLastError()));

    // Per-step timeouts are only a backstop; the Watchdog enforces the real total budget.
    const int stepTimeout = timeoutMs > 0 ? timeoutMs : 1;
    WinHttpSetTimeouts(session.get(), stepTimeout, stepTimeout, stepTimeout, stepTimeout);

    ScopedHandle connection(WinHttpConnect(session.get(), host.c_str(), parts.nPort, 0));
    if (!connection)
        return failure(describeError("WinHttpConnect", GetLastError()));

    const DWORD openFlags = parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
    const auto wideMethod = toWide(method);
    std::atomic<HINTERNET> requestHandle{WinHttpOpenRequest(connection.get(), wideMethod.c_str(), object.c_str(),
                                                            nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                                            openFlags)};
    if (requestHandle.load() == nullptr)
        return failure(describeError("WinHttpOpenRequest", GetLastError()));

    WinHttpResult result;
    std::string bodyBytes;
    DWORD error = 0;
    AbortReason abortReason = AbortReason::None;
    {
        Watchdog watchdog(requestHandle, cancelled, timeoutMs);
        const HINTERNET request = requestHandle.load();

        DWORD disableRedirects = WINHTTP_DISABLE_REDIRECTS;
        WinHttpSetOption(request, WINHTTP_OPTION_DISABLE_FEATURE, &disableRedirects, sizeof(disableRedirects));

        const auto headerBlock = buildHeaderBlock(requestHeaders);
        const auto bodyLength = static_cast<DWORD>(body.getNumBytesAsUTF8());
        const bool sent =
            WinHttpSendRequest(request, headerBlock.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headerBlock.c_str(),
                               headerBlock.empty() ? 0 : static_cast<DWORD>(-1),
                               bodyLength > 0 ? const_cast<char*>(body.toRawUTF8()) : WINHTTP_NO_REQUEST_DATA,
                               bodyLength, bodyLength, 0) != FALSE;
        finishRequest(request, cancelled, sent ? 0 : GetLastError(), result, bodyBytes, error);
        abortReason = watchdog.reason();
    } // watchdog joined: from here on only this thread touches requestHandle

    if (HINTERNET leftover = requestHandle.exchange(nullptr))
        WinHttpCloseHandle(leftover);

    // A watchdog abort closes the handle under a blocked call, so the call's own error code is
    // incidental; the watchdog's reason is the truth.
    if (abortReason == AbortReason::Cancelled || error == ERROR_WINHTTP_OPERATION_CANCELLED)
        return failure("Request aborted (cancelled).");
    if (abortReason == AbortReason::TimedOut || error == ERROR_WINHTTP_TIMEOUT) {
        result.timedOut = true;
        result.errorMessage = "Error: Request to " + url + " timed out.";
        return result;
    }
    if (error != 0) {
        result = failure(describeError("request", error));
        return result;
    }

    result.body = juce::String::fromUTF8(bodyBytes.data(), static_cast<int>(bodyBytes.size()));
    return result;
}

} // namespace synth

#endif // JUCE_WINDOWS
