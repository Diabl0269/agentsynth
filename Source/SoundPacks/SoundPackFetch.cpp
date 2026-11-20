#include "SoundPacks/SoundPackFetch.h"
#include <array>
#include <memory>

namespace synth::soundpacks {

namespace {

constexpr int kConnectTimeoutMs = 15000;
constexpr int kMaxRedirects = 5;
constexpr int kHttpErrorFloor = 400;
constexpr int kHttpNotFound = 404;
constexpr size_t kChunkBytes = 1 << 16;

FetchResult fail(const juce::String& message) {
    FetchResult result;
    result.error = message;
    return result;
}

FetchResult cancelled() {
    FetchResult result;
    result.cancelled = true;
    return result;
}

FetchResult describeStatus(int status) {
    if (status == kHttpNotFound)
        return fail("The download was not found on the server (error 404).");
    return fail("The server could not send the download (error " + juce::String(status) + ").");
}

std::atomic<bool>& httpBlocked() {
    static std::atomic<bool> blocked{false};
    return blocked;
}

FetchResult dropped() {
    return fail("The connection dropped during the download. Check your internet connection and try again.");
}

// Copies the stream into the file; leaves the file for the caller to delete on failure.
FetchResult copyStream(juce::InputStream& in, juce::FileOutputStream& out, const FetchProgress& progress,
                       const std::atomic<bool>& cancel) {
    const juce::int64 total = in.getTotalLength();
    juce::int64 done = 0;
    std::array<char, kChunkBytes> buffer{};
    while (!in.isExhausted()) {
        if (cancel.load())
            return cancelled();
        const int got = in.read(buffer.data(), (int)buffer.size());
        if (got < 0)
            return dropped();
        if (got == 0)
            break;
        if (!out.write(buffer.data(), (size_t)got))
            return fail("The download could not be saved. The disk may be full.");
        done += got;
        progress(done, total);
    }
    out.flush();
    if (total > 0 && done != total)
        return dropped();
    FetchResult ok;
    ok.ok = true;
    return ok;
}

juce::String locationOf(const juce::StringPairArray& headers, const juce::String& base) {
    for (int i = 0; i < headers.size(); ++i) {
        if (!headers.getAllKeys()[i].equalsIgnoreCase("Location"))
            continue;
        const auto target = headers.getAllValues()[i].trim();
        // A relative Location resolves against the URL that answered.
        return target.containsIgnoreCase("://") ? target : juce::URL(base).getChildURL(target).toString(true);
    }
    return {};
}

bool isHttps(const juce::String& url) { return url.trim().startsWithIgnoreCase("https://"); }

// Redirects are followed here, one hop at a time, so a hop that leaves https (a downgrade to plain http) is refused
// instead of being followed by the platform's own redirect handling.
std::unique_ptr<juce::InputStream> openFollowingHttpsRedirects(const juce::String& startUrl, int& status,
                                                               FetchResult& refusal) {
    juce::String url = startUrl;
    for (int hop = 0; hop <= kMaxRedirects; ++hop) {
        if (!isHttps(url)) {
            refusal = fail("The download was refused because it is not served over a secure connection.");
            return nullptr;
        }
        juce::StringPairArray headers;
        status = 0;
        auto options = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                           .withConnectionTimeoutMs(kConnectTimeoutMs)
                           .withNumRedirectsToFollow(0)
                           .withResponseHeaders(&headers)
                           .withStatusCode(&status);
        auto in = juce::URL(url).createInputStream(options);
        const bool redirected = status >= 300 && status < 400;
        if (!redirected)
            return in;
        const auto next = locationOf(headers, url);
        if (next.isEmpty())
            return nullptr;
        url = next;
    }
    return nullptr;
}

FetchResult download(const juce::String& url, const juce::File& destination, const FetchProgress& progress,
                     const std::atomic<bool>& cancel) {
    int status = 0;
    FetchResult refusal;
    auto in = openFollowingHttpsRedirects(url, status, refusal);
    if (refusal.error.isNotEmpty())
        return refusal;
    if (cancel.load())
        return cancelled();
    if (in == nullptr)
        return status >= kHttpErrorFloor ? describeStatus(status)
                                         : fail("Couldn't reach the server. Check your internet connection.");
    if (status >= kHttpErrorFloor)
        return describeStatus(status);
    juce::FileOutputStream out(destination);
    if (!out.openedOk())
        return fail("The download could not be saved. The disk may be full or the folder is not writable.");
    out.setPosition(0);
    out.truncate();
    return copyStream(*in, out, progress, cancel);
}

} // namespace

void setHttpBlockedForTesting(bool blocked) { httpBlocked() = blocked; }

Fetcher makeHttpFetcher() {
    return [](const juce::String& url, const juce::File& destination, const FetchProgress& progress,
              const std::atomic<bool>& cancel) {
        const FetchResult result = httpBlocked().load()
                                       ? fail("Couldn't reach the server. Check your internet connection.")
                                       : download(url, destination, progress, cancel);
        if (!result.ok)
            destination.deleteFile();
        return result;
    };
}

} // namespace synth::soundpacks
