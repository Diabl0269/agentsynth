#include "SoundPacks/SoundPackFetch.h"
#include <array>

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

FetchResult download(const juce::String& url, const juce::File& destination, const FetchProgress& progress,
                     const std::atomic<bool>& cancel) {
    int status = 0;
    auto options = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                       .withConnectionTimeoutMs(kConnectTimeoutMs)
                       .withNumRedirectsToFollow(kMaxRedirects)
                       .withStatusCode(&status);
    auto in = juce::URL(url).createInputStream(options);
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
